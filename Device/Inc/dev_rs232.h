/**
 * @file    dev_rs232.h
 * @brief   RS232 串口板级资源（USART3=RS232-0 协议 RX，USART6=语音 TX 无 DMA RX）
 */
#pragma once

#include <stdint.h>

/* IDLE DMA（仅 index0）：≥ QH 最大帧 259，且 ≤ RB_SIZE_RS232−1 */
#define RS232_BUF_SIZE (640U)

uint8_t *dev_rs232_get_buf(uint8_t index);
