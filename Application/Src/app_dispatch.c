/**
 * @file    app_dispatch.c
 * @brief   协议调度框架实现（Application 层核心）
 *
 * 接收：物理接口 → 通道任务 → app_channel_dispatch() → ring buffer + g_ch_queue
 *       → frame_dispatch_task() → 协议探测 → frame_queue → 协议处理任务；
 * 发送：ch_ops 虚表分派（OCP）。全部调度状态收敛于 dispatch_ctx_t g_dispatch。
 * 多协议共享 RB / 绑定矩阵见 doc/05_协议模块多协议兼容优化/01_architecture.md。
 */

#include "app_dispatch.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "bit_utils.h"
#include "initcall.h"
#include "app_factory_test.h"
#include "pl_task_static.h"

/* ---- WAIT 头阻塞预算（any_wait 强制重同步阈值）----
 * 帧头已匹配但数据未到齐、且头部前缀在预算内**无任何前进** → 判「残帧卡头」，
 * 强制 skip 1 字节重同步（见 frame_dispatch_task）。取值 1000ms = 最长帧
 * （RLS 530B @9600bps ≈ 552ms）的 ~1.8× 余量；不可取 500ms（会打断合法长帧，
 * 依据与现场根因见 doc/15 §9）。可用 -DFRAME_WAIT_BUDGET_MS=xxx 覆盖。 */
#ifndef FRAME_WAIT_BUDGET_MS
#define FRAME_WAIT_BUDGET_MS 1000U
#endif

/* ---- 通道通知投递重试预算 ----
 * ch_queue 投递失败（队列满）时先重试一次再计数丢弃（见 app_channel_dispatch）。
 * 队列 32 深、消费者为最高优先级独立任务，正常瞬时排空；20ms 足以覆盖一次任务
 * 切换 + 一轮探测。 */
#ifndef DISPATCH_NOTIFY_RETRY_MS
#define DISPATCH_NOTIFY_RETRY_MS 20U
#endif

/* 栈 + TCB 落 CCMRAM（启动期创建一次、永不退出；任务栈仅被 CPU 访问，不经 DMA） */
PL_TASK_STATIC_STORAGE(frame_dispatch, 256);

/* ---- g_ch_queue 静态分配 ---- */
static StaticQueue_t s_ch_queue_cb;
static channel_t *s_ch_queue_buf[MAX_CHANNELS];
static const osMessageQueueAttr_t s_ch_queue_attr = {
    .name    = "g_ch_queue",
    .cb_mem  = &s_ch_queue_cb,
    .cb_size = sizeof(s_ch_queue_cb),
    .mq_mem  = s_ch_queue_buf,
    .mq_size = sizeof(s_ch_queue_buf),
};

/* ---- 帧分发任务缓冲区 ---- */
static uint8_t _msg_dispatch_buf[sizeof(frame_msg_t) + FRAME_DATA_MAX_LEN];

/* ================================================================
 *  环形缓冲区池 — 一物理通道一 RB，体由协议 TU RB_PROVIDE_WEAK 提供
 *
 *  未编入任何 provide → weak 函数指针为 0 → acquire 返回 nullptr。
 *  编入 ≥1 协议 provide → 保留一个 getter（内含 SRAM static 缓冲）。
 * ================================================================ */

extern ring_buffer_t *RB_PROVIDE_RJ45(void) __attribute__((weak));
extern ring_buffer_t *RB_PROVIDE_RS485(void) __attribute__((weak));
extern ring_buffer_t *RB_PROVIDE_RS232(void) __attribute__((weak));

typedef ring_buffer_t *(*rb_provide_fn_t)(void);

static const rb_provide_fn_t g_rb_provide[RB_CNT_MAX] = {
    [RB_SLOT_RJ45]  = RB_PROVIDE_RJ45,
    [RB_SLOT_RS485] = RB_PROVIDE_RS485,
    [RB_SLOT_RS232] = RB_PROVIDE_RS232,
};

static const char *const names[RB_CNT_MAX] = {
    [RB_SLOT_RJ45]  = "rb_rj45",
    [RB_SLOT_RS485] = "rb_rs485",
    [RB_SLOT_RS232] = "rb_rs232",
};

_Static_assert(sizeof(g_rb_provide) / sizeof(g_rb_provide[0]) == RB_CNT_MAX, "g_rb_provide length");
_Static_assert(sizeof(names) / sizeof(names[0]) == RB_CNT_MAX, "names length");
_Static_assert(RB_SLOT_RJ45 == 0 && RB_SLOT_RS485 == 1 && RB_SLOT_RS232 == 2 &&
                   RB_SLOT_COUNT == 3,
               "rb_slot_t must stay contiguous");
_Static_assert(RB_SIZE_RJ45 == 1536U && RB_SIZE_RS485 == 768U && RB_SIZE_RS232 == 768U,
               "RB sizes must match product plan");

/* ================================================================
 *  调度上下文 — 全部运行时的唯一状态聚合
 *
 *  新增调度相关字段只需在此结构体中增加即可，
 *  所有调度函数统一通过 g_dispatch 访问。
 * ================================================================ */

dispatch_ctx_t g_dispatch;           /**< 全局调度上下文 */
osThreadId_t g_dispatch_task_handle; /**< 帧分发任务句柄（外部用于 Suspend/Resume） */

/* ---- 诊断计数访问器（只读；保留供调试器/JTAG 观察与将来复用）---- */
uint32_t app_dispatch_qfull_drops(void)
{
    return g_dispatch.qfull_drop;
}

uint32_t app_dispatch_resync_count(void)
{
    return g_dispatch.resync_byte;
}

uint32_t app_dispatch_notify_drops(void)
{
    return g_dispatch.notify_drop;
}

/* ================================================================
 *  工具函数
 * ================================================================ */

/**
 * @brief 协议掩码 → 数组索引
 *
 * 通过 bit_ctz 计算尾零位数，将 bitmask 映射为 0~31 的索引。
 *
 * @param mask  协议掩码（必须是 2 的幂或 0）
 * @return      对应的数组索引，mask=0 返回 0xFF
 */
uint8_t proto_index(uint32_t mask)
{
    if (mask == 0) return 0xff;
    return (uint8_t)bit_ctz(mask);
}

/* ================================================================
 *  协议注册 — 协议模块通过 sw_app_initcall 自注册
 *
 *  各协议模块的 init 函数中调用以下函数完成注册：
 *    1. app_proto_acquire_buf()  — 获取环形缓冲区
 *    2. app_proto_register()     — 注册探测函数 + 缓冲区，自动分配掩码并返回
 *    3. app_proto_bind_channel() — 声明该协议走哪些通道
 *    4. osThreadNew()            — 创建协议处理任务
 * ================================================================ */

/**
 * @brief 注册协议探测函数和环形缓冲区，自动分配掩码
 * @param probe  帧探测函数指针
 * @param rb     环形缓冲区指针
 * @return       自动分配的协议掩码（2 的幂），0 = 槽位已满
 */
proto_mask_t app_proto_register(proto_probe_fn_t probe, ring_buffer_t *rb)
{
    if (rb == nullptr)
        return 0;

    /* 找第一个空闲位 */
    uint32_t free_bits = ~g_dispatch.registered_mask;
    if (free_bits == 0) return 0; /* 32 槽全满 */

    uint8_t idx       = (uint8_t)bit_ctz(free_bits);
    proto_mask_t mask = (proto_mask_t)(1U << idx);

    g_dispatch.proto_rb[idx]    = rb;
    g_dispatch.proto_probe[idx] = probe;
    g_dispatch.registered_mask |= mask;

    return mask;
}

/**
 * @brief 设置协议的帧消息队列
 * @param mask   协议掩码
 * @param queue  队列句柄（osMessageQueueId_t，存储为 void *）
 *
 * 协议处理任务创建队列后调用，框架据此将完整帧推入对应队列。
 */
void app_proto_set_frame_queue(proto_mask_t mask, osMessageQueueId_t queue)
{
    uint8_t idx = proto_index(mask);
    if (idx >= PROTO_MAX_COUNT) return;
    g_dispatch.frame_queue[idx] = queue;
}

/**
 * @brief 绑定协议到通道（声明该协议监听哪些通道的数据）
 * @param mask   协议掩码
 * @param ch_id  通道标识（CH_ID_RS485 等）
 *
 * 内部为 OR 累积——多个协议可绑定同一通道，同一协议也可绑定多个通道。
 * ch_proto_map 初始全零，全部由协议模块通过此函数声明。
 *
 * ⚠ 调用时机契约（2026-10-08 C3-D 起被 `app_boot.c` 的通道启动门控依赖）：
 * **必须在 `sw_app_initcall` 注册的 init 内调用**（即 `sw_board_init()` 阶段、
 * 早于 `app_boot.c` 的通道启动）——未绑定的通道不会启动。新增协议时保持此前提；
 * 结构校验脚本 `.analysis/audit_20261008/landing_20261008/check_channel_gate.py` 会检查。
 */
void app_proto_bind_channel(proto_mask_t mask, channel_id_t ch_id)
{
    g_dispatch.ch_proto_map[ch_id] |= mask;
}

/**
 * @brief 查询某通道是否已有协议绑定（只读；越界返回 false）
 *
 * 供 app_boot.c 的通道启动编排使用（2026-10-08 C3-D 加固）：绑定关系全部由协议
 * 模块在 sw_board_init()（sw_app_initcall）中声明，早于通道启动；未绑定的通道
 * 启动后无人消费数据（此前静默丢弃、零日志），不启动可同时省下 netconn / DMA RX。
 */
bool app_dispatch_channel_has_proto(channel_id_t ch_id)
{
    if ((uint32_t)ch_id >= (uint32_t)CH_ID_MAX)
        return false;
    return g_dispatch.ch_proto_map[ch_id] != 0U;
}

/**
 * @brief 从缓冲区池获取环形缓冲区
 * @param id    rb_slot_t（RB_SLOT_RJ45 / RS485 / RS232）
 * @param size  期望容量（不得超过该槽实际 size；无体时返回 nullptr）
 * @return      环形缓冲区指针，失败返回 nullptr
 *
 * 判空顺序：id 越界 → buf_pool 缓存早返回 → provide==NULL/rb==NULL → size>rb->size → rb_init。
 */
ring_buffer_t *app_proto_acquire_buf(uint8_t id, uint16_t size)
{
    if (id >= RB_CNT_MAX)
        return nullptr;

    if (g_dispatch.buf_pool[id] != nullptr)
        return g_dispatch.buf_pool[id];

    rb_provide_fn_t provide = g_rb_provide[id];
    if (provide == nullptr)
        return nullptr;

    ring_buffer_t *rb = provide();
    if (rb == nullptr)
        return nullptr;

    if (size > rb->size)
        return nullptr;

    rb_init(rb, names[id]);
    g_dispatch.buf_pool[id] = rb;
    return rb;
}

/* ================================================================
 *  调度系统初始化 — sw_app_initcall(3)，同层按符号名字母序先于协议注册
 *
 *  创建 ch_queue → 创建 frame_dispatch_task → 返回。
 * ================================================================ */

void app_dispatch_init(void)
{
    g_dispatch.ch_queue = osMessageQueueNew(MAX_CHANNELS, sizeof(channel_t *), &s_ch_queue_attr);

    /* 帧分发任务：遍历 ring buffer，调用各协议的探测函数 */
    const osThreadAttr_t frame_dispatch_task_attr = {
        .name       = "frame_dispatch_task",
        .priority   = osPriorityNormal,
        PL_TASK_STATIC_ATTR(frame_dispatch, 256),
    };
    g_dispatch_task_handle = osThreadNew(frame_dispatch_task, nullptr, &frame_dispatch_task_attr);
}
sw_app_initcall(app_dispatch_init);

/* ================================================================
 *  frame_dispatch_task — 帧分发引擎（核心调度循环）
 *
 *  ch_queue 通知 → 按 ch_id 取协议掩码 → 逐协议 probe → READY/SKIP 消费整帧
 *  推入协议队列；无消费时按 WAIT/FAKE 语义等待或 skip 1 字节重同步。
 *  关键设计：一轮通知可连续解析多帧；共享 RB 的协议按指针去重；
 *  持锁跨整轮探测+读取（消除 TOCTOU 窗口）。
 * ================================================================ */

/**
 * @brief  环形缓冲区头部前缀指纹（WAIT 预算进度判据；调用者须已持 rb 锁）。
 *
 * 前 min(4, avail) 字节 + 参与字节数打包为 32 位指纹。判据意义：avail 增长
 *（数据变多）但头部不变时指纹不变——这正是区分「正常攒帧」与「残帧卡头」的关键。
 * @param  rb 环形缓冲区（持锁调用）；@param  avail 当前可读字节数。
 * @return 头部前缀指纹（avail==0 → 0）。
 */
static uint32_t _rb_head_fp(const ring_buffer_t *rb, uint16_t avail)
{
    uint8_t head[4];
    const uint16_t n = (avail < (uint16_t)sizeof(head)) ? avail : (uint16_t)sizeof(head);
    if (n == 0U)
        return 0U;
    rb_peek(rb, 0U, head, n, nullptr);

    uint32_t fp = (uint32_t)n;
    for (uint16_t i = 0U; i < n; i++)
        fp = (fp << 8) | head[i];
    return fp;
}

/**
 * @brief  环形缓冲区 → 缓冲池槽号（`RB_SLOT_*`；WAIT 预算分槽用；调用者持 rb 锁）。
 * @note   槽表由 `app_proto_acquire_buf` 填充，而本函数只在 `g_dispatch.proto_rb[]`
 *         中的 RB 上调用 → 必命中；未命中兜底归槽 0（不越界，理论不可达）。
 */
static uint8_t _rb_slot_of(const ring_buffer_t *rb)
{
    for (uint8_t s = 0U; s < RB_CNT_MAX; s++)
        if (g_dispatch.buf_pool[s] == rb)
            return s;
    return 0U;
}

void frame_dispatch_task(void *argument)
{
    (void)argument;
    channel_t *ch; /**< 来源通道指针（从 ch_queue 取出） */
    frame_msg_t *msg   = (frame_msg_t *)_msg_dispatch_buf;
    uint32_t frame_len = 0; /**< 探测到的完整帧长度 */
    uint8_t aux        = 0; /**< 辅助信息（如命令码） */

    /* WAIT 头阻塞预算状态（**按物理 RB 分槽**，本任务单消费者独占）。
     * 进度判据 = 头部前缀指纹，不是 avail 增长：残帧卡头时新数据仍会推高 avail，
     * 按 avail 重置预算会使强制重同步永不触发（该 RB 永久不消费，现场根因见 doc/15 §9）。
     * 按 RB 分槽：全局单状态会被「另一条 RB 的到达」不断重置，卡头 RB 永不到期。 */
    static bool     s_wait_active[RB_CNT_MAX];  /**< 各 RB 的 WAIT 预算激活 */
    static uint32_t s_wait_tick[RB_CNT_MAX];    /**< 各 RB 的预算起点 tick */
    static uint32_t s_wait_head_fp[RB_CNT_MAX]; /**< 各 RB 预算起点的头部前缀指纹 */

    for (;;) {
        /* 阻塞等待：任一通道收到数据时唤醒 */
        if (osMessageQueueGet(g_dispatch.ch_queue, &ch, NULL, osWaitForever) != osOK)
            continue;

        /* 通道回验：通知携带的指针必须仍注册在案，否则丢弃该通知。
         * 防御连接任务退出后栈上通道实例悬垂（配合通道静态化根治）：
         * 已注销（get 返回 NULL 或其它实例）的脏通知不进调度，
         * 也避免用野 ch_id 越界索引 ch_proto_map。 */
        if (ch == nullptr || app_channel_get(ch->ch_id) != ch)
            continue;

        /* 根据通道 ID 查表获得该通道承载的协议掩码 */
        proto_mask_t proto = g_dispatch.ch_proto_map[ch->ch_id];

        /* ---- 无协议承载：数据到达但没有任何协议注册并绑定到该通道
         * （ch_proto_map=0：构建期把协议目录排除掉 / 新通道漏 bind / 绑定失败）。
         * 这种情况下无法探测任何帧，字节留在 RB 里被丢弃 → 直接跳过整轮探测。 */
        if (proto == 0U) {
            continue;
        }

        /* 外循环：遍历已注册协议位 */
        uint32_t outer_iter = g_dispatch.registered_mask;
        while (outer_iter) {
            uint8_t i = (uint8_t)bit_ctz(outer_iter);
            outer_iter &= outer_iter - 1;
            uint32_t mask     = (1U << i);
            ring_buffer_t *rb = g_dispatch.proto_rb[i];

            /* 跳过：通道不承载此协议 / 空缓冲区 */
            if ((proto & mask) == 0) continue;
            if (rb == nullptr) continue;

            /* 跳过已处理过的缓冲区（仅限同一通道上的协议） */
            bool dup = false;
            for (uint8_t k = 0; k < i; k++)
                if ((proto & (1U << k)) && g_dispatch.proto_rb[k] == rb) {
                    dup = true;
                    break;
                }
            if (dup) continue;

            /* 持锁跨整轮探测+读取，消除TOCTOU */
            rb_lock(rb);
            uint16_t avail = rb_avail(rb, nullptr);

            /* 内循环：从同一缓冲区中连续提取多帧。
             * 防御性迭代上限：单轮通知最多解析 64 帧，超出则强制丢 1 字节
             * 重同步后退出，杜绝任何异常路径（如 SKIP 0 字节）死循环。 */
            uint8_t inner_guard = 0;
            while (avail > 0) {
                if (++inner_guard > 64U) {
                    avail -= rb_skip(rb, 1, nullptr);
                    break;
                }

                bool any_wait    = false; /* 有协议：帧头可能匹配但数据不足 */
                bool any_fake    = false; /* 有协议：明确不是我的帧 */
                bool any_overrun = false; /* 有协议：frame_len 越界不可信 */
                bool any_parsed  = false; /* 有协议：READY 读走或 SKIP 跳过 */

                /* 按协议优先级顺序探测已注册协议 */
                uint32_t inner_iter = g_dispatch.registered_mask;
                while (inner_iter) {
                    uint8_t j = (uint8_t)bit_ctz(inner_iter);
                    inner_iter &= inner_iter - 1;
                    uint32_t inner_mask = (1U << j);

                    if ((proto & inner_mask) == 0) continue;
                    if (g_dispatch.proto_rb[j] != rb) continue;
                    if (g_dispatch.proto_probe[j] == nullptr) continue;

                    /* 调用探测函数（调用者持锁，探测内部传 nullptr 跳过锁） */
                    proto_probe_sta_t state = g_dispatch.proto_probe[j](ch, rb, &frame_len, &aux);

                    if (state == PROTO_PROBE_READY) {
                        /* 越界钳制：frame_len > 静态缓冲上限（1044B）时不可信，
                         * 不消费该帧；本轮不置 any_parsed，交给外层决策按
                         * 重同步 skip 1 字节处理，防止 rb_read 写穿缓冲。 */
                        if (frame_len > FRAME_DATA_MAX_LEN) {
                            any_overrun = true;
                            break;
                        }

                        /* 帧头已匹配但数据未到齐：置 any_wait 等新字节，
                         * 不再置 any_parsed —— 旧代码此处置位而 avail 不变，
                         * 内层 while 空转活锁。 */
                        if (avail < frame_len) {
                            any_wait = true;
                            break;
                        }

                        /* 完整帧就绪：从缓冲区读出 → 推入协议处理队列 */
                        uint16_t actual = rb_read(rb, msg->data, frame_len, nullptr);
                        avail           = rb_avail(rb, nullptr);

                        if (actual == frame_len) {
                            msg->data_len = frame_len;
                            msg->ch       = ch;
                            /* 队列满 → 丢帧（协议任务被卡住/过载的证据）：计数后丢弃 */
                            if (osMessageQueuePut(g_dispatch.frame_queue[j], msg, 0, 0) != osOK) {
                                g_dispatch.qfull_drop++;
                            }
                        } else {
                            /* 异常：读出字节数不匹配，丢弃已读部分 */
                            rb_skip(rb, actual, nullptr);
                            avail = rb_avail(rb, nullptr);
                        }
                        any_parsed = true;
                        break; /* 成功解析一帧，回到 while 继续下一帧 */

                    } else if (state == PROTO_PROBE_SKIP) {
                        /* 帧结构合法但不属于本设备，跳过整帧。
                         * frame_len 越界同样不可信 → 交外层重同步处理。 */
                        if (frame_len > FRAME_DATA_MAX_LEN) {
                            any_overrun = true;
                            break;
                        }
                        /* 数据未到齐 → 置 any_wait 等新字节（防空转） */
                        if (avail < frame_len) {
                            any_wait = true;
                            break;
                        }
                        avail -= rb_skip(rb, frame_len, nullptr);
                        any_parsed = true;
                        break; /* SKIP 与 READY 一样终止本轮链路 */

                    } else if (state == PROTO_PROBE_WAIT) {
                        /* 数据不足，协议等待更多字节 —— 继续探测下一个协议 */
                        any_wait = true;

                    } else if (state == PROTO_PROBE_FAKE) {
                        /* 明确不是本协议 —— 继续探测下一个协议 */
                        any_fake = true;
                    }
                }

                /* 本 RB 的 WAIT 预算槽号（按 RB 独立计时，见任务头部状态注释） */
                const uint8_t wait_slot = _rb_slot_of(rb);


                /* 无协议成功解析时的决策：
                 *   any_wait    → 禁 skip，等更多字节（带 FRAME_WAIT_BUDGET_MS 防挂死）
                 *   any_overrun / any_fake → skip 1 字节重同步
                 *   其它        → 空缓冲区异常保护（防死循环） */
                if (!any_parsed) {
                    if (any_wait) {
                        /* 帧头已匹配、数据未到齐 → 等更多字节；带时间预算防
                         * 「残帧卡头后永不到齐」的挂死（预算语义/取值依据见
                         * 文件头 FRAME_WAIT_BUDGET_MS 注释）。
                         *
                         * **进度判据 = 头部前缀指纹变化（头被消费/前进），
                         * 绝不能按 avail 增长重置**：残帧卡头时用户每发一帧
                         * 新数据 avail 都增长，按 avail 重置 = 预算永不到期 =
                         * 强制重同步永不触发 = 整条 RB 永久不消费（根因 B）。
                         * 头部指纹不变（哪怕缓冲区更长）＝ 没有进度，预算照走。 */
                        const uint32_t now = osKernelGetTickCount();
                        const uint32_t fp  = _rb_head_fp(rb, avail);
                        if (!s_wait_active[wait_slot] || fp != s_wait_head_fp[wait_slot]) {
                            /* 首次等待 / 头部已前进 → 重置本 RB 的预算起点 */
                            s_wait_active[wait_slot]  = true;
                            s_wait_tick[wait_slot]    = now;
                            s_wait_head_fp[wait_slot] = fp;
                        } else if ((now - s_wait_tick[wait_slot]) >
                                   pdMS_TO_TICKS(FRAME_WAIT_BUDGET_MS)) {
                            /* 预算到期：头部前缀在预算时间内无任何前进 → 判残帧
                             * 卡头，强制 skip 1 字节重同步，并累计重同步次数。 */
                            avail -= rb_skip(rb, 1, nullptr);
                            g_dispatch.resync_byte++;
                            s_wait_active[wait_slot] = false;
                        }
                        break; /* 等待更多数据到达 */
                    } else if (any_fake || any_overrun) {
                        s_wait_active[wait_slot] = false; /* 有字节被消费，重置预算 */
                        avail -= rb_skip(rb, 1, nullptr); /* 重同步 */
                        g_dispatch.resync_byte++;
                    } else {
                        break; /* 无协议绑定或探测函数全空，防死循环 */
                    }
                } else {
                    s_wait_active[wait_slot] = false; /* 有帧被消费，重置预算 */
                }
            }
            rb_unlock(rb);
        }
    }
}

/* ================================================================
 *  channel_send — 通道发送（OCP：虚表分派）
 *
 *  协议处理任务调用此函数回复数据，通过 ch_ops 虚表分派到
 *  具体通道的 send 实现。新增通道类型无需修改此函数。
 *
 *  安全守卫: ch->ops == nullptr 表示通道已销毁，拒绝发送（返回 -1）。
 *            TCP/UDP 连接任务退出前会置 ops = nullptr。
 *  通道回验: ch 必须仍注册在案（app_channel_get 返回同一指针），
 *            防御连接任务退出后栈上通道实例悬垂（配合通道静态化根治）；
 *            ch_id 越界时 app_channel_get 返回 NULL 即不通过。
 * ================================================================ */

int32_t channel_send(channel_t *ch, uint8_t *data, uint16_t len)
{
    if (ch == nullptr || ch->ops == nullptr)
        return -1;
    if (app_channel_get(ch->ch_id) != ch)
        return -1;
    if (ch->ops->send == nullptr)
        return -1;
    return ch->ops->send(ch, data, len);
}

/* ================================================================
 *  app_channel_dispatch — 通道接收分发
 *
 *  所有通道任务的接收路径统一入口：
 *    1. 根据 ch->ch_id 查 ch_proto_map 获得协议掩码
 *    2. 遍历所有协议，将数据写入对应的 ring buffer
 *    3. 共享同一 RB 的多个协议通过 seen[] 指针去重，避免重复写入
 *    4. 向 ch_queue 发送通道指针通知帧分发任务
 *
 *  注意：通道任务不要直接操作 ring buffer 或 mutex。
 * ================================================================ */

void app_channel_dispatch(const channel_t *ch, const uint8_t *data, uint16_t len)
{
    /* 防御性断言：调度系统必须已初始化（同层字母序 app_dispatch_init 先于协议） */
    if (g_dispatch.ch_queue == nullptr) return;

    /* 根据通道 ID 查表获得协议掩码 */
    proto_mask_t proto = g_dispatch.ch_proto_map[ch->ch_id];

    /* 已写入的 RB 去重：用计数器遍历，避免依赖 seen[] 连续填充假设 */
    ring_buffer_t *seen[PROTO_MAX_COUNT] = {nullptr};
    uint8_t seen_cnt = 0;

    /* 遍历已注册协议位，将数据写入匹配的环形缓冲区 */
    uint32_t write_iter = g_dispatch.registered_mask;
    while (write_iter) {
        uint8_t i = (uint8_t)bit_ctz(write_iter);
        write_iter &= write_iter - 1;
        uint32_t mask = (1U << i);
        if ((proto & mask) == 0) continue;

        ring_buffer_t *rb = g_dispatch.proto_rb[i];
        if (rb == nullptr) continue;

        /* RB 指针去重：多个协议共享同一缓冲区时只写一次 */
        bool dup = false;
        for (uint8_t k = 0; k < seen_cnt; k++)
            if (seen[k] == rb) {
                dup = true;
                break;
            }
        if (dup) continue;

        rb_write(rb, data, len, rb->mutex);
        seen[seen_cnt++] = rb;
    }

    // 关闭工厂模式
    app_factory_mode_interrupt();

    /* 通知帧分发任务：传入通道指针的地址（不是通道结构体的地址）
     * 队列每项大小为 sizeof(channel_t *)，拷贝的是指针值本身。
     * 帧分发任务通过 osMessageQueueGet(&ch, ...) 读出指针。
     *
     * 队列满时先重试（DISPATCH_NOTIFY_RETRY_MS）再计数丢弃：通知被静默丢弃时数据
     * 虽已在 RB 里，但没有事件唤醒 frame_dispatch_task，要等下一条通知到达才被顺带处理
     *（现场表现为「发了命令无反应，重发一次才解析」）。重试窗口不持任何锁，无死锁路径。 */
    if (osMessageQueuePut(g_dispatch.ch_queue, &ch, 0, 0) != osOK) {
        if (osMessageQueuePut(g_dispatch.ch_queue, &ch, 0, pdMS_TO_TICKS(DISPATCH_NOTIFY_RETRY_MS)) !=
            osOK) {
            g_dispatch.notify_drop++;
        }
    }
}

/* ================================================================
 *  通道注册表 — 通道 init/deinit 时维护，Application 层按 ID 查询
 *
 *  app_channel_register(ch_id, ch)：通道 init 时注册，deinit 时传 nullptr 清空
 *  app_channel_get(ch_id)：返回已注册的 channel_t *（可能为 nullptr）
 *
 *  Device 层调用 register（通知模式，与 app_channel_dispatch 一致），
 *  Application 层调用 get（查询抽象指针，不触碰 Device 层全局变量）。
 * ================================================================ */

void app_channel_register(channel_id_t ch_id, channel_t *ch)
{
    if (ch_id < CH_ID_MAX)
        g_dispatch.channels[ch_id] = ch;
}

channel_t *app_channel_get(channel_id_t ch_id)
{
    if (ch_id < CH_ID_MAX)
        return g_dispatch.channels[ch_id];
    return nullptr;
}
