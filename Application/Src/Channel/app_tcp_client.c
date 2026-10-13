/**
 * @file    app_tcp_client.c
 * @brief       TCP 客户端通道（远端未配置则空转，不发起 connect）
 */

#include "app_tcp_client.h"
#include "app_dispatch.h"
#include "pl_net_adapt.h"

/* ---- 信号量 ---- */
osSemaphoreId_t client_disconnect_sem;

/* ---- 前向声明 ---- */
void tcp_client_conn_task(void *argument);
__STATIC_INLINE void tcp_keepaliveinit(struct netconn *conn);

/* ---- 连接任务属性 ---- */
static const osThreadAttr_t tcp_client_conn_attr = {
    .name       = "tcp_client_conn_task",
    .stack_size = 256 * 4, /* 对齐 udp_connect；原 2KB 偏大 */
    .priority   = osPriorityNormal,
};

osThreadId_t tcp_client_task_handle;
const osThreadAttr_t tcp_client_task_attr = {
    .name       = "tcp_client_task",
    .stack_size = 256 * 4, /* 对齐 tcp_server；原 2KB 偏大 */
    .priority   = osPriorityNormal,
};

/* ---- 通道元数据模板（每连接 copy） ---- */
channel_t g_tcp_client_channel_tmpl = {
    .ch_id = CH_ID_TCP_CLIENT,
    .ops   = &tcp_ch_ops,
};

/* ---- 通道实例（单例，持远端配置） ----
 * 哨兵：IP 全 0 或 port==0 视为未配置，主循环空转、不 connect。 */
tcp_client_channel_t g_tcp_client = {
    .host_ip   = {0, 0, 0, 0},
    .host_port = 0,
};

/** @brief 远端已配置：IP 非全 0 且 port != 0。 */
static bool _tcp_cli_remote_configured(void)
{
    const uint8_t *ip = g_tcp_client.host_ip;
    return ((ip[0] | ip[1] | ip[2] | ip[3]) != 0U) && (g_tcp_client.host_port != 0U);
}

/* ---- 连接期通道实例：文件级 static 单实例（UAF 根治） ----
 * 通道实例必须为文件级 static（放栈上会随连接任务退出悬垂），
 * ch 指针生命周期恒定；重连时复用同一实例（init 重新拷贝模板重置状态）。
 * 已注销的旧通知由 dispatch 侧 app_channel_get 回验丢弃（app_dispatch.c）。 */
static tcp_client_channel_t s_tcp_client_conn_ch;

/* ---- 构造 ---- */
void tcp_client_channel_init(tcp_client_channel_t *self, void *conn, channel_t *tmpl)
{
    self->me       = *tmpl;
    self->me.state = CH_STATE_UP;
    self->conn     = conn;
    tcp_keepaliveinit(conn);
    app_channel_register(CH_ID_TCP_CLIENT, &self->me);
}

/* ---- 配置接口 ---- */
__attribute__((used)) void app_tcp_client_set_remote(const uint8_t ip[4], uint16_t port)
{
    memcpy(g_tcp_client.host_ip, ip, 4);
    g_tcp_client.host_port = port;
    if (client_disconnect_sem != nullptr)
        osSemaphoreRelease(client_disconnect_sem);
}

uint8_t *app_tcp_client_get_host_ip(void)
{
    return g_tcp_client.host_ip;
}

uint16_t app_tcp_client_get_host_port(void)
{
    return g_tcp_client.host_port;
}

/* ================================================================
 *  主任务：未配置则空转；已配置则 connect → 派生连接任务 → 重连
 * ================================================================ */

void tcp_client_task(void *argument)
{
    (void)argument;
    if (client_disconnect_sem == NULL)
        client_disconnect_sem = osSemaphoreNew(1, 0, NULL);

    for (;;) {
        /* 未配置就不连：不 new / 不 connect，避免硬编码默认地址刷屏与 ARP */
        if (!_tcp_cli_remote_configured()) {
            osDelay(1000);
            continue;
        }

        struct netconn *conn = netconn_new(NETCONN_TCP);
        if (conn == NULL) {
            osDelay(1000);
            continue;
        }

        ip_addr_t server_addr;
        IP4_ADDR(&server_addr, g_tcp_client.host_ip[0], g_tcp_client.host_ip[1],
                 g_tcp_client.host_ip[2], g_tcp_client.host_ip[3]);

        /* 非阻塞 connect：避免 netconn_connect 在无服务器时永久阻塞 */
        netconn_set_nonblocking(conn, 1);
        err_t err = netconn_connect(conn, &server_addr, g_tcp_client.host_port);

        if (err == ERR_OK || err == ERR_INPROGRESS) {
            /* 轮询等待连接完成（4000 tick，约 4 秒） */
            uint32_t deadline = osKernelGetTickCount() + 4000;
            bool connected    = false;

            while ((int32_t)(osKernelGetTickCount() - deadline) < 0) {
                /* 等待期间被改回未配置：停连，不把旧地址连完 */
                if (!_tcp_cli_remote_configured())
                    break;
                if (conn->state != NETCONN_CONNECT) {
                    connected = (conn->pcb.tcp != NULL);
                    break;
                }
                osDelay(500);
            }

            /* 连上后若远端已清成哨兵：拆掉，勿 drain sem 后继续挂旧地址 */
            if (connected && _tcp_cli_remote_configured()) {
                netconn_set_nonblocking(conn, 0); /* 连接已建立，恢复阻塞模式供 recv 使用 */


                while (osSemaphoreAcquire(client_disconnect_sem, 0) == osOK);

                /* 连接任务创建失败（堆耗尽）由 pl_task_create_checked 判空；conn 照常关闭 */
                osThreadId_t tid = pl_task_create_checked(
                    osThreadNew(tcp_client_conn_task, conn, &tcp_client_conn_attr),
                    "tcp_client_conn_task");
                if (tid != NULL)
                    osSemaphoreAcquire(client_disconnect_sem, osWaitForever);

                netconn_close(conn);
                netconn_delete(conn);
                osDelay(1000);
                continue;
            }

            if (connected) {
                netconn_close(conn);
                netconn_delete(conn);
                continue; /* 下一轮走 idle */
            }
        }

        /* 等待中被清远端：删 conn，不打 FAIL，下一轮 idle */
        if (!_tcp_cli_remote_configured()) {
            netconn_delete(conn);
            continue;
        }

        /* 连接失败或超时：删除 netconn 后重试 */
        netconn_delete(conn);
        osDelay(1000);
    }
}

void tcp_client_conn_task(void *argument)
{
    struct netconn *conn = (struct netconn *)argument;

    tcp_client_channel_init(&s_tcp_client_conn_ch, conn, &g_tcp_client_channel_tmpl);

    channel_t *ch = &s_tcp_client_conn_ch.me;
    struct netbuf *buf;
    err_t err;
    void *data;
    uint16_t len;

    while ((err = netconn_recv(conn, &buf)) == ERR_OK) {
        do {
            netbuf_data(buf, &data, &len);
            /* 长度判据 = `len > 0`（同 app_tcp_server.c）：
             * 按 `len > 1` 丢弃 1 字节分片 → RB 残帧永不可消费。 */
            if (len > 0) {
                app_channel_dispatch(ch, (uint8_t *)data, len);
            }
        } while (netbuf_next(buf) >= 0);
        netbuf_delete(buf);
    }

    s_tcp_client_conn_ch.me.ops   = nullptr; /* 防止 send 路径访问即将释放的 netconn */
    s_tcp_client_conn_ch.me.state = CH_STATE_DOWN;
    app_channel_register(CH_ID_TCP_CLIENT, nullptr);
    osSemaphoreRelease(client_disconnect_sem);
    osThreadExit();
}

__STATIC_INLINE void tcp_keepaliveinit(struct netconn *conn)
{
    if (conn == NULL || conn->pcb.tcp == NULL) return;
    ip_set_option(conn->pcb.tcp, SOF_KEEPALIVE);
    conn->pcb.tcp->keep_idle  = 10000;
    conn->pcb.tcp->keep_intvl = 2000;
    conn->pcb.tcp->keep_cnt   = 3;
}
