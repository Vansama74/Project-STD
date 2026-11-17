/**
 * @file    app_gz_ol_proto_default.c
 * @brief   贵州治超屏协议上电默认画面（三行绿字，等价复现源固件 PowerOnDisplay()）
 *
 * `app_default_display_register` 单槽、首个注册生效：全协议构建与山东等注册者竞争
 * （归属由 initcall 链接序决定），量产单协议构建无竞争；文案含前导空格（勿删），
 * 字号/字型详见 doc/14_贵州治超屏协议/README.md §7
 */

#include <stdint.h>

#include "app_default_display.h"
#include "app_render.h"
#include "dev_display.h"
#include "initcall.h"

/* 源固件上电三行文案（UTF-8 字面量；前导空格与源固件一致） */
static const char s_gz_ol_default_line0[] = "贵州省";
static const char s_gz_ol_default_line1[] = "  高速公路";
static const char s_gz_ol_default_line2[] = "     欢迎您!";

/**
 * @brief  渲染一行上电文案（FONT_16 黑体绿字，左起 x=0）。
 * @param  line  行号 0~2（行高 = FONT_16）。
 * @param  text  UTF-8 文本。
 * @param  len   文本字节数。
 */
static void _gz_ol_default_line(dev_display_t *d, uint16_t line, const char *text, uint16_t len)
{
    const uint16_t y = (uint16_t)(line * FONT_16);
    if (y >= d->screen_cols)
        return;

    app_render(&(render_cfg_t){
        .type      = RENDER_TEXT,
        .x         = 0,
        .y         = y,
        .w         = d->screen_rows,
        .h         = FONT_16,
        .style     = &(render_style_t){
            .h_align   = ALIGN_LEFT_UP,
            .v_align   = ALIGN_LEFT_UP,
            .word_wrap = false,
        },
        .color     = COLOR_GREEN,
        .text      = text,
        .len       = len,
        .font_size = FONT_16,
        .font_type = FONT_HT, /* 源固件 fontType = FONTHT（黑体） */
        .text_enc  = FONT_ENC_UTF8,
    });
}

/** @brief 清屏后逐行渲染上电文案，并提交帧。 */
static void _gz_ol_default_show(void)
{
    dev_display_t *d = dev_display_get();
    if (d == nullptr)
        return;

    dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK);

    _gz_ol_default_line(d, 0U, s_gz_ol_default_line0, sizeof(s_gz_ol_default_line0) - 1U);
    _gz_ol_default_line(d, 1U, s_gz_ol_default_line1, sizeof(s_gz_ol_default_line1) - 1U);
    _gz_ol_default_line(d, 2U, s_gz_ol_default_line2, sizeof(s_gz_ol_default_line2) - 1U);

    dev_display_commit_frame(d);
}

static void gz_ol_default_display_init(void)
{
    app_default_display_register(_gz_ol_default_show);
}
sw_app_initcall(gz_ol_default_display_init);
