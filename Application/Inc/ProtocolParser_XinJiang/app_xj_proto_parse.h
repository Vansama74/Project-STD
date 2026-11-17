/**
 * @file    app_xj_proto_parse.h
 * @brief   新疆费额显示器协议帧解析与 BCC（纯函数，不发包/不渲染/不碰外设）
 *
 * 契约一览见 app_xj_proto.h 文件头（本文件实现 ① 帧长字段、② 行切分前提）；
 * 判据与字段表详见 doc/19_新疆费显协议/README.md §1、§3
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_xj_proto.h"

/**
 * @brief  计算 BCC（逐字节 XOR）。
 * @param  data  参与校验的字节（帧内 `[0..len-1]`，**不含** BCC 字节自身）。
 * @param  len   字节数。
 * @return 异或结果（len=0 时返回 0）。
 */
uint8_t xj_bcc_calc(const uint8_t *data, uint16_t len);

/**
 * @brief  校验整帧 BCC。
 * @param  frame      整帧（含末字节 BCC）。
 * @param  frame_len  整帧字节数（≥ XJ_FRAME_LEN_MIN）。
 * @return true = `XOR(frame[0..len-2]) == frame[len-1]`。
 */
bool xj_bcc_ok(const uint8_t *frame, uint16_t frame_len);

/**
 * @brief  命令字字节 → 命令枚举（probe 白名单与 parse 共用单一真源）。
 * @param  c  命令字字节。
 * @return 对应枚举；白名单外（含 'D' 0x44）返回 XJ_PCMD_INVALID。
 */
xj_pcmd_t xj_cmd_from_byte(uint8_t c);

/**
 * @brief  解析一帧（纯函数：无副作用、不阻塞、不持锁）。
 * @param  raw      整帧数据（probe 已保证帧头/长度/BCC）。
 * @param  raw_len  整帧字节数（= raw[XJ_LEN_OFFSET]）。
 * @return 解析结果；字段与命令不匹配（如 'T' 长度非 10+16N、'S' 非 8 字节）时
 *         parsed=false —— 执行层静默丢弃（协议未定义错误应答）。
 */
xj_parsed_cmd_t xj_parse_frame(const uint8_t *raw, uint16_t raw_len);
