/**
 * @file        pl_gpio.c
 * @brief       GPIO 初始化 + 引脚读写（Platform 层封装，隔离芯片地址）
 */

#include "pl_gpio.h"
#include "gpio.h"
#include "stm32f4xx_hal.h"
#include "initcall.h"

/* ---- 端口 ID → 芯片基地址映射（换芯片只改此表） ---- */
static const uint32_t g_port_base[] = {
    [PL_PORT_A] = (uint32_t)GPIOA,
    [PL_PORT_B] = (uint32_t)GPIOB,
    [PL_PORT_C] = (uint32_t)GPIOC,
    [PL_PORT_D] = (uint32_t)GPIOD,
    [PL_PORT_E] = (uint32_t)GPIOE,
    [PL_PORT_F] = (uint32_t)GPIOF,
    [PL_PORT_G] = (uint32_t)GPIOG,
    [PL_PORT_H] = (uint32_t)GPIOH,
};

void pl_gpio_init(void)
{
    MX_GPIO_Init();
}
hw_pl_initcall(pl_gpio_init);

void pl_gpio_write(pl_port_t port, uint8_t pin, bool high)
{
    if (port >= PL_PORT_MAX) return;
    GPIO_TypeDef *gpio = (GPIO_TypeDef *)g_port_base[port];
    uint16_t mask      = (uint16_t)(1U << pin);
    if (high)
        gpio->BSRR = mask;
    else
        gpio->BSRR = (uint32_t)mask << 16;
}

bool pl_gpio_read(pl_port_t port, uint8_t pin)
{
    if (port >= PL_PORT_MAX) return false;
    GPIO_TypeDef *gpio = (GPIO_TypeDef *)g_port_base[port];
    return (gpio->IDR & (1U << pin)) != 0;
}

void pl_gpio_config_output(pl_port_t port, uint8_t pin)
{
    if (port >= PL_PORT_MAX || pin > 15U) return;
    /* 推挽输出 / 无上下拉 / 低速：干接点级别的控制信号，无需高速档（低速档 EMI 更小，
     * 与 MX_GPIO_Init 里 Lane_Light/Flash_Light 等 DO 的档位一致）。
     * HAL_GPIO_Init 在 OUTPUT_PP 模式下**不进入 EXTI 分支、也不写 EXTI->PR**
     * （源：Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c 的
     *  `if((GPIO_Init->Mode & EXTI_MODE) != 0x00U)` 守卫）⇒ 对 EXTI 零副作用；
     * 从 EXTI 输入接管时另按 pl_gpio_release_exti() 摘线。 */
    GPIO_InitTypeDef cfg = {
        .Pin   = (1UL << pin),
        .Mode  = GPIO_MODE_OUTPUT_PP,
        .Pull  = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW,
    };
    HAL_GPIO_Init((GPIO_TypeDef *)g_port_base[port], &cfg);
}

void pl_gpio_release_exti(pl_port_t port, uint8_t pin)
{
    if (port >= PL_PORT_MAX || pin > 15U) return;

    const uint32_t line = 1UL << pin;

    /* SYSCFG 时钟：EXTI 线映射寄存器属 SYSCFG，首次配置时由 HAL_GPIO_Init 的 EXTI 分支
     * 使能；此处再确保一次（幂等，只置位 APB2ENR 的 SYSCFGEN）。 */
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* ① 逐线摘除中断/事件/双边沿触发（其它线逐位不变） */
    EXTI->IMR &= ~line;  /* 中断屏蔽：清掉后本线不再产生中断请求 */
    EXTI->EMR &= ~line;  /* 事件屏蔽 */
    EXTI->RTSR &= ~line; /* 上升沿触发 */
    EXTI->FTSR &= ~line; /* 下降沿触发 */

    /* ② 清挂起标志（EXTI_PR 为 rc_w1：写 1 清除）。HAL 的 GPIO_Init / GPIO_DeInit
     *    都不写 PR —— 若不清，旧的 pending 位会一直挂着（IMR 已清 ⇒ 不产生中断，
     *    但 NVIC 挂起寄存器/诊断视图会残留，属不稳定状态）。 */
    EXTI->PR = line;

    /* ③ 释放 SYSCFG 线映射：**仅当该线当前确实映射到本端口**时清（避免误清同号的
     *    其它端口线映射）。EXTICR 的端口编号与 pl_port_t 枚举一致（0=PA … 7=PH）。 */
    const uint32_t shift   = 4U * (pin & 0x03U);
    volatile uint32_t *ext = &SYSCFG->EXTICR[pin >> 2U];
    if (((*ext >> shift) & 0x0FU) == (uint32_t)port) {
        *ext &= ~(0x0FU << shift);
    }
    /* 注：不碰 NVIC —— EXTI15_10_IRQn 上还有其它线在用，禁用整条 IRQ 会连带杀掉
     * 无关中断（详见 pl_gpio.h 该函数注释）。 */
}
