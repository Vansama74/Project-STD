/**
 * @file    app_anhui_proto.c
 * @brief   安徽费显协议 — 注册、帧探测、处理任务
 *
 * 协议来源：2026-S304 费显通信协议。帧 `5A + 屏号 + 命令 + 数据长 + 数据 + CRC + A5`。
 *
 * 冲突纪律：安徽帧头 0x5A 在 RS485/RS232 槽唯一——既有串口协议首字节为
 * 0x7B（青海/山东/贵州/云南/四川MTC）/ 0x0A（四川ETC）/ 0xFF（四川治超/RLS），
 * 首字节互斥快拒成立，无需 EIDE 目录排除；IAP 的 0x5A5A5A5A 仅绑定 UDP（RJ45
 * 槽），与串口槽不相交。安徽非 '{' 帧族，不定义 g_brace_proto_guard。
 * 详见 doc/13_安徽费显协议/README.md §6。
 */

#include "app_anhui_proto.h"

#include "FreeRTOS.h"
#include "initcall.h"
#include "app_dispatch.h"

#include "app_anhui_proto_parse.h"
#include "app_anhui_proto_cmd.h"

/* 地区协议通道 RB：与青海/RLS/四川三协议/山东/贵州/云南等同槽 weak 合并 */
RB_PROVIDE_WEAK(rb_provide_rs485, RB_SIZE_RS485);
RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232);

static proto_mask_t s_anhui_mask;
static proto_mask_t s_anhui_mask_rs232;
static osMessageQueueId_t s_anhui_queue;

/* 静态队列：payload 对齐协议整帧上限（数据长 1B 上限 255 → 帧 ≤261）。
 * 队列体/控制块/任务帧缓冲为静态 SRAM（与青海/贵州/云南等协议模块一致，
 * CQ 才是 CCMRAM 例外）。 */
#define ANHUI_MSG_SIZE    (sizeof(frame_msg_t) + ANHUI_PAYLOAD_MAX)
#define ANHUI_QUEUE_DEPTH (3U)
_Static_assert(ANHUI_PAYLOAD_MAX <= RB_SIZE_RS485, "ANHUI frame must fit RS485 RB");
_Static_assert(ANHUI_PAYLOAD_MAX <= RB_SIZE_RS232, "ANHUI frame must fit RS232 RB");
static StaticQueue_t s_anhui_queue_cb;
static uint8_t s_anhui_queue_buf[ANHUI_QUEUE_DEPTH * ANHUI_MSG_SIZE];
static const osMessageQueueAttr_t s_anhui_queue_attr = {
    .name    = "anhui_queue",
    .cb_mem  = &s_anhui_queue_cb,
    .cb_size = sizeof(s_anhui_queue_cb),
    .mq_mem  = s_anhui_queue_buf,
    .mq_size = sizeof(s_anhui_queue_buf),
};

static proto_probe_sta_t anhui_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                           uint32_t *total_len, uint8_t *aux);

/**
 * @brief  安徽协议模块初始化入口。
 * @note   通过 initcall 自动注册到系统启动流程，负责：
 *         1. 申请 RS485 / RS232 物理通道 RB
 *         2. 注册协议探测器到多协议分发器
 *         3. 绑定 RS485 + RS232（RS232_1 禁止绑定 — 语音专用）
 *         4. 创建协议消息队列与处理任务
 *
 * @warning CH_ID_RS232_1 (PL_UART6) 为语音板 TTS 专用，任何地区协议禁止绑定。
 */
void anhui_proto_init(void)
{
    ring_buffer_t *rb = app_proto_acquire_buf(RB_SLOT_RS485, RB_SIZE_RS485);
    if (rb == nullptr)
        return;

    s_anhui_mask = app_proto_register(anhui_probe_frame, rb);
    if (s_anhui_mask == 0)
        return;

    app_proto_bind_channel(s_anhui_mask, CH_ID_RS485);

    ring_buffer_t *rb_rs232 = app_proto_acquire_buf(RB_SLOT_RS232, RB_SIZE_RS232);
    if (rb_rs232 != nullptr) {
        s_anhui_mask_rs232 = app_proto_register(anhui_probe_frame, rb_rs232);
        if (s_anhui_mask_rs232 != 0)
            app_proto_bind_channel(s_anhui_mask_rs232, CH_ID_RS232);
    }

    s_anhui_queue = osMessageQueueNew(ANHUI_QUEUE_DEPTH, ANHUI_MSG_SIZE, &s_anhui_queue_attr);
    app_proto_set_frame_queue(s_anhui_mask, s_anhui_queue);
    if (s_anhui_mask_rs232 != 0)
        app_proto_set_frame_queue(s_anhui_mask_rs232, s_anhui_queue);

    static const osThreadAttr_t s_anhui_task_attr = {
        .name       = "anhui_handle_task",
        .stack_size = 256 * 4, /* 帧缓冲为 static，不入栈 */
        .priority   = osPriorityNormal,
    };
    osThreadNew(anhui_proto_handle_task, NULL, &s_anhui_task_attr);
}
sw_app_initcall(anhui_proto_init);

/**
 * @brief 安徽协议帧探测（PURE 函数契约：只 peek，无副作用）。
 *
 * 定界模式：长度字段（对齐治超 5.3 模式）——
 * 首字节 0x5A 快拒 → 命令字预筛（提前 FAKE 掉 5A 噪声流，避免大长度字段
 * 把链式探测拖死在 WAIT）→ 数据长算整帧 → 尾字节 0xA5 校验 → READY。
 * CRC 字段协议文档注明「无检验」→ 不校验。屏号非 01 → SKIP 整帧消费。
 */
static proto_probe_sta_t anhui_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                           uint32_t *total_len, uint8_t *aux)
{
    (void)ch;
    (void)aux;
    uint32_t avail = rb_avail(rb, NULL);

    /* 首字节快速拒绝：无数据 → FAKE；首字节非 0x5A → FAKE */
    if (avail == 0)
        return PROTO_PROBE_FAKE;

    uint8_t first_byte;
    rb_peek(rb, 0, &first_byte, 1, NULL);
    if (first_byte != 0x5A)
        return PROTO_PROBE_FAKE;

    /* 需要至少 4 字节：5A + 屏号 + 命令 + 数据长 */
    if (avail < 4U)
        return PROTO_PROBE_WAIT;

    uint8_t head[4];
    rb_peek(rb, 0, head, sizeof(head), NULL);

    /* 命令字预筛：非法命令立即 FAKE（长度字段未验证前先拦，防噪声大长度拖死链式） */
    if (!anhui_cmd_supported(head[2]))
        return PROTO_PROBE_FAKE;

    /* 数据长 1 字节 → 整帧 = 数据长 + 6，≤ 261（ANHUI_PAYLOAD_MAX），天然不越 RB */
    uint32_t frame_len = (uint32_t)head[3] + ANHUI_HEAD_TAIL_SIZE;
    if (avail < frame_len)
        return PROTO_PROBE_WAIT;

    uint8_t tail;
    rb_peek(rb, frame_len - 1U, &tail, 1, NULL);
    if (tail != 0xA5)
        return PROTO_PROBE_FAKE;

    /* 屏号非本设备：帧结构合法但发给别的屏 → SKIP 整帧消费（不污染链式探测） */
    if (head[1] != ANHUI_SCREEN_ID) {
        *total_len = frame_len;
        return PROTO_PROBE_SKIP;
    }

    *total_len = frame_len; /* 仅 READY 路径写输出参数 */
    return PROTO_PROBE_READY;
}

/**
 * @brief  安徽协议帧处理任务。
 * @param  argument  任务参数，当前未使用。
 */
void anhui_proto_handle_task(void *argument)
{
    (void)argument;
    /* 帧接收缓冲：静态 SRAM，不入栈 */
    static uint8_t msg_buf[ANHUI_MSG_SIZE];
    frame_msg_t *msg = (frame_msg_t *)msg_buf;
    for (;;) {
        if (osOK != osMessageQueueGet(s_anhui_queue, msg, NULL, osWaitForever)) {
            continue;
        }
        anhui_parsed_cmd_t cmd = anhui_parse_frame(msg->data, msg->data_len);
        anhui_execute_cmd(msg->ch, &cmd);
    }
}
