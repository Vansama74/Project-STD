/**
 * @file    app_xj_proto_parse.c
 * @brief   新疆费额显示器协议帧解析与 BCC（纯函数）
 *
 * 长度字段 = 整帧总长且必须与实到长度自洽；各命令字段长度逐一校验，不匹配即
 * parsed=false（静默丢弃）。字段表与文档缺口见 doc/19_新疆费显协议/README.md §1
 */

#include "app_xj_proto_parse.h"

uint8_t xj_bcc_calc(const uint8_t *data, uint16_t len)
{
    if (data == NULL)
        return 0U;

    uint8_t bcc = 0U;
    for (uint16_t i = 0U; i < len; i++)
        bcc = (uint8_t)(bcc ^ data[i]);
    return bcc;
}

bool xj_bcc_ok(const uint8_t *frame, uint16_t frame_len)
{
    if (frame == NULL || frame_len < 2U)
        return false;
    return xj_bcc_calc(frame, (uint16_t)(frame_len - 1U)) == frame[frame_len - 1U];
}

xj_pcmd_t xj_cmd_from_byte(uint8_t c)
{
    switch (c) {
        case XJ_PCMD_TEXT:         return XJ_PCMD_TEXT;
        case XJ_PCMD_HEARTBEAT:    return XJ_PCMD_HEARTBEAT;
        case XJ_PCMD_LANE_GREEN:   return XJ_PCMD_LANE_GREEN;
        case XJ_PCMD_LANE_RED:     return XJ_PCMD_LANE_RED;
        case XJ_PCMD_LANE_BOTH_ON: return XJ_PCMD_LANE_BOTH_ON;
        case XJ_PCMD_LANE_BOTH_OFF: return XJ_PCMD_LANE_BOTH_OFF;
        case XJ_PCMD_ALARM_ON:     return XJ_PCMD_ALARM_ON;
        case XJ_PCMD_ALARM_OFF:    return XJ_PCMD_ALARM_OFF;
        case XJ_PCMD_VOICE_TTS:    return XJ_PCMD_VOICE_TTS;
        case XJ_PCMD_VOICE_PRESET: return XJ_PCMD_VOICE_PRESET;
        case XJ_PCMD_CLEAR:        return XJ_PCMD_CLEAR;
        case XJ_PCMD_LINE_OR_FULL: return XJ_PCMD_LINE_OR_FULL;
        case XJ_PCMD_QUERY:        return XJ_PCMD_QUERY;
        case XJ_PCMD_BITMAP_SHOW:  return XJ_PCMD_BITMAP_SHOW;
        default:                   return XJ_PCMD_INVALID;
    }
}

/**
 * @brief  R/G/B 三分量 → 显示颜色（各 ≥128 置对应位；组合值 0~7 与 display_color_t 恒等）。
 * @return bit0 红 / bit1 绿 / bit2 蓝；0 = 三通道全 <128（黑，执行层不画）。
 */
static display_color_t _xj_map_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    const uint8_t v = (uint8_t)(((r >= XJ_RGB_HIGH) ? 1U : 0U) | ((g >= XJ_RGB_HIGH) ? 2U : 0U) |
                                ((b >= XJ_RGB_HIGH) ? 4U : 0U));
    return (display_color_t)v;
}

/** @brief 无载荷命令（'O'/'E'/'F'/'G'/'H'/'J'/'K'/'C'/'Z'）：整帧长必须恰为 XJ_FRAME_LEN_MIN。 */
static bool _xj_len_is_bare(uint16_t raw_len)
{
    return raw_len == XJ_FRAME_LEN_MIN;
}

/**
 * @brief  'S' 帧长 → 语音形态（PDF §4.1/4.2/4.3/4.4 四种互斥帧长）。
 * @note   长度是唯一判别依据：报价 1/2/3 与其他播报的字段前缀相同（[4]语言 [5]音量），
 *         只有整帧长能把四者区分开。未知长度返回 XJ_S_KIND_INVALID。
 */
static xj_s_kind_t _xj_s_kind_from_len(uint16_t raw_len)
{
    switch (raw_len) {
        case XJ_S_QUOTE1_FRAME_LEN: return XJ_S_KIND_QUOTE1;
        case XJ_S_QUOTE2_FRAME_LEN: return XJ_S_KIND_QUOTE2;
        case XJ_S_QUOTE3_FRAME_LEN: return XJ_S_KIND_QUOTE3;
        case XJ_S_FRAME_LEN:        return XJ_S_KIND_PRESET;
        default:                    return XJ_S_KIND_INVALID;
    }
}

/**
 * @brief  拷贝 'S' 报价载荷 [6..len-2] 到 payload[]。
 * @return 拷贝字节数（0 = 载荷全非 ASCII 数字，视为非法）。
 * @note   报价载荷语义是 ASCII 数字（车型 0x30-0x37、金额/重量 0x30-0x39），但 PDF
 *         §4.1 车型上界与 §4.2 引用 0x30~0x37 不一致，此处只挡区间外的控制字节，
 *         不因个别数字越界而丢整帧（越界位在执行层原样播出，与设备行为一致）。
 */
static uint8_t _xj_copy_quote_payload(xj_parsed_cmd_t *out, const uint8_t *raw, uint16_t raw_len)
{
    const uint16_t n = (uint16_t)(raw_len - 7U); /* [6]..[len-2]，末字节为 BCC */
    if (n > (uint16_t)sizeof(out->payload))
        return 0U;

    for (uint16_t i = 0U; i < n; i++) {
        const uint8_t c = raw[XJ_S_PAYLOAD_OFFSET + i];
        if (c < XJ_S_ASCII_MIN || c > XJ_S_ASCII_MAX)
            return 0U;
        out->payload[i] = c;
    }
    out->payload_len = (uint8_t)n;
    return (uint8_t)n;
}

xj_parsed_cmd_t xj_parse_frame(const uint8_t *raw, uint16_t raw_len)
{
    xj_parsed_cmd_t out = {
        .parsed = false,
        .cmd    = XJ_PCMD_INVALID,
    };

    /* probe 已验帧头/长度字段/BCC，这里再护一道（纯函数须自洽，便于宿主推演直接调用） */
    if (raw == NULL || raw_len < XJ_FRAME_LEN_MIN || raw_len > XJ_FRAME_LEN_MAX)
        return out;
    if (raw[0] != XJ_STX0 || raw[1] != XJ_STX1)
        return out;
    if ((uint16_t)raw[XJ_LEN_OFFSET] != raw_len) /* 契约 ①：长度字段 = 整帧总长 */
        return out;
    if (!xj_bcc_ok(raw, raw_len))
        return out;

    out.cmd = xj_cmd_from_byte(raw[XJ_CMD_OFFSET]);
    if (out.cmd == XJ_PCMD_INVALID)
        return out;

    switch (out.cmd) {
        case XJ_PCMD_TEXT: { /* 'T'：内容区 = [9..len-2] = 16N（N=1~4）；[5] 显示行字段忽略（契约 ②） */
            if (raw_len <= XJ_T_TEXT_OFFSET)
                return out;
            const uint16_t content_len = (uint16_t)(raw_len - XJ_T_TEXT_OFFSET - 1U);
            if ((content_len % XJ_T_LINE_BYTES) != 0U)
                return out;
            if ((uint16_t)(content_len / XJ_T_LINE_BYTES) > XJ_T_LINE_MAX)
                return out;

            out.brightness = raw[XJ_DATA_OFFSET]; /* [4] 亮度（首个载荷字节） */
            out.color      = _xj_map_rgb(raw[6], raw[7], raw[8]);
            out.text       = &raw[XJ_T_TEXT_OFFSET];
            out.text_len   = content_len;
            break;
        }

        case XJ_PCMD_VOICE_TTS: { /* 'V'：[4]语言（忽略）[5]音量 [6..len-2] 文本（含端点） */
            if (raw_len != XJ_V_FRAME_LEN_64 && raw_len != XJ_V_FRAME_LEN_65)
                return out;
            out.volume   = raw[5];
            out.text     = &raw[6];
            out.text_len = (uint16_t)(raw_len - 7U);
            break;
        }

        case XJ_PCMD_LINE_OR_FULL: { /* 'D'：Len=23 行显示（[5] 行号 1~4）/ Len=7+16N 全屏（[5]=0） */
            if (raw_len == XJ_D_LINE_FRAME_LEN) {
                const uint8_t line = raw[5];
                if (line < XJ_D_LINE_FIELD_MIN || line > XJ_D_LINE_FIELD_MAX)
                    return out; /* 行号越界：既不按全屏兜底也不画，避免写错行残留旧字 */
                out.d_is_full = false;
                out.line      = line;
            } else {
                /* 全屏：数据区 = [6..len-2] 必须是 16 的整数倍且 2~6 行（N=4 → 71 文档表形态、
                 * N=6 → 103 PDF §5.2 采样形态；两份采样帧 BCC 均按文档规则自洽） */
                const uint16_t data_len = (uint16_t)(raw_len - XJ_D_TEXT_OFFSET - 1U);
                const uint16_t lines    = (uint16_t)(data_len / XJ_T_LINE_BYTES);
                if ((data_len % XJ_T_LINE_BYTES) != 0U || lines < XJ_D_FULL_LINE_MIN ||
                    lines > XJ_D_FULL_LINE_MAX)
                    return out; /* §3.2.1 的 'D' 是 5 字节**应答**（设备→车道机），不属本方向 */
                out.d_is_full = true;
                out.line      = 0U;
            }
            out.brightness = raw[XJ_DATA_OFFSET]; /* [4] 亮度（与 'T' 同位置） */
            out.color      = COLOR_RED;           /* 'D' 无 RGB 字段，固定红色文本 */
            out.text       = &raw[XJ_D_TEXT_OFFSET];
            out.text_len   = (uint16_t)(raw_len - XJ_D_TEXT_OFFSET - 1U);
            break;
        }

        case XJ_PCMD_BITMAP_SHOW: { /* 'P'：Len=7，[4]亮度 [5]图号 */
            if (raw_len != XJ_FRAME_LEN_MIN + 2U)
                return out;
            out.brightness = raw[XJ_DATA_OFFSET];
            out.bitmap_no  = raw[5];
            break;
        }

        case XJ_PCMD_VOICE_PRESET: { /* 'S'：四种帧长分派（报价 1/2/3 与 §4.4 预制编号） */
            out.s_kind = _xj_s_kind_from_len(raw_len);
            if (out.s_kind == XJ_S_KIND_INVALID)
                return out;

            out.volume = raw[5]; /* [5] 音量（四形态同位置） */
            if (out.s_kind == XJ_S_KIND_PRESET) {
                out.preset_no = raw[6]; /* [6] 预制编号 0xA0~0xB6 */
            } else if (_xj_copy_quote_payload(&out, raw, raw_len) == 0U) {
                return out; /* 报价载荷非 ASCII 数字 */
            }
            break;
        }

        /* 'O'/'E'/'F'/'G'/'H'/'J'/'K'/'C'/'Z'：无载荷，整帧长必须为 5 */
        case XJ_PCMD_HEARTBEAT:
        case XJ_PCMD_LANE_GREEN:
        case XJ_PCMD_LANE_RED:
        case XJ_PCMD_LANE_BOTH_ON:
        case XJ_PCMD_LANE_BOTH_OFF:
        case XJ_PCMD_ALARM_ON:
        case XJ_PCMD_ALARM_OFF:
        case XJ_PCMD_CLEAR:
        case XJ_PCMD_QUERY:
            if (!_xj_len_is_bare(raw_len))
                return out;
            break;

        default: /* XJ_PCMD_INVALID 已在上方拦截，此处兜底 */
            return out;
    }

    out.parsed = true;
    return out;
}
