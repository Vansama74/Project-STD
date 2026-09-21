/**
 * @file    host_driver.c
 * @brief   22-1665 宿主差分测试 harness（x86-64 直跑驱动编译产物）
 *
 * 用法：`host_driver < pixel_maps.bin`（每用例 `buffer_size` 字节，读到 EOF 为止）
 *       由 `.analysis/22_1665/check_host_differential.py` 驱动。
 *
 * 输出（stdout，逐用例）：
 *   GEOM screen=<w>x<h> buffer=<n> scanline=<n> modules=<r>x<c>      — 基类字段自证
 *   CASE <i>
 *   S <frame_state 十六进制>                                          — prepare 产物
 *   T <写序列逐项 port:val>                                           — scan 的 BSRR 写序
 *   K <clock 数>                                                      — CLK 脉冲次数
 *   R <A><B><C><D>                                                    — set_row(0) 后的行址电平
 *
 * 说明：本 harness 提供的桩把真机的「内联寄存器写」变成可记录的调用，
 *       新旧两份驱动（或新驱动 vs Python 参考模型）用同一 harness 编译 ⇒ 输出可直接对拍。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dev_display.h"

/* ================================================================
 *  宿主端口池与引脚表（引脚值与真板一致：G/B/E 三个端口；
 *  通道 0/1 = PG9/PG10/PG15/PB6（组 0）；通道 2/3 = PB8/PB9/PE1/PE2（组 1））
 * ================================================================ */
static GPIO_TypeDef s_pg, s_pb, s_pe;

#define HOST_PORTS 3U
static GPIO_TypeDef *const s_port_pool[HOST_PORTS] = {&s_pg, &s_pb, &s_pe};
static const char *const s_port_name[HOST_PORTS] = {"G", "B", "E"};

/* 真板引脚（Core/Inc/main.h）：0/1 = 组 0（PG9/PG10/PG15/PB6）、2/3 = 组 1（PB8/PB9/PE1/PE2）；
 * 4..9 为不与真脚撞号的占位（当前口径不用） */
const hub75_pin_t g_hub75_pin_r[HUB75_CHANNEL_MAX] = {
    {&s_pg, 0x0200U}, /* R1 = PG9  */
    {&s_pg, 0x8000U}, /* R2 = PG15 */
    {&s_pb, 0x0100U}, /* R3 = PB8  */
    {&s_pe, 0x0002U}, /* R4 = PE1  */
    {&s_pg, 0x0001U}, {&s_pg, 0x0002U}, {&s_pg, 0x0004U}, {&s_pg, 0x0008U}, {&s_pg, 0x0010U},
    {&s_pg, 0x0020U},
};
const hub75_pin_t g_hub75_pin_g[HUB75_CHANNEL_MAX] = {
    {&s_pg, 0x0400U}, /* G1 = PG10 */
    {&s_pb, 0x0040U}, /* G2 = PB6  */
    {&s_pb, 0x0200U}, /* G3 = PB9  */
    {&s_pe, 0x0004U}, /* G4 = PE2  */
    {&s_pb, 0x0008U}, {&s_pb, 0x0010U}, {&s_pb, 0x0020U}, {&s_pb, 0x0080U}, {&s_pb, 0x0400U},
    {&s_pb, 0x0800U},
};
const hub75_pin_t g_hub75_pin_b[HUB75_CHANNEL_MAX] = {
    {&s_pb, 0x0400U}, {&s_pb, 0x0800U}, {&s_pe, 0x0001U}, {&s_pe, 0x0008U}, {&s_pe, 0x0010U},
    {&s_pe, 0x0020U}, {&s_pb, 0x1000U}, {&s_pb, 0x2000U}, {&s_pb, 0x4000U}, {&s_pg, 0x0001U},
};

/* ================================================================
 *  记录设施
 * ================================================================ */
#define TRACE_MAX (1U << 16)
typedef struct {
    uint8_t port;
    uint32_t val;
} flush_rec_t;

static flush_rec_t s_trace[TRACE_MAX];
static size_t s_trace_n;
static size_t s_clocks;
static uint8_t s_row[4];
static dev_display_t *s_dev;

static int _host_port_idx(GPIO_TypeDef *p)
{
    for (uint8_t i = 0; i < HOST_PORTS; i++)
        if (p == s_port_pool[i])
            return (int)i;
    return -1;
}

void pl_hub75_bsrr_flush(const pl_hub75_bsrr_t *p)
{
    if (s_trace_n < TRACE_MAX) {
        s_trace[s_trace_n].port = (uint8_t)_host_port_idx(p->port);
        s_trace[s_trace_n].val  = p->val;
        s_trace_n++;
    }
}

void pl_hub75_clock_pulse(void)
{
    s_clocks++;
}

void pl_hub75_Decoder_set_row(uint8_t row)
{
    for (uint8_t i = 0; i < 4U; i++)
        s_row[i] = (uint8_t)((row >> i) & 1U);
}

void dev_display_register(dev_display_t *dev)
{
    s_dev = dev;
}

/* ================================================================
 *  被测驱动入口
 * ================================================================ */
extern void dev_display_22_1665_init(void);
extern dev_display_t *dev_display_22_1665_get(void);

int main(void)
{
    dev_display_22_1665_init();
    dev_display_t *dev = dev_display_22_1665_get();
    if (dev == nullptr || s_dev != dev) {
        fprintf(stderr, "harness: dev_display 注册失败\n");
        return 2;
    }

    printf("GEOM screen=%ux%u buffer=%u scanline=%u modules=%ux%u\n",
           (unsigned)dev->screen_rows, (unsigned)dev->screen_cols,
           (unsigned)dev->buffer_size, (unsigned)dev->scan_line_pixels,
           (unsigned)dev->modules_per_row, (unsigned)dev->modules_per_col);

    uint8_t *pm = malloc(dev->buffer_size);
    if (pm == nullptr)
        return 2;

    unsigned case_no = 0;
    while (fread(pm, 1, dev->buffer_size, stdin) == dev->buffer_size) {
        memcpy(dev->pixel_map, pm, dev->buffer_size);
        dev->dirty_rect_valid = false; /* 全量重排（不走脏矩形早退） */

        printf("CASE %u\n", case_no);
        dev->ops->prepare(dev);

        printf("S ");
        for (uint16_t i = 0; i < dev->scan_line_pixels; i++)
            printf("%02x", dev->hub75_buff[i]);
        printf("\n");

        s_trace_n = 0;
        s_clocks  = 0;
        memset(s_row, 0, sizeof(s_row));
        dev->ops->scan(dev, 0);
        dev->ops->set_row(0);

        printf("T %zu", s_trace_n);
        for (size_t i = 0; i < s_trace_n; i++)
            printf(" %s%08x", s_port_name[s_trace[i].port], s_trace[i].val);
        printf("\n");

        printf("K %zu\n", s_clocks);
        printf("R %u%u%u%u\n", s_row[0], s_row[1], s_row[2], s_row[3]);
        case_no++;
    }

    free(pm);
    return 0;
}
