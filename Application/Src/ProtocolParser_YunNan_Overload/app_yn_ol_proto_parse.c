/**
 * @file    app_yn_ol_proto_parse.c
 * @brief   云南治超屏协议帧解析（纯函数：不发包、不渲染、不碰外设）
 *
 * 长度字段与实到长度必须自洽（无参命令要求恰为 0、0x47 要求 ≥14B）；颜色/行号接受
 * ASCII 与二进制两种编码（只放宽、不改语义）。
 * 详见 doc/15_云南治超屏协议/README.md §1、§2、§11.1
 */

#include "app_yn_ol_proto_parse.h"

/* 字段编码容错（只放宽、不改语义）：'3'/'4' 的颜色、'3' 的行号 ASCII 与二进制原值两端都接受，
 * 越界仍整帧丢弃；口径与已裁决的 '8' 亮度超集一致（背景见 doc/15_云南治超屏协议/README.md §11.1） */

/** @brief 颜色字段容错解析：ASCII '0'~'2' 或二进制 0x00~0x02（越界返回 false）。 */
static bool _yn_ol_parse_color(uint8_t b, uint8_t *out)
{
    if (b >= 0x30U && b <= 0x32U) { /* ASCII '0'~'2' */
        *out = (uint8_t)(b - 0x30U);
        return true;
    }
    if (b <= 0x02U) { /* 二进制 0x00~0x02 */
        *out = b;
        return true;
    }
    return false;
}

/** @brief 行号字段容错解析：ASCII '1'~'5' 或二进制 0x01~0x05（越界返回 false）。 */
static bool _yn_ol_parse_row(uint8_t b, uint8_t *out)
{
    if (b >= 0x31U && b <= 0x35U) { /* ASCII '1'~'5' */
        *out = (uint8_t)(b - 0x31U);
        return true;
    }
    if (b >= 0x01U && b <= 0x05U) { /* 二进制 0x01~0x05 */
        *out = (uint8_t)(b - 0x01U);
        return true;
    }
    return false;
}

/**
 * @brief  命令字字节 → 命令枚举（probe 白名单与 parse 共用的单一真源）。
 * @param  c  命令字字节。
 * @return 对应枚举；'6'/'7'/'9'（治超屏不开发）/'B' 及未知字节返回 YN_OL_PCMD_INVALID。
 */
yn_ol_pcmd_t yn_ol_cmd_from_byte(uint8_t c)
{
    switch (c) {
        case YN_OL_PCMD_HOST_QUERY:   return YN_OL_PCMD_HOST_QUERY;
        case YN_OL_PCMD_SELF_CHECK:   return YN_OL_PCMD_SELF_CHECK;
        case YN_OL_PCMD_ONE_LINE:     return YN_OL_PCMD_ONE_LINE;
        case YN_OL_PCMD_FULL_SCREEN:  return YN_OL_PCMD_FULL_SCREEN;
        case YN_OL_PCMD_CLEAR:        return YN_OL_PCMD_CLEAR;
        case YN_OL_PCMD_BRIGHTNESS:   return YN_OL_PCMD_BRIGHTNESS;
        case YN_OL_PCMD_PERIPHERAL:   return YN_OL_PCMD_PERIPHERAL;
        case YN_OL_PCMD_CLEAR_ROW1:   return YN_OL_PCMD_CLEAR_ROW1;
        case YN_OL_PCMD_CLEAR_ROW2:   return YN_OL_PCMD_CLEAR_ROW2;
        case YN_OL_PCMD_CLEAR_ROW3:   return YN_OL_PCMD_CLEAR_ROW3;
        case YN_OL_PCMD_CLEAR_ROW4:   return YN_OL_PCMD_CLEAR_ROW4;
        case YN_OL_PCMD_CLEAR_ROW5:   return YN_OL_PCMD_CLEAR_ROW5;
        case YN_OL_PCMD_CLEAR_ROW6:   return YN_OL_PCMD_CLEAR_ROW6;
        case YN_OL_PCMD_SET_IP:       return YN_OL_PCMD_SET_IP;
        case YN_OL_PCMD_GET_IP:       return YN_OL_PCMD_GET_IP;
        case YN_OL_PCMD_SCREEN_PARAM: return YN_OL_PCMD_SCREEN_PARAM;
        case YN_OL_PCMD_GET_IP_ACK:   return YN_OL_PCMD_GET_IP_ACK;
        case YN_OL_PCMD_FILL_ALL:     return YN_OL_PCMD_FILL_ALL;
        case YN_OL_PCMD_VERSION:      return YN_OL_PCMD_VERSION;
        default:                      return YN_OL_PCMD_INVALID;
    }
}

yn_ol_parsed_cmd_t yn_ol_parse_frame(const uint8_t *raw, uint16_t raw_len)
{
    yn_ol_parsed_cmd_t cmd = {0};
    cmd.sta                = YN_OL_PARSE_ERR_FRAME;

    if (raw == nullptr || raw_len < YN_OL_FRAME_LEN_MIN || raw_len > YN_OL_FRAME_LEN_MAX)
        return cmd;
    if (raw[0] != YN_OL_STX || raw[raw_len - 1U] != YN_OL_ETX)
        return cmd;

    /* 长度字段（二进制字节值）= 参数区字节数；与实到长度必须自洽 */
    const uint16_t declared = raw[YN_OL_LEN_OFFSET];
    if ((uint16_t)(raw_len - YN_OL_FRAME_OVERHEAD) != declared)
        return cmd;

    cmd.cmd      = yn_ol_cmd_from_byte(raw[YN_OL_CMD_OFFSET]);
    cmd.data     = &raw[YN_OL_DATA_OFFSET];
    cmd.data_len = declared;

    if (cmd.cmd == YN_OL_PCMD_INVALID) {
        cmd.sta = YN_OL_PARSE_ERR_CMD;
        return cmd;
    }

    switch (cmd.cmd) {
        case YN_OL_PCMD_HOST_QUERY: /* '1' 主机查询：无参数（帧例 7B 31 00 7D） */
            if (declared != 0U) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            cmd.sta = YN_OL_PARSE_OK;
            break;

        case YN_OL_PCMD_SELF_CHECK: /* '2' 自检：无参数（帧例 7B 32 00 7D） */
            if (declared != 0U) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            cmd.sta = YN_OL_PARSE_OK;
            break;

        case YN_OL_PCMD_ONE_LINE: { /* '3' 单行：颜色 + 行号 + GBK 文本（≥3B；超宽由渲染层截断） */
            if (declared < (YN_OL_ONE_LINE_PREFIX_LEN + 1U)) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            uint8_t color;
            uint8_t row;
            if (!_yn_ol_parse_color(cmd.data[0], &color) || !_yn_ol_parse_row(cmd.data[1], &row)) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.one_line.color    = color;
            cmd.p.one_line.row      = row;
            cmd.p.one_line.text     = &cmd.data[YN_OL_ONE_LINE_PREFIX_LEN];
            cmd.p.one_line.text_len = (uint16_t)(declared - YN_OL_ONE_LINE_PREFIX_LEN);
            cmd.sta                 = YN_OL_PARSE_OK;
            break;
        }

        case YN_OL_PCMD_FULL_SCREEN: { /* '4' 全屏：颜色 + X + Y + 文本 */
            if (declared < (YN_OL_FULL_SCREEN_PREFIX_LEN + 1U)) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            uint8_t color;
            if (!_yn_ol_parse_color(cmd.data[0], &color)) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.full_screen.color    = color;
            cmd.p.full_screen.x        = cmd.data[1];
            cmd.p.full_screen.y        = cmd.data[2];
            cmd.p.full_screen.text     = &cmd.data[YN_OL_FULL_SCREEN_PREFIX_LEN];
            cmd.p.full_screen.text_len = (uint16_t)(declared - YN_OL_FULL_SCREEN_PREFIX_LEN);
            cmd.sta                    = YN_OL_PARSE_OK;
            break;
        }

        case YN_OL_PCMD_CLEAR: /* '5' 全屏清除：无参数（帧例 7B 35 00 7D） */
            if (declared != 0U) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            cmd.sta = YN_OL_PARSE_OK;
            break;

        case YN_OL_PCMD_BRIGHTNESS: /* '8' 亮度：0x00/'0' = 自动；'1'~'8' = 手动档（超集已裁决） */
            if (declared != 1U) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            if (cmd.data[0] == 0x00U || cmd.data[0] == '0') {
                cmd.p.brightness = 0U; /* 0 = 自动亮度 */
            } else if (cmd.data[0] >= '1' && cmd.data[0] <= '8') {
                cmd.p.brightness = (uint8_t)(cmd.data[0] - '0');
            } else {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            cmd.sta = YN_OL_PARSE_OK;
            break;

        case YN_OL_PCMD_PERIPHERAL: /* 'A' 外设：bit0 绿灯 / bit1 红灯 / bit2 黄闪报警 */
            if (declared != 1U) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.peripheral = cmd.data[0];
            cmd.sta          = YN_OL_PARSE_OK;
            break;

        /* 0x42~0x46 / 0x50 行清除：无参数（帧例 7B xx 00 7D） */
        case YN_OL_PCMD_CLEAR_ROW1:
        case YN_OL_PCMD_CLEAR_ROW2:
        case YN_OL_PCMD_CLEAR_ROW3:
        case YN_OL_PCMD_CLEAR_ROW4:
        case YN_OL_PCMD_CLEAR_ROW5:
        case YN_OL_PCMD_CLEAR_ROW6:
            if (declared != 0U) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.clear_row = (uint8_t)(cmd.cmd - YN_OL_PCMD_CLEAR_ROW1); /* 0x42→0 … 0x46→4 */
            if (cmd.cmd == YN_OL_PCMD_CLEAR_ROW6)
                cmd.p.clear_row = 5U; /* 0x50 = 第六行（不连续编号，单独映射） */
            cmd.sta = YN_OL_PARSE_OK;
            break;

        case YN_OL_PCMD_SET_IP: { /* 0x47 修改 IP：ip4 + mask4 + gw4 + port2 */
            if (declared < YN_OL_NET_PAYLOAD_LEN) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            for (uint8_t i = 0U; i < 4U; i++) {
                cmd.p.setip.ip[i]   = cmd.data[i];
                cmd.p.setip.mask[i] = cmd.data[4U + i];
                cmd.p.setip.gw[i]   = cmd.data[8U + i];
            }
            /* 端口字段 BE16（契约 ①；同厂 GZ_OL 先例与 LDI/IAP 同序） */
            cmd.p.setip.port = (uint16_t)(((uint16_t)cmd.data[12] << 8) | (uint16_t)cmd.data[13]);
            cmd.sta          = YN_OL_PARSE_OK;
            break;
        }

        case YN_OL_PCMD_GET_IP: /* 0x48 查询 IP：无参数（帧例 7B 48 00 7D） */
            if (declared != 0U) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            cmd.sta = YN_OL_PARSE_OK;
            break;

        case YN_OL_PCMD_SCREEN_PARAM: { /* 0x49 设置屏体参数：x0 字体 + x1 字宽 */
            if (declared != YN_OL_SCREEN_PARAM_LEN) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            if (cmd.data[0] > 3U || cmd.data[1] > 2U) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.screen.font_type = (font_type_t)cmd.data[0];
            cmd.p.screen.font_size = (cmd.data[1] == 0U)   ? FONT_16
                                     : (cmd.data[1] == 1U) ? FONT_24
                                                           : FONT_32;
            cmd.sta                = YN_OL_PARSE_OK;
            break;
        }

        case YN_OL_PCMD_GET_IP_ACK: /* 0x51 仅出站应答命令字：入站静默丢弃 */
            cmd.sta = YN_OL_PARSE_ERR_CMD;
            break;

        case YN_OL_PCMD_FILL_ALL: /* 0x01 全屏点亮：01红/02绿/03黄（04~07 扩展保留） */
            if (declared != 1U || cmd.data[0] < 1U || cmd.data[0] > 7U) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.fill_color = cmd.data[0];
            cmd.sta          = YN_OL_PARSE_OK;
            break;

        case YN_OL_PCMD_VERSION: /* 0x02 版本号：参数 1B（文档帧例 7B 02 01 00 7D） */
            if (declared != 1U) {
                cmd.sta = YN_OL_PARSE_ERR_PARAM;
                break;
            }
            cmd.sta = YN_OL_PARSE_OK;
            break;

        default:
            cmd.sta = YN_OL_PARSE_ERR_CMD;
            break;
    }

    return cmd;
}
