/**
 * @file    app_yn_ol_proto.c
 * @brief   云南治超屏协议 — 注册 / 探测 / 帧处理任务
 *
 * 契约一览见 app_yn_ol_proto.h 文件头（本文件实现 ② 掩码隔离 / ③ 互斥守卫 / ④ CCM 放置）。
 * 详见 doc/15_云南治超屏协议/README.md §3、§4、§6
 */

#include "app_yn_ol_proto.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "initcall.h"

#include "app_dispatch.h"
#include "app_yn_ol_proto_cmd.h"
#include "app_yn_ol_proto_parse.h"
#include "pl_task_static.h"

/* 处理任务栈 + TCB 静态落 CCMRAM（启动期创建一次、永不退出；账见 doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md §8）。
 * 自检任务会被反复创建/退出，保持动态（见 app_yn_ol_proto_cmd.c）。 */
PL_TASK_STATIC_STORAGE(yn_ol_handle, 256);

/* `{` 帧族互斥守卫（契约 ③）：多编 → 链接期 multiple definition；STD_ALL_PROTO 开发构建豁免 */
#ifndef STD_ALL_PROTO
__attribute__((used)) char g_brace_proto_guard;
#endif

/* RS485/RS232 槽体与同槽协议 weak 合并；RJ45 槽体由 IAP/LDI/CQ 提供，本模块只 acquire 不 provide */
RB_PROVIDE_WEAK(rb_provide_rs485, RB_SIZE_RS485);
RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232);

static proto_mask_t s_yn_ol_mask;            /* CH_ID_RS485（RS485 槽 RB） */
static proto_mask_t s_yn_ol_mask_rs232;      /* CH_ID_RS232（RS232 槽 RB） */
static proto_mask_t s_yn_ol_mask_tcp_server; /* CH_ID_TCP_SERVER（RJ45 槽 RB） */
static proto_mask_t s_yn_ol_mask_tcp_client; /* CH_ID_TCP_CLIENT（RJ45 槽 RB） */
static osMessageQueueId_t s_yn_ol_queue;

/* 静态队列（四 mask 共用）：队列体 + 控制块 + 任务帧缓冲置 CCMRAM（契约 ④，1152B；理由见 §6） */
[[gnu::section(".ccmram")]] static StaticQueue_t s_yn_ol_queue_cb;
[[gnu::section(".ccmram")]] static uint8_t s_yn_ol_queue_buf[YN_OL_QUEUE_DEPTH * YN_OL_MSG_SIZE];
static const osMessageQueueAttr_t s_yn_ol_queue_attr = {
    .name    = "yn_ol_queue",
    .cb_mem  = &s_yn_ol_queue_cb,
    .cb_size = sizeof(s_yn_ol_queue_cb),
    .mq_mem  = s_yn_ol_queue_buf,
    .mq_size = sizeof(s_yn_ol_queue_buf),
};

static proto_probe_sta_t yn_ol_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                           uint32_t *total_len, uint8_t *aux);

/**
 * @brief  云南治超屏协议模块初始化（sw_app_initcall 自注册）。
 * @note   注册五步 × 四通道（doc/08_协议模块接入规则）：槽 acquire → 每通道独立 mask
 *         register + bind → 四 mask 共用一静态队列 → 处理任务；末尾装载 0x49 持久化参数
 *         （要求 dev_w25qxx 已初始化，与任务/队列无耦合）。
 */
void yn_ol_proto_init(void)
{
    ring_buffer_t *rb = app_proto_acquire_buf(RB_SLOT_RS485, RB_SIZE_RS485);
    if (rb == nullptr)
        return;

    s_yn_ol_mask = app_proto_register(yn_ol_probe_frame, rb);
    if (s_yn_ol_mask == 0)
        return;

    app_proto_bind_channel(s_yn_ol_mask, CH_ID_RS485);

    ring_buffer_t *rb_rs232 = app_proto_acquire_buf(RB_SLOT_RS232, RB_SIZE_RS232);
    if (rb_rs232 != nullptr) {
        s_yn_ol_mask_rs232 = app_proto_register(yn_ol_probe_frame, rb_rs232);
        if (s_yn_ol_mask_rs232 != 0)
            app_proto_bind_channel(s_yn_ol_mask_rs232, CH_ID_RS232);
    }

    /* 网口槽（TCP Server / TCP Client 共享 RJ45 RB）：独立 mask 各挂一次；槽体由
     * IAP/LDI/CQ weak 提供（本模块不 provide），无提供者时静默跳过、串口不受影响。
     * 不与 CQ 竞争：CQ 不绑 TCP，probe 掩码过滤后不会被调用（契约 ②）。 */
    ring_buffer_t *rb_rj45 = app_proto_acquire_buf(RB_SLOT_RJ45, RB_SIZE_RJ45);
    if (rb_rj45 != nullptr) {
        s_yn_ol_mask_tcp_server = app_proto_register(yn_ol_probe_frame, rb_rj45);
        if (s_yn_ol_mask_tcp_server != 0)
            app_proto_bind_channel(s_yn_ol_mask_tcp_server, CH_ID_TCP_SERVER);

        s_yn_ol_mask_tcp_client = app_proto_register(yn_ol_probe_frame, rb_rj45);
        if (s_yn_ol_mask_tcp_client != 0)
            app_proto_bind_channel(s_yn_ol_mask_tcp_client, CH_ID_TCP_CLIENT);
    }

    s_yn_ol_queue = osMessageQueueNew(YN_OL_QUEUE_DEPTH, YN_OL_MSG_SIZE, &s_yn_ol_queue_attr);
    app_proto_set_frame_queue(s_yn_ol_mask, s_yn_ol_queue);
    if (s_yn_ol_mask_rs232 != 0)
        app_proto_set_frame_queue(s_yn_ol_mask_rs232, s_yn_ol_queue);
    if (s_yn_ol_mask_tcp_server != 0)
        app_proto_set_frame_queue(s_yn_ol_mask_tcp_server, s_yn_ol_queue);
    if (s_yn_ol_mask_tcp_client != 0)
        app_proto_set_frame_queue(s_yn_ol_mask_tcp_client, s_yn_ol_queue);

    /* 0x49 屏体参数持久化：上电装载（无记录/损坏 → 保持默认 FONT_16/FONT_ST） */
    (void)yn_ol_screen_cfg_load();

    static const osThreadAttr_t s_yn_ol_task_attr = {
        .name       = "yn_ol_handle_task",
        .priority   = osPriorityNormal,
        PL_TASK_STATIC_ATTR(yn_ol_handle, 256),
    };
    osThreadNew(yn_ol_proto_handle_task, NULL, &s_yn_ol_task_attr);
}
sw_app_initcall(yn_ol_proto_init);

/**
 * @brief  `{` 帧族帧探测（PURE：只 rb_peek，无副作用、无阻塞）。
 * @param  rb         源环形缓冲（只读）。
 * @param  total_len  READY 时写出整帧长（= 长度字段 + 4）。
 * @return READY / WAIT / FAKE；命令字白名单复用 `yn_ol_cmd_from_byte()`（单一真源）。
 * @note   7 步判定链（首字节快拒 → 命令字白名单 → 长度/尾字节校验）与 CQ 兼容路径
 *         见 doc/15_云南治超屏协议/README.md §3；`ch` / `aux` 为 probe 契约参数，未使用。
 */
static proto_probe_sta_t yn_ol_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                           uint32_t *total_len, uint8_t *aux)
{
    (void)ch;
    (void)aux;

    const uint32_t avail = rb_avail(rb, NULL);
    if (avail == 0U)
        return PROTO_PROBE_FAKE;

    uint8_t first;
    rb_peek(rb, 0U, &first, 1U, NULL);
    if (first != (uint8_t)YN_OL_STX)
        return PROTO_PROBE_FAKE;

    if (avail < YN_OL_HEAD_LEN)
        return PROTO_PROBE_WAIT;

    uint8_t head[YN_OL_HEAD_LEN];
    rb_peek(rb, 0U, head, YN_OL_HEAD_LEN, NULL);

    /* 命令字白名单快筛：'6'/'7'/'9'/'B' 及未知字节 → FAKE（交链式探测/1 字节重同步，不消费） */
    if (yn_ol_cmd_from_byte(head[YN_OL_CMD_OFFSET]) == YN_OL_PCMD_INVALID)
        return PROTO_PROBE_FAKE;

    const uint32_t frame_len = (uint32_t)head[YN_OL_LEN_OFFSET] + YN_OL_FRAME_OVERHEAD;
    if (avail < frame_len)
        return PROTO_PROBE_WAIT;

    uint8_t tail;
    rb_peek(rb, frame_len - 1U, &tail, 1U, NULL);
    if (tail != (uint8_t)YN_OL_ETX)
        return PROTO_PROBE_FAKE;

    *total_len = frame_len; /* 仅 READY 路径写输出参数 */
    return PROTO_PROBE_READY;
}

/**
 * @brief  帧处理任务：帧队列 → 解析 → 执行。
 * @param  argument  未使用。
 */
void yn_ol_proto_handle_task(void *argument)
{
    (void)argument;
    [[gnu::section(".ccmram")]] static uint8_t msg_buf[YN_OL_MSG_SIZE];
    frame_msg_t *msg = (frame_msg_t *)msg_buf;

    for (;;) {
        if (osOK != osMessageQueueGet(s_yn_ol_queue, msg, NULL, osWaitForever))
            continue;

        const yn_ol_parsed_cmd_t cmd = yn_ol_parse_frame(msg->data, (uint16_t)msg->data_len);


        int exec_ret = (int)YN_OL_EXEC_NOT_RUN;
        if (cmd.sta == YN_OL_PARSE_OK)
            exec_ret = yn_ol_execute_cmd(msg->ch, &cmd);

        (void)exec_ret;
    }
}
