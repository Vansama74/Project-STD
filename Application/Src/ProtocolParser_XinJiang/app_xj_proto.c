/**
 * @file    app_xj_proto.c
 * @brief   新疆费额显示器协议 — 注册 / 探测 / 帧处理任务（含心跳节拍）
 *
 * 契约一览见 app_xj_proto.h 文件头（本文件实现 ① 帧长字段判据、⑥ 自备 RS485/RS232 槽体 +
 * acquire、⑦ CCM 放置、⑧ 心跳超时）；同槽共存与判据全表见 doc/19_新疆费显协议/README.md §1、§3
 */

#include "app_xj_proto.h"

#include "FreeRTOS.h"
#include "initcall.h"

#include "app_dispatch.h"
#include "app_xj_proto_cmd.h"
#include "app_xj_proto_parse.h"
#include "pl_task_static.h"

/* 处理任务栈 + TCB 静态落 CCMRAM（只创建一次、永不退出，不占 ucHeap；账见
 * doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md §8） */
PL_TASK_STATIC_STORAGE(xj_handle, 256);

/* 串口通道 RB（与青海 485+232 / RLS 485 同槽 weak 合并）：本模块**必须自备提供者**
 * —— 单协议目标（如现行 EIDE Debug = IAP + LDI + 新疆）里没有其它协议为串口提供
 * 槽体，只 acquire 会让本模块在 acquire 处静默退出（不注册/不绑定），而串口通道又
 * 因无绑定被 `app_boot.c` 门控跳过 ⇒ 上位机发帧零反应（契约 ⑥，见 doc/19 §2.2）。
 * 多协议共存构建下本定义被他处同槽 weak 合并（同名弱符号取其一），不新增 SRAM。 */
RB_PROVIDE_WEAK(rb_provide_rs485, RB_SIZE_RS485);
RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232);

/* 两个逻辑通道各一个独立 mask（RS485 / RS232），同一 probe、同一队列；
 * 槽体由本模块或同槽其它协议 weak 提供（契约 ⑥ = 自备提供 + acquire） */
static proto_mask_t s_xj_mask_rs485; /**< CH_ID_RS485（RS485 槽 RB） */
static proto_mask_t s_xj_mask_rs232; /**< CH_ID_RS232（RS232 槽 RB） */
static osMessageQueueId_t s_xj_queue;

/* 静态队列（两 mask 共用）：队列体 + 控制块置 CCMRAM（契约 ⑦；理由见
 * doc/19_新疆费显协议/README.md §4） */
[[gnu::section(".ccmram")]] static StaticQueue_t s_xj_queue_cb;
[[gnu::section(".ccmram")]] static uint8_t s_xj_queue_buf[XJ_QUEUE_DEPTH * XJ_MSG_SIZE];
static const osMessageQueueAttr_t s_xj_queue_attr = {
    .name    = "xj_queue",
    .cb_mem  = &s_xj_queue_cb,
    .cb_size = sizeof(s_xj_queue_cb),
    .mq_mem  = s_xj_queue_buf,
    .mq_size = sizeof(s_xj_queue_buf),
};

#if XJ_FAULT_SCREEN
/** 最近一次有效帧时刻（tick）；心跳自模块 init 起算（契约 ⑧） */
static uint32_t s_xj_last_valid_tick;
/** 维护屏是否正在显示（解除时清屏一次，避免残字） */
static bool s_xj_fault_shown;
#endif

/**
 * @brief  记心跳起点（模块 init 调用；此刻尚无有效帧，不触发解除动作）。
 */
static void _xj_heartbeat_reset(void)
{
#if XJ_FAULT_SCREEN
    s_xj_last_valid_tick = osKernelGetTickCount();
    s_xj_fault_shown     = false;
#endif
}

/**
 * @brief  收到任意有效帧：刷新心跳时刻；维护屏在显示则清屏一次。
 * @note   必须先于本帧执行（'T'/'C' 自身首步即清屏，后清会把内容擦掉）。
 */
static void _xj_heartbeat_on_frame(void)
{
#if XJ_FAULT_SCREEN
    s_xj_last_valid_tick = osKernelGetTickCount();
    if (s_xj_fault_shown) {
        xj_fault_screen_clear();
        s_xj_fault_shown = false;
    }
#endif
}

/**
 * @brief  心跳节拍检查：超时且维护屏未显示 → 渲染维护屏（幂等）。
 * @note   由帧处理任务的 500ms 队列超时驱动（不新建任务）；XJ_FAULT_SCREEN=0 时
 *         整体无计数、无渲染（契约 ⑧）。
 */
static void _xj_heartbeat_poll(void)
{
#if XJ_FAULT_SCREEN
    if (s_xj_fault_shown)
        return;
    if ((uint32_t)(osKernelGetTickCount() - s_xj_last_valid_tick) <=
        pdMS_TO_TICKS(XJ_FAULT_TIMEOUT_MS))
        return;

    xj_fault_screen_show();
    s_xj_fault_shown = true;
#endif
}

static proto_probe_sta_t xj_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                        uint32_t *total_len, uint8_t *aux);

/**
 * @brief  新疆协议模块初始化（sw_app_initcall 自注册）。
 * @note   注册五步（doc/08_协议模块接入规则/01_通用规则与模块骨架.md §2）：两槽 acquire →
 *         每槽独立 mask register + bind → 两 mask 共用一静态队列 → 处理任务；本模块自备
 *         RS485/RS232 槽体（文件顶 weak provide），任一槽仍取不到（同槽 size 不符等）即
 *         静默跳过该槽，两槽皆不可用则不建队列/任务（契约 ⑥）；
 *         `CH_ID_RS232_1`（USART6 语音 TX）禁止绑定。
 */
void xj_proto_init(void)
{
    ring_buffer_t *rb485 = app_proto_acquire_buf(RB_SLOT_RS485, RB_SIZE_RS485);
    if (rb485 != nullptr) {
        s_xj_mask_rs485 = app_proto_register(xj_probe_frame, rb485);
        if (s_xj_mask_rs485 != 0)
            app_proto_bind_channel(s_xj_mask_rs485, CH_ID_RS485);
    }

    ring_buffer_t *rb232 = app_proto_acquire_buf(RB_SLOT_RS232, RB_SIZE_RS232);
    if (rb232 != nullptr) {
        s_xj_mask_rs232 = app_proto_register(xj_probe_frame, rb232);
        if (s_xj_mask_rs232 != 0)
            app_proto_bind_channel(s_xj_mask_rs232, CH_ID_RS232);
    }

    if (s_xj_mask_rs485 == 0 && s_xj_mask_rs232 == 0)
        return; /* 无槽可用（无 RB 提供者 / 64 槽全满）：整模块不激活 */

    s_xj_queue = osMessageQueueNew(XJ_QUEUE_DEPTH, XJ_MSG_SIZE, &s_xj_queue_attr);
    app_proto_set_frame_queue(s_xj_mask_rs485, s_xj_queue);
    if (s_xj_mask_rs232 != 0)
        app_proto_set_frame_queue(s_xj_mask_rs232, s_xj_queue);

    _xj_heartbeat_reset(); /* 心跳起点 = 模块 init 时刻（首个有效帧前不立即报维护） */

    static const osThreadAttr_t s_xj_task_attr = {
        .name     = "xj_handle_task",
        .priority = osPriorityNormal,
        PL_TASK_STATIC_ATTR(xj_handle, 256),
    };
    osThreadNew(xj_proto_handle_task, NULL, &s_xj_task_attr);
}
sw_app_initcall(xj_proto_init);

/**
 * @brief  新疆协议帧探测（PURE：只 rb_peek，无副作用、无阻塞）。
 * @param  rb         源环形缓冲（只读）。
 * @param  total_len  READY 时写出整帧长（= 长度字段值）；WAIT/FAKE 不写。
 * @return READY / WAIT / FAKE；判据链（帧头 0xFF 0xDC → 长度 5~74 → 命令字白名单 → BCC）
 *         与同槽四川治超/RLS/MM 四重隔离详见 doc/19_新疆费显协议/README.md §3。
 * @note   长度与命令字判据刻意放在等齐整帧之前：见到治超长帧（长度 0xDC=220）时立即
 *         FAKE 而不 WAIT，不把等待预算摊到别的协议头上。
 */
static proto_probe_sta_t xj_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                        uint32_t *total_len, uint8_t *aux)
{
    (void)ch;
    (void)aux;

    const uint32_t avail = rb_avail(rb, NULL);
    if (avail == 0U)
        return PROTO_PROBE_FAKE;

    uint8_t head[XJ_CMD_OFFSET + 1U]; /* [0..3]：帧头 2 + 长度 + 命令字 */
    rb_peek(rb, 0U, head, 1U, NULL);
    if (head[0] != XJ_STX0)
        return PROTO_PROBE_FAKE;

    if (avail < (XJ_LEN_OFFSET + 1U))
        return PROTO_PROBE_WAIT; /* 长度字段未到齐 */

    /* 读 [0..2]（帧头 2 + 长度字段）：供 [1] 判 STX1、[2] 取长度；只读 2 字节会让
     * head[XJ_LEN_OFFSET] 取栈上垃圾值（UB）→ 首个/突发帧偶发误判 FAKE/WAIT */
    rb_peek(rb, 0U, head, XJ_LEN_OFFSET + 1U, NULL);
    if (head[1] != XJ_STX1)
        return PROTO_PROBE_FAKE; /* RLS `FF FE` / MM `FF E0` / 治超长度字段在此分道 */

    const uint32_t frame_len = head[XJ_LEN_OFFSET];
    if (frame_len < XJ_FRAME_LEN_MIN || frame_len > XJ_FRAME_LEN_MAX)
        return PROTO_PROBE_FAKE; /* 治超长度 0xDC=220 一类立即排除，不 WAIT */

    if (avail < (XJ_CMD_OFFSET + 1U))
        return PROTO_PROBE_WAIT;

    rb_peek(rb, 0U, head, XJ_CMD_OFFSET + 1U, NULL);
    if (xj_cmd_from_byte(head[XJ_CMD_OFFSET]) == XJ_PCMD_INVALID)
        return PROTO_PROBE_FAKE; /* 白名单外命令字（含 'D' 0x44）不消费，交链式探测 */

    if (avail < frame_len)
        return PROTO_PROBE_WAIT; /* 整帧未到齐 */

    uint8_t frame[XJ_FRAME_LEN_MAX];
    rb_peek(rb, 0U, frame, (uint16_t)frame_len, NULL);
    if (!xj_bcc_ok(frame, (uint16_t)frame_len))
        return PROTO_PROBE_FAKE; /* BCC 第三重隔离 */

    *total_len = frame_len;
    return PROTO_PROBE_READY;
}

/**
 * @brief  帧处理任务：帧队列 → 解析 → 执行，并在队列超时节拍上做心跳判定。
 * @param  argument  未使用。
 * @note   帧缓冲 static 落 CCMRAM（契约 ⑦）；维护屏解除先于本帧执行（见 `_xj_heartbeat_on_frame`）；
 *         XJ_FAULT_SCREEN=0 时改用 `osWaitForever`（无节拍、零周期唤醒）。
 */
void xj_proto_handle_task(void *argument)
{
    (void)argument;
    [[gnu::section(".ccmram")]] static uint8_t msg_buf[XJ_MSG_SIZE];
    frame_msg_t *msg = (frame_msg_t *)msg_buf;

    for (;;) {
#if XJ_FAULT_SCREEN
        const uint32_t wait_ms = XJ_FAULT_POLL_MS; /* 500ms 节拍（超时判定粒度，不新建任务） */
#else
        const uint32_t wait_ms = osWaitForever;
#endif
        const osStatus_t st = osMessageQueueGet(s_xj_queue, msg, NULL, wait_ms);

        if (st == osOK) {
            /* probe 已验帧头/长度/命令字/BCC：任意有效帧都算「在线」 */
            _xj_heartbeat_on_frame();

            /* 只传裸帧：解析结果由 xj_execute_cmd 在自己帧内构造，任务栈不留 64B 具名副本
             * （本任务仅 256 words，与执行层帧内副本并存会逼近上限 → 栈溢出致任务停摆） */
            xj_execute_cmd(msg->ch, msg->data, (uint16_t)msg->data_len);
        }

        _xj_heartbeat_poll();
    }
}
