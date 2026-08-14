/**
 * @file    dev_rs232.c
 * @brief   RS232 串口板级资源（USART3=RS232-0 协议 DMA RX）
 *
 * USART6 语音板仅 TX（dev_rs232_voice），不分配 DMA RX 缓冲。
 * 通道生命周期和任务循环由 Application 层 app_rs232 负责。
 */

#include "dev_rs232.h"

static uint8_t s_rs232_0_buf[RS232_BUF_SIZE];

uint8_t *dev_rs232_get_buf(uint8_t index)
{
    /* index1（USART6）无协议 RX，不提供 DMA 缓冲 */
    return (index == 0U) ? s_rs232_0_buf : nullptr;
}
