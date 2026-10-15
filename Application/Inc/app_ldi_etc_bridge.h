/**
 * @file    app_ldi_etc_bridge.h
 * @brief   LDI → 四川 ETC 费显协议转义钩子（LDI 执行层调用，转义模块实现）
 *
 * LDI 设备能力层（app_ldi_device.c）在命令成功执行后调用本组钩子，由转义模块
 * app_ldi_etc_bridge.c 翻译为四川 ETC 帧、从 RS485 + RS232 双口同发。LDI 侧提供
 * weak 空实现：EIDE 排除转义模块时钩子为无操作。映射与开关见 doc/20 的 README。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* 线程上下文：LDI 处理任务 / LDI 定时任务；text 指针（内含换行 0x0A）仅调用期间有效，
 * 实现方必须立即拷贝。 */

/**
 * @brief E6 显示控制成功：把显示内容镜像为 ETC 显示帧。
 * @param row   行号：0 = 全屏，1 ~ 6 = 单行（与 ETC 行号同义）
 * @param text  已改写（'_' → 0x0A）的 GBK 文本，仅调用期间有效
 * @param len   text 有效字节数
 */
void app_ldi_etc_display(uint8_t row, const uint8_t *text, uint16_t len);

/** @brief E6 清屏成功（含 KeepTime 到期自动清屏）：镜像为 ETC 清屏帧 */
void app_ldi_etc_clear(void);

/**
 * @brief E7 通行信号灯控制成功：镜像为 ETC 灯控帧。
 * @param color  LDI 色码：01H = 绿，02H = 红，03H = 黄（黄色无 ETC 对应帧）
 */
void app_ldi_etc_lamp(uint8_t color);

/**
 * @brief E8 报警器显式开关成功：镜像为 ETC 黄闪开/关帧。
 * @param on  true = 开始报警，false = 停止报警
 * @note  KeepTime 到期自动停止不走本钩子（不补发关帧）。
 */
void app_ldi_etc_alarm(bool on);

/**
 * @brief 周期性心跳：发送 ETC 心跳帧 `0A 08 0D`。
 * @note  由 LDI 定时任务按 3 秒周期无条件调用（不依赖 LDI 命令与通道状态）；
 *        帧字节为现场口径 0x08，非 ETC 协议文档的心跳命令 0x50；
 *        未编入转义模块时本钩子为空实现（零发送）。
 */
void app_ldi_etc_heartbeat(void);
