/**
 * @file    dev_rs485.h
 * @brief   RS485 半双工收发器板级资源（USART1, RE=PA8）
 */
#pragma once

#include <stdint.h>

/* IDLE DMA：≥ RLS 最大帧 530，且 ≤ RB_SIZE_RS485−1（环缓废 1 字节） */
#define RS485_BUF_SIZE (640U)

void dev_rs485_init(void);
uint8_t *dev_rs485_get_buf(void);
