/**
 * @file    app_board_net_cfg.c
 * @brief   板级系统配置存储（内部 Flash Sector 1, 0x08004000）
 *
 * app_board_sys_info_t 记录 = magic(4B) | update_sta(4B) | FWInfo(40B) | NetConfig(16B) | CRC32(4B)
 * 总长 68B（17 words），按 word 写入 Flash。
 *
 * 操作流程：
 *   读取 → 直接内存映射 (ADDR_CONFIG_SECTOR) → 空/完整性检查 → 使用
 *   更新 → 读-改-写：空/损坏完整初始化，有效仅改 net_cfg，升级中间态拒绝，
 *          同值跳过擦写（语义详见 doc/07_LDI与IAP配置解耦）
 */

#include "app_board_net_cfg.h"
#include "dev_flash_int.h"
#include "pl_crc.h"
#include "initcall.h"

#include <string.h>

#define BOARD_CFG_SIZE 0x4000 /* 16KB */

/* ---- Sector1 Flash 存储实例 ---- */
static dev_flash_int_t g_board_cfg_flash = {
    .me        = {.capacity = BOARD_CFG_SIZE},
    .base_addr = ADDR_CONFIG_SECTOR,
    .sector    = PL_FLASH_SECTOR_1,
};

/* ---- ops 绑定（hw_dev_initcall） ---- */
static void _app_board_net_cfg_storage_init(void)
{
    g_board_cfg_flash.me.ops = &flash_int_ops;
}
hw_dev_initcall(_app_board_net_cfg_storage_init);

static dev_storage_t *_storage(void)
{
    return &g_board_cfg_flash.me;
}

/* ================================================================
 *  完整性检查
 * ================================================================ */

/** @brief Flash 擦除后全为 0xFF，magic + crc 均为 0xFFFFFFFF 表示从未写入 */
static bool _is_empty(const app_board_sys_info_t *info)
{
    return info->magic == 0xFFFFFFFF && info->config_crc == 0xFFFFFFFF;
}

/** @brief magic 匹配后校验 CRC32（覆盖整个结构体除去 crc32 字段自身） */
static bool _is_valid(const app_board_sys_info_t *info)
{
    if (info->magic != APP_BOARD_NET_CFG_MAGIC)
        return false;

    size_t len     = sizeof(app_board_sys_info_t) - sizeof(info->config_crc);
    uint32_t crc32 = pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)info, len);
    return info->config_crc == crc32;
}

/* ================================================================
 *  读取
 * ================================================================ */

/** @brief 内存映射整记录拷贝（无有效性判定，IAP 读 FWInfo/update_sta 用） */
void app_board_net_cfg_read(app_board_sys_info_t *out)
{
    memcpy(out, (const void *)ADDR_CONFIG_SECTOR, sizeof(*out));
}

/** @brief 读网络配置：记录有效返回 0，无效返回 -1 */
int32_t app_board_net_cfg_get(app_board_net_cfg_t *out)
{
    app_board_sys_info_t info;
    app_board_net_cfg_read(&info);

    if (!_is_valid(&info))
        return -1;

    memcpy(out, &info.net_cfg, sizeof(*out));
    return 0;
}

/* ================================================================
 *  写入（先擦除整扇区，再逐 word 编程）
 * ================================================================ */

static int32_t _erase(void)
{
    return dev_storage_erase(_storage(), 0, 0);
}

static int32_t _write(const app_board_sys_info_t *info)
{
    return dev_storage_write(_storage(), 0, (uint8_t *)info, sizeof(*info));
}

/**
 * @brief 同步设备 IP/掩码/网关/端口 到内部 Flash（读-改-net_cfg-写）
 *
 * 分支语义（定案 Q1/Q2/Q3 先行修复 + 方案 1 细化守卫，2026-08-14）：
 *  - 空扇区：完整初始化，镜像老 Bootloader Init_Config_Info 首次上电语义
 *    （magic + update_sta=updated + app_info 全 0 且 crc32=0xFFFFFFFF + net_cfg + CRC32），
 *    确保 Bootloader/Recovery 能读到该 IP。
 *  - 损坏记录（magic 无效或 CRC 错）：按空扇区处理，完整初始化后写新配置（自愈）。
 *    Bootloader 写记录永远是完整结构，不存在「magic 无效的升级中间态」，
 *    损坏记录无保护价值，覆盖它使 0AH/4B02 可自愈。
 *  - 有效记录且 update_sta != updated（升级中间态，Bootloader 条件 A/B 写入的
 *    强制升级/崩溃过频记录）：拒绝覆盖，返回 -1。判定独立于损坏判定——
 *    magic/CRC 无效的中间态不存在，损坏覆盖不会误伤升级流程。
 *  - 有效记录且 update_sta == updated：仅更新 net_cfg（ip/mask/gw/port），其余字段
 *    原样保留；与现扇区 net_cfg 完全一致（含 port）时跳过擦写（Q3：不做磨损均衡）。
 *
 * @return 0 成功（含同值跳过）；<0 失败（-1 = 升级中间态不覆盖，其余为擦/写错误码）
 */
int32_t app_board_net_cfg_update(const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4], uint32_t port)
{
    app_board_sys_info_t info;
    app_board_net_cfg_read(&info);

    /* Q3：写前快照原 net_cfg，用于同值跳过比较 */
    app_board_net_cfg_t old_cfg;
    memcpy(&old_cfg, &info.net_cfg, sizeof(old_cfg));

    bool empty = _is_empty(&info);
    bool valid = !empty && _is_valid(&info);

    /* 升级中间态保护：magic/CRC 有效但 update_sta != updated 的记录
     * （Bootloader 条件 A/B 写入，等待其条件 D 判定用），主固件不得覆盖 */
    if (valid && info.update_sta != APP_BOARD_UPDATED)
        return -1;

    if (!valid) {
        /* 空/损坏扇区：完整初始化，与老 Bootloader Init_Config_Info 首次上电写入逐字节同构。
         * 注意：update_sta 必须为 updated（APP_BOARD_UPDATED），写 FAILED 会被 Bootloader
         * 判 App 损坏 */
        info.magic      = APP_BOARD_NET_CFG_MAGIC;
        info.update_sta = APP_BOARD_UPDATED;
        memset(&info.app_info, 0, sizeof(info.app_info));
        info.app_info.crc32 = 0xFFFFFFFF;
    }

    memcpy(info.net_cfg.ip, ip, 4);
    memcpy(info.net_cfg.mask, mask, 4);
    memcpy(info.net_cfg.gw, gw, 4);
    info.net_cfg.port = port;

    /* Q3：net_cfg（含 port）与现扇区一致则跳过擦写，返回成功（不做磨损均衡）。
     * 条件用 valid 而非 !empty：空/损坏时 old_cfg 为无效快照，必须走写路径 */
    if (valid && memcmp(&info.net_cfg, &old_cfg, sizeof(old_cfg)) == 0)
        return 0;

    info.config_crc = pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)&info,
                                    sizeof(app_board_sys_info_t) - sizeof(info.config_crc));

    int32_t ret = _erase();
    if (ret < 0)
        return ret;
    ret = _write(&info);
    return ret < 0 ? ret : 0;
}