/**
 * @file    app_default_display_test_1n2_20260917.c
 * @brief   **历史现场前提快照** —— 第二十五/二十六轮「默认画面 = 测试串 "1\n2"」的源码事实
 *
 * 这不是驱动、不参与任何构建（不在 Makefile `SRC_FILES`、不在 `.eide/eide.yml`），
 * 仅作为 `.analysis/22_1665/round26_two_hubs.py` / `round27_wiring_b_and_etc.py`
 * 的**历史口径断言锚点**：那两个脚本取证的是「当时板上固件 + 当时默认画面」，
 * 而默认画面是**外部改动**（非驱动改动），2026-09-17 第二十八轮后已由用户回改为
 * 标准欢迎画面「欢迎行驶\n高速公路」（`FONT_SELF_ADAPT`）——于是现场前提撤销，
 * 脚本不再对现行 `Application/Src/app_default_display.c` 做判红断言，改为锚定本快照。
 *
 * 内容依据（当时的源码事实，逐字段记录）：
 *   · `Application/Src/app_default_display.c` `_render_welcome()` 的渲染参数；
 *   · 记录来源：`.analysis/22_1665/round26_two_hubs.md` §「现场前提」表（第二十六轮现场原文）。
 * 若将来又要复现该现场（双 HUB 口判型），把下面 `_render_welcome()` 的字段抄回
 * `Application/Src/app_default_display.c` 即可（驱零代码改动）。
 */

#include <string.h>

#include "app_render.h"
#include "dev_display.h"

/* 第二十五/二十六轮现场默认画面（测试串："1\n2" = 真换行两行；FONT_16 黑体） */
static void _render_welcome(void)
{
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
        .text      = "1\n2",
        .len       = strlen("1\n2"),
        .font_size = FONT_16,
        .font_type = FONT_HT,
        .text_enc  = FONT_ENC_UTF8,
    });
}
