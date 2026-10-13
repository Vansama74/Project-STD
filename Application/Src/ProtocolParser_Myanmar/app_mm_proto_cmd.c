/**
 * @file    app_mm_proto_cmd.c
 * @brief   缅甸 2D 费显协议 — 帧解析 + 干接点输出（唯一映射点）
 *
 * 契约一览见 app_mm_proto.h 文件头（本文件实现 ① 极性/上电红叉、② 接管顺序）；
 * 改脚位/极性/驱动方式只改 `s_mm_lamp_map` + `_mm_drive()`；副作用（SW1~SW3 输入语义
 * 失效 ⇒ RLS 显示「车道关闭」）详见 doc/16_缅甸2D费显协议/README.md §4.3
 */

#include "app_mm_proto.h"

#include <string.h>

#include "pl_gpio.h"    /* pl_gpio_write / pl_gpio_config_output / pl_gpio_release_exti */

/* ===== ① 灯位 → 干接点映射（唯一真源；红灯=S1 / 左下箭头=S2 / 右下箭头=S3）===== */

/**
 * @brief  单个灯位的干接点描述。
 * @note   改硬件（换脚位 / 改有效电平 / 换继电器或光耦）**只改本表 + `_mm_drive()`**。
 */
typedef struct {
    const char *contact;    /**< 干接点标识（现场端子丝印） */
    const char *lamp_name;  /**< 灯位人读名（现场对上号用） */
    pl_port_t   port;       /**< 端口（Platform 层抽象，不含芯片地址） */
    uint8_t     pin;        /**< 引脚号 0~15 */
    bool        active_low; /**< true = 低电平有效（点亮 = 输出低）；false = 高有效（点亮 = 输出高） */
} mm_lamp_map_t;

/* 极性 = 高有效（active_low=false，契约 ①）：3.3V = 点亮（推导见
 * doc/16_缅甸2D费显协议/README.md §4.1）。 */
static const mm_lamp_map_t s_mm_lamp_map[MM_LAMP_COUNT] = {
    [MM_LAMP_RED]   = {.contact = "S1", .lamp_name = "红灯",       .port = PL_PORT_E, .pin = 12, .active_low = false},
    [MM_LAMP_LEFT]  = {.contact = "S2", .lamp_name = "左下箭头灯", .port = PL_PORT_E, .pin = 11, .active_low = false},
    [MM_LAMP_RIGHT] = {.contact = "S3", .lamp_name = "右下箭头灯", .port = PL_PORT_E, .pin = 10, .active_low = false},
};

/** 箭头落点交换开关（默认 false = 默认态：0x0A→左下 / 0x0B→右下）。
 *  由 `mm_arrow_map_toggle()` 翻转、`mm_apply_init()` 开机复位；**不持久化**。
 *  volatile：出厂测试任务写、帧处理任务读（bool 存取在 Cortex-M4 上原子）。 */
static volatile bool s_mm_arrow_swapped;


/**
 * @brief  按灯位描述驱动干接点（动作单一实现点；`mm_apply_init` 的预写也走本函数）。
 * @param  m   灯位映射项。
 * @param  on  true=闭合（开灯）/ false=断开（关灯）。
 */
static void _mm_drive(const mm_lamp_map_t *m, bool on)
{
    /* 电平唯一真源（禁止别处硬编码）：high = on != m->active_low（兼容两种极性口径） */
    pl_gpio_write(m->port, m->pin, on != m->active_low);
}

/**
 * @brief  干接点输出单一映射点（帧处理任务调用；引脚接管已在 `mm_apply_init()` 完成）。
 * @param  lamp 目标灯位（红灯 / 左下箭头灯 / 右下箭头灯）。
 * @param  on   true=闭合（开灯）/ false=断开（关灯）。
 */
void mm_apply_lamp(mm_lamp_t lamp, bool on)
{
    if ((unsigned)lamp >= (unsigned)MM_LAMP_COUNT)
        return;

    const mm_lamp_map_t *m = &s_mm_lamp_map[lamp];
    _mm_drive(m, on);
}

/**
 * @brief  干接点输出层上电初始化：接管三路为推挽输出，**默认显示红叉 = 红灯 S1/PE12 点亮**。
 * @note   顺序（无毛刺）= 写灭电平（由映射表推导，不硬编码）→ 按线摘 EXTI → 切推挽输出 →
 *         点亮红灯（唯一动作点 `mm_apply_lamp()`）；协议帧到达后照常按帧驱动
 *         （doc/16_缅甸2D费显协议/README.md §14.2）。
 */
void mm_apply_init(void)
{
    /* 箭头落点交换开关复位为默认态（**不做掉电保存**：每次开机 0x0A→左下 / 0x0B→右下）。
     * 必须先于任何帧执行路径生效；本函数在任务建起前调用（mm_proto_init 内）。 */
    s_mm_arrow_swapped = false;

    for (unsigned i = 0U; i < (unsigned)MM_LAMP_COUNT; i++) {
        const mm_lamp_map_t *m = &s_mm_lamp_map[i];

        /* ① 先写「灭」电平（由映射表推导，不硬编码）：引脚仍为输入+上拉，写 ODR 不驱动引脚 */
        _mm_drive(m, false);
        /* ② 只摘本引脚那条 EXTI 线（不碰其它线、不碰 NVIC） */
        pl_gpio_release_exti(m->port, m->pin);
        /* ③ 切成推挽输出（无上下拉、低速）——此刻起真正驱动，电平 = 第 ① 步的「灭」 */
        pl_gpio_config_output(m->port, m->pin);
    }

    /* ④ 上电默认红叉：三路接管完成后点亮红灯 = 车道关闭态；只经唯一动作点
     *    `mm_apply_lamp()`（不新增电平硬编码、不破坏无毛刺顺序，详见
     *    doc/16_缅甸2D费显协议/README.md §14.2）。 */
    mm_apply_lamp(MM_LAMP_RED, true);

}

/* ==========================================================================
 * ①b 箭头落点运行时交换（出厂测试开关；不做持久化）
 * ========================================================================== */

/**
 * @brief  命令 → 灯位的「箭头落点交换」唯一生效点（红灯原样返回）。
 * @param  lamp  `mm_parse_frame()` 得到的灯位。
 * @return 本次应按哪个灯位驱动（默认态原样返回；交换态 LEFT ⇄ RIGHT，语义详见
 *         doc/16_缅甸2D费显协议/README.md §13.2）。
 */
static mm_lamp_t _mm_effective_lamp(mm_lamp_t lamp)
{
    if (!s_mm_arrow_swapped)
        return lamp;
    if (lamp == MM_LAMP_LEFT)
        return MM_LAMP_RIGHT;
    if (lamp == MM_LAMP_RIGHT)
        return MM_LAMP_LEFT;
    return lamp; /* 红灯（0x90）不参与交换 */
}

bool mm_arrow_map_is_swapped(void)
{
    return s_mm_arrow_swapped;
}

bool mm_arrow_map_toggle(void)
{
    s_mm_arrow_swapped = !s_mm_arrow_swapped;


    return s_mm_arrow_swapped;
}

/* ==========================================================================
 * ② 帧解析（纯函数）
 * ========================================================================== */

/**
 * @brief  解析定长帧的字段与车道灯命令（纯函数；不校验结束码、无 XOR——文档未定义校验，
 *         详见 doc/16_缅甸2D费显协议/README.md §6）。
 */
mm_parsed_cmd_t mm_parse_frame(const uint8_t *raw, uint16_t raw_len)
{
    mm_parsed_cmd_t out = {
        .parsed = false,
        .cmd    = MM_CMD_OTHER,
        .lamp   = MM_LAMP_RED,
        .on     = false,
    };

    /* probe 已保证长度与结构，这里再护一道（纯函数须自洽，便于宿主推演直接调用） */
    if (raw == NULL || raw_len != MM_FRAME_LEN)
        return out;
    if (raw[0] != MM_FRAME_HEAD || raw[1] != MM_FRAME_ADDR)
        return out;

    const uint8_t cmd_byte = raw[MM_CMD_OFFSET];
    const uint8_t arg      = raw[MM_ARG_OFFSET];

    switch (cmd_byte) {
        case (uint8_t)MM_CMD_LANE_RED: out.lamp = MM_LAMP_RED; break;
        case (uint8_t)MM_CMD_LANE_LEFT: out.lamp = MM_LAMP_LEFT; break;
        case (uint8_t)MM_CMD_LANE_RIGHT: out.lamp = MM_LAMP_RIGHT; break;
        default:
            /* 非本模块命令字：probe 已 SKIP，不会进队列；此处兜底返回未解析 */
            return out;
    }

    out.parsed = true;
    out.cmd    = (mm_cmd_t)cmd_byte;
    out.on     = (arg == 0x01U);
    return out;
}

/**
 * @brief  执行解析后的车道灯命令（无应答、无显示、无语音）。
 * @note   落点先经 `_mm_effective_lamp()` 交换单点再驱动（红灯不受影响，详见
 *         doc/16_缅甸2D费显协议/README.md §13）。
 */
void mm_execute_cmd(const mm_parsed_cmd_t *cmd)
{
    if (cmd == NULL || !cmd->parsed)
        return;

    mm_apply_lamp(_mm_effective_lamp(cmd->lamp), cmd->on);
}
