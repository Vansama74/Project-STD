/**
 * @file    app_gx_proto_cmd.h
 * @brief   广西费显协议执行层（显示/亮度/灯控/语音/版本/开机界面/字体）
 *
 * 契约一览见 app_gx_proto.h 文件头；渲染互斥、无显示降级与亮度映射见
 * doc/21_广西费显协议/README.md §2
 */

#pragma once

#include "app_dispatch.h" /* channel_t（应答原路回送） */
#include "app_gx_proto.h"

/**
 * @brief  解析并执行一帧（失败静默——协议未定义错误应答）。
 * @param  src_ch   收到本帧的通道（0x1B 应答原路回送；nullptr 则不回送）。
 * @param  raw      整帧字节（[0] = 0xAA … 末字节定界/载荷，probe 已验帧长）。
 * @param  raw_len  整帧字节数。
 */
void gx_execute_cmd(const channel_t *src_ch, const uint8_t *raw, uint16_t raw_len);

/**
 * @brief  总显示时间到期判定（处理任务每 GX_TICK_MS 节拍调用）。
 * @note   到期清屏并恢复开机画面（03 03 设置过则用它，否则回项目通用默认画面
 *         「欢迎行驶 / 高速公路」）；不新建任务、不阻塞。
 */
void gx_display_timer_tick(void);
