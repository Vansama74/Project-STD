/**
 * @file    app_tcp_server.c
 * @brief   TCP 服务器通道 — 串行单客户端服务循环
 *
 * 单连接任务循环：bind → listen → accept → 服务该客户端（recv 循环）
 * → 客户端断开 → 回 accept 下一客户端。
 *
 * UAF 根治：通道实例为文件级 static 单实例，生命周期内注册一次、
 * 注销仅在服务循环退出时；ch 指针恒定，dispatch 侧 app_channel_get
 * 回验兜底；不得退化为「每客户端一 conn 任务」的并发派生模式。
 */

#include "app_tcp_server.h"

#include "FreeRTOS.h"
#include "app_dispatch.h"
#include "pl_net_adapt.h"

#define TCP_SERVER_PORT 9528

/* ---- TCP keepalive（accepted 连接；参数对齐 app_tcp_client.c 口径）----
 * 必需性：本通道是「accept → 阻塞 netconn_recv 服务单一客户端 → 断开 → 回 accept」
 * 的串行单客户端循环，对端「不发 FIN 就消失」（拔网线 / 交换机瞬断 / 进程被杀）
 * 时若无 keepalive，该连接在本板永远 ESTABLISHED、netconn_recv 永远阻塞 →
 * **服务循环被永久占死**：其后新连接由 LwIP 内核完成握手却永远等不到应用层
 * accept/recv → 发数据零反应。
 * 加 keepalive 后由 LwIP 慢定时器（tcp_slowtmr，500ms 周期）探测空闲连接：
 *   对端仍在 → 回 ACK，连接保持；对端已消失 → keep_cnt 次探测无响应后 abort →
 *   netconn_recv 返回非 ERR_OK → 循环回 accept，死连接自愈（≈16s）。
 * 参数与 app_tcp_client.c 一致（10s / 2s / 3 次）。 */
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
            /* 池耗尽（MEMP_NUM_NETCONN=8）→ 500ms 后重试（控制流不变）：
             * 表现为上位机连接超时/失败（listener 建不起来）。 */
            osDelay(500);
            continue;
        }
        err_t err = netconn_bind(conn, IP_ADDR_ANY, g_port);
        if (err != ERR_OK) {
            netconn_delete(conn);
            osDelay(500);
            continue;
        }
        netconn_listen(conn);

        /* 串行单客户端服务循环：同一 listener 反复 accept */
        for (;;) {
            struct netconn *newconn = NULL;
            err                     = netconn_accept(conn, &newconn);
            if (err != ERR_OK || newconn == NULL) {
                /* 监听异常 → 重建 listener */
                osDelay(500);
                break;
            }

            /* keepalive：半开/孤儿连接自愈（16s 内探测到对端消失 → 回 accept），
             * 防止服务循环被不可达客户端永久占死（成因见文件头）。 */
            tcp_server_keepaliveinit(newconn);

            /* 服务该客户端直至断开 */
            tcp_channel_init(&s_tcp_ch, newconn, &g_tcp_server_channel_tmpl);

            channel_t *ch = &s_tcp_ch.me;
            struct netbuf *buf;
            void *data;
            uint16_t len;
            err_t recv_err;

            while ((recv_err = netconn_recv(newconn, &buf)) == ERR_OK) {
                do {
                    netbuf_data(buf, &data, &len);
                    /* 长度判据 = `len > 0`：TCP 单字节分片（如 `7B` 与其余字节
                     * 分开到达）必须入调度——按 `len > 1` 丢弃会在 RB 头部留下
                     * 永远凑不齐的残帧（帧头残缺/尾部错位）；单字节分片交给 probe
                     * 链按 1 字节重同步语义处理（与 UDP 路径口径一致）。 */
                    if (len > 0) {
                        app_channel_dispatch(ch, (uint8_t *)data, len);
                    }
                } while (netbuf_next(buf) >= 0);
                netbuf_delete(buf);
            }
            (void)recv_err;

            tcp_channel_deinit(&s_tcp_ch);
            netconn_close(newconn);
            netconn_delete(newconn);
            /* 回 accept 下一客户端 */
        }

        netconn_delete(conn);
    }
}