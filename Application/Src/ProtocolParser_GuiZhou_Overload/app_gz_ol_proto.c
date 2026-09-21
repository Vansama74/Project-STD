/**
 * @file    app_gz_ol_proto.c
 * @brief   贵州治超屏协议（"TCLY" 帧族）— 注册 / 探测 / 处理任务
 *
 * 绑定 RS485 + CH_ID_RS232 + CH_ID_UDP（10011）+ CH_ID_UDP_GZOL（专用业务口）四通道
 * （2026-09-14 增绑 UDP，用户裁决）：
 *   - 串口双通道覆盖源固件串口三路行为（README Q10）；
 *   - CH_ID_UDP = STD 既有 10011 发现口（LDI/IAP 同口）：**保留**——既有上位机若仍
 *     发往 10011 可正常应答（应答走收包通道单播回源）；
 *   - CH_ID_UDP_GZOL = 专用业务口（2026-09-14 用户裁决，README §13）：端口 =
 *     Sector1 `net_cfg.port`（与 TCP 业务口同号、不同协议栈）→ `0x40` 改端口对该
 *     口生效，语义对齐源固件「设备自身 UDP 服务口」；10011 与 CQ 业务口冲突时该
 *     实例自动跳过绑定（RTT 告警），GZ_OL 仍由 10011 通道承载。
 *     ⚠ 联调语义差异：源固件 9K23881580 出厂端口是 **10028** 且搜索应答走广播；
 *     本实现默认发往 **9528**（net_cfg.port 出厂默认）+ 单播回源；`0x40` 改端口后
 *     发往新端口即可（详见 doc/14 README §11/§13）。
 *
 * 帧头冲突纪律：首字节 0x54（ASCII 'T'）在全部既有协议槽位唯一——
 *   串口槽 0x0A(四川ETC) / 0x5A(安徽) / 0x7B('{' 族) / 0xFF(四川治超、RLS)；
 *   网口槽 0x5A5A5A5A(IAP) / 0xFF 0xFF(LDI) / '{' 或 FF FF+12B(重庆CQ)。
 * → **无需 EIDE 目录互斥排除**，非 '{' 帧族**不定义 g_brace_proto_guard**，
 *   与任何既有协议可同编共存（probe 首字节快拒 + 4 字节引导串全匹配双重判别）。
 *
 * RJ45 RB 纪律：本模块**不** provide rb_provide_rj45 —— 网口槽体由 IAP/LDI/CQ 等
 *   同槽协议 weak 提供（三者在全协议与 PROTO=CQ 口径均编入）；本模块只 acquire，
 *   避免任何裁剪构建下多出一份 1536B 缓冲（PROTO=ALL 口径 SRAM 余量仅 496B）。
 *
 * 波特率：协议要求 9600 8N1 → 现场 DIP1 必须置 OFF（9600，app_uart_baud 全局选择）。
 */

#include "app_gz_ol_proto.h"

#include "FreeRTOS.h"
#include "initcall.h"

#include "app_dispatch.h"
#include "app_gz_ol_proto_cmd.h"
#include "app_gz_ol_proto_parse.h"
#include "pl_task_static.h"

/* ---- 任务静态存储（栈 + TCB 落 CCMRAM，见 pl_task_static.h）----
 * gz_ol_handle_task：启动期创建一次、永不退出；静态化后不再占 ucHeap（省 1144B），
 * CCM 占 1124B。任务栈仅被 CPU 访问，不经 DMA。 */
PL_TASK_STATIC_STORAGE(gz_ol_handle, 256);

/* 地区协议通道 RB：与青海/四川/山东/贵州/云南/安徽等同槽 weak 合并。
 * RJ45 槽体不在此 provide（由 IAP/LDI/CQ 提供，见文件头「RJ45 RB 纪律」）。 */
RB_PROVIDE_WEAK(rb_provide_rs485, RB_SIZE_RS485);
RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232);

static proto_mask_t s_gz_ol_mask;         /* CH_ID_RS485（RS485 槽 RB） */
static proto_mask_t s_gz_ol_mask_rs232;   /* CH_ID_RS232（RS232 槽 RB） */
static proto_mask_t s_gz_ol_mask_udp;     /* CH_ID_UDP 10011（RJ45 槽 RB，同槽 weak 提供） */
static proto_mask_t s_gz_ol_mask_gzol;    /* CH_ID_UDP_GZOL 业务口（读 Sector1 net_cfg.port） */
static osMessageQueueId_t s_gz_ol_queue;

/* 静态队列：payload 256（实测合法最大帧 158B，1.6x 余量）。
 * 队列体 + 控制块 + 任务帧缓冲置 **CCMRAM**（README Q12 推荐 B）：
 * PROTO=ALL 口径 SRAM 余量仅 ~512B，放不下 ~1.1KB 静态体；这些缓冲为 CPU 独占
 * 访问（osMessageQueue 静态内存经 CPU memcpy，无 DMA/ETH），CCM 适用（CQ 先例）。 */
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
 * @brief  贵州治超屏协议模块初始化（sw_app_initcall 自注册）。
 *
 * 注册五步 × 三通道（doc/08-01）：RS485/RJ45 槽 acquire → 每通道独立 mask register
 * + bind → 三 mask 共用一静态队列 → 处理任务。
 *
 * @warning CH_ID_RS232_1 (USART6) 为语音板 TTS 专用，禁止绑定。
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

    /* 网口槽（CH_ID_UDP = 10011，LDI 发现口 + IAP 同口共享）：独立 mask 挂 RJ45 RB。
     * 槽体由 IAP/LDI/CQ weak 提供（本模块不 provide，见文件头）；若极端裁剪构建下
     * 同槽无任何提供者，acquire 返回 nullptr → 静默跳过网口绑定，串口双通道不受影响。 */
    ring_buffer_t *rb_rj45 = app_proto_acquire_buf(RB_SLOT_RJ45, RB_SIZE_RJ45);
    if (rb_rj45 != nullptr) {
        s_gz_ol_mask_udp = app_proto_register(gz_ol_probe_frame, rb_rj45);
        if (s_gz_ol_mask_udp != 0)
            app_proto_bind_channel(s_gz_ol_mask_udp, CH_ID_UDP);

        /* GZ_OL 专用业务口（用户裁决 2026-09-14，doc/14 §13）：独立 mask 同挂 RJ45 RB，
         * 绑 CH_ID_UDP_GZOL（实例端口 = Sector1 net_cfg.port，`0x40` 改端口即改此口）。
         * 同槽链式 probe 首字节 0x54 与 IAP/LDI/CQ 互斥（证据见
         * .analysis/9k23881580/gz_ol_udp_binding_report.md §2），无需额外防护。 */
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
 *
 *  ① avail == 0                  → FAKE（禁止盲 WAIT 阻塞链式探测）
 *  ② peek[0] != 0x54             → FAKE（首字节快拒）
 *  ③ avail < 4                   → WAIT（引导串未到齐）
 *  ④ peek[1..3] != 43 4C 59      → FAKE（4 字节引导串全匹配）
 *  ⑤ avail < 10                  → WAIT（长度字段未到齐）
 *  ⑥ len < 17                    → FAKE（结构性下界 = 16 帧头 + 1 尾）
 *     len > RB_SIZE_RS485-1       → FAKE（防"永不满足的 WAIT"卡死链式探测；
 *                                    三槽取保守下界 767——RJ45 槽 1535 更宽不影响：
 *                                    本协议结构合法帧 ≤256，超过 256 走 ⑨ SKIP）
 *  ⑦ avail < len                 → WAIT（整帧未到齐）
 *  ⑧ peek[len-1] != 0x00         → FAKE（尾字节定界校验）
 *  ⑨ len > GZ_OL_FRAME_LEN_MAX   → SKIP（结构合法但超本模块解析上限：整帧消费）
 *  ⑩ READY，写 *total_len = len
 *
 * 网口槽（RJ45）链式共存：同槽 IAP(0x5A5A5A5A)/LDI(0xFF 0xFF)/CQ('{' 或 FF FF+12B)
 * 三个 probe 均对首字节 0x54 快拒 FAKE，本 probe 对 0x5A/0xFF/0x7B 首字节同样 FAKE
 * → 双向不误吞（证据见 .analysis/9k23881580/gz_ol_udp_binding_report.md §2）。
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
