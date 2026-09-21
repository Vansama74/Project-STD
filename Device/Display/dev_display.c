/**
 * @file    dev_display.c
 * @brief   HUB75 显示设备 — 通用 scan_task 骨架 + ISR 回调
 *
 * 派生的模组类型（P16、P10、静态等）通过 dev_display_ops 注入差异。
 * scan_task 负责调度，不被任何其他任务抢占（osPriorityRealtime）。
 */

#include "dev_display.h"

#include <string.h>
#include "cmsis_os2.h"
#include "initcall.h"
#include "pl_tim.h"
#include "pl_task_guard.h"
#include "pl_task_static.h"

/* ---- 任务静态存储（栈 + TCB 落 CCMRAM，见 pl_task_static.h）----
 * scan_task：Realtime 优先级、启动期创建一次、永不退出。静态化后不再占 ucHeap
 * （省 1144B），CCM 占 1124B；CCM 零等待单周期访问，对扫屏热路径更友好。 */
PL_TASK_STATIC_STORAGE(scan, 256);

/* ---- 扫描任务事件 ---- */
static osEventFlagsId_t s_scan_evt;
static dev_display_t *s_active_display;
static uint8_t *s_frame_front;
static uint8_t *s_frame_back;
static volatile uint8_t *s_pending_frame;
static volatile uint8_t s_frame_ready;

/* ④a 脏矩形（2026-09-08）：commit_frame_rect 记录的待消费矩形。
 * 仅在 osKernelLock 保护下访问；未消费前多次提交合并为包围盒。 */
static uint16_t s_pending_rect_x, s_pending_rect_y, s_pending_rect_w, s_pending_rect_h;
static bool s_pending_rect_valid;

volatile uint32_t g_dev_display_commit_count;
volatile uint32_t g_dev_display_scan_count;

/* ---- 实例注册（由派生模组的 hw_dev_initcall 调用）---- */
void dev_display_register(dev_display_t *dev) { s_active_display = dev; }

dev_display_t *dev_display_get(void) { return s_active_display; }

void dev_display_commit_frame(dev_display_t *dev)
{
    if (!dev)
        return;

    osKernelLock();
    s_pending_frame      = dev->pixel_map;
    s_frame_ready        = 1U;
    s_pending_rect_valid = false; /* 全屏语义：矩形失效 → prepare 全量重排 */
    g_dev_display_commit_count++;
    osKernelUnlock();
}

void dev_display_commit_frame_rect(dev_display_t *dev, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (!dev || w == 0 || h == 0)
        return;

    /* 矩形钳位到屏幕（screen_rows=宽 / screen_cols=高）；整体越界 → 不可见，不提交 */
    if (x >= dev->screen_rows || y >= dev->screen_cols)
        return;
    if ((uint32_t)x + w > dev->screen_rows)
        w = (uint16_t)(dev->screen_rows - x);
    if ((uint32_t)y + h > dev->screen_cols)
        h = (uint16_t)(dev->screen_cols - y);
    if (w == 0 || h == 0)
        return;

    osKernelLock();
    if (s_frame_ready != 0U && !s_pending_rect_valid) {
        /* 已有未消费的全屏提交：保持全屏语义（全量 prepare 覆盖一切，新矩形无增量） */
    } else if (s_frame_ready != 0U) {
        /* 与未消费的脏矩形合并为包围盒：连续多槽提交只消费最后一帧，
         * 不合并会丢掉中间清行的 prepare（残影）。 */
        uint32_t x2  = (uint32_t)s_pending_rect_x + s_pending_rect_w;
        uint32_t y2  = (uint32_t)s_pending_rect_y + s_pending_rect_h;
        uint32_t nx2 = (uint32_t)x + w;
        uint32_t ny2 = (uint32_t)y + h;
        uint16_t nx  = (s_pending_rect_x < x) ? s_pending_rect_x : x;
        uint16_t ny  = (s_pending_rect_y < y) ? s_pending_rect_y : y;
        s_pending_rect_x = nx;
        s_pending_rect_y = ny;
        s_pending_rect_w = (uint16_t)(((x2 > nx2) ? x2 : nx2) - nx);
        s_pending_rect_h = (uint16_t)(((y2 > ny2) ? y2 : ny2) - ny);
    } else {
        s_pending_rect_x = x;
        s_pending_rect_y = y;
        s_pending_rect_w = w;
        s_pending_rect_h = h;
        s_pending_rect_valid = true;
    }
    s_pending_frame = dev->pixel_map;
    s_frame_ready   = 1U;
    g_dev_display_commit_count++;
    osKernelUnlock();
}

/* ---- TIM 周期回调（前向声明，实现在文件末尾）---- */
static void _on_tim3_period(void);
static void _on_tim4_period(void);

/* ---- 硬件初始化（所有模组通用）---- */
void dev_display_init(void)
{
    pl_tim_dbg_freeze(pl_tim_get_handle(PL_TIM3));
    pl_tim_dbg_freeze(pl_tim_get_handle(PL_TIM4));
    pl_hub75_init();
    pl_tim_register_period_cb(PL_TIM3, _on_tim3_period);
    pl_tim_register_period_cb(PL_TIM4, _on_tim4_period);
}
hw_dev_initcall(dev_display_init);

/* ---- 扫描任务骨架 ---- */
static void scan_task(void *arg)
{
    dev_display_t *dev = (dev_display_t *)arg;
    static uint8_t scan_line;

    pl_tim_start_it(pl_tim_get_handle(PL_TIM3));
    pl_tim_start_it(pl_tim_get_handle(PL_TIM4));

    s_frame_front = dev->pixel_map;
    s_frame_back  = dev->pixel_map;

    for (;;) {
        osEventFlagsWait(s_scan_evt, 0x01, osFlagsWaitAny, osWaitForever);

        /* 帧提交 → 预计算（off critical path） */
        osKernelLock();
        bool frame_ready = s_frame_ready != 0U;
        bool rect_valid  = false;
        uint16_t rect_x = 0, rect_y = 0, rect_w = 0, rect_h = 0;
        if (frame_ready) {
            s_frame_front = (uint8_t *)s_pending_frame;
            s_frame_back  = dev->hub75_buff;
            /* ④a：把待消费脏矩形取出（锁内），随后经 dev 字段传给 prepare */
            rect_valid       = s_pending_rect_valid;
            rect_x           = s_pending_rect_x;
            rect_y           = s_pending_rect_y;
            rect_w           = s_pending_rect_w;
            rect_h           = s_pending_rect_h;
            s_pending_rect_valid = false;
            s_frame_ready    = 0U;
        }
        osKernelUnlock();

        if (frame_ready) {
            dev->dirty = false;
            /* prepare 签名不变，脏矩形经 dev 字段传递（仅 scan_task 写，
             * prepare 在 scan_task 上下文读取，无并发）；返回后复位 */
            dev->dirty_rect_valid = rect_valid;
            dev->dirty_rect_x     = rect_x;
            dev->dirty_rect_y     = rect_y;
            dev->dirty_rect_w     = rect_w;
            dev->dirty_rect_h     = rect_h;
            if (dev->ops->prepare)
                dev->ops->prepare(dev);
            dev->dirty_rect_valid = false;
        }

        /* 模组专用扫描输出 */
        g_dev_display_scan_count++;
        dev->ops->scan(dev, scan_line);

        /* OE/LAT 原子窗口（所有模组通用） */
        osKernelLock();
        pl_tim_irq_disable(TIM4_IRQn);
        pl_hub75_oe_set(true);
        if (dev->ops->set_row)
            dev->ops->set_row(scan_line);
        pl_hub75_latch_pulse();
        pl_tim_irq_enable(TIM4_IRQn);
        osKernelUnlock();

        scan_line = (scan_line + 1) % dev->scan_lines;
    }
}

/* ---- 软件初始化（创建事件 + 扫描任务）---- */
void dev_display_start(void)
{
    s_scan_evt = osEventFlagsNew(NULL);

    dev_display_t *dev = s_active_display;

    dev->dirty = true;

    const osThreadAttr_t attr = {
        .name       = "scan_task",
        .priority   = osPriorityRealtime,
        PL_TASK_STATIC_ATTR(scan, 256),
    };
    pl_task_create_checked(osThreadNew(scan_task, dev, &attr), "scan_task");
}
sw_dev_initcall(dev_display_start);

/* ---- 通用像素操作 ---- */
void dev_display_set_pixel(dev_display_t *dev, uint16_t x, uint16_t y, display_color_t color)
{
    if (x < dev->screen_rows && y < dev->screen_cols) {
        dev->pixel_map[y * dev->screen_rows + x] = (uint8_t)color;
        dev->dirty                               = true;
    }
}

void dev_display_set_brightness(dev_display_t *dev, uint8_t level)
{
    if (level > DEV_DISPLAY_BRIGHTNESS_MAX) level = DEV_DISPLAY_BRIGHTNESS_MAX;
    dev->light_level = level;
}

void dev_display_fill(dev_display_t *dev, uint16_t x, uint16_t y, uint16_t w, uint16_t h, display_color_t color)
{
    /* 起点越界：整区域不可见，直接丢弃。
     * 必须先判起点再截断——否则 screen_rows - x / screen_cols - y 无符号下溢成巨值，
     * memset/行循环写穿 pixel_map（CCMRAM）导致 HardFault。 */
    if (x >= dev->screen_rows || y >= dev->screen_cols)
        return;
    /* 截断判断用 32 位运算：右对齐下溢 cur_x≈0xFFF0 时，x+w 的 uint16 加法回绕成
     * 小值绕过判界，改为 (uint32_t)x + w 后正确识别越界并截断 */
    if ((uint32_t)x + w > dev->screen_rows) w = dev->screen_rows - x;
    if ((uint32_t)y + h > dev->screen_cols) h = dev->screen_cols - y;

    for (uint16_t row = 0; row < h; row++)
        memset(&dev->pixel_map[(y + row) * dev->screen_rows + x], (uint8_t)color, w);
    dev->dirty = true;
}

void dev_display_draw_bitmap(dev_display_t *dev, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *bitmap, display_color_t color)
{
    /* 起点越界早退（与 fill 对齐）：x+w / y+h 为 uint16 加法，
     * 起点已回绕出屏时 32 位判界也无法救回，直接丢弃整区域 */
    if (x >= dev->screen_rows || y >= dev->screen_cols)
        return;

    /* 越界裁剪绘制（2026-09-14，用户裁决）：位图矩形与屏幕取交集，只画可见部分——
     * 字形框（字号 × 字号）高于可用屏高时画出其可见部分，而不是整块丢弃
     * （贵州治超 0x20 在单模组 32×16 台架需 24/32 点阵露出上半部分；224×64 整机
     * 字形全在屏内，走等价快路径、现有表现零变化）。完全无交集（w/h=0）不落笔。
     *
     * 位图布局 MSB-first 行打包：第 row 行占 row_bytes=(w+7)/8 字节，
     * bit(col) = bitmap[row*row_bytes + col/8] & (0x80 >> (col%8))。
     * 起点 x/y 为无符号且 ≥0 → 裁剪只可能发生在右/下边界，行首与行内 bit 偏移
     * 恒从 0 起，位序不会错位（无需跳行首/行尾残位）。
     * vis_w==w 且 vis_h==h（完全在屏内，常见路径）时本循环与旧快路径逐像素等价
     * （同源地址、同目的地址、同位序，指针寻址无额外开销）。 */
    uint16_t vis_w = ((uint32_t)x + w <= dev->screen_rows) ? w : (uint16_t)(dev->screen_rows - x);
    uint16_t vis_h = ((uint32_t)y + h <= dev->screen_cols) ? h : (uint16_t)(dev->screen_cols - y);
    if (vis_w == 0U || vis_h == 0U)
        return;

    uint16_t row_bytes = (w + 7) / 8;
    for (uint16_t row = 0; row < vis_h; row++) {
        const uint8_t *src = &bitmap[(uint32_t)row * row_bytes]; /* 被裁掉的底部行不再读取 */
        /* 索引中间值用 uint32 防 (y+row)*rows 的 uint16 回绕；交集保证终值在 pixel_map 内 */
        uint8_t *dst = &dev->pixel_map[(uint32_t)(y + row) * dev->screen_rows + x];
        for (uint16_t col = 0; col < vis_w; col++) {
            if (src[col / 8] & (0x80 >> (col % 8)))
                dst[col] = (uint8_t)color;
        }
    }
    dev->dirty = true;
}

/* ---- TIM 周期回调（通过 pl_tim_register_period_cb 注册到 Platform 层）---- */

static void _on_tim3_period(void)
{
    osEventFlagsSet(s_scan_evt, 0x01);
}

static void _on_tim4_period(void)
{
    dev_display_t *dev = dev_display_get();
    static uint8_t pwm_cnt;

    pl_hub75_oe_set(pwm_cnt >= dev->light_level);
    pwm_cnt = (pwm_cnt + 1) & 7;
}
