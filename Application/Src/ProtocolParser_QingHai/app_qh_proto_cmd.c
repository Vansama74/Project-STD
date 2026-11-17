/**
 * @file    app_qh_proto_cmd.c
 * @brief   青海治超站费显协议（V1.0）命令执行
 *
 * 职责边界：本文件是唯一触碰 Device/ 驱动的地方；解析器（app_qh_proto_parse.c）只产出清单。
 * 应答契约：V1.0 每条命令都要求「显示器应答：已发送数据一致」，
 *           故把应答从各 exec_* 抽出，统一由 `qh_execute_cmd` 收口（exec_* 只返回是否成功），
 *           后续新增命令不会漏应答。
 */

#include "app_qh_proto_cmd.h"
#include "app_dispatch.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app_board_net_cfg.h"
#include "app_light_sensor.h"
#include "app_qh_proto_voice.h"
#include "app_render.h"
#include "dev_display.h"
#include "dev_io_ctrl.h"

/* V1.0：点阵固定 4 行（协议「费显点阵显示 4行」） */
#define QH_DISPLAY_ROWS (4U)

/* 0x3B 设定值 → 渲染字号；0 = 自适应（按行高选） */
static uint8_t s_qh_font_cfg = 0; /* 0 自适应 / 1 = 16 / 2 = 24 / 3 = 32 */

static const display_color_t s_qh_color_map[] = {
    [0] = COLOR_RED,
    [1] = COLOR_GREEN,
    [2] = COLOR_YELLOW,
};

/**
 * @brief  统一应答。
 * @param  ch        当前通道。
 * @param  cmd_char  原命令字（回显）。
 * @param  sta       0x00 = 已执行 / 数据一致；0x01 = 参数或执行异常。
 * @note   协议只写了「显示器应答：已发送数据一致」而未给字段格式，
 *         本实现取与 '1' 查询应答同构的 `7B cmd 01 sta 7D`。
 *         若创迪给出正式应答帧格式，只改这一个函数。
 */
static void qh_reply(channel_t *ch, uint8_t cmd_char, uint8_t sta)
{
    uint8_t rsp[5] = {'{', cmd_char, 0x01, sta, '}'};
    channel_send(ch, rsp, sizeof(rsp));
}

/**
 * @brief  协议颜色索引 → 显示颜色。
 */
static display_color_t qh_map_color(uint8_t idx)
{
    return (idx < 3U) ? s_qh_color_map[idx] : COLOR_GREEN;
}

/**
 * @brief  按行高与 0x3B 设定选渲染字号。
 */
static font_size_t qh_pick_font(uint16_t row_h)
{
    switch (s_qh_font_cfg) {
        case 1:  return FONT_16;
        case 2:  return FONT_24;
        case 3:  return FONT_32;
        default: break;
    }
    if (row_h >= (uint16_t)FONT_32) return FONT_32;
    if (row_h >= (uint16_t)FONT_24) return FONT_24;
    if (row_h >= (uint16_t)FONT_16) return FONT_16;
    return FONT_14;
}

/**
 * @brief  执行 '2' 点阵显示。
 * @param  p  点阵显示参数（row：0 = 全屏自动换行，1~4 = 第 1~4 行）。
 * @return 无显示驱动时返回 false（命令未生效 → 应答 0x01）。
 */
static bool qh_exec_matrix(const qh_matrix_t *p)
{
    dev_display_t *d = dev_display_get();
    if (!d) return false;

    /* dev_display_t 命名口径：screen_rows = 水平像素，screen_cols = 垂直像素 */
    uint16_t w     = d->screen_rows;
    uint16_t h     = d->screen_cols;
    uint16_t row_h = (h >= QH_DISPLAY_ROWS) ? (uint16_t)(h / QH_DISPLAY_ROWS) : h;

    uint16_t x = 0, y = 0, fh = h;
    bool wrap = true;

    if (p->row > 0U) {
        y    = (uint16_t)((p->row - 1U) * row_h);
        fh   = row_h;
        wrap = false; /* 单行显示：超宽由渲染器裁剪，不侵占下一行 */
    }

    app_render(&(render_cfg_t){
        .type  = RENDER_TEXT,
        .x     = x,
        .y     = y,
        .w     = w,
        .h     = fh,
        .style = &(render_style_t){
            .h_align   = ALIGN_LEFT_UP,
            .v_align   = ALIGN_LEFT_UP,
            .word_wrap = wrap,
        },
        .color     = qh_map_color(p->color),
        .text      = (const char *)p->text,
        .len       = p->text_len,
        .font_size = qh_pick_font(fh),
        .font_type = FONT_ST,
        .text_enc  = FONT_ENC_GBK,
    });
    return true;
}

/**
 * @brief  执行 '3' 全屏清除。
 */
static bool qh_exec_clear(void)
{
    dev_display_t *d = dev_display_get();
    if (!d) return false;
    dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK);
    dev_display_commit_frame(d);
    return true;
}

/**
 * @brief  执行 '4' 通行灯 / 报警控制。
 * @param  ctrl  单字节十六进制：0x01 红叉 / 0x02 绿箭 / 0x03 报警 / 0x04 关闭报警。
 * @note   通行灯为红叉-绿箭双态指示（PD14）：置位 = 绿箭，复位 = 红叉。
 */
static bool qh_exec_peripheral(uint8_t ctrl)
{
    switch (ctrl) {
        case 0x01: /* 通行灯显示红叉 */
            dev_io_flash_light(false);
            dev_io_lane_light(false);
            return true;
        case 0x02: /* 通行灯显示绿箭 */
            dev_io_flash_light(false);
            dev_io_lane_light(true);
            return true;
        case 0x03: /* 报警开 */
            dev_io_flash_light(true);
            return true;
        case 0x04: /* 关闭报警 */
            dev_io_flash_light(false);
            return true;
        default:
            return false;
    }
}

/**
 * @brief  执行 '5' 语音段组合。
 */
static bool qh_exec_voice(const qh_voice_seg_t *p)
{
    if (!p->segs || p->seg_cnt == 0U) return false;
    qh_voice_segments(p->segs, p->seg_cnt);
    return true;
}

/**
 * @brief  执行 0x3A 网络配置写入（保留现有 TCP/UDP 端口值）。
 */
static bool qh_exec_set_ip(const qh_ip_cfg_t *p)
{
    app_board_net_cfg_t cur;
    if (app_board_net_cfg_get(&cur) != 0) return false;
    return app_board_net_cfg_update(p->ip, p->mask, p->gw, cur.port, cur.udp_port) == 0;
}

/**
 * @brief  执行 0x3B 字体大小设定（只影响后续显示，不重刷当前内容）。
 */
static bool qh_exec_font(uint8_t size)
{
    if (size > 3U) return false;
    s_qh_font_cfg = size;
    return true;
}

/**
 * @brief  执行 0x3C 亮度设定。
 * @param  level  0 = 自动调光 / 1 = 最暗 / 8 = 最亮。
 * @note  光敏任务每秒会重写 `light_level`，故手动档必须同时把自动调光范围钉死为该档，
 *        否则设置会被下一轮自动调光覆盖。0 = 恢复默认范围。
 */
static bool qh_exec_brightness(uint8_t level)
{
    if (level == 0U) {
        app_light_sensor_set_range(LIGHT_SENSOR_MIN_LEVEL, LIGHT_SENSOR_MAX_LEVEL);
        return true;
    }
    if (level > DEV_DISPLAY_BRIGHTNESS_MAX) return false;
    app_light_sensor_set_range(level, level);
    dev_display_t *d = dev_display_get();
    if (d) dev_display_set_brightness(d, level);
    return true;
}

/**
 * @brief  执行青海协议命令，并按 V1.0 约定统一应答。
 * @param  ch   当前通道。
 * @param  cmd  解析后的命令结构体。
 */
void qh_execute_cmd(channel_t *ch, const qh_parsed_cmd_t *cmd)
{
    if (!cmd) return;

    /* 帧结构到达但解析失败：仍回应答，避免上位机等到超时 */
    if (cmd->sta != QH_PARSE_OK) {
        if (cmd->cmd_char != 0U) qh_reply(ch, cmd->cmd_char, 0x01);
        return;
    }

    bool ok = true;
    switch (cmd->cmd) {
        case QH_PCMD_HOST_QUERY:
            /* '1'：sta 0x00 = 显示器状态正常，0x01 = 不正常 */
            ok = (dev_display_get() != nullptr);
            break;
        case QH_PCMD_MATRIX_SHOW:
            ok = qh_exec_matrix(&cmd->p.matrix);
            break;
        case QH_PCMD_CLEAR:
            ok = qh_exec_clear();
            break;
        case QH_PCMD_PERIPHERAL:
            ok = qh_exec_peripheral(cmd->p.peripheral.ctrl);
            break;
        case QH_PCMD_VOICE_SEG:
            ok = qh_exec_voice(&cmd->p.voice);
            break;
        case QH_PCMD_SET_IP:
            ok = qh_exec_set_ip(&cmd->p.ip_cfg);
            break;
        case QH_PCMD_FONT_SIZE:
            ok = qh_exec_font(cmd->p.font.size);
            break;
        case QH_PCMD_BRIGHTNESS:
            ok = qh_exec_brightness(cmd->p.brightness.level);
            break;
        default:
            ok = false;
            break;
    }

    qh_reply(ch, cmd->cmd_char, ok ? 0x00 : 0x01);
}