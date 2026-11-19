/**
 * @file    app_ldi_device.c
 * @brief   LDI 设备能力层实现（E6/E7/E8/F4H）
 *
 * 硬件映射：
 *   E6 → app_render / dev_display
 *   E7 → dev_lane_light_set（单路灯抽象；黄灯降级红叉，0CH 按附录 A.1 报红）
 *   E8 → dev_io_flash_light（报警器代实现；WorkMode 频率用软闪烁）
 *   F4H → dev_rs232_voice（RS232 TTS 语音板，USART6；仅按文字播报）
 *
 * 附录 A.1 running_status：
 *   E6: 00H关闭 / 01H开启
 *   E7: 00H红灯 / 01H绿灯
 *   E8: 00H停止 / 01H报警中
 */
    
#include "app_ldi_device.h"

#include <string.h>

#include "cmsis_os2.h"
#include "app_render.h"
#include "dev_display.h"
#include "dev_io_ctrl.h"
#include "dev_lane_light.h"
#include "dev_rs232_voice.h"
#include "app_ldi_etc_bridge.h"

/* ---- LDI → 四川 ETC 转义钩子（weak 空实现）----
 * 强实现见 Application/Src/app_ldi_etc_bridge.c；EIDE 排除该文件时回退到本层，
 * 钩子退化为无操作（不影响 LDI 自身行为与应答）。 */
__attribute__((weak)) void app_ldi_etc_display(uint8_t row, const uint8_t *text, uint16_t len)
{
    (void)row;
    (void)text;
    (void)len;
}

__attribute__((weak)) void app_ldi_etc_clear(void)
{
}

__attribute__((weak)) void app_ldi_etc_lamp(uint8_t color)
{
    (void)color;
}

__attribute__((weak)) void app_ldi_etc_alarm(bool on)
{
    (void)on;
}

__attribute__((weak)) void app_ldi_etc_heartbeat(void)
{
}

/* ---- 状态缓存 ---- */
static ldi_dev_display_sta_t s_display_sta = {
    .running_status = 0x00,
    .font_color     = 0x00,
    .keep_time      = 0x00,
};

static ldi_dev_signal_sta_t s_signal_sta = {
    .running_status = 0x00, /* 附录 A.1：默认红灯 */
    .color          = 0x02, /* 控制色码：红 */
};

static ldi_dev_alarm_sta_t s_alarm_sta = {
    .running_status = 0x00,
    .work_mode      = 0x00,
    .keep_time      = 0x00,
};

/* ---- KeepTime / 频率软定时 ---- */
static bool     s_display_timer_active;
static uint32_t s_display_clear_tick;

static bool     s_alarm_active;
static bool     s_alarm_timer_active;
static uint32_t s_alarm_off_tick;
static bool     s_alarm_freq_active;
static uint32_t s_alarm_toggle_tick;
static bool     s_alarm_out_on;

/* FontSize 01H~08H → 像素字号（协议未规定像素） */
static const font_size_t s_font_size_map[9] = {
    [0] = FONT_SELF_ADAPT,
    [1] = FONT_16,
    [2] = FONT_16,
    [3] = FONT_24,
    [4] = FONT_24,
    [5] = FONT_24,
    [6] = FONT_32,
    [7] = FONT_32,
    [8] = FONT_32,
};

static const display_color_t s_color_map[] = {
    [1] = COLOR_GREEN,
    [2] = COLOR_RED,
    [3] = COLOR_YELLOW,
};

/* ClearType: 00文字清屏→黑；01全红；02全绿；其他扩展（含03黄） */
static const display_color_t s_clear_color_map[] = {
    [0] = COLOR_BLACK,
    [1] = COLOR_RED,
    [2] = COLOR_GREEN,
    [3] = COLOR_YELLOW,
};

#define MAP(table, idx, fallback) \
    (((idx) < (sizeof(table) / sizeof((table)[0]))) ? (table)[(idx)] : (fallback))

static void display_fill(display_color_t color)
{
    app_render(&(render_cfg_t){
        .type  = RENDER_FILL,
        .x     = 0,
        .y     = 0,
        .w     = 0,
        .h     = 0,
        .color = color,
    });
}

static void alarm_output(bool on)
{
    s_alarm_out_on = on;
    dev_io_flash_light(on);
}

static void alarm_stop(void)
{
    alarm_output(false);
    s_alarm_sta.running_status = 0x00;
    s_alarm_active             = false;
    s_alarm_timer_active       = false;
    s_alarm_freq_active        = false;
}

/* ================================================================
 *  模块可用性
 * ================================================================ */

/* 判定「本固件确实实现了该模块」，与 1BH 注册表 / 0CH 可用状态同源：
 * switch 由 app_ldi.h 的 LDI_MODULE_LIST 生成，加入新模块只改那一张列表。 */
bool ldi_device_available(uint8_t device_type)
{
    switch (device_type) {
#define _LDI_MOD_CASE(t, i) case (t): return true;
        LDI_MODULE_LIST(_LDI_MOD_CASE)
#undef _LDI_MOD_CASE
        default:
            return false;
    }
}

void ldi_device_init(void)
{
    s_display_sta.running_status = 0x00;
    s_signal_sta.running_status  = 0x00; /* A.1 红灯 */
    s_signal_sta.color           = 0x02;
    s_alarm_sta.running_status   = 0x00;
    s_display_timer_active       = false;
    alarm_stop();

    dev_lane_light_set(DEV_LANE_LIGHT_RED_CROSS);
}

/* ================================================================
 *  E6 信息显示屏
 * ================================================================ */

static bool display_params_ok(const ldi_ctrl_display_t *ctrl, uint16_t text_len)
{
    if (ctrl->font_color < 0x01 || ctrl->font_color > 0x03)
        return false;
    if (ctrl->font_size > 0x08)
        return false;
    if (ctrl->font_line > 0x06)
        return false;
    if (text_len == 0)
        return false;
    /* 协议：每行最多 10 个中文字符（GBK 约 20 字节）；超长拒绝，避免脏帧撑爆屏 */
    if (text_len > 128)
        return false;
    return true;
}

/* ---- 显示行槽位：固定使用最小字号高度，避免不同字号互相覆盖 ---- */
#define DISPLAY_LINE_SLOT_HEIGHT (16U)

static ldi_dev_result_t display_show(const ldi_ctrl_display_t *ctrl, uint16_t text_len)
{
    if (!display_params_ok(ctrl, text_len))
        return LDI_DEV_FAIL;

    /* 无显示驱动构建：E6 设备无屏可显示 → 按协议回失败
     * （0x01H 由 ldi_cmd 写入应答 status，不静默假装成功）。
     * 有显示时仅多一次判空，后续路径与改动前逐条一致。 */
    if (!dev_display_guard("ldi.e6"))
        return LDI_DEV_FAIL;

    display_color_t color     = MAP(s_color_map, ctrl->font_color, COLOR_GREEN);
    font_size_t     font_size = s_font_size_map[ctrl->font_size];

    /* 协议 '_' → 换行；就地改写调用方缓冲（与 vms_ctrl 一致） */
    uint8_t *text = (uint8_t *)ctrl->text;
    for (uint16_t i = 0; i < text_len; i++) {
        if (text[i] == '_')
            text[i] = '\n';
    }

    render_style_t style = {
        .h_align   = ALIGN_LEFT_UP,
        .v_align   = ALIGN_LEFT_UP,
        .word_wrap = (ctrl->font_line == 0),
    };

    dev_display_t *d  = dev_display_get();
    uint16_t screen_w = d ? d->screen_rows : 0;
    uint16_t screen_h = d ? d->screen_cols : 0;

    uint16_t render_y = 0;
    uint16_t render_h = screen_h;
    align_t  v_align  = ALIGN_LEFT_UP;
    bool     line_selected = false;

    if (ctrl->font_line > 0 && screen_h >= DISPLAY_LINE_SLOT_HEIGHT) {
        uint16_t line_count = screen_h / DISPLAY_LINE_SLOT_HEIGHT;
        if (ctrl->font_line <= line_count) {
            render_y = (uint16_t)((ctrl->font_line - 1U) * DISPLAY_LINE_SLOT_HEIGHT);
            render_h = DISPLAY_LINE_SLOT_HEIGHT;
            line_selected = true;
            dev_display_fill(d, 0, render_y, screen_w, render_h, COLOR_BLACK);
        }
    } else if (ctrl->font_line == 0) {
        dev_display_fill(d, 0, 0, screen_w, screen_h, COLOR_BLACK);
    }

    if (font_size == FONT_SELF_ADAPT && (screen_w == 0 || screen_h == 0))
        font_size = FONT_16;
    if (line_selected && font_size != FONT_SELF_ADAPT && font_size > DISPLAY_LINE_SLOT_HEIGHT)
        font_size = FONT_16;
    style.v_align = v_align;

    app_render(&(render_cfg_t){
        .type      = RENDER_TEXT,
        .x         = 0,
        .y         = render_y,
        .w         = screen_w,
        .h         = render_h,
        .color     = color,
        .text      = (const char *)ctrl->text,
        .len       = text_len,
        .style     = &style,
        .font_size = line_selected ? FONT_SELF_ADAPT : font_size,
        .font_type = FONT_HT,
        .text_enc  = FONT_ENC_GBK,
    });

    s_display_sta.running_status = 0x01; /* A.1 开启 */
    s_display_sta.font_color     = ctrl->font_color;
    s_display_sta.keep_time      = ctrl->keep_time;

    if (ctrl->keep_time == 0) {
        app_render_save();
        s_display_timer_active = false;
    } else {
        s_display_clear_tick   = osKernelGetTickCount() + (uint32_t)ctrl->keep_time * 1000U;
        s_display_timer_active = true;
    }

    /* 转义镜像：行号按实际显示形态（未落到指定行则按全屏），文本为改写后内容 */
    app_ldi_etc_display(line_selected ? ctrl->font_line : 0x00U, (const uint8_t *)ctrl->text, text_len);

    return LDI_DEV_OK;
}

static ldi_dev_result_t display_clear(const ldi_ctrl_display_t *ctrl)
{
    /* 00/01/02 标准；03H 黄为扩展；其余按文字清屏(黑) */
    display_color_t color = COLOR_BLACK;
    if (ctrl->clear_type <= 0x03)
        color = s_clear_color_map[ctrl->clear_type];

    display_fill(color);
    app_render_save();

    s_display_sta.running_status = 0x00; /* A.1 关闭 */
    s_display_sta.font_color     = 0x00;
    s_display_sta.keep_time      = 0x00;
    s_display_timer_active       = false;

    app_ldi_etc_clear();

    return LDI_DEV_OK;
}

ldi_dev_result_t ldi_device_display_ctrl(const ldi_ctrl_display_t *ctrl, uint16_t text_len)
{
    if (ctrl == NULL)
        return LDI_DEV_FAIL;

    if (ctrl->device_func_type == 0x01)
        return display_show(ctrl, text_len);
    if (ctrl->device_func_type == 0x02)
        return display_clear(ctrl);

    return LDI_DEV_FAIL;
}

/* ================================================================
 *  E7 通行信号灯
 *
 *  控制 Color: 01H绿 / 02H红 / 03H黄
 *  0CH 附录 A.1: 00H红灯 / 01H绿灯（无黄）
 *  硬件: 单路 Lane_Light；绿=亮，红/黄=灭；黄上报按红 00H
 * ================================================================ */

ldi_dev_result_t ldi_device_lane_signal_ctrl(const ldi_ctrl_signal_t *ctrl)
{
    if (ctrl == NULL || ctrl->device_func_type != 0x01)
        return LDI_DEV_FAIL;

    switch (ctrl->color) {
        case 0x01: /* 绿 — 通行 */
            if (!dev_lane_light_set(DEV_LANE_LIGHT_GREEN_ARROW))
                return LDI_DEV_FAIL;
            s_signal_sta.color          = 0x01;
            s_signal_sta.running_status = 0x01; /* A.1 绿灯 */
            app_ldi_etc_lamp(0x01);
            return LDI_DEV_OK;
        case 0x02: /* 红 — 禁行 */
            if (!dev_lane_light_set(DEV_LANE_LIGHT_RED_CROSS))
                return LDI_DEV_FAIL;
            s_signal_sta.color          = 0x02;
            s_signal_sta.running_status = 0x00; /* A.1 红灯 */
            app_ldi_etc_lamp(0x02);
            return LDI_DEV_OK;
        case 0x03: /* 黄 — 无独立黄灯，降级为红叉（互锁保证绿箭熄灭） */
            if (!dev_lane_light_set(DEV_LANE_LIGHT_RED_CROSS))
                return LDI_DEV_FAIL;
            s_signal_sta.color          = 0x03;
            s_signal_sta.running_status = 0x00; /* A.1 无黄，按红灯 */
            app_ldi_etc_lamp(0x03);             /* 黄色无 ETC 对应帧，由转义模块决定不发 */
            return LDI_DEV_OK;
        default:
            return LDI_DEV_FAIL;
    }
}

/* ================================================================
 *  E8 报警器（Flash_Light 代实现）
 *
 *  WorkMode: 00H 持续；01H~FFH 频率报警（半周期=work_mode 秒）
 *  KeepTime: 00H 一直；01H~FFH 秒后自动停止
 * ================================================================ */

ldi_dev_result_t ldi_device_alarm_ctrl(const ldi_ctrl_alarm_t *ctrl)
{
    if (ctrl == NULL || ctrl->device_func_type != 0x01)
        return LDI_DEV_FAIL;

    if (ctrl->status == 0x00) {
        s_alarm_sta.work_mode = ctrl->work_mode;
        s_alarm_sta.keep_time = ctrl->keep_time;
        alarm_stop();
        app_ldi_etc_alarm(false); /* 显式停止才镜像（KeepTime 到期自动停止不补发） */
        return LDI_DEV_OK;
    }

    if (ctrl->status != 0x01)
        return LDI_DEV_FAIL;

    s_alarm_sta.work_mode      = ctrl->work_mode;
    s_alarm_sta.keep_time      = ctrl->keep_time;
    s_alarm_sta.running_status = 0x01; /* A.1 报警中 */
    s_alarm_active             = true;

    if (ctrl->work_mode == 0x00) {
        /* 持续报警 */
        alarm_output(true);
        s_alarm_freq_active = false;
    } else {
        /* 频率报警：半周期 = work_mode 秒，先亮后灭循环 */
        alarm_output(true);
        s_alarm_freq_active  = true;
        s_alarm_toggle_tick  = osKernelGetTickCount() + (uint32_t)ctrl->work_mode * 1000U;
    }

    if (ctrl->keep_time == 0) {
        s_alarm_timer_active = false;
    } else {
        s_alarm_off_tick     = osKernelGetTickCount() + (uint32_t)ctrl->keep_time * 1000U;
        s_alarm_timer_active = true;
    }

    app_ldi_etc_alarm(true);

    return LDI_DEV_OK;
}

/* ================================================================
 *  F4H 语音播报
 *
 *  仅「按文字播报」：GBK 文本直送 TTS 语音板（USART6）。
 *  成功判据 = 整帧确实发出（dev_rs232_voice_play 的返回值）。
 * ================================================================ */

bool ldi_device_voice_ctrl(const uint8_t *gbk_text, uint16_t text_len)
{
    return dev_rs232_voice_play(gbk_text, text_len);
}

/* ================================================================
 *  定时轮询 / 状态采集
 * ================================================================ */

void ldi_device_timer_poll(void)
{
    uint32_t now = osKernelGetTickCount();

    if (s_display_timer_active && now >= s_display_clear_tick) {
        display_fill(COLOR_BLACK);
        app_render_save();
        s_display_sta.running_status = 0x00;
        s_display_timer_active       = false;
        app_ldi_etc_clear(); /* KeepTime 到期清屏同步镜像 */
    }

    if (s_alarm_active && s_alarm_timer_active && now >= s_alarm_off_tick) {
        alarm_stop();
        return;
    }

    if (s_alarm_active && s_alarm_freq_active && now >= s_alarm_toggle_tick) {
        alarm_output(!s_alarm_out_on);
        s_alarm_toggle_tick = now + (uint32_t)s_alarm_sta.work_mode * 1000U;
    }
}

bool ldi_device_fill_sta(uint8_t device_type, ldi_device_info_t *info)
{
    if (info == NULL)
        return false;

    switch (device_type) {
        case LDI_DEV_TYPE_DISPLAY:
            info->running_status = s_display_sta.running_status;
            return true;
        case LDI_DEV_TYPE_LANE_SIGNAL:
            info->running_status = s_signal_sta.running_status;
            return true;
        case LDI_DEV_TYPE_ALARM:
            info->running_status = s_alarm_sta.running_status;
            return true;
        default:
            return false;
    }
}

const ldi_dev_display_sta_t *ldi_device_display_sta(void)
{
    return &s_display_sta;
}

const ldi_dev_signal_sta_t *ldi_device_lane_signal_sta(void)
{
    return &s_signal_sta;
}

const ldi_dev_alarm_sta_t *ldi_device_alarm_sta(void)
{
    return &s_alarm_sta;
}
