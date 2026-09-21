/**
 * @file    pl_sys.h
 * @brief   系统基础接口：时钟配置、复位、全局 HAL MSP 初始化（Platform 层）
 *
 * 合并自 pl_clock.h / pl_system.h / pl_msp.c，
 * 统一管理芯片级基础功能，均为 Platform 层对外暴露的单一职责接口。
 */

#pragma once

#include <stdint.h>

/* ---- 系统时钟：HSE → PLL → 168MHz SYSCLK (APB1=42MHz, APB2=84MHz) ---- */
void SystemClock_Config(void);

/* ---- 阻塞延时（毫秒） ---- */
void pl_delay_ms(uint32_t ms);

/* ---- 系统复位：软件复位 MCU ---- */
void pl_system_reset(void);

/* ---- 复位原因（RCC->CSR 复位标志解码；2026-09-18 YN_OL TCP 现场取证）----
 *
 * 用途：开机横幅打印「上一次复位是谁造成的」，用于区分
 *   ① 固件死机（IWDG 看门狗超时复位，`iwdg`）
 *   ② 软件复位（`NVIC_SystemReset` 命令路径 / 烧录器 SYSRESETREQ，`sft`）
 *   ③ 外部复位（NRST 引脚：烧录器、按键、外部看门狗，`pin`）
 *   ④ 电源事件（上电/掉电 `por`、欠压 `bor`）
 * RCC->CSR 的复位标志只由「写 RMVF」或上电清除，本工程全链路无清除点 ⇒
 * 开机第一现场可读；现场「板子自己重启」与「有人重烧了板子」由此可分。
 * 必须在任何可能写 RCC->CSR 的代码之前调用（本工程在 init_task 横幅处调用）。 */
typedef struct {
    uint32_t raw; /**< RCC->CSR 原值（含 LSION/LSIRDY 等非复位位） */
    bool iwdg;    /**< 独立看门狗复位（IWDGRSTF, bit29） */
    bool wwdg;    /**< 窗口看门狗复位（WWDGRSTF, bit30；本工程未用） */
    bool sft;     /**< 软件复位（SFTRSTF, bit28：NVIC_SystemReset / SYSRESETREQ） */
    bool por;     /**< 上电/掉电复位（PORRSTF, bit27） */
    bool pin;     /**< NRST 引脚复位（PINRSTF, bit26） */
    bool bor;     /**< 欠压复位（BORRSTF, bit25） */
    bool lpw;     /**< 低功耗管理复位（LPWRRSTF, bit31） */
} pl_reset_cause_t;

/** @brief 读取并解码上一次复位原因（纯读，无副作用，不清标志）。 */
pl_reset_cause_t pl_sys_reset_cause(void);
