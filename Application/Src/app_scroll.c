/**
 * @file    app_scroll.c
 * @brief   通用动态滚动显示实现 — 专用 scroll_task + 2ms 节拍 + 逐像素裁剪
 *
 * 机制（详见 doc/01_显示系统/动态滚动显示实现记录.md）：
 *   - 每槽保存 矩形(x,y,w,h)/方向/颜色/字号/字型/步进间隔/文本（GBK ≤64B 静态拷贝，
 *     4 槽 × ~90B ≈ 372B 静态 SRAM，无 CCM 对象）；
 *   - 专用 scroll_task（首次 start 惰性创建，栈 256×4=1KB，ucHeap）基础节拍 2ms：
 *     全槽空闲时事件标志休眠（零 CPU），有活动槽时 2ms 超时节拍；
 *   - 每槽相位累积 acc_ms，每 step_ms（≥2ms，协议最小速度粒度 speed×2）前进 1px；
 *   - 渲染 = dev_display_fill 清矩形 → 逐字形 app_render_draw_glyph_clipped
 *     （逐像素裁剪到矩形+屏幕边界，字形部分越界平滑裁剪）；
 *   - 越过边界后循环（左/右滚动从另一侧重新进入，上/下同理），永不自动停
 *     （循环滚动语义，用户裁决 2026-09-07）；
 *   - 每拍读 W25 字库由 dev_w25qxx.c 的 SPI 互斥锁串行保护（sw_dev_initcall 创建）。
 *
 * stay_ms（停留时间）解析并保存于槽位，v1 忽略（语义待定，见 doc/13 §8）。
 *
 * 并发模型：槽位状态单写者（协议任务 start/stop）×单读者（scroll_task 节拍渲染），
 * 经 s_scroll_mutex 串行；渲染在持锁内完成。
 *
 * 停止语义（用户裁决 2026-09-09，原「停止即清行」）：
 *   - app_scroll_stop(slot) = 冻结：置槽位非活跃、不清行、不提交，像素停在
 *     当前位置静态显示（render 锁保证在途渲染完成后才返回，无半帧风险）；
 *   - app_scroll_stop_all / _nolock = 保持清行语义（0x81 清屏 / 0x85 静态显示
 *     依赖它清掉滚动内容），与冻结路径独立实现，不共用。
 *
 * ⑤ 渲染互斥（2026-09-08）：滚动渲染与安徽静态渲染（0x85 等）经 s_render_mutex
 * 串行，消除混合帧黑条——scroll_task 的「渲染+commit」整段持锁；安徽侧在
 * 「stop_all + 渲染 + commit」整段外持同一锁（内部用无锁 stop 变体，防重入死锁）。
 *
 * ④a 脏矩形提交（2026-09-08）：tick 结束用一次 commit_frame_rect（各活跃槽矩形
 * 并集包围盒）提交，scan_task 只重排脏行而非全屏；stop_all 清行提交同样用矩形。
 * 并集只含活跃槽——冻结槽不参与，pixel_map 中冻结内容天然保留。
 */

#include "app_scroll.h"

#include <string.h>
#include "cmsis_os2.h"
#include "dev_display.h"
#include "pl_task_guard.h"
#include "pl_task_static.h"

/* ---- 任务静态存储（栈 + TCB 落 CCMRAM，见 pl_task_static.h）----
 * scroll_task：惰性创建且**只创建一次**（_ensure_engine 以 s_scroll_task 判重），
 * 创建后永不退出；静态化后不再占 ucHeap（省 1144B），CCM 占 1124B。 */
PL_TASK_STATIC_STORAGE(scroll, 256);

/* ---- 槽位文本上限（GBK 字节）----
 * 64B = 32 汉字（FONT_16/24 费显单行均够；安徽 2026-09-09 起 FONT_24，
 * 32 汉字×24px=768px 水平满屏滚动，文本长度不因字号变短）；
 * SRAM 预算：4 槽 × 64B + 控制块 ≈ 372B（≤500B 红线）；
 * 链接若超 SRAM 再收紧 48/32B（见 doc/06-04 记账）。 */
#define SCROLL_TEXT_MAX (64U)

/* 基础节拍 2ms = 协议最小速度粒度（speed×2ms）；步进间隔最小 2ms（speed=0 防御） */
#define SCROLL_TICK_MS     (2U)
#define SCROLL_STEP_MIN_MS (2U)

/* 槽位控制块（静态 SRAM） */
typedef struct {
    bool active;                /* 是否滚动中 */
    scroll_dir_t dir;           /* 滚动方向 */
    uint16_t x, y, w, h;        /* 钳位后的行区域矩形 */
    display_color_t color;      /* 绘制颜色 */
    font_size_t font_size;      /* 字号 */
    font_type_t font_type;      /* 字型 */
    uint16_t step_ms;           /* 每像素步进间隔（毫秒，≥2） */
    uint16_t stay_ms;           /* 停留时间（v1 保存但忽略） */
    uint16_t acc_ms;            /* 节拍相位累积 */
    int16_t off;                /* 文本原点相对矩形原点的位移（可为负） */
    uint16_t extent_w;          /* 文本总像素宽（水平方向回绕边界） */
    uint16_t text_len;          /* 有效文本字节数 */
    uint8_t text[SCROLL_TEXT_MAX]; /* 文本拷贝（GBK） */
} scroll_slot_t;

static scroll_slot_t s_slots[SCROLL_SLOT_MAX];

static osEventFlagsId_t s_scroll_evt; /* 唤醒标志：start/stop 置位 */
static osMutexId_t s_scroll_mutex;    /* 槽位状态互斥（惰性创建，RTOS 后） */
static osMutexId_t s_render_mutex;    /* 渲染互斥（⑤，惰性创建）：滚动渲染×静态渲染串行 */
static osThreadId_t s_scroll_task;    /* 滚动任务（惰性创建） */

#define SCROLL_EVT_WAKE (1U << 0)

static void scroll_task(void *argument);

/* ---- 槽位锁（引擎未创建时无锁直通——仅 init 前/失败路径，无并发） ---- */
static inline void _lock(void)
{
    if (s_scroll_mutex)
        osMutexAcquire(s_scroll_mutex, osWaitForever);
}

static inline void _unlock(void)
{
    if (s_scroll_mutex)
        osMutexRelease(s_scroll_mutex);
}

/* GBK 双字节判定（与 app_render.c 内部 _is_gbk 口径一致） */
static bool _scroll_is_gbk(uint8_t high, uint8_t low)
{
    return (high >= 0x81 && high <= 0xFE) && (low >= 0x40 && low <= 0xFE && low != 0x7F);
}

/* ---- 引擎惰性创建（首个 start 时；调用方为 RTOS 任务上下文） ---- */
static bool _ensure_engine(void)
{
    if (s_scroll_task != nullptr)
        return true;

    s_scroll_evt = osEventFlagsNew(nullptr);
    if (s_scroll_evt == nullptr)
        return false;
    s_scroll_mutex = osMutexNew(nullptr);
    if (s_scroll_mutex == nullptr)
        return false;
    s_render_mutex = osMutexNew(nullptr);
    if (s_render_mutex == nullptr)
        return false;

    const osThreadAttr_t attr = {
        .name       = "scroll_task",
        .priority   = osPriorityNormal,
        PL_TASK_STATIC_ATTR(scroll, 256),
    };
    s_scroll_task = pl_task_create_checked(osThreadNew(scroll_task, nullptr, &attr), "scroll_task");
    return s_scroll_task != nullptr;
}

/* ---- 位移推进（单步 1px；越过边界后从另一侧重新进入，循环滚动） ----
 * 回绕目标 = 对侧边缘内 1px（2026-09-08 修订）：原回绕到文本整体不可见位置
 * （左/右：off=w / -extent_w；上/下：off=h / -font_size），渲染结果 = 该行区域
 * 整行黑一拍（1×step_ms，低速时可达数百毫秒）→ 周期内可见黑闪。
 * 现在回绕直落首可见位置，消除整行黑拍，循环滚动视觉连续。 */
static void _advance(scroll_slot_t *s)
{
    switch (s->dir) {
        case SCROLL_DIR_LEFT: /* 从右往左：位移递减，出左缘后从右缘重新进入 */
            s->off--;
            if (s->off <= -(int32_t)s->extent_w)
                s->off = (int16_t)(s->w - 1U); /* 首字符列 1px 露右缘 */
            break;
        case SCROLL_DIR_RIGHT: /* 从左往右：位移递增，出右缘后从左缘重新进入 */
            s->off++;
            if (s->off >= (int32_t)s->w)
                s->off = (int16_t)(1 - (int32_t)s->extent_w); /* 末字符列 1px 露左缘 */
            break;
        case SCROLL_DIR_UP: /* 从下往上：位移递减，出上缘后从下缘重新进入 */
            s->off--;
            if (s->off <= -(int32_t)s->font_size)
                s->off = (int16_t)(s->h - 1U); /* 首行 1px 露下缘 */
            break;
        case SCROLL_DIR_DOWN: /* 从上往下：位移递增，出下缘后从上缘重新进入 */
            s->off++;
            if (s->off >= (int32_t)s->h)
                s->off = (int16_t)(1 - (int32_t)s->font_size); /* 末行 1px 露上缘 */
            break;
        default:
            break;
    }
}

/* ---- 渲染单槽：清矩形 → 逐字形裁剪绘制（不提交帧，由 tick 统一提交） ---- */
static void _render_slot(scroll_slot_t *s)
{
    dev_display_t *d = dev_display_get();
    if (!d)
        return;

    dev_display_fill(d, s->x, s->y, s->w, s->h, COLOR_BLACK);

    bool horiz = (s->dir == SCROLL_DIR_LEFT || s->dir == SCROLL_DIR_RIGHT);

    /* 整行文本完全在矩形外 → 只清不画 */
    if (horiz) {
        if (s->off <= -(int32_t)s->extent_w || s->off >= (int32_t)s->w)
            return;
    } else {
        if (s->off <= -(int32_t)s->font_size || s->off >= (int32_t)s->h)
            return;
    }

    uint16_t pos = 0;
    uint16_t cur = 0; /* 字形沿滚动方向的累计起点（水平=横向、竖直=横向排布） */
    while (pos < s->text_len) {
        font_enc_t enc;
        uint8_t adv;
        if (s->text[pos] >= 0x20 && s->text[pos] <= 0x7F) {
            enc = FONT_ENC_ASCII;
            adv = 1;
        } else if (pos + 1 < s->text_len && _scroll_is_gbk(s->text[pos], s->text[pos + 1])) {
            enc = FONT_ENC_GBK;
            adv = 2;
        } else {
            pos++; /* 非法字节跳过（与 app_render 一致） */
            continue;
        }

        uint8_t gw = app_render_glyph_width_px(s->font_size, enc);
        if (horiz) {
            int32_t gx = (int32_t)s->off + cur;
            if (gx + gw <= 0) { /* 字形整体在矩形左缘之外 → 跳过（省 W25 读） */
                pos += adv;
                cur += gw;
                continue;
            }
            if (gx >= (int32_t)s->w)
                break; /* 后续字形均在右缘之外 → 提前结束 */
            app_render_draw_glyph_clipped((int16_t)s->x + (int16_t)gx, (int16_t)s->y,
                                          s->x, s->y, s->w, s->h,
                                          s->font_size, s->font_type, enc, &s->text[pos],
                                          s->color);
        } else {
            /* 竖直滚动：整行文本同享 y 位移，沿 x 方向排布 */
            if (cur >= s->w)
                break; /* 后续字形均在右缘之外 → 提前结束 */
            app_render_draw_glyph_clipped((int16_t)s->x + (int16_t)cur,
                                          (int16_t)s->y + s->off,
                                          s->x, s->y, s->w, s->h,
                                          s->font_size, s->font_type, enc, &s->text[pos],
                                          s->color);
        }
        pos += adv;
        cur += gw;
    }
}

/* ---- 渲染互斥（⑤）：引擎未创建时无锁直通（无滚动渲染 → 无并发） ---- */
void app_scroll_render_lock(void)
{
    if (s_render_mutex)
        osMutexAcquire(s_render_mutex, osWaitForever);
}

void app_scroll_render_unlock(void)
{
    if (s_render_mutex)
        osMutexRelease(s_render_mutex);
}

/* ---- 节拍处理：对已到步进间隔的活跃槽推进 1px 并渲染 ----
 * ④a/⑤：渲染+commit 整段持渲染互斥；各活跃槽矩形并集包围盒，
 * 整拍仅一次 commit_frame_rect（scan_task 只重排脏行）。 */
static void _scroll_tick(void)
{
    dev_display_t *d = dev_display_get();
    bool rendered = false;
    bool have_rect = false;
    uint16_t ux = 0, uy = 0, uw = 0, uh = 0;

    /* 锁序固定 render → slot：start 仅持 slot 不持 render，stop/stop_all 同为
     * render → slot，无反向获取路径 → 无死锁 */
    app_scroll_render_lock();

    _lock();
    for (uint8_t i = 0; i < SCROLL_SLOT_MAX; i++) {
        scroll_slot_t *s = &s_slots[i];
        if (!s->active)
            continue;
        s->acc_ms += SCROLL_TICK_MS;
        if (s->acc_ms < s->step_ms)
            continue;
        s->acc_ms -= s->step_ms; /* step_ms ≥ 2 = 单节拍，单步推进 */
        _advance(s);
        _render_slot(s);
        rendered = true;

        /* 并集包围盒（相邻合并不必精细；槽矩形已在 start 钳位到屏内） */
        if (!have_rect) {
            ux = s->x;
            uy = s->y;
            uw = s->w;
            uh = s->h;
            have_rect = true;
        } else {
            uint32_t x2  = (uint32_t)ux + uw;
            uint32_t y2  = (uint32_t)uy + uh;
            uint32_t sx2 = (uint32_t)s->x + s->w;
            uint32_t sy2 = (uint32_t)s->y + s->h;
            if (s->x < ux)
                ux = s->x;
            if (s->y < uy)
                uy = s->y;
            if (sx2 > x2)
                x2 = sx2;
            if (sy2 > y2)
                y2 = sy2;
            uw = (uint16_t)(x2 - ux);
            uh = (uint16_t)(y2 - uy);
        }
    }
    _unlock();

    /* 有渲染 → 提交一帧（脏矩形版：scan_task 只重排并集覆盖的行；
     * 并集无效属防御路径 → 全量提交回退） */
    if (rendered && d) {
        if (have_rect)
            dev_display_commit_frame_rect(d, ux, uy, uw, uh);
        else
            dev_display_commit_frame(d);
    }
    app_scroll_render_unlock();
}

/* ---- 滚动任务：空闲事件标志休眠；有活动槽 2ms 节拍 ---- */
static void scroll_task(void *argument)
{
    (void)argument;
    for (;;) {
        bool any = false;
        _lock();
        for (uint8_t i = 0; i < SCROLL_SLOT_MAX; i++) {
            if (s_slots[i].active) {
                any = true;
                break;
            }
        }
        _unlock();

        if (any) {
            /* 2ms 超时 → 节拍推进（超时返回后进入 _scroll_tick） */
            osEventFlagsWait(s_scroll_evt, SCROLL_EVT_WAKE, osFlagsWaitAny, SCROLL_TICK_MS);
            _scroll_tick();
        } else {
            /* 全空闲：休眠等 start/stop 置 WAKE 唤醒，零 CPU 占用 */
            osEventFlagsWait(s_scroll_evt, SCROLL_EVT_WAKE, osFlagsWaitAny, osWaitForever);
        }
    }
}

/* ================================================================
 *  公开 API
 * ================================================================ */

bool app_scroll_start(uint8_t slot,
                      uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      const uint8_t *text, uint16_t text_len,
                      scroll_dir_t dir, display_color_t color,
                      font_size_t font_size, font_type_t font_type,
                      uint16_t step_ms, uint16_t stay_ms)
{
    if (slot >= SCROLL_SLOT_MAX || !text || text_len == 0)
        return false;
    if (dir == SCROLL_DIR_NONE)
        return false;
    if (!font_size)
        font_size = FONT_16;

    dev_display_t *d = dev_display_get();
    if (!d)
        return false;

    /* 矩形钳位到实际屏幕（screen_rows=宽 / screen_cols=高，命名反直觉）
     * 起点越界 → 整区域不可见 → 冻结该槽返回失败（stop 语义=冻结，
     * 用户裁决 2026-09-09；与 dev_display_fill 口径一致） */
    if (x >= d->screen_rows || y >= d->screen_cols) {
        app_scroll_stop(slot);
        return false;
    }
    if ((uint32_t)x + w > d->screen_rows)
        w = d->screen_rows - x;
    if ((uint32_t)y + h > d->screen_cols)
        h = d->screen_cols - y;
    if (w == 0 || h == 0) {
        app_scroll_stop(slot);
        return false;
    }

    if (!_ensure_engine())
        return false;

    /* 文本截断至槽位上限（GBK 尾部可能切开，渲染层按非法字节跳过） */
    uint16_t n = (text_len > SCROLL_TEXT_MAX) ? SCROLL_TEXT_MAX : text_len;

    /* 文本总像素宽（水平方向回绕边界；竖直方向以字形高=font_size 为回绕边界） */
    uint16_t extent_w = 0;
    for (uint16_t pos = 0; pos < n;) {
        font_enc_t enc;
        uint8_t adv;
        if (text[pos] >= 0x20 && text[pos] <= 0x7F) {
            enc = FONT_ENC_ASCII;
            adv = 1;
        } else if (pos + 1 < n && _scroll_is_gbk(text[pos], text[pos + 1])) {
            enc = FONT_ENC_GBK;
            adv = 2;
        } else {
            pos++;
            continue;
        }
        extent_w += app_render_glyph_width_px(font_size, enc);
        pos += adv;
    }
    if (extent_w == 0)
        return false; /* 文本无有效字形 */

    if (step_ms < SCROLL_STEP_MIN_MS)
        step_ms = SCROLL_STEP_MIN_MS;

    _lock();
    scroll_slot_t *s = &s_slots[slot];
    s->active    = true;
    s->dir       = dir;
    s->x         = x;
    s->y         = y;
    s->w         = w;
    s->h         = h;
    s->color     = color;
    s->font_size = font_size;
    s->font_type = font_type;
    s->step_ms   = step_ms;
    s->stay_ms   = stay_ms;
    s->acc_ms    = 0;
    s->extent_w  = extent_w;
    s->text_len  = n;
    memcpy(s->text, text, n);
    /* 初始位移：文本从可见区外进入（LEFT 从右缘、RIGHT 从左缘、UP 从下缘、DOWN 从上缘） */
    switch (dir) {
        case SCROLL_DIR_LEFT:
            s->off = (int16_t)w;
            break;
        case SCROLL_DIR_RIGHT:
            s->off = -(int16_t)extent_w;
            break;
        case SCROLL_DIR_UP:
            s->off = (int16_t)h;
            break;
        default: /* SCROLL_DIR_DOWN */
            s->off = -(int16_t)font_size;
            break;
    }
    _unlock();

    osEventFlagsSet(s_scroll_evt, SCROLL_EVT_WAKE);
    return true;
}

/* ---- 停止单槽·清行变体（无锁）：置非活跃 + 清空该行矩形 + 矩形提交。
 * 供 stop_all/_nolock（0x81/0x85 清屏路径）复用——与 app_scroll_stop 的
 * 冻结语义独立实现（用户裁决 2026-09-09）。
 * 不持渲染互斥，由公共函数或持锁调用方包裹。锁序 render → slot，无死锁。 ---- */
static void _scroll_stop_clear_nolock(uint8_t slot)
{
    uint16_t x, y, w, h;
    bool was_active;

    _lock();
    scroll_slot_t *s = &s_slots[slot];
    was_active = s->active;
    x          = s->x;
    y          = s->y;
    w          = s->w;
    h          = s->h;
    s->active  = false;
    _unlock();

    if (!was_active)
        return;
    if (s_scroll_evt)
        osEventFlagsSet(s_scroll_evt, SCROLL_EVT_WAKE);

    /* 清空该行矩形（0x81/0x85 需清掉滚动内容）；
     * ④a 用矩形提交：scan_task 只重排该行 */
    dev_display_t *d = dev_display_get();
    if (d) {
        dev_display_fill(d, x, y, w, h, COLOR_BLACK);
        dev_display_commit_frame_rect(d, x, y, w, h);
    }
}

/* ---- 停止单槽·冻结变体（无锁）：置非活跃，不清行、不提交。
 * 屏幕保留最后一次已提交的完整帧（render 锁保证在途渲染完成后才返回，
 * 无半帧风险）。用户裁决 2026-09-09：运态停止帧 = 冻结（不清除）。 ---- */
static void _scroll_freeze_nolock(uint8_t slot)
{
    bool was_active;

    _lock();
    scroll_slot_t *s = &s_slots[slot];
    was_active = s->active;
    s->active  = false;
    _unlock();

    if (!was_active)
        return;
    if (s_scroll_evt)
        osEventFlagsSet(s_scroll_evt, SCROLL_EVT_WAKE);
}

void app_scroll_stop(uint8_t slot)
{
    if (slot >= SCROLL_SLOT_MAX)
        return;

    /* ⑤ 冻结不触碰像素；持渲染互斥等待在途渲染完成，返回后屏幕为完整帧 */
    app_scroll_render_lock();
    _scroll_freeze_nolock(slot);
    app_scroll_render_unlock();
}

void app_scroll_stop_all(void)
{
    app_scroll_render_lock();
    app_scroll_stop_all_nolock();
    app_scroll_render_unlock();
}

/* ---- 停止全部槽位·清行（无锁变体，⑤ 供持锁调用方：安徽 0x81/0x85 的
 * 「stop_all + 渲染 + commit」整段持渲染互斥时调用，防重入死锁） ---- */
void app_scroll_stop_all_nolock(void)
{
    for (uint8_t i = 0; i < SCROLL_SLOT_MAX; i++)
        _scroll_stop_clear_nolock(i);
}
