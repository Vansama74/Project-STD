/**
 * @file    pl_task_static.h
 * @brief   FreeRTOS 任务「栈 + 控制块」静态分配到 CCMRAM（ucHeap 压力削减）
 *
 * ## 背景
 *
 * `configTOTAL_HEAP_SIZE` 固定 36KB（`ucHeap` 落在 SRAM `.bss`，见
 * `FreeRTOSConfig.h` / `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md`），
 * 而 SRAM 已近满（PROTO=ALL 余量数百 B）——**不能再扩 ucHeap**。
 * 多协议共存构建的任务数量已 30+，启动期「任务栈 + TCB」需求逼近 36KB，
 * 新增任务（如云南治超）即把堆打穿 → `pvPortMalloc` 失败 →
 * `vApplicationMallocFailedHook` 死循环 → 通道（RS485/RS232）无声失效。
 *
 * ## 为什么可以放 CCMRAM
 *
 * CCMRAM（64KB，0x10000000，CPU 专用 D-bus、零等待）**不可被 DMA/ETH 访问**，
 * 但任务栈与控制块只被 CPU 读写（上下文切换、函数栈帧），从不交给 DMA——
 * 因此是 CCM 的合法且高价值用途：把这部分需求从 ucHeap **整体移出**，
 * 每移一个 1KB 任务即释放 1144B 堆（= 栈块 1032 + TCB 块 112，heap_4 8B 对齐口径），
 * 代价是 1124B CCM（= 栈 1024 + StaticTask_t 100）。
 *
 * 与既有 `StaticQueue_t` / 显存 CCM 放置（`[[gnu::section(".ccmram")]]`）同源；
 * `.ccmram` 段由 `Compiler/startup.c` 整体清零，行为等同 `.bss`，
 * 满足 FreeRTOS「静态 TCB 缓冲区须已清零」的要求。
 *
 * ## 用法（模块内三步）
 *
 * ```c
 * #include "pl_task_static.h"
 *
 * // ① 声明静态存储（进 CCMRAM）
 * PL_TASK_STATIC_STORAGE(iap_handle, 256);   // 256 words = 1KB
 *
 * // ② 接入 osThreadAttr_t（stack_size 仍为 **字节数**，CMSIS 内部再折算 words）
 * static const osThreadAttr_t iap_task_attr = {
 *     .name       = "iap_handle_task",
 *     .priority   = osPriorityNormal,
 *     PL_TASK_STATIC_ATTR(iap_handle, 256),
 * };
 *
 * // ③ osThreadNew 原样调用——cb_mem/stack_mem 齐备时 CMSIS 自动走
 * //    xTaskCreateStatic，不再触碰 ucHeap
 * ```
 *
 * ## 放置层（依赖方向）
 *
 * 本头文件位于 **Platform 层**（`Platform/Inc`）：Application 与 Device 层**都**
 * 依赖 Platform，因此 Device（如 `dev_display.c` 的 `scan_task`）也能合法使用；
 * 若留在 `Application/Inc` 会被 Device 反向包含，违反「依赖方向严格单向」纪律。
 *
 * ## 约束 / 纪律
 *
 * - **仅用于「整个生命周期只创建一次」的任务**（协议处理任务、监听循环任务）。
 *   会被反复创建/退出的任务（如 UDP per-connection 任务）复用同一份静态存储
 *   时必须确保「前一个已退出、后一个才创建」的串行关系，否则两个任务共用同一
 *   栈 = 立即栈踩踏；不满足该前提的任务请保持动态分配并只做判空处理。
 * - CCMRAM 总量有限（64KB，含显存与 CQ/GZ_OL/YN_OL 队列体），新增转换前用
 *   `arm-none-eabi-size -A` 看 `.ccmram` 余量（doc/06-04 §7）。
 * - 任务栈不得交给 DMA（CCM 不可 DMA 可达）；本项目无此用法，保持即可。
 */
#pragma once

#include "FreeRTOS.h"
#include "task.h"

#include "cmsis_os2.h"

/* CCMRAM 段（Compiler/STM32F407XX_FLASH.ld 定义，NOLOAD + 启动清零） */
#define PL_TASK_CCMRAM __attribute__((section(".ccmram"), aligned(8)))

/**
 * @brief 声明静态任务控制块 + 任务栈（均落 CCMRAM，8 字节对齐）
 * @param tag         本模块内唯一标识（用作变量名后缀）
 * @param stack_words 栈深度，单位 = StackType_t（4 字节），与 FreeRTOS 口径一致
 */
#define PL_TASK_STATIC_STORAGE(tag, stack_words)                                                   \
    static StaticTask_t s_task_##tag##_tcb PL_TASK_CCMRAM;                                         \
    static StackType_t s_task_##tag##_stack[(stack_words)] PL_TASK_CCMRAM

/**
 * @brief 把静态存储接入 `osThreadAttr_t`（须放在该结构体的初始化列表内）
 * @param tag         与 PL_TASK_STATIC_STORAGE 相同的标识
 * @param stack_words 与 PL_TASK_STATIC_STORAGE 相同的栈深度（words）
 */
#define PL_TASK_STATIC_ATTR(tag, stack_words)                                                      \
    .stack_size = (stack_words) * sizeof(StackType_t),                                              \
    .cb_mem = (void *)&s_task_##tag##_tcb,                                                          \
    .cb_size = sizeof(s_task_##tag##_tcb),                                                          \
    .stack_mem = (void *)s_task_##tag##_stack
