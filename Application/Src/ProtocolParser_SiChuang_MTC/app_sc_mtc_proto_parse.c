/**
 * @file    app_sc_mtc_proto_parse.c
 * @brief   四川 MTC 费显协议（1E 方案二）帧解析
 *
 * '{' 帧族：'{' + 命令 + 参数 [+BCC] + '}'。参考项目 9K1F212701 mtc.c 不携带/不校验
 * BCC；本实现同时兼容带 BCC 变体（帧长 +1），BCC 一律不校验，按帧长定位字段。
 * 7B 40~45 原始帧族无 BCC（文档示例即全帧）。
 * 0A 帧族：0A 46 0A 主机查询 / 0A 46 0D 清屏。
 */

#include "app_sc_mtc_proto_parse.h"

/**
 * @brief  将 '{' 帧族命令字符转换为内部命令枚举。
 * @param  c  命令字节。
 * @return 对应的协议命令枚举；非法返回 SC_MTC_PCMD_INVALID。
 */
static sc_mtc_pcmd_t _sc_mtc_char_to_cmd(uint8_t c)
{
    switch (c) {
        case '1': return SC_MTC_PCMD_INIT;
        case '2': return SC_MTC_PCMD_SELF_CHECK;
        case '3': return SC_MTC_PCMD_ONE_LINE;
        case '4': return SC_MTC_PCMD_FULL_SCREEN;
        case '5': return SC_MTC_PCMD_CLEAR;
        case '6': return SC_MTC_PCMD_FIXED_DISPLAY;
        case '7': return SC_MTC_PCMD_VOICE;
        case '8': return SC_MTC_PCMD_BRIGHTNESS;
        case '9': return SC_MTC_PCMD_VOLUME;
        case 'A': return SC_MTC_PCMD_COLOR; /* 0x41 与 'A' 同值，按帧长在 parse 中细分 */
        case 0x40: return SC_MTC_PCMD_RAW_BAUD;
        case 0x42: return SC_MTC_PCMD_RAW_FONT;
        case 0x43: return SC_MTC_PCMD_RAW_PROTO;
        case 0x44: return SC_MTC_PCMD_RAW_FILL_ALL;
        case 0x45: return SC_MTC_PCMD_RAW_VERSION;
        default:   return SC_MTC_PCMD_INVALID;
    }
}

/**
 * @brief  解析 MTC 协议原始帧。
 * @param  raw      原始帧数据。
 * @param  raw_len  原始帧长度。
 * @return 解析后的命令结构体；状态字段 sta 标识解析结果。
 */
sc_mtc_parsed_cmd_t sc_mtc_parse_frame(const uint8_t *raw, uint16_t raw_len)
{
    sc_mtc_parsed_cmd_t cmd = {0};
    if (!raw || raw_len < 3) {
        cmd.sta = SC_MTC_PARSE_ERR_FRAME;
        return cmd;
    }

    /* ---- 0A 帧族：主机查询 / 清屏 ---- */
    if (raw[0] == 0x0A) {
        if (raw_len != 3U) {
            cmd.sta = SC_MTC_PARSE_ERR_FRAME;
            return cmd;
        }
        if (raw[1] == 0x46 && raw[2] == 0x0A) {
            cmd.cmd = SC_MTC_PCMD_HOST_QUERY;
            cmd.sta = SC_MTC_PARSE_OK;
        } else if (raw[1] == 0x46 && raw[2] == 0x0D) {
            cmd.cmd = SC_MTC_PCMD_HOST_CLEAR;
            cmd.sta = SC_MTC_PARSE_OK;
        } else {
            cmd.cmd = SC_MTC_PCMD_INVALID;
            cmd.sta = SC_MTC_PARSE_ERR_CMD;
        }
        return cmd;
    }

    /* ---- '{' 帧族 ---- */
    if (raw[0] != '{' || raw[raw_len - 1U] != '}') {
        cmd.sta = SC_MTC_PARSE_ERR_FRAME;
        return cmd;
    }

    cmd.cmd = _sc_mtc_char_to_cmd(raw[1]);
    if (cmd.cmd == SC_MTC_PCMD_INVALID) {
        cmd.sta = SC_MTC_PARSE_ERR_CMD;
        return cmd;
    }

    cmd.data     = &raw[2];
    cmd.data_len = (uint16_t)(raw_len - 3U); /* 去掉 '{' + 命令 + '}' */

    /* ---- 0x41 双义：'A'(颜色, 5B+BCC) 与 7B 41(点阵大小, 4B 无 BCC) ---- */
    if (raw[1] == 0x41) {
        if (raw_len == 4U) {
            cmd.cmd            = SC_MTC_PCMD_RAW_DOT_SIZE;
            cmd.p.byte_val.val = raw[2];
            cmd.sta = (raw[2] <= 2U) ? SC_MTC_PARSE_OK : SC_MTC_PARSE_ERR_PARAM;
        } else if (raw_len == 5U) {
            cmd.cmd            = SC_MTC_PCMD_COLOR;
            cmd.p.byte_val.val = (uint8_t)(raw[2] - '0');
            cmd.sta = (raw[2] >= '1' && raw[2] <= '3') ? SC_MTC_PARSE_OK : SC_MTC_PARSE_ERR_PARAM;
        } else {
            cmd.sta = SC_MTC_PARSE_ERR_FRAME;
        }
        return cmd;
    }

    /* ---- 7B 40~45 原始帧族（无 BCC）---- */
    switch (cmd.cmd) {
        case SC_MTC_PCMD_RAW_BAUD:
            if (raw_len != 6U) {
                cmd.sta = SC_MTC_PARSE_ERR_FRAME;
                return cmd;
            }
            cmd.p.baud.mode   = raw[2];
            cmd.p.baud.baud16 = (uint16_t)(((uint16_t)raw[3] << 8) | raw[4]);
            if (cmd.p.baud.mode > 1U ||
                (cmd.p.baud.baud16 != 0x2580U && cmd.p.baud.baud16 != 0xC200U)) {
                cmd.sta = SC_MTC_PARSE_ERR_PARAM;
            } else {
                cmd.sta = SC_MTC_PARSE_OK;
            }
            return cmd;
        case SC_MTC_PCMD_RAW_FONT:
            if (raw_len != 4U) {
                cmd.sta = SC_MTC_PARSE_ERR_FRAME;
                return cmd;
            }
            cmd.p.byte_val.val = raw[2];
            cmd.sta = (raw[2] <= 3U) ? SC_MTC_PARSE_OK : SC_MTC_PARSE_ERR_PARAM;
            return cmd;
        case SC_MTC_PCMD_RAW_PROTO:
            if (raw_len != 4U) {
                cmd.sta = SC_MTC_PARSE_ERR_FRAME;
                return cmd;
            }
            cmd.p.byte_val.val = raw[2];
            cmd.sta = (raw[2] <= 2U) ? SC_MTC_PARSE_OK : SC_MTC_PARSE_ERR_PARAM;
            return cmd;
        case SC_MTC_PCMD_RAW_FILL_ALL:
            if (raw_len != 4U) {
                cmd.sta = SC_MTC_PARSE_ERR_FRAME;
                return cmd;
            }
            cmd.p.byte_val.val = raw[2];
            cmd.sta = (raw[2] <= 2U) ? SC_MTC_PARSE_OK : SC_MTC_PARSE_ERR_PARAM;
            return cmd;
        case SC_MTC_PCMD_RAW_VERSION:
            if (raw_len != 3U) {
                cmd.sta = SC_MTC_PARSE_ERR_FRAME;
                return cmd;
            }
            cmd.sta = SC_MTC_PARSE_OK;
            return cmd;
        default:
            break;
    }

    /* ---- '1'~'9','A' 帧族：无 BCC 参考格式（9K1F212701）+ 带 BCC 变体，
     * BCC 不校验（参考项目 mtc.c 不校验 BCC），按帧长定位字段 ---- */
    switch (cmd.cmd) {
        case SC_MTC_PCMD_INIT:
        case SC_MTC_PCMD_SELF_CHECK:
        case SC_MTC_PCMD_CLEAR:
            cmd.sta = (raw_len == 3U || raw_len == 4U) ? SC_MTC_PARSE_OK : SC_MTC_PARSE_ERR_PARAM;
            break;

        case SC_MTC_PCMD_ONE_LINE:
            if (raw_len != 20U && raw_len != 21U) {
                cmd.sta = SC_MTC_PARSE_ERR_PARAM;
                break;
            }
            /* 参考 9K1F212701 行号 '1'~'8'；本屏 192×96 最多 6 行 16pt，收 '1'~'6' */
            if (raw[2] < '1' || raw[2] > '6') {
                cmd.sta = SC_MTC_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.one_line.row      = (uint8_t)(raw[2] - '1');
            cmd.p.one_line.text     = &raw[3];
            cmd.p.one_line.text_len = 16U; /* 两种格式数据区均为 16B */
            cmd.sta = SC_MTC_PARSE_OK;
            break;

        case SC_MTC_PCMD_FULL_SCREEN:
            if (raw_len != 67U && raw_len != 68U) {
                cmd.sta = SC_MTC_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.full_screen.text     = &raw[2];
            cmd.p.full_screen.text_len = 64U; /* 两种格式数据区均为 64B */
            cmd.sta = SC_MTC_PARSE_OK;
            break;

        case SC_MTC_PCMD_FIXED_DISPLAY: {
            /* 客车 15/16B，货车 24/25B；偶数长=带 BCC */
            bool has_bcc = (raw_len == 16U || raw_len == 25U);
            uint16_t data_bytes = (uint16_t)(raw_len - 4U - (has_bcc ? 1U : 0U));
            if ((raw_len != 15U && raw_len != 16U && raw_len != 24U && raw_len != 25U)) {
                cmd.sta = SC_MTC_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.fixed.type    = (uint8_t)(raw[2] - '0');
            cmd.p.fixed.raw     = &raw[3];
            cmd.p.fixed.raw_len = data_bytes; /* X1 起的参数字节（11/20） */
            cmd.sta = (cmd.p.fixed.type <= 1U) ? SC_MTC_PARSE_OK : SC_MTC_PARSE_ERR_PARAM;
            break;
        }

        case SC_MTC_PCMD_VOICE:
            if (raw_len < 4U) {
                cmd.sta = SC_MTC_PARSE_ERR_PARAM;
                break;
            }
            if (raw[2] >= '0' && raw[2] <= '7') {
                if (raw_len != 4U && raw_len != 5U) {
                    cmd.sta = SC_MTC_PARSE_ERR_PARAM;
                    break;
                }
                cmd.p.voice.idx = (uint8_t)(raw[2] - '0');
                cmd.sta = SC_MTC_PARSE_OK;
            } else if (raw[2] == '8') {
                cmd.p.voice.idx      = 8U;
                cmd.p.voice.text     = &raw[3];
                cmd.p.voice.text_len = (uint16_t)(raw_len - 5U);
                cmd.sta = (cmd.p.voice.text_len > 0U) ? SC_MTC_PARSE_OK : SC_MTC_PARSE_ERR_PARAM;
            } else {
                cmd.sta = SC_MTC_PARSE_ERR_PARAM; /* '9' 未定义 */
            }
            break;

        case SC_MTC_PCMD_BRIGHTNESS:
            if (raw_len != 4U && raw_len != 5U) {
                cmd.sta = SC_MTC_PARSE_ERR_PARAM;
                break;
            }
            /* 参考 9K1F212701 mtc.c cmd_mtc_setlight_ctrl：参数为二进制 0~8
             * （0=自动调光，1~8 硬件档）；同时兼容 ASCII '0'~'8' 变体
             * （0x30~0x38 与二进制区间无重叠，二者可安全并存） */
            if (raw[2] <= 8U) {
                cmd.p.byte_val.val = raw[2];
                cmd.sta = SC_MTC_PARSE_OK;
            } else if (raw[2] >= '0' && raw[2] <= '8') {
                cmd.p.byte_val.val = (uint8_t)(raw[2] - '0');
                cmd.sta = SC_MTC_PARSE_OK;
            } else {
                cmd.sta = SC_MTC_PARSE_ERR_PARAM;
            }
            break;

        case SC_MTC_PCMD_VOLUME:
            if (raw_len != 4U && raw_len != 5U) {
                cmd.sta = SC_MTC_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.byte_val.val = (uint8_t)(raw[2] - '0');
            cmd.sta = (raw[2] >= '1' && raw[2] <= '5') ? SC_MTC_PARSE_OK : SC_MTC_PARSE_ERR_PARAM;
            break;

        default:
            cmd.sta = SC_MTC_PARSE_ERR_CMD;
            break;
    }

    return cmd;
}