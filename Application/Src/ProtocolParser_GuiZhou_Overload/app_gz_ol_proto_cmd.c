/**
 * @file    app_gz_ol_proto_cmd.c
 * @brief   贵州治超屏协议（"TCLY" 帧族）命令执行与应答组帧
 *
 * 显示映射：app_render（GBK 直通；0x20 按 X/Y 定位、先整屏清黑、按屏宽自动换行、
 * 超屏裁剪）；亮度/自动调光映射：dev_display_set_brightness + 光敏任务挂起/恢复；
 * 网络配置：STD 中立模块 app_board_net_cfg（Sector1），不写私有 W25Qxx 记录。
 *
 * 应答帧与请求同帧族（引导串 + 长度 + 命令 + 载荷 + 0x00），一律**单播回源通道**
 * （README Q4 推荐 A：STD channel_send 语义；源固件搜索应答为广播，联调如证明上位机
 * 依赖广播再评估）。
 *
 * 渲染互斥（README Q13 推荐 B）：本模块「stop/清屏 + 渲染 + 提交」整段持
 * app_scroll_render_lock，与安徽动态滚动（app_scroll）渲染串行，消除同屏混合帧黑条；
 * 引擎未创建时该锁为空操作，零成本。
 */

#include "app_gz_ol_proto_cmd.h"

#include <string.h>

#include "app_board_net_cfg.h"
#include "app_diag.h"
#include "app_light_sensor.h"
#include "app_render.h"
#include "app_scroll.h"
#include "cmsis_os2.h"
#include "dev_display.h"
#include "SEGGER_RTT.h"    /* 联调诊断（GZ_OL_RTT_DIAG）；置 0 时该头无引用 */
#include "stm32f4xx_hal.h" /* NVIC_SystemReset */

/* ---- 模块运行态 ---- */
static bool s_gz_ol_auto_dim_off; /**< 光敏自动调光是否被本模块挂起（0x80 副作用） */

/* ---- 应答组帧 ---- */

/**
 * @brief  构造并发送本协议应答帧。
 * @param  ch           来源通道（单播回源）。
 * @param  cmd          应答命令字（4B 容器低字节有效）。
 * @param  payload      载荷（可为 NULL）。
 * @param  payload_len  载荷字节数。
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
 * @brief  读当前网络配置并填入 14B 应答载荷（ip4 + mask4 + gw4 + port2 高字节在前）。
 *
 *  port 字段语义（2026-09-14 用户裁决，doc/14 §13）：报 Sector1 net_cfg.port——
 *  它同时是 GZ_OL 专用业务口（CH_ID_UDP_GZOL）的监听口与 TCP 业务口（同号不同协议栈），
 *  与 LDI 12H / IAP 0x01 上报一致；udp_port（CQ 专有）不参与。
 *  字节序（2026-09-15 用户裁决，doc/14 §13.5.1/§14）：**高字节在前 BE16**——`27 2C`
 *  在线上的语义是 10028（有意偏离源固件/协议文档的低字节在前口径）。
 *
 * @param  out  14B 输出缓冲。
 * @return true = 读到有效配置；false = 记录无效（调用方用出厂默认兜底）。
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

/* ---- 联调诊断（2026-09-14 临时）----
 * 0x20 每帧打印：载荷前 16 字节 + 解析结果 + 传送给渲染的参数 + 屏体几何 + 字形适配判定
 *  + 字号映射自证 + **字库 16/24/32 三档实读**（addr/返回码/非零字节数/前 8 字节）
 *  + 渲染后显存非黑像素数（「渲染到底有没有落屏」的直接证据）。
 * 0x40 每帧打印：落库返回值 + 写前/写后 read-back + 即将重启提示。
 * 用途：现场一眼区分「帧没到/被解析丢弃」「字号映射错」「字库缺该字号数据/地址越界」
 *       「渲染/裁剪丢字」「屏体几何放不下（整字形丢弃）」五类可能。
 *
 * **一键关闭**：GZ_OL_RTT_DIAG 置 0（默认跟随总开关 APP_DIAG_BANNER，见 app_diag.h；
 * 也可单独覆盖 0/1）。关闭后本块整体消失，协议行为不变（诊断全部只读）。 */
#ifndef GZ_OL_RTT_DIAG
#define GZ_OL_RTT_DIAG (APP_DIAG_BANNER)
#endif

#if GZ_OL_RTT_DIAG

/** @brief 显存非黑像素计数（0 = 渲染后屏上确实没内容；>0 = 已落 pixel_map） */
static uint32_t _gz_ol_diag_nonblack(const dev_display_t *d, uint8_t *out_first_color)
{
    uint32_t n     = 0;
    uint8_t first  = 0;
    const uint32_t total = (uint32_t)d->screen_rows * (uint32_t)d->screen_cols;
    for (uint32_t i = 0; i < total; i++) {
        if (d->pixel_map[i] != (uint8_t)COLOR_BLACK) {
            if (n == 0U)
                first = d->pixel_map[i];
            n++;
        }
    }
    if (out_first_color != nullptr)
        *out_first_color = first;
    return n;
}

/** @brief 载荷前 16 字节十六进制 dump（越界/空字节以 '.' 占位；输出 "xx xx ..." 共 47B + NUL）
 *
 *  诊断用：现场只需看这一行就能确认「上位机到底发了什么」——尤其 0x40 的
 *  `payload[12..13]`（端口字段：**高字节在前 BE16**，2026-09-15 用户裁决）是否真的是
 *  `27 2c`（10028）而不是别的编码（ASCII "10"/"28"、偏移错位、或 IP 字段被写成
 *  0.0.0.0 等）。 */
static void _gz_ol_diag_dump16(const uint8_t *pl, uint16_t plen, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (uint8_t i = 0U; i < 16U; i++) {
        if (pl != nullptr && i < plen) {
            out[i * 3U]      = hex[pl[i] >> 4];
            out[i * 3U + 1U] = hex[pl[i] & 0x0FU];
        } else {
            out[i * 3U]      = '.';
            out[i * 3U + 1U] = '.';
        }
        out[i * 3U + 2U] = (i == 15U) ? '\0' : ' ';
    }
}

/**
 * @brief  文本首字符的字库三档（16/24/32）实读诊断 —— 问题①的决定性证据。
 *
 *  对同一字符分别按 FONT_16/24/32 计算地址并实读一次，打印：
 *    地址 / 是否落在存储容量内 / dev_storage_read 返回码 / 非零字节数 / 前 8 字节。
 *  读法：
 *    - rd<0 或 inCAP=0        → 地址越界/读取失败（激活的字库布局与实际芯片不符）
 *    - rd>=0 且 nz=0          → 该字号区块确实是空数据（字库没有这个字号）
 *    - rd>=0 且 nz 合理        → 字库有数据 → 问题在渲染/裁剪/屏体几何侧
 *  另打印「另一套芯片布局」的纯算术地址（不切换全局配置、不读），用于判断
 *  当前激活布局是否选错（DIP2）。
 */
static void _gz_ol_diag_font_probe(const uint8_t *text, uint16_t text_len, font_type_t type)
{
    if (text == nullptr || text_len == 0U) {
        SEGGER_RTT_printf(0, "[gz_ol] font-probe: empty text, skip\n");
        return;
    }

    /* 与 app_render._is_gbk 同判据（此处独立实现，避免为诊断改动渲染内部） */
    const bool gbk = (text_len >= 2U) && (text[0] >= 0x81U && text[0] <= 0xFEU) &&
                     (text[1] >= 0x40U && text[1] <= 0xFEU) && (text[1] != 0x7FU);
    uint8_t ch[2] = {text[0], 0U};
    if (gbk)
        ch[1] = text[1];
    const font_enc_t enc = gbk ? FONT_ENC_GBK : FONT_ENC_ASCII;

    app_render_chip_info_t ci;
    if (app_render_chip_info_get(&ci)) {
        SEGGER_RTT_printf(0, "[gz_ol] font-probe chip=%s cap=%u enc=%s type=%d ch=%02x,%02x\n",
                          ci.name, (unsigned)ci.flash_capacity, gbk ? "GBK" : "ASCII", (int)type,
                          (unsigned)ch[0], (unsigned)ch[1]);
    }

    static const font_size_t sizes[3] = {FONT_16, FONT_24, FONT_32};
    for (uint8_t i = 0U; i < 3U; i++) {
        app_render_glyph_probe_t pb;
        if (!app_render_glyph_probe(sizes[i], type, enc, ch, &pb))
            continue;
        SEGGER_RTT_printf(
            0,
            "[gz_ol]   size=%u addr=0x%06x inCAP=%u rd=%d nz=%u/%u head=%02x %02x %02x %02x %02x %02x %02x %02x\n",
            (unsigned)sizes[i], (unsigned)pb.addr, pb.addr_in_range ? 1U : 0U, (int)pb.ret,
            (unsigned)pb.nonzero, (unsigned)pb.total, (unsigned)pb.head[0], (unsigned)pb.head[1],
            (unsigned)pb.head[2], (unsigned)pb.head[3], (unsigned)pb.head[4], (unsigned)pb.head[5],
            (unsigned)pb.head[6], (unsigned)pb.head[7]);
    }

    /* 另一套芯片布局的地址（纯算术，不切换 s_active_config、不读 Flash）——
     * 判断「激活布局是否选错（DIP2 / 字库芯片型号）」 */
    const font_chip_id_t cur = app_render_chip_current();
    const font_chip_id_t alt = (cur == FONT_CHIP_W25Q64) ? FONT_CHIP_MX25L256 : FONT_CHIP_W25Q64;
    SEGGER_RTT_printf(
        0, "[gz_ol]   alt-layout(%s) addr 16=0x%06x 24=0x%06x 32=0x%06x (arith only)\n",
        (alt == FONT_CHIP_W25Q64) ? "W25Q64" : "MX25L256",
        (unsigned)app_render_glyph_addr_for(alt, FONT_16, type, enc, ch),
        (unsigned)app_render_glyph_addr_for(alt, FONT_24, type, enc, ch),
        (unsigned)app_render_glyph_addr_for(alt, FONT_32, type, enc, ch));
}

#endif /* GZ_OL_RTT_DIAG */

/**
 * @brief  把 0x20 显示文本里的字面两字节序列 `\n`（0x5C 0x6E）转换为换行符 0x0A
 *         （原地转换，输出长度 ≤ 输入长度）。
 *
 *  2026-09-18 用户裁决（doc/14 README §1/§14）：上位机 GBK 文本以字面 `\` + `n` 两个
 *  字节作行分隔符，固件须解析为换行后交给渲染引擎（app_render 支持 0x0A 换行，
 *  山东 '4' / 贵州常规 '4' 先例）。仅处理 `\n`：`\r` 与其它转义序列不解析。
 *
 *  GBK 安全行走（关键正确性）：GBK 双字节字符的**尾字节可以是 0x5C**，朴素两字节匹配
 *  会把「某 GBK 汉字尾字节 0x5C + 下一个 ASCII 'n'」误判成换行、毁掉该汉字。因此按
 *  GBK 结构行走：
 *   - src[i]==0x5C 且 src[i+1]==0x6E → 输出 0x0A，i += 2；
 *   - src[i] 是 GBK 首字节（0x81~0xFE）且后续尾字节合法（0x40~0xFE 除 0x7F）
 *     → 原样拷贝两字节（**不检查尾字节是否为 0x5C**），i += 2；
 *   - 其余单字节拷贝，i += 1。
 *  纯 ASCII 文本（全部 < 0x81）逐字节行走，结果与朴素匹配一致。
 *
 *  原地安全：text 指向任务帧缓冲拷贝（gz_ol_proto_handle_task 的 msg_buf，
 *  GZ_OL_MSG_SIZE = sizeof(frame_msg_t) + GZ_OL_PAYLOAD_MAX = 8 + 256；osMessageQueueGet
 *  取帧后的任务独占私有拷贝，非共享 RB 数据），且本函数输出 ≤ 输入、双指针只落后不
 *  超前，无需额外缓冲。文本上界 = GZ_OL_FRAME_LEN_MAX(256) − GZ_OL_FRAME_LEN_MIN(17)
 *  − GZ_OL_DISPLAY_PREFIX_LEN(17) = 222B，恒在 msg_buf 内。
 *
 * @param  text    文本（GBK/ASCII 混合，可原地修改）。
 * @param  len     文本字节数。
 * @param  out_nl  输出：转换出的换行个数（可为 NULL）。
 * @return 转换后文本字节数（≤ len）。
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
 * @brief  执行 0x20 立即显示：先整屏清黑，再按 (x, y) 定位渲染（按屏宽自动换行）。
 * @param  cmd  解析结果（p.display 为显示参数；data/data_len 仅供诊断 dump 原始载荷）。
 *
 * **字号与屏体几何（2026-09-14 用户裁决修订）**：字号（16/24/32）**不再要求 ≤ 屏高**。
 * 渲染层仅对「行首已完全越出渲染区域」（cur_y >= h）的行终止绘制，行框下缘越界
 * （cur_y < h <= cur_y+字号）保留并由 `dev_display_draw_bitmap()` 按屏幕交集逐像素
 * 裁剪（该函数越界由整块丢弃改为裁剪绘制，见 `Device/Display/dev_display.c`）。因此
 * 「字号 > 屏高」时字形露出**可见的上部**（单模组 32×16 台架上 24/32 点阵），不再是
 * 「清屏后全黑」；224×64 整机字形全在屏内、表现零变化；16 点阵在两种几何上均不变。
 * 本函数传参保持「区域 = 剩余屏幕」：w = 屏宽 − x、h = 屏高 − y——首行不受行终止判定
 * 约束，h 无需为 24/32 放大即可绘制。诊断将该判定打印为「完整/裁剪」。
 * 文本预处理（2026-09-18 用户裁决）：渲染前把字面两字节序列 `\n`（0x5C 0x6E）原地
 * 转换为换行 0x0A（GBK 安全行走，见 _gz_ol_text_unescape_nl），其余字节不变。
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

#if GZ_OL_RTT_DIAG
    {
        char dump[3U * 16U + 1U]; /* "xx xx ... xx" 16 字节 + NUL */
        _gz_ol_diag_dump16(cmd->data, cmd->data_len, dump);
        /* 字形框与屏体几何：完整可见 / 裁剪可见（draw_bitmap 2026-09-14 起按交集裁剪绘制） */
        const uint16_t gw     = (uint16_t)p->font_size; /* GBK 字形宽 = 字号（ASCII 半宽） */
        const uint32_t vis_h  = ((uint32_t)y + p->font_size <= d->screen_cols)
                                    ? (uint32_t)p->font_size
                                    : ((uint32_t)y < d->screen_cols ? (uint32_t)d->screen_cols - y : 0U);
        const uint32_t vis_w  = ((uint32_t)x + gw <= d->screen_rows)
                                    ? (uint32_t)gw
                                    : ((uint32_t)x < d->screen_rows ? (uint32_t)d->screen_rows - x : 0U);
        SEGGER_RTT_printf(0, "[gz_ol] 0x20 plen=%u payload[0..15]=%s\n", (unsigned)cmd->data_len,
                          dump);
        SEGGER_RTT_printf(0,
                          "[gz_ol] 0x20 x=%u y=%u size=%d type=%d color=%d text_len=%u"
                          " -> render x=%u y=%u w=%u h=%u wrap=1 enc=GBK\n",
                          (unsigned)p->x, (unsigned)p->y, (int)p->font_size, (int)p->font_type,
                          (int)p->color, (unsigned)p->text_len, (unsigned)x, (unsigned)y,
                          (unsigned)w, (unsigned)h);
        SEGGER_RTT_printf(0,
                          "[gz_ol] 0x20 size-map field=%u/%u -> FONT_%d glyph_box=%ux%u\n",
                          (unsigned)cmd->data[12], (unsigned)cmd->data[13], (int)p->font_size,
                          (unsigned)gw, (unsigned)p->font_size);
        SEGGER_RTT_printf(0, "[gz_ol] 0x20 screen=%ux%u code=%s glyph_visible=%ux%u of %ux%u%s\n",
                          (unsigned)d->screen_rows, (unsigned)d->screen_cols,
                          (d->module_code != nullptr) ? d->module_code : "?", (unsigned)vis_w,
                          (unsigned)vis_h, (unsigned)gw, (unsigned)p->font_size,
                          (vis_w == gw && vis_h == (uint32_t)p->font_size)
                              ? ""
                              : " *** glyph CLIPPED by screen edge (draw_bitmap intersection) ***");
        /* 字库三档实读（问题①决定性证据，见函数注释） */
        _gz_ol_diag_font_probe((const uint8_t *)p->text, p->text_len, p->font_type);
        /* 渲染前显存基线（清屏后应为 0） */
        SEGGER_RTT_printf(0, "[gz_ol] 0x20 pixel_map before render: nonblack=%u\n",
                          (unsigned)_gz_ol_diag_nonblack(d, nullptr));
    }
#endif

    /* 文本字面 "\n"（0x5C 0x6E）→ 换行 0x0A 原地转换（2026-09-18 用户裁决，GBK 安全行走）。
     * p->text 指向任务帧缓冲拷贝（任务独占、可变，见 _gz_ol_text_unescape_nl 注释），
     * 转换输出 ≤ 输入，无额外缓冲；执行层转换保证 RS485/RS232/UDP10011/专用口四通道一致。 */
    uint16_t text_len = p->text_len;
    uint16_t nl_count = 0U;
    if (text_len > 0U)
        text_len = _gz_ol_text_unescape_nl((char *)p->text, text_len, &nl_count);

#if GZ_OL_RTT_DIAG
    if (nl_count > 0U) {
        SEGGER_RTT_printf(0,
                          "[gz_ol] 0x20 nl-unescape: literal \\n x %u -> 0x0A, text_len %u -> %u"
                          " (GBK-safe walk)\n",
                          (unsigned)nl_count, (unsigned)p->text_len, (unsigned)text_len);
    }
#endif

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

#if GZ_OL_RTT_DIAG
    /* 渲染后显存非黑像素数 = 「渲染是否真的落到 pixel_map」的直接证据：
     *   >0 → 渲染侧正常（屏幕若仍无内容 → 问题在屏体几何/扫描侧，对照横幅 screen=）；
     *   =0 → 渲染/裁剪/字库侧（结合上方 font-probe 的 rd/nz 判定）。 */
    {
        uint8_t first_color = 0;
        const uint32_t nz   = _gz_ol_diag_nonblack(d, &first_color);
        SEGGER_RTT_printf(0,
                          "[gz_ol] 0x20 post-render: nonblack=%u first_color=%u commits=%u\n",
                          (unsigned)nz, (unsigned)first_color,
                          (unsigned)g_dev_display_commit_count);
    }
#endif

    app_scroll_render_unlock();
}

/**
 * @brief  执行 0x30 清屏（全屏清黑）。
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
 * @brief  执行 0x80 创迪扩展全屏纯色。
 *
 *  复现源固件行为：仅识别 FF0000(红)/00FF00(绿)/0000FF(黄) 三色，其余**不动屏幕**；
 *  副作用：关闭光敏自动调光并置亮度 8（最亮）。
 *
 * @param  rgb  三字节 RGB。
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
 * @brief  执行 0x40 修改 IP：写 Sector1（port 字段；udp_port 保留现值）→ 应答 0x50 帧
 *         → 短延时后软复位（源固件语义：应答后重启生效）。
 * @param  ch   来源通道。
 * @param  cmd  解析结果（`p.setip` 为 14B 网络参数；`data/data_len` 仅供诊断 dump 原始载荷）。
 *
 *  诊断（`GZ_OL_RTT_DIAG`）：打印**原始载荷 16 字节**（确认上位机端口字段的字节序/偏移/
 *  编码——10028 必须表现为 `payload[12..13] = 27 2c`；**高字节在前 BE16，2026-09-15 用户
 *  裁决**）+ 写前快照 + 落库返回值 + 写后 read-back。`0x50` 应答只是回显请求，**不是落盘
 *  证据**；落盘证据是 read-back 与重启后 `0x70` 搜索回显（见 README §13.4）。
 */
static void _gz_ol_exec_set_ip(channel_t *ch, const gz_ol_parsed_cmd_t *cmd)
{
    const gz_ol_setip_t *p = &cmd->p.setip;

    /* 端口字段语义（2026-09-14 用户裁决，doc/14 §13）：写 net_cfg.port——重启后由
     * app_net_boot 应用到 TCP 业务口、由本协议专用 UDP 实例（CH_ID_UDP_GZOL）应用到
     * GZ_OL 业务口（同号不同协议栈）；udp_port 保留现值（CQ 专有）。
     * 记录无效时 udp_port 按出厂默认 20103 兜底（与 app_net_boot 默认一致）。 */
    app_board_net_cfg_t cur;
    uint32_t udp_port = (app_board_net_cfg_get(&cur) == 0) ? cur.udp_port : 20103U;

#if GZ_OL_RTT_DIAG
    /* 原始载荷 + 写前快照（自证「改端口是否真的落库」；0x50 回显只是回声、不能作落盘证据） */
    {
        char dump[3U * 16U + 1U];
        _gz_ol_diag_dump16(cmd->data, cmd->data_len, dump);
        SEGGER_RTT_printf(0, "[gz_ol] 0x40 plen=%u payload[0..15]=%s\n", (unsigned)cmd->data_len,
                          dump);
        SEGGER_RTT_printf(0,
                          "[gz_ol] 0x40 port field payload[12..13]=%02x %02x -> %u (BE16); "
                          "ip=%u.%u.%u.%u\n",
                          (unsigned)cmd->data[12], (unsigned)cmd->data[13], (unsigned)p->port,
                          p->ip[0], p->ip[1], p->ip[2], p->ip[3]);
    }
    {
        app_board_net_cfg_t before;
        const bool before_ok = (app_board_net_cfg_get(&before) == 0);
        SEGGER_RTT_printf(0, "[gz_ol] 0x40 write-before: rec=%s port=%u udp_port=%u\n",
                          before_ok ? "valid" : "INVALID",
                          before_ok ? (unsigned)before.port : 0U,
                          before_ok ? (unsigned)before.udp_port : 0U);
        SEGGER_RTT_printf(0, "[gz_ol] 0x40 requested: ip=%u.%u.%u.%u mask=%u.%u.%u.%u gw=%u.%u.%u.%u "
                             "port=%u (keep udp_port=%u)\n",
                          p->ip[0], p->ip[1], p->ip[2], p->ip[3], p->mask[0], p->mask[1],
                          p->mask[2], p->mask[3], p->gw[0], p->gw[1], p->gw[2], p->gw[3],
                          (unsigned)p->port, (unsigned)udp_port);
    }
#endif

    const int32_t wr_ret = app_board_net_cfg_update(p->ip, p->mask, p->gw, (uint32_t)p->port, udp_port);

#if GZ_OL_RTT_DIAG
    /* 写后立即 read-back（唯一有效的落盘证据）+ 重启后预期端口提示 */
    {
        app_board_net_cfg_t after;
        const bool after_ok = (app_board_net_cfg_get(&after) == 0);
        SEGGER_RTT_printf(0,
                          "[gz_ol] 0x40 write ret=%d (%s) readback=%s ip=%u.%u.%u.%u port=%u "
                          "udp_port=%u\n",
                          (int)wr_ret, (wr_ret == 0) ? "OK" : "ERR(erase/write failed)",
                          after_ok ? "valid" : "INVALID", after.ip[0], after.ip[1], after.ip[2],
                          after.ip[3], after_ok ? (unsigned)after.port : 0U,
                          after_ok ? (unsigned)after.udp_port : 0U);
        SEGGER_RTT_printf(0,
                          "[gz_ol] 0x40 ack=0x50 echo (NOT evidence) -> reboot in 100ms; after "
                          "reboot expect: GZ_OL UDP service port=%u (CH_ID_UDP_GZOL) + TCP biz "
                          "port=%u + 10011 unchanged\n",
                          (unsigned)p->port, (unsigned)p->port);
    }
#endif
    (void)wr_ret; /* 非诊断构建下显式忽略（诊断构建下已在上一块打印） */

    /* 应答 0x50 帧（**按解析值重建**本次下发的配置，与源固件一致；
     * 端口字段与请求同序 = BE16 高字节在前，2026-09-15 用户裁决；**回显不是落盘证据**） */
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
 *
 *  协议文档「若硬件无此故障信息硬件，该位补 0」→ STD 硬件无对应故障采集点，
 *  全 0 合规且诚实（源固件为空实现不应答，会让上位机轮询超时，不复制该缺陷）。
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
        case GZ_OL_PCMD_SET_IP_ACK: /* 入站 0x50 兼容为搜索触发（README Q3 推荐 C） */
            _gz_ol_exec_search(ch);
            break;
        default:
            break; /* 0x70 等仅出站命令字：静默丢弃 */
    }
}
