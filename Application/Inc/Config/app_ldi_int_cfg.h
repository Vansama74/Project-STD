/**
 * @file    app_ldi_int_cfg.h
 * @brief   LDI 配置的内部 Flash 槽式持久化（Sector 11，0x080E0000~0x080FFFFF）
 *
 * 契约（改错静默致现场故障）：① Sector 11 主应用独占，镜像不得侵入 0x080E0000；
 * ② 记录 = magic + version + 配置 + CRC32，总长须 ≤ 槽 256B；
 * ③ 保存只追加空槽，槽满才整扇区擦除并回槽 0。
 * 详见 doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md §17
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app_ldi_cfg.h"

/* ---- Sector 11 槽日志布局：128KB / 256B = 512 槽，顺序追加 ---- */
#define APP_LDI_INT_CFG_ADDR (0x080E0000U)                       /**< 记录区基址（= FLASH 区尾） */
#define APP_LDI_INT_CFG_SIZE (128U * 1024U)                      /**< 独占扇区容量 */
#define APP_LDI_INT_CFG_SLOT_SIZE (256U)                         /**< 单槽字节数，擦除态全 0xFF */
#define APP_LDI_INT_CFG_SLOT_COUNT (APP_LDI_INT_CFG_SIZE / APP_LDI_INT_CFG_SLOT_SIZE) /**< 512 槽 */

#define APP_LDI_INT_CFG_MAGIC (0x4C444943U) /**< 记录魔数：不随 W25 旧魔数复用，避免误认 */
#define APP_LDI_INT_CFG_VERSION (1U)        /**< 记录布局版本，仅本值被接受 */

/**
 * 槽内记录：magic + version + 完整 LDI 配置 + CRC32
 *
 * 总长按 sizeof 计（11 模块 × 13B ⇒ 192B，含对齐 padding），必须 ≤ 槽 256B；
 * CRC32 覆盖 [0, offsetof(crc32))（188B，整字）。结构体里同时保留网络四项，
 * 作为 Sector1 的恢复镜像（真源仍是 Sector1）。
 */
typedef struct {
    uint32_t magic;               /**< APP_LDI_INT_CFG_MAGIC；0xFFFFFFFF = 未写入 */
    uint32_t version;             /**< APP_LDI_INT_CFG_VERSION；不匹配按无效记录跳过 */
    app_flash_ldi_cfg_info_t cfg; /**< 0AH/0BH 下发的完整 LDI 配置 */
    uint32_t crc32;               /**< CRC32 覆盖 magic..cfg（不含自身） */
} app_ldi_int_cfg_record_t;

/* 契约 ②：记录长度 ≤ 槽 256B（现 192B），且记录长度与 crc32 偏移均 4 字节对齐——
 * 越限/错位会静默写坏相邻槽或让 CRC 校验恒失败；不变量清单见
 * doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md §17.1 */

/**
 * @brief 从 Sector 11 槽日志装载 LDI 配置
 * @param out 输出配置，函数恒写满 sizeof(*out) 字节（无有效记录时清零）
 * @return true = 找到有效记录；false = 无有效记录（未配置，调用方按出厂口径处理）
 * @note 扫描全部 512 槽取最后一个 magic/version/CRC 合法者：半写槽被 CRC 挡下、
 *       自动回退上一槽；上电调用一次，纯内存映射读，不擦写 Flash。
 */
bool app_ldi_int_cfg_load(app_flash_ldi_cfg_info_t *out);

/**
 * @brief 追加保存 LDI 配置到 Sector 11 槽日志
 * @param in 待保存配置（完整 app_flash_ldi_cfg_info_t，含网络镜像字段）
 * @return 0 成功（已回读校验）；-1 参数非法或已有保存进行中；-2 整扇区擦除失败；
 *         -3 编程失败或回读 CRC 不符
 * @note 顺序追加到第一个全 0xFF 空槽，不擦扇区；512 槽全满才整扇区擦除
 *       （擦除期间总线停顿 1~2s）并从槽 0 重来。固件不自动重试，由 0AH/0BH
 *       上位机重发兜底。
 */
int32_t app_ldi_int_cfg_save(const app_flash_ldi_cfg_info_t *in);
