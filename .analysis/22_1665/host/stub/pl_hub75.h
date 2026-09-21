/**
 * @file    pl_hub75.h
 * @brief   22-1665 宿主差分测试桩 — HUB75 抽象（**不是**真机头文件）
 *
 * 用途：把 `Device/Display/dev_display_22_1665.c` 编到 x86-64 上直接运行，
 *       与同口径编译的旧版驱动 / Python 参考模型逐字节对拍
 *       （见 `.analysis/22_1665/check_host_differential.py`）。
 *
 * 与真机 `Platform/Inc/pl_hub75.h` 的差异（差分测试不受影响：新旧两侧同桩编译）：
 *   · BSRR 写、CLK 脉冲、行址译码从「内联直写寄存器」改为**记录函数**（可对拍写序列）；
 *   · 端口池由宿主提供（`g_ports[0] = PG`、`g_ports[1] = PB`），引脚值与真板一致。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define HUB75_CHANNEL_MAX 10

/** CMSIS 的 __NOP() 桩（真机来自 cmsis_gcc.h） */
#ifndef __NOP
#define __NOP() __asm__ volatile("nop")
#endif

/** 宿主伪 GPIO：BSRR 只作占位（真写入被 `pl_hub75_bsrr_flush` 拦下记录） */
typedef struct {
    volatile uint32_t BSRR;
} GPIO_TypeDef;

/** @brief HUB75 单通道 RGB 引脚描述（与真机同布局） */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} hub75_pin_t;

/** @brief BSRR 预计算值（与真机同布局） */
typedef struct {
    uint32_t val;
    GPIO_TypeDef *port;
} pl_hub75_bsrr_t;

extern const hub75_pin_t g_hub75_pin_r[HUB75_CHANNEL_MAX];
extern const hub75_pin_t g_hub75_pin_g[HUB75_CHANNEL_MAX];
extern const hub75_pin_t g_hub75_pin_b[HUB75_CHANNEL_MAX];

/* ---- 记录型桩（真机是内联寄存器写）---- */
void pl_hub75_bsrr_flush(const pl_hub75_bsrr_t *p);
void pl_hub75_clock_pulse(void);
void pl_hub75_Decoder_set_row(uint8_t row);
