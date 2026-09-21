/**
 * @file    pl_sys.c
 * @brief   系统基础：时钟配置、系统复位
 */

#include "pl_sys.h"
#include "main.h"
#include "initcall.h"

/* ---- 系统时钟：HSE → PLL → 168MHz ---- */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.LSIState       = RCC_LSI_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 4;
    RCC_OscInitStruct.PLL.PLLN       = 168;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }
}

/* ---- 系统复位 ---- */
void pl_system_reset(void)
{
    NVIC_SystemReset();
}

/* ---- 复位原因解码（RCC->CSR 复位标志；纯读，不清标志）----
 * 位定义见 RM0090 RCC_CSR：bit25 BORRSTF / bit26 PINRSTF / bit27 PORRSTF /
 * bit28 SFTRSTF / bit29 IWDGRSTF / bit30 WWDGRSTF / bit31 LPWRRSTF。
 * **不要**在此清标志（本函数的唯一价值就是保留现场）：现场复位后重启再看一次
 * 横幅，即可判断上一次复位是看门狗、软件、引脚还是电源引起。 */
pl_reset_cause_t pl_sys_reset_cause(void)
{
    const uint32_t csr = RCC->CSR;
    /* 位定义用 HAL 的 RCC_FLAG_* + __HAL_RCC_GET_FLAG（CSR 区标志：#define 为 0x70+） */
    pl_reset_cause_t c = {
        .raw  = csr,
        .iwdg = (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != 0U),
        .wwdg = (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != 0U),
        .sft  = (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != 0U),
        .por  = (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != 0U),
        .pin  = (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != 0U),
        .bor  = (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) != 0U),
        .lpw  = (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != 0U),
    };
    return c;
}
