/**
 * @file    app_yn_ol_proto.c
 * @brief   云南治超屏协议（YN_1.3.0，`{` 帧族）— 注册 / 探测 / 处理任务
 *
 * 绑定 **四个逻辑通道**：`CH_ID_RS485` + `CH_ID_RS232` + `CH_ID_TCP_SERVER` +
 * `CH_ID_TCP_CLIENT`（2026-09-17 用户裁决：现场可能用网口控制 → 追加 TCP 双通道）；
 * 每逻辑通道独立 mask、四个 mask 共用同一静态队列（与青海/山东/贵州/云南常规同模式）。
 *   - 串口通道覆盖文档「信息显示牌（费显）通过串口与车道工控机相连」的现场形态，
 *     波特率由全局 DIP1 选择（文档 9600~115200，默认 9600 → OFF）；
 *   - TCP 双通道覆盖「网口控制」需求（复用既有 TCP Server/Client 通道，无新 LwIP 资源）。
 *   ⚠ 禁止绑定 CH_ID_RS232_1（USART6 语音 TX 专用）。
 *
 * ── RJ45 槽 probe 竞争：为什么绑 TCP 就不冲突（设计裁决 2026-09-17）────────
 *   本协议首字节 0x7B 与重庆 CQ 的 JSON 帧**同为 '{'**，而 CQ probe 对 '{' 开头数据做
 *   花括号深度扫描（`cq_probe_frame`）——它会把本协议的 `{ cmd len … }` 帧（如
 *   `7B 31 00 7D`）当作 4 字节完整 JSON 抢先 READY 认领并送 CQ 任务（JSON 解析失败后
 *   丢弃）。若两者同在一条 probe 链上必然互吞，而收录序上 CQ 更早（Makefile 中 CQ 源文件
 *   在 YunNan_Overload 之前 → 同槽时 CQ 先注册先认领）。
 *
 *   解法**不在收录序，而在通道掩码**：`app_dispatch.c` 的 `frame_dispatch_task` 按
 *   `proto = ch_proto_map[ch->ch_id]` 过滤，**只有绑定在该通道上的协议才进 probe 链**；
 *   而 CQ 只绑 `CH_ID_UDP` + `CH_ID_UDP_CQ`（`app_cq_proto.c` 第 3 步），
 *   **不绑 `CH_ID_TCP_SERVER` / `CH_ID_TCP_CLIENT`** → TCP 通道上 CQ probe
 *   根本不会被调用，本协议帧（含半帧）不可能被 CQ 抢先认领；反过来本模块
 *   **不绑 `CH_ID_UDP`（10011）** → CQ 的 UDP 行为与现状逐字节不变（零削弱）。
 *   TCP 通道上同槽的其它协议只有 LDI（首字节 0xFF 快拒），本协议独占 '{' 首字节。
 *   反向安全（CQ 帧不被本 probe 误认/卡住）：CQ JSON 第二字节恒为 '"'（0x22），
 *   不在本 probe 的命令字白名单 → 立即 FAKE 放行；CQ 二进制帧首字节 0xFF → 首字节快拒；
 *   半帧（avail<3）→ WAIT，而调度器在 WAIT 时**继续探测下一协议**、本轮无消费则等更多
 *   字节 → 不会卡住 CQ 的深度扫描。见 doc/15 §4/§7。
 *
 * ── 上电效果（**不实现**，2026-09-17 用户裁决）──────────────────────────
 *   文档「上电后显示『祝您一路平安』稍候熄灭」不实现：原 `app_yn_ol_proto_default.c`
 *   及其注册、5s 熄灭任务已删除，上电画面由 STD 现有默认显示链路给出。
 *
 * ── 0x49 屏体参数持久化（2026-09-17 用户裁决）───────────────────────────
 *   `sw_app_initcall` 初始化时 `yn_ol_screen_cfg_load()` 从 W25Qxx 独立 4KB 扇区
 *   （`capacity - 12288`）装载字体/字宽；写入路径见 `app_yn_ol_proto_cmd.c`。
 *
 * ── 帧头冲突纪律（`{` 帧族）────────────────────────────────────────────
 *   首字节 0x7B 与青海/山东/贵州常规/云南常规/四川 MTC 相同 → **必须定义
 *   `g_brace_proto_guard` 编译期互斥守卫**（`#ifndef STD_ALL_PROTO` 包裹 +
 *   `__attribute__((used))`）；量产 EIDE 必须与其他 `{` 帧族协议目录二选一
 *   （链接期 `multiple definition` 兜底强制）。**与云南常规 `app_yn_proto` 这一对
 *   已 `arm-none-eabi-ld -r` 双编实测报 `multiple definition`（doc/15 §8）**。
 *   本协议**独有**的 0x46~0x51 二进制命令字不在任何其它 `{` 帧族 probe 的命令集内
 *   （云南常规只认 '1'~'9'/'A'/'B'/0x01/0x02，MTC 上限 0x45）→ 全协议开发构建
 *   （STD_ALL_PROTO 豁免守卫）下这些帧由本 probe 独占认领；ASCII 命令字
 *   （'1'~'5'/'8'/'A'）与云南常规重叠、0x42~0x45 与 MTC 重叠（且 0x42 与云南常规
 *   的 'B' 同字节歧义），由源码收录序先注册者先认领——coexistence 与青海/贵州先例
 *   一致，量产互斥后无此问题。详见 doc/15 §4。
 */

#include "app_yn_ol_proto.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "initcall.h"

#include "app_diag.h"   /* APP_DIAG_BANNER（YN_OL_RTT_DIAG 默认跟随） */
#include "app_dispatch.h"
#include "app_yn_ol_proto_cmd.h"
#include "app_yn_ol_proto_parse.h"
#include "dev_display.h" /* 逐帧诊断打印屏体几何 */
#include "pl_task_static.h"
#include "SEGGER_RTT.h" /* 逐帧诊断输出通道（NO_BLOCK_SKIP） */

/* ---- 逐帧 RTT 诊断开关（默认跟随总开关；置 0 则整段代码被宏消除、零开销）----
 * 与 app_yn_ol_proto_cmd.c 顶部同名宏保持同一默认（默认跟随 APP_DIAG_BANNER）。 */
#ifndef YN_OL_RTT_DIAG
#define YN_OL_RTT_DIAG (APP_DIAG_BANNER)
#endif

/* ---- 任务静态存储（栈 + TCB 落 CCMRAM，见 pl_task_static.h）----
 * yn_ol_handle_task：启动期创建一次、永不退出（自检任务 yn_ol_selftest 会被
 * 反复创建/退出，**保持动态分配**，见 app_yn_ol_proto_cmd.c）。
 * 静态化后不再占 ucHeap（省 1144B），CCM 占 1124B——本轮新增的云南治超任务
 * 正是压垮 36KB 堆的最后一份（A/B 实测，见 doc/06-04 §7.3）。 */
PL_TASK_STATIC_STORAGE(yn_ol_handle, 256);

/* `{` 帧族互斥守卫：同一构建只允许编入一个 `{` 帧族协议
 * （青海/山东/贵州常规/四川MTC/云南常规/云南治超）。
 * 多个同时编入 → 链接期 multiple definition 强制报错；
 * Makefile 全协议开发构建定义 STD_ALL_PROTO 跳过守卫（共存由 probe 注册序与文档纪律约束）。 */
#ifndef STD_ALL_PROTO
__attribute__((used)) char g_brace_proto_guard;
#endif

/* 地区协议通道 RB：与青海/四川/山东/贵州/云南/安徽/贵州治超等同槽 weak 合并。
 * RJ45 槽体由 IAP/LDI/CQ 提供（本模块只 acquire 不 provide，避免第二份 1536B 网口缓冲）。 */
RB_PROVIDE_WEAK(rb_provide_rs485, RB_SIZE_RS485);
RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232);

static proto_mask_t s_yn_ol_mask;            /* CH_ID_RS485（RS485 槽 RB） */
static proto_mask_t s_yn_ol_mask_rs232;      /* CH_ID_RS232（RS232 槽 RB） */
static proto_mask_t s_yn_ol_mask_tcp_server; /* CH_ID_TCP_SERVER（RJ45 槽 RB） */
static proto_mask_t s_yn_ol_mask_tcp_client; /* CH_ID_TCP_CLIENT（RJ45 槽 RB） */
static osMessageQueueId_t s_yn_ol_queue;

/* 静态队列：payload 覆盖协议最大帧（参数区 1B 长度字段 → 帧 ≤259，与云南常规同构）。
 * 队列体 + 控制块 + 任务帧缓冲置 **CCMRAM**（doc/15 §6）：
 * PROTO=ALL 口径 SRAM 余量仅数百 B，放不下 ~1.1KB 静态体；这些缓冲为 CPU 独占
 * 访问（osMessageQueue 静态内存经 CPU memcpy，无 DMA/ETH），CCM 适用（CQ/GZ_OL 先例）。 */
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
 *
 * 注册五步 × 四通道（doc/08-01）：RS485/RS232/TCP双口 槽 acquire → 每通道独立 mask
 * register + bind → 四 mask 共用一静态队列 → 处理任务；随后装载 0x49 持久化参数。
 * @note   队列建立在 `osMessageQueueNew` 之后才 `app_proto_set_frame_queue`，故装载
 *         屏体参数放在最后（与任务/队列无耦合，仅要求 dev_w25qxx 已初始化）。
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

    /* 网口槽（TCP Server / TCP Client 共享 RJ45 RB）：独立 mask 各挂一次。
     * 槽体由 IAP/LDI/CQ weak 提供（本模块不 provide）；若极端裁剪构建下同槽无任何
     * 提供者，acquire 返回 nullptr → 静默跳过网口绑定，串口双通道不受影响。
     * **不与 CQ 竞争**：CQ 不绑 TCP 通道，probe 掩码过滤后 CQ 在本通道上不会被调用
     * （见文件头「RJ45 槽 probe 竞争」）。 */
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
 * @brief  `{` 帧族帧探测（PURE 契约：只 rb_peek，无副作用、无阻塞）。
 *
 *  ① avail == 0                       → FAKE（禁止盲 WAIT 阻塞链式探测）
 *  ② peek[0] != '{'                   → FAKE（首字节快拒）
 *  ③ avail < 3                        → WAIT（命令字 + 长度字段未到齐）
 *  ④ 命令字不在本协议实现集合内         → FAKE（'6'/'7'/'9'/'B' 及未知字节；
 *                                       云南常规/青海等由各自 probe 认领或逐字节重同步）
 *                                       **CQ 兼容路径**：CQ JSON 帧第二字节恒为 '"'（0x22）、
 *                                       CQ 二进制帧首字节 0xFF → 本 probe 均 FAKE 放行，
 *                                       不抢占、不卡住 CQ（CQ 亦不绑 TCP 通道，见文件头）
 *  ⑤ frame_len = len + 4；avail < it   → WAIT（整帧未到齐）
 *  ⑥ peek[frame_len-1] != '}'         → FAKE（尾字节定界校验）
 *  ⑦ READY，写 *total_len = frame_len
 *
 * 长度字段为**二进制字节值**（参数区长度），帧总长 = len + 4（与云南常规同构）。
 * 命令字白名单复用 parse 层 `yn_ol_cmd_from_byte()`（单一真源）。
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

    /* 命令字快筛：本协议不实现的命令字（'6' 固定格式 / '7' 语音 / '9' 音量 / 'B' 等）
     * 立即 FAKE，交同槽链式探测的其它协议或 1 字节重同步（PURE：不消费、无副作用）。 */
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

/* ================================================================
 *  逐帧 RTT 诊断（2026-09-17 YN_OL 联调轮；用户问题③）
 *
 *  现场判读链：帧有没有到（rx 行）→ 解析过没过（sta）→ 执行了没有（exec ret）
 *  → 执行了为什么看不见（'3' 的 y/屏高/OFF-SCREEN 标注）。
 *  打印纪律：单帧最多两次 SEGGER_RTT_printf（1KB 上行缓冲、NO_BLOCK_SKIP）——
 *    第 1 次：rx（通道/长度/原始 hex）+ sta + cmd + 关键字段；
 *    第 2 次：exec 结果（或解析失败的 drop 原因）。
 *  关闭（YN_OL_RTT_DIAG=0）时本段全部宏内消除，零输出零开销。
 * ================================================================ */
#if YN_OL_RTT_DIAG

/** 原始帧 hex 打印上限（超长帧打前 N 字节 + 总长；控 RTT 体量） */
#define YN_OL_DIAG_HEX_MAX (32U)

/** @brief 通道 id → 诊断短名。 */
static const char *_yn_ol_ch_name(uint8_t ch_id)
{
    switch (ch_id) {
        case CH_ID_RS485:      return "RS485";
        case CH_ID_RS232:      return "RS232";
        case CH_ID_TCP_SERVER: return "TCP_SRV";
        case CH_ID_TCP_CLIENT: return "TCP_CLI";
        default:               return "?";
    }
}

/** @brief 解析状态 → 诊断短名。 */
static const char *_yn_ol_sta_name(yn_ol_parse_sta_t sta)
{
    switch (sta) {
        case YN_OL_PARSE_OK:        return "OK";
        case YN_OL_PARSE_ERR_FRAME: return "ERR_FRAME";
        case YN_OL_PARSE_ERR_CMD:   return "ERR_CMD";
        default:                    return "ERR_PARAM";
    }
}

/**
 * @brief  原始帧 hex dump（≤YN_OL_DIAG_HEX_MAX 全打；超长打前 N 字节）。
 * @return true = 已截断（调用方标注 cut）。
 */
static bool _yn_ol_hex_dump(const uint8_t *d, uint16_t len, char *out, uint16_t out_sz)
{
    static const char digs[] = "0123456789abcdef";
    const uint16_t n = (len > YN_OL_DIAG_HEX_MAX) ? (uint16_t)YN_OL_DIAG_HEX_MAX : len;
    uint16_t w = 0U;
    /* 每字节 3 字符（2 hex + 1 空格），缓冲 = N*3 + 1（NUL）；条件用 <= 保证 N 字节全打 */
    for (uint16_t i = 0U; i < n && (w + 4U) <= out_sz; i++) {
        out[w++] = digs[d[i] >> 4];
        out[w++] = digs[d[i] & 0x0FU];
        out[w++] = ' ';
    }
    if (w > 0U)
        w--; /* 去掉尾空格 */
    out[w] = '\0';
    return (len > YN_OL_DIAG_HEX_MAX);
}

/** @brief 第 1 行：来源通道 + 原始长度 + hex + 解析状态 + 命令字 + 关键字段。 */
static void _yn_ol_diag_frame(const frame_msg_t *msg, const yn_ol_parsed_cmd_t *cmd)
{
    char hex[YN_OL_DIAG_HEX_MAX * 3U + 1U];
    char field[112];
    const uint8_t ch_id    = (msg->ch != nullptr) ? msg->ch->ch_id : 0xFFU;
    const uint16_t raw_len = (uint16_t)msg->data_len;
    const bool cut         = _yn_ol_hex_dump(msg->data, raw_len, hex, (uint16_t)sizeof(hex));
    const uint8_t cmd_byte = (raw_len >= 2U) ? msg->data[1] : 0U;

    field[0] = '\0';
    if (cmd->sta == YN_OL_PARSE_OK) {
        switch (cmd->cmd) {
            case YN_OL_PCMD_ONE_LINE:
                snprintf(field, sizeof(field), " color=%u(%s) row=%u(%s) text_len=%u",
                         (unsigned)cmd->p.one_line.color, (cmd->data[0] <= 0x02U) ? "bin" : "ascii",
                         (unsigned)cmd->p.one_line.row, (cmd->data[1] <= 0x05U) ? "bin" : "ascii",
                         (unsigned)cmd->p.one_line.text_len);
                break;
            case YN_OL_PCMD_FULL_SCREEN:
                snprintf(field, sizeof(field), " color=%u(%s) x=%u y=%u text_len=%u",
                         (unsigned)cmd->p.full_screen.color,
                         (cmd->data[0] <= 0x02U) ? "bin" : "ascii",
                         (unsigned)cmd->p.full_screen.x, (unsigned)cmd->p.full_screen.y,
                         (unsigned)cmd->p.full_screen.text_len);
                break;
            case YN_OL_PCMD_BRIGHTNESS:
                snprintf(field, sizeof(field), " level=%u(%s)", (unsigned)cmd->p.brightness,
                         (cmd->p.brightness == 0U) ? "auto" : "manual");
                break;
            case YN_OL_PCMD_PERIPHERAL:
                snprintf(field, sizeof(field), " ctrl=0x%02x", (unsigned)cmd->p.peripheral);
                break;
            case YN_OL_PCMD_FILL_ALL:
                snprintf(field, sizeof(field), " fill_color=%u", (unsigned)cmd->p.fill_color);
                break;
            default:
                break; /* 0x47/0x48/0x49 在 cmd 侧已有专项打印，此处只报 rx+sta+cmd */
        }
    }

    SEGGER_RTT_printf(0, "[yn_ol] rx ch=%s(%u) len=%u%s hex=%s%s -> sta=%s cmd=0x%02x%s\n",
                      _yn_ol_ch_name(ch_id), (unsigned)ch_id, (unsigned)raw_len, cut ? " cut" : "",
                      hex, cut ? "..." : "", _yn_ol_sta_name(cmd->sta), (unsigned)cmd_byte, field);
}

/**
 * @brief 第 2 行（解析失败）：拒绝原因 + 判读线索（含原始 hex 已在第 1 行）。
 */
static void _yn_ol_diag_drop(const uint8_t *raw, uint16_t raw_len, const yn_ol_parsed_cmd_t *cmd)
{
    switch (cmd->sta) {
        case YN_OL_PARSE_ERR_FRAME: {
            const unsigned first = (raw_len >= 1U) ? raw[0] : 0U;
            const unsigned tail  = (raw_len >= 1U) ? raw[raw_len - 1U] : 0U;
            const unsigned decl  = (raw_len >= 3U) ? raw[2] : 0U;
            SEGGER_RTT_printf(0,
                              "[yn_ol] drop sta=ERR_FRAME len=%u first=0x%02x tail=0x%02x "
                              "declared=%u actual=%u (head/tail/length mismatch)\n",
                              (unsigned)raw_len, first, tail, decl,
                              (unsigned)((raw_len >= 4U) ? raw_len - 4U : 0U));
            break;
        }
        case YN_OL_PARSE_ERR_CMD:
            SEGGER_RTT_printf(0,
                              "[yn_ol] drop sta=ERR_CMD cmd=0x%02x (unsupported / not implemented)\n",
                              (unsigned)((raw_len >= 2U) ? raw[1] : 0U));
            break;
        default: { /* ERR_PARAM：参数长度/取值非法——打印前两个关键字节 */
            const unsigned b0 = (raw_len >= 4U) ? raw[3] : 0U;
            const unsigned b1 = (raw_len >= 5U) ? raw[4] : 0U;
            SEGGER_RTT_printf(0,
                              "[yn_ol] drop sta=ERR_PARAM cmd=0x%02x declared=%u b0=0x%02x b1=0x%02x "
                              "(keyfield/length rejected)\n",
                              (unsigned)((raw_len >= 2U) ? raw[1] : 0U),
                              (unsigned)((raw_len >= 3U) ? raw[2] : 0U), b0, b1);
            break;
        }
    }
}

/**
 * @brief 第 2 行（执行结果）：ret + 关键字段；'3' 额外算「行号 × 字高 = y」并给落屏判读。
 */
static void _yn_ol_diag_exec(const yn_ol_parsed_cmd_t *cmd, int ret)
{
    switch (cmd->cmd) {
        case YN_OL_PCMD_ONE_LINE: {
            /* 现场最常见误解：「发了单行但屏幕没变化」= 行号越出可视区（执行但不落屏）。
             * 例：16×32 屏（W16/H32）+ FONT_16 → 只有行 1/2 可视，行 3~5 落在屏外。 */
            const dev_display_t *d = dev_display_get();
            const uint16_t rowh  = (uint16_t)yn_ol_current_font_size();
            const uint16_t y     = (uint16_t)((uint16_t)cmd->p.one_line.row * rowh);
            const uint16_t scr_h = (d != nullptr) ? d->screen_cols : 0U;
            const char *vis      = (d == nullptr)  ? "NO-DISPLAY"
                                   : (y >= scr_h)  ? "OFF-SCREEN(executed but invisible)"
                                   : ((uint16_t)(y + rowh) > scr_h)
                                       ? "PARTIAL(bottom clipped, glyphs cut)"
                                       : "ON-SCREEN";
            SEGGER_RTT_printf(0,
                              "[yn_ol] exec cmd=0x33 ret=%d color=%u row=%u text_len=%u y=%u "
                              "screen_w=%u screen_h=%u rowh=%u -> %s\n",
                              ret, (unsigned)cmd->p.one_line.color, (unsigned)cmd->p.one_line.row,
                              (unsigned)cmd->p.one_line.text_len, (unsigned)y,
                              (unsigned)((d != nullptr) ? d->screen_rows : 0U), (unsigned)scr_h,
                              (unsigned)rowh, vis);
            break;
        }
        case YN_OL_PCMD_FULL_SCREEN: {
            const dev_display_t *d = dev_display_get();
            SEGGER_RTT_printf(0,
                              "[yn_ol] exec cmd=0x34 ret=%d color=%u x=%u y=%u text_len=%u "
                              "screen_w=%u screen_h=%u\n",
                              ret, (unsigned)cmd->p.full_screen.color,
                              (unsigned)cmd->p.full_screen.x, (unsigned)cmd->p.full_screen.y,
                              (unsigned)cmd->p.full_screen.text_len,
                              (unsigned)((d != nullptr) ? d->screen_rows : 0U),
                              (unsigned)((d != nullptr) ? d->screen_cols : 0U));
            break;
        }
        default:
            SEGGER_RTT_printf(0, "[yn_ol] exec cmd=0x%02x ret=%d\n", (unsigned)cmd->cmd, ret);
            break;
    }
}

#endif /* YN_OL_RTT_DIAG */

/**
 * @brief  帧处理任务：帧队列 → 解析 → 执行（+ 逐帧 RTT 诊断）。
 * @param  argument  未使用。
 *
 * 诊断（YN_OL_RTT_DIAG，默认跟随 APP_DIAG_BANNER）打印链：
 *   第 1 行 `[yn_ol] rx ch=… len=… hex=… -> sta=… cmd=… <关键字段>`（每帧一次）；
 *   第 2 行 `[yn_ol] exec cmd=… ret=… …`（执行结果）/ `[yn_ol] drop …`（解析失败原因）。
 * 单帧最多两次 SEGGER_RTT_printf（1KB 上行缓冲、NO_BLOCK_SKIP，避免挤掉 [diag]）。
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

#if YN_OL_RTT_DIAG
        _yn_ol_diag_frame(msg, &cmd); /* 第 1 行：rx + 原始 hex + 解析状态 + 关键字段 */
#endif

        int exec_ret = (int)YN_OL_EXEC_NOT_RUN;
        if (cmd.sta == YN_OL_PARSE_OK)
            exec_ret = yn_ol_execute_cmd(msg->ch, &cmd);

#if YN_OL_RTT_DIAG
        if (cmd.sta == YN_OL_PARSE_OK)
            _yn_ol_diag_exec(&cmd, exec_ret); /* 第 2 行：执行结果（'3' 含落屏判读） */
        else
            _yn_ol_diag_drop(msg->data, (uint16_t)msg->data_len, &cmd); /* 第 2 行：拒绝原因 */
#endif
        (void)exec_ret;
    }
}
