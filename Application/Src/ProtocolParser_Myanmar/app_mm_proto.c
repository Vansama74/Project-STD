/**
 * @file    app_mm_proto.c
 * @brief   缅甸 2D 费显协议（车道灯开关量控制）— 注册 / 探测 / 帧处理任务
 *
 * 帧事实、命令表与 probe 判据见 app_mm_proto.h 文件头（来源：`2D缅甸费显协议.doc`）。
 *
 * 通道：**只绑 CH_ID_RS232**（不绑 RS485 / 网口 / RS232_1 语音口，doc/08 规则 8）；
 * 波特率由 DIP1 全局选择 → 现场必须置 **OFF**（9600 8N1）。
 *
 * 首字节冲突（RS232 槽 0xFF 家族，与四川治超 SC_OL 重叠）：SC_OL 见本协议帧返回
 * WAIT（链式契约继续试下一协议）→ 本 probe 以严格判据 READY(20) 认领；反向本 probe
 * 对 byte[1] ≠ 0xE0 立即 FAKE、且非本模块命令须「结束码正签名 `[15..18]=00×4`」才
 * SKIP ⇒ **双向不误吞、不卡链、无需 EIDE 互斥**（非 '{' 帧族）。
 *
 * 上电默认画面（2026-09-29 用户需求）：本模块在**带显示驱动**的构建里注册
 * 「红色叉（×）」默认画面（几何取自 dev_display_get()，无显示构建不注册）；因单槽
 * 首个生效且本模块收录序在山东/贵州治超之后，PROTO=ALL 开发构建画面不变。
 * 详见 doc/16_缅甸2D费显协议/README.md（§10 守卫 / §13 出厂测试 / §14 默认画面）。
 */

#include "app_mm_proto.h"

#include "FreeRTOS.h"
#include "initcall.h"

#include "app_default_display.h" /* app_default_display_register（开机默认画面，红叉） */
#include "app_dispatch.h"
#include "dev_display.h" /* dev_display_get / set_pixel / fill / commit_frame */
#include "pl_task_static.h"

/* 栈 + TCB 落 CCMRAM（启动期创建一次、永不退出，不占 ucHeap；仅 CPU 访问，不经 DMA） */
PL_TASK_STATIC_STORAGE(mm_handle, 256);

/* RS232 槽体与同槽其它协议 weak 合并；本模块不 provide RS485/RJ45（只绑 RS232） */
RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232);

static proto_mask_t s_mm_mask; /**< CH_ID_RS232（RS232 槽 RB） */
static osMessageQueueId_t s_mm_queue;

/* 队列体 + 控制块置 CCMRAM（同 GZ_OL/YN_OL 先例）：PROTO=ALL 口径 SRAM 余量放不下，
 * 且该缓冲 CPU 独占访问、不经 DMA/ETH。 */
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

/* ==========================================================================
 * 开机默认画面 —— 红色叉（×）（2026-09-29 用户需求）
 *
 * 两条对角线（COLOR_RED，整数插值逐行落笔 + 线宽随屏高缩放），几何取自
 * `dev_display_get()`（不硬编码分辨率、不依赖字库字形）；**只在带显示驱动的构建注册**
 *（无显示构建不注册、零告警，守卫纪律见 doc/16 §10）；单槽首个生效 ⇒ PROTO=ALL 开发
 * 构建画面不变，仅 EIDE Debug / Myanmar 等构建由本回调生效。
 * ========================================================================== */

/**
 * @brief  画一个红色像素（x 越界丢弃；y 由调用者保证在屏内）。
 * @param  d  显示实例（调用者已判空）。
 * @param  w  屏宽（`screen_rows`），用于右边界判界。
 * @param  x  水平坐标。
 * @param  y  垂直坐标。
 */
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
 *
 * 无显示驱动构建（`dev_display_get() == nullptr`）不注册 ⇒ 不改变既有行为
 * （调用方 `app_default_display()` 仍走 `dev_display_guard("default-display")`
 * 的安全跳过路径）。
 */
static void _mm_default_display_init(void)
{
    if (dev_display_get() != nullptr)
        app_default_display_register(_mm_default_red_cross);
}

/**
 * @brief  缅甸 2D 费显协议模块初始化（sw_app_initcall 自注册）。
 *
 * 六步：acquire RS232 槽 → register probe → bind CH_ID_RS232 → **注册开机默认画面
 * （红叉，仅带显示驱动的构建）** → 建静态队列 → set_frame_queue + 建处理任务。
 *
 * @warning CH_ID_RS232_1 (USART6) 为语音板 TTS 专用，禁止绑定。
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

    /* 上电默认画面 = 红叉（用户需求 2026-09-29）：仅带显示驱动的构建注册；
     * 无显示驱动构建（EIDE targets.Myanmar）零调用零告警（见本文件「开机默认画面」节）。 */
    _mm_default_display_init();

    /* 上电干接点输出层初始化（**默认显示红叉**：红灯点亮、绿箭头熄灭）——必须在任务
     * 建起前完成，避免上电沿出现未定义灯态窗口。本调用同时完成 PE12/PE11/PE10 的
     * EXTI 摘除 + 推挽输出接管（详见 app_mm_proto_cmd.c 文件头「干接点 S1/S2/S3 引脚」段）。 */
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
 * @brief  缅甸 2D 费显定长帧探测（PURE 契约：只 rb_peek，无副作用、无阻塞）。
 *
 * 判据与决策矩阵见文件头「probe 判据」；三种返回语义：
 *   READY → 本模块车道灯命令，整帧 20B 入队；
 *   SKIP  → 结构合法但非本模块命令（0x10/0x40/0x80 等）或 DATA0 取值非法 → 整帧消费；
 *   FAKE  → 首字节/机器地址不符 → 交外层 1 字节重同步。
 *
 * @note   `*total_len` 只在 SKIP / READY 两个分支写出（doc/05-01 §3.2 规则 3）。
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
        /* 非本模块命令字：**仅在「文档结构正签名」成立时**整帧消费（SKIP），否则 FAKE。
         *
         * 正签名 = 结束码前 4 字节 [15..18] 全为 0x00（文档「结束字符5..2」取值）。
         * 为什么必须加这一道（doc/16 §帧头冲突纪律）：
         *   RS232 槽的四川治超（SC_OL）**首字节同为 0xFF**，其 byte[1] = 帧长字段；
         *   若某条治超帧帧长恰为 0xE0(224) 且**半帧到达**（avail < 224 → SC_OL 返回
         *   WAIT，链式继续试本 probe），而无条件 SKIP 会把这条治超帧的头 20 字节当
         *   本协议帧消费掉 → 治超帧被截断损坏。正签名（固定偏移连续 4 字节 0x00）把
         *   该误判概率压到 (1/256)^4 量级；缺失正签名时返回 FAKE，交外层按 1 字节
         *   重同步、不消费任何字节，**绝不损坏同槽其它 0xFF 帧族**。 */
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
