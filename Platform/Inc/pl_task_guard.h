/**
 * @file    pl_task_guard.h
 * @brief   任务创建失败的统一判空报告（FreeRTOS 堆耗尽可观测化）
 *
 * ## 背景
 *
 * `osThreadNew()`（CMSIS-RTOS V2）在 `xTaskCreate()` 因堆耗尽返回 `pdFAIL`
 * 时**只返回 NULL**，不产生任何日志；此前各通道启动处直接丢弃返回值 →
 * 「RS485/RS232 任务没起来、串口无声失效」在现场完全不可见，唯一现象是
 * `vApplicationMallocFailedHook` 死循环（见 `app_boot.c`）。
 *
 * 本头文件把「创建 + 判空 + RTT 报告」收敛为一处，供各通道/引擎的启动点使用。
 *
 * ## 用法
 *
 * ```c
 * return pl_task_create_checked(osThreadNew(rs485_task, self, &attr), "rs485_task");
 * ```
 *
 * ## 说明
 *
 * - 报告走 **RTT 通道 0**，恒开（不受 `APP_DIAG_BANNER` 影响）——通道静默失效
 *   属于错误路径，代价仅为每处数十字节常量字符串。
 * - 打印 `xPortGetFreeHeapSize()/xPortGetMinimumEverFreeHeapSize()`：前者是
 *   当前余量，后者是**开机以来最小余量**（判断「是否曾经擦边」的权威值）。
 * - 已改用静态 CCMRAM 存储的任务（见 `pl_task_static.h`）创建不可能失败
 *   （`xTaskCreateStatic` 仅当传入的空栈指针为 NULL 才失败，而那是编译期常量），
 *   仍在启动点判空属防御性冗余，不额外增加运行开销。
 */
#pragma once

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"

#include "SEGGER_RTT.h"

/**
 * @brief 任务创建结果判空；失败时 RTT 报错（含堆余量与历史最小余量）
 * @param tid `osThreadNew` 的返回值
 * @param tag 任务名（静态字符串，便于现场对号入座）
 * @return 原样返回 tid（成功非 NULL；失败 NULL）
 */
static inline osThreadId_t pl_task_create_checked(osThreadId_t tid, const char *tag) {
    if (tid == nullptr) {
        SEGGER_RTT_printf(0, "[err] task '%s' create FAILED (heap exhausted): free=%u min=%u\n",
                          (tag != nullptr) ? tag : "?", (unsigned)xPortGetFreeHeapSize(),
                          (unsigned)xPortGetMinimumEverFreeHeapSize());
    }
    return tid;
}
