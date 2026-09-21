/**
 * @file    dev_display_22_1665.c
 * @brief   22-1665 模组派生类型 — 16x16 红绿双色 / 静态单扫 / 4 链 MBI5034B
 *
 * 料号 2200001665：列驱动 MBI5034B（16 通道恒流 + 16 位移位寄存器，SDO→SDI 级联），
 * 静态单扫（`scan_lines = 1`，不使用行址线）。实现 dev_display_ops：
 * prepare（pixel_map → 帧状态数组）+ scan（BSRR 查表输出）+ set_row（行址恒 0）。
 *
 * 几何（改屏体尺寸只改「一」的四个宏，`_Static_assert` 把关）：
 *   单模块 16x16 像素 = 256 像素（256 红 die + 256 绿 die = 512 颗 LED）；
 *   每模块 4 条链（上半 R/G、下半 R/G），每条链 = 8 片 × 16 位 = 128 位；
 *   屏面 = MODULE_ROWS × MODULE_PIXEL_ROW（宽） × MODULE_COLS × MODULE_PIXEL_COL（高）。
 *
 * 数据流：prepare 逐时钟位 p 合成状态字节 `frame_state[p]`（bit i = 第 i 条链该位的电平；
 * 红链取像素 R 位、绿链取 G 位，故黄 = 两条都亮）→ scan 逐位查合并写表 `g_bsrr_tab`
 * 按 GPIO 端口写 BSRR（本板 2 次）→ CLK 脉冲；帧末由基类在 OE 消隐窗口内打 LAT。
 *
 * 落点规则（现场反解、逐位确认，唯一模型）：
 *   链位 p → 片 c = p/16、片内级序 i = p%16；区域块栅格 blk_cols = w/4；
 *   bc = (blk_cols-1) - (c % blk_cols)、br = c / blk_cols；块内 dx = 3-(i%4)、
 *   dy = ((i/4)+2)%4；屏面 (X,Y) = (x0 + 4*bc + dx, y0 + 4*br + dy)。
 *
 * CCMRAM（本模组默认口径 **1792B**）：chain_dst 4×128×2 + pixel_map 256 + frame_state 128
 *   + g_bsrr_tab 3×16×8；改大 MODULE_ROWS/COLS 按比例增长（预算上限见「二」）。
 *
 * 开关宏：`_22_1665_BLUE_AS_LIT`（双色屏无蓝 die，蓝分量并入红绿）、
 *         `_22_1665_DATA_ACTIVE_HIGH`（数据极性：出现补色图案时改 0）。
 * 选编：显示模组三选一，由 Makefile `DISP=22_1665` / EIDE `excludeList` 表达（本文件不含选编开关）。
 *
 * 引脚定标与历史轮次见 `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`（§0 = as-built、
 * 附录 A = 历史）；标定用旧驱动与探针脚本已归档 `.analysis/22_1665/archive/prev_round/`，
 * 如需再标定（换屏 / 换接线）从那里取回。
 */

#include "dev_display.h"

#include "initcall.h"

/* ================================================================
 *  一、模组参数（改屏体尺寸只改本节四个宏）
 * ================================================================ */
#define _22_1665_MODULE_CODE "2200001665"

/* ---- 屏体尺寸（**唯一几何入口**；可 `-D` 覆盖，改动后由「二」的断言把关）---- */
#ifndef _22_1665_MODULE_ROWS
#define _22_1665_MODULE_ROWS (1U) /* 每行模块数（水平）→ 屏宽 = MODULE_ROWS × MODULE_PIXEL_ROW */
#endif
#ifndef _22_1665_MODULE_COLS
#define _22_1665_MODULE_COLS (1U) /* 每列模块数（垂直）→ 屏高 = MODULE_COLS × MODULE_PIXEL_COL */
#endif
#ifndef _22_1665_MODULE_PIXEL_ROW
#define _22_1665_MODULE_PIXEL_ROW (16U) /* 单模块每行像素数（16×16 面板；块栅格 4×4 对齐） */
#endif
#ifndef _22_1665_MODULE_PIXEL_COL
#define _22_1665_MODULE_PIXEL_COL (16U) /* 单模块每列像素数 */
#endif

/* ---- 面板硬件常量（与拼屏尺寸无关）---- */
#define _22_1665_CHIP_BITS         (16U) /* 一片的位宽（MBI5034B 16 位移位寄存器） */
#define _22_1665_CHIPS             (8U)  /* 每条链的片数：8 × 16 = 128 位 */
#define _22_1665_CHAINS_PER_MODULE (4U)  /* 每模块 4 条链：上半 R/G + 下半 R/G */
#define _22_1665_BLK_W             (4U)  /* 块宽（像素）：一片 16 路输出铺满一个 4×4 块 */
#define _22_1665_BLK_H             (4U)  /* 块高（像素） */

/* ---- 派生参数（由上面宏计算，勿手改）---- */
#define _22_1665_SCREEN_ROWS (_22_1665_MODULE_ROWS * _22_1665_MODULE_PIXEL_ROW) /* 屏宽 = 每行像素数 */
#define _22_1665_SCREEN_COLS (_22_1665_MODULE_COLS * _22_1665_MODULE_PIXEL_COL) /* 屏高 = 每列像素数 */
#define _22_1665_BUFFER_SIZE (_22_1665_SCREEN_ROWS * _22_1665_SCREEN_COLS)      /* 像素数（1B/像素） */
#define _22_1665_HALF_COLS   (_22_1665_MODULE_PIXEL_COL / 2U)                   /* 半屏高 = 单链区域高 */
#define _22_1665_FRAME_BITS  (_22_1665_CHIPS * _22_1665_CHIP_BITS)              /* 每链每帧位数 = 128 */

/* 链数 = 每模块 4 条 × 模块数；合并写表状态数 = 2^链数（状态字节 uint8_t ≤ 256） */
#define _22_1665_CHAIN_COUNT \
    (_22_1665_CHAINS_PER_MODULE * _22_1665_MODULE_ROWS * _22_1665_MODULE_COLS)
#define _22_1665_PORT_MAX (3U) /* 数据脚分布的端口槽数（本板 PG + PB 两个，取 3 留余量） */
#define _22_1665_STATES \
    ((uint16_t)(1U << ((_22_1665_CHAIN_COUNT <= 8U) ? _22_1665_CHAIN_COUNT : 8U)))

/* ---- 行为开关（可命令行 `-D` 覆盖）---- */

/** 数据极性：1（默认）= 链上 1 → 数据脚高（MBI 系列恒流下沉口径：数据 1 点亮）；
 *  0 = 反相（该亮的灭、该灭的亮）。全部数据脚共用同一极性（出现补色图案时改这里）。 */
#ifndef _22_1665_DATA_ACTIVE_HIGH
#define _22_1665_DATA_ACTIVE_HIGH (1U)
#endif

/** 双色屏无蓝 die：蓝分量按「非黑即亮」处理 —— 1（默认）= 蓝 bit 置位时红、绿两条链都点亮
 *  （蓝/紫/青/白 → 红+绿）；0 = 丢弃蓝分量（紫 → 只红、青 → 只绿；白仍为红+绿）。 */
#ifndef _22_1665_BLUE_AS_LIT
#define _22_1665_BLUE_AS_LIT (1U)
#endif

/* ================================================================
 *  二、编译期防御（口径自相矛盾即编译报错，不留到实机）
 * ================================================================ */
_Static_assert(_22_1665_MODULE_ROWS >= 1U && _22_1665_MODULE_COLS >= 1U, "模块数须 >= 1");
_Static_assert((_22_1665_MODULE_PIXEL_ROW % _22_1665_BLK_W) == 0U, "单模块宽须为块宽 4 的倍数");
_Static_assert((_22_1665_MODULE_PIXEL_COL % 2U) == 0U, "单模块高须为偶数（分上/下半屏）");
_Static_assert(_22_1665_HALF_COLS >= _22_1665_BLK_H && (_22_1665_HALF_COLS % _22_1665_BLK_H) == 0U,
               "半屏高须 >= 4 且为块高 4 的倍数");
_Static_assert((_22_1665_MODULE_PIXEL_ROW / _22_1665_BLK_W) *
                       (_22_1665_HALF_COLS / _22_1665_BLK_H) ==
                   _22_1665_CHIPS,
               "每条链区域须恰好 8 块（128 位）：单模块须为 256 像素（16×16 / 32×8 …）");
_Static_assert(_22_1665_CHAIN_COUNT >= 1U && _22_1665_CHAIN_COUNT <= 8U,
               "链数只能 1..8（状态字节 uint8_t；每模块 4 条）");
_Static_assert(_22_1665_STATES <= 256U, "状态数超 uint8_t（链数须 <= 8）");
_Static_assert(_22_1665_BUFFER_SIZE <= 65535U, "落点表偏移为 uint16_t（像素数不得超 65535）");
_Static_assert(_22_1665_FRAME_BITS <= 65535U, "帧位数为 uint16_t 循环上界");
_Static_assert(_22_1665_PORT_MAX >= 2U, "数据脚至少跨 2 个 GPIO 端口（本板 PG + PB）");

/* ================================================================
 *  三、链（硬件事实表）—— 行号 = 链号，每模块 4 条：上半 R/G + 下半 R/G
 *
 *  · line = 该链数据脚（`g_hub75_pin_*` 表项；换板只改 Core/Inc/main.h）
 *  · role = 该链驱动的 die 颜色（RED 取像素 bit0 / GREEN 取 bit1）
 *  链的区域 (x0,y0,w,h) 不入表：由链号 + 模块栅格 + 半屏高在「七」推导。
 *
 *  现场定标（2026-09-17 逐脚实测）= R1(上半红) / G1(上半绿) / R2(下半红) / G2(下半绿)；
 *  B1/B2/A/B/C/D 六根实测无链，不占用。模块按行主序排布（先 MODULE_ROWS 方向）；
 *  扩屏（MODULE_ROWS/COLS > 1）须按模块顺序补行，否则下面的 `_Static_assert` 报错。
 * ================================================================ */
#define _22_1665_ROLE_RED   (0U)
#define _22_1665_ROLE_GREEN (1U)

typedef struct {
    const char *name;        /* 链名（诊断 / 文档用） */
    const hub75_pin_t *line; /* 数据脚 */
    uint8_t role;            /* _22_1665_ROLE_RED / _22_1665_ROLE_GREEN */
} _22_1665_chain_t;

static const _22_1665_chain_t _22_1665_chains[] = {
    {"R1", &g_hub75_pin_r[0], _22_1665_ROLE_RED},   /* 模块 0 上半 红（PG9） */
    {"G1", &g_hub75_pin_g[0], _22_1665_ROLE_GREEN}, /* 模块 0 上半 绿（PG10） */
    {"R2", &g_hub75_pin_r[1], _22_1665_ROLE_RED},   /* 模块 0 下半 红（PG15） */
    {"G2", &g_hub75_pin_g[1], _22_1665_ROLE_GREEN}, /* 模块 0 下半 绿（PB6） */
};
_Static_assert(sizeof(_22_1665_chains) / sizeof(_22_1665_chains[0]) == _22_1665_CHAIN_COUNT,
               "链表行数须 = 4 × MODULE_ROWS × MODULE_COLS（每模块 4 条链：上半 R/G + 下半 R/G）");

/* ================================================================
 *  四、CCMRAM 缓冲与实例
 * ================================================================ */
typedef struct {
    dev_display_t me;
} dev_display_22_1665_t;

[[gnu::section(".ccmram")]] static uint8_t _22_1665_pixel_map[_22_1665_BUFFER_SIZE];

/* 发送缓存 = 帧状态数组（1B/时钟位，bit i = 第 i 条链该位的电平）——基类 hub75_buff 指向它。
 * 长度 = FRAME_BITS（128），与 buffer_size（像素数 256）不同：scan 只按 FRAME_BITS 访问。 */
[[gnu::section(".ccmram")]] static uint8_t _22_1665_frame_state[_22_1665_FRAME_BITS];

/** 逐链落点表：链 i 的链位 p → 该位驱动的像素在 pixel_map（行主序）中的偏移 */
[[gnu::section(".ccmram")]] static uint16_t _22_1665_chain_dst[_22_1665_CHAIN_COUNT][_22_1665_FRAME_BITS];

/** 合并写表：[端口槽][数据脚状态] → 该端口 32 位 BSRR 字（init 期生成，scan 热路径只查表） */
[[gnu::section(".ccmram")]] static pl_hub75_bsrr_t g_bsrr_tab[_22_1665_PORT_MAX][_22_1665_STATES];
static uint8_t g_bsrr_port_cnt; /* 实际参与输出的端口槽数（≤ PORT_MAX） */

/** 本模组 CCMRAM 预算（CCMRAM 共 64KB，与 CQ / 贵州治超 / 云南治超队列共用） */
#define _22_1665_CCM_BYTES                                                                     \
    (sizeof(uint16_t) * _22_1665_CHAIN_COUNT * _22_1665_FRAME_BITS +                           \
     (uint32_t)_22_1665_BUFFER_SIZE + _22_1665_FRAME_BITS +                                    \
     sizeof(pl_hub75_bsrr_t) * _22_1665_PORT_MAX * _22_1665_STATES)
_Static_assert(_22_1665_CCM_BYTES <= 16384U, "本模组 CCMRAM 超 16KB 预算（改小 MODULE_ROWS/COLS）");

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
        .scan_line_pixels    = _22_1665_FRAME_BITS, /* 静态：整屏一次 = 每链 128 位 */
        .buffer_size         = _22_1665_BUFFER_SIZE,
        .pixel_map           = _22_1665_pixel_map,
        .hub75_buff          = _22_1665_frame_state,
        .module_code         = _22_1665_MODULE_CODE,
        .light_level         = DEV_DISPLAY_BRIGHTNESS_MAX,
    },
};

dev_display_t *dev_display_22_1665_get(void)
{
    return &g_22_1665.me;
}

/* ================================================================
 *  五、prepare：pixel_map → 帧状态数组
 *
 *  逐时钟位把 N 条链的电平拼成一个状态字节（bit i = 第 i 条链）：
 *      st[p] = Σ_i ( 该链像素的颜色分量位 << i )      红链取 R 位、绿链取 G 位
 *  脏矩形只用来判「有无变化」：任何一次提交都整帧重算 128 位（≈数 µs，远小于 500µs 帧周期），
 *  避免「链位散落全屏」时的反查复杂度。
 * ================================================================ */
#if _22_1665_BLUE_AS_LIT
/** 颜色 → 该链是否点亮（[角色][颜色索引]；双色屏无蓝 die：蓝分量并入红绿） */
static const uint8_t _22_1665_role_bit[2][8] = {
    /* RED   */ {0U, 1U, 0U, 1U, 1U, 1U, 1U, 1U}, /* 黑/红/绿/黄/蓝/紫/青/白 */
    /* GREEN */ {0U, 0U, 1U, 1U, 1U, 1U, 1U, 1U},
};
#else
/** `_22_1665_BLUE_AS_LIT = 0`：丢弃蓝分量（紫 → 只红、青 → 只绿、白仍红+绿） */
static const uint8_t _22_1665_role_bit[2][8] = {
    /* RED   */ {0U, 1U, 0U, 1U, 0U, 1U, 0U, 1U},
    /* GREEN */ {0U, 0U, 1U, 1U, 0U, 0U, 1U, 1U},
};
#endif

static void _22_1665_prepare(dev_display_t *dev)
{
    if (dev->dirty_rect_valid && dev->dirty_rect_h == 0U)
        return; /* 空矩形：内容无变化，不重算 */

    const uint8_t *pixel_map = dev->pixel_map;
    uint8_t *state           = dev->hub75_buff; /* = _22_1665_frame_state */

    for (uint16_t p = 0; p < _22_1665_FRAME_BITS; p++) {
        uint8_t s = 0U;
        for (uint8_t i = 0; i < _22_1665_CHAIN_COUNT; i++) {
            const uint8_t c = (uint8_t)(pixel_map[_22_1665_chain_dst[i][p]] & 0x07U);
            /* 该链该位点亮 → 置状态字节第 i 位（scan 据此驱动该链的数据脚） */
            if (_22_1665_role_bit[_22_1665_chains[i].role][c] != 0U)
                s = (uint8_t)(s | (uint8_t)(1U << i));
        }
        state[p] = s;
    }
}

/* ================================================================
 *  六、scan / set_row / ops 虚表
 *
 *  静态单扫 → line 恒 0，一次 scan 输出整帧：逐时钟位把状态字节写到全部参与端口
 *  （每端口一次合并 BSRR 写，本板 2 次）、给建立裕量、再打 CLK 脉冲。先移入的位停在
 *  链尾，故按链位 p 递减遍历；帧末由基类在 OE 消隐窗口内打 LAT。
 * ================================================================ */

/** 数据建立裕量（数据脚与 CLK 不在同一 GPIO 端口，数据先于时钟建立；4×NOP ≈24ns @168MHz）。
 *  现场判「随机散点 / 随时间变化」时加大到 8 / 16 个 NOP（帧时长 25~35µs → 60~80µs）。 */
#define _22_1665_BIT_MARGIN() \
    do {                      \
        __NOP();              \
        __NOP();              \
        __NOP();              \
        __NOP();              \
    } while (0)

/** @brief 把「数据脚状态 st」写到全部参与端口（端口数是 init 期常数，展开 3 槽省循环开销） */
[[gnu::always_inline]] static inline void _22_1665_flush_state(uint8_t st)
{
    pl_hub75_bsrr_flush(&g_bsrr_tab[0][st]);
    if (g_bsrr_port_cnt > 1U)
        pl_hub75_bsrr_flush(&g_bsrr_tab[1][st]);
    if (g_bsrr_port_cnt > 2U)
        pl_hub75_bsrr_flush(&g_bsrr_tab[2][st]);
}

static inline void _22_1665_scan(dev_display_t *dev, uint8_t line)
{
    const uint8_t *state = dev->hub75_buff; /* 帧状态数组：1B/时钟位 */
    (void)line;                             /* 静态单扫：line 恒 0 */

    for (uint16_t p = _22_1665_FRAME_BITS; p-- > 0;) {
        _22_1665_flush_state(state[p]);
        _22_1665_BIT_MARGIN();
        pl_hub75_clock_pulse();
    }
}

/** @brief 静态单扫：行址恒 0（四条链都在 RGB 数据脚上，A/B/C/D 不被数据路径占用） */
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
 *  七、init：落点表 + 合并写表 + 绑定 ops
 * ================================================================ */

/** @brief 链位 p 的屏面落点 → pixel_map 偏移（默认落点规则，见文件头） */
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

/** @brief 逐链建落点表：链 i 的模块位 = i/4、半屏位 = (i%4)/2（区域 = 该模块的上/下半屏） */
static void _22_1665_build_chain_dst(void)
{
    const uint16_t blk_cols = (uint16_t)(_22_1665_MODULE_PIXEL_ROW / _22_1665_BLK_W);

    for (uint8_t i = 0; i < _22_1665_CHAIN_COUNT; i++) {
        const uint8_t module = (uint8_t)(i / _22_1665_CHAINS_PER_MODULE);
        const uint8_t half   = (uint8_t)((i % _22_1665_CHAINS_PER_MODULE) / 2U);
        const uint16_t x0 =
            (uint16_t)((module % _22_1665_MODULE_ROWS) * _22_1665_MODULE_PIXEL_ROW);
        const uint16_t y0 = (uint16_t)((module / _22_1665_MODULE_ROWS) * _22_1665_MODULE_PIXEL_COL +
                                       half * _22_1665_HALF_COLS);
        for (uint16_t p = 0; p < _22_1665_FRAME_BITS; p++)
            _22_1665_chain_dst[i][p] = _22_1665_region_offset(x0, y0, blk_cols, p);
    }
}

/** @brief 建合并写表：数据脚按 GPIO 端口分组，预计算「状态 → 该端口 BSRR 字」 */
static void _22_1665_build_bsrr_table(void)
{
    GPIO_TypeDef *ports[_22_1665_PORT_MAX];
    uint8_t chain_slot[_22_1665_CHAIN_COUNT];
    uint8_t port_cnt = 0U;

    /* ① 收集数据脚所在端口（首次出现即分配槽；超 PORT_MAX 的链丢弃 = 该脚不写） */
    for (uint8_t i = 0; i < _22_1665_CHAIN_COUNT; i++) {
        chain_slot[i]      = 0xFFU;
        GPIO_TypeDef *port = _22_1665_chains[i].line->port;
        uint8_t slot       = 0xFFU;
        for (uint8_t s = 0; s < port_cnt; s++) {
            if (ports[s] == port) {
                slot = s;
                break;
            }
        }
        if (slot == 0xFFU) {
            if (port_cnt >= _22_1665_PORT_MAX)
                continue; /* 换板端口数 > PORT_MAX 时加大 `_22_1665_PORT_MAX` 并补 flush 槽 */
            slot              = port_cnt;
            ports[port_cnt++] = port;
        }
        chain_slot[i] = slot;
    }

    /* ② 逐状态：参与脚「亮 → 置位、灭 → 复位」（极性由 DATA_ACTIVE_HIGH 决定） */
    for (uint16_t st = 0U; st < _22_1665_STATES; st++) {
        for (uint8_t s = 0; s < port_cnt; s++) {
            g_bsrr_tab[s][st].port = ports[s];
            g_bsrr_tab[s][st].val  = 0U;
        }
        for (uint8_t i = 0; i < _22_1665_CHAIN_COUNT; i++) {
            if (chain_slot[i] == 0xFFU)
                continue;
            const uint32_t mask = (uint32_t)_22_1665_chains[i].line->pin;
            const bool on = (((st >> i) & 1U) != 0U) ^ (_22_1665_DATA_ACTIVE_HIGH == 0U);
            g_bsrr_tab[chain_slot[i]][st].val |= on ? mask : (mask << 16U);
        }
    }

    g_bsrr_port_cnt = port_cnt; /* 有数据脚的槽必然非空（每槽至少一条链，状态里会点亮它） */
}

void dev_display_22_1665_init(void)
{
    g_22_1665.me.ops = &_22_1665_ops;
    dev_display_register(&g_22_1665.me);

    _22_1665_build_chain_dst();  /* 落点表：区域由链号 + 模块栅格推导 */
    _22_1665_build_bsrr_table(); /* 合并写表：依赖链数据脚 */
}
hw_dev_initcall(dev_display_22_1665_init);
