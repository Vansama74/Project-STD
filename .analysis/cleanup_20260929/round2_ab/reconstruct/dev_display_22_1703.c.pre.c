/**
 * @file    dev_display_22_1703.c
 * @brief   22-1703 模组派生类型 — P10 32x16 双色/全彩（1/4 扫描）
 *
 * 料号 2200001703（P10 32x16，列驱动 MBI5124GM 16 通道恒流，行驱动译码器型 ABCD）。
 * 实现 dev_display_ops: prepare (pixel_map→hub75_buff) + scan (BSRR 查表输出)。
 *
 * 几何（与源固件 9K23881580 `USER/TIMER/timer.h:5-19` 同构）：
 *   单模块 32x16，2 通道/模块（R1G1B1 + R2G2B2），1/4 扫描；
 *   7 模块/行 x 4 模块/列 → 224x64，8 通道，显存 14336B x2（CCMRAM）。
 *
 * 移位链推导（每扫描线每通道 448 bit = 448 时钟）：
 *   1/4 扫 → 每扫描线点亮 4 物理行（y、y+4、y+8、...、y+60 共 16 行？否——
 *   本模组单通道覆盖 8 物理行：上半 4 行 + 下半 4 行，见下）；
 *   通道 = y/8（0~7），扫描行 = y%4，半行 = (y%8 >= 4)；
 *   每通道像素 1792 = 32*16*7/2，每扫描行 1792/4 = 448。
 *
 * hub75_buff 布局（= 源固件 fixBuf 位流顺序，逐字节等价）：
 *   组（GROUP_SIZE=16 字节）= 同一扫描线的 8 个 x（上半行）+ 同 8 个 x（下半行）；
 *   组内偏移 0..7 = 上半行 x%8，偏移 8..15 = 下半行 x%8（顺序，非逆序）；
 *   组号 = (y/8*4 + y%4) * (448/16) + x/8，每扫描行 28 组。
 *   scan 逐像素步进 448 次，每次取 8 通道各 1 字节（步长 CHANNEL_PIXELS=1792）。
 *
 * 映射裁决（2026-09-14，任务 Q3）：以 wyh 分支实机驱动
 * `Project-STD-wyh/Device/Display/dev_P10_32x16_2200001703.c` 为准 ——
 * 通道序为自然序 {R1..R8}（ch = y/8），下半行位序为顺序（偏移 +8），
 * 与源固件 9K23881580 的推导逐位一致（wyh 版为源公式的代数等价改写，
 * 已用宿主穷举比对验证 14336/14336 全像素命中；差异仅在屏体拼装宏
 * MODULE_ROWS：wyh 版 10（320x64）/ 本文件 7（224x64，本订单口径））。
 * 与已删除的旧 STD `convert_pixelmap_p10` 不同：后者下半行逆序 + 通道相邻互换，
 * 本屏体不采用（该实现所在工程/DOM 已废弃）。
 *
 * scan 实现档位 D+（合并写 BSRR，2026-09-14）：把 8 通道 24 个数据脚按
 * 「同一 GPIO 端口」分组（本 PCB 为 5 组：G/B/E/C/F，由 pl_hub75 引脚表推导），
 * 每时钟只写 5 次 BSRR（合并同端口多通道的置位/复位位），替代档位 A 的
 * 24 次写引脚；key 位序按组内信号排列，表在 init 期由 g_hub75_pin_* 生成，
 * 并校验同组信号确在同一端口——校验失败自动回退通用逐通道路径
 * （pl_hub75_set_rgb），不静默出错。
 *
 * 仅改拼装宏即可适配不同屏体，但两者代价不同：
 *   - `_22_1703_MODULE_ROWS`（每行模块数）**可直接改**：`GROUPS_PER_LINE`、`row_dst`
 *     与扫描均自动跟随（组公式按 MODULE_ROWS*4 自适应）；
 *   - `_22_1703_MODULE_COLS`（每列模块数）**改后须同步重做端口分组表**：它决定通道数
 *     （TOTAL_CHANNELS = MODULE_COLS*2）与每通道覆盖行数，下方的 5 组信号表与 scan 的
 *     c0..c7 均按 8 通道写死；通道数变化时需按新引脚分布重排分组与 key 表达式。
 */

#include "dev_display.h"

#include <string.h>
#include "initcall.h"

/* ================================================================
 *  22-1703 模组参数 — P10 32x16
 * ================================================================ */
#define MODULE_CODE "2200001703"

#define _22_1703_MODULE_ROWS         (7U)  /* 每行模块数（水平） */
#define _22_1703_MODULE_COLS         (4U)  /* 每列模块数（垂直） */
#define _22_1703_MODULE_PIXEL_ROW    (32U) /* 单模块每行的像素个数 */
#define _22_1703_MODULE_PIXEL_COL    (16U) /* 单模块每列的像素个数 */
#define _22_1703_CHANNELS_PER_MODULE (2U)  /* 每模块通道数（R1G1B1 + R2G2B2） */
#define _22_1703_SCAN_LINES          (4U)  /* 1/4 扫描 */

/* ---- 派生参数（由模组参数计算，勿手动修改） ---- */
#define _22_1703_SCREEN_ROWS    (_22_1703_MODULE_ROWS * _22_1703_MODULE_PIXEL_ROW) /* 屏幕每行像素数 = 224 */
#define _22_1703_SCREEN_COLS    (_22_1703_MODULE_COLS * _22_1703_MODULE_PIXEL_COL) /* 屏幕每列像素数 = 64 */
#define _22_1703_BUFFER_SIZE    (_22_1703_SCREEN_ROWS * _22_1703_SCREEN_COLS)      /* 14336 */
#define _22_1703_TOTAL_CHANNELS (_22_1703_MODULE_COLS * _22_1703_CHANNELS_PER_MODULE) /* 8 */
/* 注意：除数是 CHANNELS_PER_MODULE（每模块 2 通道），不是 TOTAL_CHANNELS——
 * 除错会得 448 而非 1792，整屏映射全错（1-263 同式）。 */
#define _22_1703_CHANNEL_PIXELS (_22_1703_MODULE_PIXEL_ROW * _22_1703_MODULE_PIXEL_COL * _22_1703_MODULE_ROWS / _22_1703_CHANNELS_PER_MODULE)
#define _22_1703_SCAN_LINE_PX   (_22_1703_CHANNEL_PIXELS / _22_1703_SCAN_LINES) /* 448 */

#define _22_1703_GROUP_SIZE    (16U)                                        /* 每组像素数（8 上 + 8 下） */
#define _22_1703_GROUPS_PER_LINE (_22_1703_SCAN_LINE_PX / _22_1703_GROUP_SIZE) /* 每扫描行 28 组 */
/* 单通道覆盖的行数 = 单模块列像素 / 每模块通道数 = 8（上半 4 行 + 下半 4 行） */
#define _22_1703_ROWS_PER_CHANNEL (_22_1703_MODULE_PIXEL_COL / _22_1703_CHANNELS_PER_MODULE)

/* ---- 合并写查表（档位 D+）---- */

/** 组内信号描述：通道 ch 的 sig 脚（0=R/1=G/2=B）占用 key 的第 bit 位 */
typedef struct {
    uint8_t ch;
    uint8_t sig;
    uint8_t bit;
} _22_1703_sig_t;

/**
 * 端口分组（由本 PCB 的 pl_hub75 引脚表推导，见 Core/Inc/main.h:74-193）：
 *   grp0 ← ch0(R1,G1,B1) + ch1(R2)                    —— PG9/PG10/PG12/PG15
 *   grp1 ← ch1(G2,B2)    + ch2(R3,G3)                 —— PB7/PB6/PB8/PB9
 *   grp2 ← ch2(B3) + ch3(R4,G4,B4) + ch4(R5,G5,B5)    —— PE0/PE1/PE2/PE3/PE4/PE5/PE6
 *   grp3 ← ch5(R6,G6,B6)                              —— PC13/PC14/PC15
 *   grp4 ← ch6(R7,G7,B7) + ch7(R8,G8,B8)              —— PF0..PF5
 *
 * key 位序必须与下方 scan 热路径的 key 表达式逐位一致（颜色位序沿用 g_bsrr 约定：
 * color bit0=R / bit1=G / bit2=B，故「通道颜色字节的第 sig 位」= 该信号亮灭）：
 *   grp0 key = c0 | c1<<3        → ch0 占 key[2:0]、ch1.R 占 key[3]
 *   grp1 key = c1 | c2<<3        → ch1 占 key[2:0]（key[0]=R2 无关项）、ch2.R/G 占 key[3]/key[4]
 *   grp2 key = c2 | c3<<3 | c4<<6→ ch2 占 key[2:0]、ch3 占 key[5:3]、ch4 占 key[8:6]
 *   grp3 key = c5                → ch5 占 key[2:0]
 *   grp4 key = c6 | c7<<3        → ch6 占 key[2:0]、ch7 占 key[5:3]
 * 无关位（该端口未接的建议脚）在表中重复填充，由 key_bits 位宽覆盖。
 * 表内容由 init 期 _22_1703_build_merge_tables() 按 g_hub75_pin_* 生成并校验端口一致性。
 */
static const _22_1703_sig_t _22_1703_grp0_sigs[] = {
    {0, 0, 0}, {0, 1, 1}, {0, 2, 2}, {1, 0, 3}};
static const _22_1703_sig_t _22_1703_grp1_sigs[] = {
    {1, 1, 1}, {1, 2, 2}, {2, 0, 3}, {2, 1, 4}};
static const _22_1703_sig_t _22_1703_grp2_sigs[] = {
    {2, 2, 2}, {3, 0, 3}, {3, 1, 4}, {3, 2, 5}, {4, 0, 6}, {4, 1, 7}, {4, 2, 8}};
static const _22_1703_sig_t _22_1703_grp3_sigs[] = {
    {5, 0, 0}, {5, 1, 1}, {5, 2, 2}};
static const _22_1703_sig_t _22_1703_grp4_sigs[] = {
    {6, 0, 0}, {6, 1, 1}, {6, 2, 2}, {7, 0, 3}, {7, 1, 4}, {7, 2, 5}};

/* 合并表置 CCMRAM：无初始化器（init 期生成），.ccmram 段 NOLOAD 清零不丢失内容 */
[[gnu::section(".ccmram")]] static uint32_t _22_1703_tab_g[1U << 6]; /* key = c0 | c1<<3        */
[[gnu::section(".ccmram")]] static uint32_t _22_1703_tab_b[1U << 6]; /* key = c1 | c2<<3        */
[[gnu::section(".ccmram")]] static uint32_t _22_1703_tab_e[1U << 9]; /* key = c2 | c3<<3 | c4<<6 */
[[gnu::section(".ccmram")]] static uint32_t _22_1703_tab_c[1U << 3]; /* key = c5                */
[[gnu::section(".ccmram")]] static uint32_t _22_1703_tab_f[1U << 6]; /* key = c6 | c7<<3        */

typedef struct {
    GPIO_TypeDef *port;                 /* 组数据脚所在端口（init 校验一致性后取值） */
    const _22_1703_sig_t *sigs;         /* 组内信号 */
    uint8_t count;                      /* 信号个数 */
    uint8_t key_bits;                   /* key 位宽（表大小 = 1<<key_bits） */
    uint32_t *table;                    /* 合并后的 BSRR 字表 */
} _22_1703_grp_t;

static _22_1703_grp_t _22_1703_grps[] = {
    {nullptr, _22_1703_grp0_sigs, 4U, 6U, _22_1703_tab_g},
    {nullptr, _22_1703_grp1_sigs, 4U, 6U, _22_1703_tab_b},
    {nullptr, _22_1703_grp2_sigs, 7U, 9U, _22_1703_tab_e},
    {nullptr, _22_1703_grp3_sigs, 3U, 3U, _22_1703_tab_c},
    {nullptr, _22_1703_grp4_sigs, 6U, 6U, _22_1703_tab_f},
};
#define _22_1703_GRP_COUNT (sizeof(_22_1703_grps) / sizeof(_22_1703_grps[0]))

/** 合并写路径可用标志：init 期校验同组信号同端口，失败回退通用逐通道路径 */
static bool s_22_1703_merged_ok;

/* ---- 22-1703 实例 ---- */
typedef struct {
    dev_display_t me;
} dev_display_22_1703_t;

[[gnu::section(".ccmram")]] static uint8_t _22_1703_pixel_map[_22_1703_BUFFER_SIZE];
[[gnu::section(".ccmram")]] static uint8_t _22_1703_hub75_buff[_22_1703_BUFFER_SIZE];
[[gnu::section(".ccmram")]] static uint16_t _22_1703_row_dst[_22_1703_SCREEN_COLS];

static dev_display_22_1703_t g_22_1703 = {
    .me = {
        .ops                 = nullptr, /* 由 dev_display_22_1703_init 设置 */
        .module_rows         = _22_1703_MODULE_PIXEL_ROW,
        .module_cols         = _22_1703_MODULE_PIXEL_COL,
        .channels_per_module = _22_1703_CHANNELS_PER_MODULE,
        .modules_per_row     = _22_1703_MODULE_ROWS,
        .modules_per_col     = _22_1703_MODULE_COLS,
        .scan_lines          = _22_1703_SCAN_LINES,
        .screen_rows         = _22_1703_SCREEN_ROWS,
        .screen_cols         = _22_1703_SCREEN_COLS,
        .total_channels      = _22_1703_TOTAL_CHANNELS,
        .channel_pixels      = _22_1703_CHANNEL_PIXELS,
        .scan_line_pixels    = _22_1703_SCAN_LINE_PX,
        .buffer_size         = _22_1703_BUFFER_SIZE,
        .pixel_map           = _22_1703_pixel_map,
        .hub75_buff          = _22_1703_hub75_buff,
        .module_code         = MODULE_CODE,
        .light_level         = DEV_DISPLAY_BRIGHTNESS_MAX,
    },
};

dev_display_t *dev_display_22_1703_get(void)
{
    return &g_22_1703.me;
}

/* ================================================================
 *  prepare: pixel_map → hub75_buff 像素重排
 *
 *  行起始偏移由 init 预计算到 _22_1703_row_dst[]：
 *    row_dst[y] = (y/8)*CHANNEL_PIXELS + (y%4)*SCAN_LINE_PX + ((y%8)>=4 ? 8 : 0)
 *  每逻辑行 = 28 个 8 字节块，块内 8 字节连续，块间步长 GROUP_SIZE(16)，
 *  下半行整体偏移 +8（与源固件 fixBuf 组内布局一致）。
 * ================================================================ */

static void _22_1703_prepare(dev_display_t *dev)
{
    const uint16_t screen_rows = dev->screen_rows; /* 224：行主序步长（= 行宽） */
    const uint16_t screen_cols = dev->screen_cols; /* 64：行数（高） */
    const uint8_t *pixel_map   = dev->pixel_map;
    uint8_t *hub75_buff        = dev->hub75_buff;

    /* ④a 脏矩形行子集：每逻辑行独立落入自己的 8 字节半区，行子集重排安全；
     * 矩形无效 → 全量路径（保守回退，正确性不变）。 */
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
        const uint8_t *src = pixel_map + (uint32_t)row * screen_rows;
        uint8_t *dst_base  = hub75_buff + _22_1703_row_dst[row];

        for (uint16_t blk = 0; blk < _22_1703_GROUPS_PER_LINE; blk++) {
            /* 8 字节块 = 连续 8 个 x；块间步长 16（另一半行占步长内 8 字节） */
            memcpy(dst_base + (uint16_t)(blk * _22_1703_GROUP_SIZE),
                   src + (uint16_t)(blk * 8U), 8U);
        }
    }
}

/* ================================================================
 *  scan: 一个扫描行的位流输出
 *
 *  档位 D+：每时钟合并写 5 次 BSRR（同端口多通道一次写出），
 *  替代档位 A 的 8 通道 x 3 次写引脚。每扫描线 448 时钟。
 * ================================================================ */

static inline void _22_1703_scan(dev_display_t *dev, uint8_t line)
{
    const uint8_t *base  = dev->hub75_buff + (uint16_t)line * _22_1703_SCAN_LINE_PX;
    const uint16_t stride = _22_1703_CHANNEL_PIXELS;

    if (!s_22_1703_merged_ok) {
        /* 通用回退：逐通道写引脚（pl_hub75_set_rgb），任何引脚排布都正确 */
        for (uint16_t px = 0; px < _22_1703_SCAN_LINE_PX; px++, base++) {
            for (uint8_t ch = 0; ch < _22_1703_TOTAL_CHANNELS; ch++) {
                pl_hub75_set_rgb(ch, (hub75_color_t)base[(uint16_t)stride * ch]);
            }
            pl_hub75_clock_pulse();
        }
        return;
    }

    GPIO_TypeDef *const pg = _22_1703_grps[0].port;
    GPIO_TypeDef *const pb = _22_1703_grps[1].port;
    GPIO_TypeDef *const pe = _22_1703_grps[2].port;
    GPIO_TypeDef *const pc = _22_1703_grps[3].port;
    GPIO_TypeDef *const pf = _22_1703_grps[4].port;

    for (uint16_t px = 0; px < _22_1703_SCAN_LINE_PX; px++) {
        const uint8_t *q = base + px;
        const uint32_t c0 = q[0U * stride], c1 = q[1U * stride], c2 = q[2U * stride], c3 = q[3U * stride];
        const uint32_t c4 = q[4U * stride], c5 = q[5U * stride], c6 = q[6U * stride], c7 = q[7U * stride];

        /* 每端口一次写：置位/复位位已在查表时合并（未占用位保持复位值） */
        pg->BSRR = _22_1703_tab_g[c0 | (c1 << 3)];
        pb->BSRR = _22_1703_tab_b[c1 | (c2 << 3)];
        pe->BSRR = _22_1703_tab_e[c2 | (c3 << 3) | (c4 << 6)];
        pc->BSRR = _22_1703_tab_c[c5];
        pf->BSRR = _22_1703_tab_f[c6 | (c7 << 3)];

        /* 时钟上升沿移入模组移位链 */
        pl_hub75_clock_pulse();
    }
}

/* ---- set_row: 译码器型行驱动（A/B 二进制行址，1/4 扫；C/D 恒 0） ---- */
static void _22_1703_set_row(uint8_t row)
{
    pl_hub75_Decoder_set_row(row);
}

/* ---- ops 虚表 ---- */
static const dev_display_ops_t _22_1703_ops = {
    .prepare = _22_1703_prepare,
    .scan    = _22_1703_scan,
    .set_row = _22_1703_set_row,
};

/* ================================================================
 *  dev_display_22_1703_init: row_dst / 合并表预计算 + 绑定 ops
 * ================================================================ */

/** 取信号的引脚描述（sig: 0=R / 1=G / 2=B） */
static inline const hub75_pin_t *_22_1703_pin(uint8_t ch, uint8_t sig)
{
    switch (sig) {
        case 0: return &g_hub75_pin_r[ch];
        case 1: return &g_hub75_pin_g[ch];
        default: return &g_hub75_pin_b[ch];
    }
}

/**
 * @brief  生成 5 个端口的 BSRR 合并表，并校验同组信号确在同一端口。
 *
 *  表项 = 该 key 下单端口 32 位 BSRR 字（bit=1 → 置位输出高，bit=0 → 复位输出低）。
 *  同组信号跨端口时合并写会互相覆盖 → 置 s_22_1703_merged_ok=false 走通用回退。
 */
static void _22_1703_build_merge_tables(void)
{
    bool ok = true;

    for (uint8_t g = 0; g < _22_1703_GRP_COUNT; g++) {
        _22_1703_grp_t *grp = &_22_1703_grps[g];
        grp->port           = (GPIO_TypeDef *)_22_1703_pin(grp->sigs[0].ch, grp->sigs[0].sig)->port;

        for (uint8_t i = 1; i < grp->count; i++) {
            if (_22_1703_pin(grp->sigs[i].ch, grp->sigs[i].sig)->port != grp->port)
                ok = false; /* 同组信号不在同一端口：合并写不成立 */
        }

        const uint32_t key_max = 1U << grp->key_bits;
        for (uint32_t key = 0; key < key_max; key++) {
            uint32_t word = 0;
            for (uint8_t i = 0; i < grp->count; i++) {
                const hub75_pin_t *pin = _22_1703_pin(grp->sigs[i].ch, grp->sigs[i].sig);
                const uint32_t mask    = (uint32_t)pin->pin;
                word |= ((key >> grp->sigs[i].bit) & 1U) ? mask : (mask << 16);
            }
            grp->table[key] = word;
        }
    }

    s_22_1703_merged_ok = ok;
}

void dev_display_22_1703_init(void)
{
    g_22_1703.me.ops = &_22_1703_ops;
    dev_display_register(&g_22_1703.me);

    /* row_dst[y]：ch = y/8（自然通道序），line = y%4，半行 = (y%8)/4 */
    for (uint16_t row = 0; row < _22_1703_SCREEN_COLS; row++) {
        const uint16_t ch     = (uint16_t)(row / _22_1703_ROWS_PER_CHANNEL);
        const uint16_t line   = (uint16_t)(row % _22_1703_SCAN_LINES);
        const uint16_t half   = (uint16_t)((row % _22_1703_ROWS_PER_CHANNEL) / _22_1703_SCAN_LINES);
        _22_1703_row_dst[row] = (uint16_t)(ch * _22_1703_CHANNEL_PIXELS + line * _22_1703_SCAN_LINE_PX + half * 8U);
    }

    _22_1703_build_merge_tables();
}
hw_dev_initcall(dev_display_22_1703_init);
