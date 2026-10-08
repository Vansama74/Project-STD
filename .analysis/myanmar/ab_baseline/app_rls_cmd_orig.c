#include "app_rls_cmd.h"

#include "app_key.h"
#include "app_render.h"
#include "app_scroll.h"

static void cmd_test(channel_t *ch, void *data);
static void cmd_display(channel_t *ch, void *data);
static void cmd_display_save(channel_t *ch, void *data);

const rls_cmd_handler_fn_t g_rls_cmd_table[] = {
    cmd_test,
    cmd_display,
    cmd_display_save,
};

[[maybe_unused]] static void cmd_test(channel_t *ch, void *data)
{
    (void)ch;
    (void)data;
}

static void cmd_display(channel_t *ch, void *data)
{
    (void)ch;
    rls_dispaly_t *display_ctx = (rls_dispaly_t *)data;

    // 显示前先清屏
    app_render(&(render_cfg_t){
        .type  = RENDER_FILL,
        .x     = 0,
        .y     = 0,
        .w     = 0,
        .h     = 0,
        .color = COLOR_BLACK,
    });
    app_render(&(render_cfg_t){
        .type   = RENDER_BITMAP,
        .x      = 0,
        .y      = 0,
        .w      = dev_display_get()->screen_rows,
        .h      = dev_display_get()->screen_cols,
        .color  = display_ctx->color,
        .bitmap = display_ctx->bitmap,
    });
}

static void cmd_display_save(channel_t *ch, void *data)
{
    (void)ch;
    rls_dispaly_t *display_ctx = (rls_dispaly_t *)data;

    // 显示前先清屏
    app_render(&(render_cfg_t){
        .type  = RENDER_FILL,
        .x     = 0,
        .y     = 0,
        .w     = 0,
        .h     = 0,
        .color = COLOR_BLACK,
    });
    app_render(&(render_cfg_t){
        .type   = RENDER_BITMAP,
        .x      = 0,
        .y      = 0,
        .w      = dev_display_get()->screen_rows,
        .h      = dev_display_get()->screen_cols,
        .color  = display_ctx->color,
        .bitmap = display_ctx->bitmap,
    });
    app_render_save();
}

/* ================================================================
 *  干接点车道状态显示（2026-09-09）
 *
 *  三路干接点：SW1(PE12)=ETC专用 / SW2(PE11)=ETC/人工 / SW3(PE10)=车道关闭
 *  （低有效=按下/闭合）。由 rls_handle_task 队列 100ms 超时节拍驱动。
 *
 *  消抖：dev_key_get_state 直读 GPIO（无软件滤波），本模块两级采样确认——
 *  状态向量连续两拍相同（约 100ms 窗口）才提交显示。
 *
 *  行为：仅在状态变化瞬间全屏渲染一次；此后上位机 RLS bitmap 帧可正常
 *  覆盖。三路全开 → 清屏（清除干接点残留）；多路闭合 → 优先级
 *  车道关闭 > ETC专用 > ETC/人工（安全语义）。渲染与提交整段持
 *  app_scroll_render_lock，与 app_scroll 滚动渲染串行（⑤ 渲染互斥）。
 * ================================================================ */

typedef enum {
    RLS_DC_NONE       = 0, /* 三路全开 → 全屏清除 */
    RLS_DC_ETC_ONLY   = 1, /* SW1 闭合 → ETC专用（绿） */
    RLS_DC_ETC_MANUAL = 2, /* SW2 闭合 → ETC/人工（绿） */
    RLS_DC_LANE_CLOSE = 3, /* SW3 闭合 → 车道关闭（红） */
} rls_dc_state_t;

/* 干接点显示文本（UTF-8 字面量，经 app_render FONT_ENC_UTF8 自动转 GBK） */
static const uint8_t s_dc_text_etc_only[]   = "ETC专用";
static const uint8_t s_dc_text_etc_manual[] = "ETC/人工";
static const uint8_t s_dc_text_lane_close[] = "车道关闭";

/* 状态机缓存：上一拍原始向量（去抖确认用）+ 已提交显示的状态 */
static uint8_t s_dc_raw_prev;
static rls_dc_state_t s_dc_committed;

/** @brief 状态向量解码：多路闭合按 车道关闭 > ETC专用 > ETC/人工 优先级。 */
static rls_dc_state_t _rls_dc_decode(uint8_t raw)
{
    if (raw & 0x04U) /* SW3 车道关闭：最高优先级（安全语义） */
        return RLS_DC_LANE_CLOSE;
    if (raw & 0x01U) /* SW1 ETC专用 */
        return RLS_DC_ETC_ONLY;
    if (raw & 0x02U) /* SW2 ETC/人工 */
        return RLS_DC_ETC_MANUAL;
    return RLS_DC_NONE;
}

/** @brief 全屏渲染干接点状态（FONT_24 居中；NONE=清屏）。 */
static void _rls_dc_render(rls_dc_state_t state)
{
    dev_display_t *d = dev_display_get();
    if (!d)
        return;

    app_scroll_render_lock(); /* ⑤ 渲染互斥：与滚动渲染串行，防混合帧 */

    if (state == RLS_DC_NONE) {
        /* 三路全开：清除干接点残留显示 */
        dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK);
        dev_display_commit_frame(d);
        app_scroll_render_unlock();
        return;
    }

    const uint8_t *text;
    display_color_t color;
    switch (state) {
        case RLS_DC_ETC_ONLY:
            text  = s_dc_text_etc_only;
            color = COLOR_GREEN;
            break;
        case RLS_DC_ETC_MANUAL:
            text  = s_dc_text_etc_manual;
            color = COLOR_GREEN;
            break;
        default: /* RLS_DC_LANE_CLOSE */
            text  = s_dc_text_lane_close;
            color = COLOR_RED;
            break;
    }

    /* 清屏 + FONT_24 全屏居中渲染（app_render 内部统一提交帧） */
    dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK);
    app_render(&(render_cfg_t){
        .type = RENDER_TEXT,
        .x    = 0,
        .y    = 0,
        .w    = d->screen_rows,
        .h    = d->screen_cols,
        .style = &(render_style_t){
            .h_align   = ALIGN_CENTER,
            .v_align   = ALIGN_CENTER,
            .word_wrap = false,
        },
        .color     = color,
        .text      = (const char *)text,
        .len       = (uint16_t)strlen((const char *)text),
        .font_size = FONT_24,
        .font_type = FONT_ST,
        .text_enc  = FONT_ENC_UTF8,
    });

    app_scroll_render_unlock();
}

/** @brief 干接点轮询：采样三路、两级消抖、状态变化时渲染一次。 */
void rls_dry_contact_poll(void)
{
    /* 采样当前拍：bit0=SW1 / bit1=SW2 / bit2=SW3（1=闭合） */
    uint8_t raw = (app_key_get_state(DEV_KEY_SW1) ? 0x01U : 0U) |
                  (app_key_get_state(DEV_KEY_SW2) ? 0x02U : 0U) |
                  (app_key_get_state(DEV_KEY_SW3) ? 0x04U : 0U);

    /* 两级采样消抖：与上一拍不同只更新缓存、不提交，下一拍（100ms 后）确认 */
    if (raw != s_dc_raw_prev) {
        s_dc_raw_prev = raw;
        return;
    }

    rls_dc_state_t state = _rls_dc_decode(raw);
    if (state == s_dc_committed)
        return; /* 无变化：不渲染、不抢屏（协议 bitmap 帧正常显示） */

    s_dc_committed = state;
    _rls_dc_render(state);
}
