/**
 * @file    dev_display_p20.c
 * @brief   P20 模组派生类型 — 静态扫描、2 通道、R/G/B 分离
 *
 * 实现 dev_display_ops: prepare (pixel_map→hub75_buff) + scan (BSRR 查表输出)
 * g_bsrr 查表放在 CCMRAM 中，避免 Flash 读取和分支。
 */

#include "dev_display.h"

#include <string.h>
#include "initcall.h"

/* ================================================================
 *  P20 模组参数
 * ================================================================ */

#define P20_MODULE_ROWS         (2U)  /* 每行模块数 */
#define P20_MODULE_COLS         (2U)  /* 每列模块数 */
#define P20_MODULE_PIXEL_ROW    (24U) /* 单模块像素行数 */
#define P20_MODULE_PIXEL_COL    (12U) /* 单模块像素列数 */
#define P20_CHANNELS_PER_MODULE (2U)  /* 每模块通道数（R1G1B1 + R2G2B2） */
#define P20_SCAN_LINES          (1U)  /* 静态扫描 */

/* ---- 派生参数（由模组参数计算，勿手动修改） ---- */
#define P20_SCREEN_ROWS    (P20_MODULE_ROWS * P20_MODULE_PIXEL_ROW)
#define P20_SCREEN_COLS    (P20_MODULE_COLS * P20_MODULE_PIXEL_COL)
#define P20_PIXEL_MAP_SIZE (P20_SCREEN_ROWS * P20_SCREEN_COLS)
#define P20_HUB75_BUF_SIZE (P20_SCREEN_ROWS * P20_MODULE_COLS * 16U)
// #define P20_TOTAL_CHANNELS (P20_MODULE_COLS * P20_CHANNELS_PER_MODULE)
// #define P20_CHANNEL_PIXELS (P20_MODULE_PIXEL_ROW * P20_MODULE_ROWS * 8U)
// #define P20_SCAN_LINE_PX   (P20_CHANNEL_PIXELS / P20_SCAN_LINES)
#define P20_TOTAL_CHANNELS (8U)
#define P20_CHANNEL_PIXELS (192U)
#define P20_SCAN_LINE_PX   (192U)

/* ---- BSRR 预计算查表 ---- */
typedef struct {
    pl_hub75_bsrr_t r, g, b;
} p20_bsrr_t;

[[gnu::section(".ccmram")]] static p20_bsrr_t g_bsrr[P20_TOTAL_CHANNELS][8];

/* ---- P20 实例 ---- */
typedef struct {
    dev_display_t me;
} dev_display_p20_t;

[[gnu::section(".ccmram")]] static uint8_t p20_pixel_map[P20_PIXEL_MAP_SIZE];
[[gnu::section(".ccmram")]] static uint8_t p20_hub75_buff[P20_HUB75_BUF_SIZE];

static dev_display_p20_t g_p20 = {
    .me = {
        .ops                 = nullptr, /* 由 dev_display_p20_init 设置 */
        .module_rows         = P20_MODULE_PIXEL_ROW,
        .module_cols         = P20_MODULE_PIXEL_COL,
        .channels_per_module = P20_CHANNELS_PER_MODULE,
        .modules_per_row     = P20_MODULE_ROWS,
        .modules_per_col     = P20_MODULE_COLS,
        .scan_lines          = P20_SCAN_LINES,
        .screen_rows         = P20_SCREEN_ROWS,
        .screen_cols         = P20_SCREEN_COLS,
        .total_channels      = P20_TOTAL_CHANNELS,
        .channel_pixels      = P20_CHANNEL_PIXELS,
        .scan_line_pixels    = P20_SCAN_LINE_PX,
        .buffer_size         = P20_PIXEL_MAP_SIZE,
        .pixel_map           = p20_pixel_map,
        .hub75_buff          = p20_hub75_buff,
        .light_level         = 1,
    },
};

dev_display_t *dev_display_p20_get(void)
{
    return &g_p20.me;
}

/* ================================================================
 *  prepare: pixel_map → hub75_buff 像素重排
 *
 *  pixel_map[] 按行优先存储（y * screen_rows + x），
 *  hub75_buff[] 按"组"组织以匹配 HUB75 移位寄存器输入时序。
 *  此函数将像素值从逻辑坐标映射到硬件输出缓冲。
 * ================================================================ */

static void _p20_prepare(dev_display_t *dev)
{
    int32_t group_index = 0;
    int32_t group_row = 0, group_col = 0;
    int32_t pixel_col = 0, pixel_row = 0;

    for (int32_t linear = 0; linear < (int32_t)dev->buffer_size; linear++) {
        pixel_col = linear % (int32_t)dev->screen_rows; /* 行优先: x 先递增 */
        pixel_row = linear / (int32_t)dev->screen_rows;

        group_row   = pixel_row % 12 / 4 + (pixel_row % 12 / 4 + 1) / 4;
        group_col   = pixel_col % 24 / 4;
        group_index = (1 - group_row % 2) * 6 + group_row / 2 * 2 * 6 + group_col % 6;

        int32_t col = pixel_col % 24 % 4, row = pixel_row % 12 % 4;
        int32_t res = (col + (1 - row) * 4) * (1 - row / 2) + ((3 - col) + row * 4) * (row / 2);

        uint16_t offset = 0;
        if ((pixel_col < 24) && (pixel_row < 12))
            offset = 0 * 384;
        else if ((pixel_col >= 24) && (pixel_row < 12))
            offset = 1 * 384;
        else if ((pixel_col < 24) && (pixel_row >= 12))
            offset = 2 * 384;
        else if ((pixel_col >= 24) && (pixel_row >= 12))
            offset = 3 * 384;

        dev->hub75_buff[res + (group_index) * 16 + offset] = dev->pixel_map[linear];
    }
}

/* ================================================================
 *  scan: 逐像素输出 — BSRR 查表 + CLK 脉冲
 *
 *  hub75_buff[] 存储的是颜色索引 (0~7)，扫描时查 g_bsrr
 *  得到该通道、该颜色的 R/G/B 三组 {port, BSRR_val}，直接 flush 输出。
 * ================================================================ */

static inline void _p20_scan(dev_display_t *dev, uint8_t line)
{
    for (uint16_t pixel = 0; pixel < dev->scan_line_pixels; pixel++) {
        /* 当前像素在所有通道中的起始偏移 */
        uint16_t pixel_base = (uint16_t)line * dev->scan_line_pixels + pixel;

        /* 同一像素位置同时输出所有通道的颜色数据 */
        for (uint8_t ch = 0; ch < dev->total_channels; ch++) {
            uint8_t color    = dev->hub75_buff[pixel_base + ch * dev->channel_pixels];
            p20_bsrr_t *bsrr = &g_bsrr[ch][color];
            pl_hub75_bsrr_flush(&bsrr->r);
            pl_hub75_bsrr_flush(&bsrr->g);
            pl_hub75_bsrr_flush(&bsrr->b);
        }

        /* 锁存当前像素数据到移位寄存器 */
        pl_hub75_clock_pulse();
    }
}

/* ---- set_row: ABCD 行地址编码 (静态扫描无需切换行) ---- */
static void _p20_set_row(uint8_t row)
{
    pl_hub75_set_row(row);
}

/* ---- ops 虚表 ---- */
static const dev_display_ops_t p20_ops = {
    .prepare = _p20_prepare,
    .scan    = _p20_scan,
    .set_row = _p20_set_row,
};

/* ================================================================
 *  dev_display_p20_init: 预计算 BSRR 查表 + 绑定 ops
 *
 *  g_bsrr[ch][c] 存储通道 ch 在颜色 c (0~7) 时的 R/G/B 引脚输出值。
 *  颜色 c 的 bit0→R, bit1→G, bit2→B:
 *    - R 亮: (c & 1) != 0 → BSRR 置位（高电平）
 *    - R 灭: (c & 1) == 0 → BSRR 复位（低电平）
 *    - G 亮: (c & 2) != 0,  B 亮: (c & 4) != 0
 *
 *  预计算避免扫描热路径中的分支判断。
 * ================================================================ */

void dev_display_p20_init(void)
{
    g_p20.me.ops = &p20_ops;
    dev_display_register(&g_p20.me);

    for (uint8_t ch = 0; ch < g_p20.me.total_channels; ch++) {
        for (uint8_t color = 0; color < 8; color++) {
            /* R 通道: color bit0 决定亮灭 */
            g_bsrr[ch][color].r.port = g_hub75_pin_r[ch].port;
            g_bsrr[ch][color].r.val  = (color & 1) ? (uint32_t)g_hub75_pin_r[ch].pin        /* 置位: 输出高 */
                                                   : (uint32_t)g_hub75_pin_r[ch].pin << 16; /* 复位: 输出低 */

            /* G 通道: color bit1 决定亮灭 */
            g_bsrr[ch][color].g.port = g_hub75_pin_g[ch].port;
            g_bsrr[ch][color].g.val  = (color & 2) ? (uint32_t)g_hub75_pin_g[ch].pin
                                                   : (uint32_t)g_hub75_pin_g[ch].pin << 16;

            /* B 通道: color bit2 决定亮灭 */
            g_bsrr[ch][color].b.port = g_hub75_pin_b[ch].port;
            g_bsrr[ch][color].b.val  = (color & 4) ? (uint32_t)g_hub75_pin_b[ch].pin
                                                   : (uint32_t)g_hub75_pin_b[ch].pin << 16;
        }
    }
}
hw_dev_initcall(dev_display_p20_init);
