/**
 * @file    app_gx_proto_cmd.c
 * @brief   广西费显协议执行层（显示/亮度/灯控/语音/版本/开机界面/字体）
 *
 * 契约一览见 app_gx_proto.h 文件头（本文件实现 ② 亮度映射、⑤ 开机界面仅运行态）；
 * 渲染互斥、无显示降级与语音口径见 doc/21_广西费显协议/README.md §2
 */

#include "app_gx_proto_cmd.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h" /* pdMS_TO_TICKS */
#include "app_boot.h" /* PROGRAM_CODE（版本号） */
#include "app_default_display.h"
#include "app_gx_proto_parse.h" /* gx_parsed_cmd_t / gx_parse_frame */
#include "app_light_sensor.h" /* g_light_sensor_task_handle（手动亮度挂起自动调光） */
#include "app_render.h"
#include "app_scroll.h"       /* app_scroll_render_lock / unlock */
#include "cmsis_os2.h"        /* osThreadSuspend / osKernelGetTickCount */
#include "dev_display.h"
#include "dev_io_ctrl.h"      /* 车道灯 PD14 / 报警灯 PD15 */
#include "dev_lane_light.h"
#include "dev_rs232_voice.h"  /* 语音板 TTS（USART6 旁路） */
#include "text_cvt.h"         /* UTF8ToGBK（语音文案） */

/* ---- 模块运行态（SRAM）---- */
static display_color_t s_gx_text_color = COLOR_YELLOW; /**< 最近显示色（旧固件全局 fontColor 语义） */
static font_size_t s_gx_font_size      = FONT_16;      /**< 51 54 渲染字号（04 04 可改，不持久化） */
static font_type_t s_gx_font_type      = FONT_ST;      /**< 51 54 渲染字型（04 04 可改，不持久化） */
static bool s_gx_auto_dim_off;                         /**< 光敏自动调光是否已被本模块挂起 */

static bool s_gx_timed;              /**< 总显示时间计时是否生效（0 秒命令即清除） */
static uint32_t s_gx_deadline_tick;  /**< 到期 tick（s_gx_timed=true 时有效） */

static uint8_t s_gx_boot_text[GX_BOOT_TEXT_LEN];     /**< 03 03 设置的开机界面文本（运行态） */
static display_color_t s_gx_boot_color = COLOR_YELLOW;/**< 开机界面颜色 */
static bool s_gx_boot_valid;                          /**< true = 开机界面已被 03 03 设置 */

/** 04 04 编码 → 字号/字型查表；字型名用于三行确认文案。 */
static const font_size_t s_gx_font_sizes[3] = {FONT_16, FONT_24, FONT_32};
static const font_type_t s_gx_font_types[4] = {FONT_ST, FONT_FS, FONT_KT, FONT_HT};
static const char *const s_gx_font_type_names[4] = {"宋体", "仿宋", "楷体", "黑体"};

/** 礼貌语文案（UTF-8 字面量，播报前转 GBK；编号 1~5 对应下标 0~4）。 */
static const char *const s_gx_polite_texts[GX_VOICE_CODE_MAX] = {
    "您好,欢迎光临",
    "您好,免费车",
    "谢谢合作,祝您一路平安",
    "请出示通行卡",
    "再见",
};

/* ---- 通用小工具 ---- */

/** @brief 色码 0/1/2 → display_color_t（红/绿/黄，枚举值与码 + 1 恒等）。 */
static display_color_t _gx_color_from_code(uint8_t code)
{
    return (display_color_t)((uint32_t)code + 1U);
}

/** @brief 是否 GBK 首字节（0x81~0xFE）。 */
static bool _gx_gbk_lead(uint8_t b)
{
    return b >= 0x81U && b <= 0xFEU;
}

/** @brief 取消总显示时间计时（其它绘制命令覆盖屏幕时调用，避免旧计时擦掉新内容）。 */
static void _gx_timer_cancel(void)
{
    s_gx_timed = false;
}

/**
 * @brief  启动/清除总显示时间计时。
 * @param  seconds  [10] 原始值：0 = 永久（清除计时），1~255 = 秒数。
 */
static void _gx_timer_start(uint8_t seconds)
{
    if (seconds == 0U) {
        s_gx_timed = false;
        return;
    }
    s_gx_deadline_tick = osKernelGetTickCount() + pdMS_TO_TICKS((uint32_t)seconds * 1000U);
    s_gx_timed         = true;
}

/** @brief 整屏清黑（不提交；由随后的 app_render / commit 统一提交）。 */
static void _gx_clear(dev_display_t *d)
{
    dev_display_fill(d, 0U, 0U, d->screen_rows, d->screen_cols, COLOR_BLACK);
}

/**
 * @brief  在指定行区域渲染 GBK 文本（调用方须已清屏并持渲染锁）。
 * @param  y     行区域起点（像素）。
 * @param  h     行区域高度（像素）。
 * @param  wrap  true = 按屏幕宽度自动换行（整屏文本）；false = 单行截断。
 * @return true = 已调用渲染（app_render 内部提交帧）；false = 区域越屏未画。
 */
static bool _gx_draw_text(dev_display_t *d, uint16_t y, uint16_t h, const uint8_t *text,
                          uint16_t len, display_color_t color, font_size_t size,
                          font_type_t type, bool wrap)
{
    if (len == 0U || y >= d->screen_cols)
        return false;

    app_render(&(render_cfg_t){
        .type  = RENDER_TEXT,
        .x     = 0U,
        .y     = y,
        .w     = d->screen_rows,
        .h     = h,
        .style = &(render_style_t){
            .h_align   = ALIGN_LEFT_UP,
            .v_align   = ALIGN_LEFT_UP,
            .word_wrap = wrap,
        },
        .color     = color,
        .text      = (const char *)text,
        .len       = len,
        .font_size = size,
        .font_type = type,
        .text_enc  = FONT_ENC_GBK,
    });
    return true;
}

/* ---- 51 54 文本预处理：换行归一 ---- */

/**
 * @brief  归一化显示文本：0x0D 丢弃（0A 0D 对 → 0A）、字面 `\n` → 0x0A。
 * @param  src  原始文本（GBK/ASCII）。
 * @param  len  原始字节数。
 * @param  dst  输出缓冲（容量 ≥ GX_TEXT_MAX）。
 * @return 输出字节数（≤ len）。
 * @note   GBK 双字节整体拷贝，尾字节 0x5C 不会被误判为字面 `\n` 的首字节；
 *         不支持 `_`（0x5F）作换行。详见 doc/21_广西费显协议/README.md §2.3。
 */
static uint16_t _gx_normalize_text(const uint8_t *src, uint16_t len, uint8_t *dst)
{
    uint16_t out = 0U;
    uint16_t i   = 0U;

    while (i < len) {
        if (src[i] == 0x0DU) { /* 0A 0D 对：丢 CR、留 LF */
            i++;
            continue;
        }
        if (_gx_gbk_lead(src[i]) && (uint16_t)(i + 1U) < len) {
            dst[out++] = src[i];
            dst[out++] = src[i + 1U];
            i = (uint16_t)(i + 2U);
            continue;
        }
        if (src[i] == 0x5CU && (uint16_t)(i + 1U) < len && src[i + 1U] == 0x6EU) {
            dst[out++] = 0x0AU; /* 字面 `\n` → 换行 */
            i = (uint16_t)(i + 2U);
            continue;
        }
        dst[out++] = src[i++];
    }
    return out;
}

/* ---- 开机界面（03 03 运行态 + 总显示时间到期恢复）---- */

/** @brief 按 6 行 × 16B 渲染开机界面文本（调用方须已清屏并持渲染锁）。 */
static void _gx_render_boot_lines(dev_display_t *d)
{
    const uint8_t *p = s_gx_boot_text;

    for (uint16_t row = 0U; row < GX_BOOT_LINES; row++) {
        const uint8_t *line = &p[row * GX_BOOT_LINE_BYTES];
        uint16_t       n    = GX_BOOT_LINE_BYTES;

        while (n > 0U && (line[n - 1U] == 0x00U || line[n - 1U] == 0x20U))
            n--; /* 去尾部 0x00/0x20 填充（两者均非 GBK 尾字节） */

        (void)_gx_draw_text(d, (uint16_t)(row * (uint16_t)FONT_16), (uint16_t)FONT_16, line, n,
                            s_gx_boot_color, FONT_16, FONT_ST, false);
    }
}

/** @brief 恢复开机画面：03 03 设置过则用它，否则回项目通用默认画面（无显示时安全跳过）。 */
static void _gx_restore_boot_screen(void)
{
    if (!s_gx_boot_valid) {
        app_default_display_show(); /* 项目通用「欢迎行驶 / 高速公路」 */
        return;
    }

    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;

    app_scroll_render_lock();
    _gx_clear(d);
    _gx_render_boot_lines(d);
    dev_display_commit_frame(d);
    app_scroll_render_unlock();
}

/* ---- 各命令执行 ---- */

/**
 * @brief  执行 51 54：记录颜色/总显示时间 → 清屏 → 按当前字号字型整屏渲染。
 * @param  cmd  解析结果（text/text_len/color/display_seconds 有效）。
 * @note   播放方式/速度/停留 v1 全部忽略（直接显示，与现场旧固件一致）；
 *         文本经换行归一后交给 app_render（GBK 直通）；空文本只清屏。
 */
static void _gx_exec_display(const gx_parsed_cmd_t *cmd)
{
    s_gx_text_color = _gx_color_from_code(cmd->color);
    _gx_timer_start(cmd->display_seconds);

    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;

    uint8_t        text[GX_TEXT_MAX];
    const uint16_t len = _gx_normalize_text(cmd->text, cmd->text_len, text);

    app_scroll_render_lock();
    _gx_clear(d);
    if (!_gx_draw_text(d, 0U, d->screen_cols, text, len, s_gx_text_color, s_gx_font_size,
                       s_gx_font_type, true))
        dev_display_commit_frame(d); /* 空文本：只提交清屏 */
    app_scroll_render_unlock();
}

/**
 * @brief  执行 4B 4C：亮度 0/1/2/3 → 8/6/4/2 档，并挂起光敏自动调光。
 * @param  cmd  解析结果（brightness 0~3）。
 * @note   协议无「恢复自动」命令，挂起后直到重启才恢复（与旧固件一致）；
 *         无显示实例时不做任何动作。
 */
static void _gx_exec_brightness(const gx_parsed_cmd_t *cmd)
{
    static const uint8_t levels[4] = {8U, 6U, 4U, 2U}; /* 档位：0 最亮 */

    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;

    if (!s_gx_auto_dim_off && g_light_sensor_task_handle != nullptr) {
        osThreadSuspend(g_light_sensor_task_handle);
        s_gx_auto_dim_off = true;
    }
    dev_display_set_brightness(d, levels[cmd->brightness & 0x03U]);
}

/**
 * @brief  执行 4B 5A：车道通行灯（单红/绿切换）与报警灯（PD14 / PD15）。
 * @param  cmd  解析结果（lamp 为六种动作之一）。
 */
static void _gx_exec_lane(const gx_parsed_cmd_t *cmd)
{
    switch (cmd->lamp) {
        case GX_LAMP_GREEN_ON:
        case GX_LAMP_RED_OFF:
            dev_lane_light_set(DEV_LANE_LIGHT_GREEN_ARROW);
            break;

        case GX_LAMP_GREEN_OFF:
        case GX_LAMP_RED_ON:
            dev_lane_light_set(DEV_LANE_LIGHT_RED_CROSS);
            break;

        case GX_LAMP_ALARM_ON:
            dev_io_flash_light(true);
            break;

        case GX_LAMP_ALARM_OFF:
            dev_io_flash_light(false);
            break;

        default:
            break;
    }
}

/**
 * @brief  执行 01 01：整屏纯色（00 红 / 01 绿 / 02 黄）。
 * @param  cmd  解析结果（color 0~2）。
 */
static void _gx_exec_fullscreen(const gx_parsed_cmd_t *cmd)
{
    _gx_timer_cancel();

    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;

    app_scroll_render_lock();
    dev_display_fill(d, 0U, 0U, d->screen_rows, d->screen_cols, _gx_color_from_code(cmd->color));
    dev_display_commit_frame(d);
    app_scroll_render_unlock();
}

/** @brief 执行 02 02：整屏红字「版本:PROGRAM_CODE」（无应答）。 */
static void _gx_exec_version(void)
{
    _gx_timer_cancel();

    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;

    char       text[32];
    const int  n = snprintf(text, sizeof(text), "版本:%s", PROGRAM_CODE);
    if (n <= 0)
        return;
    const uint16_t len = (n >= (int)sizeof(text)) ? (uint16_t)(sizeof(text) - 1U) : (uint16_t)n;

    app_scroll_render_lock();
    _gx_clear(d);
    (void)_gx_draw_text(d, 0U, d->screen_cols, (const uint8_t *)text, len, COLOR_RED, FONT_16,
                        FONT_ST, true);
    app_scroll_render_unlock();
}

/**
 * @brief  执行 03 03：保存开机界面（运行态）并立即显示（固定 6 行 × 16B、FONT_16）。
 * @param  cmd  解析结果（text 96B / color 0~2）。
 * @note   不写 W25Qxx（掉电回项目通用默认画面）；保存后成为总显示时间到期恢复的画面。
 */
static void _gx_exec_boot_screen(const gx_parsed_cmd_t *cmd)
{
    _gx_timer_cancel();

    memcpy(s_gx_boot_text, cmd->text, GX_BOOT_TEXT_LEN);
    s_gx_boot_color = _gx_color_from_code(cmd->color);
    s_gx_boot_valid = true;
    s_gx_text_color = s_gx_boot_color;

    _gx_restore_boot_screen();
}

/**
 * @brief  执行 04 04：更新字号/字型运行态并显示三行确认文案。
 * @param  cmd  解析结果（font_size_code / font_type_code 为原始半字节）。
 * @note   越界半字节保持原值（与旧固件逐半字节独立判断一致）；不持久化；
 *         后续 51 54 跟随新字号/字型，03 03 固定 FONT_16。
 */
static void _gx_exec_font(const gx_parsed_cmd_t *cmd)
{
    _gx_timer_cancel();

    if (cmd->font_size_code <= GX_FONT_SIZE_MAX)
        s_gx_font_size = s_gx_font_sizes[cmd->font_size_code];
    if (cmd->font_type_code <= GX_FONT_TYPE_MAX)
        s_gx_font_type = s_gx_font_types[cmd->font_type_code];

    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;

    static const char s_title[] = "字体设置";
    char              size_line[24];
    char              type_line[24];
    const int sn = snprintf(size_line, sizeof(size_line), "大小：%d点阵", (int)s_gx_font_size);
    const int tn = snprintf(type_line, sizeof(type_line), "类型：%s",
                            s_gx_font_type_names[(uint32_t)s_gx_font_type]);

    const uint16_t line_h = (uint16_t)s_gx_font_size;
    bool           drawn  = false;

    app_scroll_render_lock();
    _gx_clear(d);
    drawn |= _gx_draw_text(d, 0U, line_h, (const uint8_t *)s_title, sizeof(s_title) - 1U,
                           s_gx_text_color, s_gx_font_size, s_gx_font_type, false);
    if (sn > 0 && (size_t)sn < sizeof(size_line))
        drawn |= _gx_draw_text(d, line_h, line_h, (const uint8_t *)size_line, (uint16_t)sn,
                               s_gx_text_color, s_gx_font_size, s_gx_font_type, false);
    if (tn > 0 && (size_t)tn < sizeof(type_line))
        drawn |= _gx_draw_text(d, (uint16_t)(2U * line_h), line_h, (const uint8_t *)type_line,
                               (uint16_t)tn, s_gx_text_color, s_gx_font_size, s_gx_font_type, false);
    if (!drawn)
        dev_display_commit_frame(d);
    app_scroll_render_unlock();
}

/* ---- 56 4F 语音 ---- */

/** @brief 播报一条 UTF-8 文案（转 GBK 后直送语音板）。 */
static void _gx_voice_play_utf8(const char *text)
{
    uint8_t  gbk[64];
    uint32_t gbk_len = sizeof(gbk);

    if (text == NULL)
        return;
    UTF8ToGBK(text, (uint32_t)strlen(text), (char *)gbk, &gbk_len);
    if (gbk_len == 0U)
        return;
    dev_rs232_voice_play(gbk, (uint16_t)gbk_len);
}

/** @brief 播报礼貌语（编号 1~5，越界忽略）。 */
static void _gx_voice_polite(uint8_t code)
{
    if (code < GX_VOICE_CODE_MIN || code > GX_VOICE_CODE_MAX)
        return;
    _gx_voice_play_utf8(s_gx_polite_texts[code - 1U]);
}

/**
 * @brief  播报收费语音「您好,请交费 X 元」。
 * @param  amount  7 位 ASCII 数字（万/千/百/十/个/十分/百分）。
 * @note   金额全 0 不播；小数按「分」格式化（`%02u`），避免旧固件 `%d.%d`
 *         把 1.05 播成 1.5；分为 0 时只念整数。详见 doc/21 §2.5。
 */
static void _gx_voice_fee(const uint8_t *amount)
{
    uint32_t yuan    = 0U;
    uint32_t nonzero = 0U;

    for (uint8_t i = 0U; i < 5U; i++)
        yuan = yuan * 10U + (uint32_t)(amount[i] - '0');
    const uint8_t cents = (uint8_t)((uint8_t)(amount[5] - '0') * 10U + (uint8_t)(amount[6] - '0'));

    for (uint8_t i = 0U; i < GX_VOICE_AMOUNT_DIGITS; i++)
        nonzero += (uint32_t)(amount[i] - '0');
    if (nonzero == 0U)
        return; /* 金额全 0：只回应答、不播报 */

    char      utf8[48];
    const int n = (cents == 0U)
                      ? snprintf(utf8, sizeof(utf8), "您好,请交费%lu元", (unsigned long)yuan)
                      : snprintf(utf8, sizeof(utf8), "您好,请交费%lu.%02u元", (unsigned long)yuan,
                                 (unsigned)cents);
    if (n > 0)
        _gx_voice_play_utf8(utf8);
}

/** @brief 执行 56 4F：按子命令分派礼貌语 / 收费语音。 */
static void _gx_exec_voice(const gx_parsed_cmd_t *cmd)
{
    if (cmd->voice_kind == GX_VOICE_KIND_POLITE)
        _gx_voice_polite(cmd->voice_code);
    else if (cmd->voice_kind == GX_VOICE_KIND_FEE)
        _gx_voice_fee(cmd->amount);
}

/* ---- 应答 ---- */

/**
 * @brief  回送单字节 0x1B（从收帧通道原路发出）。
 * @param  ch  收到本帧的通道（nullptr 或通道已 DOWN 时静默忽略）。
 */
static void _gx_send_ack(const channel_t *ch)
{
    if (ch == nullptr)
        return;

    uint8_t ack = (uint8_t)GX_ACK;
    (void)channel_send((channel_t *)ch, &ack, 1U);
}

/* ---- 对外入口 ---- */

void gx_display_timer_tick(void)
{
    if (!s_gx_timed)
        return;
    if ((int32_t)(osKernelGetTickCount() - s_gx_deadline_tick) < 0)
        return;

    s_gx_timed = false;
    _gx_restore_boot_screen();
}

void gx_execute_cmd(const channel_t *src_ch, const uint8_t *raw, uint16_t raw_len)
{
    const gx_parsed_cmd_t cmd = gx_parse_frame(raw, raw_len);
    if (!cmd.parsed)
        return; /* 协议未定义错误应答：字段非法帧静默丢弃 */

    switch (cmd.cmd) {
        case GX_CMD_DISPLAY:
            _gx_exec_display(&cmd);
            _gx_send_ack(src_ch);
            break;

        case GX_CMD_BRIGHTNESS:
            _gx_exec_brightness(&cmd);
            _gx_send_ack(src_ch);
            break;

        case GX_CMD_LANE:
            _gx_exec_lane(&cmd);
            _gx_send_ack(src_ch);
            break;

        case GX_CMD_VOICE:
            _gx_exec_voice(&cmd);
            _gx_send_ack(src_ch);
            break;

        case GX_CMD_FULLSCREEN:
            _gx_exec_fullscreen(&cmd);
            break; /* 增加命令无返回值：不回 0x1B */

        case GX_CMD_VERSION:
            _gx_exec_version();
            break; /* 不回 0x1B */

        case GX_CMD_BOOT_SCREEN:
            _gx_exec_boot_screen(&cmd);
            _gx_send_ack(src_ch);
            break;

        case GX_CMD_FONT:
            _gx_exec_font(&cmd);
            break; /* 文档未记载本命令：不回 0x1B */

        default:
            break;
    }
}
