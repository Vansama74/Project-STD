/**
 * @file    pl_gpio.h
 * @brief   GPIO 初始化 + 基本读写（Platform 层抽象，隔离芯片 GPIO 基地址）
 */

#pragma once

#include <stdint.h>

/** @brief 端口标识（不与芯片 GPIO 基地址耦合） */
typedef enum {
    PL_PORT_A = 0,
    PL_PORT_B,
    PL_PORT_C,
    PL_PORT_D,
    PL_PORT_E,
    PL_PORT_F,
    PL_PORT_G,
    PL_PORT_H,
    PL_PORT_MAX,
} pl_port_t;

void pl_gpio_init(void);
void pl_gpio_write(pl_port_t port, uint8_t pin, bool high);
bool pl_gpio_read(pl_port_t port, uint8_t pin);

/**
 * @brief  把引脚配置为**推挽输出**（无上下拉、低速）。
 *
 * @note   **只改引脚模式**（MODER/OTYPER/OSPEEDR/PUPDR），**不改输出电平**——
 *         输出电平仍由 `pl_gpio_write()` 决定。因此「接管一个原本是 EXTI 输入的引脚」
 *         的正确顺序是：
 *           ① `pl_gpio_write(port, pin, 无效电平)`  （引脚仍是输入 ⇒ 不驱动，无毛刺）
 *           ② `pl_gpio_release_exti(port, pin)`    （摘掉 EXTI 线，见下）
 *           ③ `pl_gpio_config_output(port, pin)`   （切推挽输出；此刻起真正驱动）
 *         切到输出瞬间引脚即输出第 ① 步写入的电平，**不会出现中间态毛刺**。
 *
 * @param  port  端口（`pl_port_t`，不含芯片地址）。
 * @param  pin   引脚号 0~15（越界直接返回，不动作）。
 */
void pl_gpio_config_output(pl_port_t port, uint8_t pin);

/**
 * @brief  摘除**单个引脚对应 EXTI 线**的硬件配置（把引脚从 EXTI 输入模式释放出来）。
 *
 * 逐线清理（只影响该引脚一条线）：
 *   `EXTI->IMR` 中断屏蔽 / `EXTI->EMR` 事件屏蔽 / `EXTI->RTSR`+`FTSR` 双边沿触发 /
 *   `EXTI->PR` 挂起标志（写 1 清除；HAL 的 `GPIO_Init/DeInit` **都不碰 PR**，
 *   必须在此显式清）/ `SYSCFG->EXTICR` 线映射（仅当该线当前确实映射到本端口时清）。
 *
 * @warning **不动 NVIC**：不调用 `HAL_NVIC_DisableIRQ()`——整条 `EXTI15_10_IRQn`
 *          上还挂着别的线（如 PD8/TEST 键在 `EXTI9_5`；同组的 PE7/PE8 为轮询输入
 *          不占 EXTI），一刀切禁用会连带杀掉无关中断。本函数只按线摘除，
 *          其它 EXTI 线与 NVIC 状态**逐位不变**。
 *          本函数也不改引脚模式（由 `pl_gpio_config_output()` 负责）。
 *
 * @param  port  端口（`pl_port_t`）。
 * @param  pin   引脚号 0~15（越界直接返回，不动作）。
 */
void pl_gpio_release_exti(pl_port_t port, uint8_t pin);
