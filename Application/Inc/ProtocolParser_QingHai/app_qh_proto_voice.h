/**
 * @file    app_qh_proto_voice.h
 * @brief   青海协议语音段播报接口
 */
#pragma once

#include <stdint.h>

/**
 * @brief 播报语音段组合（V1.0 命令 '5'）。
 * @param segs     语音段号数组，每字节一个段号。
 * @param seg_cnt  段号个数。
 * @note  段号按顺序拼接成一句文本后整体送语音板；未定义段号（>45）跳过。
 */
void qh_voice_segments(const uint8_t *segs, uint16_t seg_cnt);