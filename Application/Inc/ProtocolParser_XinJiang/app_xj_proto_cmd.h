/**
 * @file    app_xj_proto_cmd.h
 * @brief   新疆费额显示器协议执行层（显示/灯控/语音/维护屏）
 *
 * 契约一览见 app_xj_proto.h 文件头（本文件实现 ② 行切分、③ 颜色、④ 亮度、⑦ 维护屏）；
 * 渲染互斥与无显示降级详见 doc/19_新疆费显协议/README.md §2、§4
 */

#pragma once

#include "app_xj_proto.h"

/**
 * @brief  解析并执行一帧（失败静默——协议未定义的错误无应答）。
 * @param  src_ch   收到本帧的通道（§3.2 应答原路回送用；nullptr 则不回送）。
 * @param  raw      整帧字节（[0]=0xFF … [len-1]=BCC，probe 已验长度/BCC）。
 * @param  raw_len  整帧字节数。
 * @note   解析结果只存在于本函数一帧内，**不得**由调用方具名持有：`xj_parsed_cmd_t`
 *         约 64B，具名副本会与本函数帧内的副本并存，在 256 words 的任务栈上逼近上限
 *         （见 doc/19 §2.8 栈预算）。调用方只传裸帧，零拷贝、不占任务栈。
 */
void xj_execute_cmd(const channel_t *src_ch, const uint8_t *raw, uint16_t raw_len);

/**
 * @brief  渲染心跳超时维护屏：整屏清黑 + 红字居中「系统维护中」（FONT_16/FONT_ST）。
 * @note   仅由 `app_xj_proto.c` 在 `XJ_FAULT_SCREEN=1` 时的超时路径调用；无显示实例时
 *         直接返回（`dev_display_get()` 判空）。整段持 `app_scroll_render_lock()`。
 */
void xj_fault_screen_show(void);

/**
 * @brief  清除维护屏（整屏清黑后提交），避免解除后残字。
 * @note   收到任意有效帧时由 `app_xj_proto.c` 调用一次（先于该帧执行，见任务注释）。
 */
void xj_fault_screen_clear(void);
