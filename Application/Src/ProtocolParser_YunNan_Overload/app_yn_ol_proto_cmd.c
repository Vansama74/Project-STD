/**
 * @file    app_yn_ol_proto_cmd.c
 * @brief   云南治超屏协议（YN_1.3.0）命令执行与应答组帧
 *
 * 显示映射：app_render（GBK 直通；'3' 单行按「行号 × 当前字号」定位、先清行再渲染、
 * 超宽截断；'4' 全屏按 X/Y 定位、先整屏清黑、按屏宽自动换行；0x01 全屏纯色）；
 * 行清除（0x42~0x46 / 0x50）按当前字号行高清行，越界行「执行但不落屏」（fill 起点越界
 * 早退——与云南常规行 5 口径一致）；亮度映射 0/'0'/0x00 = 恢复光敏自动、'1'~'8' = 挂起
 * 光敏 + 硬件档恒等映射；灯控映射 dev_io_lane_light / dev_io_flash_light（红优先）；
 * 网络配置走 STD 中立模块 app_board_net_cfg（Sector1 唯一真源，不建私有记录）。
 *
 * 应答帧与请求同帧族（`{` + 命令字 + 长度 + 载荷 + `}`），一律**单播回源通道**
 * （与 GZ_OL 裁决一致：STD channel_send 语义）。
 *
 * 渲染互斥：本模块所有「清屏/清行/渲染/提交」整段持 app_scroll_render_lock，
 * 与安徽动态滚动（app_scroll）渲染串行，消除同屏混合帧黑条；引擎未创建时空操作。
 *
 * 屏体参数（0x49）为**持久化配置**（2026-09-17 用户裁决 Q5）：模块级 12B 记录
 * （magic + version + 字体/字宽 + CRC32，仿 `Application/Src/LDI/app_ldi_cfg.c` 范式）
 * 落在 W25Qxx **独立 4KB 扇区** `capacity - 12288`（选址依据与被占扇区核对见
 * `app_yn_ol_proto.h`「0x49 持久化」段）；`sw_app_initcall` 初始化时装载，`0x49` 执行时
 * 写入；写入失败/记录损坏 → 回退/保持默认并打 RTT 告警，**不阻塞、不重启**。
 * 默认值 = **STD 工程既有默认口径** FONT_16 / FONT_ST（与青海/贵州/云南常规一致——
 * 沿用同一默认，不再按行清除命令数推断；见 doc/15 §3/§8）。
 *
 * 语音相关命令（'7'/'9'）与固定格式显示（'6'）文档明示治超屏不开发 → 本模块不实现。
 *
 * 上电效果（文档「祝您一路平安 + 稍候熄灭」）**不实现**（2026-09-17 用户裁决）：
 * 原 `app_yn_ol_proto_default.c` 与 5s 熄灭惰性任务已删除；上电画面走 STD 默认显示链路。
 */

#include "app_yn_ol_proto_cmd.h"

#include <stdio.h>
#include <string.h>

#include "pl_task_guard.h"

#include "app_board_net_cfg.h"
#include "app_boot.h" /* PROGRAM_CODE（0x02 版本号应答） */
#include "app_diag.h"
#include "app_light_sensor.h"
#include "app_render.h"
#include "app_scroll.h"
#include "cmsis_os2.h"
#include "dev_display.h"
#include "dev_io_ctrl.h"
#include "dev_w25qxx.h"    /* 0x49 持久化（W25Qxx 独立扇区记录） */
#include "pl_crc.h"        /* CRC32（记录校验，与 LDI 记录同源） */
#include "SEGGER_RTT.h"    /* 联调诊断（YN_OL_RTT_DIAG）；置 0 时该头无引用 */
#include "stm32f4xx_hal.h" /* NVIC_SystemReset */

/* ---- 模块运行态（0x49 屏体参数；默认 = STD 工程默认口径，可由 W25Qxx 记录覆盖）---- */
static font_size_t s_yn_ol_font_size = FONT_16;
static font_type_t s_yn_ol_font_type = FONT_ST;

/* ---- 自检运行状态（'2'）---- */
static volatile bool s_yn_ol_selftest_running;
static volatile bool s_yn_ol_selftest_abort;

/* 自检单步时长（文档未定义节奏；500ms/字为推断值，见 doc/15 §8 残余待确认项） */
#define YN_OL_SELFTEST_STEP_MS (500U)
/* 中止检查分片（保证下一帧命令到达后 ≤100ms 内退出，与云南常规自检同策） */
#define YN_OL_SELFTEST_ABORT_SLICE_MS (100U)

/* ---- 联调诊断（临时，只读）----
 * 0x47/0x51：打印端口字段原始序与解析值（自证「高字节在前 BE16」裁决口径）；
 * 0x49：打印字体/字宽参数、生效字号与落盘结果；
 * 上电装载：记录无效/为空 → 打一条回退默认的告警。
 * 一键关闭：YN_OL_RTT_DIAG 置 0（默认跟随总开关 APP_DIAG_BANNER，见 app_diag.h）。 */
#ifndef YN_OL_RTT_DIAG
#define YN_OL_RTT_DIAG (APP_DIAG_BANNER)
#endif

/* ---- 应答组帧 ---- */

/**
 * @brief  构造并发送本协议应答帧（`{` + cmd + len + payload + `}`）。
 * @param  ch           来源通道（单播回源）。
 * @param  cmd          应答命令字。
 * @param  payload      载荷（可为 NULL）。
 * @param  payload_len  载荷字节数（本模块应答最长 14B：0x51 网络配置帧）。
 */
static void _yn_ol_send_frame(channel_t *ch, uint8_t cmd, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t buf[YN_OL_HEAD_LEN + YN_OL_NET_PAYLOAD_LEN + 1U];
    if (payload_len > YN_OL_NET_PAYLOAD_LEN)
        return;

    buf[0] = YN_OL_STX;
    buf[1] = cmd;
    buf[2] = (uint8_t)payload_len;
    if (payload_len > 0U && payload != nullptr)
        memcpy(&buf[YN_OL_DATA_OFFSET], payload, payload_len);
    buf[YN_OL_HEAD_LEN + payload_len] = YN_OL_ETX;

    channel_send(ch, buf, (uint16_t)(YN_OL_HEAD_LEN + payload_len + 1U));
}

/**
 * @brief  读当前网络配置并填入 14B 应答载荷（ip4 + mask4 + gw4 + port2 高字节在前）。
 *
 *  port 字段 = Sector1 `net_cfg.port`（STD 统一「配置功能端口」口径，与 LDI 12H /
 *  IAP 0x01 / GZ_OL 0x70 一致）；`udp_port`（CQ 专有）不参与。
 *  字节序 = **高字节在前 BE16**（`25 38` = 9528）——**已裁决口径**（2026-09-17），
 *  见 `app_yn_ol_proto.h` 文件头「端口字段字节序」。
 *
 * @param  out  14B 输出缓冲。
 * @return true = 读到有效配置；false = 记录无效（调用方回全 0 兜底）。
 */
static bool _yn_ol_net_payload(uint8_t *out)
{
    app_board_net_cfg_t cfg;
    if (app_board_net_cfg_get(&cfg) != 0)
        return false;

    memcpy(&out[0], cfg.ip, 4);
    memcpy(&out[4], cfg.mask, 4);
    memcpy(&out[8], cfg.gw, 4);
    out[12] = (uint8_t)(cfg.port >> 8); /* 高字节在前（见头文件「端口字段字节序」） */
    out[13] = (uint8_t)(cfg.port & 0xFFU);
    return true;
}

/* ================================================================
 *  0x49 屏体参数持久化（W25Qxx 12B 记录；2026-09-17 用户裁决 Q5）
 *
 *  记录：magic(4) + version(2) + font_type(1) + font_size(1) + crc32(4) = 12B
 *  地址：`dev_storage_capacity(w25) - YN_OL_CFG_SECTOR_OFFSET`（capacity - 12288：
 *        倒数第三个 4KB 扇区；LDI 占 capacity-4096、app_render persist 占
 *        capacity-8192，字库数据止于第 1997 扇区 → 本扇区无占用，见头文件）。
 *  语义：记录无效/为空 → 保持默认（FONT_16/FONT_ST）并打告警；写入失败 → 运行态仍生效，
 *        仅掉电不保留 + 告警。**不阻塞、不重启**。
 *
 *  缓冲说明：读写各用 12B 静态 SRAM（与 LDI 记录同范式；读写路径经 dev_w25qxx 内部锁，
 *  写路径为其整扇区读-改-写，本模块不需要自备 4KB 镜像）。
 * ================================================================ */

static yn_ol_screen_cfg_record_t s_yn_ol_cfg_rec; /**< 12B：装载/落盘共用缓冲（SRAM） */

/** @brief 记录所在绝对地址（W25Qxx 倒数第三个 4KB 扇区）。 */
static uint32_t _yn_ol_cfg_addr(dev_storage_t *w25)
{
    return dev_storage_capacity(w25) - YN_OL_CFG_SECTOR_OFFSET;
}

/** @brief 字体/字宽字段是否在协议允许范围内（0~3 / FONT_16/24/32）。 */
static bool _yn_ol_cfg_fields_valid(uint8_t font_type, uint8_t font_size)
{
    if (font_type > (uint8_t)FONT_HT)
        return false;
    return (font_size == (uint8_t)FONT_16 || font_size == (uint8_t)FONT_24 ||
            font_size == (uint8_t)FONT_32);
}

bool yn_ol_screen_cfg_load(void)
{
    dev_storage_t *w25 = dev_w25qxx_get();
    if (w25 == nullptr)
        return false;

    memset(&s_yn_ol_cfg_rec, 0, sizeof(s_yn_ol_cfg_rec));
    const uint32_t addr = _yn_ol_cfg_addr(w25);
    if (dev_storage_read(w25, addr, (uint8_t *)&s_yn_ol_cfg_rec, sizeof(s_yn_ol_cfg_rec)) < 0)
        return false;

    /* 空扇区（全 0xFF）→ 未写过记录：直接静默回默认（首次上电的正常路径，不打告警） */
    if (s_yn_ol_cfg_rec.magic == 0xFFFFFFFFU && s_yn_ol_cfg_rec.crc32 == 0xFFFFFFFFU)
        return false;

    const bool valid =
        (s_yn_ol_cfg_rec.magic == YN_OL_CFG_MAGIC) &&
        (s_yn_ol_cfg_rec.version == YN_OL_CFG_VERSION) &&
        _yn_ol_cfg_fields_valid(s_yn_ol_cfg_rec.font_type, s_yn_ol_cfg_rec.font_size) &&
        (s_yn_ol_cfg_rec.crc32 ==
         pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)&s_yn_ol_cfg_rec,
                       YN_OL_CFG_RECORD_SIZE - sizeof(uint32_t)));

    if (!valid) {
#if YN_OL_RTT_DIAG
        SEGGER_RTT_printf(0,
                          "[yn_ol] 0x49 cfg INVALID at boot (magic=%08x ver=%u type=%u size=%u "
                          "crc=%08x) -> defaults FONT_16/FONT_ST\n",
                          (unsigned)s_yn_ol_cfg_rec.magic, (unsigned)s_yn_ol_cfg_rec.version,
                          (unsigned)s_yn_ol_cfg_rec.font_type, (unsigned)s_yn_ol_cfg_rec.font_size,
                          (unsigned)s_yn_ol_cfg_rec.crc32);
#endif
        return false;
    }

    s_yn_ol_font_type = (font_type_t)s_yn_ol_cfg_rec.font_type;
    s_yn_ol_font_size = (font_size_t)s_yn_ol_cfg_rec.font_size;

#if YN_OL_RTT_DIAG
    SEGGER_RTT_printf(0, "[yn_ol] 0x49 cfg loaded from %u: font_type=%d font_size=%d\n",
                      (unsigned)addr, (int)s_yn_ol_font_type, (int)s_yn_ol_font_size);
#endif
    return true;
}

bool yn_ol_screen_cfg_save(void)
{
    dev_storage_t *w25 = dev_w25qxx_get();
    if (w25 == nullptr)
        return false;

    s_yn_ol_cfg_rec.magic     = YN_OL_CFG_MAGIC;
    s_yn_ol_cfg_rec.version   = YN_OL_CFG_VERSION;
    s_yn_ol_cfg_rec.font_type = (uint8_t)s_yn_ol_font_type;
    s_yn_ol_cfg_rec.font_size = (uint8_t)s_yn_ol_font_size;
    s_yn_ol_cfg_rec.crc32     = pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)&s_yn_ol_cfg_rec,
                                              YN_OL_CFG_RECORD_SIZE - sizeof(uint32_t));

    const int32_t ret = dev_storage_write(w25, _yn_ol_cfg_addr(w25), (const uint8_t *)&s_yn_ol_cfg_rec,
                                          sizeof(s_yn_ol_cfg_rec));
    return ret == 0;
}

/* ---- 颜色/显示基元 ---- */

/** @brief 协议颜色索引（0 红 / 1 绿 / 2 黄）→ 显示颜色枚举。 */
static display_color_t _yn_ol_map_color(uint8_t idx)
{
    switch (idx) {
        case 0: return COLOR_RED;
        case 1: return COLOR_GREEN;
        case 2: return COLOR_YELLOW;
        default: return COLOR_RED; /* 解析已保证 ≤2，此处仅防御 */
    }
}

/**
 * @brief  把第 row 行区域刷黑（**不提交**；调用方统一在渲染后提交或自行提交）。
 * @param  d    显示实例。
 * @param  row  行号 0~5（行高 = 当前生效字号）。
 * @note   行起点越界（row × 字号 ≥ 屏高）时 dev_display_fill 起点越界早退，
 *         即「执行但不落屏」（安全无副作用）。
 */
static void _yn_ol_row_black(dev_display_t *d, uint8_t row)
{
    const uint16_t rowh = (uint16_t)s_yn_ol_font_size;
    dev_display_fill(d, 0, (uint16_t)((uint16_t)row * rowh), d->screen_rows, rowh, COLOR_BLACK);
}

/** @brief 整屏清黑并提交（调用方须已持渲染锁）。 */
static void _yn_ol_clear_screen(dev_display_t *d)
{
    dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK);
    dev_display_commit_frame(d);
}

/* ---- '1' 主机查询 ---- */

/**
 * @brief  执行 '1' 主机查询：回固定「状态正常」应答帧 `7B 31 01 00 7D`。
 * @param  ch  源通道（应答回源）。
 * @return YN_OL_EXEC_OK。
 * @note   文档定义正常/异常两种应答；本设备恒正常（异常应答无触发条件），
 *         与云南常规 '1' 口径一致。
 */
static int _yn_ol_exec_host_query(channel_t *ch)
{
    const uint8_t sta[1] = {0x00U}; /* 00 = 状态正常 */
    _yn_ol_send_frame(ch, (uint8_t)YN_OL_PCMD_HOST_QUERY, sta, sizeof(sta));
    return YN_OL_EXEC_OK;
}

/* ---- '2' 自检 ---- */

/**
 * @brief  自检任务：整屏居中显示数字 1~9 交替（每 500ms 步进），可被下一帧命令打断。
 * @param  argument  未使用。
 * @note   文档原文「固定汉字信息及数字 1～9 交替显示，语音报出示例语音内容」——
 *         治超屏不开发语音（文档开头声明），故仅实现显示部分；「固定汉字信息」
 *         的内容文档未给出 → 本实现只做数字交替（见 doc/15 §8 残余待确认项）。
 *         被打断退出时**不清屏**（屏幕交给打断它的新命令）。
 */
static void _yn_ol_selftest_task(void *argument)
{
    (void)argument;
    dev_display_t *d = dev_display_get();

    for (uint8_t n = 1U; !s_yn_ol_selftest_abort; n = (uint8_t)((n % 9U) + 1U)) {
        if (d != nullptr) {
            const char digit[2] = {(char)('0' + n), '\0'};
            app_scroll_render_lock();
            dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK);
            app_render(&(render_cfg_t){
                .type      = RENDER_TEXT,
                .x         = 0,
                .y         = 0,
                .w         = d->screen_rows,
                .h         = d->screen_cols,
                .style     = &(render_style_t){
                    .h_align   = ALIGN_CENTER,
                    .v_align   = ALIGN_CENTER,
                    .word_wrap = false,
                },
                .color     = COLOR_RED,
                .text      = digit,
                .len       = 1U,
                .font_size = FONT_SELF_ADAPT, /* 自适应：小屏亦不越界 */
                .font_type = s_yn_ol_font_type,
                .text_enc  = FONT_ENC_ASCII,
            });
            app_scroll_render_unlock();
        }

        /* 分片延时：及时响应下一帧命令的中止（≤100ms） */
        for (uint32_t ms = 0U; ms < YN_OL_SELFTEST_STEP_MS && !s_yn_ol_selftest_abort;
             ms += YN_OL_SELFTEST_ABORT_SLICE_MS) {
            osDelay(YN_OL_SELFTEST_ABORT_SLICE_MS);
        }
    }

    s_yn_ol_selftest_running = false;
    s_yn_ol_selftest_abort   = false;
    osThreadExit();
}

/** @brief 启动自检任务（'2'；进行中重复触发忽略——防重入）。 */
static int _yn_ol_exec_self_check(void)
{
    if (s_yn_ol_selftest_running)
        return YN_OL_EXEC_OK; /* 已在自检：忽略重复触发（协议未定义二次语义） */
    s_yn_ol_selftest_running = true; /* 先置标志防重入；任务结束时自清 */
    s_yn_ol_selftest_abort   = false;

    static const osThreadAttr_t s_yn_ol_selftest_attr = {
        .name       = "yn_ol_selftest",
        .stack_size = 256 * 4,
        .priority   = osPriorityNormal,
    };
    /* 判空 + RTT；创建失败回退防重入标志（否则自检永久锁死）。本任务与云南常规
     * 自检同构，同样**保持动态分配**（末尾先清标志再 osThreadExit，复用静态栈有踩踏风险）。 */
    osThreadId_t tid =
        pl_task_create_checked(osThreadNew(_yn_ol_selftest_task, NULL, &s_yn_ol_selftest_attr),
                                "yn_ol_selftest");
    if (tid == nullptr)
        s_yn_ol_selftest_running = false;
    return YN_OL_EXEC_OK;
}

/* ---- '3' 单行 / '4' 全屏 ---- */

/**
 * @brief  执行 '3' 单行显示：先清行再按当前字号渲染文本（GBK 直通、超宽截断）。
 * @param  p  单行显示参数（color/row/text）。
 * @note   行号 '1'~'5'（row 0~4）全按协议接受；行 5 在屏高不足时渲染调用照常发出，
 *         由 dev_display_fill / draw_bitmap 起点越界早退「执行但不落屏」。
 *         app_render 内部自带提交，故清行不单独提交（同帧一并生效）。
 */
static int _yn_ol_exec_one_line(const yn_ol_one_line_t *p)
{
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return YN_OL_EXEC_NO_DISPLAY;

    const uint16_t rowh = (uint16_t)s_yn_ol_font_size;

    app_scroll_render_lock();
    _yn_ol_row_black(d, p->row);
    app_render(&(render_cfg_t){
        .type  = RENDER_TEXT,
        .x     = 0,
        .y     = (uint16_t)((uint16_t)p->row * rowh),
        .w     = d->screen_rows,
        .h     = rowh,
        .style = &(render_style_t){
            .h_align   = ALIGN_LEFT_UP,
            .v_align   = ALIGN_LEFT_UP,
            .word_wrap = false, /* 单行：超出行宽截断 */
        },
        .color     = _yn_ol_map_color(p->color),
        .text      = (const char *)p->text,
        .len       = p->text_len,
        .font_size = s_yn_ol_font_size,
        .font_type = s_yn_ol_font_type,
        .text_enc  = FONT_ENC_GBK,
    });
    app_scroll_render_unlock();
    /* 「执行但不落屏」（行起点越界时 fill/draw 早退）不算失败：协议语义正常，
     * 诊断行会打印 y/屏高并标注 OFF-SCREEN 供现场判读。 */
    return (int)YN_OL_EXEC_OK;
}

/**
 * @brief  执行 '4' 全屏可编辑显示：先整屏清黑，再按 (x, y) 定位渲染（按屏宽自动换行）。
 * @param  p  全屏显示参数（color/x/y/text）。
 * @return YN_OL_EXEC_OK（坐标越界为「执行但不落屏」，同 '3' 口径）。
 * @note   文本中回车 0x0A/0x0D 由渲染引擎原生处理（0x0A 换行、0x0D 跳过）。
 *         坐标越界（x ≥ 屏宽或 y ≥ 屏高）时清屏后不渲染（同云南常规 '4' 口径）。
 */
static int _yn_ol_exec_full_screen(const yn_ol_full_screen_t *p)
{
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return YN_OL_EXEC_NO_DISPLAY;

    app_scroll_render_lock();
    dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK);

    if (p->x >= d->screen_rows || p->y >= d->screen_cols) {
        dev_display_commit_frame(d); /* 坐标越界：仅清屏 */
        app_scroll_render_unlock();
        return (int)YN_OL_EXEC_OK;
    }

    app_render(&(render_cfg_t){
        .type  = RENDER_TEXT,
        .x     = p->x,
        .y     = p->y,
        .w     = (uint16_t)(d->screen_rows - p->x),
        .h     = (uint16_t)(d->screen_cols - p->y),
        .style = &(render_style_t){
            .h_align   = ALIGN_LEFT_UP,
            .v_align   = ALIGN_LEFT_UP,
            .word_wrap = true,
        },
        .color     = _yn_ol_map_color(p->color),
        .text      = (const char *)p->text,
        .len       = p->text_len,
        .font_size = s_yn_ol_font_size,
        .font_type = s_yn_ol_font_type,
        .text_enc  = FONT_ENC_GBK,
    });
    app_scroll_render_unlock();
    return (int)YN_OL_EXEC_OK;
}

/* ---- '5' 清屏 / 0x42~0x50 行清除 / 0x01 全屏点亮 ---- */

/** @brief 执行 '5' 全屏清除。 */
static int _yn_ol_exec_clear(void)
{
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return YN_OL_EXEC_NO_DISPLAY;
    app_scroll_render_lock();
    _yn_ol_clear_screen(d);
    app_scroll_render_unlock();
    return YN_OL_EXEC_OK;
}

/** @brief 执行行清除（0x42~0x46 / 0x50，row 0~5）。 */
static int _yn_ol_exec_clear_row(uint8_t row)
{
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return YN_OL_EXEC_NO_DISPLAY;
    app_scroll_render_lock();
    _yn_ol_row_black(d, row);
    dev_display_commit_frame(d);
    app_scroll_render_unlock();
    return YN_OL_EXEC_OK; /* 行起点越界 = 执行但不落屏（诊断行标注） */
}

/**
 * @brief  执行 0x01 全屏点亮（整屏填充单色）。
 * @param  color  DATA0 二进制颜色值（与 display_color_t 枚举恒等）：
 *                0x01 红 / 0x02 绿 / 0x03 黄（文档三色）+
 *                0x04 蓝 / 0x05 紫 / 0x06 青 / 0x07 白（扩展保留，与云南常规则一致）。
 */
static int _yn_ol_exec_fill_all(uint8_t color)
{
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return YN_OL_EXEC_NO_DISPLAY;
    if (color < 1U || color > 7U)
        return YN_OL_EXEC_BAD_PARAM; /* 防御：解析已限定，此处仅守边界 */
    app_scroll_render_lock();
    dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, (display_color_t)color);
    dev_display_commit_frame(d);
    app_scroll_render_unlock();
    return YN_OL_EXEC_OK;
}

/* ---- '8' 亮度 / 'A' 外设 ---- */

/**
 * @brief  执行 '8' 亮度设定。
 * @param  level  0 = 自动亮度（参数 0x00 / '0'）；1~8 = 手动档（参数 '1'~'8'，8 最亮）。
 * @note   0 → 恢复光敏任务自动调光（osThreadResume）；
 *         1~8 → 挂起光敏任务（防 1s 周期自动调光覆盖手动档）+ 硬件档恒等映射。
 *         文档口径为 '0'~'5'（5 最亮）——本实现接受超集 '1'~'8'（**已裁决维持**，doc/15 §8）。
 */
static int _yn_ol_exec_brightness(uint8_t level)
{
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return YN_OL_EXEC_NO_DISPLAY;

    if (level == 0U) {
        if (g_light_sensor_task_handle != nullptr)
            osThreadResume(g_light_sensor_task_handle);
        return YN_OL_EXEC_OK;
    }

    if (level > DEV_DISPLAY_BRIGHTNESS_MAX)
        return YN_OL_EXEC_BAD_PARAM;
    if (g_light_sensor_task_handle != nullptr)
        osThreadSuspend(g_light_sensor_task_handle); /* 手动设定 → 关闭自动调光 */
    dev_display_set_brightness(d, level);            /* 协议档位与硬件档位恒等映射 */
    return YN_OL_EXEC_OK;
}

/**
 * @brief  执行 'A' 外设控制（bit0 绿灯 / bit1 红灯 / bit2 黄闪报警）。
 * @param  ctrl  控制位图。
 * @note   车道灯为单灯互斥（true=绿/false=红）：红绿同置时红灯优先
 *         （沿贵州治超/云南常规裁决，协议未定义优先级）。
 *         文档附注（云南 P6 治超屏 192×96）：`7B 41 01 00 7D` 黄闪关、
 *         `7B 41 01 04 7D` 黄闪开——与 bit2 语义一致。
 */
static int _yn_ol_exec_peripheral(uint8_t ctrl)
{
    const bool green  = (ctrl & 0x01U) != 0U;
    const bool red    = (ctrl & 0x02U) != 0U;
    const bool yellow = (ctrl & 0x04U) != 0U;
    if (red) {
        dev_io_lane_light(false);
    } else if (green) {
        dev_io_lane_light(true);
    }
    dev_io_flash_light(yellow);
    return YN_OL_EXEC_OK;
}

/* ---- 0x47 修改 IP / 0x48 查询 IP / 0x49 屏体参数 ---- */

/**
 * @brief  执行 0x47 修改 IP：写 Sector1（`port` 字段；`udp_port` 保留现值）→ 应答 0x51
 *         回显 → 短延时后软复位（STD 约定「改 IP/端口重启生效」；对齐 GZ_OL `0x40` 先例）。
 * @param  ch   来源通道。
 * @param  cmd  解析结果（`p.setip` 为 14B 网络参数）。
 * @note   **已裁决维持**（2026-09-17 Q2）：协议文档未定义 0x47 的应答与重启行为——
 *         本实现借 0x51（查询 IP 返回值）作「已写入」回执 + osDelay(100) 后软复位
 *         （STD 约定「改 IP/端口重启生效」；对齐 GZ_OL `0x40` 先例）。
 */
static int _yn_ol_exec_set_ip(channel_t *ch, const yn_ol_parsed_cmd_t *cmd)
{
    const yn_ol_setip_t *p = &cmd->p.setip;

    app_board_net_cfg_t cur;
    const uint32_t udp_port = (app_board_net_cfg_get(&cur) == 0) ? cur.udp_port : 20103U;

#if YN_OL_RTT_DIAG
    SEGGER_RTT_printf(0,
                      "[yn_ol] 0x47 port field payload[12..13]=%02x %02x -> %u (BE16); "
                      "ip=%u.%u.%u.%u mask=%u.%u.%u.%u gw=%u.%u.%u.%u keep_udp_port=%u\n",
                      (unsigned)cmd->data[12], (unsigned)cmd->data[13], (unsigned)p->port, p->ip[0],
                      p->ip[1], p->ip[2], p->ip[3], p->mask[0], p->mask[1], p->mask[2], p->mask[3],
                      p->gw[0], p->gw[1], p->gw[2], p->gw[3], (unsigned)udp_port);
#endif

    const int32_t wr_ret = app_board_net_cfg_update(p->ip, p->mask, p->gw, (uint32_t)p->port, udp_port);

#if YN_OL_RTT_DIAG
    {
        app_board_net_cfg_t after;
        const bool ok = (app_board_net_cfg_get(&after) == 0);
        SEGGER_RTT_printf(0, "[yn_ol] 0x47 write ret=%d readback=%s port=%u -> reboot in 100ms\n",
                          (int)wr_ret, ok ? "valid" : "INVALID",
                          ok ? (unsigned)after.port : 0U);
    }
#endif
    (void)wr_ret;

    /* 应答 0x51：按解析值重建本次下发的配置（回显，非读回记录——落盘证据看 readback/重启后 0x48） */
    uint8_t payload[YN_OL_NET_PAYLOAD_LEN];
    memcpy(&payload[0], p->ip, 4);
    memcpy(&payload[4], p->mask, 4);
    memcpy(&payload[8], p->gw, 4);
    payload[12] = (uint8_t)(p->port >> 8); /* 与请求同序：高字节在前（见头文件） */
    payload[13] = (uint8_t)(p->port & 0xFFU);
    _yn_ol_send_frame(ch, (uint8_t)YN_OL_PCMD_GET_IP_ACK, payload, YN_OL_NET_PAYLOAD_LEN);

    osDelay(100); /* 9600bps 下 18B ≈ 19ms，取 100ms 余量 */
    NVIC_SystemReset();
    return YN_OL_EXEC_OK; /* 不可达（复位）；仅为满足返回类型 */
}

/**
 * @brief  执行 0x48 查询 IP：读 Sector1 net_cfg → 应答 0x51 帧（14B 载荷，单播回源）。
 * @param  ch  来源通道。
 * @return YN_OL_EXEC_OK。
 * @note   记录无效时回全 0（IP/掩码/网关/端口全 0 = 尚未配置，诚实回显）。
 */
static int _yn_ol_exec_get_ip(channel_t *ch)
{
    uint8_t payload[YN_OL_NET_PAYLOAD_LEN] = {0};
    (void)_yn_ol_net_payload(payload);

#if YN_OL_RTT_DIAG
    SEGGER_RTT_printf(0, "[yn_ol] 0x48 -> 0x51 payload port=%u (BE16 bytes %02x %02x)\n",
                      (unsigned)(((uint16_t)payload[12] << 8) | payload[13]),
                      (unsigned)payload[12], (unsigned)payload[13]);
#endif

    _yn_ol_send_frame(ch, (uint8_t)YN_OL_PCMD_GET_IP_ACK, payload, YN_OL_NET_PAYLOAD_LEN);
    return YN_OL_EXEC_OK;
}

/**
 * @brief  执行 0x49 设置屏体参数：更新运行态（字体/字宽）并**持久化到 W25Qxx**。
 * @param  p  屏体参数（font_type / font_size）。
 * @return YN_OL_EXEC_OK（运行态已生效）；写盘失败仍返回 OK（仅告警，不阻塞、不重启）。
 * @note   影响后续 '3'/'4'/行清除的行高与字形；掉电保留（独立 4KB 扇区 12B 记录，
 *         见头文件「0x49 持久化」）。
 */
static int _yn_ol_exec_screen_param(const yn_ol_screen_param_t *p)
{
    s_yn_ol_font_type = p->font_type;
    s_yn_ol_font_size = p->font_size;

    const bool saved = yn_ol_screen_cfg_save();

#if YN_OL_RTT_DIAG
    SEGGER_RTT_printf(0, "[yn_ol] 0x49 font_type=%d font_size=%d (persist %s)\n", (int)p->font_type,
                      (int)p->font_size, saved ? "ok" : "FAILED (runtime only)");
#else
    (void)saved;
#endif
    return YN_OL_EXEC_OK;
}

/* ---- 0x02 版本号 ---- */

/**
 * @brief  执行 0x02 获取版本号：串口/网络回裸 ASCII PROGRAM_CODE + 屏幕居中显示
 *         「版本:PROGRAM_CODE」。
 * @param  ch  源通道。
 * @return YN_OL_EXEC_OK（版本串已回）；YN_OL_EXEC_NO_DISPLAY 仅屏显部分跳过。
 * @note   文档示例版本号 `YN_ZCP_P10_3.0` 为源固件私有串；本实现沿云南常规/
 *         贵州常规裁决口径回编译期常量 PROGRAM_CODE（唯一固件身份真源），
 *         屏上显示格式沿贵州常规 0x02 先例（`版本:` 前缀 + FONT_SELF_ADAPT 居中）。
 */
static int _yn_ol_exec_version(channel_t *ch)
{
    channel_send(ch, (uint8_t *)PROGRAM_CODE, sizeof(PROGRAM_CODE) - 1U);

    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return YN_OL_EXEC_NO_DISPLAY;

    char txt[64];
    const int n = snprintf(txt, sizeof(txt), "版本:%s", PROGRAM_CODE);
    if (n <= 0 || n >= (int)sizeof(txt))
        return YN_OL_EXEC_BAD_PARAM;

    app_scroll_render_lock();
    dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK);
    app_render(&(render_cfg_t){
        .type      = RENDER_TEXT,
        .x         = 0,
        .y         = 0,
        .w         = d->screen_rows,
        .h         = d->screen_cols,
        .style     = &(render_style_t){
            .h_align   = ALIGN_CENTER,
            .v_align   = ALIGN_CENTER,
            .word_wrap = false,
        },
        .color     = COLOR_RED,
        .text      = txt,
        .len       = (uint16_t)n,
        .font_size = FONT_SELF_ADAPT,
        .font_type = s_yn_ol_font_type,
        .text_enc  = FONT_ENC_UTF8, /* 文案为 UTF-8 字面量（引擎内部转 GBK） */
    });
    app_scroll_render_unlock();
    return YN_OL_EXEC_OK;
}

/* ---- 对外状态查询 ---- */

font_size_t yn_ol_current_font_size(void)
{
    return s_yn_ol_font_size;
}

font_type_t yn_ol_current_font_type(void)
{
    return s_yn_ol_font_type;
}

/* ---- 命令分派 ---- */

/**
 * @brief  执行云南治超屏协议命令。
 * @param  ch   当前通道。
 * @param  cmd  解析后的命令结构体。
 * @return yn_ol_exec_ret_t：0 = 已执行；<0 = 未执行/未生效原因（逐帧诊断用）。
 * @note   仅 '1'（状态应答）、0x48（0x51 应答）、0x47（0x51 回执 + 复位）产生上行；
 *         其余命令单向执行不回；解析失败静默丢弃（协议未定义错误应答）。
 */
int yn_ol_execute_cmd(channel_t *ch, const yn_ol_parsed_cmd_t *cmd)
{
    if (cmd == nullptr || cmd->sta != YN_OL_PARSE_OK)
        return YN_OL_EXEC_NOT_RUN;

    /* 自检打断：自检进行中收到其它有效命令 → 置中止标志（自检任务下一分片退出） */
    if (s_yn_ol_selftest_running && cmd->cmd != YN_OL_PCMD_SELF_CHECK)
        s_yn_ol_selftest_abort = true;

    switch (cmd->cmd) {
        case YN_OL_PCMD_HOST_QUERY:
            return _yn_ol_exec_host_query(ch);
        case YN_OL_PCMD_SELF_CHECK:
            return _yn_ol_exec_self_check();
        case YN_OL_PCMD_ONE_LINE:
            return _yn_ol_exec_one_line(&cmd->p.one_line);
        case YN_OL_PCMD_FULL_SCREEN:
            return _yn_ol_exec_full_screen(&cmd->p.full_screen);
        case YN_OL_PCMD_CLEAR:
            return _yn_ol_exec_clear();
        case YN_OL_PCMD_BRIGHTNESS:
            return _yn_ol_exec_brightness(cmd->p.brightness);
        case YN_OL_PCMD_PERIPHERAL:
            return _yn_ol_exec_peripheral(cmd->p.peripheral);
        case YN_OL_PCMD_CLEAR_ROW1:
        case YN_OL_PCMD_CLEAR_ROW2:
        case YN_OL_PCMD_CLEAR_ROW3:
        case YN_OL_PCMD_CLEAR_ROW4:
        case YN_OL_PCMD_CLEAR_ROW5:
        case YN_OL_PCMD_CLEAR_ROW6:
            return _yn_ol_exec_clear_row(cmd->p.clear_row);
        case YN_OL_PCMD_SET_IP:
            return _yn_ol_exec_set_ip(ch, cmd);
        case YN_OL_PCMD_GET_IP:
            return _yn_ol_exec_get_ip(ch);
        case YN_OL_PCMD_SCREEN_PARAM:
            return _yn_ol_exec_screen_param(&cmd->p.screen);
        case YN_OL_PCMD_FILL_ALL:
            return _yn_ol_exec_fill_all(cmd->p.fill_color);
        case YN_OL_PCMD_VERSION:
            return _yn_ol_exec_version(ch);
        default:
            return YN_OL_EXEC_NOT_MATCH; /* 0x51 等仅出站命令字：解析即丢弃，理论不可达 */
    }
}
