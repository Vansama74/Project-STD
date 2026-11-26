/**
 * @file    app_ldi_w25_migrate.h
 * @brief   W25 旧格式 LDI 记录 → Sector 11 双库的一次性迁移垫片（只读）
 *
 * 详见 doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md §18.4
 */

#pragma once

#include <stdbool.h>

#include "app_ldi_cfg.h"

/**
 * @brief 只读扫描 W25 末 4KB 旧格式（magic 0x0D001B00）LDI 记录并装载
 *
 * 倒序扫 16 槽，按 v3（11 模块 × 13B）→ v2（11 模块 × 12B，param 混在
 * vendor[0]）→ v1（6 模块 × 12B）逐级 CRC 试探，命中即输出并返回 true。
 * 本函数**不写任何介质**：W25 保持原样（作最终取证/回退依据），落库由调用方
 * app_ldi_int_cfg_save 完成。新格式乒乓记录（magic 0x4C444943）由魔数隔离，
 * 本函数自然忽略；反之本模块的旧魔数记录也不被双库装载路径识别。
 *
 * @param out 输出配置；命中时写满 sizeof(*out)，未命中时清零
 * @return true = 找到合法旧记录；false = 无/读取失败
 * @note 上电调用一次（ldi_ctx_init 在双库装载失败后调用）；W25 读为 SPI+DMA，
 *       缓冲静态落 SRAM。一次性语义：迁移成功落库后双库装载不再失败，
 *       本函数不再被触发。
 */
bool app_ldi_w25_migrate_try(app_flash_ldi_cfg_info_t *out);
