/**
 * @file    app_gz_ol_proto.c
 * @brief   贵州治超屏协议 — 注册 / 探测 / 帧处理任务
 *
 * 契约一览见 app_gz_ol_proto.h 文件头（本文件实现 ③ CCM 放置）。
 * 详见 doc/14_贵州治超屏协议/README.md §3、§4、§11、§13
 */

#include "app_gz_ol_proto.h"

#include "FreeRTOS.h"
#include "initcall.h"

#include "app_dispatch.h"
#include "app_gz_ol_proto_cmd.h"
#include "app_gz_ol_proto_parse.h"
#include "pl_task_static.h"

/* 处理任务栈 + TCB 静态落 CCMRAM（只创建一次、永不退出，不占 ucHeap；账见
 * doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md §8） */
PL_TASK_STATIC_STORAGE(gz_ol_handle, 256);

/* RS485/RS232 槽体与同槽协议 weak 合并；RJ45 槽体由 IAP/LDI/CQ 提供，本模块只 acquire 不 provide */
RB_PROVIDE_WEAK(rb_provide_rs485, RB_SIZE_RS485);
RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232);

static proto_mask_t s_gz_ol_mask;         /* CH_ID_RS485（RS485 槽 RB） */
static proto_mask_t s_gz_ol_mask_rs232;   /* CH_ID_RS232（RS232 槽 RB） */
static proto_mask_t s_gz_ol_mask_udp;     /* CH_ID_UDP 10011（RJ45 槽 RB，同槽 weak 提供） */
static proto_mask_t s_gz_ol_mask_gzol;    /* CH_ID_UDP_GZOL 业务口（读 Sector1 net_cfg.port） */
static osMessageQueueId_t s_gz_ol_queue;

/* 静态队列（四 mask 共用）：队列体 + 控制块 + 任务帧缓冲置 CCMRAM
 * （契约 ③，1136B；理由详见 doc/14_贵州治超屏协议/README.md §6） */
[[gnu::section(".ccmram")]] static StaticQueue_t s_gz_ol_queue_cb;
[[gnu::section(".ccmram")]] static uint8_t s_gz_ol_queue_buf[GZ_OL_QUEUE_DEPTH * GZ_OL_MSG_SIZE];
static const osMessageQueueAttr_t s_gz_ol_queue_attr = {
    .name    = "gz_ol_queue",
    .cb_mem  = &s_gz_ol_queue_cb,
    .cb_size = sizeof(s_gz_ol_queue_cb),
    .mq_mem  = s_gz_ol_queue_buf,
    .mq_size = sizeof(s_gz_ol_queue_buf),
};

static proto_probe_sta_t gz_ol_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                           uint32_t *total_len, uint8_t *aux);

/**
 * @brief  协议注册与任务创建（`sw_app_initcall` 自注册）。
 * @note   注册五步（doc/08_协议模块接入规则/01_通用规则与模块骨架.md §2）：槽 acquire → 每通道独立 mask register + bind
 *         → 四 mask 共用一静态队列 → 处理任务；网口槽无提供者时静默跳过网口绑定（串口不受影响）；
 *         `CH_ID_RS232_1`（USART6 语音 TX）禁止绑定。
 */
void gz_ol_proto_init(void)
{
    ring_buffer_t *rb = app_proto_acquire_buf(RB_SLOT_RS485, RB_SIZE_RS485);
    if (rb == nullptr)
        return;

    s_gz_ol_mask = app_proto_register(gz_ol_probe_frame, rb);
    if (s_gz_ol_mask == 0)
        return;

    app_proto_bind_channel(s_gz_ol_mask, CH_ID_RS485);

    ring_buffer_t *rb_rs232 = app_proto_acquire_buf(RB_SLOT_RS232, RB_SIZE_RS232);
    if (rb_rs232 != nullptr) {
        s_gz_ol_mask_rs232 = app_proto_register(gz_ol_probe_frame, rb_rs232);
        if (s_gz_ol_mask_rs232 != 0)
            app_proto_bind_channel(s_gz_ol_mask_rs232, CH_ID_RS232);
    }

    /* 网口槽（CH_ID_UDP = 10011）：独立 mask 挂 RJ45 RB；无提供者 → 静默跳过网口绑定 */
    ring_buffer_t *rb_rj45 = app_proto_acquire_buf(RB_SLOT_RJ45, RB_SIZE_RJ45);
    if (rb_rj45 != nullptr) {
        s_gz_ol_mask_udp = app_proto_register(gz_ol_probe_frame, rb_rj45);
        if (s_gz_ol_mask_udp != 0)
            app_proto_bind_channel(s_gz_ol_mask_udp, CH_ID_UDP);

        /* GZ_OL 专用业务口：端口 = Sector1 `net_cfg.port`（`0x40` 改端口即改此口，契约 ②）；
         * 同槽 IAP/LDI/CQ 与本 probe 首字节双向快拒（判据见 `doc/14_贵州治超屏协议/README.md` §13）。 */
        s_gz_ol_mask_gzol = app_proto_register(gz_ol_probe_frame, rb_rj45);
        if (s_gz_ol_mask_gzol != 0)
            app_proto_bind_channel(s_gz_ol_mask_gzol, CH_ID_UDP_GZOL);
    }

    s_gz_ol_queue = osMessageQueueNew(GZ_OL_QUEUE_DEPTH, GZ_OL_MSG_SIZE, &s_gz_ol_queue_attr);
    app_proto_set_frame_queue(s_gz_ol_mask, s_gz_ol_queue);
    if (s_gz_ol_mask_rs232 != 0)
        app_proto_set_frame_queue(s_gz_ol_mask_rs232, s_gz_ol_queue);
    if (s_gz_ol_mask_udp != 0)
        app_proto_set_frame_queue(s_gz_ol_mask_udp, s_gz_ol_queue);
    if (s_gz_ol_mask_gzol != 0)
        app_proto_set_frame_queue(s_gz_ol_mask_gzol, s_gz_ol_queue);

    static const osThreadAttr_t s_gz_ol_task_attr = {
        .name       = "gz_ol_handle_task",
        .priority   = osPriorityNormal,
        PL_TASK_STATIC_ATTR(gz_ol_handle, 256),
    };
    osThreadNew(gz_ol_proto_handle_task, NULL, &s_gz_ol_task_attr);
}
sw_app_initcall(gz_ol_proto_init);

/**
 * @brief  "TCLY" 帧探测（PURE 契约：只 rb_peek，无副作用、无阻塞）。
 * @param  rb         源环形缓冲（只读）；`total_len` 在 READY 时写出整帧长。
 * @return READY / WAIT / SKIP（结构合法但 > 256B：整帧消费不入队）/ FAKE。
 * @note   判定链 = 快拒 0x54 → 引导串全匹配 → 长度字段 [17, RB-1] 与尾字节 0x00 双校验
 *         （`len > RB_SIZE_RS485-1` 必 FAKE，否则永久 WAIT 卡死链式探测）；同槽双向快拒详见
 *         doc/14_贵州治超屏协议/README.md §4；`ch` / `aux` 为 probe 契约参数，未使用。
 */
static proto_probe_sta_t gz_ol_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                           uint32_t *total_len, uint8_t *aux)
{
    (void)ch;
    (void)aux;

    uint32_t avail = rb_avail(rb, NULL);
    if (avail == 0U)
        return PROTO_PROBE_FAKE;

    uint8_t head;
    rb_peek(rb, 0U, &head, 1U, NULL);
    if (head != GZ_OL_GUIDE0)
        return PROTO_PROBE_FAKE;

    if (avail < GZ_OL_GUIDE_LEN)
        return PROTO_PROBE_WAIT;

    uint8_t guide[GZ_OL_GUIDE_LEN];
    rb_peek(rb, 0U, guide, GZ_OL_GUIDE_LEN, NULL);
    if (guide[1] != GZ_OL_GUIDE1 || guide[2] != GZ_OL_GUIDE2 || guide[3] != GZ_OL_GUIDE3)
        return PROTO_PROBE_FAKE;

    if (avail < (GZ_OL_LEN_OFFSET + 2U))
        return PROTO_PROBE_WAIT;

    uint8_t len_bytes[2];
    rb_peek(rb, GZ_OL_LEN_OFFSET, len_bytes, 2U, NULL);
    const uint32_t len = (uint32_t)len_bytes[0] | ((uint32_t)len_bytes[1] << 8);

    if (len < GZ_OL_FRAME_LEN_MIN)
        return PROTO_PROBE_FAKE;
    /* 长度上界不得超 RB 可用容量：否则本 probe 会永久 WAIT，卡死同槽链式探测 */
    if (len > (RB_SIZE_RS485 - 1U))
        return PROTO_PROBE_FAKE;

    if (avail < len)
        return PROTO_PROBE_WAIT;

    uint8_t tail;
    rb_peek(rb, len - 1U, &tail, 1U, NULL);
    if (tail != GZ_OL_TAIL)
        return PROTO_PROBE_FAKE;

    *total_len = len;
    /* 长于本模块解析上限的结构合法帧：整帧跳过（rb_skip 消费），不进队列、不污染重同步 */
    return (len > GZ_OL_FRAME_LEN_MAX) ? PROTO_PROBE_SKIP : PROTO_PROBE_READY;
}

/**
 * @brief  帧处理任务：帧队列 → 解析 → 执行；每帧先恢复光敏自动调光（源固件语义）。
 * @param  argument  未使用。
 * @note   帧缓冲 static 落 CCMRAM；`msg->data` 为任务私有拷贝（0x20 的 `\n` 原地转换依赖
 *         该所有权，见 app_gz_ol_proto_cmd.c）。
 */
void gz_ol_proto_handle_task(void *argument)
{
    (void)argument;
    [[gnu::section(".ccmram")]] static uint8_t msg_buf[GZ_OL_MSG_SIZE];
    frame_msg_t *msg = (frame_msg_t *)msg_buf;

    for (;;) {
        if (osOK != osMessageQueueGet(s_gz_ol_queue, msg, NULL, osWaitForever))
            continue;

        /* 源固件语义：收到任何数据即开启自动调光（0x80 的挂起副作用在下帧被解除） */
        gz_ol_resume_auto_dim();

        gz_ol_parsed_cmd_t cmd = gz_ol_parse_frame(msg->data, (uint16_t)msg->data_len);
        gz_ol_execute_cmd(msg->ch, &cmd);
    }
}
