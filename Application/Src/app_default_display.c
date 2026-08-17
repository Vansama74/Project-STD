/**
 * @file    app_default_display.c
 * @brief   注册制默认显示界面 — 协议模块可选覆盖，默认欢迎画面兜底
 *
 * init_task 在 sw_board_init（sw_initcall 执行点）之后调用
 * app_default_display：协议模块若在 sw_app_initcall 中注册了回调，
 * 则显示其默认界面；否则回退渲染「欢迎行驶\n高速公路」。
 */

#include <stdint.h>
#include <string.h>

#include "app_default_display.h"
#include "app_render.h"
#include "dev_display.h"

/* 默认显示回调槽：首个注册生效（4B bss） */
static app_default_display_fn_t s_default_fn = nullptr;

void app_default_display_register(app_default_display_fn_t fn)
{
    if (s_default_fn == nullptr)
        s_default_fn = fn;
}

void app_default_display(void)
{
    if (s_default_fn) {
        s_default_fn();
        return;
    }

    /* 无协议注册：回退默认欢迎画面 */
    app_render(&(render_cfg_t){
        .type  = RENDER_TEXT,
        .x     = 0,
        .y     = 0,
        .w     = dev_display_get()->screen_rows,
        .h     = dev_display_get()->screen_cols,
        .style = &(render_style_t){
            .h_align = ALIGN_CENTER,
            .v_align = ALIGN_LEFT_UP,
        },
        .color     = COLOR_YELLOW,
        .text      = "欢迎行驶\n高速公路",
        .len       = strlen("欢迎行驶\n高速公路"),
        .font_size = FONT_SELF_ADAPT,
        .font_type = FONT_HT,
        .text_enc  = FONT_ENC_UTF8,
    });
}