/**
 * @file    pl_task_guard.h
 * @brief   任务创建结果判空的统一收敛点（FreeRTOS 堆耗尽可观测化）
 *
 * `osThreadNew()`（CMSIS-RTOS V2）在 `xTaskCreate()` 因堆耗尽失败时**只返回 NULL**、
 * 不产生任何日志；各启动点若直接丢弃返回值，「任务没起来、通道无声失效」将完全不可见。
 * 堆预算与水位见 doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md。
 *
 * 用法：
 *   return pl_task_create_checked(osThreadNew(rs485_task, self, &attr), "rs485_task");
 *
 * 说明：已改用静态 CCMRAM 存储的任务（pl_task_static.h，xTaskCreateStatic）不会创建
 * 失败，此处判空属防御性冗余，不增加运行开销。
 */
#pragma once

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief 任务创建结果判空
 * @param tid `osThreadNew` 的返回值
 * @param tag 任务名（静态字符串，供调试器/调用点定位）
 * @return 原样返回 tid（成功非 NULL；失败 NULL）
 */
static inline osThreadId_t pl_task_create_checked(osThreadId_t tid, const char *tag) {
    (void)tag;
    return tid;
}
