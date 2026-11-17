/**
 * @file    app_qh_proto_parse.c
 * @brief   青海治超站费显协议（V1.0）帧解析
 *
 * 纯函数：不触碰任何 Device/ 驱动，只把原始帧解成 `qh_parsed_cmd_t` 清单。
 *
 * 长度字段口径：`raw[2]` 是**二进制字节数**（参数数据长度），整帧长 = len + 4。
 * 判据来自协议原文示例：`7B 35 04 1A 0F 0A 0E 7D`（len = 0x04 配 4 字节数据）、
 * `7B 3A 0C ...`（len = 0x0C = 12 = 4+4+4 的 IP/掩码/网关）。
 */

#include "app_qh_proto_parse.h"

/**
 * @brief  将青海协议命令字转换为内部命令枚举。
 * @param  c  协议中的命令字。
 * @return 对应的协议命令枚举；非 V1.0 命令字返回 QH_PCMD_INVALID。
 */
static qh_pcmd_t qh_char_to_cmd(uint8_t c)
{
    switch (c) {
        case '1':  return QH_PCMD_HOST_QUERY;
        case '2':  return QH_PCMD_MATRIX_SHOW;
        case '3':  return QH_PCMD_CLEAR;
        case '4':  return QH_PCMD_PERIPHERAL;
        case '5':  return QH_PCMD_VOICE_SEG;
        case 0x3A: return QH_PCMD_SET_IP;
        case 0x3B: return QH_PCMD_FONT_SIZE;
        case 0x3C: return QH_PCMD_BRIGHTNESS;
        default:   return QH_PCMD_INVALID;
    }
}

/**
 * @brief  按命令解码参数区，越界置 QH_PARSE_ERR_PARAM。
 * @param  cmd  待填充的解析结果（`cmd->data` / `data_len` 已就绪）。
 */
static void qh_decode_params(qh_parsed_cmd_t *cmd)
{
    const uint8_t *d = cmd->data;
    uint16_t n       = cmd->data_len;

    switch (cmd->cmd) {
        case QH_PCMD_MATRIX_SHOW: {
            /* x0 行号（ASCII '0'~'4'）、x1 颜色、x2.. 显示内容 */
            if (n < 2) { cmd->sta = QH_PARSE_ERR_PARAM; break; }
            if (d[0] < '0' || d[0] > '4') { cmd->sta = QH_PARSE_ERR_PARAM; break; }
            cmd->p.matrix.row = (uint8_t)(d[0] - '0');

            /* 颜色：单字节二进制 0 红 / 1 绿 / 2 黄（协议原文未标 ASCII） */
            if (d[1] > 2) { cmd->sta = QH_PARSE_ERR_PARAM; break; }
            cmd->p.matrix.color    = d[1];
            cmd->p.matrix.text     = &d[2];
            cmd->p.matrix.text_len = (uint16_t)(n - 2);
            break;
        }

        case QH_PCMD_CLEAR:
            /* `7B 33 00 7D`：无参数。带参数也容忍，忽略即可。 */
            break;

        case QH_PCMD_PERIPHERAL:
            /* 单字节十六进制：0x01 红叉 / 0x02 绿箭 / 0x03 报警 / 0x04 关报警 */
            if (n < 1) { cmd->sta = QH_PARSE_ERR_PARAM; break; }
            if (d[0] < 0x01 || d[0] > 0x04) { cmd->sta = QH_PARSE_ERR_PARAM; break; }
            cmd->p.peripheral.ctrl = d[0];
            break;

        case QH_PCMD_VOICE_SEG: {
            /* 每字节一个语音段号，0~45 为协议定义段，其余留待执行器忽略 */
            if (n < 1) { cmd->sta = QH_PARSE_ERR_PARAM; break; }
            cmd->p.voice.segs    = d;
            cmd->p.voice.seg_cnt = n;
            break;
        }

        case QH_PCMD_SET_IP: {
            /* IP(4) + mask(4) + gateway(4) */
            if (n < 12) { cmd->sta = QH_PARSE_ERR_PARAM; break; }
            for (uint8_t i = 0; i < 4; i++) {
                cmd->p.ip_cfg.ip[i]   = d[i];
                cmd->p.ip_cfg.mask[i] = d[4 + i];
                cmd->p.ip_cfg.gw[i]   = d[8 + i];
            }
            break;
        }

        case QH_PCMD_FONT_SIZE: {
            if (n < 1) { cmd->sta = QH_PARSE_ERR_PARAM; break; }
            if (d[0] > 3) { cmd->sta = QH_PARSE_ERR_PARAM; break; }
            cmd->p.font.size = d[0];
            break;
        }

        case QH_PCMD_BRIGHTNESS: {
            if (n < 1) { cmd->sta = QH_PARSE_ERR_PARAM; break; }
            if (d[0] > 8) { cmd->sta = QH_PARSE_ERR_PARAM; break; }
            cmd->p.brightness.level = d[0];
            break;
        }

        case QH_PCMD_HOST_QUERY:
        default:
            /* '1' 无参数；长度非 0 也容忍 */
            break;
    }
}

/**
 * @brief  解析青海协议原始帧。
 * @param  raw      原始帧数据。
 * @param  raw_len  原始帧长度。
 * @return 解析后的命令结构体；`sta` 标识解析结果。
 */
qh_parsed_cmd_t qh_parse_frame(const uint8_t *raw, uint16_t raw_len)
{
    qh_parsed_cmd_t cmd = {0};

    if (!raw || raw_len < 4U || raw[0] != '{' || raw[raw_len - 1U] != '}') {
        cmd.sta = QH_PARSE_ERR_FRAME;
        return cmd;
    }

    cmd.cmd_char = raw[1];
    cmd.cmd      = qh_char_to_cmd(raw[1]);
    if (cmd.cmd == QH_PCMD_INVALID) {
        cmd.sta = QH_PARSE_ERR_CMD;
        return cmd;
    }

    /* 长度字段是二进制字节值；参数区实长 = raw_len - 4（'{' + cmd + len + '}'）。 */
    uint16_t declared_len = raw[2];
    if ((uint16_t)(raw_len - 4U) < declared_len) {
        cmd.sta = QH_PARSE_ERR_FRAME;
        return cmd;
    }

    cmd.data     = &raw[3];
    cmd.data_len = declared_len;
    cmd.sta      = QH_PARSE_OK;

    qh_decode_params(&cmd);
    return cmd;
}