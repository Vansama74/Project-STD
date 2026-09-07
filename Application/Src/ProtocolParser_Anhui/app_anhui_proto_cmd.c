/**
 * @file    app_anhui_proto_cmd.c
 * @brief   安徽费显协议命令执行（渲染、GPIO、语音）
 *
 * 协议文档未定义应答 → 全部命令单向不回。
 * 动态显示（0x86~0x89 滚动/扫屏）当前留空（TODO 占位，见 _anhui_exec_scroll）。
 * 录制语音（0x96/0x97）依赖语音板段号接口，当前占位（见 _anhui_exec_voice_seg）。
 */

#include "app_anhui_proto_cmd.h"
#include "app_dispatch.h"

#include <stdint.h>

#include "app_render.h"
#include "app_light_sensor.h"
#include "dev_display.h"
#include "dev_io_ctrl.h"
#include "app_anhui_proto_voice.h"

/* 协议文档未定义显示颜色 → 默认红色（费额显示器常规红）；待上位机联调确认 */
#define ANHUI_DEFAULT_COLOR COLOR_RED

/**
 * @brief  执行清屏（0x81）。
 */
static void _anhui_exec_clear(void)
{
    dev_display_t *d = dev_display_get();
    if (!d)
        return;
    dev_display_fill(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK);
    dev_display_commit_frame(d);
}

/**
 * @brief  执行显示点（0x82）/关闭点（0x83）。
 * @param  p     坐标参数。
 * @param  color 写入颜色（关闭点 = COLOR_BLACK）。
 * @note   坐标上限以模组驱动实际屏幕尺寸为准（screen_rows 宽/screen_cols 高，
 *         用户裁决 2026-09-04）：越界坐标由 dev_display_set_pixel 内部边界
 *         检查丢弃（执行但不落屏）。
 */
static void _anhui_exec_pixel(const anhui_point_t *p, display_color_t color)
{
    dev_display_t *d = dev_display_get();
    if (!d)
        return;
    dev_display_set_pixel(d, p->x, p->y, color);
    dev_display_commit_frame(d);
}

/**
 * @brief  执行静态显示（0x85）。
 * @param  p  静态显示参数（X/Y + GBK 文本）。
 * @note   文档定义四行行首 Y = 00/10/20/30（16 点阵），按 FONT_16 渲染，
 *         单行不换行、超宽截断；文本前不清行（文档未要求）。
 *         坐标上限以实际屏幕尺寸为准（用户裁决 2026-09-04）：起点越界
 *         整区域不可见不落屏，渲染宽度截断到屏宽（screen_rows - x）。
 */
static void _anhui_exec_static_text(const anhui_static_text_t *p)
{
    dev_display_t *d = dev_display_get();
    if (!d)
        return;
    if (p->x >= d->screen_rows || p->y >= d->screen_cols)
        return; /* 起点越界：整区域不可见，不落屏 */

    app_render(&(render_cfg_t){
        .type = RENDER_TEXT,
        .x    = p->x,
        .y    = p->y,
        .w    = (uint16_t)(d->screen_rows - p->x),
        .h    = FONT_16,
        .style = &(render_style_t){
            .h_align   = ALIGN_LEFT_UP,
            .v_align   = ALIGN_LEFT_UP,
            .word_wrap = false,
        },
        .color     = ANHUI_DEFAULT_COLOR,
        .text      = (const char *)p->text,
        .len       = p->text_len,
        .font_size = FONT_16,
        .font_type = FONT_ST,
        .text_enc  = FONT_ENC_GBK, /* 协议 ANSI = GBK，直通不转换 */
    });
}

/**
 * @brief  执行动态显示（0x86~0x89）/动态停止。
 * @param  p  动态显示参数。
 *
 * TODO（动态显示留空）：滚动/扫屏类动态显示功能尚未实现（**已裁决 2026-09-04：
 * 继续留空不实现**）。帧已按文档解析校验：p->mode（运动模式，0=停止）、p->speed（速度 ×2ms）、
 * p->stay_ms（停留时间 ×100ms）、p->text（滚动文本，GBK）。
 * 实现时需要：
 *   1. 按行（p->row 0~3，对应命令 86~89）创建/驱动滚动渲染任务，速度与停留
 *      时间按 ×2ms / ×100ms 单位换算；
 *   2. mode==0 停止该行滚动（含文档「运态停止」帧 00 00 00 00）；
 *   3. 滚动渲染需与 scan_task 的 prepare 协作并核算任务栈/缓冲预算。
 */
static void _anhui_exec_scroll(const anhui_scroll_t *p)
{
    (void)p;
}

/**
 * @brief  执行设置亮度（0x92）。
 * @param  value  亮度参数两字节（大端，0~999）。
 * @note   用户裁决（2026-09-04）：0 = 恢复光敏自动调光（恢复光敏任务），
 *         1~999 均匀映射 8 档亮度 light_level 1..8（8 = OE 100% 常亮）：
 *             level = 1 + value*7/999
 *         底层 8 档已具备（DEV_DISPLAY_BRIGHTNESS_MAX=8，TIM4 PWM 1/8 粒度），
 *         映射结果恒落在 1..8 内，无需再钳制。
 */
static void _anhui_exec_brightness(uint16_t value)
{
    dev_display_t *d = dev_display_get();
    if (!d)
        return;

    if (value == 0) {
        /* 自动亮度：恢复光敏任务（此前可能被手动档挂起） */
        if (g_light_sensor_task_handle != nullptr)
            osThreadResume(g_light_sensor_task_handle);
        return;
    }

    if (g_light_sensor_task_handle != nullptr)
        osThreadSuspend(g_light_sensor_task_handle); /* 手动设定 → 关闭自动调光 */

    /* 1~999 均匀映射 8 档：value=1 → 1（最暗）、999 → 8（最亮），
     * 分子最大 6993，结果恒在 1..8 内，不会超 DEV_DISPLAY_BRIGHTNESS_MAX */
    uint8_t level = (uint8_t)(1U + ((uint32_t)value * 7U / 999U));
    dev_display_set_brightness(d, level);
}

/**
 * @brief  执行设置通行灯/报警器（0x94）。
 * @param  p  IO 控制参数（type：01=通行灯、02=报警器；param：00=灭/关、01=亮/开）。
 * @note   报警器映射黄闪灯（PD15）——硬件无独立报警器输出；待上位机联调确认。
 */
static void _anhui_exec_io_ctrl(const anhui_io_ctrl_t *p)
{
    if (p->type == 1) {
        dev_io_lane_light(p->param == 1);
    } else {
        dev_io_flash_light(p->param == 1);
    }
}

/**
 * @brief  执行播放声音（0x95）。
 * @param  p  语音参数（编号 01~15 + 音量 0~9 + 变长参数）。
 * @note   音量文档定义 0~9 共 10 级：解析层仍按协议接受该字节（字段保留），
 *         执行层忽略——语音板不支持协议调音量，不再调用 dev_rs232_voice_volume
 *         （用户裁决 2026-09-04）；编号 01~14 模板句构造见
 *         app_anhui_proto_voice.c；编号 15 参数区为全部播报内容（GBK 自由文本），
 *         经 dev_rs232_voice_play 直送语音板（232_2 = USART6 语音口，路径不变）。
 */
static void _anhui_exec_voice(const anhui_voice_t *p)
{
    /* 音量字节被忽略（见上），仅播报文本 */
    anhui_voice_play(p->voice_no, p->params, p->params_len);
}

/**
 * @brief  执行播放录制语音（0x96）/录制语音（0x97）。
 * @param  p  段号参数。
 *
 * TODO（占位）：语音板（dev_rs232_voice TTS，USART6）当前仅提供文本合成播报
 * （dev_rs232_voice_play）与音量接口，无「录制语音段号」播报/录制接口。
 * 0x96/0x97 段号已解析校验，待语音板协议支持段号播报/录制后接入。
 */
static void _anhui_exec_voice_seg(const anhui_voice_seg_t *p)
{
    (void)p;
}

/**
 * @brief  执行安徽协议命令。
 * @param  ch   当前通道（协议无应答，未使用）。
 * @param  cmd  解析后的命令结构体。
 */
void anhui_execute_cmd(channel_t *ch, const anhui_parsed_cmd_t *cmd)
{
    (void)ch;
    if (!cmd || cmd->sta != ANHUI_PARSE_OK)
        return;

    switch (cmd->cmd) {
        case ANHUI_PCMD_CLEAR:
            _anhui_exec_clear();
            break;
        case ANHUI_PCMD_PIXEL_ON:
            _anhui_exec_pixel(&cmd->p.point, ANHUI_DEFAULT_COLOR);
            break;
        case ANHUI_PCMD_PIXEL_OFF:
            _anhui_exec_pixel(&cmd->p.point, COLOR_BLACK);
            break;
        case ANHUI_PCMD_STATIC_TEXT:
            _anhui_exec_static_text(&cmd->p.static_text);
            break;
        case ANHUI_PCMD_SCROLL_1:
        case ANHUI_PCMD_SCROLL_2:
        case ANHUI_PCMD_SCROLL_3:
        case ANHUI_PCMD_SCROLL_4:
            _anhui_exec_scroll(&cmd->p.scroll);
            break;
        case ANHUI_PCMD_BRIGHTNESS:
            _anhui_exec_brightness(cmd->p.brightness.value);
            break;
        case ANHUI_PCMD_IO_CTRL:
            _anhui_exec_io_ctrl(&cmd->p.io_ctrl);
            break;
        case ANHUI_PCMD_VOICE:
            _anhui_exec_voice(&cmd->p.voice);
            break;
        case ANHUI_PCMD_VOICE_REC_PLAY:
        case ANHUI_PCMD_VOICE_RECORD:
            _anhui_exec_voice_seg(&cmd->p.voice_seg);
            break;
        default:
            break;
    }
}
