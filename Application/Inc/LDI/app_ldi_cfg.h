#pragma once

#include <stdint.h>
#include "dev_storage.h"

// Flash Sector 11 (0x080E0000~0x080FFFFF, 128KB) 已释放。
// LDI 配置现已迁移至 W25Qxx 最后一个 4KB 扇区，通过 dev_storage_ops 访问。
#define APP_FLASH_LDI_MAGIC    0x0D001B00
#define APP_FLASH_LDI_MAX_MODULES (11U) // Flash 最大可存储的功能模块数量

/**
 * 单功能模块配置信息（从 0BH 命令下发，共 13 字节）
 *
 * 每个模块包含设备类型编码、模块序号、类型参数和厂商自定义参数段。
 * 存储在 Flash 中供 1EH 参数采集命令回读。
 */
typedef struct {
    uint8_t device_type;   // ldi_device_t 枚举值 (E1H~EBH)
    uint8_t device_index;  // 功能模块序号，从 01H 开始
    uint8_t param;         // 类型参数：E4H=FilterTime / E6H=FontLine / 其余类型=0（协议 §4.2/4.5 模块第 3 字节）
    uint8_t vendor[10];    // 厂商自定义参数段
} app_flash_ldi_module_cfg_t;

/* 契约：单模块条目固定 13B（device_type 1 + device_index 1 + param 1 + vendor 10），
 * W25 记录按此布局读写，改错静默错位。 */

/**
 * LDI 车道设备配置信息
 *
 * 存储 0AH 指令下发的全部网络参数 + 0BH 下发的模块配置。
 * 设备上电后从中加载配置。
 */
typedef struct {
    uint8_t device_ip[4];  // 设备自身 IP 地址
    uint16_t device_port;  // 设备端口号
    uint8_t host_ip[4];    // 上位机 IP 地址 (外设控制服务)
    uint16_t host_port;    // 上位机端口号
    uint8_t netmask[4];    // 子网掩码
    uint8_t gateway[4];    // 网关地址
    uint8_t lane_hex[5];   // 车道 HEX 编号 (来自 req_head.lane_code)
    uint8_t cert[8];       // 设备验证信息 (来自 req_head.cert_info)
    uint8_t module_count;  // 功能模块数量 N
    app_flash_ldi_module_cfg_t modules[APP_FLASH_LDI_MAX_MODULES]; // N <= MAX_MODULES
} app_flash_ldi_cfg_info_t;

/* 槽式存储：4KB 扇区切成 16 个 256B 槽，顺序追加、擦除后从槽 0 重来。
 * 追加写不擦扇区 ⇒ 单次保存掉电最多毁掉"正在写的那个槽"，旧记录仍在。 */
#define APP_FLASH_LDI_SLOT_SIZE  (256U)
#define APP_FLASH_LDI_SLOT_COUNT (4096U / APP_FLASH_LDI_SLOT_SIZE)

/**
 * Flash 存储记录（槽内布局）= magic + 配置 + CRC32 校验
 *
 * 总长按 sizeof(app_flash_ldi_record_t) 计（11 模块 × 13B，含对齐 padding，
 * 必须 <= APP_FLASH_LDI_SLOT_SIZE），CRC32 覆盖 [0, offsetof(crc32))。
 * 记录**不带版本号**：旧固件布局与本布局的区分靠 CRC 试探（magic 相同、
 * CRC 取不同长度各试一次），故跨版本升级时必须保留下方 v2 / v1 布局定义。
 */
typedef struct {
    uint32_t magic;
    app_flash_ldi_cfg_info_t cfg;
    uint32_t crc32;
} app_flash_ldi_record_t;

/* v2 布局（2026-11-18 前：11 模块 × 12B，无 param 字段，param 混入 vendor[0]）
 * ——只读迁移，禁止写入 */
typedef struct {
    uint8_t device_type;
    uint8_t device_index;
    uint8_t vendor[10];
} app_flash_ldi_module_cfg_v2_t;

typedef struct {
    uint8_t device_ip[4];
    uint16_t device_port;
    uint8_t host_ip[4];
    uint16_t host_port;
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t lane_hex[5];
    uint8_t cert[8];
    uint8_t module_count;
    app_flash_ldi_module_cfg_v2_t modules[APP_FLASH_LDI_MAX_MODULES];
} app_flash_ldi_cfg_info_v2_t;

typedef struct {
    uint32_t magic;
    app_flash_ldi_cfg_info_v2_t cfg;
    uint32_t crc32;
} app_flash_ldi_record_v2_t;

/* v1 布局（2026-11-17 前的 6 模块版，116B 记录）——只用于装载时识别并迁移，禁止写入。
 * 模块元素沿用 12B 的 app_flash_ldi_module_cfg_v2_t，以保证本布局恒为 116B。 */
#define APP_FLASH_LDI_V1_MAX_MODULES (6U)

typedef struct {
    uint8_t device_ip[4];
    uint16_t device_port;
    uint8_t host_ip[4];
    uint16_t host_port;
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t lane_hex[5];
    uint8_t cert[8];
    uint8_t module_count;
    app_flash_ldi_module_cfg_v2_t modules[APP_FLASH_LDI_V1_MAX_MODULES];
} app_flash_ldi_cfg_info_v1_t;

typedef struct {
    uint32_t magic;
    app_flash_ldi_cfg_info_v1_t cfg;
    uint32_t crc32;
} app_flash_ldi_record_v1_t;

bool app_flash_ldi_is_config_empty(volatile const app_flash_ldi_record_t *rec);
bool app_flash_ldi_is_config_valid(volatile const app_flash_ldi_record_t *rec);
int32_t app_flash_ldi_erase_config(void);
int32_t app_flash_ldi_write_config(app_flash_ldi_record_t *rec);
/**
 * @brief 保存 LDI 配置到 W25 槽区（顺序追加到第一个空槽，不做写后回读）
 * @return 0 成功；<0 擦除或写入失败（dev_storage 错误码）
 */
int32_t app_flash_ldi_save_config(app_flash_ldi_cfg_info_t *info);
/** @brief 从 W25Qxx 加载 LDI 配置（倒序找最新槽，兼容 v1 旧记录并自动迁移），返回 true 表示读取到有效配置 */
bool app_flash_ldi_load_config(app_flash_ldi_cfg_info_t *info);

/** @brief 获取 LDI Flash 存储句柄（内部使用） */
dev_storage_t *app_flash_ldi_get_storage(void);