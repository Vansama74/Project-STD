/**
 * @file    app_ldi_cfg.h
 * @brief   LDI 配置的 RAM/持久化共用布局（模块条目、车道设备配置）
 *
 * 契约（改错静默致现场故障）：① 模块条目固定 13B（type+index+param+vendor[10]）；
 * ② 配置记录 ≤ 256B（持久化槽宽）；③ 网络四项真源是 Sector1，本结构里是恢复镜像。
 * 详见 doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md §17
 */

#pragma once

#include <stdint.h>

#define APP_FLASH_LDI_MAX_MODULES (11U) /**< 配置可存的功能模块数量上限（协议 E1H~EBH 子集） */

/**
 * 单功能模块配置信息（从 0BH 命令下发，共 13 字节）
 *
 * 每个模块包含设备类型编码、模块序号、类型参数和厂商自定义参数段。
 * 存储在 Flash 中供 1EH 参数采集命令回读。
 */
typedef struct {
    uint8_t device_type;  /**< ldi_device_t 枚举值 (E1H~EBH) */
    uint8_t device_index; /**< 功能模块序号，从 01H 开始 */
    uint8_t param;        /**< 类型参数：E4H=FilterTime / E6H=FontLine / 其余类型=0 */
    uint8_t vendor[10];   /**< 厂商自定义参数段 */
} app_flash_ldi_module_cfg_t;

/* 契约 ①：单模块条目固定 13B，持久化记录按此布局读写，改错静默错位。 */

/**
 * LDI 车道设备配置信息
 *
 * 存储 0AH 指令下发的全部网络参数 + 0BH 下发的模块配置，既是 LDI 运行态 RAM
 * 镜像（`ldi_ctx_t.cfg`），也是跨介质双库记录（Sector 11 + W25 末扇区）的载荷。
 */
typedef struct {
    uint8_t device_ip[4];  /**< 设备自身 IP 地址（镜像，真源 Sector1） */
    uint16_t device_port;  /**< 设备端口号（镜像，真源 Sector1） */
    uint8_t host_ip[4];    /**< 上位机 IP 地址 (外设控制服务) */
    uint16_t host_port;    /**< 上位机端口号 */
    uint8_t netmask[4];    /**< 子网掩码（镜像，真源 Sector1） */
    uint8_t gateway[4];    /**< 网关地址（镜像，真源 Sector1） */
    uint8_t lane_hex[5];   /**< 车道 HEX 编号 (来自 req_head.lane_code) */
    uint8_t cert[8];       /**< 设备验证信息 (来自 req_head.cert_info) */
    uint8_t module_count;  /**< 功能模块数量 N（≤ MAX_MODULES） */
    app_flash_ldi_module_cfg_t modules[APP_FLASH_LDI_MAX_MODULES]; /**< N ≤ MAX_MODULES */
} app_flash_ldi_cfg_info_t;
