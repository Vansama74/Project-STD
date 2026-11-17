/**
 * @file    app_gx_proto_parse.c
 * @brief   广西费显协议帧解析与累加和（纯函数）
 *
 * 契约一览见 app_gx_proto.h 文件头（本文件实现 ① 文本上限与定界前提）；
 * 逐字段取值与文档/旧固件差异见 doc/21_广西费显协议/README.md §1、§3
 */

#include "app_gx_proto_parse.h"

gx_cmd_t gx_cmd_from_pair(uint8_t c1, uint8_t c2)
{
    if (c1 == 0x51U && c2 == 0x54U)
        return GX_CMD_DISPLAY;
    if (c1 == 0x4BU && c2 == 0x4CU)
        return GX_CMD_BRIGHTNESS;
    if (c1 == 0x4BU && c2 == 0x5AU)
        return GX_CMD_LANE;
    if (c1 == 0x56U && c2 == 0x4FU)
        return GX_CMD_VOICE;
    if (c1 == 0x01U && c2 == 0x01U)
        return GX_CMD_FULLSCREEN;
    if (c1 == 0x02U && c2 == 0x02U)
        return GX_CMD_VERSION;
    if (c1 == 0x03U && c2 == 0x03U)
        return GX_CMD_BOOT_SCREEN;
    if (c1 == 0x04U && c2 == 0x04U)
        return GX_CMD_FONT;
    return GX_CMD_INVALID;
}

gx_lamp_t gx_lamp_from_triplet(const uint8_t *triple)
{
    if (triple == NULL)
        return GX_LAMP_INVALID;

    const uint8_t a = triple[0];
    const uint8_t b = triple[1];
    const uint8_t c = triple[2];

    if (a == 0xFFU && b == 0x00U && c == 0xFFU)
        return GX_LAMP_GREEN_ON;
    if (a == 0x00U && b == 0x00U && c == 0x00U)
        return GX_LAMP_GREEN_OFF;
    if (a == 0x01U && b == 0x01U && c == 0x00U)
        return GX_LAMP_RED_ON;
    if (a == 0x00U && b == 0x01U && c == 0xFFU)
        return GX_LAMP_RED_OFF;
    if (a == 0x01U && b == 0x02U && c == 0xFFU)
        return GX_LAMP_ALARM_ON;
    if (a == 0x02U && b == 0x02U && c == 0x00U)
        return GX_LAMP_ALARM_OFF;
    return GX_LAMP_INVALID;
}

uint8_t gx_sum_calc(const uint8_t *data, uint16_t len)
{
    if (data == NULL)
        return 0U;

    uint8_t sum = 0U;
    for (uint16_t i = 0U; i < len; i++)
        sum = (uint8_t)(sum + data[i]);
    return sum;
}

/** @brief 收费金额 7 位 ASCII 数字校验（非数字返回 false）。 */
static bool _gx_amount_digits_ok(const uint8_t *p)
{
    for (uint8_t i = 0U; i < GX_VOICE_AMOUNT_DIGITS; i++) {
        if (p[i] < '0' || p[i] > '9')
            return false;
    }
    return true;
}

gx_parsed_cmd_t gx_parse_frame(const uint8_t *raw, uint16_t raw_len)
{
    gx_parsed_cmd_t out = {
        .parsed = false,
        .cmd    = GX_CMD_INVALID,
    };

    /* probe 已验帧头与帧长，这里再护一道（纯函数须自洽，便于宿主推演直接调用） */
    if (raw == NULL || raw_len < GX_CMD_LEN)
        return out;
    if (raw[0] != GX_STX0 || raw[GX_STXB_OFFSET] != GX_STXB)
        return out;

    /* 契约：v1 不判地址（GX_LOCAL_ADDR = 0）；启用过滤时放行本机地址与广播 */
#if GX_LOCAL_ADDR != 0
    if (raw[GX_ADDR_OFFSET] != (uint8_t)GX_LOCAL_ADDR && raw[GX_ADDR_OFFSET] != GX_BROADCAST_ADDR)
        return out;
#endif

    out.address = raw[GX_ADDR_OFFSET];
    out.cmd     = gx_cmd_from_pair(raw[GX_CMD1_OFFSET], raw[GX_CMD2_OFFSET]);
    if (out.cmd == GX_CMD_INVALID)
        return out;

    switch (out.cmd) {
        case GX_CMD_DISPLAY: {
            /* 51 54：AA addr BB 51 54 sum play speed stay color time text… FF */
            if (raw_len < GX_DISPLAY_FRAME_LEN_MIN || raw_len > GX_FRAME_LEN_MAX)
                return out;
            if (raw[raw_len - 1U] != GX_ETX)
                return out;

            const uint16_t text_len = (uint16_t)(raw_len - GX_DISPLAY_TEXT_OFFSET - 1U);
            if (text_len > GX_TEXT_MAX)
                return out;
            if (raw[GX_DISPLAY_COLOR_OFFSET] > GX_COLOR_MAX)
                return out;

            out.playback        = raw[GX_DISPLAY_PLAYBACK_OFFSET]; /* v1 忽略 */
            out.speed           = raw[GX_DISPLAY_SPEED_OFFSET];    /* v1 忽略 */
            out.stay            = raw[GX_DISPLAY_STAY_OFFSET];     /* v1 忽略 */
            out.color           = raw[GX_DISPLAY_COLOR_OFFSET];
            out.display_seconds = raw[GX_DISPLAY_TIME_OFFSET];
            out.text            = &raw[GX_DISPLAY_TEXT_OFFSET];
            out.text_len        = text_len;
            /* [5] 数据累加和：v1 解析但丢弃该字节（不校验，与现场旧固件一致） */
            break;
        }

        case GX_CMD_BRIGHTNESS: {
            /* 4B 4C：7B，[5] 亮度 0~3（[6] 同值，旧固件与 v1 均忽略） */
            if (raw_len != GX_FRAME_LEN_BRIGHTNESS)
                return out;
            if (raw[GX_SUM_OFFSET] > 3U)
                return out;
            out.brightness = raw[GX_SUM_OFFSET];
            break;
        }

        case GX_CMD_LANE: {
            /* 4B 5A：8B，[5..7] 六种固定三元组 */
            if (raw_len != GX_FRAME_LEN_LANE)
                return out;
            out.lamp = gx_lamp_from_triplet(&raw[GX_SUM_OFFSET]);
            if (out.lamp == GX_LAMP_INVALID)
                return out;
            break;
        }

        case GX_CMD_VOICE: {
            /* 56 4F：[5] 子命令 0 礼貌语（[6] 编号）/ 1 收费（[6..12] 七位 ASCII 金额） */
            if (raw_len < GX_VOICE_POLITE_FRAME_LEN)
                return out;
            out.voice_kind = raw[GX_VOICE_KIND_OFFSET];

            if (out.voice_kind == GX_VOICE_KIND_POLITE) {
                if (raw_len != GX_VOICE_POLITE_FRAME_LEN)
                    return out;
                out.voice_code = raw[GX_VOICE_CODE_OFFSET];
                if (out.voice_code < GX_VOICE_CODE_MIN || out.voice_code > GX_VOICE_CODE_MAX)
                    return out;
            } else if (out.voice_kind == GX_VOICE_KIND_FEE) {
                if (raw_len != GX_VOICE_FEE_FRAME_LEN)
                    return out;
                if (!_gx_amount_digits_ok(&raw[GX_VOICE_AMOUNT_OFFSET]))
                    return out;
                for (uint8_t i = 0U; i < GX_VOICE_AMOUNT_DIGITS; i++)
                    out.amount[i] = raw[GX_VOICE_AMOUNT_OFFSET + i];
            } else {
                return out;
            }
            break;
        }

        case GX_CMD_FULLSCREEN: {
            /* 01 01：7B，[5] 与 [6] 必须同为 0/1/2（3 及更大的色码丢弃） */
            if (raw_len != GX_FRAME_LEN_FULLSCREEN)
                return out;
            if (raw[GX_SUM_OFFSET] != raw[GX_DATA_OFFSET])
                return out;
            if (raw[GX_SUM_OFFSET] > GX_COLOR_MAX)
                return out;
            out.color = raw[GX_SUM_OFFSET];
            break;
        }

        case GX_CMD_VERSION: {
            /* 02 02：5B，无载荷 */
            if (raw_len != GX_FRAME_LEN_VERSION)
                return out;
            break;
        }

        case GX_CMD_BOOT_SCREEN: {
            /* 03 03：103B = [5] 色 + 96B（6 行 × 16B）+ [102] 0xFF */
            if (raw_len != GX_FRAME_LEN_BOOT)
                return out;
            if (raw[GX_FRAME_LEN_BOOT - 1U] != GX_ETX)
                return out;
            if (raw[GX_BOOT_COLOR_OFFSET] > GX_COLOR_MAX)
                return out;
            out.color    = raw[GX_BOOT_COLOR_OFFSET];
            out.text     = &raw[GX_BOOT_TEXT_OFFSET];
            out.text_len = GX_BOOT_TEXT_LEN;
            break;
        }

        case GX_CMD_FONT: {
            /* 04 04：6B，[5] 低半字节字号 / 高半字节字型（越界半字节由执行层保持原值） */
            if (raw_len != GX_FRAME_LEN_FONT)
                return out;
            out.font_size_code = (uint8_t)(raw[GX_FONT_CODE_OFFSET] & 0x0FU);
            out.font_type_code = (uint8_t)(raw[GX_FONT_CODE_OFFSET] >> 4);
            break;
        }

        default: /* GX_CMD_INVALID 已在上方拦截，此处兜底 */
            return out;
    }

    out.parsed = true;
    return out;
}
