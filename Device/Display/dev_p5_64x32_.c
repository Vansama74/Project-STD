/**
 * @file    dev_p5_64x32_.c
 * @brief   P5 64x32 模组派生类型 — 1/8 扫描、2 通道、R/G/B 分离
 *
 * 实现 dev_display_ops: prepare (pixel_map→hub75_buff) + scan (BSRR 查表输出)
 * s_bsrr_table 查表放在 CCMRAM 中，避免 Flash 读取和分支。
 *
 * 单模块口径 1×1（64×32）；hub75_buff 链段口径与 1-543（同几何 P5 模组）一致：
 * 每扫描线每通道 128 字节，前 64 字节 = 高行带（行 L+8），后 64 字节 = 低行带（行 L）。
 */

#include "dev_display.h"

#include <string.h>
#include "initcall.h"

/* ================================================================
 *  P5 模组参数
 * ================================================================ */

#define MODULE_CODE         "P5-64X32" /* 料号未提供：暂用几何标识（开机画面 MD: 行使用） */
#define MODULE_ROWS         (1U)  /* 每行模块数 */
#define MODULE_COLS         (1U)  /* 每列模块数 */
#define MODULE_PIXEL_ROW    (64U) /* 单模块像素行数 */
#define MODULE_PIXEL_COL    (32U) /* 单模块像素列数 */
#define CHANNELS_PER_MODULE (2U)  /* 每模块通道数（R1G1B1 + R2G2B2） */
#define SCAN_LINES          (8U)  /* 1/8 扫描 */

/* ---- 派生参数（由模组参数计算，勿手动修改） ---- */
#define SCREEN_ROWS    (MODULE_ROWS * MODULE_PIXEL_ROW)
#define SCREEN_COLS    (MODULE_COLS * MODULE_PIXEL_COL)
#define BUFFER_SIZE    (SCREEN_ROWS * SCREEN_COLS)
#define TOTAL_CHANNELS (MODULE_COLS * CHANNELS_PER_MODULE)
#define CHANNEL_PIXELS (MODULE_PIXEL_ROW * MODULE_PIXEL_COL * MODULE_ROWS / CHANNELS_PER_MODULE)
#define SCAN_LINE_PX   (CHANNEL_PIXELS / SCAN_LINES)
#define ROWS_PER_CHANNEL (MODULE_PIXEL_COL / CHANNELS_PER_MODULE) /* 每通道覆盖的行数 */

#define GROUP_SIZE     (SCAN_LINE_PX / MODULE_ROWS)

/* ---- BSRR 预计算查表 ---- */
typedef struct {
    pl_hub75_bsrr_t r, g, b;
} bsrr_t;

[[gnu::section(".ccmram")]] static bsrr_t s_bsrr_table[TOTAL_CHANNELS][8];

/* ---- P5 实例 ---- */
typedef struct {
    dev_display_t base;
} dev_p5_64x32_t;

[[gnu::section(".ccmram")]] static uint8_t s_pixel_map[BUFFER_SIZE];
[[gnu::section(".ccmram")]] static uint8_t s_hub75_buff[BUFFER_SIZE];

static dev_p5_64x32_t g_p5_64x32 = {
    .base = {
        .ops                  = nullptr, /* 由 dev_p5_64x32_init 设置 */
        .module_rows          = MODULE_PIXEL_ROW,
        .module_cols          = MODULE_PIXEL_COL,
        .channels_per_module  = CHANNELS_PER_MODULE,
        .modules_per_row      = MODULE_ROWS,
        .modules_per_col      = MODULE_COLS,
        .scan_lines           = SCAN_LINES,
        .screen_rows          = SCREEN_ROWS,
        .screen_cols          = SCREEN_COLS,
        .total_channels       = TOTAL_CHANNELS,
        .channel_pixels       = CHANNEL_PIXELS,
        .scan_line_pixels     = SCAN_LINE_PX,
        .buffer_size          = BUFFER_SIZE,
        .pixel_map            = s_pixel_map,
        .hub75_buff           = s_hub75_buff,
        .module_code          = MODULE_CODE,
        .light_level          = DEV_DISPLAY_BRIGHTNESS_MAX,
    },
};

dev_display_t *dev_p5_64x32_get(void)
{
    return &g_p5_64x32.base;
}

/* ================================================================
 *  prepare: pixel_map → hub75_buff 像素重排
 *
 *  pixel_map[] 按行优先存储（y * screen_rows + x），
 *  hub75_buff[] 按"组"组织以匹配 HUB75 移位寄存器输入时序。
 *  此函数将像素值从逻辑坐标映射到硬件输出缓冲。
 * ================================================================ */

static void _prepare(dev_display_t *dev)
{
    const int32_t screen_rows = (int32_t)dev->screen_rows;

    for (int32_t linear = 0; linear < (int32_t)dev->buffer_size; linear++) {
        const int32_t pixel_col = linear % screen_rows; /* 像素列坐标 x（行优先: x 先递增） */
        const int32_t pixel_row = linear / screen_rows; /* 像素行坐标 y */

        /* 组索引 = 通道 × 扫描线 × 行内模块号：
         *   通道     = pixel_row / ROWS_PER_CHANNEL（1/8 扫下每通道 16 行）
         *   扫描线   = pixel_row % SCAN_LINES
         *   行内模块 = pixel_col / MODULE_PIXEL_ROW */
        const int32_t group_index = pixel_row % SCAN_LINES * MODULE_ROWS
                                  + pixel_col / MODULE_PIXEL_ROW
                                  + pixel_row / ROWS_PER_CHANNEL * (SCAN_LINES * MODULE_ROWS);

        /* 组内偏移：行带各占半组（低 8 行 → 后半组，高 8 行 → 前半组），
         * 组内列号对模块宽取模（与 1-543 实机确认的链段口径一致） */
        const int32_t band_half  = ((pixel_row / SCAN_LINES) % 2) ^ 1;
        const int32_t dst_offset = group_index * GROUP_SIZE + band_half * (GROUP_SIZE / 2) + pixel_col % MODULE_PIXEL_ROW;

        dev->hub75_buff[dst_offset] = dev->pixel_map[linear];
    }
}

/* ================================================================
 *  scan: 逐像素输出 — BSRR 查表 + CLK 脉冲
 *
 *  hub75_buff[] 存储的是颜色索引 (0~7)，扫描时查 s_bsrr_table
 *  得到该通道、该颜色的 R/G/B 三组 {port, BSRR_val}，直接 flush 输出。
 * ================================================================ */

static inline void _scan(dev_display_t *dev, uint8_t line)
{
    for (uint16_t pixel = 0; pixel < dev->scan_line_pixels; pixel++) {
        /* 当前像素在所有通道中的起始偏移 */
        uint16_t pixel_base = (uint16_t)line * dev->scan_line_pixels + pixel;

        /* 同一像素位置同时输出所有通道的颜色数据 */
        for (uint8_t ch = 0; ch < dev->total_channels; ch++) {
            uint8_t color     = dev->hub75_buff[pixel_base + ch * dev->channel_pixels];
            const bsrr_t *bsrr = &s_bsrr_table[ch][color];
            pl_hub75_bsrr_flush(&bsrr->r);
            pl_hub75_bsrr_flush(&bsrr->g);
            pl_hub75_bsrr_flush(&bsrr->b);
        }

        /* 锁存当前像素数据到移位寄存器 */
        pl_hub75_clock_pulse();
    }
}

/* ---- set_row: ABCD 行地址编码（译码器型行驱动，1/8 扫只用到 A/B/C） ---- */
static void _set_row(uint8_t row)
{
    pl_hub75_Decoder_set_row(row);
}

/* ---- ops 虚表 ---- */
static const dev_display_ops_t s_ops = {
    .prepare = _prepare,
    .scan    = _scan,
    .set_row = _set_row,
};

/* ================================================================
 *  dev_p5_64x32_init: 预计算 BSRR 查表 + 绑定 ops
 *
 *  s_bsrr_table[ch][c] 存储通道 ch 在颜色 c (0~7) 时的 R/G/B 引脚输出值。
 *  颜色 c 的 bit0→R, bit1→G, bit2→B:
 *    - R 亮: (c & 1) != 0 → BSRR 置位（高电平）
 *    - R 灭: (c & 1) == 0 → BSRR 复位（低电平）
 *    - G 亮: (c & 2) != 0,  B 亮: (c & 4) != 0
 *
 *  预计算避免扫描热路径中的分支判断。
 * ================================================================ */

void dev_p5_64x32_init(void)
{
    g_p5_64x32.base.ops = &s_ops;
    dev_display_register(&g_p5_64x32.base);

    for (uint8_t ch = 0; ch < g_p5_64x32.base.total_channels; ch++) {
        for (uint8_t color = 0; color < 8; color++) {
            /* R 通道: color bit0 决定亮灭 */
            s_bsrr_table[ch][color].r.port = g_hub75_pin_r[ch].port;
            s_bsrr_table[ch][color].r.val  = (color & 1) ? (uint32_t)g_hub75_pin_r[ch].pin        /* 置位: 输出高 */
                                                         : (uint32_t)g_hub75_pin_r[ch].pin << 16; /* 复位: 输出低 */

            /* G 通道: color bit1 决定亮灭 */
            s_bsrr_table[ch][color].g.port = g_hub75_pin_g[ch].port;
            s_bsrr_table[ch][color].g.val  = (color & 2) ? (uint32_t)g_hub75_pin_g[ch].pin
                                                         : (uint32_t)g_hub75_pin_g[ch].pin << 16;

            /* B 通道: color bit2 决定亮灭 */
            s_bsrr_table[ch][color].b.port = g_hub75_pin_b[ch].port;
            s_bsrr_table[ch][color].b.val  = (color & 4) ? (uint32_t)g_hub75_pin_b[ch].pin
                                                         : (uint32_t)g_hub75_pin_b[ch].pin << 16;
        }
    }
}
hw_dev_initcall(dev_p5_64x32_init);
