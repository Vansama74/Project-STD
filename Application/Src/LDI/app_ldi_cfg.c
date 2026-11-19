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
 *  每条记录（magic + cfg + CRC32，11 模块 × 13B = 188B）占一个槽：
 *    - 保存 = 顺序追加到第一个「空槽」（magic 全 0xFF），**不擦扇区**；
 *    - 槽写满（或无空槽）才擦整扇区、回到槽 0；
 *    - 装载 = 倒序扫描，第一个 magic+CRC 合法的槽即最新记录。
 *  追加语义下，单次保存中途掉电最多毁掉「正在写的那个槽」，之前的旧记录仍在；
 *  早期单槽实现（擦整扇区再写）掉电即整条记录报废，本布局消除了该窗口。
 *  三级布局识别：历史记录无版本号，magic 相同但 CRC 覆盖面不同，故按
 *  「v3（11 模块 × 13B / 188B 记录）→ v2（11 模块 × 12B / 176B）→ v1（6 模块
 *  116B）」逐级 CRC 试探；v2/v1 命中即搬运字段并以当前布局重写（新代码只写
 *  当前布局，v1/v2 布局仅用于读取）。v2 的 E4H/E6H 模块 vendor[0] 实为 param
 *  （旧 0BH 混排写入），迁移时右移还原（见 _migrate_v2_to_cur）。
 *  保存路径**不做写后回读**：dev_w25qxx._dma_recv 的 10ms 事件等待存在 DMA
 *  完成标志竞态，回读偶发失败会误报命令 01H 但配置实际已写入（2026-11-18
 *  板上实测）；写入完整性由装载路径的 CRC 校验与槽追加语义把关（写坏的槽
 *  自动跳过、回退上一槽）。
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
/* 整槽判空：只查前 4B magic 会把「magic 为 FF 但槽内残留非 FF 的半写槽」误判为空槽，
 * 进而让 dev_w25qxx._write 的读改写路径触发整扇区擦除 + 回写——该窗口掉电即毁掉全部
 * 16 槽历史记录。必须逐字节确认整个槽都是擦除态。 */
static bool _slot_blank(const uint8_t *buf)
{
    for (uint16_t i = 0; i < APP_FLASH_LDI_SLOT_SIZE; i++)
        if (buf[i] != 0xFF)
            return false;
    return true;
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

/** @brief v2 布局（11 模块 / 12B，176B 记录）校验：magic + CRC32（v2 覆盖面） */
static bool _rec_valid_v2(const uint8_t *buf)
{
    const app_flash_ldi_record_v2_t *rec = (const app_flash_ldi_record_v2_t *)buf;
    if (rec->magic != APP_FLASH_LDI_MAGIC)
        return false;
    uint32_t crc = pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)rec,
                                 (uint32_t)offsetof(app_flash_ldi_record_v2_t, crc32));
    return rec->crc32 == crc;
}

/**
 * @brief v2 → 当前布局（v3）迁移：前部网络/身份字段同偏移直接搬，modules 逐项转换
 *
 * v2 → v3 模块迁移：E4H/E6H 的旧记录 vendor[0] 实为 param（0BH 混排写入），
 * 右移还原 param=vendor[0]、vendor[i]=旧vendor[i+1]、vendor[9]=0；其余类型原样搬。
 */
static void _migrate_v2_to_cur(const app_flash_ldi_record_v2_t *old, app_flash_ldi_cfg_info_t *info)
{
    const app_flash_ldi_cfg_info_v2_t *src = &old->cfg;

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
        /* 仅 E4H(E4H=0xE4)/E6H 在协议里带 param 字节，旧记录把它混在 vendor[0] */
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
 * @brief 保存 LDI 配置信息（顺序追加到第一个空槽）
 *
 * 关于 dev_w25qxx._write：其实现为「读整扇区 → 目标区间若非 0xFF 则擦扇区 →
 * 回写整 4KB」。往空槽写时目标区间全 0xFF，不触发擦除，且回写时把其它槽的
 * 原值原样收回，因此「不擦扇区、追加写一个槽」在本驱动上是安全的（本文件
 * 依赖该行为，不改 dev_w25qxx.c）。
 *
 * @return 0 成功；<0 擦除或写入失败
 */
int32_t app_flash_ldi_save_config(app_flash_ldi_cfg_info_t *info)
{
    /* 组包用静态缓冲：188B 记录不落任务栈（避免深栈），s_out/s_scan 均在普通
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
        /* 读失败按「非空」保守跳过该槽（不覆盖数据），不因单次瞬时 SPI 故障
         * 让整个保存失败；16 槽全读失败仍会落到擦除重建分支。 */
        if (dev_storage_read(w25, _slot_addr(idx), s_scan, sizeof(s_scan)) < 0)
            continue;
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

    /* 写目标槽（空槽 ⇒ 驱动不触发扇区擦除，其它槽原值原样保留）。
     * 不做写后回读：见文件头说明（DMA 完成标志竞态会误报失败）。 */
    int32_t ret = dev_storage_write(w25, _slot_addr(idx), (uint8_t *)&s_out, sizeof(s_out));
    if (ret < 0)
        return ret;

    return 0;
}

/**
 * @brief 从 W25Qxx 加载 LDI 配置信息
 *
 * 倒序扫描槽（最新在最后）：依次按当前布局（v3）、v2（11 模块 × 12B）、
 * v1（6 模块 × 12B）校验，旧布局命中即迁移并立即以当前布局写回。
 * 单个槽读失败只跳过该槽，不中断扫描。
 *
 * @param info  输出参数，有效配置写入此处
 * @return true  读取到有效配置（含 v1/v2 迁移成功）
 * @return false 配置为空/校验失败/读取错误
 */
bool app_flash_ldi_load_config(app_flash_ldi_cfg_info_t *info)
{
    /* 静态 SRAM：禁止用任务栈缓冲做 SPI DMA 目的（CCM/栈均不可靠作 DMA 目的）。
     * aligned(4)：s_buf 会被强转为 record 指针读 uint32_t/uint16_t 成员，理由同 save_config。 */
    static uint8_t s_buf[APP_FLASH_LDI_SLOT_SIZE] __attribute__((aligned(4)));
    dev_storage_t *w25 = app_flash_ldi_get_storage();

    /* ---- 第 1 遍：当前布局 v3（倒序，第一个合法槽即最新） ---- */
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

    /* ---- 第 2 遍：v2 布局（11 模块 × 12B，识别后迁移并立即以当前布局写回） ---- */
    for (int32_t i = (int32_t)APP_FLASH_LDI_SLOT_COUNT - 1; i >= 0; i--) {
        memset(s_buf, 0, sizeof(s_buf));
        if (dev_storage_read(w25, _slot_addr((uint32_t)i), s_buf,
                             sizeof(app_flash_ldi_record_v2_t)) < 0)
            continue;
        if (!_rec_valid_v2(s_buf))
            continue;

        _migrate_v2_to_cur((const app_flash_ldi_record_v2_t *)s_buf, info);

        /* 立即以当前布局写回（追加到空槽，旧 v2 记录不被破坏）；写失败也让运行时
         * 先用上迁移结果，下次保存会再尝试 */
        (void)app_flash_ldi_save_config(info);
        return true;
    }

    /* ---- 第 3 遍：v1 布局（旧固件记录，识别后迁移并立即以新布局写回） ---- */
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
        /* modules 只搬 v1.module_count 项（上限钳到当前容量），其余保持清零；
         * v1 时代无 param 字段，模块逐字段搬：param=0、vendor 原样 */
        uint8_t n = old->module_count;
        if (n > APP_FLASH_LDI_V1_MAX_MODULES)
            n = APP_FLASH_LDI_V1_MAX_MODULES;
        if (n > APP_FLASH_LDI_MAX_MODULES)
            n = APP_FLASH_LDI_MAX_MODULES;
        for (uint8_t i = 0; i < n; i++) {
            info->modules[i].device_type  = old->modules[i].device_type;
            info->modules[i].device_index = old->modules[i].device_index;
            info->modules[i].param        = 0U;
            memcpy(info->modules[i].vendor, old->modules[i].vendor,
                   sizeof(info->modules[i].vendor));
        }

        /* 立即以当前布局写回（追加到空槽，旧 v1 记录不被破坏）；写失败也让运行时
         * 先用上迁移结果，下次保存会再尝试 */
        (void)app_flash_ldi_save_config(info);
        return true;
    }

    memset(info, 0, sizeof(*info));
    return false;
}