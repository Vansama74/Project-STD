/**
 * @file    app_gx_proto.h
 * @brief   广西费显协议（AA addr BB + 双字节命令 + 定界 0xFF）公共定义
 *
 * 契约（改错静默致现场故障）：① 51 54 文本 ≤192B、以 0xFF 定界；
 * ② 亮度 0/1/2/3 → 8/6/4/2 档并挂起自动调光；③ 应答 0x1B 从收帧通道原路回；
 * ④ 自备 RS485/RS232 槽体 + acquire；⑤ 队列/帧缓冲/任务栈置 CCMRAM。
 * 详见 doc/21_广西费显协议/README.md §1、§2、§4
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_dispatch.h" /* frame_msg_t（GX_MSG_SIZE） */

/* ---- 帧结构常量：AA + addr + BB + cmd1 + cmd2 + [sum] + data + FF ---- */
#define GX_STX0 (0xAAU)            /**< 引导码 1（[0]） */
#define GX_STXB (0xBBU)            /**< 引导码 2（[2]） */
#define GX_ETX (0xFFU)             /**< 结束码（仅变长命令有） */
#define GX_ADDR_OFFSET (1U)        /**< 地址偏移（v1 不判，见 GX_LOCAL_ADDR） */
#define GX_STXB_OFFSET (2U)        /**< 引导码 2 偏移 */
#define GX_CMD1_OFFSET (3U)        /**< 命令码高字节偏移 */
#define GX_CMD2_OFFSET (4U)        /**< 命令码低字节偏移 */
#define GX_SUM_OFFSET (5U)         /**< 数据累加和偏移（仅 51 54；v1 不校验） */
#define GX_DATA_OFFSET (6U)        /**< 通用载荷起始偏移 */
#define GX_CMD_LEN (5U)            /**< 帧头到命令码末字节的长度（AA/addr/BB/cmd1/cmd2） */
#define GX_ACK (0x1BU)             /**< 成功应答单字节 */
#define GX_BROADCAST_ADDR (0xFFU)  /**< 广播地址（GX_LOCAL_ADDR != 0 时放行） */

/** 本机地址过滤：0 = 不判地址（v1 默认）；1~254 = 只接受本机地址与广播。 */
#ifndef GX_LOCAL_ADDR
#define GX_LOCAL_ADDR (0)
#endif

/* ---- 51 54 显示命令（唯一变长命令：0xFF 定界）---- */
#define GX_DISPLAY_PLAYBACK_OFFSET (6U)  /**< 播放方式偏移（1~21；v1 只做直接显示） */
#define GX_DISPLAY_SPEED_OFFSET (7U)     /**< 速度偏移（0~7；v1 忽略） */
#define GX_DISPLAY_STAY_OFFSET (8U)      /**< 停留偏移（0~99；v1 忽略） */
#define GX_DISPLAY_COLOR_OFFSET (9U)     /**< 颜色码偏移（0 红 / 1 绿 / 2 黄） */
#define GX_DISPLAY_TIME_OFFSET (10U)     /**< 总显示时间偏移（秒，0 = 永久） */
#define GX_DISPLAY_TEXT_OFFSET (11U)     /**< 文本起始偏移 */
#define GX_TEXT_MAX (192U)               /**< 文本字节上限（GBK 即 96 个汉字） */
#define GX_DISPLAY_FRAME_LEN_MIN (12U)   /**< 11 + 空文本 + 结束码 */
#define GX_FRAME_LEN_MAX (204U)          /**< 11 + 192 + 结束码 */
#define GX_DISPLAY_ETX_MAX_OFFSET (203U) /**< 结束码最晚偏移（= 11 + 192） */
/* 契约：文本 ≤ GX_TEXT_MAX 且帧 ≤ GX_FRAME_LEN_MAX；越限后果 = 帧被丢弃/重同步。 */

/* ---- 其余定长命令 ---- */
#define GX_FRAME_LEN_BRIGHTNESS (7U)  /**< 4B 4C：[5] 亮度 0~3 */
#define GX_FRAME_LEN_LANE (8U)        /**< 4B 5A：[5..7] 灯组三元组 */
#define GX_FRAME_LEN_FULLSCREEN (7U)  /**< 01 01：[5][6] 同色码 */
#define GX_FRAME_LEN_VERSION (5U)     /**< 02 02：无载荷 */
#define GX_FRAME_LEN_BOOT (103U)      /**< 03 03：[5] 色 + 96B 文本 + 0xFF */
#define GX_FRAME_LEN_FONT (6U)        /**< 04 04：[5] 低半字节字号 / 高半字节字型 */

/* ---- 03 03 开机界面 ---- */
#define GX_BOOT_COLOR_OFFSET (5U)     /**< 颜色码偏移 */
#define GX_BOOT_TEXT_OFFSET (6U)      /**< 文本起始偏移（固定 6 行 × 16B） */
#define GX_BOOT_LINE_BYTES (16U)      /**< 每行 16B（8 个 GBK 汉字） */
#define GX_BOOT_LINES (6U)            /**< 固定 6 行 */
#define GX_BOOT_TEXT_LEN (GX_BOOT_LINE_BYTES * GX_BOOT_LINES) /**< 96B */

/* ---- 56 4F 语音 ---- */
#define GX_VOICE_KIND_OFFSET (5U)       /**< 子命令：0 礼貌语 / 1 收费语音 */
#define GX_VOICE_CODE_OFFSET (6U)       /**< 礼貌语编号 1~5 */
#define GX_VOICE_AMOUNT_OFFSET (6U)     /**< 收费金额 ASCII 起始偏移 */
#define GX_VOICE_AMOUNT_DIGITS (7U)     /**< 金额位数：万/千/百/十/个/十分/百分 */
#define GX_VOICE_POLITE_FRAME_LEN (8U)  /**< 礼貌语整帧长 */
#define GX_VOICE_FEE_FRAME_LEN (14U)    /**< 收费语音整帧长 */
#define GX_VOICE_CODE_MIN (1U)          /**< 礼貌语编号下界 */
#define GX_VOICE_CODE_MAX (5U)          /**< 礼貌语编号上界 */
#define GX_VOICE_KIND_POLITE (0U)       /**< [5] = 0：礼貌语 */
#define GX_VOICE_KIND_FEE (1U)          /**< [5] = 1：收费语音 */

/* ---- 04 04 字体/字号 ---- */
#define GX_FONT_CODE_OFFSET (5U)     /**< 编码字节：低 4 位字号 0/1/2、高 4 位字型 0/1/2/3 */
#define GX_FONT_SIZE_MAX (2U)        /**< 低半字节上界（32 点阵） */
#define GX_FONT_TYPE_MAX (3U)        /**< 高半字节上界（黑体） */

/* ---- 颜色码 ---- */
#define GX_COLOR_MAX (2U) /**< 颜色码上界（黄） */

/** 灯组三元组（4B 5A 的 [5][6][7]） */
typedef enum {
    GX_LAMP_GREEN_ON  = 0,    /**< FF 00 FF：通行绿灯亮 */
    GX_LAMP_GREEN_OFF = 1,    /**< 00 00 00：通行绿灯灭（= 红灯亮） */
    GX_LAMP_RED_ON    = 2,    /**< 01 01 00：通行红灯亮 */
    GX_LAMP_RED_OFF   = 3,    /**< 00 01 FF：通行红灯灭（= 绿灯亮） */
    GX_LAMP_ALARM_ON  = 4,    /**< 01 02 FF：报警（黄闪）开 */
    GX_LAMP_ALARM_OFF = 5,    /**< 02 02 00：报警（黄闪）关 */
    GX_LAMP_INVALID   = 0xFF, /**< 未定义三元组 */
} gx_lamp_t;

/** 逻辑命令（8 组命令码 → 10 条逻辑命令，语音按子命令再分） */
typedef enum {
    GX_CMD_DISPLAY = 0,    /**< 51 54 显示文本（回 0x1B） */
    GX_CMD_BRIGHTNESS = 1, /**< 4B 4C 亮度 0~3（回 0x1B） */
    GX_CMD_LANE = 2,       /**< 4B 5A 车道灯/报警器（回 0x1B） */
    GX_CMD_VOICE = 3,      /**< 56 4F 语音（回 0x1B） */
    GX_CMD_FULLSCREEN = 4, /**< 01 01 全屏点亮（不回） */
    GX_CMD_VERSION = 5,    /**< 02 02 版本号（不回） */
    GX_CMD_BOOT_SCREEN = 6, /**< 03 03 设置开机界面（回 0x1B） */
    GX_CMD_FONT = 7,       /**< 04 04 字体/字号（不回） */
    GX_CMD_INVALID = 0xFF, /**< 白名单外命令码 */
} gx_cmd_t;

/* ---- 队列与任务 ---- */
#define GX_QUEUE_DEPTH (3U) /**< 帧队列深度（RS485/RS232 两 mask 共用一队列） */
#define GX_PAYLOAD_MAX (GX_FRAME_LEN_MAX)
#define GX_MSG_SIZE (sizeof(frame_msg_t) + GX_PAYLOAD_MAX)
#define GX_TICK_MS (500U) /**< 处理任务队列等待节拍（总显示时间到期判定粒度） */
/* 契约：GX_MSG_SIZE 覆盖最大帧；改小会在 rb_read 时截断（帧长已由 probe 钳到 204）。 */

/** @brief 广西费显协议注册入口（sw_app_initcall 自注册）。 */
void gx_proto_init(void);

/**
 * @brief  广西费显协议帧处理任务（帧队列 → 解析执行 + 显示时间到期判定）。
 * @param  argument  未使用。
 */
void gx_proto_handle_task(void *argument);
