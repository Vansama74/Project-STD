/**
 * @file    app_qh_proto.c
 * @brief   青海治超站费显协议（V1.0）— 注册 / 探测 / 帧处理任务
 *
 * 通道口径（V1.0 协议要求「同时支持网口和串口」）：
 *   - 串口：RS485（USART1）+ RS232（USART3），9600-8-N-1 由 Core/Src/usart.c 配置；
 *   - 网口：UDP，端口用**本工程约定 10011**（协议原文 2929 未采用，见用户裁决）。
 *   `CH_ID_RS232_1`（USART6）为语音板 TTS 专用，任何地区协议禁止绑定。
 */

#include "app_qh_proto.h"

#include <stdbool.h>

#include "FreeRTOS.h"
#include "initcall.h"
#include "app_dispatch.h"

#include "app_qh_proto_parse.h"
#include "app_qh_proto_cmd.h"
#include "pl_task_static.h"

/* ---- 任务静态存储（栈 + TCB 落 CCMRAM，见 pl_task_static.h）----
 * qh_handle_task：启动期创建一次、永不退出；静态化后不再占 ucHeap（省 1144B），
 * CCM 占 1124B。任务栈仅被 CPU 访问，不经 DMA。 */
PL_TASK_STATIC_STORAGE(qh_handle, 256);

/* '{' 帧族互斥守卫：同一构建只允许编入一个 '{' 帧族协议（青海/山东/贵州/四川MTC）。
 * 多个同时编入 → 链接期 multiple definition 强制报错；
 * 全协议开发构建定义 STD_ALL_PROTO 跳过守卫（共存由 probe 注册序与文档纪律约束）。 */
#ifndef STD_ALL_PROTO
__attribute__((used)) char g_brace_proto_guard;
#endif

/* 地区协议通道 RB：与 RLS（485）等同槽 weak 合并；RJ45 槽由 IAP/LDI/CQ 提供，本模块只 acquire */
RB_PROVIDE_WEAK(rb_provide_rs485, RB_SIZE_RS485);
RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232);

static proto_mask_t s_qh_mask;        /* CH_ID_RS485 */
static proto_mask_t s_qh_mask_rs232;  /* CH_ID_RS232 */
static proto_mask_t s_qh_mask_udp;    /* CH_ID_UDP（10011，RJ45 槽） */
static osMessageQueueId_t s_qh_queue;

/* 静态队列：payload 对齐协议上限（len 字段 1B → 帧 ≤259），勿用 IAP 的 1044 */
#define QH_PAYLOAD_MAX  (259U) /* '{' + cmd + len + data[0..255] + '}' */
#define QH_MSG_SIZE     (sizeof(frame_msg_t) + QH_PAYLOAD_MAX)
#define QH_QUEUE_DEPTH  (3U)
/* 契约：payload 上限须 ≤ 串口 / 网口 RB 容量（越限被 RB 截断，静默收错帧），
 * 详见 doc/08_协议模块接入规则/01_通用规则与模块骨架.md §4。 */
static StaticQueue_t s_qh_queue_cb;
static uint8_t s_qh_queue_buf[QH_QUEUE_DEPTH * QH_MSG_SIZE];
static const osMessageQueueAttr_t s_qh_queue_attr = {
    .name    = "qh_queue",
    .cb_mem  = &s_qh_queue_cb,
    .cb_size = sizeof(s_qh_queue_cb),
    .mq_mem  = s_qh_queue_buf,
    .mq_size = sizeof(s_qh_queue_buf),
};

static proto_probe_sta_t qh_proto_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                              uint32_t *total_len, uint8_t *aux);

/**
 * @brief  青海协议模块初始化入口。
 * @note   通过 initcall 自动注册到系统启动流程，负责：
 *         1. 申请 RS485 / RS232 / RJ45 物理通道 RB
 *         2. 注册协议探测器到多协议分发器（每通道一个独立 mask）
 *         3. 绑定 RS485 + RS232 + UDP(10011)
 *         4. 创建协议消息队列与处理任务
 *
 * @warning CH_ID_RS232_1 (PL_UART6) 为语音板 TTS 专用，任何地区协议禁止绑定。
 */
void qh_proto_init(void)
{
    ring_buffer_t *rb = app_proto_acquire_buf(RB_SLOT_RS485, RB_SIZE_RS485);
    if (rb == nullptr)
        return;

    s_qh_mask = app_proto_register(qh_proto_probe_frame, rb);
    if (s_qh_mask == 0)
        return;

    app_proto_bind_channel(s_qh_mask, CH_ID_RS485);

    ring_buffer_t *rb_rs232 = app_proto_acquire_buf(RB_SLOT_RS232, RB_SIZE_RS232);
    if (rb_rs232 != nullptr) {
        s_qh_mask_rs232 = app_proto_register(qh_proto_probe_frame, rb_rs232);
        if (s_qh_mask_rs232 != 0)
            app_proto_bind_channel(s_qh_mask_rs232, CH_ID_RS232);
    }

    /* 网口槽（CH_ID_UDP = 10011）：独立 mask 挂 RJ45 RB；无提供者 → 静默跳过网口绑定 */
    ring_buffer_t *rb_rj45 = app_proto_acquire_buf(RB_SLOT_RJ45, RB_SIZE_RJ45);
    if (rb_rj45 != nullptr) {
        s_qh_mask_udp = app_proto_register(qh_proto_probe_frame, rb_rj45);
        if (s_qh_mask_udp != 0)
            app_proto_bind_channel(s_qh_mask_udp, CH_ID_UDP);
    }

    s_qh_queue = osMessageQueueNew(QH_QUEUE_DEPTH, QH_MSG_SIZE, &s_qh_queue_attr);
    app_proto_set_frame_queue(s_qh_mask, s_qh_queue);
    if (s_qh_mask_rs232 != 0)
        app_proto_set_frame_queue(s_qh_mask_rs232, s_qh_queue);
    if (s_qh_mask_udp != 0)
        app_proto_set_frame_queue(s_qh_mask_udp, s_qh_queue);

    static const osThreadAttr_t s_qh_task_attr = {
        .name       = "qh_handle_task",
        .priority   = osPriorityNormal,
        PL_TASK_STATIC_ATTR(qh_handle, 256),
    };
    osThreadNew(qh_proto_handle_task, NULL, &s_qh_task_attr);
}
sw_app_initcall(qh_proto_init);

/**
 * @brief 青海协议帧探测（PURE 函数契约：只 peek，无副作用）
 *
 * 帧格式：`{` (0x7B) + 命令字 + 二进制长度 + 数据 + `}` (0x7D)，整帧长 = len + 4。
 *
 * 首字节快速拒绝：首字节非 '{' (0x7B) → 立即 FAKE。
 * 命令字白名单 = V1.0 全集：'1'~'5'、0x3A、0x3B、0x3C；
 * 非白名单命令字（含 2019 版遗留的 '6'~'9'/'A'/'B'）整帧 FAKE，交分发器逐字节重同步。
 */
static proto_probe_sta_t qh_proto_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                              uint32_t *total_len, uint8_t *aux)
{
    (void)ch;
    (void)aux;
    uint32_t avail = rb_avail(rb, NULL);

    /* 首字节快速拒绝：无数据 → FAKE；首字节非 '{' → FAKE */
    if (avail == 0)
        return PROTO_PROBE_FAKE;

    uint8_t first_byte;
    rb_peek(rb, 0, &first_byte, 1, NULL);
    if (first_byte != '{')
        return PROTO_PROBE_FAKE;

    /* 需要至少 3 字节：'{' + 命令码 + 长度字段 */
    if (avail < 3U)
        return PROTO_PROBE_WAIT;

    uint8_t head[3];
    rb_peek(rb, 0, head, sizeof(head), NULL);

    /* V1.0 命令字白名单 */
    bool cmd_ok = (head[1] >= '1' && head[1] <= '5') ||
                  head[1] == 0x3A || head[1] == 0x3B || head[1] == 0x3C;
    if (!cmd_ok)
        return PROTO_PROBE_FAKE;

    /* 长度字段是二进制字节值，不是 ASCII 数字。 */
    uint16_t data_len  = head[2];
    uint32_t frame_len = (uint32_t)data_len + 4U;
    if (avail < frame_len)
        return PROTO_PROBE_WAIT;

    uint8_t tail;
    rb_peek(rb, frame_len - 1U, &tail, 1, NULL);
    if (tail != '}')
        return PROTO_PROBE_FAKE;

    *total_len = frame_len; /* 仅 READY 路径写输出参数 */
    return PROTO_PROBE_READY;
}

/**
 * @brief  青海协议帧处理任务。
 * @param  argument  任务参数，当前未使用。
 */
void qh_proto_handle_task(void *argument)
{
    (void)argument;
    static uint8_t msg_buf[QH_MSG_SIZE];
    frame_msg_t *msg = (frame_msg_t *)msg_buf;
    for (;;) {
        if (osOK != osMessageQueueGet(s_qh_queue, msg, NULL, osWaitForever)) {
            continue;
        }
        qh_parsed_cmd_t cmd = qh_parse_frame(msg->data, msg->data_len);
        qh_execute_cmd(msg->ch, &cmd);
    }
}