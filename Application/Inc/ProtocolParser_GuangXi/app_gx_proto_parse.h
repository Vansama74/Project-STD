/**
 * @file    app_gx_proto_parse.h
 * @brief   广西费显协议帧解析（纯函数：不发包/不渲染/不碰外设）
 *
 * 契约一览见 app_gx_proto.h 文件头；字段表与文档/旧固件差异见
 * doc/21_广西费显协议/README.md §1、§3
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_gx_proto.h"

/** 帧解析结果（parsed=false 时执行层不动作） */
typedef struct {
    bool parsed;            /**< true = 帧头/命令码/长度/字段取值全部通过 */
    gx_cmd_t cmd;           /**< 逻辑命令（parsed=false 时为 GX_CMD_INVALID） */
    uint8_t address;        /**< 地址字节（v1 不判，原样带出） */
    const uint8_t *text;    /**< 51 54 文本 / 03 03 开机界面文本（GBK，指向 raw 不拷贝） */
    uint16_t text_len;      /**< text 字节数（51 54 ≤192；03 03 恒 96） */
    uint8_t color;          /**< 颜色码 0 红 / 1 绿 / 2 黄（display_color_t = 码 + 1） */
    uint8_t playback;       /**< 51 54 播放方式 1~21（v1 忽略） */
    uint8_t speed;          /**< 51 54 速度 0~7（v1 忽略） */
    uint8_t stay;           /**< 51 54 停留 0~99（v1 忽略） */
    uint8_t display_seconds;/**< 51 54 总显示时间（秒，0 = 永久） */
    uint8_t brightness;     /**< 4B 4C 亮度 0~3（0 最亮） */
    gx_lamp_t lamp;         /**< 4B 5A 灯组动作（GX_LAMP_INVALID = 未知三元组） */
    uint8_t voice_kind;     /**< 56 4F 子命令：0 礼貌语 / 1 收费语音 */
    uint8_t voice_code;     /**< 56 4F 礼貌语编号 1~5 */
    uint8_t amount[GX_VOICE_AMOUNT_DIGITS]; /**< 56 4F 收费金额 ASCII（万…百分） */
    uint8_t font_size_code; /**< 04 04 字号码 0/1/2 → 16/24/32 点阵 */
    uint8_t font_type_code; /**< 04 04 字型码 0/1/2/3 → 宋/仿宋/楷/黑 */
} gx_parsed_cmd_t;

/**
 * @brief  命令码双字节 → 逻辑命令（probe 白名单与 parse 共用单一真源）。
 * @param  c1  命令码高字节（[3]）。
 * @param  c2  命令码低字节（[4]）。
 * @return 对应枚举；白名单外返回 GX_CMD_INVALID。
 */
gx_cmd_t gx_cmd_from_pair(uint8_t c1, uint8_t c2);

/**
 * @brief  灯组三元组 → 动作（probe 与 parse 共用单一真源）。
 * @param  triple  指向 4B 5A 帧的 [5]（3 字节）。
 * @return 对应动作；未定义三元组返回 GX_LAMP_INVALID。
 */
gx_lamp_t gx_lamp_from_triplet(const uint8_t *triple);

/**
 * @brief  计算数据累加和（[6..结束码前] 逐字节累加取低 8 位）。
 * @param  data  参与累加的字节（51 54 为 [6..len-2]）。
 * @param  len   字节数。
 * @return 累加和（len=0 返回 0）。
 * @note   v1 不校验该字段（与现场旧固件一致）；保留本函数供联调期核对上位机算法。
 */
uint8_t gx_sum_calc(const uint8_t *data, uint16_t len);

/**
 * @brief  解析一帧（纯函数：无副作用、不阻塞、不持锁）。
 * @param  raw      整帧数据（probe 已保证帧头/命令码/定界与帧长）。
 * @param  raw_len  整帧字节数。
 * @return 解析结果；字段取值非法（颜色/亮度/灯组/语音编号/金额非数字）时
 *         parsed=false —— 执行层静默丢弃（协议未定义错误应答）。
 */
gx_parsed_cmd_t gx_parse_frame(const uint8_t *raw, uint16_t raw_len);
