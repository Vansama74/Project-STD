/**
 * @file    app_sc_mtc_proto_voice.c
 * @brief   四川 MTC 费显协议（1E）固定语音播报
 *
 * '7' 命令 '0'~'7' 固定语音通过 dev_rs232_voice 直送 TTS 板（USART6 旁路），
 * 参照青海 _voice.c 用法。动态变量（车型/金额/总重/超重）TTS 板不支持变量合成，
 * 本实现按文档语义取核心用语播报，并在注释标注取舍。
 */

#include "app_sc_mtc_proto_voice.h"

#include <stdint.h>

#include "dev_rs232_voice.h"

static const struct {
    const uint8_t *text;
    uint16_t len;
} s_mtc_fixed_voices[8] = {
    /* '0' 您好，请交费，谢谢合作，祝您一路平安（文档含车型/金额变量，不拼读） */
    {(const uint8_t *)"\xC4\xFA\xBA\xC3\xA3\xAC\xC7\xEB\xBD\xBB\xB7\xD1\xA3\xAC"
                      "\xD0\xBB\xD0\xBB\xBA\xCF\xD7\xF7\xA3\xAC\xD7\xA3\xC4\xFA"
                      "\xD2\xBB\xC2\xB7\xC6\xBD\xB0\xB2", 36},
    /* '1' 您好，请交费（文档含总重/超重/金额变量，不拼读） */
    {(const uint8_t *)"\xC4\xFA\xBA\xC3\xA3\xAC\xC7\xEB\xBD\xBB\xB7\xD1", 12},
    /* '2' 您好（文档含车型变量，不拼读） */
    {(const uint8_t *)"\xC4\xFA\xBA\xC3", 4},
    /* '3' 谢谢合作，祝您一路平安（文档含总重/超重/金额变量，不拼读） */
    {(const uint8_t *)"\xD0\xBB\xD0\xBB\xBA\xCF\xD7\xF7\xA3\xAC\xD7\xA3\xC4\xFA"
                      "\xD2\xBB\xC2\xB7\xC6\xBD\xB0\xB2", 22},
    /* '4' 谢谢合作，祝您一路平安 */
    {(const uint8_t *)"\xD0\xBB\xD0\xBB\xBA\xCF\xD7\xF7\xA3\xAC\xD7\xA3\xC4\xFA"
                      "\xD2\xBB\xC2\xB7\xC6\xBD\xB0\xB2", 22},
    /* '5' 月票车，请通行 */
    {(const uint8_t *)"\xD4\xC2\xC6\xB1\xB3\xB5\xA3\xAC\xC7\xEB\xCD\xA8\xD0\xD0", 14},
    /* '6' 免费车，请通行 */
    {(const uint8_t *)"\xC3\xE2\xB7\xD1\xB3\xB5\xA3\xAC\xC7\xEB\xCD\xA8\xD0\xD0", 14},
    /* '7' 车辆闯关 */
    {(const uint8_t *)"\xB3\xB5\xC1\xBE\xB4\xB3\xB9\xD8", 8},
};

/**
 * @brief  播报固定语音（'7' 命令 '0'~'7'）。
 * @param  idx  语音索引（0~7）。
 */
void sc_mtc_voice_play_fixed(uint8_t idx)
{
    if (idx >= (sizeof(s_mtc_fixed_voices) / sizeof(s_mtc_fixed_voices[0]))) {
        return;
    }
    dev_rs232_voice_play(s_mtc_fixed_voices[idx].text, s_mtc_fixed_voices[idx].len);
}