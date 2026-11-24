/**
 * @file    pl_dwt.c
 * @brief   DWT 周期计数器（ARM Cortex-M4 调试单元）+ HAL_Delay 让出式改写
 */

#include "pl_dwt.h"
#include "stm32f4xx_hal.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"

void pl_dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t pl_dwt_get_cycles(void)
{
    return DWT->CYCCNT;
}

void pl_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

/**
 * @brief   HAL_Delay 让出式改写（__weak 覆盖）
 *
 * 原实现为 uwTick 忙等。忙等在 RTOS 下会饿死低优先级任务——本工程 TIM3
 * 行同步事件经 FreeRTOS Timer 守护任务（configTIMER_TASK_PRIORITY=2）中转
 * 到 scan_task，任何毫秒级忙等（HAL_ETH_Transmit_IT 内 2×1ms、Start/Stop_IT
 * 各 1ms 等）都会周期性地让扫描停摆 → 面板锁存末行呈现横杠闪烁
 * （现场 2026-11-21 定级，同 Vansama74 心跳闪屏旧案机理）；EthIf 重建路径
 * 的忙等还会在 Realtime 优先级上冻结全系统（2026-11-20 拔线冻结链）。
 * 调度器运行后改用 osDelay 让出；启动早期（initcall 阶段，dev_eth_init
 * 的 PHY 轮询/HAL_ETH_Init 会经 pl_delay_ms 调用 HAL_Delay）回退 uwTick
 * 忙等——TIM7 时基在 HAL_Init 已启动，此时 uwTick 可用。
 */
void HAL_Delay(uint32_t Delay)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        (void)osDelay(Delay);
    } else {
        uint32_t tickstart = HAL_GetTick();
        uint32_t wait      = Delay;
        while ((HAL_GetTick() - tickstart) < wait) {
        }
    }
}

