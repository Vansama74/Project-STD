/**
 * @file    app_mm_proto_cmd.c
 * @brief   缅甸 2D 费显协议 — 帧解析 + 车道灯执行 + **干接点输出单一映射点**
 *
 * ① `mm_parse_frame`：定长 20 字节帧的字段抽取与命令字/取值判别（纯函数）；
 * ② `s_mm_lamp_map` + `_mm_drive()` / `mm_apply_lamp()` / `mm_apply_init()`：
 *    灯位 → 干接点输出的**唯一**映射点——改脚位/改有效电平/改继电器或光耦驱动
 *    **只改这两处**；极性（现行**高有效**，2026-09-28 现场实测翻转）与引脚表见下；
 * ③ `mm_arrow_map_toggle()`：0x0A ⇄ 0x0B 箭头落点运行时交换（出厂测试用，生效单点 =
 *    `mm_execute_cmd()` → `_mm_effective_lamp()`，红灯 0x90 不受影响、不掉电保存）。
 *
 * 引脚（2026-09-24 用户裁决，2026-09-28 修正极性）：红灯 = S1 = PE12 / 左下箭头 =
 * S2 = PE11 / 右下箭头 = S3 = PE10，复用原 SW1~SW3 端子、方向改**推挽输出**、
 * **高 = 点亮**（复位后红灯即亮 + PE12 实测 3.3V，文档 doc/16 §4.1）；接管顺序
 * （先写灭电平 → 摘该线 EXTI → 切输出 → 全灭 → 红灯点亮）保证无毛刺。
 * ⚠ 接管副作用（预期）：SW1~SW3 输入语义失效（灯灭读「闭合」/点亮读「未闭合」），
 * `PROTO=ALL` 同编构建下 RLS 干接点轮询可能据此渲染车道状态，量产排除 RLS 即无影响。
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

/* 现行极性 = **高有效**（active_low=false）：复位后红灯即亮 + PE12 实测 3.3V
 * ⇒ 3.3V = 点亮（2026-09-28 现场实测；推导见 doc/16 §4.1）。 */
static const mm_lamp_map_t s_mm_lamp_map[MM_LAMP_COUNT] = {
    [MM_LAMP_RED]   = {.contact = "S1", .lamp_name = "红灯",       .port = PL_PORT_E, .pin = 12, .active_low = false},
    [MM_LAMP_LEFT]  = {.contact = "S2", .lamp_name = "左下箭头灯", .port = PL_PORT_E, .pin = 11, .active_low = false},
    [MM_LAMP_RIGHT] = {.contact = "S3", .lamp_name = "右下箭头灯", .port = PL_PORT_E, .pin = 10, .active_low = false},
};

/** 影子状态（上电默认 = 红叉：红灯亮 / 两绿箭头灭，见 `mm_apply_init()`）。 */
static bool s_mm_lamp_state[MM_LAMP_COUNT];

/** 箭头落点交换开关（默认 false = 默认态：0x0A→左下 / 0x0B→右下）。
 *  由 `mm_arrow_map_toggle()` 翻转、`mm_apply_init()` 开机复位；**不持久化**。
 *  volatile：出厂测试任务写、帧处理任务读（bool 存取在 Cortex-M4 上原子）。 */
static volatile bool s_mm_arrow_swapped;


/**
 * @brief  按灯位描述驱动干接点（动作单一实现点；`mm_apply_init` 的预写也走本函数）。
 * @param  m 灯位映射项；@param on true=闭合（开灯）/ false=断开（关灯）。
 */
static void _mm_drive(const mm_lamp_map_t *m, bool on)
{
    /* **电平唯一真源**（禁止别处硬编码电平）：high = (on != m->active_low)，同一
     * 表达式同时覆盖高有效（现行）与低有效（历史口径）。 */
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
    s_mm_lamp_state[lamp] = on;

}

/**
 * @brief  上电初始化干接点输出层（**默认显示红叉**：红灯 S1/PE12 点亮、两个绿箭头熄灭）。
 *
 * 接管顺序见文件头（先置**灭电平** → 摘 EXTI → 切推挽输出 → 全灭 → 红灯点亮），
 * 全程无毛刺；灭电平由映射表推导（`_mm_drive(m, false)`，**不在本函数硬编码高低**）；
 * 红灯点亮走 `mm_apply_lamp()`（唯一动作点 ⇒ 影子状态一并更新）。
 * 「三路全灭」是 2026-09-24 的旧语义，2026-09-29 按用户需求改为**上电默认红叉**
 * （车道关闭态）；协议帧到达后照常按帧驱动。
 */
void mm_apply_init(void)
{
    /* 箭头落点交换开关复位为默认态（**不做掉电保存**：每次开机 0x0A→左下 / 0x0B→右下）。
     * 必须先于任何帧执行路径生效；本函数在任务建起前调用（mm_proto_init 内）。 */
    s_mm_arrow_swapped = false;

    for (unsigned i = 0U; i < (unsigned)MM_LAMP_COUNT; i++) {
        const mm_lamp_map_t *m = &s_mm_lamp_map[i];

        /* ① 先把输出数据寄存器写成「灭」电平（**由映射表推导**：高有效 ⇒ 低）——
         *    `_mm_drive` 是电平唯一真源，本处不硬编码。此时引脚仍是输入 + 上拉，
         *    写 ODR 不驱动引脚、不会产生毛刺；步骤 ③ 切成输出的瞬间即为「灭」。 */
        _mm_drive(m, false);
        /* ② 只摘本引脚那条 EXTI 线（不碰其它线、不碰 NVIC） */
        pl_gpio_release_exti(m->port, m->pin);
        /* ③ 切成推挽输出（无上下拉、低速）——此刻起真正驱动，电平 = 第 ① 步的「灭」 */
        pl_gpio_config_output(m->port, m->pin);
        /* ④ 影子状态 = 灭 */
        s_mm_lamp_state[i] = false;
    }

    /* ⑤ 上电默认显示红叉（用户需求 2026-09-29）：三路接管完成后把红灯 S1/PE12 点亮 =
     *    车道关闭态；两个绿箭头保持熄灭。只经 `mm_apply_lamp()`（唯一动作点）驱动，
     *    不新增电平硬编码路径、不破坏「接管无毛刺」顺序（①②③ 之后才点亮）。 */
    mm_apply_lamp(MM_LAMP_RED, true);

}

/* ==========================================================================
 * ①b 箭头落点运行时交换（出厂测试开关；不做持久化）
 * ========================================================================== */

/**
 * @brief  命令 → 灯位的「箭头落点交换」单点（**唯一生效点**）。
 *
 * 交换只在 0x0A / 0x0B 两条命令的落点之间进行（**红灯 0x90 不受影响**）：
 *   默认态：MM_LAMP_LEFT → 左下箭头（S2/PE11）／MM_LAMP_RIGHT → 右下箭头（S3/PE10）
 *   交换态：MM_LAMP_LEFT → 右下箭头（S3/PE10）／MM_LAMP_RIGHT → 左下箭头（S2/PE11）
 *
 * 交换的是「命令落点」而非映射表：`s_mm_lamp_map` / `_mm_drive()` / probe / 解析 / 队列
 * 全部零改动；`mm_apply_lamp()` 仍按**最终灯位**取表，日志因此如实反映实际驱动的灯与引脚。
 *
 * @param  lamp  `mm_parse_frame()` 得到的命令对应灯位。
 * @return 本次应按哪个灯位驱动（默认态原样返回）。
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
 * @brief  解析缅甸 2D 费显帧（纯函数）。
 *
 * 只做字段抽取与命令字判别：不作结束码/校验校验（文档未定义校验；结束码不校验的理由
 * 见 `app_mm_proto.c` 文件头 probe 注释）。
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
 * @brief  执行解析后的车道灯命令（帧处理任务调用；无应答、无显示、无语音）。
 *
 * 落点经 `_mm_effective_lamp()` 做「箭头落点交换」后驱动（红灯不受影响）；
 * 交换开关的语义与生命周期见 `mm_arrow_map_toggle()` / `mm_apply_init()`。
 */
void mm_execute_cmd(const mm_parsed_cmd_t *cmd)
{
    if (cmd == NULL || !cmd->parsed)
        return;

    mm_apply_lamp(_mm_effective_lamp(cmd->lamp), cmd->on);
}
