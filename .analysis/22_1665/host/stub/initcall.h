/**
 * @file    initcall.h
 * @brief   22-1665 宿主差分测试桩 — initcall（**不是**真机头文件）
 *
 * 宿主不需要链接期自注册段：`hw_dev_initcall(fn)` 展开为空，
 * harness 显式调用 `dev_display_22_1665_init()`。
 */
#pragma once

#define hw_dev_initcall(fn) /* 宿主桩：不注册，由 harness 显式调用 init */
