/**
 * @file    app_ldi_int_cfg.c
 * @brief   LDI 配置的内部 Flash 槽式持久化实现（Sector 11 追加日志）
 *
 * 本文件实现契约 ①②③（见 app_ldi_int_cfg.h 文件头）。
 * 详见 doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md §17
 */

#include "app_ldi_int_cfg.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "dev_flash_int.h"
#include "initcall.h"
#include "pl_crc.h"

/* ---- Sector 11 存储实例：整扇区归 LDI 配置日志独占 ---- */
static dev_flash_int_t g_ldi_int_cfg_flash = {
    .me        = {.capacity = APP_LDI_INT_CFG_SIZE},
    .base_addr = APP_LDI_INT_CFG_ADDR,
    .sector    = PL_FLASH_SECTOR_11,
};

/* ---- ops 绑定（hw_dev_initcall，与 app_board_net_cfg 同款） ---- */
static void _app_ldi_int_cfg_storage_init(void)
{
    g_ldi_int_cfg_flash.me.ops = &flash_int_ops;
}
hw_dev_initcall(_app_ldi_int_cfg_storage_init);

/* 保存重入保护：0AH/0BH（ldi_handle_task）与上电自愈（init_task）可能分属不同任务，
 * 而 Flash 解锁→编程→上锁区间不可交叉，否则擦写序列互相打断。 */
static volatile uint8_t s_saving;

static bool _save_enter(void)
{
    bool ok = false;
    taskENTER_CRITICAL();
    if (s_saving == 0U) {
        s_saving = 1U;
        ok       = true;
    }
    taskEXIT_CRITICAL();
    return ok;
}

static void _save_leave(void)
{
    taskENTER_CRITICAL();
    s_saving = 0U;
    taskEXIT_CRITICAL();
}

/* ================================================================
 *  内部辅助（纯内存映射读，不经 dev_storage 虚表）
 * ================================================================ */

/** @brief 槽号 → 记录指针（基址与槽长均 4 字节对齐；内部 Flash 可直接读） */
static const app_ldi_int_cfg_record_t *_slot(uint32_t idx)
{
    return (const app_ldi_int_cfg_record_t *)(uintptr_t)(APP_LDI_INT_CFG_ADDR +
                                                        idx * APP_LDI_INT_CFG_SLOT_SIZE);
}

/** @brief 空槽判定：整槽 256B 全 0xFF（擦除态） */
/* 必须整槽判空：只查 magic 会把「magic 未写成但槽内已有残留」的半写槽误判为空槽，
 * 再次编程即叠加写坏，破坏「掉电只毁正在写的那个槽」的追加语义。 */
static bool _slot_blank(uint32_t idx)
{
    const volatile uint8_t *p =
        (const volatile uint8_t *)(uintptr_t)(APP_LDI_INT_CFG_ADDR + idx * APP_LDI_INT_CFG_SLOT_SIZE);

    for (uint32_t i = 0; i < APP_LDI_INT_CFG_SLOT_SIZE; i++) {
        if (p[i] != 0xFFU)
            return false;
    }
    return true;
}

/** @brief 记录校验：magic + version + CRC32（覆盖 [0, offsetof(crc32))） */
static bool _rec_valid(const app_ldi_int_cfg_record_t *rec)
{
    if (rec->magic != APP_LDI_INT_CFG_MAGIC)
        return false;
    if (rec->version != APP_LDI_INT_CFG_VERSION)
        return false;

    uint32_t crc = pl_crc32_calc(pl_crc_get_handle(), (const uint8_t *)rec,
                                 (uint32_t)offsetof(app_ldi_int_cfg_record_t, crc32));
    return rec->crc32 == crc;
}

/* ================================================================
 *  实现
 * ================================================================ */

bool app_ldi_int_cfg_load(app_flash_ldi_cfg_info_t *out)
{
    if (out == nullptr)
        return false;

    /* 正序扫描全部槽并记住最后一个合法记录：追加语义下最新记录一定在最后，
     * 半写/损坏槽被 magic+version+CRC 挡下，自然回退到上一槽。 */
    const app_ldi_int_cfg_record_t *last = nullptr;
    for (uint32_t i = 0; i < APP_LDI_INT_CFG_SLOT_COUNT; i++) {
        const app_ldi_int_cfg_record_t *rec = _slot(i);
        if (_rec_valid(rec))
            last = rec;
    }

    if (last == nullptr) {
        memset(out, 0, sizeof(*out));
        return false;
    }

    memcpy(out, &last->cfg, sizeof(*out));
    return true;
}

int32_t app_ldi_int_cfg_save(const app_flash_ldi_cfg_info_t *in)
{
    /* 组包用静态 .bss：192B 记录不落任务栈（ldi_handle_task 栈仅 1KB）。
     * 内部 Flash 无 DMA 约束，但仍不放栈以免挤压既有余量。 */
    static app_ldi_int_cfg_record_t s_out;

    if (in == nullptr)
        return -1;
    if (!_save_enter())
        return -1;

    memset(&s_out, 0, sizeof(s_out));
    s_out.magic   = APP_LDI_INT_CFG_MAGIC;
    s_out.version = APP_LDI_INT_CFG_VERSION;
    memcpy(&s_out.cfg, in, sizeof(s_out.cfg));
    s_out.crc32 = pl_crc32_calc(pl_crc_get_handle(), (const uint8_t *)&s_out,
                                (uint32_t)offsetof(app_ldi_int_cfg_record_t, crc32));

    /* 正序找第一个空槽；全满则整扇区擦除后从槽 0 重来（1~2s 总线停顿，出厂低频操作） */
    uint32_t idx  = 0;
    bool found    = false;
    for (idx = 0; idx < APP_LDI_INT_CFG_SLOT_COUNT; idx++) {
        if (_slot_blank(idx)) {
            found = true;
            break;
        }
    }

    if (!found) {
        if (dev_storage_erase(&g_ldi_int_cfg_flash.me, 0, 0) < 0) {
            _save_leave();
            return -2;
        }
        idx = 0;
    }

    if (dev_storage_write(&g_ldi_int_cfg_flash.me, idx * APP_LDI_INT_CFG_SLOT_SIZE,
                          (const uint8_t *)&s_out, sizeof(s_out)) < 0) {
        _save_leave();
        return -3;
    }

    /* 写后回读校验：dev_flash_int 的 _write 不上报单字编程结果（掉压 / 写保护只能靠回读
     * 暴露）。HAL 编程结束会 flush ART 缓存，故此处读到的是 Flash 实际内容；失败不重试，
     * 由上位机重发 0AH/0BH 兜底（与 Sector1 同口径）。 */
    if (!_rec_valid(_slot(idx))) {
        _save_leave();
        return -3;
    }

    _save_leave();
    return 0;
}
