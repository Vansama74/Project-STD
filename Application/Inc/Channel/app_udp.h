/**
 * @file    app_udp.h
 * @brief   UDP 广播接收通道
 *
 * 监听端口固定 10011（宏 `LDI_DISCOVERY_PORT`，定义于 app_ldi.h），同口承载：
 *   - 创迪发现口（LDI 21H/12H）
 *   - IAP 固件升级（0x5A5A5A5A）
 * 端口固定，禁止修改接口。
 */

#pragma once

#include <stdint.h>

#include "cmsis_os2.h"
#include "app_dispatch.h"
#include "pl_task_guard.h"

/** @brief UDP 通道子类（文件级 static 单实例，UAF 根治：
 *  连接任务重入复用同一实例，ch 指针恒定；dispatch 侧回验兜底） */
typedef struct {
    channel_t me;
    void *conn; /**< 不透明句柄（中间件 netconn），在 .c 中 cast 回具体类型 */
    uint16_t listen_port;
    uint8_t src_ip[4]; /**< 源 IP 地址（IPv4 字节数组） */
    uint16_t src_port;
} udp_channel_t;

extern const ch_ops_t udp_ch_ops;
extern channel_t g_udp_channel_tmpl;

extern osThreadId_t udp_task_handle;
extern const osThreadAttr_t udp_task_attr;

void udp_task(void *argument);

static inline osThreadId_t app_udp_start(void)
{
    return pl_task_create_checked(osThreadNew(udp_task, NULL, &udp_task_attr), "udp_task");
}

uint16_t app_udp_get_port(void);
void app_udp_broadcast(const uint8_t *data, uint16_t len);

/* ---- CQ 业务口 UDP 通道（CH_ID_UDP_CQ；PROTO_CHONGQING 读 Sector1 net_cfg.udp_port
       默认 20103，dev 共存构建固定 20103，见 app_udp.c _udp_cq_read_port）---- */

void udp_cq_task(void *argument);
extern const osThreadAttr_t udp_cq_task_attr;

static inline osThreadId_t app_udp_cq_start(void)
{
    return pl_task_create_checked(osThreadNew(udp_cq_task, NULL, &udp_cq_task_attr), "udp_cq_task");
}

uint16_t app_udp_cq_get_port(void);

/** @brief CQ 业务口广播（255.255.255.255:20103，SOF_BROADCAST）。 */
void app_udp_cq_broadcast(const uint8_t *data, uint16_t len);

/* ---- 贵州治超业务口 UDP 通道（CH_ID_UDP_GZOL；读 Sector1 net_cfg.port，与
       TCP 业务口同号不同协议栈；空/0 回退 9528，与 10011/CQ 同号则跳过绑定，
       见 app_udp.c _udp_gzol_read_port / udp_gzol_task）---- */

void udp_gzol_task(void *argument);
extern const osThreadAttr_t udp_gzol_task_attr;

static inline osThreadId_t app_udp_gzol_start(void)
{
    return pl_task_create_checked(osThreadNew(udp_gzol_task, NULL, &udp_gzol_task_attr),
                                   "udp_gzol_task");
}

uint16_t app_udp_gzol_get_port(void);

/* ---- GZ_OL 专用口绑定结果（只读诊断，见 app_diag.h）---- */

/** @brief GZ_OL 专用 UDP 业务口最近一次绑定结果 */
typedef enum {
    APP_UDP_GZOL_BIND_UNKNOWN = 0, /**< 任务尚未跑过一轮 */
    APP_UDP_GZOL_BIND_OK,          /**< 绑定成功（正在服务） */
    APP_UDP_GZOL_BIND_FALLBACK,    /**< net_cfg.port 无效/0 → 回退 9528 + 告警 */
    APP_UDP_GZOL_BIND_SKIPPED,     /**< 与 10011 / CQ 业务口同号 → 跳过绑定 + 告警 */
    APP_UDP_GZOL_BIND_FAIL,        /**< netconn_bind 失败（netconn_new 失败/端口占用等） */
} app_udp_gzol_bind_sta_t;

/** @brief 读 GZ_OL 专用口绑定结果（跨任务只读，无需锁） */
app_udp_gzol_bind_sta_t app_udp_gzol_bind_status(void);

/** @brief 绑定结果字符串（RTT 打印用，静态常量） */
const char *app_udp_gzol_bind_status_str(void);
