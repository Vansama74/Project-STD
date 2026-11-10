/**
 * @file    dev_display_1_543.c
 * @brief   1-543 模组派生类型(蓝极星P5) — P5 户外全彩（行带 × 列块链段映射）
 *
 * 实现 dev_display_ops: prepare (pixel_map→hub75_buff) + scan (BSRR 查表输出)
 * 两块整屏缓冲（pixel_map / hub75_buff）放在 CCMRAM；查表 g_bsrr / 行基址 /
 * 链段索引放在普通 SRAM（CCM 预算不足，三者均为 CPU 独占访问、无 DMA 需求）。
 *
 * 单模块 64x32 / 1/8 扫 / 2 通道（R1G1B1 上半、R2G2B2 下半）；
 * 列驱动 ICN2037BP（16 通道恒流）×8 颗/数据线，行驱动 URC7258D（8 路译码）。
 * 链段与物理区块（行带 × 列块）的对应见 _1_543_chain_seg 与 doc/01。
 */

#include "dev_display.h"

#include <string.h>
#include "initcall.h"

/* ================================================================
 *  1-543 模组参数 — P5 户外全彩（P5-8S，320x160mm）
 *
 *  单模块 64x32 像素，HUB75 2 通道/模块，1/8 扫描。
 *  改 MODULE_ROWS / MODULE_COLS 即可切换拼屏（如 1x1 → 64x32，3x4 → 192x128）。
 * ================================================================ */
#define MODULE_CODE                "1000000543"

#define _1_543_MODULE_ROWS         (3U)  /* 每行模块数（水平） */
#define _1_543_MODULE_COLS         (3U)  /* 每列模块数（垂直） */
#define _1_543_MODULE_PIXEL_ROW    (64U) /* 单模块每行的像素个数 */
#define _1_543_MODULE_PIXEL_COL    (32U) /* 单模块每列的像素个数 */
#define _1_543_CHANNELS_PER_MODULE (2U)  /* 每模块通道数（R1G1B1 + R2G2B2） */
#define _1_543_SCAN_LINES          (8U)  /* 1/8 扫描 */

/* ---- 派生参数（由模组参数计算，勿手动修改） ---- */
#define _1_543_SCREEN_ROWS    (_1_543_MODULE_ROWS * _1_543_MODULE_PIXEL_ROW) /* 屏幕每行像素数 */
#define _1_543_SCREEN_COLS    (_1_543_MODULE_COLS * _1_543_MODULE_PIXEL_COL) /* 屏幕每列像素数 */
#define _1_543_BUFFER_SIZE    (_1_543_SCREEN_ROWS * _1_543_SCREEN_COLS)
#define _1_543_TOTAL_CHANNELS (_1_543_MODULE_COLS * _1_543_CHANNELS_PER_MODULE)
#define _1_543_CHANNEL_PIXELS (_1_543_MODULE_PIXEL_ROW * _1_543_MODULE_PIXEL_COL * _1_543_MODULE_ROWS / _1_543_CHANNELS_PER_MODULE)
#define _1_543_SCAN_LINE_PX   (_1_543_CHANNEL_PIXELS / _1_543_SCAN_LINES)

/* ================================================================
 *  hub75_buff 布局（1-543 行带 × 列块）
 *
 *  每通道内按 scan line 连续存放，每条 scan line 128 bit/模块：
 *
 *    bit 段 k (16 bit = 1 颗 ICN2037BP 的 16 路输出 = 物理 16 列 x 8 行)
 *    ─────────────────────────────────────────────────────────────
 *      k=0..3  本通道高行带（local_y 8..15），X 0-15 / 16-31 / 32-47 / 48-63
 *      k=4..7  本通道低行带（local_y 0..7 ），X 0-15 / 16-31 / 32-47 / 48-63
 *
 *  行映射（1/8 扫描，每通道覆盖 16 行，每扫描线点亮 4 物理行）：
 *    ch = row / 16，line = row & 7，row 带 = (row >> 3) & 1
 *    ch0: 低带=行 L、高带=行 L+8；ch1: 低带=行 L+16、高带=行 L+24
 *
 *  契约：_1_543_chain_seg 必须把 2 行带 × 4 列块各覆盖一次——重复项会让某个
 *  16x8 区块永不刷新、另一区块被写两遍（无明显现象，只表现为局部错图）；
 *  段顺序与实机不符表现为整屏碎成 8 块，判读与改法见 doc/01 的 1-543 记录。
 * ================================================================ */

#define _1_543_SEG_PIXELS     (16U)                                            /* 16：一颗 ICN2037BP */
#define _1_543_SEGS_PER_BAND  (_1_543_MODULE_PIXEL_ROW / _1_543_SEG_PIXELS)    /* 4：每行带列块数 */
#define _1_543_BANDS          (_1_543_CHANNELS_PER_MODULE)                     /* 2：每通道行带数 */
#define _1_543_SEGS_PER_MODULE (_1_543_SEGS_PER_BAND * _1_543_BANDS)           /* 8：单模块每扫描线段数 */
#define _1_543_MODULE_BUFF_STRIDE (_1_543_SEGS_PER_MODULE * _1_543_SEG_PIXELS) /* 128：单模块每扫描线占用 */
#define _1_543_ROWS_PER_CHANNEL (_1_543_MODULE_PIXEL_COL / _1_543_CHANNELS_PER_MODULE) /* 16 */

/* 链段 → 物理区块：band 0 = 本通道低 8 行，1 = 高 8 行；col = 列块 0..3 */
typedef struct {
    uint8_t band;
    uint8_t col;
} _1_543_seg_t;

static const _1_543_seg_t _1_543_chain_seg[_1_543_SEGS_PER_MODULE] = {
    {1, 0}, {1, 1}, {1, 2}, {1, 3}, /* 链首 4 段：高行带整行（X 0..63） */
    {0, 0}, {0, 1}, {0, 2}, {0, 3}, /* 链尾 4 段：低行带整行（X 0..63） */
};

/* ---- BSRR 预计算查表（普通 SRAM：CCM 预算留给两块整屏缓冲）---- */
typedef struct {
    pl_hub75_bsrr_t r, g, b;
} _1_543_bsrr_t;

static _1_543_bsrr_t g_bsrr[_1_543_TOTAL_CHANNELS][8];

/* ---- 1-543 实例 ---- */
typedef struct {
    dev_display_t me;
} dev_display_1_543_t;

[[gnu::section(".ccmram")]] static uint8_t _1_543_pixel_map[_1_543_BUFFER_SIZE];
[[gnu::section(".ccmram")]] static uint8_t _1_543_hub75_buff[_1_543_BUFFER_SIZE];
static uint16_t _1_543_row_base[_1_543_SCREEN_COLS];                          /* 行所在扫描线基址：普通 SRAM */
static uint8_t _1_543_seg_idx[_1_543_BANDS][_1_543_SEGS_PER_BAND];            /* (行带,列块) → 链段号 */
/* 契约：偏移表为 uint16 ⇒ 拼屏模块数须 R×C ≤ 32（最大偏移 buffer_size-1 恰可表示；
 * 越限 = 静默截断错图，无编译期断言，见 doc/01 的 1-543 记录）。 */

static dev_display_1_543_t g_1_543 = {
    .me = {
        .ops                 = nullptr, /* 由 dev_display_1_543_init 设置 */
        .module_rows         = _1_543_MODULE_PIXEL_ROW,
        .module_cols         = _1_543_MODULE_PIXEL_COL,
        .channels_per_module = _1_543_CHANNELS_PER_MODULE,
        .modules_per_row     = _1_543_MODULE_ROWS,
        .modules_per_col     = _1_543_MODULE_COLS,
        .scan_lines          = _1_543_SCAN_LINES,
        .screen_rows         = _1_543_SCREEN_ROWS,
        .screen_cols         = _1_543_SCREEN_COLS,
        .total_channels      = _1_543_TOTAL_CHANNELS,
        .channel_pixels      = _1_543_CHANNEL_PIXELS,
        .scan_line_pixels    = _1_543_SCAN_LINE_PX,
        .buffer_size         = _1_543_BUFFER_SIZE,
        .pixel_map           = _1_543_pixel_map,
        .hub75_buff          = _1_543_hub75_buff,
        .module_code         = MODULE_CODE,
        .light_level         = DEV_DISPLAY_BRIGHTNESS_MAX,
    },
};

dev_display_t *dev_display_1_543_get(void)
{
    return &g_1_543.me;
}

/* ================================================================
 *  prepare: pixel_map → hub75_buff 像素重排
 *
 *  pixel_map[] 按行优先存储（y * screen_rows + x）；
 *  逻辑行按 _1_543_row_base[] 定位到通道/扫描线，再把该行 4 个 16 列块
 *  各拷到 _1_543_seg_idx[行带][列块] 指定的链段。
 * ================================================================ */

static void _1_543_prepare(dev_display_t *dev)
{
    const uint16_t screen_rows = dev->screen_rows;
    const uint16_t screen_cols = dev->screen_cols;
    const uint8_t *pixel_map   = dev->pixel_map;
    uint8_t *hub75_buff        = dev->hub75_buff;

    /* 脏矩形行子集：prepare 按逻辑行整行拷贝，有效脏矩形 → 只重排矩形覆盖的
     * y 行范围；矩形无效 → 全量路径（保守回退，正确性不变）。 */
    uint16_t row_begin = 0;
    uint16_t row_end   = screen_cols;
    if (dev->dirty_rect_valid) {
        if (dev->dirty_rect_h == 0)
            return; /* 空矩形：无行变化，无需重排 */
        row_begin = dev->dirty_rect_y;
        if (row_begin >= screen_cols)
            return; /* 矩形整体在屏外（防御，提交侧已钳位） */
        uint32_t end = (uint32_t)row_begin + dev->dirty_rect_h;
        row_end      = (end > screen_cols) ? screen_cols : (uint16_t)end;
    }

    for (uint16_t row = row_begin; row < row_end; row++) {
        const uint8_t *src     = pixel_map + (uint16_t)(row * screen_rows);
        const uint8_t *seg_idx = _1_543_seg_idx[(row >> 3) & 1U];
        const uint16_t row_base = _1_543_row_base[row];

        for (uint16_t m = 0; m < _1_543_MODULE_ROWS; m++) {
            uint8_t *dst     = hub75_buff + (uint16_t)(row_base + m * _1_543_MODULE_BUFF_STRIDE);
            const uint8_t *s = src + m * _1_543_MODULE_PIXEL_ROW;

            for (uint8_t seg = 0; seg < _1_543_SEGS_PER_BAND; seg++) {
                memcpy(dst + (uint16_t)seg_idx[seg] * _1_543_SEG_PIXELS,
                       s + seg * _1_543_SEG_PIXELS,
                       _1_543_SEG_PIXELS);
            }
        }
    }
}

/* ================================================================
 *  scan: 逐像素输出 — BSRR 查表 + CLK 脉冲
 *
 *  每扫描线 128 时钟/模块：每时钟 ch0（R1G1B1）/ ch1（R2G2B2）各取 1 字节
 *  颜色索引，查 g_bsrr 输出 R/G/B，时钟上升沿移入模组移位链。
 * ================================================================ */

static inline void _1_543_scan(dev_display_t *dev, uint8_t line)
{
    const uint16_t scan_line_pixels = dev->scan_line_pixels;
    const uint16_t channel_pixels   = dev->channel_pixels;
    const uint8_t total_channels    = dev->total_channels;
    const _1_543_bsrr_t (*bsrr)[8]  = g_bsrr;
    const uint8_t *base             = dev->hub75_buff + (uint16_t)line * scan_line_pixels;

    for (uint16_t pixel = 0; pixel < scan_line_pixels; pixel++, base++) {
        const uint8_t *channel_ptr = base;
        for (uint8_t ch = 0; ch < total_channels; ch++) {
            uint8_t color = channel_ptr[(uint16_t)channel_pixels * ch];
            const _1_543_bsrr_t *entry = &bsrr[ch][color];
            pl_hub75_bsrr_flush(&entry->r);
            pl_hub75_bsrr_flush(&entry->g);
            pl_hub75_bsrr_flush(&entry->b);
        }
        pl_hub75_clock_pulse();
    }
}

/* ---- set_row: URC7258D 译码器型行驱动（A/B/C 直通二进制行址，D 恒 0） ---- */
static void _1_543_set_row(uint8_t row)
{
    pl_hub75_Decoder_set_row(row);
}

/* ---- ops 虚表 ---- */
static const dev_display_ops_t _1_543_ops = {
    .prepare = _1_543_prepare,
    .scan    = _1_543_scan,
    .set_row = _1_543_set_row,
};

/* ================================================================
 *  dev_display_1_543_init: 预计算行基址 / 链段索引 / BSRR 查表 + 绑定 ops
 *
 *  _1_543_row_base[row] = ch * CHANNEL_PIXELS + line * SCAN_LINE_PX
 *  _1_543_seg_idx[band][col] = 该区块在链上的段号（由 _1_543_chain_seg 反查）
 *  g_bsrr[ch][c] = 通道 ch 在颜色 c (0~7) 时的 R/G/B 引脚输出值
 * ================================================================ */

void dev_display_1_543_init(void)
{
    g_1_543.me.ops = &_1_543_ops;
    dev_display_register(&g_1_543.me);

    for (uint16_t row = 0; row < _1_543_SCREEN_COLS; row++) {
        const uint16_t ch   = (uint16_t)(row / _1_543_ROWS_PER_CHANNEL);
        const uint16_t line = (uint16_t)(row % _1_543_SCAN_LINES);
        _1_543_row_base[row] = (uint16_t)(ch * _1_543_CHANNEL_PIXELS + line * _1_543_SCAN_LINE_PX);
    }

    for (uint8_t seg = 0; seg < _1_543_SEGS_PER_MODULE; seg++) {
        _1_543_seg_idx[_1_543_chain_seg[seg].band][_1_543_chain_seg[seg].col] = seg;
    }

    for (uint8_t ch = 0; ch < g_1_543.me.total_channels; ch++) {
        const uint32_t r_on  = (uint32_t)g_hub75_pin_r[ch].pin;
        const uint32_t g_on  = (uint32_t)g_hub75_pin_g[ch].pin;
        const uint32_t b_on  = (uint32_t)g_hub75_pin_b[ch].pin;
        const uint32_t r_off = r_on << 16;
        const uint32_t g_off = g_on << 16;
        const uint32_t b_off = b_on << 16;

        for (uint8_t color = 0; color < 8; color++) {
            g_bsrr[ch][color].r.port = g_hub75_pin_r[ch].port;
            g_bsrr[ch][color].r.val  = (color & 1U) ? r_on : r_off;
            g_bsrr[ch][color].g.port = g_hub75_pin_g[ch].port;
            g_bsrr[ch][color].g.val  = (color & 2U) ? g_on : g_off;
            g_bsrr[ch][color].b.port = g_hub75_pin_b[ch].port;
            g_bsrr[ch][color].b.val  = (color & 4U) ? b_on : b_off;
        }
    }
}
hw_dev_initcall(dev_display_1_543_init);
