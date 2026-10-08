/**
 * @file    app_mm_proto_cmd.c
 * @brief   缅甸 2D 费显协议 — 帧解析 + 车道灯执行 + **干接点输出单一映射点**
 *
 * 本文件承载三件事：
 *   ① 定长 20 字节帧的字段抽取与命令字/取值判别（纯函数 `mm_parse_frame`）；
 *   ② **唯一**的「灯位 → 干接点输出动作」映射点（`s_mm_lamp_map` + `_mm_drive` /
 *      `mm_apply_lamp` / `mm_apply_init`）——改脚位/改有效电平/改成继电器或光耦驱动
 *      **只改这一处**（用户需求 4）；
 *   ③ **0x0A ⇄ 0x0B 箭头落点的运行时交换**（`mm_arrow_map_toggle()`，出厂测试用；
 *      生效单点 = `mm_execute_cmd()` → `_mm_effective_lamp()`，见下节说明）。
 *
 * ---------------------------------------------------------------------------
 * 箭头落点运行时交换（2026-09-28 新增；出厂测试用，不做掉电保存）
 * ---------------------------------------------------------------------------
 *   `mm_arrow_map_toggle()` 把 **0x0A / 0x0B 两条命令的干接点落点**互换：
 *     默认态：0x0A → 左下箭头（S2/PE11）／0x0B → 右下箭头（S3/PE10）
 *     交换态：0x0A → 右下箭头（S3/PE10）／0x0B → 左下箭头（S2/PE11）
 *   再按一次还原（可来回切）。**红灯 0x90 不受影响**；**开机复位为默认态**
 *   （`mm_apply_init()` 里清 `s_mm_arrow_swapped`）。
 *
 *   生效单点 = `mm_execute_cmd()` 里的 `_mm_effective_lamp()`（命令 → 灯位的最后一步）：
 *   probe 判据 / 帧解析 / 队列 / `s_mm_lamp_map` / `_mm_drive()` 极性表达式全部零改动；
 *   `mm_apply_lamp()` 仍按**最终灯位**取映射表，故 `[mm]` 执行日志如实反映实际驱动的灯
 *   与引脚（交换态下 0x0A 的日志就是 S3/PE10）。
 *
 * ---------------------------------------------------------------------------
 * 干接点 S1/S2/S3 引脚（2026-09-24 用户裁决；2026-09-28 现场实测修正极性）
 * ---------------------------------------------------------------------------
 *   红灯 = **S1 = PE12** ／ 左下箭头灯 = **S2 = PE11** ／ 右下箭头灯 = **S3 = PE10**；
 *   复用原 SW1/SW2/SW3 干接点端子，但**方向由输入改为输出**：
 *     · **推挽输出**（`GPIO_MODE_OUTPUT_PP`，无上下拉，低速档）；
 *     · **高电平有效**（**2026-09-28 现场实测翻转**，证据见 doc/16 §4.1：
 *       复位后红灯即亮 + 65 脚（PE12）实测 3.3V ⇒ **3.3V = 点亮**）：
 *       点亮 = 输出**高**，熄灭 = 输出**低**；
 *     · **上电/复位后三路全灭 = 三路输出低**（沿用同源 8051 `C1=C2=C3=0` 语义）；
 *     · 关灯 = **断开**（非脉冲）。
 *
 *   接管顺序（`mm_apply_init()`，避免模式切换毛刺）：
 *     ① 先写 ODR 为「灭电平 = 低」（**由映射表 `active_low` 推导**，即复用
 *        `_mm_drive(m, false)`，本文件不硬编码任何电平；此时引脚仍是输入 + 上拉，
 *        写 ODR 不驱动引脚 ⇒ 无毛刺，且保证步骤 ③ 切成输出的瞬间即为「灭」，
 *        不会先冲出一次「亮」）
 *     ② `pl_gpio_release_exti()` 摘掉该引脚的 EXTI 线（IMR/EMR/双边沿/PR/SYSCFG EXTICR）
 *     ③ `pl_gpio_config_output()` 切成推挽输出（此刻起真正驱动，电平 = 第 ① 步的「灭」）
 *     ④ 影子状态 = 全灭
 *
 *   ⚠ 步骤 ① 之前（复位 → `mm_proto_init`）：三脚仍是 `dev_key.c` 的**输入 + 内部上拉**
 *   （`Core/Src/gpio.c` 的 `SW1/SW2/SW3_Pin`，`GPIO_MODE_IT_FALLING` + `GPIO_PULLUP`）。
 *   高有效口径下该窗口外部呈**弱高**（是否足以点亮取决于灯板输入阻抗）——列为现场
 *   观察项，见 doc/16 §4.3。
 *
 *   ⚠ **EXTI 接管副作用（预期，非缺陷）**：PE10/PE11/PE12 原为 SW1/SW2/SW3 干接点
 *   **输入**（`Device/IO/dev_key.c`：`GPIO_MODE_IT_FALLING` + 内部上拉；`app_key` 消抖
 *   轮询与 RLS 干接点轮询在消费）。接管为输出后：
 *     · `dev_key_get_state()` **不会失效**（它是直读 GPIO）——但读的是**本板自己驱动
 *       的电平**：灯灭（输出高）→ 读为「未闭合」（一般无动作，安全）；**某灯点亮
 *       （输出低）→ 三个 SW 位中对应位读为「闭合」**，RLS 干接点轮询在
 *       `PROTO=ALL` 同编构建下可能据此渲染车道状态（ETC专用/ETC人工/车道关闭）。
 *       缅甸量产构建排除 RLS 与其它协议时无此副作用（见 doc/16 §4 与 doc/05-01 兼容矩阵）。
 *     · EXTI **不再产生中断**（IMR 已按线清掉）；TEST 键 PD8（`EXTI9_5`）与其它 EXTI
 *       线**未被触碰**（不调 `HAL_NVIC_DisableIRQ`，不做整条 IRQ 一刀切）。
 */

#include "app_mm_proto.h"

#include <string.h>

#include "app_diag.h"   /* APP_DIAG_BANNER（MM_RTT_DIAG 默认跟随） */
#include "pl_gpio.h"    /* pl_gpio_write / pl_gpio_config_output / pl_gpio_release_exti */
#include "SEGGER_RTT.h" /* 诊断打印（MM_RTT_DIAG）；置 0 时该头无引用 */

/**
 * 一键关闭（默认跟随总开关 `APP_DIAG_BANNER`，见 app_diag.h）：
 *   `make APP_DIAG=0` 或把 app_diag.h 的 APP_DIAG_BANNER 改 0 → 本文件 RTT 全消除
 *   （与工程其余模块的 `*_RTT_DIAG` 门控口径一致）。
 */
#ifndef MM_RTT_DIAG
#define MM_RTT_DIAG (APP_DIAG_BANNER)
#endif

/* ==========================================================================
 * ① 灯位 → 干接点映射（**唯一真源**；用户裁决：红灯=S1 / 左下箭头=S2 / 右下箭头=S3）
 * ========================================================================== */

/**
 * @brief  单个灯位的干接点描述。
 *
 * @note   改硬件（换脚位 / 改有效电平 / 改成继电器或光耦驱动）**只改本表 + `_mm_drive()`**。
 */
typedef struct {
    const char *contact;    /**< 干接点标识（现场端子丝印） */
    const char *lamp_name;  /**< 灯位人读名（诊断打印用） */
    pl_port_t   port;       /**< 端口（Platform 层抽象，不含芯片地址） */
    uint8_t     pin;        /**< 引脚号 0~15 */
    bool        active_low; /**< true = 低电平有效（点亮 = 输出低）；false = 高有效（点亮 = 输出高） */
} mm_lamp_map_t;

/* 现行极性 = **高有效**（`active_low = false`）：2026-09-28 现场实测翻转 ——
 * 复位后红灯即亮、PE12 实测 3.3V ⇒ 3.3V = 点亮（证据与推导见 doc/16 §4.1）。 */
static const mm_lamp_map_t s_mm_lamp_map[MM_LAMP_COUNT] = {
    [MM_LAMP_RED]   = {.contact = "S1", .lamp_name = "红灯",       .port = PL_PORT_E, .pin = 12, .active_low = false},
    [MM_LAMP_LEFT]  = {.contact = "S2", .lamp_name = "左下箭头灯", .port = PL_PORT_E, .pin = 11, .active_low = false},
    [MM_LAMP_RIGHT] = {.contact = "S3", .lamp_name = "右下箭头灯", .port = PL_PORT_E, .pin = 10, .active_low = false},
};

/** 影子状态（上电全灭 = 源 8051 `C1=C2=C3=0` 语义）；用于「本帧是否发生状态跳变」判别与诊断。 */
static bool s_mm_lamp_state[MM_LAMP_COUNT];

#if MM_RTT_DIAG
static bool s_mm_init_logged; /**< 上电（接管完成）日志只打一次 */

/** 端口名（诊断打印用，索引与 `pl_port_t` 一致）。 */
static const char *const s_mm_port_name[PL_PORT_MAX] = {
    [PL_PORT_A] = "PA", [PL_PORT_B] = "PB", [PL_PORT_C] = "PC", [PL_PORT_D] = "PD",
    [PL_PORT_E] = "PE", [PL_PORT_F] = "PF", [PL_PORT_G] = "PG", [PL_PORT_H] = "PH",
};
#endif

/**
 * @brief  按灯位描述驱动干接点（**动作单一实现点**）。
 * @param  m   灯位映射项。
 * @param  on  true=闭合（开灯）/ false=断开（关灯）。
 */
static void _mm_drive(const mm_lamp_map_t *m, bool on)
{
    /* **电平唯一真源**（`mm_apply_init` 的预写也走本函数，禁止别处硬编码电平）：
     *   high = (on != m->active_low)
     *   高有效（现行，active_low=false）：on=1 → high=1（点亮）✓；on=0 → high=0（熄灭）✓
     *   低有效（active_low=true，历史口径）：on=1 → high=0；on=0 → high=1 —— 同一表达式覆盖 */
    pl_gpio_write(m->port, m->pin, on != m->active_low);
}

/**
 * @brief  干接点输出单一映射点（帧处理任务调用）。
 *
 * @note   帧处理任务在 sw_app_initcall 建起；`mm_apply_init()` 已先完成引脚接管与全灭，
 *         因此本函数只做「按表驱动 + 更新影子状态 + （诊断开启时）打一行 RTT」。
 *
 * @param  lamp  目标灯位（红灯 / 左下箭头灯 / 右下箭头灯）。
 * @param  on    true=闭合（开灯）/ false=断开（关灯）。
 */
void mm_apply_lamp(mm_lamp_t lamp, bool on)
{
    if ((unsigned)lamp >= (unsigned)MM_LAMP_COUNT)
        return;

    const mm_lamp_map_t *m = &s_mm_lamp_map[lamp];
    const bool changed     = (s_mm_lamp_state[lamp] != on);
    _mm_drive(m, on);
    s_mm_lamp_state[lamp] = on;

#if MM_RTT_DIAG
    /* **每帧打印**（不是只在变化时打）：现场判「帧到底收到没有 / 输出到底动了没有」的
     * 第一现场证据 —— 重复发同一条命令也会留痕（命令级频度、天然低速率）；
     * 同值重复的行尾带 `[same]`，与真正发生跳变的行一眼可分。 */
    SEGGER_RTT_printf(0, "[mm] %s (%s) <- %s => %s%d %s (%s)%s\n", m->contact, m->lamp_name,
                      on ? "ON(开灯)" : "OFF(关灯)", s_mm_port_name[m->port], (int)m->pin,
                      (on != m->active_low) ? "HIGH" : "LOW",
                      m->active_low ? "低有效:低=点亮" : "高有效:高=点亮", changed ? "" : " [same]");
#else
    (void)m;
    (void)changed;
#endif
}

/**
 * @brief  上电初始化干接点输出层（三路灯全灭，源 8051 `C1=C2=C3=0` 语义）。
 *
 * 接管顺序见文件头（先置**灭电平** → 摘 EXTI → 切推挽输出 → 全灭），全程无毛刺；
 * 灭电平由映射表推导（`_mm_drive(m, false)`，**不在本函数硬编码高低**）；
 * `MM_RTT_DIAG` 开启时打一行「接管完成」摘要（仅一次）。
 */
void mm_apply_init(void)
{
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

#if MM_RTT_DIAG
    if (!s_mm_init_logged) {
        s_mm_init_logged = true;
        SEGGER_RTT_printf(0,
                          "[mm] dry contacts taken over: S1=PE12 / S2=PE11 / S3=PE10 "
                          "(push-pull, active-high, all OFF; EXTI released per-line)\n");
    }
#endif
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

    mm_apply_lamp(cmd->lamp, cmd->on);
}
