/**
 * @file    app_anhui_proto_voice.h
 * @brief   安徽费显协议语音播报接口（0x95 播放声音）
 */
#pragma once

#include <stdint.h>

/**
 * @brief  按语音编号构造模板句并播报。
 * @param  voice_no    语音编号 01~15（15 = 自由文本：参数区即全部播报内容，GBK 原样透传）。
 * @param  params      参数区（编号 01~14：变长 ASCII 数字串，按编号模板解析；编号 15：GBK 自由文本）。
 * @param  params_len  参数区长度。
 * @note   参数长度/格式不符时静默放弃播报；音量由调用方经 dev_rs232_voice_volume 设置。
 */
void anhui_voice_play(uint8_t voice_no, const uint8_t *params, uint16_t params_len);
