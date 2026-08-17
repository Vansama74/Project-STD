/**
 * @file    app_sc_mtc_proto.c
 * @brief   四川 MTC 费显协议模块（1E 方案二）— 注册 / 探测 / 处理任务
 *
 * 绑定 RS485 + RS232 双通道。探测支持双帧型：
 *   1. '{' (0x7B) 帧族：'1'~'9','A' 命令（无 BCC 参考格式 + 带 BCC 变体，BCC 不校验）
 *      与 7B 40~45 原始帧族（无 BCC）；
 *   2. 0A 46 0A / 0A 46 0D 主机查询/清屏。
 * 与青海协议（同为 '{' 开头）的互斥分析见 doc/05_协议模块多协议兼容优化/01_architecture.md §6：
 * 本 probe 以「每命令定长 + BCC + '}' 尾字节」拒绝青海帧（青海帧长度字段为二进制长度，
 * 与 MTC 定长不符即 FAKE）；极端巧合见文档说明，量产由 EIDE 目录排除纪律兜底。
 */

#include "app_sc_mtc_proto.h"

#include "FreeRTOS.h"
#include "initcall.h"
#include "app_dispatch.h"

#include "app_sc_mtc_proto_parse.h"
#include "app_sc_mtc_proto_cmd.h"

/* 地区协议通道 RB：与青海/ETC/治超等同槽 weak 合并 */
RB_PROVIDE_WEAK(rb_provide_rs485, RB_SIZE_RS485);
RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232);

static proto_mask_t s_sc_mtc_mask;
static proto_mask_t s_sc_mtc_mask_rs232;
static osMessageQueueId_t s_sc_mtc_queue;

/* 静态队列：payload 覆盖协议最大帧（'4' 全屏 68B / '7'自定义语音上限 74B） */
#define SC_MTC_PAYLOAD_MAX (74U)
#define SC_MTC_MSG_SIZE    (sizeof(frame_msg_t) + SC_MTC_PAYLOAD_MAX)
#define SC_MTC_QUEUE_DEPTH (3U)
_Static_assert(SC_MTC_PAYLOAD_MAX <= RB_SIZE_RS485, "SC_MTC frame must fit RS485 RB");
_Static_assert(SC_MTC_PAYLOAD_MAX <= RB_SIZE_RS232, "SC_MTC frame must fit RS232 RB");
static StaticQueue_t s_sc_mtc_queue_cb;
static uint8_t s_sc_mtc_queue_buf[SC_MTC_QUEUE_DEPTH * SC_MTC_MSG_SIZE];
static const osMessageQueueAttr_t s_sc_mtc_queue_attr = {
    .name    = "sc_mtc_queue",
    .cb_mem  = &s_sc_mtc_queue_cb,
    .cb_size = sizeof(s_sc_mtc_queue_cb),
    .mq_mem  = s_sc_mtc_queue_buf,
    .mq_size = sizeof(s_sc_mtc_queue_buf),
};

static proto_probe_sta_t sc_mtc_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                            uint32_t *total_len, uint8_t *aux);

/**
 * @brief  计算 '{' 帧族 BCC：命令字(含)到参数(含)逐字节异或。
 * @param  buf  帧缓冲（buf[0]='{'）。
 * @param  len  帧长度。
 * @return BCC 期望值（与 buf[len-2] 比较）。
 */
static uint8_t _sc_mtc_bcc_calc(const uint8_t *buf, uint16_t len)
{
    uint8_t bcc = 0;
    for (uint16_t i = 1U; i + 2U < len; i++) { /* [1, len-3]，不含 BCC(len-2) 与 '}'(len-1) */
        bcc ^= buf[i];
    }
    return bcc;
}

/**
 * @brief  '{' 帧族：BCC 与 '}' 尾字节联合校验。
 * @param  rb    环形缓冲区。
 * @param  len   候选帧长度。
 * @return true=校验通过。
 */
static bool _sc_mtc_verify_brace_frame(const ring_buffer_t *rb, uint16_t len)
{
    uint8_t buf[SC_MTC_PAYLOAD_MAX];
    if (rb_peek(rb, 0, buf, len, NULL) != len)
        return false;
    if (buf[len - 1U] != '}')
        return false;
    return _sc_mtc_bcc_calc(buf, len) == buf[len - 2U];
}

/**
 * @brief  四川 MTC 费显协议模块初始化入口。
 * @note   通过 initcall 自动注册：申请 RS485 / RS232 RB → 注册探测器 →
 *         绑定双通道 → 创建队列与任务。
 *
 * @warning CH_ID_RS232_1 (PL_UART6) 为语音板 TTS 专用，任何地区协议禁止绑定。
 */
void sc_mtc_proto_init(void)
{
    ring_buffer_t *rb = app_proto_acquire_buf(RB_SLOT_RS485, RB_SIZE_RS485);
    if (rb == nullptr)
        return;

    s_sc_mtc_mask = app_proto_register(sc_mtc_probe_frame, rb);
    if (s_sc_mtc_mask == 0)
        return;

    app_proto_bind_channel(s_sc_mtc_mask, CH_ID_RS485);

    ring_buffer_t *rb_rs232 = app_proto_acquire_buf(RB_SLOT_RS232, RB_SIZE_RS232);
    if (rb_rs232 != nullptr) {
        s_sc_mtc_mask_rs232 = app_proto_register(sc_mtc_probe_frame, rb_rs232);
        if (s_sc_mtc_mask_rs232 != 0)
            app_proto_bind_channel(s_sc_mtc_mask_rs232, CH_ID_RS232);
    }

    s_sc_mtc_queue = osMessageQueueNew(SC_MTC_QUEUE_DEPTH, SC_MTC_MSG_SIZE, &s_sc_mtc_queue_attr);
    app_proto_set_frame_queue(s_sc_mtc_mask, s_sc_mtc_queue);
    if (s_sc_mtc_mask_rs232 != 0)
        app_proto_set_frame_queue(s_sc_mtc_mask_rs232, s_sc_mtc_queue);

    static const osThreadAttr_t s_sc_mtc_task_attr = {
        .name       = "sc_mtc_handle_task",
        .stack_size = 256 * 4,
        .priority   = osPriorityNormal,
    };
    osThreadNew(sc_mtc_proto_handle_task, NULL, &s_sc_mtc_task_attr);
}
sw_app_initcall(sc_mtc_proto_init);

/**
 * @brief  MTC 协议帧探测（PURE 函数契约：只 rb_peek，无副作用）
 *
 * 首字节快速拒绝：非 '{' 且非 0x0A → 立即 FAKE。
 *
 * '{' 帧族：
 *   - '1'~'9','A'：按命令双长度匹配——无 BCC（9K1F212701 格式：
 *     '1','2','5'→3B；'3'→20B；'4'→67B；'6'→15/24B；'7'固定→4B；'8','9'→4B）
 *     与带 BCC 变体（+1B）；BCC 不校验（参考项目不校验），'}' 尾字节必查。
 *   - '7'+'8' 自定义文本→变长扫描（扩展帧，校验 BCC）。
 *   - 0x40~45 原始帧族（创迪添加协议，文档示例无 BCC）：40→6B，41~44→4B，45→3B。
 *     注：0x41='A'、0x42='B' 与青海 'A'/'B' 命令字重叠，本模块 initcall 符号名
 *     app_sc_mtc_proto_init < qh_proto_init，同 RB 上 MTC probe 先于青海被调用，
 *     4 字节 7B 41/42 原始帧由 MTC 认领；青海帧按长度/BCC 被拒。详见兼容矩阵。
 * 0x0A 帧族：仅 0A 46 0A / 0A 46 0D 两种，其余（含 ETC 0A 帧）→ FAKE。
 */
static proto_probe_sta_t sc_mtc_probe_frame(const channel_t *ch, const ring_buffer_t *rb,
                                            uint32_t *total_len, uint8_t *aux)
{
    (void)ch;
    (void)aux;
    uint32_t avail = rb_avail(rb, NULL);

    /* 首字节快速拒绝：无数据 → FAKE */
    if (avail == 0)
        return PROTO_PROBE_FAKE;

    uint8_t first_byte;
    rb_peek(rb, 0, &first_byte, 1, NULL);

    if (first_byte == 0x0A) {
        /* 主机查询 0A 46 0A / 清屏 0A 46 0D；其余 0A 帧属 ETC → FAKE */
        if (avail < 3U)
            return PROTO_PROBE_WAIT;
        uint8_t b[2];
        rb_peek(rb, 1, b, sizeof(b), NULL);
        if (b[0] == 0x46 && (b[1] == 0x0A || b[1] == 0x0D)) {
            *total_len = 3U;
            return PROTO_PROBE_READY;
        }
        return PROTO_PROBE_FAKE;
    }

    if (first_byte != '{')
        return PROTO_PROBE_FAKE;

    if (avail < 2U)
        return PROTO_PROBE_WAIT;

    uint8_t cmd_byte;
    rb_peek(rb, 1, &cmd_byte, 1, NULL);

    /* ---- 7B 40~45 原始帧族（无 BCC）；0x41 与 'A' 同字节值，交由 'A' 分支按长度区分 ---- */
    if ((cmd_byte >= 0x40 && cmd_byte <= 0x45) && cmd_byte != 0x41) {
        uint16_t frame_len;
        if (cmd_byte == 0x40) {
            frame_len = 6U; /* 7B 40 mode baud_hi baud_lo 7D */
        } else if (cmd_byte == 0x45) {
            frame_len = 3U; /* 7B 45 7D */
        } else {
            frame_len = 4U; /* 7B 42/43/44 xx 7D */
        }
        if (avail < frame_len)
            return PROTO_PROBE_WAIT;
        uint8_t tail;
        rb_peek(rb, (uint32_t)(frame_len - 1U), &tail, 1, NULL);
        if (tail != '}')
            return PROTO_PROBE_FAKE;
        *total_len = frame_len;
        return PROTO_PROBE_READY;
    }

    /* ---- '1'~'9','A' 帧族：按命令双长度匹配（无 BCC / 带 BCC）----
     * 参考项目 9K1F212701 mtc.c：'{' 帧族【不含 BCC】，帧长：
     *   '1','2','5'→3B；'3'→20B；'4'→67B；'6'→客车15B/货车24B；'7'固定→4B；'8','9'→4B。
     * 同时兼容带 BCC 变体（长度 +1：4/21/68/16/25/5/5B）；BCC 不校验（参考项目不校验）。
     * 注：带 BCC 帧的数据末字节若恰为 0x7D('}') 会被误判为无 BCC 短帧（≈1/256），
     * 与青海 '3' 帧重叠风险同属已知残留，量产由 EIDE 目录排除纪律兜底。 */
    if (!((cmd_byte >= '1' && cmd_byte <= '9') || cmd_byte == 'A'))
        return PROTO_PROBE_FAKE;

    uint16_t len_lo = 0U; /* 无 BCC 帧长（9K1F212701 格式） */
    uint16_t len_hi = 0U; /* 带 BCC 帧长（+1） */
    bool variable_len = false;

    switch (cmd_byte) {
        case '1':
        case '2':
        case '5':
            len_lo = 3U; len_hi = 4U;
            break;
        case '3':
            len_lo = 20U; len_hi = 21U; /* '{' '3' 行号 + 16B (+BCC) '}' */
            break;
        case '4':
            len_lo = 67U; len_hi = 68U; /* '{' '4' 64B (+BCC) '}' */
            break;
        case '6':
            if (avail < 3U)
                return PROTO_PROBE_WAIT;
            {
                uint8_t type;
                rb_peek(rb, 2, &type, 1, NULL);
                if (type == '0') {
                    len_lo = 15U; len_hi = 16U; /* 客车 X1~X11 (+BCC) */
                } else if (type == '1') {
                    len_lo = 24U; len_hi = 25U; /* 货车 X1~X20 (+BCC) */
                } else {
                    return PROTO_PROBE_FAKE;
                }
            }
            break;
        case '7':
            if (avail < 3U)
                return PROTO_PROBE_WAIT;
            {
                uint8_t v;
                rb_peek(rb, 2, &v, 1, NULL);
                if (v >= '0' && v <= '7') {
                    len_lo = 4U; len_hi = 5U; /* '{' '7' X (+BCC) '}' */
                } else if (v == '8') {
                    variable_len = true; /* '{' '7' '8' GBK文本 BCC '}'（扩展，校验 BCC） */
                } else {
                    return PROTO_PROBE_FAKE; /* '9' 未定义 */
                }
            }
            break;
        case '8':
        case '9':
            len_lo = 4U; len_hi = 5U;
            break;
        case 'A': /* 0x41 双义：7B 41 xx 7D(4B 原始点阵大小，xx≤2) 与 '{A x BCC }'(5B 颜色，x='1'~'3') */
            if (avail < 4U)
                return PROTO_PROBE_WAIT;
            {
                uint8_t b2, b3;
                rb_peek(rb, 2, &b2, 1, NULL);
                rb_peek(rb, 3, &b3, 1, NULL);
                /* b2≤2 才可能是点阵帧：颜色帧 x='1'~'3'(0x31~0x33)>2 永不被 4B 认领，
                 * 消除「颜色帧 BCC 恰为 '}' 被误判 4B」的 1/256 巧合（原实现无条件 b3=='}' 认领） */
                if (b3 == '}' && b2 <= 2U) {
                    *total_len = 4U;
                    return PROTO_PROBE_READY;
                }
            }
            len_lo = 5U; len_hi = 5U;
            break;
        default:
            return PROTO_PROBE_FAKE;
    }

    if (variable_len) {
        /* 变长：扫描 '}' 并验证 BCC；总长上限 SC_MTC_PAYLOAD_MAX */
        uint32_t max_scan = (avail < SC_MTC_PAYLOAD_MAX) ? avail : SC_MTC_PAYLOAD_MAX;
        for (uint32_t e = 5U; e + 1U <= max_scan; e++) { /* e = '}' 位置 */
            uint8_t tail;
            rb_peek(rb, e, &tail, 1, NULL);
            if (tail != '}')
                continue;
            if (_sc_mtc_verify_brace_frame(rb, (uint16_t)(e + 1U))) {
                *total_len = e + 1U;
                return PROTO_PROBE_READY;
            }
        }
        /* 未找到合法 '}'：缓冲不足等待，达到上限则 FAKE */
        if (avail >= SC_MTC_PAYLOAD_MAX)
            return PROTO_PROBE_FAKE;
        return PROTO_PROBE_WAIT;
    }

    /* 双长度匹配：先无 BCC 短帧，后带 BCC 长帧；尾部必须 '}'，BCC 不校验 */
    if (avail >= len_lo) {
        uint8_t tail;
        rb_peek(rb, (uint32_t)(len_lo - 1U), &tail, 1, NULL);
        if (tail == '}') {
            *total_len = len_lo;
            return PROTO_PROBE_READY;
        }
    }
    if (avail >= len_hi) {
        uint8_t tail;
        rb_peek(rb, (uint32_t)(len_hi - 1U), &tail, 1, NULL);
        if (tail == '}') {
            *total_len = len_hi;
            return PROTO_PROBE_READY;
        }
        return PROTO_PROBE_FAKE;
    }
    return PROTO_PROBE_WAIT;
}

/**
 * @brief  MTC 协议帧处理任务。
 * @param  argument  任务参数，当前未使用。
 */
void sc_mtc_proto_handle_task(void *argument)
{
    (void)argument;
    static uint8_t msg_buf[SC_MTC_MSG_SIZE];
    frame_msg_t *msg = (frame_msg_t *)msg_buf;
    for (;;) {
        if (osOK != osMessageQueueGet(s_sc_mtc_queue, msg, NULL, osWaitForever)) {
            continue;
        }
        sc_mtc_parsed_cmd_t cmd = sc_mtc_parse_frame(msg->data, msg->data_len);
        sc_mtc_execute_cmd(msg->ch, &cmd);
    }
}