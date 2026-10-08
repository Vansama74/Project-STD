/**
 * @file    app_yn_ol_proto_parse.c
 * @brief   云南治超屏协议（YN_1.3.0，`{` 帧族）帧解析（纯解析：不发包、不渲染、不碰外设）
 *
 * 帧格式：'{' + 命令字 + 二进制长度 + 参数 + '}'（与云南常规协议完全同构）。
 * 协议原文：《车道费额显示器通信协议》YN_1.3.0（2021-11-24，治超屏裁剪版）。
 *
 * 解析纪律：
 *   - 长度字段必须等于实到参数区字节数（probe 按同一公式定界，此处二次防御）；
 *   - **文档明示治超屏不开发的命令字（'6' 固定格式 / '7' 礼貌用语语音 / '9' 音量）
 *     在 probe 层已快拒**；本模块对残留的此类帧（如自其它通道转发）按不支持整帧丢弃，
 *     与文档「不开发」语义一致（协议未定义错误应答 → 静默）；
 *   - 参数长度按各命令文档帧例校验：无参命令要求长度恰为 0（帧例 `7B xx 00 7D`）；
 *     0x47 要求 ≥14B（多余字节忽略，防越界读）；
 *   - 颜色索引 0/1/2（红/绿/黄）越界 → 整帧丢弃（文档只定义三色）。
 *   - '3' 单行的颜色/行号与 '4' 的颜色**接受 ASCII 与二进制两种编码**（超集容错，
 *     2026-09-17 联调轮：只放宽、不改变语义；见下方 `_yn_ol_parse_color/_row`）：
 *     颜色 ASCII '0'~'2' 或 0x00~0x02；行号 ASCII '1'~'5' 或 0x01~0x05。
 *   - 0x47/0x51 端口字段 = **高字节在前 BE16**（**已裁决**，见 app_yn_ol_proto.h 文件头
 *     「端口字段字节序」）。
 */

#include "app_yn_ol_proto_parse.h"

/* ---- 字段编码容错（2026-09-17 联调轮；**只放宽、不改变语义**）----
 * 背景：现场上位机对 '3' 单行的颜色/行号可能发 ASCII 码（文档帧例口径
 * `7B 33 03 30 31 41 7D` = '0' '1' 'A'）也可能发二进制原值；两者字节不重叠
 * （ASCII '0'~'2' = 0x30~0x32、'1'~'5' = 0x31~0x35；二进制仅 0x00~0x02 / 0x01~0x05）。
 * 口径与已裁决的 '8' 亮度超集一致（`'0'`/0x00 两端均接受）→ 现场两种上位机都能动。
 * 越界值仍整帧丢弃（不引入新的接受域）。 */

/**
 * @brief  颜色字段容错解析：ASCII '0'~'2' 或二进制 0x00~0x02。
 * @param  b    原始字节（'3'/'4' 参数区第 0 字节）。
 * @param  out  输出颜色索引 0/1/2（红/绿/黄）。
 * @return true = 合法；false = 越界（调用方整帧丢弃）。
 */
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

/**
 * @brief  行号字段容错解析：ASCII '1'~'5' 或二进制 0x01~0x05。
 * @param  b    原始字节。
 * @param  out  输出行号 0~4（协议 '1'~'5'）。
 * @return true = 合法；false = 越界（调用方整帧丢弃）。
 */
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
 *
 * 仅列出本模块**实现**的命令字；'6'/'7'/'9'（治超屏不开发）与 'B'（云南常规专有）
 * 及未知字节一律 YN_OL_PCMD_INVALID。
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

        case YN_OL_PCMD_ONE_LINE: { /* '3' 单行：颜色 + 行号 + 文本 */
            /* 参数至少 2 字节头 + 1 字节文本；文本上限不设（1B 长度字段天然 ≤253，
             * 超宽由渲染层按屏宽截断；行号 '1'~'5' → 0~4，行 5 在屏高不足时
             * 「执行但不落屏」——与云南常规 '6'/行 5 口径一致）。 */
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

        case YN_OL_PCMD_BRIGHTNESS: /* '8' 亮度：'0'/0x00 = 自动；'1'~'8' = 手动档 */
            /* 文档口径为 ASCII '0'~'5'（5 最亮）；本实现按云南常规先例接受超集：
             * 0x00（NUL）与 '0' = 自动；'1'~'8' = 手动档（硬件 8 档，恒等映射）。 */
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
            /* 端口字段字节序（**已裁决：高字节在前 BE16**，2026-09-17）：文档未明示，
             * 取同厂 GZ_OL 先例与 LDI/IAP 既有口径 → 写 9528 发 `25 38`。 */
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
