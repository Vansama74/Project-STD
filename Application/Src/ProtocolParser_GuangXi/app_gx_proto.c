/**
 * @file    app_gx_proto.c
 * @brief   广西费显协议 — 注册 / 探测 / 帧处理任务
 *
 * 契约一览见 app_gx_proto.h 文件头（本文件实现 ③ 应答原路、④ 自备串口槽体、
 * ⑤ CCM 放置）；probe 判据链与同槽共存见 doc/21_广西费显协议/README.md §3、§4
 */

#include "app_gx_proto.h"

#include "FreeRTOS.h"
#include "initcall.h"

#include "app_dispatch.h"
#include "app_gx_proto_cmd.h"
#include "app_gx_proto_parse.h"
#include "pl_task_static.h"

/* 处理任务栈 + TCB 静态落 CCMRAM（只创建一次、永不退出，不占 ucHeap；
 * 账见 doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md §8） */
PL_TASK_STATIC_STORAGE(gx_handle, 256);

/* 串口通道 RB：本模块**必须自备提供者**——单协议目标（如只有 IAP + LDI + 广西）
 * 里没有其它协议为串口提供槽体，只 acquire 会让模块在 acquire 处静默退出
 * （不注册/不绑定），串口通道又因无绑定被 `app_boot.c` 门控跳过 ⇒ 上位机发帧
 * 零反应（契约 ④）。多协议共存构建下同名 weak 符号合并，不额外占 SRAM。 */
RB_PROVIDE_WEAK(rb_provide_rs485, RB_SIZE_RS485);
RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232);

/* 两个逻辑通道各一个独立 mask（RS485 / RS232），同一 probe、同一队列 */
static proto_mask_t s_gx_mask_rs485; /**< CH_ID_RS485（RS485 槽 RB） */
static proto_mask_t s_gx_mask_rs232; /**< CH_ID_RS232（RS232 槽 RB） */
static osMessageQueueId_t s_gx_queue;

/* 静态队列（两 mask 共用）：队列体 + 控制块置 CCMRAM（契约 ⑤） */
[[gnu::section(".ccmram")]] static StaticQueue_t s_gx_queue_cb;
[[gnu::section(".ccmram")]] static uint8_t s_gx_queue_buf[GX_QUEUE_DEPTH * GX_MSG_SIZE];
static const osMessageQueueAttr_t s_gx_queue_attr = {
    .name    = "gx_queue",
    .cb_mem  = &s_gx_queue_cb,
    .cb_size = sizeof(s_gx_queue_cb),
    .mq_mem  = s_gx_queue_buf,
    .mq_size = sizeof(s_gx_queue_buf),
};

/** @brief READY：帧已可整帧取出（avail 不足时继续等）。 */
static proto_probe_sta_t _gx_ready(uint32_t avail, uint32_t frame_len, uint32_t *total_len)
{
    if (avail < frame_len)
        return PROTO_PROBE_WAIT;
    *total_len = frame_len;
    return PROTO_PROBE_READY;
}

/** @brief SKIP：命令码已识别、整帧长可定但字段非法 → 整帧消费（不执行、不应答）。 */
static proto_probe_sta_t _gx_skip(uint32_t avail, uint32_t frame_len, uint32_t *total_len)
{
    if (avail < frame_len)
        return PROTO_PROBE_WAIT;
    *total_len = frame_len;
    return PROTO_PROBE_SKIP;
}

/**
 * @brief  51 54 显示帧探测：从 [11] 起扫描结束码 0xFF（文本上限 192B）。
 * @param  avail      当前可读字节数。
 * @param  total_len  READY/SKIP 时写出整帧长。
 * @return READY / SKIP（颜色码非法）/ WAIT（未到齐）/ FAKE（超 204B 仍无结束码）。
 */
static proto_probe_sta_t _gx_probe_display(const ring_buffer_t *rb, uint32_t avail,
                                           uint32_t *total_len)
{
    if (avail < GX_DISPLAY_FRAME_LEN_MIN)
        return PROTO_PROBE_WAIT;

    const uint32_t last =
        (avail - 1U < GX_DISPLAY_ETX_MAX_OFFSET) ? (avail - 1U) : GX_DISPLAY_ETX_MAX_OFFSET;

    for (uint32_t i = GX_DISPLAY_TEXT_OFFSET; i <= last; i++) {
        uint8_t b = 0U;
        if (!rb_peekc(rb, (uint16_t)i, &b, NULL))
            return PROTO_PROBE_WAIT;
        if (b != GX_ETX)
            continue;

        uint8_t color = 0U;
        if (!rb_peekc(rb, GX_DISPLAY_COLOR_OFFSET, &color, NULL))
            return PROTO_PROBE_WAIT;

        *total_len = i + 1U;
        return (color <= GX_COLOR_MAX) ? PROTO_PROBE_READY : PROTO_PROBE_SKIP;
    }

    if (avail < GX_FRAME_LEN_MAX)
        return PROTO_PROBE_WAIT; /* 文本可能未收全 */
    return PROTO_PROBE_FAKE;     /* 超上限仍无结束码：交 1 字节重同步 */
}

/**
 * @brief  4B 4C 亮度帧探测（定长 7B，[5] 必须 0~3）。
 * @return READY / SKIP（亮度越界）/ WAIT（未到齐）。
 */
static proto_probe_sta_t _gx_probe_brightness(const ring_buffer_t *rb, uint32_t avail,
                                              uint32_t *total_len)
{
    if (avail < GX_FRAME_LEN_BRIGHTNESS)
        return PROTO_PROBE_WAIT;

    uint8_t v = 0U;
    if (!rb_peekc(rb, GX_SUM_OFFSET, &v, NULL))
        return PROTO_PROBE_WAIT;

    return (v <= 3U) ? _gx_ready(avail, GX_FRAME_LEN_BRIGHTNESS, total_len)
                     : _gx_skip(avail, GX_FRAME_LEN_BRIGHTNESS, total_len);
}

/**
 * @brief  4B 5A 灯组帧探测（定长 8B，[5..7] 必须为六种三元组之一）。
 * @return READY / SKIP（未知三元组）/ WAIT（未到齐）。
 */
static proto_probe_sta_t _gx_probe_lane(const ring_buffer_t *rb, uint32_t avail,
                                        uint32_t *total_len)
{
    if (avail < GX_FRAME_LEN_LANE)
        return PROTO_PROBE_WAIT;

    uint8_t triple[3];
    if (rb_peek(rb, GX_SUM_OFFSET, triple, sizeof(triple), NULL) != sizeof(triple))
        return PROTO_PROBE_WAIT;

    return (gx_lamp_from_triplet(triple) != GX_LAMP_INVALID)
               ? _gx_ready(avail, GX_FRAME_LEN_LANE, total_len)
               : _gx_skip(avail, GX_FRAME_LEN_LANE, total_len);
}

/**
 * @brief  56 4F 语音帧探测（[5] 子命令决定 8B / 14B）。
 * @return READY / SKIP（编号或金额非法）/ WAIT（未到齐）/ FAKE（未知子命令）。
 * @note   子命令未知时整帧长不可定，只能 FAKE 交后续协议与重同步。
 */
static proto_probe_sta_t _gx_probe_voice(const ring_buffer_t *rb, uint32_t avail,
                                         uint32_t *total_len)
{
    if (avail < (GX_VOICE_KIND_OFFSET + 1U))
        return PROTO_PROBE_WAIT;

    uint8_t kind = 0U;
    if (!rb_peekc(rb, GX_VOICE_KIND_OFFSET, &kind, NULL))
        return PROTO_PROBE_WAIT;

    if (kind == GX_VOICE_KIND_POLITE) {
        if (avail < GX_VOICE_POLITE_FRAME_LEN)
            return PROTO_PROBE_WAIT;

        uint8_t code = 0U;
        if (!rb_peekc(rb, GX_VOICE_CODE_OFFSET, &code, NULL))
            return PROTO_PROBE_WAIT;

        return (code >= GX_VOICE_CODE_MIN && code <= GX_VOICE_CODE_MAX)
                   ? _gx_ready(avail, GX_VOICE_POLITE_FRAME_LEN, total_len)
                   : _gx_skip(avail, GX_VOICE_POLITE_FRAME_LEN, total_len);
    }

    if (kind == GX_VOICE_KIND_FEE) {
        if (avail < GX_VOICE_FEE_FRAME_LEN)
            return PROTO_PROBE_WAIT;

        uint8_t digits[GX_VOICE_AMOUNT_DIGITS];
        if (rb_peek(rb, GX_VOICE_AMOUNT_OFFSET, digits, sizeof(digits), NULL) != sizeof(digits))
            return PROTO_PROBE_WAIT;

        for (uint8_t i = 0U; i < GX_VOICE_AMOUNT_DIGITS; i++) {
            if (digits[i] < '0' || digits[i] > '9')
                return _gx_skip(avail, GX_VOICE_FEE_FRAME_LEN, total_len);
        }
        return _gx_ready(avail, GX_VOICE_FEE_FRAME_LEN, total_len);
    }

    return PROTO_PROBE_FAKE;
}

/**
 * @brief  01 01 全屏帧探测（定长 7B，[5] 与 [6] 同为 0/1/2）。
 * @return READY / SKIP（两字节不等或色码越界）/ WAIT（未到齐）。
 */
static proto_probe_sta_t _gx_probe_fullscreen(const ring_buffer_t *rb, uint32_t avail,
                                              uint32_t *total_len)
{
    if (avail < GX_FRAME_LEN_FULLSCREEN)
        return PROTO_PROBE_WAIT;

    uint8_t c[2];
    if (rb_peek(rb, GX_SUM_OFFSET, c, sizeof(c), NULL) != sizeof(c))
        return PROTO_PROBE_WAIT;

    return (c[0] == c[1] && c[0] <= GX_COLOR_MAX)
               ? _gx_ready(avail, GX_FRAME_LEN_FULLSCREEN, total_len)
               : _gx_skip(avail, GX_FRAME_LEN_FULLSCREEN, total_len);
}

/**
 * @brief  03 03 开机界面帧探测（定长 103B，[5] 色码 + [102] 结束码）。
 * @return READY / SKIP（色码越界）/ WAIT（未到齐）/ FAKE（末字节非结束码）。
 * @note   结束码不符时不按定长消费——可能是错位残帧，交 1 字节重同步以免吞掉后续真帧。
 */
static proto_probe_sta_t _gx_probe_boot_screen(const ring_buffer_t *rb, uint32_t avail,
                                               uint32_t *total_len)
{
    if (avail < GX_FRAME_LEN_BOOT)
        return PROTO_PROBE_WAIT;

    uint8_t color = 0U;
    if (!rb_peekc(rb, GX_BOOT_COLOR_OFFSET, &color, NULL))
        return PROTO_PROBE_WAIT;
    if (color > GX_COLOR_MAX)
        return _gx_skip(avail, GX_FRAME_LEN_BOOT, total_len);

    uint8_t last = 0U;
    if (!rb_peekc(rb, GX_FRAME_LEN_BOOT - 1U, &last, NULL))
        return PROTO_PROBE_WAIT;
    if (last != GX_ETX)
        return PROTO_PROBE_FAKE;

    return _gx_ready(avail, GX_FRAME_LEN_BOOT, total_len);
}

/**
 * @brief  广西协议帧探测（PURE：只 rb_peek/rb_peekc，无副作用、无阻塞）。
 * @param  rb         源环形缓冲（只读）。
 * @param  total_len  READY/SKIP 时写出整帧长；WAIT/FAKE 不写。
 * @return READY / WAIT / SKIP / FAKE；判据链（0xAA → 0xBB → 命令码白名单 →
 *         定长/结束码）与同槽共存见 doc/21_广西费显协议/README.md §3。
 */
static proto_probe_sta_t gx_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                        uint32_t *total_len, uint8_t *aux)
{
    (void)ch;
    (void)aux;

    const uint32_t avail = rb_avail(rb, NULL);
    if (avail == 0U)
        return PROTO_PROBE_FAKE;

    uint8_t head[GX_CMD_LEN];
    if (rb_peek(rb, 0U, head, 1U, NULL) != 1U)
        return PROTO_PROBE_FAKE;
    if (head[0] != GX_STX0)
        return PROTO_PROBE_FAKE; /* 首字节快拒（0xAA 在现有三槽唯一） */

    if (avail < (GX_STXB_OFFSET + 1U))
        return PROTO_PROBE_WAIT;
    if (rb_peek(rb, 0U, head, GX_STXB_OFFSET + 1U, NULL) != GX_STXB_OFFSET + 1U)
        return PROTO_PROBE_WAIT;
    if (head[GX_STXB_OFFSET] != GX_STXB)
        return PROTO_PROBE_FAKE;

    if (avail < GX_CMD_LEN)
        return PROTO_PROBE_WAIT;
    if (rb_peek(rb, 0U, head, GX_CMD_LEN, NULL) != GX_CMD_LEN)
        return PROTO_PROBE_WAIT;

    switch (gx_cmd_from_pair(head[GX_CMD1_OFFSET], head[GX_CMD2_OFFSET])) {
        case GX_CMD_DISPLAY:
            return _gx_probe_display(rb, avail, total_len);

        case GX_CMD_BRIGHTNESS:
            return _gx_probe_brightness(rb, avail, total_len);

        case GX_CMD_LANE:
            return _gx_probe_lane(rb, avail, total_len);

        case GX_CMD_VOICE:
            return _gx_probe_voice(rb, avail, total_len);

        case GX_CMD_FULLSCREEN:
            return _gx_probe_fullscreen(rb, avail, total_len);

        case GX_CMD_VERSION:
            return _gx_ready(avail, GX_FRAME_LEN_VERSION, total_len);

        case GX_CMD_BOOT_SCREEN:
            return _gx_probe_boot_screen(rb, avail, total_len);

        case GX_CMD_FONT:
            return _gx_ready(avail, GX_FRAME_LEN_FONT, total_len);

        default:
            return PROTO_PROBE_FAKE; /* 白名单外命令码不消费，交链式探测 */
    }
}

/**
 * @brief  广西协议模块初始化（sw_app_initcall 自注册）。
 * @note   注册五步（doc/08_协议模块接入规则/01_通用规则与模块骨架.md §2）：两槽 acquire →
 *         每槽独立 mask register + bind → 两 mask 共用一静态队列 → 处理任务；本模块自备
 *         RS485/RS232 槽体（文件顶 weak provide），任一槽取不到即跳过该槽，两槽皆不可用
 *         则不建队列/任务（契约 ④）；`CH_ID_RS232_1`（USART6 语音 TX）禁止绑定。
 *         本模块不注册开机默认画面（未收到 03 03 时由 app_default_display 显示项目通用画面）。
 */
void gx_proto_init(void)
{
    ring_buffer_t *rb485 = app_proto_acquire_buf(RB_SLOT_RS485, RB_SIZE_RS485);
    if (rb485 != nullptr) {
        s_gx_mask_rs485 = app_proto_register(gx_probe_frame, rb485);
        if (s_gx_mask_rs485 != 0)
            app_proto_bind_channel(s_gx_mask_rs485, CH_ID_RS485);
    }

    ring_buffer_t *rb232 = app_proto_acquire_buf(RB_SLOT_RS232, RB_SIZE_RS232);
    if (rb232 != nullptr) {
        s_gx_mask_rs232 = app_proto_register(gx_probe_frame, rb232);
        if (s_gx_mask_rs232 != 0)
            app_proto_bind_channel(s_gx_mask_rs232, CH_ID_RS232);
    }

    if (s_gx_mask_rs485 == 0 && s_gx_mask_rs232 == 0)
        return; /* 无槽可用（无 RB 提供者 / 64 槽全满）：整模块不激活 */

    s_gx_queue = osMessageQueueNew(GX_QUEUE_DEPTH, GX_MSG_SIZE, &s_gx_queue_attr);
    app_proto_set_frame_queue(s_gx_mask_rs485, s_gx_queue);
    if (s_gx_mask_rs232 != 0)
        app_proto_set_frame_queue(s_gx_mask_rs232, s_gx_queue);

    static const osThreadAttr_t s_gx_task_attr = {
        .name     = "gx_handle_task",
        .priority = osPriorityNormal,
        PL_TASK_STATIC_ATTR(gx_handle, 256),
    };
    osThreadNew(gx_proto_handle_task, NULL, &s_gx_task_attr);
}
sw_app_initcall(gx_proto_init);

/**
 * @brief  帧处理任务：帧队列 → 解析执行，并在队列超时节拍上判定总显示时间到期。
 * @param  argument  未使用。
 * @note   帧缓冲 static 落 CCMRAM（契约 ⑤）；节拍 = GX_TICK_MS（不新建任务）。
 */
void gx_proto_handle_task(void *argument)
{
    (void)argument;
    [[gnu::section(".ccmram")]] static uint8_t msg_buf[GX_MSG_SIZE];
    frame_msg_t *msg = (frame_msg_t *)msg_buf;

    for (;;) {
        if (osMessageQueueGet(s_gx_queue, msg, NULL, GX_TICK_MS) == osOK)
            gx_execute_cmd(msg->ch, msg->data, (uint16_t)msg->data_len);

        gx_display_timer_tick();
    }
}
