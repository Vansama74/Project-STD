/**
 * @file    app_gz_ol_proto_parse.c
 * @brief   贵州治超屏协议（"TCLY" 帧族）帧解析
 *
 * 帧结构：`54 43 4C 59` + 包序号(4) + 长度(2 小端，整帧长) + 保留(2) + 命令(4，低字节有效)
 *          + 载荷(变长) + `0x00` 结束符；**无校验**（协议文档未定义任何校验字段）。
 *
 * 解析纪律（对齐 README §待裁决清单的采纳默认值）：
 *   - 长度字段必须等于整帧长、尾字节必须是 0x00（probe 已判，此处二次防御）；
 *   - 0x20 字号仅接受 16/24/32（源固件对其它字号读未初始化变量属未定义行为，
 *     本实现比源固件更严格：整帧丢弃，不复制缺陷）；
 *   - 0x10/0x30/0x50/0x60 不校验载荷长度（源固件各 handler 均忽略载荷；协议文档
 *     0x30 清屏帧例本身即带 17B 显示参数块，严格校验会拒掉上位机真实帧）；
 *   - 0x40 要求载荷 ≥ 14B（源固件不检查直接读，本实现加下界防越界，多余字节忽略）；
 *   - 颜色仅识别 FF0000/00FF00/0000FF 三原色（其余回退红，复现源固件），
 *     其中 00 00 FF 按源固件映射为黄色（现场既有行为，README Q7）；
 *   - 屏宽/屏高字段读取后忽略（渲染一律以 dev_display 实际几何为准，README Q9）。
 *
 * ⚠ 端口字段字节序（**有意偏差**，2026-09-15 用户裁决，README §13.5.1/§14）：
 *   0x40 载荷 `[12..13]` 为**高字节在前（BE16）**——写 10028 发 `27 2C`；
 *   这**不参考协议文档/源固件**（源固件 `UDP_SERVER.C:233` 为低字节在前），
 *   是本设备与上位机对齐后的设计口径。**其它 16 位字段（帧长、0x20 的 X/Y/屏宽/屏高）
 *   仍为小端 LE16，未动**。
 */

#include "app_gz_ol_proto_parse.h"

#include <string.h>

/** @brief 引导串匹配（4 字节）。 */
static inline bool _gz_ol_guide_match(const uint8_t *raw)
{
    return raw[0] == GZ_OL_GUIDE0 && raw[1] == GZ_OL_GUIDE1 &&
           raw[2] == GZ_OL_GUIDE2 && raw[3] == GZ_OL_GUIDE3;
}

/** @brief 2 字节小端读取（帧长字段与 0x20 的 X/Y/屏宽/屏高——这些字段口径未变）。 */
static inline uint16_t _gz_ol_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/** @brief 2 字节高字节在前（BE16）读取——**仅 0x40 端口字段**（2026-09-15 用户裁决）。 */
static inline uint16_t _gz_ol_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/**
 * @brief  字体名（GBK 4 字节）→ 字型；未命中回退宋体（与源固件一致）。
 * @param  name  4 字节字体名字段。
 */
static font_type_t _gz_ol_font_type(const uint8_t *name)
{
    if (memcmp(name, "宋体", GZ_OL_FONT_NAME_LEN) == 0)
        return FONT_ST;
    if (memcmp(name, "仿宋", GZ_OL_FONT_NAME_LEN) == 0)
        return FONT_FS;
    if (memcmp(name, "楷体", GZ_OL_FONT_NAME_LEN) == 0)
        return FONT_KT;
    if (memcmp(name, "黑体", GZ_OL_FONT_NAME_LEN) == 0)
        return FONT_HT;
    return FONT_ST;
}

/**
 * @brief  字体大小字段（两字节均为数值 16/24/32）→ 字号。
 * @param  size_lo,size_hi  字号字段两字节。
 * @param  out              解析结果（成功时写入）。
 * @return true = 合法字号。
 */
static bool _gz_ol_font_size(uint8_t size_lo, uint8_t size_hi, font_size_t *out)
{
    if (size_lo != size_hi)
        return false;
    switch (size_lo) {
        case 16: *out = FONT_16; return true;
        case 24: *out = FONT_24; return true;
        case 32: *out = FONT_32; return true;
        default: return false;
    }
}

/**
 * @brief  字体颜色（RGB 三字节）→ 8 色枚举。
 *
 *  复现源固件映射：FF0000→红、00FF00→绿、0000FF→**黄**（源固件既有行为）、
 *  其余→红（默认）。
 */
static display_color_t _gz_ol_color(const uint8_t *rgb)
{
    if (rgb[0] == 0xFF && rgb[1] == 0x00 && rgb[2] == 0x00)
        return COLOR_RED;
    if (rgb[0] == 0x00 && rgb[1] == 0xFF && rgb[2] == 0x00)
        return COLOR_GREEN;
    if (rgb[0] == 0x00 && rgb[1] == 0x00 && rgb[2] == 0xFF)
        return COLOR_YELLOW;
    return COLOR_RED;
}

/** @brief 命令字（4B 容器低字节）→ 命令枚举。 */
static gz_ol_pcmd_t _gz_ol_cmd(uint8_t c)
{
    switch (c) {
        case GZ_OL_PCMD_FAULT_QUERY: return GZ_OL_PCMD_FAULT_QUERY;
        case GZ_OL_PCMD_DISPLAY: return GZ_OL_PCMD_DISPLAY;
        case GZ_OL_PCMD_CLEAR: return GZ_OL_PCMD_CLEAR;
        case GZ_OL_PCMD_SET_IP: return GZ_OL_PCMD_SET_IP;
        case GZ_OL_PCMD_SET_IP_ACK: return GZ_OL_PCMD_SET_IP_ACK;
        case GZ_OL_PCMD_SEARCH: return GZ_OL_PCMD_SEARCH;
        case GZ_OL_PCMD_SEARCH_ACK: return GZ_OL_PCMD_SEARCH_ACK;
        case GZ_OL_PCMD_FULL_COLOR: return GZ_OL_PCMD_FULL_COLOR;
        default: return GZ_OL_PCMD_INVALID;
    }
}

gz_ol_parsed_cmd_t gz_ol_parse_frame(const uint8_t *raw, uint16_t raw_len)
{
    gz_ol_parsed_cmd_t cmd = {0};
    cmd.sta                = GZ_OL_PARSE_ERR_FRAME;

    if (raw == nullptr || raw_len < GZ_OL_FRAME_LEN_MIN || raw_len > GZ_OL_FRAME_LEN_MAX)
        return cmd;
    if (!_gz_ol_guide_match(raw))
        return cmd;
    if (_gz_ol_le16(&raw[GZ_OL_LEN_OFFSET]) != raw_len)
        return cmd;
    if (raw[raw_len - 1U] != GZ_OL_TAIL)
        return cmd;

    cmd.cmd      = _gz_ol_cmd(raw[GZ_OL_CMD_OFFSET]);
    cmd.data     = &raw[GZ_OL_DATA_OFFSET];
    cmd.data_len = (uint16_t)(raw_len - GZ_OL_FRAME_LEN_MIN); /* 载荷 = 整帧 - 17 */

    if (cmd.cmd == GZ_OL_PCMD_INVALID) {
        cmd.sta = GZ_OL_PARSE_ERR_CMD;
        return cmd;
    }

    switch (cmd.cmd) {
        case GZ_OL_PCMD_DISPLAY: {
            /* 前 17 字节固定前缀 + 变长文本 */
            if (cmd.data_len < GZ_OL_DISPLAY_PREFIX_LEN) {
                cmd.sta = GZ_OL_PARSE_ERR_PARAM;
                break;
            }
            const uint8_t *p = cmd.data;
            font_size_t size = FONT_16;
            if (!_gz_ol_font_size(p[12], p[13], &size)) {
                /* 字号非 16/24/32：源固件此处读未初始化变量（UB），本实现整帧丢弃 */
                cmd.sta = GZ_OL_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.display.x         = _gz_ol_le16(&p[0]);
            cmd.p.display.y         = _gz_ol_le16(&p[2]);
            /* p[4..7] 屏宽/屏高：源固件读取后忽略，本实现同样忽略（README Q9） */
            cmd.p.display.font_type = _gz_ol_font_type(&p[8]);
            cmd.p.display.font_size = size;
            cmd.p.display.color     = _gz_ol_color(&p[14]);
            cmd.p.display.text      = (const char *)&p[GZ_OL_DISPLAY_PREFIX_LEN];
            cmd.p.display.text_len  = (uint16_t)(cmd.data_len - GZ_OL_DISPLAY_PREFIX_LEN);
            cmd.sta                 = GZ_OL_PARSE_OK;
            break;
        }

        case GZ_OL_PCMD_SET_IP: {
            /* 源固件不检查载荷长度直接读前 14B；本实现要求至少 14B（防越界读），
             * 多余字节忽略 */
            if (cmd.data_len < GZ_OL_NET_PAYLOAD_LEN) {
                cmd.sta = GZ_OL_PARSE_ERR_PARAM;
                break;
            }
            memcpy(cmd.p.setip.ip, &cmd.data[0], 4);
            memcpy(cmd.p.setip.mask, &cmd.data[4], 4);
            memcpy(cmd.p.setip.gw, &cmd.data[8], 4);
            /* 端口字段 **高字节在前**（2026-09-15 用户裁决，有意偏离源固件/协议文档；
             * 只此一字段，见文件头与 README §13.5.1/§14）：`27 2C` → 10028。 */
            cmd.p.setip.port = _gz_ol_be16(&cmd.data[12]);
            cmd.sta          = GZ_OL_PARSE_OK;
            break;
        }

        case GZ_OL_PCMD_FULL_COLOR: {
            if (cmd.data_len < 3U) {
                cmd.sta = GZ_OL_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.rgb.r = cmd.data[0];
            cmd.p.rgb.g = cmd.data[1];
            cmd.p.rgb.b = cmd.data[2];
            cmd.sta     = GZ_OL_PARSE_OK;
            break;
        }

        /* 无载荷命令：**载荷长度不限**（与源固件一致——各 handler 不读载荷）。
         * 依据：协议文档 0x30 清屏帧例实际带 17B 显示参数块（长度字段 0x22=34），
         * 严格校验会拒掉上位机的真实帧；源固件 cmd_screenclear/cmd_getfailure/
         * cmd_scip_ctrl 均忽略载荷。 */
        case GZ_OL_PCMD_FAULT_QUERY:
        case GZ_OL_PCMD_CLEAR:
        case GZ_OL_PCMD_SEARCH:
        case GZ_OL_PCMD_SET_IP_ACK: /* 兼容源固件：入站 0x50 亦作搜索触发（README Q3） */
            cmd.sta = GZ_OL_PARSE_OK;
            break;

        case GZ_OL_PCMD_SEARCH_ACK: /* 0x70 仅出站：入站静默丢弃 */
        default:
            cmd.sta = GZ_OL_PARSE_ERR_CMD;
            break;
    }

    return cmd;
}
