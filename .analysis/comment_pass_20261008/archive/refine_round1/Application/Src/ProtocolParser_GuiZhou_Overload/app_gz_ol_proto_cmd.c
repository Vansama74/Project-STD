/**
 * @file    app_gz_ol_proto_cmd.c
 * @brief   贵州治超屏协议 — 命令执行与应答组帧
 *
 * 显示走 app_render（GBK 直通、先整屏清黑、按屏宽换行、越屏裁剪绘制）；0x20 文本字面
 * `\n`（0x5C 0x6E）在执行层原地转 0x0A（GBK 安全行走）；亮度/调光 = set_brightness +
 * 光敏任务挂起/恢复（0x80 副作用）；网络配置写 Sector1 `net_cfg.port`（`udp_port` 保留）、
 * 端口 **BE16**；「清屏/渲染/提交」持 `app_scroll_render_lock`；应答单播回源。
 * 详见 doc/14_贵州治超屏协议/README.md §1、§3、§8、§10、§11、§13
 */

#include "app_gz_ol_proto_cmd.h"

#include <string.h>

#include "app_board_net_cfg.h"
#include "app_light_sensor.h"
#include "app_render.h"
#include "app_scroll.h"
#include "cmsis_os2.h"
#include "dev_display.h"
#include "stm32f4xx_hal.h" /* NVIC_SystemReset */

/* ---- 模块运行态 ---- */
static bool s_gz_ol_auto_dim_off; /**< 光敏自动调光是否被本模块挂起（0x80 副作用，见 §1） */

/* ---- 应答组帧 ---- */

/**
 * @brief  组帧并经来源通道单播发送。
 * @param  ch           来源通道。
 * @param  cmd          应答命令字（4B 容器低字节有效）。
 * @param  payload      载荷（可为 NULL）。
 * @param  payload_len  载荷字节数（本模块应答最长 14B，超出直接返回）。
 */
static void _gz_ol_send_frame(channel_t *ch, uint8_t cmd, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t buf[GZ_OL_HEAD_LEN + GZ_OL_NET_PAYLOAD_LEN + 1U];
    if (payload_len > GZ_OL_NET_PAYLOAD_LEN)
        return; /* 本模块应答载荷最长 14B（0x10 的 2B / 0x50、0x70 的 14B） */

    const uint16_t total = (uint16_t)(GZ_OL_HEAD_LEN + payload_len + 1U);

    buf[0] = GZ_OL_GUIDE0;
    buf[1] = GZ_OL_GUIDE1;
    buf[2] = GZ_OL_GUIDE2;
    buf[3] = GZ_OL_GUIDE3;
    buf[4] = 0U; /* 包序号：文档规定 00 00 00 00 */
    buf[5] = 0U;
    buf[6] = 0U;
    buf[7] = 0U;
    buf[8]  = (uint8_t)(total & 0xFFU); /* 长度 = 整帧总长（小端） */
    buf[9]  = (uint8_t)(total >> 8);
    buf[10] = 0U; /* 保留位 */
    buf[11] = 0U;
    buf[12] = cmd; /* 命令字 4B 容器，低字节有效，其余补 0 */
    buf[13] = 0U;
    buf[14] = 0U;
    buf[15] = 0U;
    if (payload_len > 0U && payload != nullptr)
        memcpy(&buf[GZ_OL_DATA_OFFSET], payload, payload_len);
    buf[total - 1U] = GZ_OL_TAIL;

    channel_send(ch, buf, total);
}

/**
 * @brief  读 Sector1 net_cfg 填入 14B 载荷（ip4 + mask4 + gw4 + port2 **BE16**）。
 * @param  out  14B 输出缓冲。
 * @return true = 读到有效配置；false = 记录无效（调用方用全 0 兜底）。
 * @note   port 报 `net_cfg.port`——它同时是 GZ_OL 专用 UDP 业务口与 TCP 业务口的监听号
 *         （与 LDI 12H / IAP 0x01 同源）；`udp_port`（CQ 专有）不参与。详见 §13。
 */
static bool _gz_ol_net_payload(uint8_t *out)
{
    app_board_net_cfg_t cfg;
    if (app_board_net_cfg_get(&cfg) != 0)
        return false;

    memcpy(&out[0], cfg.ip, 4);
    memcpy(&out[4], cfg.mask, 4);
    memcpy(&out[8], cfg.gw, 4);
    out[12] = (uint8_t)(cfg.port >> 8); /* 高字节在前（BE16，2026-09-15 用户裁决） */
    out[13] = (uint8_t)(cfg.port & 0xFFU);
    return true;
}

/* ---- 显示 ---- */

/** @brief 整屏清黑并提交（调用方须已持渲染锁）。 */
static void _gz_ol_clear_screen(dev_display_t *d)
{
    dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK);
    dev_display_commit_frame(d);
}

/**
 * @brief  把 0x20 文本里的字面 `\n`（0x5C 0x6E）原地转换为换行 0x0A。
 * @param  text    文本（GBK/ASCII 混合，可原地修改）。
 * @param  len     文本字节数；`out_nl` 输出转换出的换行个数（可为 NULL）。
 * @return 转换后长度（≤ len；输出 ≤ 输入、双指针只落后不超前，无需额外缓冲）。
 * @note   必须按 GBK 结构行走（尾字节可为 0x5C，朴素匹配会毁字）：首字节 0x81~0xFE +
 *         合法尾字节整体拷贝、不参与转义判定；仅 `\n` 转换。裁决与陷阱回归见 §1、§14。
 */
static uint16_t _gz_ol_text_unescape_nl(char *text, uint16_t len, uint16_t *out_nl)
{
    uint16_t i = 0U, o = 0U, nl = 0U;
    while (i < len) {
        if (text[i] == 0x5CU && (i + 1U) < len && text[i + 1U] == 0x6EU) {
            text[o++] = 0x0AU; /* 字面 "\n" → 换行（2026-09-18 用户裁决） */
            i += 2U;
            nl++;
        } else if (text[i] >= 0x81U && text[i] <= 0xFEU && (i + 1U) < len &&
                   text[i + 1U] >= 0x40U && text[i + 1U] <= 0xFEU && text[i + 1U] != 0x7FU) {
            /* GBK 双字节字符：整体原样拷贝（尾字节即使是 0x5C 也不参与转义判定） */
            text[o++] = text[i];
            text[o++] = text[i + 1U];
            i += 2U;
        } else {
            text[o++] = text[i];
            i += 1U;
        }
    }
    if (out_nl != nullptr)
        *out_nl = nl;
    return o;
}

/**
 * @brief  执行 0x20：先整屏清黑，再按 (x, y) 渲染（按屏宽自动换行、越屏裁剪绘制）。
 * @param  cmd  解析结果（`p.display` 为显示参数；`text` 可原地转换，见 `_gz_ol_text_unescape_nl`）。
 * @note   字号**不受屏高限制**：行框下缘越屏由 `dev_display_draw_bitmap` 按屏幕交集裁剪绘制
 *         （2026-09-14 裁决，另两层改动见 §12.6）；区域 = 剩余屏幕（w/h 不做字号补偿，
 *         首行不受行终止判定约束）。`x`/`y` ≥ 屏宽/屏高时钳 0（源固件行为）。
 */
static void _gz_ol_exec_display(const gz_ol_parsed_cmd_t *cmd)
{
    const gz_ol_display_t *p = &cmd->p.display;
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;

    const uint16_t x = (p->x < d->screen_rows) ? p->x : 0U;
    const uint16_t y = (p->y < d->screen_cols) ? p->y : 0U;
    const uint16_t w = (uint16_t)(d->screen_rows - x);
    const uint16_t h = (uint16_t)(d->screen_cols - y);


    /* 字面 `\n`（0x5C 0x6E）→ 0x0A 原地转换（2026-09-18 裁决）；p->text 为任务帧缓冲
     * 私有拷贝（可变），转换输出 ≤ 输入、无额外缓冲——四通道一律经此转换。 */
    uint16_t text_len = p->text_len;
    if (text_len > 0U)
        text_len = _gz_ol_text_unescape_nl((char *)p->text, text_len, nullptr);


    app_scroll_render_lock();

    _gz_ol_clear_screen(d);

    if (text_len > 0U) {
        app_render(&(render_cfg_t){
            .type      = RENDER_TEXT,
            .x         = x,
            .y         = y,
            .w         = w,
            .h         = h,
            .style     = &(render_style_t){
                .h_align   = ALIGN_LEFT_UP,
                .v_align   = ALIGN_LEFT_UP,
                .word_wrap = true,
            },
            .color     = p->color,
            .text      = p->text,
            .len       = text_len,
            .font_size = p->font_size,
            .font_type = p->font_type,
            .text_enc  = FONT_ENC_GBK,
        });
        dev_display_commit_frame(d);
    }


    app_scroll_render_unlock();
}

/**
 * @brief  执行 0x30 清屏（全屏清黑；载荷长度不限）。
 */
static void _gz_ol_exec_clear(void)
{
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;
    app_scroll_render_lock();
    _gz_ol_clear_screen(d);
    app_scroll_render_unlock();
}

/**
 * @brief  执行 0x80 全屏纯色（复现源固件：仅 FF0000/00FF00/0000FF 三色，其余不动屏幕）。
 * @param  rgb  三字节 RGB。
 * @note   副作用 = 挂起光敏任务 + 亮度 8（`DEV_DISPLAY_BRIGHTNESS_MAX`）；收到任意帧即由
 *         `gz_ol_resume_auto_dim()` 恢复。见 §1 命令表与 §8 Q6。
 */
static void _gz_ol_exec_full_color(const gz_ol_rgb_t *rgb)
{
    display_color_t color;
    if (rgb->r == 0xFF && rgb->g == 0x00 && rgb->b == 0x00) {
        color = COLOR_RED;
    } else if (rgb->r == 0x00 && rgb->g == 0xFF && rgb->b == 0x00) {
        color = COLOR_GREEN;
    } else if (rgb->r == 0x00 && rgb->g == 0x00 && rgb->b == 0xFF) {
        color = COLOR_YELLOW; /* 源固件口径：蓝→黄 */
    } else {
        return; /* 未识别颜色：源固件不动屏幕 */
    }

    /* 副作用：关闭自动调光 + 亮度 8（复现源固件 lightFLAG=false / lightLev=8） */
    if (g_light_sensor_task_handle != nullptr) {
        osThreadSuspend(g_light_sensor_task_handle);
        s_gz_ol_auto_dim_off = true;
    }
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;
    dev_display_set_brightness(d, DEV_DISPLAY_BRIGHTNESS_MAX);

    app_scroll_render_lock();
    dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, color);
    dev_display_commit_frame(d);
    app_scroll_render_unlock();
}

/**
 * @brief  收到任意帧即恢复光敏自动调光（复现源固件「收到数据 lightFLAG=true」语义）。
 * @note   由帧处理任务在解析前调用；未挂起时为空操作。
 */
void gz_ol_resume_auto_dim(void)
{
    if (!s_gz_ol_auto_dim_off)
        return;
    if (g_light_sensor_task_handle != nullptr)
        osThreadResume(g_light_sensor_task_handle);
    s_gz_ol_auto_dim_off = false;
}

/**
 * @brief  执行 0x40：写 Sector1 `net_cfg.port`（`udp_port` 保留）→ 应答 0x50 → 延时软复位。
 * @param  ch   来源通道。
 * @param  cmd  解析结果（`p.setip` 为 14B 网络参数，端口已按 BE16 解析）。
 * @note   `0x50` 应答是**按解析值重建的回显、不是落盘证据**（落盘证据 = 重启后 `0x70`
 *         回读 + 新端口实发，见 §13.4）；`osDelay(100)` 给 9600bps 下的应答留传输时间。
 */
static void _gz_ol_exec_set_ip(channel_t *ch, const gz_ol_parsed_cmd_t *cmd)
{
    const gz_ol_setip_t *p = &cmd->p.setip;

    /* 写 net_cfg.port：重启后由 app_net_boot 应用到 TCP 业务口、由 GZ_OL 专用 UDP 实例
     * （CH_ID_UDP_GZOL）应用到业务口（同号不同协议栈）；udp_port 保留现值（CQ 专有），
     * 记录无效时按出厂默认 20103 兜底（与 app_net_boot 默认一致）。见 §13。 */
    app_board_net_cfg_t cur;
    uint32_t udp_port = (app_board_net_cfg_get(&cur) == 0) ? cur.udp_port : 20103U;


    const int32_t wr_ret = app_board_net_cfg_update(p->ip, p->mask, p->gw, (uint32_t)p->port, udp_port);

    (void)wr_ret; /* 返回值不影响应答与重启流程（0x50 回显 + 重启为固定语义） */

    /* 应答 0x50 帧（**按解析值重建**本次下发的配置，与源固件一致；端口与请求同序 BE16，
     * 2026-09-15 用户裁决；**回显不是落盘证据**） */
    uint8_t payload[GZ_OL_NET_PAYLOAD_LEN];
    memcpy(&payload[0], p->ip, 4);
    memcpy(&payload[4], p->mask, 4);
    memcpy(&payload[8], p->gw, 4);
    payload[12] = (uint8_t)(p->port >> 8);
    payload[13] = (uint8_t)(p->port & 0xFFU);
    _gz_ol_send_frame(ch, GZ_OL_PCMD_SET_IP_ACK, payload, GZ_OL_NET_PAYLOAD_LEN);

    /* 给应答帧留出传输时间（源固件 delay_ms(30)；9600bps 下 31B ≈ 32ms，取 100ms 余量），
     * 随后软复位使新配置生效（STD 约定改 IP 统一重启生效）。 */
    osDelay(100);
    NVIC_SystemReset();
}

/**
 * @brief  执行 0x50 / 0x60 搜索：应答 0x70 帧（14B 载荷，单播回源）。
 * @param  ch  来源通道。
 * @note   记录无效（`app_board_net_cfg_get` 失败）时回**全 0** 载荷（尚未配置，诚实回显）。
 */
static void _gz_ol_exec_search(channel_t *ch)
{
    /* 记录无效时回全 0（IP/掩码/网关/端口全 0 = 尚未配置，诚实回显） */
    uint8_t payload[GZ_OL_NET_PAYLOAD_LEN] = {0};
    (void)_gz_ol_net_payload(payload);
    _gz_ol_send_frame(ch, GZ_OL_PCMD_SEARCH_ACK, payload, GZ_OL_NET_PAYLOAD_LEN);
}

/**
 * @brief  执行 0x10 故障查询：应答 2 字节状态 0x0000（先低字节后高字节）。
 * @note   文档「无此故障硬件的位补 0」→ STD 无对应故障采集点，全 0 合规；源固件空实现
 *         不应答会让上位机轮询超时，不复制该缺陷（§2 #2）。
 */
static void _gz_ol_exec_fault_query(channel_t *ch)
{
    const uint8_t sta[2] = {0x00U, 0x00U};
    _gz_ol_send_frame(ch, GZ_OL_PCMD_FAULT_QUERY, sta, sizeof(sta));
}

void gz_ol_execute_cmd(channel_t *ch, const gz_ol_parsed_cmd_t *cmd)
{
    if (cmd == nullptr || cmd->sta != GZ_OL_PARSE_OK)
        return;

    switch (cmd->cmd) {
        case GZ_OL_PCMD_DISPLAY:
            _gz_ol_exec_display(cmd);
            break;
        case GZ_OL_PCMD_CLEAR:
            _gz_ol_exec_clear();
            break;
        case GZ_OL_PCMD_FULL_COLOR:
            _gz_ol_exec_full_color(&cmd->p.rgb);
            break;
        case GZ_OL_PCMD_FAULT_QUERY:
            _gz_ol_exec_fault_query(ch);
            break;
        case GZ_OL_PCMD_SET_IP:
            _gz_ol_exec_set_ip(ch, cmd);
            break;
        case GZ_OL_PCMD_SEARCH:
        case GZ_OL_PCMD_SET_IP_ACK: /* 入站 0x50 兼容为搜索触发（§8 Q3） */
            _gz_ol_exec_search(ch);
            break;
        default:
            break; /* 0x70 等仅出站命令字：静默丢弃 */
    }
}
