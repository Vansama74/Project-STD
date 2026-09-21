/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * File Name          : Target/lwipopts.h
 * Description        : This file overrides LwIP stack default configuration
 *                      done in opt.h file.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Define to prevent recursive inclusion --------------------------------------*/
#ifndef __LWIPOPTS__H__
#define __LWIPOPTS__H__

#include "main.h"

/*-----------------------------------------------------------------------------*/
/* Current version of LwIP supported by CubeMx: 2.1.2 -*/
/*-----------------------------------------------------------------------------*/

/* Within 'USER CODE' section, code will be kept by default at each generation */
/* USER CODE BEGIN 0 */

/* ---- LwIP 调试输出档位收紧（2026-09-18 YN_OL TCP 现场问题取证补强）--------
 * 由来：Core/Inc/main.h（CubeMX 生成区）里 LWIP_DEBUG / SOCKETS_DEBUG / TCP_DEBUG
 * 均为 LWIP_DBG_ON 且 LWIP_DBG_MIN_LEVEL = LWIP_DBG_LEVEL_ALL ⇒ LwIP 的
 * tcp_slowtmr / tcp_recved / tcp_fasttmr 等 LEVEL_ALL 级信息行经
 * printf → _write（SEGGER_RTT_Syscalls_GCC.c）→ RTT 通道 0 输出，
 * 实测 ~5~17 行/秒（每个活动 pcb 每 500ms 数行）。
 * 后果（实测确认）：RTT 上行缓冲仅 1KB 且为 NO_BLOCK_SKIP 模式（满则**整条丢**），
 * 开机横幅 + LwIP 噪声十几秒即把缓冲灌满 ⇒ **此后所有现场诊断
 * （[err] 堆失败钩子、[disp]、[yn_ol]、[tcp_srv]、[diag] 体检行）全部静默丢失**。
 * 2026-09-18 之前「死机前后 RTT 无任何输出、写不进任何证据」即由此而来
 * （需用外部读走缓冲才能看到，见 .analysis/yn_ol/fix_verify/rtt_log.py）。
 * 处置：只过滤 LEVEL_ALL 常规信息行，**警告/错误（WARNING 及以上）与工程自有
 * 诊断（直接 SEGGER_RTT_printf，不走 LwIP 调试宏）全部保留**。
 * 位置：main.h 属 CubeMX 生成文件（改动可能被重新生成覆盖），故覆盖放在本文件
 * USER CODE 段（CubeMX 保留区）；数值用字面量（此时 debug.h 未必已包含）。
 * 一键复原：`make DISP=... APP_LWIP_DBG_LEVEL=0x00`（或改下面的默认值）即恢复全量
 * LwIP 日志（用于对比采样/怀疑某告警被过滤时）。 */
#ifndef APP_LWIP_DBG_LEVEL
#define APP_LWIP_DBG_LEVEL 0x01 /* = LWIP_DBG_LEVEL_WARNING（0x00 = LEVEL_ALL 全开） */
#endif
#undef LWIP_DBG_MIN_LEVEL
#define LWIP_DBG_MIN_LEVEL APP_LWIP_DBG_LEVEL

/* USER CODE END 0 */

#ifdef __cplusplus
 extern "C" {
#endif

/* STM32CubeMX Specific Parameters (not defined in opt.h) ---------------------*/
/* Parameters set in STM32CubeMX LwIP Configuration GUI -*/
/*----- WITH_RTOS enabled (Since FREERTOS is set) -----*/
#define WITH_RTOS 1
/*----- CHECKSUM_BY_HARDWARE enabled -----*/
#define CHECKSUM_BY_HARDWARE 1
/*-----------------------------------------------------------------------------*/

/* LwIP Stack Parameters (modified compared to initialization value in opt.h) -*/
/* Parameters set in STM32CubeMX LwIP Configuration GUI -*/
/*----- Value in opt.h for MEM_ALIGNMENT: 1 -----*/
#define MEM_ALIGNMENT 4
/*----- Default Value for MEM_SIZE: 1600 ---*/
#define MEM_SIZE 12*1024
/*----- Default Value for MEMP_NUM_SYS_TIMEOUT: 3 ---*/
#define MEMP_NUM_SYS_TIMEOUT 20
/*----- Value in opt.h for LWIP_ETHERNET: LWIP_ARP || PPPOE_SUPPORT -*/
#define LWIP_ETHERNET 1
/*----- Value in opt.h for LWIP_DNS_SECURE: (LWIP_DNS_SECURE_RAND_XID | LWIP_DNS_SECURE_NO_MULTIPLE_OUTSTANDING | LWIP_DNS_SECURE_RAND_SRC_PORT) -*/
#define LWIP_DNS_SECURE 7
/*----- Value in opt.h for TCP_SND_QUEUELEN: (4*TCP_SND_BUF + (TCP_MSS - 1))/TCP_MSS -----*/
#define TCP_SND_QUEUELEN 9
/*----- Value in opt.h for TCP_SNDLOWAT: LWIP_MIN(LWIP_MAX(((TCP_SND_BUF)/2), (2 * TCP_MSS) + 1), (TCP_SND_BUF) - 1) -*/
#define TCP_SNDLOWAT 1071
/*----- Value in opt.h for TCP_SNDQUEUELOWAT: LWIP_MAX(TCP_SND_QUEUELEN)/2, 5) -*/
#define TCP_SNDQUEUELOWAT 5
/*----- Value in opt.h for TCP_WND_UPDATE_THRESHOLD: LWIP_MIN(TCP_WND/4, TCP_MSS*4) -----*/
#define TCP_WND_UPDATE_THRESHOLD 536
/*----- Enable UDP broadcast send/receive -----*/
#define IP_SOF_BROADCAST      1
#define IP_SOF_BROADCAST_RECV 1

/*----- Default Value for LWIP_NETIF_STATUS_CALLBACK: 0 ---*/
#define LWIP_NETIF_STATUS_CALLBACK 1
/*----- Value in opt.h for LWIP_NETIF_LINK_CALLBACK: 0 -----*/
#define LWIP_NETIF_LINK_CALLBACK 1
/*----- Value in opt.h for TCPIP_THREAD_STACKSIZE: 0 -----*/
#define TCPIP_THREAD_STACKSIZE 1024
/*----- Value in opt.h for TCPIP_THREAD_PRIO: 1 -----*/
#define TCPIP_THREAD_PRIO 24
/*----- Value in opt.h for TCPIP_MBOX_SIZE: 0 -----*/
#define TCPIP_MBOX_SIZE 6
/*----- Value in opt.h for SLIPIF_THREAD_STACKSIZE: 0 -----*/
#define SLIPIF_THREAD_STACKSIZE 1024
/*----- Value in opt.h for SLIPIF_THREAD_PRIO: 1 -----*/
#define SLIPIF_THREAD_PRIO 3
/*----- Value in opt.h for DEFAULT_THREAD_STACKSIZE: 0 -----*/
#define DEFAULT_THREAD_STACKSIZE 1024
/*----- Value in opt.h for DEFAULT_THREAD_PRIO: 1 -----*/
#define DEFAULT_THREAD_PRIO 3
/*----- Value in opt.h for DEFAULT_UDP_RECVMBOX_SIZE: 0 -----*/
#define DEFAULT_UDP_RECVMBOX_SIZE 6
/*----- Value in opt.h for DEFAULT_TCP_RECVMBOX_SIZE: 0 -----*/
#define DEFAULT_TCP_RECVMBOX_SIZE 6
/*----- Value in opt.h for DEFAULT_ACCEPTMBOX_SIZE: 0 -----*/
#define DEFAULT_ACCEPTMBOX_SIZE 6
/*----- Default Value for LWIP_TCP_KEEPALIVE: 0 ---*/
#define LWIP_TCP_KEEPALIVE 1
/*----- Value in opt.h for RECV_BUFSIZE_DEFAULT: INT_MAX -----*/
#define RECV_BUFSIZE_DEFAULT 2000000000
/*----- Value in opt.h for LWIP_STATS: 1 -----*/
#define LWIP_STATS 0
/*----- Value in opt.h for CHECKSUM_GEN_IP: 1 -----*/
#define CHECKSUM_GEN_IP 0
/*----- Value in opt.h for CHECKSUM_GEN_UDP: 1 -----*/
#define CHECKSUM_GEN_UDP 0
/*----- Value in opt.h for CHECKSUM_GEN_TCP: 1 -----*/
#define CHECKSUM_GEN_TCP 0
/*----- Value in opt.h for CHECKSUM_GEN_ICMP: 1 -----*/
#define CHECKSUM_GEN_ICMP 0
/*----- Value in opt.h for CHECKSUM_GEN_ICMP6: 1 -----*/
#define CHECKSUM_GEN_ICMP6 0
/*----- Value in opt.h for CHECKSUM_CHECK_IP: 1 -----*/
#define CHECKSUM_CHECK_IP 0
/*----- Value in opt.h for CHECKSUM_CHECK_UDP: 1 -----*/
#define CHECKSUM_CHECK_UDP 0
/*----- Value in opt.h for CHECKSUM_CHECK_TCP: 1 -----*/
#define CHECKSUM_CHECK_TCP 0
/*----- Value in opt.h for CHECKSUM_CHECK_ICMP: 1 -----*/
#define CHECKSUM_CHECK_ICMP 0
/*----- Value in opt.h for CHECKSUM_CHECK_ICMP6: 1 -----*/
#define CHECKSUM_CHECK_ICMP6 0
/*-----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

#define MQTT_REQ_MAX_IN_FLIGHT 16
#define LWIP_SO_RCVTIMEO      1 /* UDP recv_timeout，用于探测通道可用性 */

/* 网络通道资源池（2026-08-21 修复 LDI 搜索广播丢包；2026-09-14 重核）：
 * dev 共存构建常驻 netconn = UDP 10011 + UDP 20103(CQ) + UDP 9528(GZ_OL 业务口)
 * + TCP Server listener + TCP Client = 5，叠加 TCP Server 已连接客户端 1 =
 * 6；广播回退临时 conn 最多 +2（LDI/CQ 各一，常驻 conn 就绪时不占用）→
 * 峰值 8 恰为池容量。**新增 UDP 端口协议时仍须随通道数核算（doc/06 预算）**，
 * 届时优先看本行：常驻数不得逼近池容量，否则广播回退静默丢包复发。 */
#define MEMP_NUM_NETCONN 8
#define MEMP_NUM_UDP_PCB 8
/* USER CODE END 1 */

#ifdef __cplusplus
}
#endif
#endif /*__LWIPOPTS__H__ */
