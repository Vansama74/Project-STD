/**
 * @file    app_ldi_w25_migrate.c
 * @brief   W25 旧格式 LDI 记录 → Sector 11 双库的一次性迁移垫片（只读）
 *
 * 背景：旧固件曾把 LDI 配置唯一存于 W25Qxx 末 4KB 扇区（16 槽 × 256B，
 * 旧魔数 0x0D001B00）；迁入内部 Sector 11 时删除了 W25 装载器，现场设备
 * 升级后即失去 lane/cert/host 等身份配置。本垫片恢复**只读装载**能力，
 * 由 ldi_ctx_init 在双库装载失败后调用，命中即经 app_ldi_int_cfg_save 落内部库。
 * 一次性语义：落库成功后双库装载不再失败，本函数不再被触发；W25 旧记录
 * 保持原样（最终取证/回退依据），直到双库轮转擦除 W25 末扇区为止。
 *
 * 三级布局识别：旧记录无版本号，magic 相同但 CRC 覆盖面不同，故按
 * 「v3（11 模块 × 13B / 188B 记录）→ v2（11 模块 × 12B / 176B）→ v1（6 模块
 * 116B）」逐级 CRC 试探（继承自 git 37de80e 的 app_ldi_cfg.c 装载逻辑）。
 * v2 的 E4H/E6H 模块 vendor[0] 实为 param（旧 0BH 混排写入），迁移时右移还原。
 */

#include "app_ldi_w25_migrate.h"

#include <string.h>

#include "dev_storage.h"
#include "dev_w25qxx.h"
#include "initcall.h"
#include "pl_crc.h"

/* ---- 旧记录魔数（与双库新魔数 0x4C444943 隔离，互不误认） ---- */
#define APP_FLASH_LDI_MAGIC_OLD (0x0D001B00U)

/* ---- 旧布局定义（仅本文件使用；尺寸按旧固件 37de80e 原样复制） ---- */

/* v2 模块（旧布局：11 模块 × 12B，无 param 字段，param 混入 vendor[0]） */
typedef struct {
    uint8_t device_type;
    uint8_t device_index;
    uint8_t vendor[10];
} app_ldi_mod_cfg_v2_t;

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
    app_ldi_mod_cfg_v2_t modules[APP_FLASH_LDI_MAX_MODULES];
} app_ldi_cfg_info_v2_t;

typedef struct {
    uint32_t magic;
    app_ldi_cfg_info_v2_t cfg;
    uint32_t crc32;
} app_ldi_record_v2_t;

/* v1（更早的 6 模块版，116B 记录），模块元素沿用 12B 版 */
#define APP_LDI_V1_MAX_MODULES (6U)

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
    app_ldi_mod_cfg_v2_t modules[APP_LDI_V1_MAX_MODULES];
} app_ldi_cfg_info_v1_t;

typedef struct {
    uint32_t magic;
    app_ldi_cfg_info_v1_t cfg;
    uint32_t crc32;
} app_ldi_record_v1_t;

/* v3（旧固件末期 = 当前布局）记录：直接复用 app_flash_ldi_cfg_info_t */
typedef struct {
    uint32_t magic;
    app_flash_ldi_cfg_info_t cfg;
    uint32_t crc32;
} app_ldi_record_v3_t;

/* ---- W25 旧记录区：末 4KB 扇区，16 槽 × 256B ---- */
#define W25_OLD_SLOT_SIZE (256U)
#define W25_OLD_SLOT_COUNT (16U)

static uint32_t s_w25_base;

/* sw_dev_initcall：确保 dev_w25qxx 已初始化后再按容量算末扇区基址 */
static void _app_ldi_w25_migrate_init(void)
{
    dev_storage_t *w25 = dev_w25qxx_get();
    s_w25_base         = dev_storage_capacity(w25) - 4096U;
}
sw_dev_initcall(_app_ldi_w25_migrate_init);

static uint32_t _slot_addr(uint32_t idx)
{
    return s_w25_base + idx * W25_OLD_SLOT_SIZE;
}

/** @brief 各代记录校验：magic + CRC32（覆盖面 = offsetof(crc32)，与旧固件同口径） */
static bool _valid_v3(const uint8_t *buf)
{
    const app_ldi_record_v3_t *rec = (const app_ldi_record_v3_t *)buf;
    if (rec->magic != APP_FLASH_LDI_MAGIC_OLD)
        return false;
    uint32_t crc = pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)rec,
                                 (uint32_t)offsetof(app_ldi_record_v3_t, crc32));
    return rec->crc32 == crc;
}

static bool _valid_v2(const uint8_t *buf)
{
    const app_ldi_record_v2_t *rec = (const app_ldi_record_v2_t *)buf;
    if (rec->magic != APP_FLASH_LDI_MAGIC_OLD)
        return false;
    uint32_t crc = pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)rec,
                                 (uint32_t)offsetof(app_ldi_record_v2_t, crc32));
    return rec->crc32 == crc;
}

static bool _valid_v1(const uint8_t *buf)
{
    const app_ldi_record_v1_t *rec = (const app_ldi_record_v1_t *)buf;
    if (rec->magic != APP_FLASH_LDI_MAGIC_OLD)
        return false;
    uint32_t crc = pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)rec,
                                 (uint32_t)offsetof(app_ldi_record_v1_t, crc32));
    return rec->crc32 == crc;
}

/**
 * @brief v2 → 当前布局：前部网络/身份字段同偏移直接搬，modules 逐项转换
 *
 * E4H/E6H 的旧记录 vendor[0] 实为 param（旧 0BH 混排写入），右移还原
 * param=vendor[0]、vendor[i]=旧vendor[i+1]、vendor[9]=0；其余类型原样搬。
 */
static void _migrate_v2(const app_ldi_cfg_info_v2_t *src, app_flash_ldi_cfg_info_t *info)
{
    memset(info, 0, sizeof(*info));
    memcpy(info->device_ip, src->device_ip, sizeof(info->device_ip));
    info->device_port = src->device_port;
    memcpy(info->host_ip, src->host_ip, sizeof(info->host_ip));
    info->host_port = src->host_port;
    memcpy(info->netmask, src->netmask, sizeof(info->netmask));
    memcpy(info->gateway, src->gateway, sizeof(info->gateway));
    memcpy(info->lane_hex, src->lane_hex, sizeof(info->lane_hex));
    memcpy(info->cert, src->cert, sizeof(info->cert));

    uint8_t n = src->module_count;
    if (n > APP_FLASH_LDI_MAX_MODULES)
        n = APP_FLASH_LDI_MAX_MODULES;
    info->module_count = n;
    for (uint8_t i = 0; i < n; i++) {
        info->modules[i].device_type  = src->modules[i].device_type;
        info->modules[i].device_index = src->modules[i].device_index;
        if (src->modules[i].device_type == 0xE4U || src->modules[i].device_type == 0xE6U) {
            info->modules[i].param = src->modules[i].vendor[0];
            memcpy(info->modules[i].vendor, &src->modules[i].vendor[1], 9);
            info->modules[i].vendor[9] = 0U;
        } else {
            info->modules[i].param = 0U;
            memcpy(info->modules[i].vendor, src->modules[i].vendor,
                   sizeof(info->modules[i].vendor));
        }
    }
}

/** @brief v1 → 当前布局（6 模块 → 11 模块，无 param 字段） */
static void _migrate_v1(const app_ldi_cfg_info_v1_t *src, app_flash_ldi_cfg_info_t *info)
{
    memset(info, 0, sizeof(*info));
    memcpy(info->device_ip, src->device_ip, sizeof(info->device_ip));
    info->device_port = src->device_port;
    memcpy(info->host_ip, src->host_ip, sizeof(info->host_ip));
    info->host_port = src->host_port;
    memcpy(info->netmask, src->netmask, sizeof(info->netmask));
    memcpy(info->gateway, src->gateway, sizeof(info->gateway));
    memcpy(info->lane_hex, src->lane_hex, sizeof(info->lane_hex));
    memcpy(info->cert, src->cert, sizeof(info->cert));

    uint8_t n = src->module_count;
    if (n > APP_LDI_V1_MAX_MODULES)
        n = APP_LDI_V1_MAX_MODULES;
    info->module_count = n;
    for (uint8_t i = 0; i < n; i++) {
        info->modules[i].device_type  = src->modules[i].device_type;
        info->modules[i].device_index = src->modules[i].device_index;
        info->modules[i].param        = 0U;
        memcpy(info->modules[i].vendor, src->modules[i].vendor,
               sizeof(info->modules[i].vendor));
    }
}

bool app_ldi_w25_migrate_try(app_flash_ldi_cfg_info_t *out)
{
    /* 静态 SRAM：禁止用任务栈缓冲做 SPI DMA 目的（CCM/栈均不可靠作 DMA 目的）。
     * aligned(4)：s_buf 会被强转为 record 指针读 uint32_t/uint16_t 成员。 */
    static uint8_t s_buf[W25_OLD_SLOT_SIZE] __attribute__((aligned(4)));
    dev_storage_t *w25;

    if (out == nullptr)
        return false;
    w25 = dev_w25qxx_get();

    /* ---- 第 1 遍：v3（11 模块 × 13B，倒序，第一个合法槽即最新） ---- */
    for (int32_t i = (int32_t)W25_OLD_SLOT_COUNT - 1; i >= 0; i--) {
        memset(s_buf, 0, sizeof(s_buf));
        if (dev_storage_read(w25, _slot_addr((uint32_t)i), s_buf, sizeof(app_ldi_record_v3_t)) < 0)
            continue;
        if (!_valid_v3(s_buf))
            continue;
        memcpy(out, &((const app_ldi_record_v3_t *)s_buf)->cfg, sizeof(*out));
        return true;
    }

    /* ---- 第 2 遍：v2（11 模块 × 12B） ---- */
    for (int32_t i = (int32_t)W25_OLD_SLOT_COUNT - 1; i >= 0; i--) {
        memset(s_buf, 0, sizeof(s_buf));
        if (dev_storage_read(w25, _slot_addr((uint32_t)i), s_buf, sizeof(app_ldi_record_v2_t)) < 0)
            continue;
        if (!_valid_v2(s_buf))
            continue;
        _migrate_v2(&((const app_ldi_record_v2_t *)s_buf)->cfg, out);
        return true;
    }

    /* ---- 第 3 遍：v1（6 模块 × 12B） ---- */
    for (int32_t i = (int32_t)W25_OLD_SLOT_COUNT - 1; i >= 0; i--) {
        memset(s_buf, 0, sizeof(s_buf));
        if (dev_storage_read(w25, _slot_addr((uint32_t)i), s_buf, sizeof(app_ldi_record_v1_t)) < 0)
            continue;
        if (!_valid_v1(s_buf))
            continue;
        _migrate_v1(&((const app_ldi_record_v1_t *)s_buf)->cfg, out);
        return true;
    }

    memset(out, 0, sizeof(*out));
    return false;
}
