/**
 * @file    app_mm_proto.c
 * @brief   缅甸 2D 费显协议 — 注册 / 探测 / 帧处理任务
 *
 * 契约一览见 app_mm_proto.h 文件头（本文件实现 ④ 正签名判据、⑥ CCM 放置）；
 * RS232 槽与四川治超重叠的共存纪律、开机默认画面见
 * doc/16_缅甸2D费显协议/README.md §5、§6、§7、§14
 */

#include "app_mm_proto.h"

#include "FreeRTOS.h"
#include "initcall.h"

#include "app_default_display.h" /* app_default_display_register（开机默认画面，红叉） */
#include "app_dispatch.h"
#include "dev_display.h" /* dev_display_get / set_pixel / fill / commit_frame */
#include "pl_task_static.h"

/* 处理任务栈 + TCB 静态落 CCMRAM（契约 ⑥；账见
 * doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md §8） */
PL_TASK_STATIC_STORAGE(mm_handle, 256);

/* RS232 槽体与同槽其它协议 weak 合并；本模块不 provide RS485/RJ45（只绑 RS232） */
RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232);

static proto_mask_t s_mm_mask; /**< CH_ID_RS232（RS232 槽 RB） */
static osMessageQueueId_t s_mm_queue;

/* 静态队列：队列体 + 控制块置 CCMRAM（契约 ⑥）；理由与账见 doc/16_缅甸2D费显协议/README.md §6 */
[[gnu::section(".ccmram")]] static StaticQueue_t s_mm_queue_cb;
[[gnu::section(".ccmram")]] static uint8_t s_mm_queue_buf[MM_QUEUE_DEPTH * MM_MSG_SIZE];
static const osMessageQueueAttr_t s_mm_queue_attr = {
    .name    = "mm_queue",
    .cb_mem  = &s_mm_queue_cb,
    .cb_size = sizeof(s_mm_queue_cb),
    .mq_mem  = s_mm_queue_buf,
    .mq_size = sizeof(s_mm_queue_buf),
};

static proto_probe_sta_t mm_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                        uint32_t *total_len, uint8_t *aux);

/* ---- 开机默认画面「红色叉（×）」：仅带显示驱动的构建注册（§14.3）----
 * 两条对角线取 `dev_display_get()` 几何（不硬编码分辨率、不依赖字库）；单槽首个注册生效。 */

/** @brief 画一个红色像素（x 越界丢弃；y 由调用者保证在屏内） */
static void _mm_cross_px(dev_display_t *d, uint16_t w, uint16_t x, uint16_t y)
{
    if (x < w)
        dev_display_set_pixel(d, x, y, COLOR_RED);
}

/** @brief 开机默认画面：整屏清黑 + 红色叉（两条对角线，线宽随屏高缩放）。 */
static void _mm_default_red_cross(void)
{
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return; /* 无显示驱动构建：无屏可画（注册点已判空，此处再护一道） */

    const uint16_t w = d->screen_rows; /* 宽（x 方向像素数） */
    const uint16_t h = d->screen_cols; /* 高（y 方向像素数） */
    if (w < 2U || h < 2U)
        return;

    /* 线宽：随屏高缩放（h/24），至少 2px —— 96 高 ⇒ 4px、32/64 高 ⇒ 2px */
    uint16_t thick = (uint16_t)(h / 24U);
    if (thick < 2U)
        thick = 2U;

    /* 先清黑再画（与其它默认画面一致：默认画面为整屏语义） */
    dev_display_fill(d, 0, 0, w, h, COLOR_BLACK);

    const uint16_t x_max = (uint16_t)(w - 1U);
    const uint16_t y_max = (uint16_t)(h - 1U);
    for (uint16_t y = 0U; y < h; y++) {
        /* 两条对角线在本扫描行的交点（整数线性插值；32 位中间量防溢出） */
        const uint16_t x_l = (uint16_t)(((uint32_t)y * x_max) / y_max); /* ↖→↘ */
        const uint16_t x_r = (uint16_t)(x_max - x_l);                   /* ↗→↙ */
        for (uint16_t k = 0U; k < thick; k++) {
            _mm_cross_px(d, w, (uint16_t)(x_l + k), y);
            if (x_r >= k)
                _mm_cross_px(d, w, (uint16_t)(x_r - k), y);
        }
    }

    dev_display_commit_frame(d);
}

/**
 * @brief  注册开机默认画面（仅当本构建带显示驱动）。
 * @note   无显示构建不注册 ⇒ 回落到 `app_default_display()` 的守卫跳过路径（§10.2）。
 */
static void _mm_default_display_init(void)
{
    if (dev_display_get() != nullptr)
        app_default_display_register(_mm_default_red_cross);
}

/**
 * @brief  缅甸 2D 费显协议模块初始化（`sw_app_initcall` 自注册）。
 * @note   次序：acquire RS232 槽 → register → bind → 注册开机默认画面（仅带显示驱动）→
 *         `mm_apply_init()` 接管干接点（**建任务前完成**，避免上电沿出现未定义灯态窗口）→
 *         建静态队列 → 建处理任务；`CH_ID_RS232_1`（USART6 语音 TX）**禁止绑定**，见 §7.1。
 */
void mm_proto_init(void)
{
    ring_buffer_t *rb = app_proto_acquire_buf(RB_SLOT_RS232, RB_SIZE_RS232);
    if (rb == nullptr)
        return;

    s_mm_mask = app_proto_register(mm_probe_frame, rb);
    if (s_mm_mask == 0)
        return;

    app_proto_bind_channel(s_mm_mask, CH_ID_RS232);

    /* 上电默认画面 = 红叉：仅带显示驱动的构建注册；无显示构建零调用零告警（§14.3） */
    _mm_default_display_init();

    /* 干接点接管（默认红叉）必须在建任务前完成，避免上电沿出现未定义灯态窗口；
     * PE12/PE11/PE10 的 EXTI 摘除与推挽接管见 app_mm_proto_cmd.c（§4.2）。 */
    mm_apply_init();

    s_mm_queue = osMessageQueueNew(MM_QUEUE_DEPTH, MM_MSG_SIZE, &s_mm_queue_attr);
    app_proto_set_frame_queue(s_mm_mask, s_mm_queue);

    static const osThreadAttr_t s_mm_task_attr = {
        .name     = "mm_handle_task",
        .priority = osPriorityNormal,
        PL_TASK_STATIC_ATTR(mm_handle, 256),
    };
    osThreadNew(mm_proto_handle_task, NULL, &s_mm_task_attr);
}
sw_app_initcall(mm_proto_init);

/**
 * @brief  定长帧探测（PURE：只 rb_peek，无副作用、无阻塞）。
 * @param  rb         源环形缓冲（只读）。
 * @param  total_len  SKIP / READY 分支写出整帧长（20）；FAKE 不写
 *                    （doc/05_协议模块多协议兼容优化/01_architecture.md §3.2 规则 3）。
 * @return READY = 车道灯命令整帧 20B 入队；SKIP = 非本模块命令或 DATA0 越界（整帧消费）；
 *         FAKE = 首字节/机器地址不符 → 交外层按 1 字节重同步。
 * @note   判据全表见 §6；「非本模块命令的正签名门槛」是防误吞同槽治超帧的关键（§5④）。
 */
static proto_probe_sta_t mm_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                        uint32_t *total_len, uint8_t *aux)
{
    (void)ch;
    (void)aux;

    uint32_t avail = rb_avail(rb, NULL);
    if (avail == 0U)
        return PROTO_PROBE_FAKE;

    uint8_t head;
    rb_peek(rb, 0U, &head, 1U, NULL);
    if (head != MM_FRAME_HEAD)
        return PROTO_PROBE_FAKE;

    if (avail < 2U)
        return PROTO_PROBE_WAIT;

    uint8_t addr;
    rb_peek(rb, 1U, &addr, 1U, NULL);
    if (addr != MM_FRAME_ADDR)
        return PROTO_PROBE_FAKE;

    /* 定长帧：帧长与命令字无关，等齐 20 字节再判，避免半帧误判命令 */
    if (avail < MM_FRAME_LEN)
        return PROTO_PROBE_WAIT;

    uint8_t cmd_arg[2]; /* [0] = 命令方式字，[1] = DATA0 */
    rb_peek(rb, MM_CMD_OFFSET, cmd_arg, 2U, NULL);

    if (cmd_arg[0] != (uint8_t)MM_CMD_LANE_RED && cmd_arg[0] != (uint8_t)MM_CMD_LANE_LEFT &&
        cmd_arg[0] != (uint8_t)MM_CMD_LANE_RIGHT) {
        /* 非本模块命令字：须「结束码正签名」成立才整帧消费（SKIP），否则 FAKE——
         * 无条件 SKIP 会截断同槽四川治超的半帧（其 byte[1] 可为 0xE0）；防误吞纪律见 §5④ */
        uint8_t sig[MM_TAIL_SIG_LEN];
        rb_peek(rb, MM_TAIL_SIG_OFFSET, sig, MM_TAIL_SIG_LEN, NULL);
        for (uint32_t i = 0U; i < MM_TAIL_SIG_LEN; i++) {
            if (sig[i] != MM_TAIL_SIG_VALUE)
                return PROTO_PROBE_FAKE;
        }
        /* 文档 0x10/0x40/0x80 等：结构合法、非本模块命令 → 整帧消费 */
        *total_len = MM_FRAME_LEN;
        return PROTO_PROBE_SKIP;
    }

    if (cmd_arg[1] > 0x01U) {
        /* 本模块命令但 DATA0 越界（协议只定义 1/0）：整帧消费，不误判为坏流 */
        *total_len = MM_FRAME_LEN;
        return PROTO_PROBE_SKIP;
    }

    *total_len = MM_FRAME_LEN;
    return PROTO_PROBE_READY;
}

/**
 * @brief  帧处理任务：帧队列 → 解析 → 干接点动作。
 * @param  argument  未使用。
 */
void mm_proto_handle_task(void *argument)
{
    (void)argument;
    [[gnu::section(".ccmram")]] static uint8_t msg_buf[MM_MSG_SIZE];
    frame_msg_t *msg = (frame_msg_t *)msg_buf;

    for (;;) {
        if (osOK != osMessageQueueGet(s_mm_queue, msg, NULL, osWaitForever))
            continue;

        mm_parsed_cmd_t cmd = mm_parse_frame(msg->data, (uint16_t)msg->data_len);
        mm_execute_cmd(&cmd);
    }
}
