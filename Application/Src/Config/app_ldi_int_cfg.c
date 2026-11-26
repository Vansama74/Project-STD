/**
 * @file    app_ldi_int_cfg.c
 * @brief   LDI 配置持久化：内部 Sector 11 + W25 末扇区 跨介质双库乒乓
 *
 * 本文件实现契约 ①②③④（见 app_ldi_int_cfg.h 文件头）。
 * 详见 doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md §18
 */

#include "app_ldi_int_cfg.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "dev_flash_int.h"
#include "dev_storage.h"
#include "dev_w25qxx.h"
#include "initcall.h"
#include "pl_crc.h"

/* 布局不变量（改前须复算，越限后果与判据见 doc/07 02 §18.1；业务代码不写断言，
 * doc/18_注释规范.md §2 第 10 条）：v2 记录 ≤ 256B 槽宽且 crc32 偏移 4 对齐（现 196B/192）；
 * v1 记录同（现 192B/188）。 */

/* ---- 库 0：内部 Sector 11 存储实例（整扇区归 LDI 配置日志独占） ---- */
static dev_flash_int_t g_ldi_int_cfg_flash = {
    .me        = {.capacity = APP_LDI_INT_CFG_SIZE},
    .base_addr = APP_LDI_INT_CFG_ADDR,
    .sector    = PL_FLASH_SECTOR_11,
};

/* ---- 库 1：W25Qxx 末 4KB 扇区（16 槽，与字库共片不冲突：只取末扇区） ---- */
static uint32_t s_w25_ldi_base;

/* ---- ops 绑定（hw_dev_initcall，与 app_board_net_cfg 同款） ---- */
static void _app_ldi_int_cfg_storage_init(void)
{
    g_ldi_int_cfg_flash.me.ops = &flash_int_ops;
}
hw_dev_initcall(_app_ldi_int_cfg_storage_init);

/* W25 库基址按容量运行时计算（sw_dev_initcall：确保 dev_w25qxx 已初始化）。
 * 容量读取失败时基址落为 0xFFFFF000 域，后续读写恒失败 ⇒ 该库静默失效，
 * 内部库照常工作（乒乓退化为仅内部库，擦除窗口回到旧行为，但设备可用）。 */
static void _app_ldi_int_cfg_w25_init(void)
{
    dev_storage_t *w25 = dev_w25qxx_get();
    s_w25_ldi_base     = dev_storage_capacity(w25) - 4096U;
}
sw_dev_initcall(_app_ldi_int_cfg_w25_init);

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

/** @brief 内部库槽号 → 记录指针（基址与槽长均 4 字节对齐；内部 Flash 可直接读） */
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

/** @brief v2 记录校验：magic + version + CRC32（覆盖 [0, offsetof(crc32))） */
static bool _rec_valid_v2(const app_ldi_int_cfg_record_t *rec)
{
    if (rec->magic != APP_LDI_INT_CFG_MAGIC)
        return false;
    if (rec->version != APP_LDI_INT_CFG_VERSION)
        return false;

    uint32_t crc = pl_crc32_calc(pl_crc_get_handle(), (const uint8_t *)rec,
                                 (uint32_t)offsetof(app_ldi_int_cfg_record_t, crc32));
    return rec->crc32 == crc;
}

/** @brief v1 记录校验（无 seq 字段，仅装载识别，视为 seq=0） */
static bool _rec_valid_v1(const app_ldi_int_cfg_record_v1_t *rec)
{
    if (rec->magic != APP_LDI_INT_CFG_MAGIC)
        return false;
    if (rec->version != 1U)
        return false;

    uint32_t crc = pl_crc32_calc(pl_crc_get_handle(), (const uint8_t *)rec,
                                 (uint32_t)offsetof(app_ldi_int_cfg_record_v1_t, crc32));
    return rec->crc32 == crc;
}

/**
 * @brief 单槽校验（版本字段分流）：v2 → seq 取自记录；v1 → seq=0；其余/损坏 → 无效
 *
 * @return true 且 *seq 有效；false 记录不可用（*seq 未定义）
 */
static bool _rec_check(const app_ldi_int_cfg_record_t *rec, uint32_t *seq)
{
    if (_rec_valid_v2(rec)) {
        *seq = rec->seq;
        return true;
    }
    /* v1 记录与 v2 共用槽宽/魔数，仅缺 seq 字段，按 seq=0 参与排序 */
    if (_rec_valid_v1((const app_ldi_int_cfg_record_v1_t *)rec)) {
        *seq = 0U;
        return true;
    }
    return false;
}

/* ================================================================
 *  W25 库槽读写（SPI；缓冲静态落 SRAM，可作 DMA 目的）
 * ================================================================ */

/** @brief W25 库槽号 → 绝对地址 */
static uint32_t _w25_slot_addr(uint32_t idx)
{
    return s_w25_ldi_base + idx * APP_LDI_W25_SLOT_SIZE;
}

/** @brief 读 W25 库单槽到 s_buf，返回 0 成功 / <0 读取失败 */
static int32_t _w25_read_slot(uint8_t *buf, uint32_t idx)
{
    return dev_storage_read(dev_w25qxx_get(), _w25_slot_addr(idx), buf,
                            APP_LDI_W25_SLOT_SIZE);
}

/** @brief W25 库空槽判定：整槽 256B 全 0xFF（语义同内部库） */
static bool _w25_slot_blank(uint8_t *buf)
{
    for (uint32_t i = 0; i < APP_LDI_W25_SLOT_SIZE; i++) {
        if (buf[i] != 0xFFU)
            return false;
    }
    return true;
}

/* ================================================================
 *  双库扫描（装载 / 最大序号 / 空槽定位共用）
 * ================================================================ */

/* 库标记 */
#define BANK_INT (0U) /**< 内部 Sector 11 */
#define BANK_W25 (1U) /**< W25 末扇区 */

/**
 * @brief 双库全局扫描：取 seq 最大且合法记录所在库与槽号
 *
 * 内部库 512 槽（v2/v1 皆认）→ W25 库 16 槽（仅 v2；旧格式魔数不同自然跳过）。
 * seq 相等时后扫描者胜（仅 v1 记录可能出现并列，后扫描 = 更大槽号 = 更晚写入，
 * 维持旧版「最后一槽即最新」语义）。W25 槽读失败跳过，不中断扫描。
 *
 * @param best_seq  输出：最大有效 seq（无有效记录时 = 0）
 * @param best_bank 输出：最大 seq 记录所在库（无有效记录时 = BANK_INT）
 * @param best_slot 输出：最大 seq 记录槽号（无有效记录时未定义）
 * @return true = 存在有效记录；false = 两库皆无
 */
static bool _scan_best(uint32_t *best_seq, uint8_t *best_bank, uint32_t *best_slot)
{
    static uint8_t s_buf[APP_LDI_W25_SLOT_SIZE] __attribute__((aligned(4)));

    uint32_t b_seq = 0U;
    bool have      = false;
    uint8_t b_bank = BANK_INT;
    uint32_t b_slot = 0U;

    for (uint32_t i = 0; i < APP_LDI_INT_CFG_SLOT_COUNT; i++) {
        uint32_t seq;
        if (_rec_check(_slot(i), &seq) && (!have || seq >= b_seq)) {
            b_seq  = seq;
            b_bank = BANK_INT;
            b_slot = i;
            have   = true;
        }
    }

    for (uint32_t i = 0; i < APP_LDI_W25_SLOT_COUNT; i++) {
        memset(s_buf, 0, sizeof(s_buf));
        if (_w25_read_slot(s_buf, i) < 0)
            continue;
        uint32_t seq;
        if (_rec_check((const app_ldi_int_cfg_record_t *)s_buf, &seq) &&
            (!have || seq >= b_seq)) {
            b_seq  = seq;
            b_bank = BANK_W25;
            b_slot = i;
            have   = true;
        }
    }

    if (!have) {
        b_bank = BANK_INT;
        b_slot = 0U;
    }
    *best_seq  = b_seq;
    *best_bank = b_bank;
    *best_slot = b_slot;
    return have;
}

/** @brief 在活动库找第一个空槽；找到返回 true 并给出槽号 */
static bool _find_blank(uint8_t bank, uint32_t *idx)
{
    static uint8_t s_buf[APP_LDI_W25_SLOT_SIZE] __attribute__((aligned(4)));

    if (bank == BANK_INT) {
        for (uint32_t i = 0; i < APP_LDI_INT_CFG_SLOT_COUNT; i++) {
            if (_slot_blank(i)) {
                *idx = i;
                return true;
            }
        }
        return false;
    }

    for (uint32_t i = 0; i < APP_LDI_W25_SLOT_COUNT; i++) {
        memset(s_buf, 0, sizeof(s_buf));
        if (_w25_read_slot(s_buf, i) < 0)
            continue; /* 读失败按「非空」保守跳过（不覆盖数据） */
        if (_w25_slot_blank(s_buf)) {
            *idx = i;
            return true;
        }
    }
    return false;
}

/** @brief 写一条记录到指定库指定槽（库内容须为擦除态），返回 0 成功 */
static int32_t _write_record(uint8_t bank, uint32_t idx, const app_ldi_int_cfg_record_t *rec)
{
    if (bank == BANK_INT) {
        return dev_storage_write(&g_ldi_int_cfg_flash.me, idx * APP_LDI_INT_CFG_SLOT_SIZE,
                                 (const uint8_t *)rec, sizeof(*rec));
    }
    return dev_storage_write(dev_w25qxx_get(), _w25_slot_addr(idx),
                             (const uint8_t *)rec, sizeof(*rec));
}

/**
 * @brief 写后回读校验
 *
 * 内部库：dev_flash_int 的 _write 不上报单字编程结果（掉压 / 写保护只能靠回读
 * 暴露）。HAL 编程结束会 flush ART 缓存，故此处读到的是 Flash 实际内容。
 * W25 库：驱动读路径有 DMA 完成标志竞态兜底（Clear 后启动 + 10ms 超时），
 * 回读偶发失败不重写（重写 = 叠加写坏），改为重读重试 3 次。
 *
 * @return true = 回读 CRC 合法；false = 记录不可信
 */
static bool _verify_record(uint8_t bank, uint32_t idx)
{
    static uint8_t s_buf[APP_LDI_W25_SLOT_SIZE] __attribute__((aligned(4)));
    const uint32_t retries = (bank == BANK_W25) ? 3U : 1U;

    for (uint32_t n = 0; n < retries; n++) {
        const app_ldi_int_cfg_record_t *rec;
        if (bank == BANK_INT) {
            rec = _slot(idx);
        } else {
            memset(s_buf, 0, sizeof(s_buf));
            if (_w25_read_slot(s_buf, idx) < 0)
                continue;
            rec = (const app_ldi_int_cfg_record_t *)s_buf;
        }
        if (_rec_valid_v2(rec))
            return true;
    }
    return false;
}

/** @brief 擦除指定库整库；返回 0 成功 */
static int32_t _erase_bank(uint8_t bank)
{
    if (bank == BANK_INT) {
        return dev_storage_erase(&g_ldi_int_cfg_flash.me, 0, 0);
    }
    return dev_storage_erase(dev_w25qxx_get(), s_w25_ldi_base, 4096U);
}

/* ================================================================
 *  实现
 * ================================================================ */

bool app_ldi_int_cfg_load(app_flash_ldi_cfg_info_t *out)
{
    static uint8_t s_buf[APP_LDI_W25_SLOT_SIZE] __attribute__((aligned(4)));

    if (out == nullptr) {
        return false;
    }

    uint32_t b_seq;
    uint8_t b_bank;
    uint32_t b_slot;
    if (!_scan_best(&b_seq, &b_bank, &b_slot)) {
        memset(out, 0, sizeof(*out));
        return false;
    }

    const app_ldi_int_cfg_record_t *rec;
    if (b_bank == BANK_INT) {
        rec = _slot(b_slot);
    } else {
        memset(s_buf, 0, sizeof(s_buf));
        if (_w25_read_slot(s_buf, b_slot) < 0) {
            memset(out, 0, sizeof(*out));
            return false;
        }
        rec = (const app_ldi_int_cfg_record_t *)s_buf;
    }

    memcpy(out, &rec->cfg, sizeof(*out));
    return true;
}

int32_t app_ldi_int_cfg_save(const app_flash_ldi_cfg_info_t *in)
{
    /* 组包用静态 .bss：196B 记录不落任务栈（ldi_handle_task 栈仅 1KB）。
     * 内部 Flash 无 DMA 约束，但仍不放栈以免挤压既有余量。 */
    static app_ldi_int_cfg_record_t s_out;

    if (in == nullptr)
        return -1;
    if (!_save_enter())
        return -1;

    /* seq = 两库现存最大 seq + 1；两库皆空时从 1 起（seq 回绕到 0 的极端情形
     * 忽略：uint32 全量 4.3e9 次保存才触发一次，且回绕只影响一次排序，CRC
     * 仍保证记录可用）。 */
    uint32_t best_seq;
    uint8_t best_bank;
    uint32_t best_slot;
    bool have = _scan_best(&best_seq, &best_bank, &best_slot);

    memset(&s_out, 0, sizeof(s_out));
    s_out.magic   = APP_LDI_INT_CFG_MAGIC;
    s_out.version = APP_LDI_INT_CFG_VERSION;
    s_out.seq     = (have ? best_seq : 0U) + 1U;
    memcpy(&s_out.cfg, in, sizeof(s_out.cfg));
    s_out.crc32 = pl_crc32_calc(pl_crc_get_handle(), (const uint8_t *)&s_out,
                                (uint32_t)offsetof(app_ldi_int_cfg_record_t, crc32));

    uint8_t bank    = have ? best_bank : BANK_INT;
    uint32_t idx    = 0;
    bool found      = false;

    /* 活动库（含最新记录的库）正序找第一个空槽 */
    found = _find_blank(bank, &idx);

    if (!found) {
        /* 活动库写满 → 轮转到对侧库：先擦对侧（内部库 128KB 擦除 = 1~2s 总线停顿，
         * 出厂低频操作；此刻最新记录仍完整保存在活动库，擦除窗口为零）→ 写槽 0
         * → 回读校验。校验失败不切换：活动库原样保留（其最新记录 seq 仍最大，
         * 装载回退到它），对侧库留半写态由 CRC 挡下。 */
        bank = (bank == BANK_INT) ? BANK_W25 : BANK_INT;
        if (_erase_bank(bank) < 0) {
            _save_leave();
            return -2;
        }
        idx = 0;
    }

    if (_write_record(bank, idx, &s_out) < 0) {
        _save_leave();
        return -3;
    }

    if (!_verify_record(bank, idx)) {
        _save_leave();
        return -3;
    }

    _save_leave();
    return 0;
}
