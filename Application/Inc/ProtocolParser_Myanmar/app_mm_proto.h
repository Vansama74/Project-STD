/**
 * @file    app_mm_proto.h
 * @brief   缅甸 2D 费显协议（车道灯开关量控制）公共定义
 *
 * 契约（改错静默致现场故障）：① 干接点高有效（点亮 = 输出高）；上电默认红灯亮；
 * ② 接管顺序固定；按线摘 EXTI（禁整条 IRQ 禁用）；
 * ③ 非本模块命令须结束码正签名才 SKIP；④ 只绑 CH_ID_RS232，禁绑 RS232_1；
 * ⑤ 队列/帧缓冲/任务栈置 CCMRAM；⑥ 箭头交换仅运行态；⑦ 9600 8N1（现场 DIP1 置 OFF）。
 * 详见 doc/16_缅甸2D费显协议/README.md §3、§4、§6、§7、§13、§14
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

/** 结束码「结构正签名」（SKIP 分支门槛）：[15..18] 四字节 == 0x00；**末字节 [19] 不参与判别**
 *（同源 8051 把该字节当 XOR；纳入判别会误拒合法帧，详见 doc/16_缅甸2D费显协议/README.md §6）。 */
#define MM_TAIL_SIG_OFFSET (MM_FRAME_LEN - MM_TAIL_LEN) /**< 15 */
#define MM_TAIL_SIG_LEN (MM_TAIL_LEN - 1U)              /**< 4 */
#define MM_TAIL_SIG_VALUE (0x00U)                       /**< 结束字符5..2 取值 */

/** 入队/解析上限 = 定长帧长（本协议无变长帧） */
#define MM_PAYLOAD_MAX (MM_FRAME_LEN)
#define MM_QUEUE_DEPTH (3U) /**< 队列深度（文档「先到先处理」缓存语义） */
#define MM_MSG_SIZE    (sizeof(frame_msg_t) + MM_PAYLOAD_MAX) /**< 单消息 = frame_msg_t + 定长载荷 */

/* 契约：帧定长 20B（协议文档固定）且 ≤ RS232 RB 容量；改错静默解析失败，
 * 详见 doc/16_缅甸2D费显协议/README.md §2。 */

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
    MM_LAMP_COUNT    = 3, /**< 灯位总数（遍历上界） */
} mm_lamp_t;

/** 帧解析结果 */
typedef struct {
    bool parsed;      /**< true = 帧结构合法且命令字属于本模块 */
    mm_cmd_t cmd;     /**< 命令字（parsed=false 时为 MM_CMD_OTHER） */
    mm_lamp_t lamp;   /**< 目标灯位（parsed=true 时有效） */
    bool on;          /**< true=开灯 / false=关灯（DATA0：1/0） */
} mm_parsed_cmd_t;

/**
 * @brief  解析定长帧的字段与车道灯命令（纯函数：无副作用、不阻塞、不持锁）。
 * @param  raw      原始帧（probe 保证结构；长度 ≠ `MM_FRAME_LEN` 时返回 parsed=false）。
 * @param  raw_len  原始帧长度。
 * @return 解析结果；parsed=false 时执行层不动作。
 * @note   不校验结束码（文档未定义校验；取值判别在 probe 层，详见
 *         doc/16_缅甸2D费显协议/README.md §6）。
 */
mm_parsed_cmd_t mm_parse_frame(const uint8_t *raw, uint16_t raw_len);

/**
 * @brief  执行解析后的车道灯命令（无应答、无显示、无语音）。
 * @param  cmd  解析结果（parsed=false 时直接返回）。
 * @note   落点先经「箭头交换」单点 `_mm_effective_lamp()` 再驱动（红灯不受影响，详见
 *         doc/16_缅甸2D费显协议/README.md §13）。
 */
void mm_execute_cmd(const mm_parsed_cmd_t *cmd);

/**
 * @brief  交换 / 还原 `0x0A` ⇄ `0x0B` 的干接点落点（出厂测试开关）。
 * @return 切换后状态：true = 已交换（0x0A→右下 S3/PE10 / 0x0B→左下 S2/PE11）；false = 默认态。
 * @note   红灯 `0x90` 不受影响；**不持久化**（`mm_apply_init()` 开机复位）；消费者 =
 *         缅甸版出厂测试（与标准版工厂测试二选一编译），现场矩阵详见
 *         doc/16_缅甸2D费显协议/README.md §13。
 */
bool mm_arrow_map_toggle(void);

/**
 * @brief  查询箭头落点是否处于交换态。
 * @return true = 已交换（0x0A→右下箭头 / 0x0B→左下箭头）；false = 默认态。
 */
bool mm_arrow_map_is_swapped(void);

/**
 * @brief  干接点输出单一映射点（灯位 → 端口/引脚/有效电平）。
 * @param  lamp  目标灯位（红灯 / 左下箭头灯 / 右下箭头灯）。
 * @param  on    true=闭合（开灯）/ false=断开（关灯）。
 * @note   引脚与极性只在 `app_mm_proto_cmd.c` 的 `s_mm_lamp_map` + `_mm_drive()` 里改
 *         （详见 doc/16_缅甸2D费显协议/README.md §3）。
 */
void mm_apply_lamp(mm_lamp_t lamp, bool on);

/**
 * @brief  干接点输出层上电初始化：接管三路为推挽输出，**默认显示红叉 = 红灯 S1/PE12 点亮**。
 * @note   接管顺序（无毛刺，契约 ②）：写「灭」电平（由映射表推导）→ 按线摘 EXTI（`pl_gpio_release_exti`，
 *         **不碰其它线/NVIC**）→ 切推挽输出 → 点亮红灯（唯一动作点 `mm_apply_lamp()`）；协议帧到达后
 *         照常按帧驱动（`0x90 00` 仍可关红灯）；箭头交换开关同时复位为默认态。
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
