/**
 * @file    app_mm_proto.h
 * @brief   缅甸 2D 费显协议（车道灯开关量控制）公共定义
 *
 * 协议原文：`2D缅甸费显协议.doc`（3 页）。实读要点：RS232 / 9600 / 8N1；
 * 帧 = 起始码 0xFF + 机器地址 0xE0 + 命令方式字 1B + 命令内容 12B(DATA0~11) +
 * 结束码 5B ⇒ **整帧定长 20 字节**（字节序 [0]=0xFF、[1]=0xE0、[2]=命令字、
 * [3..14]=DATA0~11、[15..19]=结束码 00×5；本模块只读 DATA0=[3]）。
 *
 * 命令方式字（文档仅列 5 个）：0x10 数码管 / 0x40 亮度 / 0x80 黄闪报警 /
 * 0x90 红色车道灯 / 0x0A 车道灯（文档标题与注文自相矛盾）。
 * **本模块只解析车道灯开关量指令**（用户需求），其余命令结构合法即整帧 SKIP 消费。
 *
 * 用户裁决的灯位映射：`0x90→红灯` / `0x0A→左下箭头` / `0x0B→右下箭头`（DATA0：1 开 / 0 关）。
 * ⚠ 文档没有 0x0B（0x0A 例 2 后即结束），0x0A 标题/注文不一致——按用户口径实现，
 * 待现场确认项见 doc/16 §8。⚠ 箭头落点可运行时交换（`mm_arrow_map_toggle()`，出厂测试用，
 * 红灯不受影响、不掉电保存，见 doc/16 §13）。
 *
 * 开关量输出层：红灯 = S1 = PE12 / 左下箭头 = S2 = PE11 / 右下箭头 = S3 = PE10，
 * **推挽输出、高电平有效**（2026-09-28 现场实测：复位后红灯即亮 + PE12 实测 3.3V ⇒
 * 高 = 点亮），**上电默认红叉 = 红灯点亮**（2026-09-29 用户需求，取代「三路全灭」）；
 * 映射单一改动点 = `app_mm_proto_cmd.c` 的 `s_mm_lamp_map` + `_mm_drive()`。
 * 接管时按线摘 EXTI（`pl_gpio_release_exti`，不碰其它线/NVIC），原 SW1~SW3 输入语义
 * 随之失效——副作用与兼容矩阵见 doc/16 §4 与 doc/05-01 §6。
 *
 * 通道：**只绑 CH_ID_RS232**（RS232_1/USART6 为语音 TTS 专用，禁止 bind）；
 * 波特率 9600 由 DIP1 全局选择 ⇒ 现场必须置 OFF。命名前缀 `mm_` / `MM_`，
 * 与 doc/12 未实施的旧缅甸方案（`app_md_*`）不复用、不混用。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_dispatch.h"

/* ---- 帧结构常量（定长 20 字节，文档「命令格式」表）---- */
#define MM_FRAME_HEAD (0xFFU) /**< 起始码（[0]） */
#define MM_FRAME_ADDR (0xE0U) /**< 机器地址（[1]） */
#define MM_CMD_OFFSET (2U)    /**< 命令方式字偏移 */
#define MM_ARG_OFFSET (3U)    /**< DATA0 偏移（车道灯指令的开关量取值位） */
#define MM_DATA_LEN (12U)     /**< 命令内容字节数 DATA0~DATA11 */
#define MM_TAIL_LEN (5U)      /**< 结束码字节数（结束字符5..1） */
/** 整帧定长 = 起始码 1 + 机器地址 1 + 命令 1 + 内容 12 + 结束码 5（文档「命令长度 20 字节」） */
#define MM_FRAME_LEN (3U + MM_DATA_LEN + MM_TAIL_LEN)

/** 结束码「结构正签名」：用于区分「本设备的结构合法帧」与「同槽其它 0xFF 帧族」。
 *  取 [15..18] = 结束字符5..2（文档取值 00 00 00 00）；**末字节 [19] 不参与判别**——
 *  同源 8051 固件（`MianDian_Fee/C51_UART.C`）把该字节当 XOR 字节（算出但从不比对），
 *  现场若上位机确实填了 XOR，纳入判别会导致该帧被误拒。 */
#define MM_TAIL_SIG_OFFSET (MM_FRAME_LEN - MM_TAIL_LEN) /**< 15 */
#define MM_TAIL_SIG_LEN (MM_TAIL_LEN - 1U)              /**< 4 */
#define MM_TAIL_SIG_VALUE (0x00U)                       /**< 结束字符5..2 取值 */

/** 入队/解析上限 = 定长帧长（本协议无变长帧） */
#define MM_PAYLOAD_MAX (MM_FRAME_LEN)
#define MM_QUEUE_DEPTH (3U)
#define MM_MSG_SIZE    (sizeof(frame_msg_t) + MM_PAYLOAD_MAX)

_Static_assert(MM_PAYLOAD_MAX <= RB_SIZE_RS232, "MM frame must fit RS232 RB");
_Static_assert(MM_FRAME_LEN == 20U, "MM frame is fixed 20 bytes per protocol doc");

/* ---- 命令方式字（仅本模块处理的三个车道灯命令）---- */
typedef enum {
    MM_CMD_LANE_RED      = 0x90,   /**< 红色车道灯/红灯（DATA0：1 开 / 0 关） */
    MM_CMD_LANE_LEFT     = 0x0A,   /**< 左下箭头灯（DATA0：1 开 / 0 关） */
    MM_CMD_LANE_RIGHT    = 0x0B,   /**< 右下箭头灯（DATA0：1 开 / 0 关） */
    MM_CMD_OTHER         = 0xFF,   /**< 非本模块命令字（文档 0x10/0x40/0x80 等）→ 整帧 SKIP */
} mm_cmd_t;

/** 灯位（= 干接点序号；顺序即现场 S1/S2/S3） */
typedef enum {
    MM_LAMP_RED      = 0, /**< 红灯      ← 命令 0x90 */
    MM_LAMP_LEFT     = 1, /**< 左下箭头灯 ← 命令 0x0A */
    MM_LAMP_RIGHT    = 2, /**< 右下箭头灯 ← 命令 0x0B */
    MM_LAMP_COUNT    = 3,
} mm_lamp_t;

/** 帧解析结果 */
typedef struct {
    bool parsed;      /**< true = 帧结构合法且命令字属于本模块 */
    mm_cmd_t cmd;     /**< 命令字（parsed=false 时为 MM_CMD_OTHER） */
    mm_lamp_t lamp;   /**< 目标灯位（parsed=true 时有效） */
    bool on;          /**< true=开灯 / false=关灯（DATA0：1/0） */
} mm_parsed_cmd_t;

/**
 * @brief  解析缅甸 2D 费显帧（纯函数：无副作用、不阻塞、不持有锁）。
 *
 * 只做定长帧的字段抽取与命令字/取值判别；不做整帧校验（文档未定义校验字节，
 * 结束码 5 字节的取值判别在 probe 层完成——见 `app_mm_proto.c` 的 probe 注释）。
 *
 * @param  raw      原始帧（调用者保证长度 = MM_FRAME_LEN，由 probe 保证）。
 * @param  raw_len  原始帧长度；≠ MM_FRAME_LEN 时直接返回 parsed=false。
 * @return 解析结果；parsed=false 时执行层不动作。
 */
mm_parsed_cmd_t mm_parse_frame(const uint8_t *raw, uint16_t raw_len);

/**
 * @brief  执行解析后的车道灯命令（帧处理任务调用）。
 * @param  cmd  解析结果（parsed=false 时直接返回）。
 * @note   仅本模块持有的一处干接点映射 → `_mm_apply_lamp()`；无应答、无显示动作。
 *         实际落点经 `_mm_effective_lamp()` 做「箭头落点交换」后驱动（见 `mm_arrow_map_toggle()`）。
 */
void mm_execute_cmd(const mm_parsed_cmd_t *cmd);

/**
 * @brief  交换 / 还原 0x0A 与 0x0B 两条箭头命令的干接点落点（出厂测试开关）。
 *
 * 语义（默认态 ⇄ 交换态，可来回切换；**不做掉电保存**，每次开机回默认态）：
 *   默认态：`0x0A` → 左下箭头（`MM_LAMP_LEFT`，S2/PE11）；`0x0B` → 右下箭头（`MM_LAMP_RIGHT`，S3/PE10）
 *   交换态：`0x0A` → 右下箭头；`0x0B` → 左下箭头
 * 不变量：
 *   · **红灯 `0x90` 不受影响**（只有 LEFT ⇄ RIGHT 两支参与交换）；
 *   · probe 判据 / 帧解析 / 队列 / `s_mm_lamp_map` / 极性表达式**零改动**——
 *     交换只发生在「命令 → 灯位」的单点（`_mm_effective_lamp()`，`mm_execute_cmd()` 调用）；
 *   · 实际驱动的灯与引脚始终如实对应（`mm_apply_lamp()` 侧本就按最终灯位取映射表）。
 *
 * @return 切换后的状态：true = 已交换（0x0A→右下 / 0x0B→左下）；false = 默认态。
 * @note   消费者 = 缅甸版出厂测试 `app_factory_test_mm.c`（TEST 键，与标准版二选一编译）；
 *         现场测试矩阵见 doc/16 §13。
 */
bool mm_arrow_map_toggle(void);

/**
 * @brief  查询箭头落点是否处于交换态。
 * @return true = 已交换（0x0A→右下箭头 / 0x0B→左下箭头）；false = 默认态。
 */
bool mm_arrow_map_is_swapped(void);

/**
 * @brief  干接点输出单一映射点（灯位 → 端口/引脚/有效电平）。
 *
 * @note   灯位与引脚的对应关系（以及有效电平）全部写在 `app_mm_proto_cmd.c` 的
 *         映射表 `s_mm_lamp_map` 里：**红灯 = S1 = PE12 / 左下箭头 = S2 = PE11 /
 *         右下箭头 = S3 = PE10，推挽输出、高电平有效**（点亮 = 输出高 / 熄灭 = 输出低；
 *         2026-09-28 现场实测翻转）。改硬件只改该表 + `_mm_drive()`，其余代码零改动。
 *
 * @param  lamp  目标灯位（红灯 / 左下箭头灯 / 右下箭头灯）。
 * @param  on    true=闭合（开灯）/ false=断开（关灯）。
 */
void mm_apply_lamp(mm_lamp_t lamp, bool on);

/**
 * @brief  上电初始化干接点输出层（**默认显示红叉**：红灯 S1/PE12 点亮、绿箭头熄灭）。
 *
 * @note   源 8051 固件（`/home/yystation/Program/3218008/MianDian_Fee`）上电把三路
 *         开关量（C1/C2/C3）全部清零 = **灯全灭**；本实现**不再沿用**该语义——
 *         2026-09-29 用户需求「开机默认显示红叉」⇒ 三路接管完成后**红灯点亮**
 *         （车道关闭态，安全默认），两个绿箭头熄灭。
 *         接管顺序（无毛刺）：① 先写 ODR 为「灭」电平（**由映射表 `active_low` 推导**，
 *         不硬编码；引脚仍是输入 + 上拉，写 ODR 不驱动引脚）→ ② 按线摘除 EXTI
 *         （`pl_gpio_release_exti`，不碰其它线/NVIC）→ ③ 切成推挽输出
 *         （`pl_gpio_config_output`，此刻起稳定驱动「灭 = 低」）→ ④ 影子状态 = 全灭
 *         → ⑤ `mm_apply_lamp(MM_LAMP_RED, true)` 点亮红灯（唯一动作点，影子状态与
 *         诊断日志同步；协议帧到达后照常按帧驱动，红灯可被 `0x90 00` 关掉）。
 *         另把**箭头落点交换开关复位为默认态**（`mm_arrow_map_toggle()` 的状态不做持久化，
 *         每次开机回默认：0x0A→PE11 / 0x0B→PE10，用户需求 3）。
 */
void mm_apply_init(void);

/**
 * @brief  缅甸 2D 费显协议帧处理任务入口（帧队列 → 解析 → 干接点动作）。
 * @param  argument  未使用。
 */
void mm_proto_handle_task(void *argument);

/**
 * @brief  缅甸 2D 费显协议注册入口（sw_app_initcall 自注册）。
 */
void mm_proto_init(void);
