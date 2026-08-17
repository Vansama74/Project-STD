/**
 * @file    app_factory_test.h
 * @brief   出厂检测模式 — TEST 按键驱动状态机
 *
 * 状态流程：
 *   IDLE ──[TEST]──▶ SHOW_CODE ──[TEST]──▶ DEAD_PIXEL ──[TEST×7]──▶ OBLIQUE_SCAN ──[TEST]──▶ AGING ──[TEST]──▶ 退出
 */

#pragma once

/** @brief 业务数据到达 → 中止工厂测试当前序列，monitor 回 IDLE 待机（不销毁任务，TEST 键保持可用） */
void app_factory_mode_interrupt(void);
