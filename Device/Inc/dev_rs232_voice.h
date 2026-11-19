/**
 * @file    dev_rs232_voice.h
 * @brief   RS232 TTS 语音板封装
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DEV_RS232_VOICE_MAX_TEXT (200U)

/** @return true = 整帧已发出；false = 参数非法或发送失败/超时 */
bool dev_rs232_voice_play(const uint8_t *gbk_text, uint16_t len);
bool dev_rs232_voice_volume(uint8_t level);
