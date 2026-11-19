/**
 * @file    dev_rs232_voice.c
 * @brief   RS232 TTS 语音板封装
 */

#include "dev_rs232_voice.h"

#include <string.h>

#include "pl_uart.h"

static pl_uart_handle_t dev_rs232_voice_uart(void)
{
    /* 语音板 TTS 专用 USART6 (PL_UART6)，不再使用 USART3 */
    return pl_uart_get_handle(PL_UART6);
}

/* 9600 8N1 ≈ 1.04ms/字节：超时按帧长推导（固定 100ms 只够发约 96B，
 * 更长的播报文本会在中途被中止） */
static bool dev_rs232_voice_send(const uint8_t *frame, uint16_t len)
{
    pl_uart_handle_t uart = dev_rs232_voice_uart();
    if (!uart || !frame || len == 0U) {
        return false;
    }

    uint32_t timeout_ms = (uint32_t)len * 2U + 50U;
    return pl_uart_send(uart, frame, len, timeout_ms) == (int32_t)len;
}

bool dev_rs232_voice_play(const uint8_t *gbk_text, uint16_t len)
{
    if (!gbk_text || len == 0U || len > DEV_RS232_VOICE_MAX_TEXT) {
        return false;
    }

    uint8_t frame[DEV_RS232_VOICE_MAX_TEXT + 5U];
    const uint16_t payload_len = (uint16_t)(len + 2U);
    if (payload_len > 0xFFU) {
        return false;
    }

    frame[0] = 0xFDU;
    frame[1] = 0x00U;
    frame[2] = (uint8_t)payload_len;
    frame[3] = 0x01U;
    frame[4] = 0x01U;
    memcpy(&frame[5], gbk_text, len);

    return dev_rs232_voice_send(frame, (uint16_t)(len + 5U));
}

bool dev_rs232_voice_volume(uint8_t level)
{
    uint8_t frame[6];
    frame[0] = 0xFDU;
    frame[1] = 0x00U;
    frame[2] = 0x03U;
    frame[3] = 0x01U;
    frame[4] = 0x02U;
    frame[5] = level;
    return dev_rs232_voice_send(frame, sizeof(frame));
}
