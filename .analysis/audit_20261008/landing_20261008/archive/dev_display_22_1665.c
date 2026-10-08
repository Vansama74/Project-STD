/**
 * @file    dev_display_22_1665.c
 * @brief   22-1665 模组派生类型 — 16x16 红绿双色 / 静态单扫（MBI5034B 多链）
 *
 * 料号 2200001665：列驱动 MBI5034B（16 通道恒流 + 16 位移位寄存器，8 片 / 链段 = 128 位），
 * 静态单扫（`scan_lines = 1`，不用行址线）。实现 dev_display_ops: prepare (pixel_map→
 * 帧状态数组) + scan (合并写 BSRR 查表) + set_row (行址恒 0)。
 *
 * 几何：单模块 16x16（4 条数据线 × 128 位 = 512 颗 LED）；接线 = 每列模块一组 4 根数据脚
 * （组 g = 兄弟驱动通道对 2g / 2g+1 的 R/G 脚，各组并行、每帧 128 × MODULE_ROWS 时钟）；
 * **改屏体尺寸只改「模组参数」一节的 MODULE_ROWS / MODULE_COLS 两个宏**（其余全派生）。
 *
 * 物理约束（契约，非编译期守卫）：MODULE_COLS ≤ 5（HUB75 通道对上限，超限会越界读
 * `_22_1665_lines`）、像素数 / 帧位数 ≤ 65535（uint16 落点偏移与 scan 上界）；
 * 整片 CCMRAM 是否装得下由链接期兜底。
 *
 * 详细说明（几何 / 接线与技术史 / 数据流 / 落点规则 / CCM 组成 / 选编 / 标定与历史轮次）
 * 见 `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` §0；宿主脚本索引见
 * `.analysis/22_1665/README.md`。
 */

#include "dev_display.h"

#include "initcall.h"

/* ================================================================
 *  22-1665 模组参数 — 16x16 红绿双色 / 静态单扫
 * ================================================================ */
#define MODULE_CODE "2200001665"

/* ---- 屏体尺寸（**唯一几何入口**：兄弟驱动同款普通 `#define`，改值即改口径；
 *      换口径脚本用「源码替换」——命令行 `-D` 会被文件值静默覆盖）---- */
#define _22_1665_MODULE_ROWS      (8U) /* 每行模块数（水平）→ 屏宽 = MODULE_ROWS × MODULE_PIXEL_ROW */
#define _22_1665_MODULE_COLS      (4U) /* 每列模块数（垂直）→ 屏高 = MODULE_COLS × MODULE_PIXEL_COL */
#define _22_1665_MODULE_PIXEL_ROW (16U) /* 单模块每行像素数（16×16 面板；块栅格 4×4 对齐） */
#define _22_1665_MODULE_PIXEL_COL (16U) /* 单模块每列像素数 */

/* ---- 面板硬件常量（与拼屏尺寸无关）---- */
#define _22_1665_CHIP_BITS      (16U) /* 一片的位宽（MBI5034B 16 位移位寄存器） */
#define _22_1665_CHIPS          (8U)  /* 每条链段的片数：8 × 16 = 128 位 */
#define _22_1665_LINE_COUNT     (4U)  /* 每组数据线条数 = 每模块链段数（R/G × 上/下半屏） */
#define _22_1665_BLK_W          (4U)  /* 块宽（像素）：一片 16 路输出铺满一个 4×4 块 */
#define _22_1665_BLK_H          (4U)  /* 块高（像素） */
#define _22_1665_GROUP_PORT_MAX (2U)  /* 单组 4 线分布的端口槽数上限（本板每组 2：G+B / B+E） */
/* 状态数 = 2^4 = 16（每字节 bit j = 组内第 j 条数据线；与模块数、组数无关） */
#define _22_1665_STATES ((uint16_t)(1U << _22_1665_LINE_COUNT))

/* ---- 派生参数（由上面宏计算，勿手改）---- */
#define _22_1665_SCREEN_ROWS  (_22_1665_MODULE_ROWS * _22_1665_MODULE_PIXEL_ROW) /* 屏宽 = 每行像素数 */
#define _22_1665_SCREEN_COLS  (_22_1665_MODULE_COLS * _22_1665_MODULE_PIXEL_COL) /* 屏高 = 每列像素数 */
#define _22_1665_BUFFER_SIZE  (_22_1665_SCREEN_ROWS * _22_1665_SCREEN_COLS)      /* 像素数（1B/像素） */
#define _22_1665_HALF_COLS    (_22_1665_MODULE_PIXEL_COL / 2U)                   /* 半屏高 = 单链段区域高 */
#define _22_1665_MODULE_TOTAL (_22_1665_MODULE_ROWS * _22_1665_MODULE_COLS)      /* 模块总数 M */
#define _22_1665_SEGMENT_BITS (_22_1665_CHIPS * _22_1665_CHIP_BITS)             /* 单模块单线位数 = 128 */
/* 帧状态数组长度 = 链位总数 = 128 × M（= 组数 × 每帧时钟数） */
#define _22_1665_FRAME_BITS (_22_1665_SEGMENT_BITS * _22_1665_MODULE_TOTAL)
/* 链段数 = 4 × M（每列一组，4 条线一组） */
#define _22_1665_CHAIN_COUNT (_22_1665_LINE_COUNT * _22_1665_MODULE_TOTAL)

/* ---- 行为开关（**保留 `#ifndef` 守卫**：现场可命令行 `-D` 覆盖；上方几何宏为普通 `#define`）---- */

/** 链首口径（贴数据脚那一块装谁的位流）：0（默认）= 链首装**组内最后一个**模块
 *  （组内行尾 mx = MODULE_ROWS-1）；1 = 链首装组内**第一个**模块（mx = 0）。
 *  只影响「组内多于一块模组」的口径（MODULE_ROWS = 1 时每组仅一块，无影响）；
 *  现场首接多模块（MODULE_ROWS ≥ 2）时用它做 A/B（见 doc/01 §0.10）。 */
#ifndef _22_1665_CHAIN_HEAD_IS_MODULE0
#define _22_1665_CHAIN_HEAD_IS_MODULE0 (0U)
#endif

/* ---- 接线派生（每列一口 = 模型 B，唯一形态）：组数 = 模块列数；每帧时钟 = 128 × 每列模块数
 *      （各组并行、共享时钟）---- */
#define _22_1665_GROUP_COUNT          (_22_1665_MODULE_COLS)
#define _22_1665_CLOCKS               (_22_1665_SEGMENT_BITS * _22_1665_MODULE_ROWS)
#define _22_1665_GROUP_MODULE_COUNT   (_22_1665_MODULE_ROWS)
#define _22_1665_MODULE_OF_GROUP(g, local) ((uint16_t)((g) * _22_1665_MODULE_ROWS + (local)))
/* 链首块号：链上最后移入的 128 位停在链首（= 组内最后一块） */
#if _22_1665_CHAIN_HEAD_IS_MODULE0
#define _22_1665_BLOCK_OF(local, group_total) ((uint16_t)(local))
#else
#define _22_1665_BLOCK_OF(local, group_total) ((uint16_t)((group_total) - 1U - (local)))
#endif

/* ================================================================
 *  物理数据线（硬件事实表：**组 × 4 行**，前 MODULE_COLS 组在用；换接线才改本表）
 *
 *  · 组 g = 一个 HUB 口 = 一个模块列，占 HUB75 通道对 2g / 2g+1 的 R/G 脚
 *    （组内 4 行 = 上半 R/G + 下半 R/G；通道序同兄弟驱动 1_263 / 22_1703）
 *  · 每行的 role/half 模式恒 {RED,0}/{GREEN,0}/{RED,1}/{GREEN,1}（与模块数、组号无关）
 *  · 表按通道表上限预声明 5 组（HUB75_CHANNEL_MAX / 2），不用满 5 组是常态
 *
 *  现场定标（2026-09-17 逐脚实测）：组 0 = R1/G1/R2/G2（PG9/PG10/PG15/PB6）、
 *  组 1 = R3/G3/R4/G4（PB8/PB9/PE1/PE2）；B1/B2/A/B/C/D 六根实测无链，不占用。
 * ================================================================ */
#define _22_1665_ROLE_RED   (0U)
#define _22_1665_ROLE_GREEN (1U)
#define _22_1665_GROUP_MAX  (HUB75_CHANNEL_MAX / 2U)

typedef struct {
    const char *name;        /* 线名（文档 / 烧录物身份用，不参与逻辑） */
    const hub75_pin_t *line; /* 数据脚 */
    uint8_t role;            /* _22_1665_ROLE_RED / _22_1665_ROLE_GREEN */
    uint8_t half;            /* 0 = 上半屏 / 1 = 下半屏（仅组 0 的 4 行被读） */
} _22_1665_line_t;

/* 五行 = 通道对 (0,1) (2,3) (4,5) (6,7) (8,9)；改模块数**不要动本表**（只用前 MODULE_COLS 行） */
static const _22_1665_line_t
    _22_1665_lines[_22_1665_GROUP_MAX][_22_1665_LINE_COUNT] = {
        {{"R1", &g_hub75_pin_r[0], _22_1665_ROLE_RED, 0U},   /* 上半 红（PG9） */
         {"G1", &g_hub75_pin_g[0], _22_1665_ROLE_GREEN, 0U}, /* 上半 绿（PG10） */
         {"R2", &g_hub75_pin_r[1], _22_1665_ROLE_RED, 1U},   /* 下半 红（PG15） */
         {"G2", &g_hub75_pin_g[1], _22_1665_ROLE_GREEN, 1U}}, /* 下半 绿（PB6） */
        {{"R3", &g_hub75_pin_r[2], _22_1665_ROLE_RED, 0U},   /* 上半 红（PB8） */
         {"G3", &g_hub75_pin_g[2], _22_1665_ROLE_GREEN, 0U}, /* 上半 绿（PB9） */
         {"R4", &g_hub75_pin_r[3], _22_1665_ROLE_RED, 1U},   /* 下半 红（PE1） */
         {"G4", &g_hub75_pin_g[3], _22_1665_ROLE_GREEN, 1U}}, /* 下半 绿（PE2） */
        {{"R5", &g_hub75_pin_r[4], _22_1665_ROLE_RED, 0U},
         {"G5", &g_hub75_pin_g[4], _22_1665_ROLE_GREEN, 0U},
         {"R6", &g_hub75_pin_r[5], _22_1665_ROLE_RED, 1U},
         {"G6", &g_hub75_pin_g[5], _22_1665_ROLE_GREEN, 1U}},
        {{"R7", &g_hub75_pin_r[6], _22_1665_ROLE_RED, 0U},
         {"G7", &g_hub75_pin_g[6], _22_1665_ROLE_GREEN, 0U},
         {"R8", &g_hub75_pin_r[7], _22_1665_ROLE_RED, 1U},
         {"G8", &g_hub75_pin_g[7], _22_1665_ROLE_GREEN, 1U}},
        {{"R9", &g_hub75_pin_r[8], _22_1665_ROLE_RED, 0U},
         {"G9", &g_hub75_pin_g[8], _22_1665_ROLE_GREEN, 0U},
         {"R10", &g_hub75_pin_r[9], _22_1665_ROLE_RED, 1U},
         {"G10", &g_hub75_pin_g[9], _22_1665_ROLE_GREEN, 1U}},
};

/* ================================================================
 *  CCMRAM 缓冲与实例
 *
 *  本模组 CCM 通式（模型 B）= chain_dst 1024·M + pixel_map 256·M + 帧状态 128·M
 *  + 合并写表 256·MODULE_COLS = **1408·M + 256·COLS**（1×1 = 1664B、8×4 = 46080B）。
 * ================================================================ */
typedef struct {
    dev_display_t me;
} dev_display_22_1665_t;

[[gnu::section(".ccmram")]] static uint8_t _22_1665_pixel_map[_22_1665_BUFFER_SIZE];

/* 发送缓存 = 帧状态数组（1B/时钟位/组，bit j = 组内第 j 条数据线该位的电平）——基类 hub75_buff
 * 指向它。布局 `state[组 g × CLOCKS + 时钟位 p]`，长度 = 组数 × CLOCKS = 128 × M（≠ 像素数）。 */
[[gnu::section(".ccmram")]] static uint8_t _22_1665_frame_state[_22_1665_FRAME_BITS];

/** 逐链段落点表：链段 (模块 m, 线 j) = 行号 m×4+j，链位 p' → 该位驱动的像素偏移 */
[[gnu::section(".ccmram")]] static uint16_t
    _22_1665_chain_dst[_22_1665_CHAIN_COUNT][_22_1665_SEGMENT_BITS];

/** 合并写表：[组][端口槽][4 位状态] → 该端口 32 位 BSRR 字（init 期生成，热路径只查表） */
[[gnu::section(".ccmram")]] static pl_hub75_bsrr_t
    _22_1665_bsrr_tab[_22_1665_GROUP_COUNT][_22_1665_GROUP_PORT_MAX][_22_1665_STATES];
static uint8_t _22_1665_bsrr_slot_cnt[_22_1665_GROUP_COUNT]; /* 各组件实际参与输出的端口数（≤ 2） */

static dev_display_22_1665_t g_22_1665 = {
    .me = {
        .ops                 = nullptr, /* 由 dev_display_22_1665_init 设置 */
        .module_rows         = _22_1665_MODULE_PIXEL_ROW,
        .module_cols         = _22_1665_MODULE_PIXEL_COL,
        .channels_per_module = 1,
        .modules_per_row     = _22_1665_MODULE_ROWS,
        .modules_per_col     = _22_1665_MODULE_COLS,
        .scan_lines          = 1, /* 静态单扫（无行址动作） */
        .screen_rows         = _22_1665_SCREEN_ROWS,
        .screen_cols         = _22_1665_SCREEN_COLS,
        .total_channels      = 1, /* scan 不走通道表，此二字段仅为基类语义占位 */
        .channel_pixels      = _22_1665_BUFFER_SIZE,
        .scan_line_pixels    = _22_1665_FRAME_BITS, /* 静态：整帧状态字节数 = 128 × 模块数 */
        .buffer_size         = _22_1665_BUFFER_SIZE,
        .pixel_map           = _22_1665_pixel_map,
        .hub75_buff          = _22_1665_frame_state,
        .module_code         = MODULE_CODE,
        .light_level         = DEV_DISPLAY_BRIGHTNESS_MAX,
    },
};

dev_display_t *dev_display_22_1665_get(void)
{
    return &g_22_1665.me;
}

/* ================================================================
 *  prepare: pixel_map → 帧状态数组
 *
 *  逐组（一个模块列 = 一个 HUB 口）填它那 CLOCKS 个时钟位；状态字节 bit j = 组内第 j 条
 *  数据线在该时钟位的电平（恒 4 位 / 16 状态）。链段 (m, j) 的像素来源 = 落点表。
 *  脏矩形只用来判「有无变化」：任何一次提交都整帧重算。
 * ================================================================ */
/** 颜色 → 该线是否点亮（[角色][颜色索引]；双色屏无蓝 die：蓝分量并入红绿，现场已验证） */
static const uint8_t _22_1665_role_bit[2][8] = {
    /* RED   */ {0U, 1U, 0U, 1U, 1U, 1U, 1U, 1U}, /* 黑/红/绿/黄/蓝/紫/青/白 */
    /* GREEN */ {0U, 0U, 1U, 1U, 1U, 1U, 1U, 1U},
};

static void _22_1665_prepare(dev_display_t *dev)
{
    if (dev->dirty_rect_valid && dev->dirty_rect_h == 0U)
        return; /* 空矩形：内容无变化，不重算 */

    const uint8_t *pixel_map = dev->pixel_map;
    uint8_t *state           = dev->hub75_buff; /* = _22_1665_frame_state */

    /* 链首口径：0（默认）= 组内最后一块装末 128 位（停链首）；1 = 组内第一块装末 128 位 */
    for (uint8_t g = 0U; g < _22_1665_GROUP_COUNT; g++) {
        const _22_1665_line_t *lines = _22_1665_lines[g]; /* 本组 4 根数据线（角色 / 半屏） */
        uint8_t *state_g             = &state[(uint32_t)g * _22_1665_CLOCKS];
        for (uint16_t local = 0U; local < _22_1665_GROUP_MODULE_COUNT; local++) {
            const uint16_t m = _22_1665_MODULE_OF_GROUP(g, local);
            uint8_t *state_m = &state_g[_22_1665_BLOCK_OF(local, _22_1665_GROUP_MODULE_COUNT) *
                                       _22_1665_SEGMENT_BITS];
            const uint16_t(*dst)[_22_1665_SEGMENT_BITS] = &_22_1665_chain_dst[m * _22_1665_LINE_COUNT];
            for (uint16_t p = 0; p < _22_1665_SEGMENT_BITS; p++) {
                uint8_t s = 0U;
                for (uint8_t j = 0; j < _22_1665_LINE_COUNT; j++) {
                    const uint8_t c = (uint8_t)(pixel_map[dst[j][p]] & 0x07U);
                    /* 该线该位点亮 → 置状态字节第 j 位（scan 据此驱动本组第 j 根数据脚） */
                    if (_22_1665_role_bit[lines[j].role][c] != 0U)
                        s = (uint8_t)(s | (uint8_t)(1U << j));
                }
                state_m[p] = s;
            }
        }
    }
}

/* ================================================================
 *  scan / set_row / ops 虚表
 *
 *  静态单扫 → line 恒 0，一次 scan 输出整帧：逐时钟位把各组状态写到各组参与端口
 *  （本板每时钟 4 次合并 BSRR 写：组 0 的 G/B + 组 1 的 B/E）、给建立裕量、再打 CLK 脉冲。
 *  先移入的位停在链尾，故按时钟位 p 递减遍历；帧末由基类在 OE 消隐窗口内打 LAT。
 * ================================================================ */

/** 数据建立裕量（数据脚与 CLK 不在同一 GPIO 端口，数据先于时钟建立；4×NOP ≈24ns @168MHz）；
 *  信号完整性问题（随机散点 / 随时间变化）时可加大到 8 / 16 个 NOP（见 doc/01 §1.3）。 */
#define _22_1665_BIT_MARGIN() \
    do {                      \
        __NOP();              \
        __NOP();              \
        __NOP();              \
        __NOP();              \
    } while (0)

/** @brief 把「组 g 的 4 位状态 st」写到该组全部参与端口（端口数是 init 期常数，展开 2 槽） */
[[gnu::always_inline]] static inline void _22_1665_flush_group(uint8_t g, uint8_t st)
{
    const pl_hub75_bsrr_t(*slots)[_22_1665_STATES] = _22_1665_bsrr_tab[g];
    pl_hub75_bsrr_flush(&slots[0][st]);
    if (_22_1665_bsrr_slot_cnt[g] > 1U)
        pl_hub75_bsrr_flush(&slots[1][st]);
#if _22_1665_GROUP_PORT_MAX > 2
    if (_22_1665_bsrr_slot_cnt[g] > 2U)
        pl_hub75_bsrr_flush(&slots[2][st]);
#endif
}

static inline void _22_1665_scan(dev_display_t *dev, uint8_t line)
{
    const uint8_t *state = dev->hub75_buff; /* 帧状态数组：1B/时钟位/组 */
    (void)line;                             /* 静态单扫：line 恒 0 */

    for (uint16_t p = _22_1665_CLOCKS; p-- > 0;) {
        for (uint8_t g = 0U; g < _22_1665_GROUP_COUNT; g++)
            _22_1665_flush_group(g, state[(uint32_t)g * _22_1665_CLOCKS + p]);
        _22_1665_BIT_MARGIN();
        pl_hub75_clock_pulse();
    }
}

/** @brief 静态单扫：行址恒 0（数据线都在 RGB 数据脚上，A/B/C/D 不被数据路径占用） */
static void _22_1665_set_row(uint8_t row)
{
    pl_hub75_Decoder_set_row(row);
}

/* ---- ops 虚表 ---- */
static const dev_display_ops_t _22_1665_ops = {
    .prepare = _22_1665_prepare,
    .scan    = _22_1665_scan,
    .set_row = _22_1665_set_row,
};

/* ================================================================
 *  dev_display_22_1665_init: 落点表 / 合并写表预计算 + 绑定 ops
 * ================================================================ */

/** @brief 链位 p' 的屏面落点 → pixel_map 偏移（落点规则见 `doc/01` §0.3） */
static uint16_t _22_1665_region_offset(uint16_t x0, uint16_t y0, uint16_t blk_cols, uint16_t p)
{
    const uint16_t c  = (uint16_t)(p / _22_1665_CHIP_BITS);
    const uint16_t i  = (uint16_t)(p % _22_1665_CHIP_BITS);
    const uint16_t bc = (uint16_t)((blk_cols - 1U) - (c % blk_cols));
    const uint16_t br = (uint16_t)(c / blk_cols);
    const uint16_t dx = (uint16_t)(3U - (i % 4U));
    const uint16_t dy = (uint16_t)(((i / 4U) + 2U) % 4U);
    return (uint16_t)((y0 + 4U * br + dy) * _22_1665_SCREEN_ROWS + (x0 + 4U * bc + dx));
}

/** @brief 逐链段建落点表：模块 m（行主序）的区域 + 线 j 的半屏 → 链段行号 m×4 + j
 *  （半屏模式取组 0 的 4 行 {0,0,1,1}：与组号无关，见「物理数据线」表） */
static void _22_1665_build_chain_dst(void)
{
    const uint16_t blk_cols = (uint16_t)(_22_1665_MODULE_PIXEL_ROW / _22_1665_BLK_W);

    for (uint16_t m = 0; m < _22_1665_MODULE_TOTAL; m++) {
        const uint16_t x0 = (uint16_t)((m % _22_1665_MODULE_ROWS) * _22_1665_MODULE_PIXEL_ROW);
        const uint16_t y0 = (uint16_t)((m / _22_1665_MODULE_ROWS) * _22_1665_MODULE_PIXEL_COL);
        for (uint8_t j = 0; j < _22_1665_LINE_COUNT; j++) {
            const uint16_t yj = (uint16_t)(y0 + _22_1665_lines[0][j].half * _22_1665_HALF_COLS);
            uint16_t *dst    = _22_1665_chain_dst[m * _22_1665_LINE_COUNT + j];
            for (uint16_t p = 0; p < _22_1665_SEGMENT_BITS; p++)
                dst[p] = _22_1665_region_offset(x0, yj, blk_cols, p);
        }
    }
}

/** @brief 建合并写表：逐组把 4 根线按 GPIO 端口分组，预计算「状态 → 端口 BSRR 字」
 *  （状态 bit = 1 表示该线点亮；数据 1 → 置位、0 → 复位，MBI 恒流下沉口径，现场已验证） */
static void _22_1665_build_bsrr_table(void)
{
    for (uint8_t g = 0U; g < _22_1665_GROUP_COUNT; g++) {
        GPIO_TypeDef *ports[_22_1665_GROUP_PORT_MAX];
        uint8_t line_slot[_22_1665_LINE_COUNT];
        uint8_t slot_cnt = 0U;

        /* ① 收集本组数据脚所在端口（首次出现即分配槽；超 GROUP_PORT_MAX 的线丢弃 = 该脚不写） */
        for (uint8_t j = 0; j < _22_1665_LINE_COUNT; j++) {
            line_slot[j]       = 0xFFU;
            GPIO_TypeDef *port = _22_1665_lines[g][j].line->port;
            uint8_t slot       = 0xFFU;
            for (uint8_t s = 0U; s < slot_cnt; s++) {
                if (ports[s] == port) {
                    slot = s;
                    break;
                }
            }
            if (slot == 0xFFU) {
                if (slot_cnt >= _22_1665_GROUP_PORT_MAX)
                    continue; /* 换板单组端口数 > GROUP_PORT_MAX 时加大该宏并补 flush 分支 */
                slot              = slot_cnt;
                ports[slot_cnt++] = port;
            }
            line_slot[j] = slot;
        }
        _22_1665_bsrr_slot_cnt[g] = slot_cnt;

        /* ② 逐状态：本组参与线「亮 → 置位、灭 → 复位」 */
        for (uint16_t st = 0U; st < _22_1665_STATES; st++) {
            for (uint8_t s = 0U; s < slot_cnt; s++) {
                _22_1665_bsrr_tab[g][s][st].port = ports[s];
                _22_1665_bsrr_tab[g][s][st].val  = 0U;
            }
            for (uint8_t j = 0U; j < _22_1665_LINE_COUNT; j++) {
                if (line_slot[j] == 0xFFU)
                    continue;
                const uint32_t mask = (uint32_t)_22_1665_lines[g][j].line->pin;
                const bool on       = ((st >> j) & 1U) != 0U;
                _22_1665_bsrr_tab[g][line_slot[j]][st].val |= on ? mask : (mask << 16U);
            }
        }
    }
}

void dev_display_22_1665_init(void)
{
    g_22_1665.me.ops = &_22_1665_ops;
    dev_display_register(&g_22_1665.me);

    _22_1665_build_chain_dst();  /* 落点表：区域由模块序号 + 线序号推导 */
    _22_1665_build_bsrr_table(); /* 合并写表：依赖数据线数据脚 */
}
hw_dev_initcall(dev_display_22_1665_init);
