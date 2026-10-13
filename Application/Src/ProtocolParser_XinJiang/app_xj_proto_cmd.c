/**
 * @file    app_xj_proto_cmd.c
 * @brief   新疆费额显示器协议执行层（显示/灯控/语音/维护屏）
 *
 * 契约一览见 app_xj_proto.h 文件头（本文件实现 ② 行切分、③ 颜色映射、④ 亮度、
 * ⑧ 维护屏）；渲染互斥/无显示降级/音量映射详见 doc/19_新疆费显协议/README.md §2
 */

#include "app_xj_proto_cmd.h"

#include <string.h>

#include "app_light_sensor.h" /* g_light_sensor_task_handle（手动亮度挂起自动调光） */
#include "app_render.h"
#include "app_scroll.h"      /* app_scroll_render_lock / unlock */
#include "app_xj_proto_parse.h" /* xj_bcc_calc（§3.2 应答帧 BCC） */
#include "app_scroll.h"      /* app_scroll_render_lock / unlock */
#include "cmsis_os2.h"       /* osThreadSuspend / osThreadResume */
#include "dev_display.h"
#include "dev_io_ctrl.h"     /* 车道灯 PD14 / 黄闪灯 PD15 */
#include "dev_rs232_voice.h" /* 语音板 TTS + 音量（USART6 旁路） */
#include "text_cvt.h"        /* UTF8ToGBK（预制语音文案） */

/* ---- 模块运行态（SRAM）---- */
static bool s_xj_auto_dim_off; /**< 光敏自动调光是否被本模块挂起（亮度非 0 挂起 / 收到 0 恢复） */

/* ---- 亮度（契约 ④）---- */

/**
 * @brief  应用 'T' 帧亮度字段：0 = 恢复光敏自动调光；1~255 均匀映射 8 档并挂起自动调光。
 * @param  v  'T' 帧 [4] 原始亮度 0~255。
 * @note   无显示实例时直接返回（本函数整体是显示属性）；挂起标志只在本模块挂起过时恢复
 *         （不越权唤醒他处挂起的任务）。映射与 doc/19_新疆费显协议/README.md §2.4 一致。
 */
static void _xj_apply_brightness(uint8_t v)
{
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;

    if (v == 0U) {
        if (s_xj_auto_dim_off) {
            if (g_light_sensor_task_handle != nullptr)
                osThreadResume(g_light_sensor_task_handle);
            s_xj_auto_dim_off = false;
        }
        return;
    }

    /* 1~255 → 1~8：v=1 → 1（最暗）、v=255 → 8（最亮），均匀分布 */
    const uint8_t level = (uint8_t)(1U + ((uint32_t)(v - 1U) * (DEV_DISPLAY_BRIGHTNESS_MAX - 1U)) / 254U);

    if (g_light_sensor_task_handle != nullptr) {
        osThreadSuspend(g_light_sensor_task_handle);
        s_xj_auto_dim_off = true;
    }
    dev_display_set_brightness(d, level);
}

/* ---- 'T' 文本显示（契约 ②：严格 16 字节/行、忽略显示行字段）---- */

/** @brief 是否 GBK 首字节（0x81~0xFE）。 */
static bool _xj_gbk_lead(uint8_t b)
{
    return b >= 0x81U && b <= 0xFEU;
}

/** @brief 是否 GBK 尾字节（0x40~0xFE 且 ≠0x7F）。 */
static bool _xj_gbk_trail(uint8_t b)
{
    return b >= 0x40U && b <= 0xFEU && b != 0x7FU;
}

/**
 * @brief  按 16 字节/行逐行渲染内容（调用方须已备好清屏/清行并持渲染锁）。
 * @param  d         显示实例（调用方已判空）。
 * @param  cmd       解析结果（text/text_len/color 有效）。
 * @param  start_row 首行行号（0 基）——'T'/'D' 全屏传 0，'D' 行显示传 (line-1)。
 * @note   行内容去尾部 0x00/0x20 后为空则该行不画；GBK 双字节不被 16B 边界劈开
 *         （窗口末字节为 GBK 首字节且其尾字节在窗口外时，该字整字节留给下一行）；
 *         行起点越屏（y ≥ 屏高）= 执行但不落屏。详见 doc/19_新疆费显协议/README.md §2.3。
 */
static void _xj_render_text_lines(dev_display_t *d, const xj_parsed_cmd_t *cmd, uint8_t start_row)
{
    const uint8_t *p   = cmd->text;
    uint16_t       rem = cmd->text_len;
    uint16_t       row = start_row;

    while (rem > 0U) {
        uint16_t take = (rem >= XJ_T_LINE_BYTES) ? (uint16_t)XJ_T_LINE_BYTES : rem;

        /* GBK 边界自适应：窗口末字节为首字节、其后仍有尾字节 → 该字符整体挪到下一行
         * （行宽因此永不超过 16 字节 = 协议 8 汉字口径，代价是行数可能多 1） */
        if (take == (uint16_t)XJ_T_LINE_BYTES && rem > (uint16_t)XJ_T_LINE_BYTES &&
            _xj_gbk_lead(p[XJ_T_LINE_BYTES - 1U]) && _xj_gbk_trail(p[XJ_T_LINE_BYTES]))
            take = (uint16_t)(XJ_T_LINE_BYTES - 1U);

        /* 去尾部 0x00/0x20 填充（两者都不是 GBK 尾字节，去除安全） */
        uint16_t n = take;
        while (n > 0U && (p[n - 1U] == 0x00U || p[n - 1U] == 0x20U))
            n--;

        const uint16_t y = (uint16_t)(row * (uint16_t)FONT_16);
        if (n > 0U && y < d->screen_cols) {
            app_render(&(render_cfg_t){
                .type  = RENDER_TEXT,
                .x     = 0U,
                .y     = y,
                .w     = d->screen_rows,
                .h     = (uint16_t)FONT_16,
                .style = &(render_style_t){
                    .h_align   = ALIGN_LEFT_UP,
                    .v_align   = ALIGN_LEFT_UP,
                    .word_wrap = false, /* 行已由协议切好，超屏宽由显示层裁剪 */
                },
                .color     = cmd->color,
                .text      = (const char *)p,
                .len       = n,
                .font_size = FONT_16,
                .font_type = FONT_ST,
                .text_enc  = FONT_ENC_GBK,
            });
        }

        p += take;
        rem = (uint16_t)(rem - take);
        row++;
    }
}

/**
 * @brief  执行 'T'：应用亮度 → 整屏清黑 → 按 16 字节/行逐行渲染（黑字只清屏）。
 * @param  cmd  解析结果。
 * @note   清屏 + 渲染 + 提交整段持 `app_scroll_render_lock()`；无显示实例时只做亮度判空跳过。
 */
static void _xj_exec_text(const xj_parsed_cmd_t *cmd)
{
    _xj_apply_brightness(cmd->brightness);

    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;

    app_scroll_render_lock();
    dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK);
    if (cmd->color != COLOR_BLACK)
        _xj_render_text_lines(d, cmd, 0U);
    dev_display_commit_frame(d);
    app_scroll_render_unlock();
}

/**
 * @brief  执行 'D'：应用亮度 → 渲染文本（§3.1.1 行显示只改指定行，§3.1.2 全屏显示整屏重画）。
 * @param  cmd  解析结果（text/text_len/brightness/line/d_is_full 有效）。
 * @note   行显示**不清屏**：只清并重画目标行，避免擦掉其余行（PDF §3.1.1「显示或更新某一
 *         指定的行的信息」）；全屏显示先整屏清黑再按 16B/行逐行画。
 *         'D' 无 RGB 字段（parse 已置 COLOR_RED），故不判 COLOR_BLACK 分支。
 */
static void _xj_exec_line_or_full(const xj_parsed_cmd_t *cmd)
{
    _xj_apply_brightness(cmd->brightness);

    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;

    app_scroll_render_lock();
    if (cmd->d_is_full) {
        dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK);
    } else {
        /* 行显示：只清目标行那一行像素，宽度取整屏，N=4 时 y=48 可能超屏由显示层裁剪 */
        const uint16_t y = (uint16_t)((cmd->line - 1U) * (uint16_t)FONT_16);
        if (y < d->screen_cols)
            dev_display_fill(d, 0, y, d->screen_rows, (uint16_t)FONT_16, COLOR_BLACK);
    }
    _xj_render_text_lines(d, cmd, cmd->d_is_full ? 0U : (uint8_t)(cmd->line - 1U));
    dev_display_commit_frame(d);
    app_scroll_render_unlock();
}

/* ---- 'C' 重置屏幕 ---- */

/** @brief 整屏清黑并提交（'C'；调用方须持渲染锁）。 */
static void _xj_clear_screen(dev_display_t *d)
{
    dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK);
    dev_display_commit_frame(d);
}

/** @brief 执行 'C' 重置屏幕（整屏清黑；无显示实例时跳过）。 */
static void _xj_exec_clear(void)
{
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;

    app_scroll_render_lock();
    _xj_clear_screen(d);
    app_scroll_render_unlock();
}

/* ---- 'V' TTS / 'S' 预制语音 ---- */

/**
 * @brief  协议音量字节 0x30~0x37 → 语音板档位 1~9 线性映射。
 * @param  v  音量字节（越界钳到 0x30~0x37）。
 * @return 板载档位 1~9（0x30→1、0x37→9）。
 * @note   语音板音量非线性，档位 1~9 取贵州先例区间 {1,3,5,7,9} 的两端；
 *         映射值为现场待校准项，见 doc/19_新疆费显协议/README.md §2.6。
 */
static uint8_t _xj_voice_level(uint8_t v)
{
    if (v < XJ_VOL_MIN)
        v = XJ_VOL_MIN;
    if (v > XJ_VOL_MAX)
        v = XJ_VOL_MAX;
    return (uint8_t)(1U + ((uint32_t)(v - XJ_VOL_MIN) * 8U) / (XJ_VOL_MAX - XJ_VOL_MIN));
}

/** @brief 去尾部 0x00/0x20 后的文本长度（语音/显示共用；0x00/0x20 非 GBK 尾字节，去除安全）。 */
static uint16_t _xj_trim_len(const uint8_t *p, uint16_t len)
{
    while (len > 0U && (p[len - 1U] == 0x00U || p[len - 1U] == 0x20U))
        len--;
    return len;
}

/**
 * @brief  执行 'V'：GBK 文本直送语音板播报（忽略 [4] 语言/音色字段）。
 * @param  cmd  解析结果（text/text_len 有效）。
 * @note   文本去尾部填充后为空则不播报；语音板单帧上限由 `dev_rs232_voice_play` 兜底
 *         （>200B 直接丢弃，本协议内容区 ≤65B）。维语/音色能力缺口见
 *         doc/19_新疆费显协议/README.md §8。
 */
static void _xj_exec_voice_tts(const xj_parsed_cmd_t *cmd)
{
    const uint16_t len = _xj_trim_len(cmd->text, cmd->text_len);
    if (len == 0U)
        return;

    dev_rs232_voice_volume(_xj_voice_level(cmd->volume));
    dev_rs232_voice_play(cmd->text, len);
}

/* 预制语音编号（PDF §4.4：入口发卡机 0xA0~0xAF，出口收卡机 0xA0~0xB6）→ 中文文案
 * （UTF-8 字面量，播报前转 GBK）。文档 §4.1/4.2/4.3 要求的维语/音色能力本硬件没有
 * （语言字段一律忽略），见 doc/19_新疆费显协议/README.md §8。
 * 同编号在入口/出口语义不同（0xA0 等），本表按「出口收卡机」一栏实现——该栏文案自足，
 * 入口侧同为 ETC 收卡流程可直接复用。 */
static const char *const s_xj_preset_texts[] = {
    /* 0xA0 */ "ETC已交易成功，请通行",   /* 出口 */
    /* 0xB0 */ "缴费XX元，请展示付款码",   /* 移动支付 */
    /* 0xB1 */ "缴费成功",
    /* 0xB2 */ "请将通行卡插入卡口",       /* 收卡 */
    /* 0xB3 */ "未检测到通行卡，请联系工作人员",
    /* 0xB4 */ "通行卡损坏，请联系工作人员",
    /* 0xB5 */ "如需发票，请按键取票",
    /* 0xB6 */ "请拿好通行卡，祝您一路平安",
};
/** 预制编号 → s_xj_preset_texts 下标（编号非连续，0xA0 与 0xB0~0xB6 之间空 15 个）。 */
static const uint8_t s_xj_preset_index[] = {
    0xA0U, 0xB0U, 0xB1U, 0xB2U, 0xB3U, 0xB4U, 0xB5U, 0xB6U,
};
#define XJ_PRESET_COUNT (sizeof(s_xj_preset_index) / sizeof(s_xj_preset_index[0]))
/* 契约：编号表与文案表逐项同序等长（同下标 = 同一条文案）。改错播错语音，见 doc/19 §2.6。 */

/** @brief 播报预制语音文案（'S' §4.4 形态；未收录编号静默忽略）。 */
static void _xj_exec_voice_preset(const xj_parsed_cmd_t *cmd)
{
    uint8_t idx = 0xFFU;
    for (uint8_t i = 0U; i < (uint8_t)XJ_PRESET_COUNT; i++) {
        if (s_xj_preset_index[i] == cmd->preset_no) {
            idx = i;
            break;
        }
    }
    if (idx == 0xFFU)
        return; /* 未收录编号（含摘录版 0x31~0x35）不播报 */

    const char *text = s_xj_preset_texts[idx];
    uint8_t     gbk[64];
    uint32_t    gbk_len = sizeof(gbk);
    UTF8ToGBK(text, (uint32_t)strlen(text), (char *)gbk, &gbk_len);
    if (gbk_len == 0U)
        return;

    dev_rs232_voice_volume(_xj_voice_level(cmd->volume));
    dev_rs232_voice_play(gbk, (uint16_t)gbk_len);
}

/* ---- 'S' 语音报价 1/2/3（PDF §4.1/§4.2/§4.3）：载荷是 ASCII 数字，需拼成中文句子 ----
 *  参考语序（PDF 原文）：
 *    报价 1「3 型车，请交费 20 元」→ 车型 + 金额
 *    报价 2「3 型车，车货总重 23.78 吨，请交费 20 元」→ 车型 + 总重 + 金额
 *    报价 3「3 型车，车货总重 73.40 吨，超重 20.34 吨」→ 车型 + 总重 + 超重
 *  数字点以 '、' 念出，故插入分隔符；数字段原样播出，不做数值运算。 */

/** 报价语音 GBK 组装缓冲上限。
 *  最坏情况（PDF 语序）：报价 2「3型车，车货总重023.78吨，请交费0020元」= 10 个 ASCII
 *  数字 + 13 个汉字 = 36B（报价 3 = 35B，更短）。取 48B 留余量后**不再增长**——
 *  本缓冲位于 256 words 任务栈上，放大它会挤压栈预算（doc/19 §2.8）。 */
#define XJ_QUOTE_GBK_MAX (48U)

/** @brief 向 GBK 缓冲追写一段 UTF-8 文本（容量不足即停止并截断）。 */
static void _xj_gbk_append(uint8_t *buf, uint32_t *len, uint32_t cap, const char *utf8)
{
    if (*len >= cap)
        return;
    uint32_t room = cap - *len;
    UTF8ToGBK(utf8, (uint32_t)strlen(utf8), (char *)&buf[*len], &room);
    *len += room;
}

/** @brief 向 GBK 缓冲追写一段 ASCII 数字载荷（直拷，语音板按数字念）。 */
static void _xj_gbk_append_digits(uint8_t *buf, uint32_t *len, uint32_t cap,
                                  const uint8_t *p, uint8_t n)
{
    for (uint8_t i = 0U; i < n && *len < cap; i++)
        buf[(*len)++] = p[i];
}

/**
 * @brief  执行 'S' 报价 1/2/3：按形态拼中文报价句并播报。
 * @param  cmd  解析结果（s_kind/volume/payload/payload_len 有效）。
 * @note   载荷长度不足该形态（parse 已按帧长定形，此处再按 payload_len 复核）时静默跳过；
 *         金额/重量不做前导零与小数位规整，原样念（设备侧亦然）。
 */
static void _xj_exec_voice_quote(const xj_parsed_cmd_t *cmd)
{
    uint8_t gbk[XJ_QUOTE_GBK_MAX];
    uint32_t len = 0U;

    /* 车型（各形态 payload 首字节），PDF 语序统一为「X 型车，……」开头 */
    _xj_gbk_append_digits(gbk, &len, XJ_QUOTE_GBK_MAX, cmd->payload, 1U);
    _xj_gbk_append(gbk, &len, XJ_QUOTE_GBK_MAX, "型车，");

    if (cmd->s_kind == XJ_S_KIND_QUOTE1) {
        /* payload[0]=车型 [1..4]=金额 */
        if (cmd->payload_len < 5U)
            return;
        _xj_gbk_append(gbk, &len, XJ_QUOTE_GBK_MAX, "请交费");
        _xj_gbk_append_digits(gbk, &len, XJ_QUOTE_GBK_MAX, &cmd->payload[1], 4U);
        _xj_gbk_append(gbk, &len, XJ_QUOTE_GBK_MAX, "元");
    } else if (cmd->s_kind == XJ_S_KIND_QUOTE2) {
        /* payload[0]=车型 [1..3]=总重整数 [4..5]=总重小数 [6..9]=金额 */
        if (cmd->payload_len < 10U)
            return;
        _xj_gbk_append(gbk, &len, XJ_QUOTE_GBK_MAX, "车货总重");
        _xj_gbk_append_digits(gbk, &len, XJ_QUOTE_GBK_MAX, &cmd->payload[1], 3U);
        _xj_gbk_append(gbk, &len, XJ_QUOTE_GBK_MAX, ".");
        _xj_gbk_append_digits(gbk, &len, XJ_QUOTE_GBK_MAX, &cmd->payload[4], 2U);
        _xj_gbk_append(gbk, &len, XJ_QUOTE_GBK_MAX, "吨，请交费");
        _xj_gbk_append_digits(gbk, &len, XJ_QUOTE_GBK_MAX, &cmd->payload[6], 4U);
        _xj_gbk_append(gbk, &len, XJ_QUOTE_GBK_MAX, "元");
    } else if (cmd->s_kind == XJ_S_KIND_QUOTE3) {
        /* payload[0]=车型 [1..3]=总重整数 [4..5]=总重小数 [6..8]=超重整数 [9..10]=超重小数 */
        if (cmd->payload_len < 11U)
            return;
        _xj_gbk_append(gbk, &len, XJ_QUOTE_GBK_MAX, "车货总重");
        _xj_gbk_append_digits(gbk, &len, XJ_QUOTE_GBK_MAX, &cmd->payload[1], 3U);
        _xj_gbk_append(gbk, &len, XJ_QUOTE_GBK_MAX, ".");
        _xj_gbk_append_digits(gbk, &len, XJ_QUOTE_GBK_MAX, &cmd->payload[4], 2U);
        _xj_gbk_append(gbk, &len, XJ_QUOTE_GBK_MAX, "吨，超重");
        _xj_gbk_append_digits(gbk, &len, XJ_QUOTE_GBK_MAX, &cmd->payload[6], 3U);
        _xj_gbk_append(gbk, &len, XJ_QUOTE_GBK_MAX, ".");
        _xj_gbk_append_digits(gbk, &len, XJ_QUOTE_GBK_MAX, &cmd->payload[9], 2U);
        _xj_gbk_append(gbk, &len, XJ_QUOTE_GBK_MAX, "吨");
    } else {
        return; /* 非报价形态 */
    }

    if (len == 0U)
        return;
    dev_rs232_voice_volume(_xj_voice_level(cmd->volume));
    dev_rs232_voice_play(gbk, (uint16_t)len);
}

/* ---- 心跳超时维护屏（XJ_FAULT_SCREEN=0 时无引用，随 --gc-sections 整体丢弃）---- */

/** @brief 维护屏文案（UTF-8 字面量，渲染前由引擎转 GBK）。 */
static const char s_xj_fault_text[] = "系统维护中";

void xj_fault_screen_show(void)
{
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;

    app_scroll_render_lock();
    _xj_clear_screen(d);
    app_render(&(render_cfg_t){
        .type  = RENDER_TEXT,
        .x     = 0U,
        .y     = 0U,
        .w     = d->screen_rows,
        .h     = d->screen_cols,
        .style = &(render_style_t){
            .h_align   = ALIGN_CENTER,
            .v_align   = ALIGN_CENTER,
            .word_wrap = false,
        },
        .color     = COLOR_RED,
        .text      = s_xj_fault_text,
        .len       = (uint16_t)(sizeof(s_xj_fault_text) - 1U),
        .font_size = FONT_16,
        .font_type = FONT_ST,
        .text_enc  = FONT_ENC_UTF8,
    });
    app_scroll_render_unlock();
}

void xj_fault_screen_clear(void)
{
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;

    app_scroll_render_lock();
    _xj_clear_screen(d);
    app_scroll_render_unlock();
}

/* ---- §3.2 应答帧回送（'D'/'C'/'Z'/'P' 的 5 字节确认）----
 *  摘录版协议未描述任何应答，PDF §3.2 才定义。应答走收到该命令的同一通道回送
 *  （`channel_send` 虚表分派，RS485/RS232 各自原路返回，车道机才能确认在线）。 */

/**
 * @brief  组装并回送一帧 5 字节无载荷应答（FF DC 05 <cmd> BCC）。
 * @param  cmd  解析结果（src_ch 为来源通道）。
 * @param  c    应答命令字（'D'/'C'/'Z'/'P'/'R'）。
 * @note   BCC 自算；通道不可用（TCP/UDP 任务已退出、ops 为空）时 `channel_send` 返回 -1，
 *         静默忽略——应答是尽力而为，绝不影响已完成的显示/灯控动作。
 */
static void _xj_send_ack(const xj_parsed_cmd_t *cmd, uint8_t c)
{
    if (cmd->src_ch == nullptr)
        return;

    uint8_t ack[XJ_FRAME_LEN_MIN] = {XJ_STX0, XJ_STX1, (uint8_t)XJ_FRAME_LEN_MIN, c, 0U};
    ack[XJ_FRAME_LEN_MIN - 1U] = xj_bcc_calc(ack, (uint16_t)(XJ_FRAME_LEN_MIN - 1U));
    (void)channel_send((channel_t *)cmd->src_ch, ack, XJ_FRAME_LEN_MIN);
}

/* ---- 命令分派 ---- */

/**
 * @brief  执行解析后的命令。
 * @param  cmd  解析结果（parsed=false 直接返回）。
 * @note   未支持命令在 probe 已 FAKE（不进队列），字段非法帧在此静默丢弃。
 *         §3.2.7 的 '?' Nack 不实现：无法在静默丢帧的前提下定位到「哪一帧」出错，
 *         盲目回 Nack 反而会让车道机误判费显故障，语义上劣于不响应。
 */
void xj_execute_cmd(const channel_t *src_ch, const uint8_t *raw, uint16_t raw_len)
{
    xj_parsed_cmd_t cmd = xj_parse_frame(raw, raw_len);
    if (!cmd.parsed)
        return;
    cmd.src_ch = src_ch; /* 应答原路回送；仅本帧有效，不外传 */

    switch (cmd.cmd) {
        case XJ_PCMD_TEXT:
            _xj_exec_text(&cmd);
            break;

        case XJ_PCMD_LINE_OR_FULL:
            _xj_exec_line_or_full(&cmd);
            _xj_send_ack(&cmd, (uint8_t)XJ_PCMD_LINE_OR_FULL); /* §3.2.1 'D' 应答 */
            break;

        case XJ_PCMD_CLEAR:
            _xj_exec_clear();
            _xj_send_ack(&cmd, (uint8_t)XJ_PCMD_CLEAR); /* §3.2.2 'C' 应答 */
            break;

        case XJ_PCMD_QUERY:
            /* §3.1.10 本身不做任何操作，只为换回 §3.2.3 的 'Z' 应答 */
            _xj_send_ack(&cmd, (uint8_t)XJ_PCMD_QUERY);
            break;

        case XJ_PCMD_BITMAP_SHOW:
            /* §3.1.11：本硬件无位图存储，任何图号都不匹配 → 按协议清屏后应答 */
            _xj_exec_clear();
            _xj_send_ack(&cmd, (uint8_t)XJ_PCMD_BITMAP_SHOW); /* §3.2.4 'P' 应答 */
            break;

        case XJ_PCMD_LANE_GREEN:
            dev_io_lane_light(true); /* PD14：亮 = 通行绿灯 */
            break;

        case XJ_PCMD_LANE_RED:
            dev_io_lane_light(false);
            break;

        /* §3.1.6 'G' 全亮 / §3.1.7 'H' 全关：本硬件只有一路通行灯（PD14 单灯，
         * 红绿双色靠继电器切换），无法同时点亮红绿双灯。故 'G' 取通行侧（亮）、
         * 'H' 取关闭侧（灭）——这是单灯硬件上对「全亮/全关」的唯一可表达近似，
         * 与 PDF §2「若集成通行灯……命令有效否则无效」的降级口径一致。 */
        case XJ_PCMD_LANE_BOTH_ON:
            dev_io_lane_light(true);
            break;

        case XJ_PCMD_LANE_BOTH_OFF:
            dev_io_lane_light(false);
            break;

        case XJ_PCMD_ALARM_ON:
            dev_io_flash_light(true); /* PD15：报警器 */
            break;

        case XJ_PCMD_ALARM_OFF:
            dev_io_flash_light(false);
            break;

        case XJ_PCMD_VOICE_TTS:
            _xj_exec_voice_tts(&cmd);
            break;

        case XJ_PCMD_VOICE_PRESET:
            /* 'S' 四形态按帧长判别（parse 已定 s_kind）：报价 1/2/3 拼句，其余按编号播报 */
            if (cmd.s_kind == XJ_S_KIND_PRESET)
                _xj_exec_voice_preset(&cmd);
            else
                _xj_exec_voice_quote(&cmd);
            break;

        case XJ_PCMD_HEARTBEAT:
            /* 心跳无显示/外设动作：在线状态由 app_xj_proto.c 的时刻刷新维持 */
            break;

        default:
            break;
    }
}
