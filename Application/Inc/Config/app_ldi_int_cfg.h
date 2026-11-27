/**
 * @file    app_ldi_int_cfg.h
 * @brief   LDI 配置持久化：内部 Flash Sector 11 + W25 末扇区 跨介质双库乒乓
 *
 * 契约（改错静默致现场故障）：
 * ① Sector 11（0x080E0000~0x080FFFFF，128KB）主应用独占，镜像不得侵入；
 * ② 记录 = magic + version + seq + 配置 + CRC32，总长须 ≤ 槽 256B；
 * ③ 保存 = 追加写活动库空槽；活动库写满时轮转到对侧库（擦对侧 → 写槽 0 →
 *    回读校验），**任何时刻最新已提交记录至少在一个介质上**——内部库 128KB
 *    擦除（1~2s 停顿）期间最新记录已校验落在 W25，反之亦然，擦除窗口归零；
 * ④ 双库记录用同一魔数 0x4C444943 + seq 递增序号统一排序；W25 上的旧格式
 *    记录（0x0D001B00）只由迁移垫片（app_ldi_w25_migrate）识别，本模块忽略。
 * 详见 doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md §18
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app_ldi_cfg.h"

/* ---- 库 0：内部 Sector 11，128KB / 256B = 512 槽，顺序追加 ---- */
#define APP_LDI_INT_CFG_ADDR (0x080E0000U)                       /**< 记录区基址（= FLASH 区尾） */
#define APP_LDI_INT_CFG_SIZE (128U * 1024U)                      /**< 独占扇区容量 */
#define APP_LDI_INT_CFG_SLOT_SIZE (256U)                         /**< 单槽字节数，擦除态全 0xFF */
#define APP_LDI_INT_CFG_SLOT_COUNT (APP_LDI_INT_CFG_SIZE / APP_LDI_INT_CFG_SLOT_SIZE) /**< 512 槽 */

/* ---- 库 1：W25Qxx 末 4KB 扇区，16 槽（基址运行时按容量计算，见 .c） ---- */
#define APP_LDI_W25_SLOT_SIZE (256U)        /**< 与内部库同槽宽 */
#define APP_LDI_W25_SLOT_COUNT (16U)        /**< 4KB / 256B */

#define APP_LDI_INT_CFG_MAGIC (0x4C444943U) /**< 双库新记录魔数：不随 W25 旧魔数复用，避免误认 */
#define APP_LDI_INT_CFG_VERSION (2U)        /**< v2：记录带 seq 递增序号；v1 只读兼容 */

/**
 * 库内记录（v2）：magic + version + seq + 完整 LDI 配置 + CRC32
 *
 * 总长按 sizeof 计（11 模块 × 13B ⇒ 196B，含对齐 padding），必须 ≤ 槽 256B；
 * CRC32 覆盖 [0, offsetof(crc32))（192B，整字）。结构体里同时保留网络四项，
 * 作为 Sector1 的恢复镜像（真源仍是 Sector1）。
 * seq 单调递增（每次保存 = 两库现存最大 seq + 1），装载时跨两库取 seq 最大者。
 */
typedef struct {
    uint32_t magic;               /**< APP_LDI_INT_CFG_MAGIC；0xFFFFFFFF = 未写入 */
    uint32_t version;             /**< APP_LDI_INT_CFG_VERSION；不匹配按无效记录跳过 */
    uint32_t seq;                 /**< 递增序号：两库统一排序，最大者即最新记录 */
    app_flash_ldi_cfg_info_t cfg; /**< 0AH/0BH 下发的完整 LDI 配置 */
    uint32_t crc32;               /**< CRC32 覆盖 magic..cfg（不含自身） */
} app_ldi_int_cfg_record_t;

/**
 * v1 记录（旧布局，无 seq 字段）：仅装载识别用（视为 seq=0），禁止写入。
 * 保留定义是为了台架/早期设备的旧记录不被本固件判废。
 */
typedef struct {
    uint32_t magic;
    uint32_t version;
    app_flash_ldi_cfg_info_t cfg;
    uint32_t crc32;
} app_ldi_int_cfg_record_v1_t;

/* 契约 ②：记录长度 ≤ 槽 256B（v2 现 196B），且记录长度与 crc32 偏移均 4 字节对齐——
 * 越限/错位会静默写坏相邻槽或让 CRC 校验恒失败；不变量清单见
 * doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md §18.1 */

/**
 * @brief 从双库（内部 Sector 11 + W25 末扇区）装载 LDI 配置
 * @param out 输出配置，函数恒写满 sizeof(*out) 字节（无有效记录时清零）
 * @return true = 找到有效记录；false = 无有效记录（未配置，调用方按出厂口径处理）
 * @note 扫描两库全部槽，取 seq 最大且 magic/version/CRC 合法者；内部库的 v1 记录
 *       （无 seq）按 seq=0 参与比较。半写槽被 CRC 挡下、自动回退上一槽。上电调用
 *       一次；内部库纯内存映射读，W25 走 SPI 读（16 槽）。
 */
bool app_ldi_int_cfg_load(app_flash_ldi_cfg_info_t *out);

/**
 * @brief 追加保存 LDI 配置（跨介质乒乓）
 * @param in 待保存配置（完整 app_flash_ldi_cfg_info_t，含网络镜像字段）
 * @return 0 成功（已回读校验）；-1 参数非法或已有保存进行中；-2 对侧库擦除失败；
 *         -3 编程失败或回读 CRC 不符
 * @note 新记录 seq = 两库现存最大 seq + 1，追加到活动库（含最新记录的库；两库皆
 *       无有效记录时默认内部库）第一个全 0xFF 空槽，不擦库；活动库写满则擦对侧
 *       库（内部 128KB 擦除期间总线停顿 1~2s）→ 写对侧槽 0 → 回读校验——校验
 *       失败不切换、旧库原样保留；校验成功即完成轮转，被换下的库留作只读历史，
 *       下次轮转时再擦。固件不自动重试，由 0AH/0BH 上位机重发兜底。
 */
int32_t app_ldi_int_cfg_save(const app_flash_ldi_cfg_info_t *in);
