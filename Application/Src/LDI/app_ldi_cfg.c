#include <stddef.h>
#include <string.h>
#include "app_ldi_cfg.h"

#include "pl_crc.h"
#include "dev_w25qxx.h"
#include "initcall.h"

/* ================================================================
 *  LDI 配置在 W25Qxx 的存储布局（最后 4KB 扇区，避免与字库冲突）
 *
 *  4KB 扇区切成 APP_FLASH_LDI_SLOT_COUNT(16) 个 APP_FLASH_LDI_SLOT_SIZE(256B) 槽，
 *  每条记录（magic + cfg + CRC32，11 模块 = 176B）占一个槽：
 *    - 保存 = 顺序追加到第一个「空槽」（magic 全 0xFF），**不擦扇区**；
 *    - 槽写满（或无空槽）才擦整扇区、回到槽 0；
 *    - 装载 = 倒序扫描，第一个 magic+CRC 合法的槽即最新记录。
 *  追加语义下，单次保存中途掉电最多毁掉「正在写的那个槽」，之前的旧记录仍在；
 *  早期单槽实现（擦整扇区再写）掉电即整条记录报废，本布局消除了该窗口。
 *  v1 迁移：旧固件记录（6 模块 / 116B）无版本号，magic 相同但 CRC 覆盖面不同，
 *  故按「当前布局 CRC 试探 → v1 布局 CRC 试探」两步识别，v1 命中即搬运字段并
 *  以当前布局重写（新代码只写当前布局，v1 布局仅用于读取）。
 *  所有写入都做回读校验（memcmp 整条记录），失败返回 <0，调用方可如实应答。
 * ================================================================ */

/* ---- LDI 配置在 W25Qxx 的存储地址（最后一个 4KB 扇区，避免与字库冲突） ---- */
static uint32_t s_ldi_base;

/* ---- 初始化（sw_dev_initcall: 确保 dev_w25qxx 已初始化） ---- */
static void _app_flash_ldi_storage_init(void)
{
    dev_storage_t *w25 = dev_w25qxx_get();
    s_ldi_base         = dev_storage_capacity(w25) - 4096;
}
sw_dev_initcall(_app_flash_ldi_storage_init);

dev_storage_t *app_flash_ldi_get_storage(void)
{
    return dev_w25qxx_get();
}

/* ================================================================
 *  内部辅助（只对调用方已读入的缓冲做判定，不自带 SPI 操作）
 * ================================================================ */

/** @brief 槽号 → W25 绝对地址 */
static uint32_t _slot_addr(uint32_t idx)
{
    return s_ldi_base + idx * APP_FLASH_LDI_SLOT_SIZE;
}

/** @brief 空槽判定：magic 前 4B 全 0xFF（擦除态） */
static bool _slot_blank(const uint8_t *buf)
{
    return buf[0] == 0xFF && buf[1] == 0xFF && buf[2] == 0xFF && buf[3] == 0xFF;
}

/** @brief 当前布局记录校验：magic + CRC32（覆盖 [0, offsetof(crc32))） */
static bool _rec_valid_cur(const uint8_t *buf)
{
    const app_flash_ldi_record_t *rec = (const app_flash_ldi_record_t *)buf;
    if (rec->magic != APP_FLASH_LDI_MAGIC)
        return false;
    uint32_t crc = pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)rec,
                                 (uint32_t)offsetof(app_flash_ldi_record_t, crc32));
    return rec->crc32 == crc;
}

/** @brief v1 布局（6 模块 / 116B）记录校验：magic + CRC32（v1 覆盖面） */
static bool _rec_valid_v1(const uint8_t *buf)
{
    const app_flash_ldi_record_v1_t *rec = (const app_flash_ldi_record_v1_t *)buf;
    if (rec->magic != APP_FLASH_LDI_MAGIC)
        return false;
    uint32_t crc = pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)rec,
                                 (uint32_t)offsetof(app_flash_ldi_record_v1_t, crc32));
    return rec->crc32 == crc;
}

/* ================================================================
 *  实现
 * ================================================================ */

/**
 * @brief 判断 Flash 配置项是否为空（或 0xFF 默认值）
 */
bool app_flash_ldi_is_config_empty(volatile const app_flash_ldi_record_t *rec)
{
    if (rec->magic == 0xFFFFFFFF && rec->crc32 == 0xFFFFFFFF)
        return true;
    return false;
}

/**
 * @brief 验证 Flash 配置校验值 (magic + CRC32)
 */
bool app_flash_ldi_is_config_valid(volatile const app_flash_ldi_record_t *rec)
{
    if (rec->magic != APP_FLASH_LDI_MAGIC)
        return false;

    /* CRC 校验覆盖 magic + 配置，不包含 crc32 自身（覆盖面 = offsetof(crc32)） */
    uint32_t crc = pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)rec,
                                 (uint32_t)offsetof(app_flash_ldi_record_t, crc32));
    if (rec->crc32 != crc)
        return false;

    return true;
}

/**
 * @brief 擦除 W25Qxx 中 LDI 配置所在扇区（最后一个 4KB 扇区）
 */
int32_t app_flash_ldi_erase_config(void)
{
    return dev_storage_erase(app_flash_ldi_get_storage(), s_ldi_base, 4096);
}

/**
 * @brief 将 app_flash_ldi_record_t 写入 W25Qxx（固定落槽 0，追加写请用 save_config）
 */
int32_t app_flash_ldi_write_config(app_flash_ldi_record_t *rec)
{
    return dev_storage_write(app_flash_ldi_get_storage(), _slot_addr(0), (uint8_t *)rec, sizeof(*rec));
}

/**
 * @brief 保存 LDI 配置信息（顺序追加到第一个空槽 + 回读校验）
 *
 * 关于 dev_w25qxx._write：其实现为「读整扇区 → 目标区间若非 0xFF 则擦扇区 →
 * 回写整 4KB」。往空槽写时目标区间全 0xFF，不触发擦除，且回写时把其它槽的
 * 原值原样收回，因此「不擦扇区、追加写一个槽」在本驱动上是安全的（本文件
 * 依赖该行为，不改 dev_w25qxx.c）。
 *
 * @return 0 成功；<0 擦除 / 写入 / 回读校验失败
 */
int32_t app_flash_ldi_save_config(app_flash_ldi_cfg_info_t *info)
{
    /* 组包用静态缓冲：176B 记录不落任务栈（避免深栈），s_out/s_scan 均在普通
     * .bss，可作 SPI DMA 目的（CCM/栈均不可靠作 DMA 目的） */
    static app_flash_ldi_record_t s_out;
    /* aligned(4)：s_scan 会被强转为 app_flash_ldi_record_t* 读 magic/crc32（uint32_t），
     * 未对齐的指针转换在 C 标准下属 UB，-O3 下不可依赖 Cortex-M4 的硬件非对齐容忍 */
    static uint8_t s_scan[APP_FLASH_LDI_SLOT_SIZE] __attribute__((aligned(4)));
    dev_storage_t *w25 = app_flash_ldi_get_storage();

    memset(&s_out, 0, sizeof(s_out));
    s_out.magic = APP_FLASH_LDI_MAGIC;
    memcpy(&s_out.cfg, info, sizeof(s_out.cfg));
    s_out.crc32 = pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)&s_out,
                                (uint32_t)offsetof(app_flash_ldi_record_t, crc32));

    /* 正序找第一个空槽；全满（或读失败）则擦扇区后从槽 0 重来 */
    uint32_t idx  = 0;
    bool found    = false;
    for (idx = 0; idx < APP_FLASH_LDI_SLOT_COUNT; idx++) {
        memset(s_scan, 0, sizeof(s_scan));
        /* 读失败直接报错返回：不能把「读不到」当成「无空槽」——那会走到擦扇区，
         * 用一次瞬时 SPI 故障换掉全部 15 条历史记录 */
        if (dev_storage_read(w25, _slot_addr(idx), s_scan, sizeof(s_scan)) < 0)
            return -1;
        if (_slot_blank(s_scan)) {
            found = true;
            break;
        }
    }
    if (!found) {
        int32_t ret = app_flash_ldi_erase_config();
        if (ret < 0)
            return ret;
        idx = 0;
    }

    /* 写目标槽（空槽 ⇒ 驱动不触发扇区擦除，其它槽原值原样保留） */
    int32_t ret = dev_storage_write(w25, _slot_addr(idx), (uint8_t *)&s_out, sizeof(s_out));
    if (ret < 0)
        return ret;

    /* 回读校验：整条记录逐字节比对，防写失败 / 半写 / 地址错位静默成功 */
    memset(s_scan, 0, sizeof(s_scan));
    if (dev_storage_read(w25, _slot_addr(idx), s_scan, sizeof(s_scan)) < 0)
        return -1;
    if (memcmp(s_scan, &s_out, sizeof(s_out)) != 0)
        return -1;

    return 0;
}

/**
 * @brief 从 W25Qxx 加载 LDI 配置信息
 *
 * 倒序扫描槽（最新在最后）：先按当前布局校验，再按 v1 布局校验（兼容迁移）。
 * 单个槽读失败只跳过该槽，不中断扫描。
 *
 * @param info  输出参数，有效配置写入此处
 * @return true  读取到有效配置（含 v1 迁移成功）
 * @return false 配置为空/校验失败/读取错误
 */
bool app_flash_ldi_load_config(app_flash_ldi_cfg_info_t *info)
{
    /* 静态 SRAM：禁止用任务栈缓冲做 SPI DMA 目的（CCM/栈均不可靠作 DMA 目的）。
     * aligned(4)：s_buf 会被强转为 record 指针读 uint32_t/uint16_t 成员，理由同 save_config。 */
    static uint8_t s_buf[APP_FLASH_LDI_SLOT_SIZE] __attribute__((aligned(4)));
    dev_storage_t *w25 = app_flash_ldi_get_storage();

    /* ---- 第 1 遍：当前布局（倒序，第一个合法槽即最新） ---- */
    for (int32_t i = (int32_t)APP_FLASH_LDI_SLOT_COUNT - 1; i >= 0; i--) {
        memset(s_buf, 0, sizeof(s_buf));
        if (dev_storage_read(w25, _slot_addr((uint32_t)i), s_buf,
                             sizeof(app_flash_ldi_record_t)) < 0)
            continue;
        if (!_rec_valid_cur(s_buf))
            continue;
        memcpy(info, &((const app_flash_ldi_record_t *)s_buf)->cfg, sizeof(*info));
        return true;
    }

    /* ---- 第 2 遍：v1 布局（旧固件记录，识别后迁移并立即以新布局写回） ---- */
    for (int32_t i = (int32_t)APP_FLASH_LDI_SLOT_COUNT - 1; i >= 0; i--) {
        memset(s_buf, 0, sizeof(s_buf));
        if (dev_storage_read(w25, _slot_addr((uint32_t)i), s_buf,
                             sizeof(app_flash_ldi_record_v1_t)) < 0)
            continue;
        if (!_rec_valid_v1(s_buf))
            continue;

        const app_flash_ldi_cfg_info_v1_t *old = &((const app_flash_ldi_record_v1_t *)s_buf)->cfg;
        memset(info, 0, sizeof(*info));
        memcpy(info->device_ip, old->device_ip, sizeof(info->device_ip));
        info->device_port = old->device_port;
        memcpy(info->host_ip, old->host_ip, sizeof(info->host_ip));
        info->host_port = old->host_port;
        memcpy(info->netmask, old->netmask, sizeof(info->netmask));
        memcpy(info->gateway, old->gateway, sizeof(info->gateway));
        memcpy(info->lane_hex, old->lane_hex, sizeof(info->lane_hex));
        memcpy(info->cert, old->cert, sizeof(info->cert));
        info->module_count = old->module_count;
        /* modules 只搬 v1.module_count 项（上限钳到当前容量），其余保持清零 */
        uint8_t n = old->module_count;
        if (n > APP_FLASH_LDI_V1_MAX_MODULES)
            n = APP_FLASH_LDI_V1_MAX_MODULES;
        if (n > APP_FLASH_LDI_MAX_MODULES)
            n = APP_FLASH_LDI_MAX_MODULES;
        memcpy(info->modules, old->modules, (size_t)n * sizeof(old->modules[0]));

        /* 立即以当前布局写回（追加到空槽，旧 v1 记录不被破坏）；写失败也让运行时
         * 先用上迁移结果，下次保存会再尝试 */
        (void)app_flash_ldi_save_config(info);
        return true;
    }

    memset(info, 0, sizeof(*info));
    return false;
}