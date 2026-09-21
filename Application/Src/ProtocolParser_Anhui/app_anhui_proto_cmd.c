/**
 * @file    app_anhui_proto_cmd.c
 * @brief   安徽费显协议命令执行（渲染、GPIO、语音）
 *
 * 协议文档未定义应答 → 全部命令单向不回。
 * 动态显示（0x86~0x89 滚动）已接入通用滚动模块 app_scroll（2026-09-07）。
 * 录制语音（0x96/0x97）依赖语音板段号接口，当前占位（见 _anhui_exec_voice_seg）。
 */

#include "app_anhui_proto_cmd.h"
#include "app_dispatch.h"

#include <stdint.h>

#include "app_diag.h"
#include "app_render.h"
#include "app_scroll.h"
#include "app_light_sensor.h"
#include "dev_display.h"
#include "dev_io_ctrl.h"
#include "app_anhui_proto_voice.h"
#include "SEGGER_RTT.h"    /* 联调诊断（ANHUI_RTT_DIAG）；置 0 时该头无引用 */

/* 协议文档未定义显示颜色 → 默认红色（费额显示器常规红）；待上位机联调确认 */
#define ANHUI_DEFAULT_COLOR COLOR_RED

/**
 * @brief  执行清屏（0x81）。
 * @note   ⑤ 渲染互斥（2026-09-08）：「停止全部动态 + 清屏 + 提交」整段持
 *         app_scroll_render_lock，与 scroll_task 渲染串行，消除混合帧黑条；
 *         锁内 stop 用无锁变体 app_scroll_stop_all_nolock 防重入死锁。
 */
static void _anhui_exec_clear(void)
{
    dev_display_t *d = dev_display_get();
    if (!d)
        return;
    app_scroll_render_lock();
    app_scroll_stop_all_nolock(); /* 清屏同时终止全部动态显示（用户裁决 2026-09-07） */
    dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK);
    dev_display_commit_frame(d);
    app_scroll_render_unlock();
}

/**
 * @brief  执行显示点（0x82）/关闭点（0x83）。
 * @param  p     坐标参数。
 * @param  color 写入颜色（关闭点 = COLOR_BLACK）。
 * @note   坐标上限以模组驱动实际屏幕尺寸为准（screen_rows 宽/screen_cols 高，
 *         用户裁决 2026-09-04）：越界坐标由 dev_display_set_pixel 内部边界
 *         检查丢弃（执行但不落屏）。
 *         ⑤ 写 pixel_map + commit 整段持渲染互斥（与滚动渲染串行）。
 */
static void _anhui_exec_pixel(const anhui_point_t *p, display_color_t color)
{
    dev_display_t *d = dev_display_get();
    if (!d)
        return;
    app_scroll_render_lock();
    dev_display_set_pixel(d, p->x, p->y, color);
    dev_display_commit_frame(d);
    app_scroll_render_unlock();
}

/* ---- 联调诊断（0x85 帧级证据，2026-09-15）----
 * 每帧打印：数据区前两字节原始序（b0=X / b1=Y，直用像素）+ 解析结果 x/y
 *  + 文本前 4 字节 hex——现场一眼确认「上位机到底发的什么坐标」。
 * 实测证据（2026-09-15 RTT 采集）：上位机「第一/二/三/四行」四帧逐字节完全
 * 相同——b0=00 b1=00、文本仅 1 字节 0x31('1')：行号信息不在数据区坐标字节里，
 * 四帧都渲染在 (0,0) 是该帧语义下的正确行为（doc/13 §1.2）。
 * 后续联调判据：上位机若真发 (0,16)，屏幕必须从 y=16 起显示、诊断行 b1=10
 * 即实证。
 * **一键关闭**：ANHUI_RTT_DIAG 置 0（默认跟随总开关 APP_DIAG_BANNER，见
 * app_diag.h；也可单独覆盖 0/1）。关闭后本块消失，协议行为不变（只读打印）。 */
#ifndef ANHUI_RTT_DIAG
#define ANHUI_RTT_DIAG (APP_DIAG_BANNER)
#endif

/**
 * @brief  执行静态显示（0x85）。
 * @param  p  静态显示参数（X/Y + GBK 文本；x=data[0]、y=data[1] 均为直接像素
 *            坐标，2026-09-15 终裁，见 parse 层与 doc/13 §1.2）。
 * @note   坐标直用（2026-09-15 用户终裁）：显示位置是 (0,16) 就从 (0,16)
 *         开始渲染，固件不做任何行号换算——此前「row=Y/16 → pixel_y=row×24」
 *         与「data[0]/data[1] 对调」两轮口径均已撤销。
 *         按 FONT_24 渲染（用户裁决 2026-09-09，原 FONT_16），单行不换行、
 *         超宽截断（整字丢弃，非逐像素裁剪）；文本前不清行（文档未要求）。
 *         字形框越屏由 dev_display_draw_bitmap 按屏幕交集裁剪绘制可见部分。
 *         坐标上限以实际屏幕尺寸为准（用户裁决 2026-09-04）：起点越界
 *         整区域不可见不落屏，渲染宽度截断到屏宽（screen_rows - x）。
 *         ⑤ 渲染互斥（2026-09-08）：「停止全部动态 + 渲染 + 提交」整段持
 *         app_scroll_render_lock（app_render 内部提交帧），与滚动渲染串行；
 *         锁内 stop 用无锁变体防重入死锁。
 */
static void _anhui_exec_static_text(const anhui_static_text_t *p)
{
#if ANHUI_RTT_DIAG
    /* 帧级证据（见上方诊断块说明）：b0/b1 = 数据区前两字节原始序
     * （x=b0、y=b1 直用像素，无换算） */
    SEGGER_RTT_printf(0,
                      "[anhui] 0x85 b0=%02x b1=%02x -> x=%u y=%u len=%u "
                      "t=%02x %02x %02x %02x\n",
                      (unsigned)p->x, (unsigned)p->y,
                      (unsigned)p->x, (unsigned)p->y, (unsigned)p->text_len,
                      (unsigned)((p->text_len > 0U) ? p->text[0] : 0U),
                      (unsigned)((p->text_len > 1U) ? p->text[1] : 0U),
                      (unsigned)((p->text_len > 2U) ? p->text[2] : 0U),
                      (unsigned)((p->text_len > 3U) ? p->text[3] : 0U));
#endif

    app_scroll_render_lock();
    app_scroll_stop_all_nolock(); /* 静态显示终止全部动态显示（用户裁决 2026-09-07，越界帧同样终止） */

    dev_display_t *d = dev_display_get();
    if (d && p->x < d->screen_rows && p->y < d->screen_cols) {
        app_render(&(render_cfg_t){
            .type = RENDER_TEXT,
            .x    = p->x,
            .y    = p->y,
            .w    = (uint16_t)(d->screen_rows - p->x),
            .h    = FONT_24,
            .style = &(render_style_t){
                .h_align   = ALIGN_LEFT_UP,
                .v_align   = ALIGN_LEFT_UP,
                .word_wrap = false,
            },
            .color     = ANHUI_DEFAULT_COLOR,
            .text      = (const char *)p->text,
            .len       = p->text_len,
            .font_size = FONT_24,
            .font_type = FONT_ST,
            .text_enc  = FONT_ENC_GBK, /* 协议 ANSI = GBK，直通不转换 */
        });
    }
    /* 起点越界：整区域不可见，不落屏（但动态已停止，保持原语义） */
    app_scroll_render_unlock();
}

/**
 * @brief  执行动态显示（0x86~0x89）/动态停止。
 * @param  p  动态显示参数（row 0~3 对应命令 86~89）。
 *
 * 已接入通用滚动模块 app_scroll（2026-09-07，此前留空）。
 * 权威文档核对（2026-09-08，2026-S304 费显通信协议.doc 第 8/9 节）——以下均与
 * 文档一致：命令 86/87/88/89=第 1/2/3/4 行；数据长=运动模式至文本区；速度 1B
 * ×2ms；停留时间 2B ×100ms 大端（示例「00 10」=1.6s）；停止帧=数据长 4 全 0
 * （5A 01 86 04 00 00 00 00 00 A5）。文档未定义「运动模式」取值表与「停留」
 * 行为——mode 1~4 方向映射与循环滚动语义均为用户拍板约定，待联调校准（doc/13 §8）。
 *
 * 字号口径（用户裁决 2026-09-09，原 FONT_16）：动态显示改用 24 点阵——
 * 四行行首 Y = 00/18/30/48（row×24，协议文档 00/10/20/30 为 16 点阵口径，
 * 24 点阵行距按字号推导为假设，待联调确认，doc/13 §8）、行高 24（FONT_24）。
 *
 *   - 运动模式 → 方向映射（用户拍板约定：01=从右往左、02=从左往右、03=从下往上、
 *     04=从上往下；枚举值对齐模式字节，零转换）：
 *        1 → SCROLL_DIR_LEFT、2 → SCROLL_DIR_RIGHT、3 → SCROLL_DIR_UP、4 → SCROLL_DIR_DOWN；
 *   - mode==0 或全 0 帧（parse 已置 text 空）→ app_scroll_stop(row) 冻结该行——
 *     像素停在当前位置静态显示、不清除（用户裁决 2026-09-09，原「停止即清行」）；
 *   - 循环滚动：文本持续滚动，收到停止帧才停；stay_ms（×100ms）解析保存、v1 忽略
 *     （待定，见 doc/13 §8）；
 *   - 行区域：Y = row×24（24 点阵行首 00/18/30/48，假设待联调）、x=0、w=实际屏宽
 *     （dev_display_get()->screen_rows）、h=24（FONT_24），app_scroll 内部钳位；
 *   - 速度 speed×2ms → step_ms（最小 2ms）；文本 64B 截断（app_scroll 槽位上限）。
 */
static void _anhui_exec_scroll(const anhui_scroll_t *p)
{
    if (!p)
        return;

    /* 停止帧：mode=0（含全 0 帧 00 00 00 00，parse 已防御 text 置空）→ 冻结 */
    if (p->mode == 0 || p->text == nullptr || p->text_len == 0) {
        app_scroll_stop(p->row);
        return;
    }

    scroll_dir_t dir;
    switch (p->mode) {
        case 1:
            dir = SCROLL_DIR_LEFT; /* 从右往左 */
            break;
        case 2:
            dir = SCROLL_DIR_RIGHT; /* 从左往右 */
            break;
        case 3:
            dir = SCROLL_DIR_UP; /* 从下往上 */
            break;
        case 4:
            dir = SCROLL_DIR_DOWN; /* 从上往下 */
            break;
        default:
            return; /* 未知模式：保持该行现状（语义待确认，见 doc/13 §8） */
    }

    dev_display_t *d = dev_display_get();
    if (!d)
        return;

    /* 行区域：Y = row×24（24 点阵行首 00/18/30/48），x=0，w=屏宽，h=24 */
    app_scroll_start((uint8_t)p->row,
                     0, (uint16_t)(p->row * 24U),
                     d->screen_rows, 24U,
                     p->text, p->text_len,
                     dir, ANHUI_DEFAULT_COLOR,
                     FONT_24, FONT_ST,
                     (uint16_t)p->speed * 2U, p->stay_ms);
}

/**
 * @brief  执行设置亮度（0x92）。
 * @param  value  亮度参数两字节（大端，0~999）。
 * @note   用户裁决（2026-09-04）：0 = 恢复光敏自动调光（恢复光敏任务），
 *         1~999 均匀映射 8 档亮度 light_level 1..8（8 = OE 100% 常亮）：
 *             level = 1 + value*7/999
 *         底层 8 档已具备（DEV_DISPLAY_BRIGHTNESS_MAX=8，TIM4 PWM 1/8 粒度），
 *         映射结果恒落在 1..8 内，无需再钳制。
 */
static void _anhui_exec_brightness(uint16_t value)
{
    dev_display_t *d = dev_display_get();
    if (!d)
        return;

    if (value == 0) {
        /* 自动亮度：恢复光敏任务（此前可能被手动档挂起） */
        if (g_light_sensor_task_handle != nullptr)
            osThreadResume(g_light_sensor_task_handle);
        return;
    }

    if (g_light_sensor_task_handle != nullptr)
        osThreadSuspend(g_light_sensor_task_handle); /* 手动设定 → 关闭自动调光 */

    /* 1~999 均匀映射 8 档：value=1 → 1（最暗）、999 → 8（最亮），
     * 分子最大 6993，结果恒在 1..8 内，不会超 DEV_DISPLAY_BRIGHTNESS_MAX */
    uint8_t level = (uint8_t)(1U + ((uint32_t)value * 7U / 999U));
    dev_display_set_brightness(d, level);
}

/**
 * @brief  执行设置通行灯/报警器（0x94）。
 * @param  p  IO 控制参数（type：01=通行灯、02=报警器；param：00=灭/关、01=亮/开）。
 * @note   报警器映射黄闪灯（PD15）——硬件无独立报警器输出；待上位机联调确认。
 */
static void _anhui_exec_io_ctrl(const anhui_io_ctrl_t *p)
{
    if (p->type == 1) {
        dev_io_lane_light(p->param == 1);
    } else {
        dev_io_flash_light(p->param == 1);
    }
}

/**
 * @brief  执行播放声音（0x95）。
 * @param  p  语音参数（编号 01~15 + 音量 0~9 + 变长参数）。
 * @note   音量文档定义 0~9 共 10 级：解析层仍按协议接受该字节（字段保留），
 *         执行层忽略——语音板不支持协议调音量，不再调用 dev_rs232_voice_volume
 *         （用户裁决 2026-09-04）；编号 01~14 模板句构造见
 *         app_anhui_proto_voice.c；编号 15 参数区为全部播报内容（GBK 自由文本），
 *         经 dev_rs232_voice_play 直送语音板（232_2 = USART6 语音口，路径不变）。
 */
static void _anhui_exec_voice(const anhui_voice_t *p)
{
    /* 音量字节被忽略（见上），仅播报文本 */
    anhui_voice_play(p->voice_no, p->params, p->params_len);
}

/**
 * @brief  执行播放录制语音（0x96）/录制语音（0x97）。
 * @param  p  段号参数。
 *
 * TODO（占位）：语音板（dev_rs232_voice TTS，USART6）当前仅提供文本合成播报
 * （dev_rs232_voice_play）与音量接口，无「录制语音段号」播报/录制接口。
 * 0x96/0x97 段号已解析校验，待语音板协议支持段号播报/录制后接入。
 */
static void _anhui_exec_voice_seg(const anhui_voice_seg_t *p)
{
    (void)p;
}

/**
 * @brief  执行安徽协议命令。
 * @param  ch   当前通道（协议无应答，未使用）。
 * @param  cmd  解析后的命令结构体。
 */
void anhui_execute_cmd(channel_t *ch, const anhui_parsed_cmd_t *cmd)
{
    (void)ch;
    if (!cmd || cmd->sta != ANHUI_PARSE_OK)
        return;

    switch (cmd->cmd) {
        case ANHUI_PCMD_CLEAR:
            _anhui_exec_clear();
            break;
        case ANHUI_PCMD_PIXEL_ON:
            _anhui_exec_pixel(&cmd->p.point, ANHUI_DEFAULT_COLOR);
            break;
        case ANHUI_PCMD_PIXEL_OFF:
            _anhui_exec_pixel(&cmd->p.point, COLOR_BLACK);
            break;
        case ANHUI_PCMD_STATIC_TEXT:
            _anhui_exec_static_text(&cmd->p.static_text);
            break;
        case ANHUI_PCMD_SCROLL_1:
        case ANHUI_PCMD_SCROLL_2:
        case ANHUI_PCMD_SCROLL_3:
        case ANHUI_PCMD_SCROLL_4:
            _anhui_exec_scroll(&cmd->p.scroll);
            break;
        case ANHUI_PCMD_BRIGHTNESS:
            _anhui_exec_brightness(cmd->p.brightness.value);
            break;
        case ANHUI_PCMD_IO_CTRL:
            _anhui_exec_io_ctrl(&cmd->p.io_ctrl);
            break;
        case ANHUI_PCMD_VOICE:
            _anhui_exec_voice(&cmd->p.voice);
            break;
        case ANHUI_PCMD_VOICE_REC_PLAY:
        case ANHUI_PCMD_VOICE_RECORD:
            _anhui_exec_voice_seg(&cmd->p.voice_seg);
            break;
        default:
            break;
    }
}
