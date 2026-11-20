/**
 * @file    stm32f4xx_it.c
 * @brief   系统异常处理器（外设 ISR 已全部迁移至 Platform/Device 层）
 */

#include "main.h"
#include "stm32f4xx_it.h"

/* ---- HardFault 现场保护：崩溃信息存入全局变量（调试器 Watch 读取） ---- */
static volatile uint32_t g_fault_stack[8]; /* R0,R1,R2,R3,R12,LR,PC,xPSR */
static volatile uint32_t g_fault_pc;       /* 崩溃指令地址 */
static volatile uint32_t g_fault_cfsr;     /* SCB->CFSR 故障状态寄存器 */
static volatile uint32_t g_fault_bfar;     /* SCB->BFAR 总线错误地址 */
static volatile uint32_t g_fault_r0;       /* 故障时 R0 */

void hard_fault_handler_c(uint32_t *stack_frame)
{
    for (int i = 0; i < 8; i++)
        g_fault_stack[i] = stack_frame[i];
    g_fault_pc   = stack_frame[6];
    g_fault_r0   = stack_frame[0];
    g_fault_cfsr = SCB->CFSR;
    g_fault_bfar = SCB->BFAR;
    /* 2026-11-20 诊断：现场写 RTC 备份寄存器（复位不丢）后立即复位。
     * 原实现死循环等 IWDG（~33s），且复位后 bss 清零导致现场全丢；
     * 启动时 app_boot 回读 DR7/DR8/DR9 打印 "HF pc=... cfsr=..."。 */
    HAL_PWR_EnableBkUpAccess();
    RTC->BKP7R = stack_frame[6];   /* 故障 PC */
    RTC->BKP8R = SCB->CFSR;        /* 故障状态 */
    RTC->BKP9R = 0x48465A54U;      /* "HFZT" 标记 */
    NVIC_SystemReset();
    for (;;);
}

[[gnu::naked]] void HardFault_Handler(void)
{
    __asm volatile(
        "TST    LR, #4          \n"
        "ITE    EQ              \n"
        "MRSEQ  R0, MSP         \n"
        "MRSNE  R0, PSP         \n"
        "B      hard_fault_handler_c \n");
}

void NMI_Handler(void)
{
    for (;;);
}

void MemManage_Handler(void)
{
    for (;;);
}

void BusFault_Handler(void)
{
    for (;;);
}

void UsageFault_Handler(void)
{
    for (;;);
}

void DebugMon_Handler(void)
{
}
