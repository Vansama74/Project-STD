/**
 * @file    app_tcp_server.c
 * @brief   TCP 服务器通道 — 串行单客户端服务循环
 *
 * 单连接任务循环：bind → listen → accept → 服务该客户端（recv 循环）
 * → 客户端断开 → 回 accept 下一客户端。
 *
 * UAF 根治：通道实例为文件级 static 单实例，生命周期内注册一次、
 * 注销仅在服务循环退出时；ch 指针恒定，dispatch 侧 app_channel_get
 * 回验兜底。原「每客户端一 conn 任务」的并发派生模式已移除。
 */

#include "app_tcp_server.h"

#include "FreeRTOS.h"
#include "app_diag.h" /* TCP_SRV_RTT_DIAG 默认跟随 APP_DIAG_BANNER */
#include "app_dispatch.h"
#include "pl_net_adapt.h"
#include "SEGGER_RTT.h" /* 门控 RTT 诊断（NO_BLOCK_SKIP，不阻塞服务循环） */

#define TCP_SERVER_PORT 9528

/* ================================================================
 *  通道级 RTT 诊断（盲区 A，2026-09-17 YN_OL 联调第二轮）
 *
 *  背景：本任务原先对 listen/accept/recv/close 全无输出 ⇒ 现场无法判断
 *  「字节到底有没有到板子、到在哪条连接上、来了几段」。开启后在四个生命周期点
 *  各打一行（单行 ≤ 96B；RTT 上行 1KB + NO_BLOCK_SKIP，满则整条丢，禁止刷屏）：
 *    [tcp_srv] listen port=9528            （监听就绪 / 绑定失败原因）
 *    [tcp_srv] accept 192.168.x.x:port     （新客户端接入）
 *    [tcp_srv] recv ch=tcp_server len=N head=<≤16B hex[ ..cut]>
 *    [tcp_srv] close  192.168.x.x:port reason=err （断开/理由）
 *  置 0（或 `make APP_DIAG=0`）：整段宏内消除，零输出零开销。
 * ================================================================ */
#ifndef TCP_SRV_RTT_DIAG
#define TCP_SRV_RTT_DIAG (APP_DIAG_BANNER)
#endif

#if TCP_SRV_RTT_DIAG
/** recv 日志头部 hex 上限（超长打前 N 字节 + cut；控 RTT 体量） */
#define TCP_SRV_LOG_HEX_MAX (16U)

/** @brief 取对端地址快照（失败回 0.0.0.0:0）。 */
static void _tcp_srv_peer(struct netconn *conn, uint8_t ip[4], uint16_t *port)
{
    ip_addr_t peer;
    u16_t p = 0U;
    ip[0] = ip[1] = ip[2] = ip[3] = 0U;
    *port = 0U;
    if (netconn_peer(conn, &peer, &p) == ERR_OK) {
        ip[0] = (uint8_t)ip4_addr1_16(ip_2_ip4(&peer));
        ip[1] = (uint8_t)ip4_addr2_16(ip_2_ip4(&peer));
        ip[2] = (uint8_t)ip4_addr3_16(ip_2_ip4(&peer));
        ip[3] = (uint8_t)ip4_addr4_16(ip_2_ip4(&peer));
        *port = (uint16_t)p;
    }
}

/** @brief recv 单行：len + 头部 hex（≤16B；超长标注 cut）。 */
static void _tcp_srv_log_recv(const uint8_t *data, uint16_t len)
{
    static const char digs[] = "0123456789abcdef";
    char hex[TCP_SRV_LOG_HEX_MAX * 3U + 1U];
    const uint16_t n = (len > TCP_SRV_LOG_HEX_MAX) ? (uint16_t)TCP_SRV_LOG_HEX_MAX : len;
    uint16_t w = 0U;

    for (uint16_t i = 0U; i < n; i++) {
        hex[w++] = digs[data[i] >> 4];
        hex[w++] = digs[data[i] & 0x0FU];
        hex[w++] = ' ';
    }
    if (w > 0U)
        w--; /* 去尾空格 */
    hex[w] = '\0';

    SEGGER_RTT_printf(0, "[tcp_srv] recv ch=tcp_server len=%u head=%s%s\n", (unsigned)len, hex,
                      (len > n) ? " ..cut" : "");
}
#endif /* TCP_SRV_RTT_DIAG */

/* ---- TCP keepalive（accepted 连接；参数对齐 app_tcp_client.c 既有口径）----
 * 由来（2026-09-18 现场问题「连上没反应、必须重启板子」根因修复）：
 * 本通道是「accept → 阻塞 netconn_recv 服务单一客户端 → 断开 → 回 accept」的
 * 串行单客户端循环，而原先 **accepted conn 不设 keepalive**（全工程唯一的
 * keepalive 在 TCP Client 侧）。于是对端只要「不发 FIN 就消失」（拔网线 /
 * 交换机瞬断 / 上位机进程被杀 / 上位机只关 UI 不关 socket），该连接在本板永远
 * ESTABLISHED、`netconn_recv` 永远阻塞 → **服务循环被永久占死**：其后所有新连接
 * 由 LwIP 内核完成三次握手（上位机显示「连接成功」）却永远等不到应用层 accept /
 * recv → 发数据零反应，只能重启板子清空 TCP 栈。
 * 加 keepalive 后由 LwIP 慢定时器（tcp_slowtmr，500ms 周期）探测空闲连接：
 *   对端仍在 → 回 ACK，连接保持（无副作用，不影响正常空闲的长连接）；
 *   对端已消失 → keep_cnt 次探测无响应后 abort → netconn_recv 返回非 ERR_OK
 *   → 循环回 accept，死连接自愈（≈ keep_idle + keep_cnt×keep_intvl = 16s）。
 * 参数与 app_tcp_client.c:246-253 完全一致（10s / 2s / 3 次）。 */
__STATIC_INLINE void tcp_server_keepaliveinit(struct netconn *conn)
{
    if (conn == NULL || conn->pcb.tcp == NULL)
        return;
    ip_set_option(conn->pcb.tcp, SOF_KEEPALIVE);
    conn->pcb.tcp->keep_idle  = 10000;
    conn->pcb.tcp->keep_intvl = 2000;
    conn->pcb.tcp->keep_cnt   = 3;
}

/* ---- TCP 通道虚表：send = netconn_write ---- */
static int32_t tcp_send(channel_t *ch, const uint8_t *data, uint16_t len)
{
    tcp_server_channel_t *tcp = container_of(ch, tcp_server_channel_t, me);
    err_t err                 = netconn_write((struct netconn *)tcp->conn, data, len, NETCONN_COPY);
    return (err == ERR_OK) ? (int32_t)len : -1;
}

const ch_ops_t tcp_ch_ops = {.send = tcp_send};

/* ---- 通道元数据模板 ---- */
channel_t g_tcp_server_channel_tmpl = {
    .ch_id = CH_ID_TCP_SERVER,
    .ops   = &tcp_ch_ops,
};

/* ---- 文件级 static 单实例通道（UAF 根治）：
 * 每客户端不再新建实例/任务，ch 指针生命周期恒定；
 * 已注销的旧通知由 dispatch 侧 app_channel_get 回验丢弃。 */
static tcp_server_channel_t s_tcp_ch;

/** netconn 池耗尽告警去重标记（成功建 conn 即复位，见 tcp_server_task） */
static bool s_netconn_pool_warned;

/* ---- 配置接口 ---- */
static uint16_t g_port = TCP_SERVER_PORT;

void app_tcp_server_set_port(uint16_t port)
{ g_port = port; }
uint16_t app_tcp_server_get_port(void)
{ return g_port; }

osThreadId_t tcp_server_task_handle;
const osThreadAttr_t tcp_server_task_attr = {
    .name       = "tcp_server_task",
    .stack_size = 256 * 4,
    .priority   = osPriorityNormal,
};

/* ---- 调试变量 ---- */
volatile int g_tcp_server_connected;

/* ---- 通道生命周期 ---- */
static void tcp_channel_init(tcp_server_channel_t *self, void *conn, channel_t *tmpl)
{
    self->me       = *tmpl;
    self->me.state = CH_STATE_UP;
    self->conn     = conn;
    app_channel_register(CH_ID_TCP_SERVER, &self->me);
}

static void tcp_channel_deinit(tcp_server_channel_t *self)
{
    self->me.ops   = nullptr; /* 防止 send 路径访问即将释放的 netconn */
    self->me.state = CH_STATE_DOWN;
    app_channel_register(CH_ID_TCP_SERVER, nullptr);
}

/* ================================================================
 *  服务任务: bind → listen → accept → 服务客户端 → 断开 → 回 accept
 * ================================================================ */

void tcp_server_task(void *argument)
{
    (void)argument;

    for (;;) {
        struct netconn *conn = netconn_new(NETCONN_TCP);
        if (conn == NULL) {
            /* 池耗尽（MEMP_NUM_NETCONN=8）→ 原实现是完全静默的 500ms 重试，
             * 现场「新连接无人应答」时无法区分「netconn 池耗尽」与「服务循环
             * 被旧连接占死」。此处补一行告警（同一失败连续段只打一行，成功后复位
             * 去重标记，下次失败仍可见）：
             *   · 池耗尽 ⇒ 上位机表现为**连接超时/失败**（listener 建不起来）；
             *   · 循环占死 ⇒ 上位机表现为**连接成功但零应答**（握手由内核完成）。
             * 两者据此一行即可分辨。控制流不动（仍 500ms 重试）。 */
            if (!s_netconn_pool_warned) {
                s_netconn_pool_warned = true;
                SEGGER_RTT_printf(
                    0, "[tcp_srv] netconn_new NULL (netconn pool exhausted; retry 500ms)\n");
            }
            osDelay(500);
            continue;
        }
        s_netconn_pool_warned = false; /* 成功即复位去重标记 */

        err_t err = netconn_bind(conn, IP_ADDR_ANY, g_port);
        if (err != ERR_OK) {
#if TCP_SRV_RTT_DIAG
            SEGGER_RTT_printf(0, "[tcp_srv] bind FAIL port=%u err=%d (retry in 500ms)\n",
                              (unsigned)g_port, (int)err);
#endif
            netconn_delete(conn);
            osDelay(500);
            continue;
        }
        netconn_listen(conn);
#if TCP_SRV_RTT_DIAG
        SEGGER_RTT_printf(0, "[tcp_srv] listen port=%u (accept loop)\n", (unsigned)g_port);
#endif

        /* 串行单客户端服务循环：同一 listener 反复 accept */
        for (;;) {
            struct netconn *newconn = NULL;
            err                     = netconn_accept(conn, &newconn);
            if (err != ERR_OK || newconn == NULL) {
#if TCP_SRV_RTT_DIAG
                SEGGER_RTT_printf(0, "[tcp_srv] accept FAIL err=%d (rebuild listener in 500ms)\n",
                                  (int)err);
#endif
                /* 监听异常 → 重建 listener */
                osDelay(500);
                break;
            }

            /* keepalive：半开/孤儿连接自愈（16s 内探测到对端消失 → 回 accept）。
             * 这是「服务循环被不可达客户端永久占死 → 需重启板子」的修复点，
             * 详见文件头 tcp_server_keepaliveinit 的成因说明。 */
            tcp_server_keepaliveinit(newconn);

            /* 服务该客户端直至断开 */
            tcp_channel_init(&s_tcp_ch, newconn, &g_tcp_server_channel_tmpl);
            g_tcp_server_connected = 1;
#if TCP_SRV_RTT_DIAG
            {
                uint8_t ip[4];
                uint16_t port = 0U;
                _tcp_srv_peer(newconn, ip, &port);
                SEGGER_RTT_printf(0, "[tcp_srv] accept %u.%u.%u.%u:%u\n", ip[0], ip[1], ip[2],
                                  ip[3], (unsigned)port);
            }
#endif

            channel_t *ch = &s_tcp_ch.me;
            struct netbuf *buf;
            void *data;
            uint16_t len;
            err_t recv_err;

            while ((recv_err = netconn_recv(newconn, &buf)) == ERR_OK) {
                do {
                    netbuf_data(buf, &data, &len);
                    /* 长度判据 = `len > 0`。**修复（2026-09-17 YN_OL 联调轮）**：
                     * 原 `len > 1` 会静默丢弃长度为 1 的 TCP 分片——上位机帧若被
                     * TCP 栈恰好切成 1 字节片（如 `7B` 与其余字节分开到达），该字节
                     * 永久丢失 → 调度层 RB 头部留下永远凑不齐的残帧（帧头残缺/尾部
                     * 错位），现场表现为「首次发命令无反应、重连后只执行一次、
                     * 此后不再受控」。UDP 路径本就以 `len > 0` 入调度（app_udp.c），
                     * 此处对齐；单字节分片交给 probe 链按 1 字节重同步语义处理。 */
                    if (len > 0) {
#if TCP_SRV_RTT_DIAG
                        _tcp_srv_log_recv((const uint8_t *)data, len);
#endif
                        app_channel_dispatch(ch, (uint8_t *)data, len);
                    }
                } while (netbuf_next(buf) >= 0);
                netbuf_delete(buf);
            }
            (void)recv_err;

#if TCP_SRV_RTT_DIAG
            {
                uint8_t ip[4];
                uint16_t port = 0U;
                _tcp_srv_peer(newconn, ip, &port);
                SEGGER_RTT_printf(0, "[tcp_srv] close  %u.%u.%u.%u:%u reason=%d\n", ip[0], ip[1],
                                  ip[2], ip[3], (unsigned)port, (int)recv_err);
            }
#endif

            g_tcp_server_connected = 0;
            tcp_channel_deinit(&s_tcp_ch);
            netconn_close(newconn);
            netconn_delete(newconn);
            /* 回 accept 下一客户端 */
        }

        netconn_delete(conn);
    }
}