#pragma once

#include "string.h"

#include "app_rls.h"
#include "app_dispatch.h"

typedef struct {
    uint8_t light_level;
    uint8_t color;
    uint8_t style;
    uint8_t pic_num;
    uint8_t bitmap[];
} rls_dispaly_t;

/**
 * RLS 命令处理函数指针类型
 * @param meta  通道元信息（来源通道类型、编号等）
 * @param data  指向帧 DATA 域首字节
 */
typedef void (*rls_cmd_handler_fn_t)(channel_t *, void *);

/** RLS 命令处理函数表，按命令码索引 */
extern const rls_cmd_handler_fn_t g_rls_cmd_table[];

/**
 * @brief 干接点车道状态轮询（rls_handle_task 队列超时 100ms 节拍调用）。
 *
 * SW1=ETC专用 / SW2=ETC/人工 / SW3=车道关闭（低有效=闭合）。两级采样消抖，
 * 仅在状态变化瞬间全屏渲染一次，不抢协议 bitmap 帧的正常显示。
 */
void rls_dry_contact_poll(void);
