/**
 * @file    app_anhui_proto_parse.c
 * @brief   安徽费显协议帧解析（纯函数：不碰外设、不发包）
 *
 * 帧结构：5A + 屏号(01) + 命令 + 数据长 + 数据 + CRC + A5。
 * CRC 字段协议文档注明「无检验，默认为 0」→ 不做校验。
 */

#include "app_anhui_proto_parse.h"

/**
 * @brief  判断命令字是否属于安徽协议支持集。
 */
bool anhui_cmd_supported(uint8_t cmd)
{
    switch (cmd) {
        case ANHUI_PCMD_CLEAR:
        case ANHUI_PCMD_PIXEL_ON:
        case ANHUI_PCMD_PIXEL_OFF:
        case ANHUI_PCMD_STATIC_TEXT:
        case ANHUI_PCMD_SCROLL_1:
        case ANHUI_PCMD_SCROLL_2:
        case ANHUI_PCMD_SCROLL_3:
        case ANHUI_PCMD_SCROLL_4:
        case ANHUI_PCMD_BRIGHTNESS:
        case ANHUI_PCMD_IO_CTRL:
        case ANHUI_PCMD_VOICE:
        case ANHUI_PCMD_VOICE_REC_PLAY:
        case ANHUI_PCMD_VOICE_RECORD:
            return true;
        default:
            return false;
    }
}

/** 是否为 ASCII 数字字符 '0'~'9'。 */
static bool anhui_is_ascii_digit(uint8_t c)
{
    return c >= '0' && c <= '9';
}

/**
 * @brief  解析安徽协议原始帧。
 * @param  raw      原始帧数据。
 * @param  raw_len  原始帧长度。
 * @return 解析后的命令结构体；状态字段 sta 标识解析结果。
 * @note   坐标上限以模组驱动实际屏幕尺寸为准（用户裁决 2026-09-04）：
 *         协议文档的 128×64 不作为标准，parse 层不做坐标范围校验，
 *         越界由执行层按 dev_display_get() 的 screen_rows/screen_cols 判界丢弃。
 */
anhui_parsed_cmd_t anhui_parse_frame(const uint8_t *raw, uint16_t raw_len)
{
    anhui_parsed_cmd_t cmd = {0};
    if (!raw || raw_len < ANHUI_HEAD_TAIL_SIZE || raw[0] != 0x5A || raw[raw_len - 1] != 0xA5) {
        cmd.sta = ANHUI_PARSE_ERR_FRAME;
        return cmd;
    }

    uint16_t declared = raw[3];
    if (raw_len != (uint16_t)(declared + ANHUI_HEAD_TAIL_SIZE)) {
        cmd.sta = ANHUI_PARSE_ERR_FRAME;
        return cmd;
    }

    if (!anhui_cmd_supported(raw[2])) {
        cmd.sta = ANHUI_PARSE_ERR_CMD;
        return cmd;
    }
    cmd.cmd = (anhui_pcmd_t)raw[2];

    const uint8_t *data = &raw[4];
    cmd.sta              = ANHUI_PARSE_OK;

    switch (raw[2]) {
        case ANHUI_PCMD_CLEAR:
            /* 清屏：无数据（数据长必须为 0） */
            if (declared != 0)
                cmd.sta = ANHUI_PARSE_ERR_PARAM;
            break;

        case ANHUI_PCMD_PIXEL_ON:
        case ANHUI_PCMD_PIXEL_OFF:
            /* 显示点/关闭点：X(1B) + Y(1B)；
             * 坐标不在此校验（用户裁决 2026-09-04）：范围以模组驱动实际屏幕
             * 尺寸为准，越界坐标由执行层 dev_display_set_pixel 边界检查丢弃 */
            if (declared != 2) {
                cmd.sta = ANHUI_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.point.x = data[0];
            cmd.p.point.y = data[1];
            break;

        case ANHUI_PCMD_STATIC_TEXT:
            /* 静态显示：X(1B) + Y(1B) + 文本（GBK）；数据长 = 2 + 文本长度；
             * 坐标不在此校验（用户裁决 2026-09-04）：范围以模组驱动实际屏幕
             * 尺寸为准，起点越界由执行层 _anhui_exec_static_text 早退丢弃 */
            if (declared < 2) {
                cmd.sta = ANHUI_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.static_text.x        = data[0];
            cmd.p.static_text.y        = data[1];
            cmd.p.static_text.text     = &data[2];
            cmd.p.static_text.text_len = (uint16_t)(declared - 2U);
            break;

        case ANHUI_PCMD_SCROLL_1:
        case ANHUI_PCMD_SCROLL_2:
        case ANHUI_PCMD_SCROLL_3:
        case ANHUI_PCMD_SCROLL_4: {
            /* 动态显示：运动模式(1B) + 速度(1B ×2ms) + 停留时间(2B ×100ms) + 文本；
             * 动态停止 = 数据长 4 且四字节全 0（文档「运态停止」：5A 01 86 04 00 00 00 00 00 A5） */
            if (declared < 4) {
                cmd.sta = ANHUI_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.scroll.row     = (uint8_t)(raw[2] - ANHUI_PCMD_SCROLL_1);
            cmd.p.scroll.mode    = data[0];
            cmd.p.scroll.speed   = data[1];
            cmd.p.scroll.stay_ms = (uint16_t)(((uint16_t)data[2] << 8) | data[3]);
            if (declared == 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 0) {
                /* 停止帧：无文本，mode=0 语义 = 停止该行动态显示 */
                cmd.p.scroll.text     = nullptr;
                cmd.p.scroll.text_len = 0;
            } else if (declared == 4) {
                /* 防御（2026-09-04）：数据长恰为 4 但非停止帧（mode/speed/stay 占满
                 * 全部数据区）——无文本字节，&data[4] 会指向数据区之外（CRC 字节）。
                 * 置空不影响现状（动态显示留空），但防将来实现滚动时踩雷。 */
                cmd.p.scroll.text     = nullptr;
                cmd.p.scroll.text_len = 0;
            } else {
                cmd.p.scroll.text     = &data[4];
                cmd.p.scroll.text_len = (uint16_t)(declared - 4U);
            }
            break;
        }

        case ANHUI_PCMD_BRIGHTNESS:
            /* 设置亮度：亮度参数两字节（大端），最大 0x03E7(999) */
            if (declared != 2) {
                cmd.sta = ANHUI_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.brightness.value = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
            if (cmd.p.brightness.value > 999)
                cmd.sta = ANHUI_PARSE_ERR_PARAM;
            break;

        case ANHUI_PCMD_IO_CTRL:
            /* 设置通行灯/报警器：类型(01=通行灯,02=报警器) + 参数(00=灭/关,01=亮/开) */
            if (declared != 2) {
                cmd.sta = ANHUI_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.io_ctrl.type  = data[0];
            cmd.p.io_ctrl.param = data[1];
            if ((cmd.p.io_ctrl.type != 1 && cmd.p.io_ctrl.type != 2) || cmd.p.io_ctrl.param > 1)
                cmd.sta = ANHUI_PARSE_ERR_PARAM;
            break;

        case ANHUI_PCMD_VOICE:
            /* 播放声音：语音编号(2B ASCII 01~15) + 音量(1B ASCII '0'~'9') + 参数
             * （编号 01~14 = 模板句变长 ASCII 参数；编号 15 = 全部播报内容，GBK 自由文本） */
            if (declared < 3 || !anhui_is_ascii_digit(data[0]) || !anhui_is_ascii_digit(data[1])) {
                cmd.sta = ANHUI_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.voice.voice_no = (uint8_t)((data[0] - '0') * 10 + (data[1] - '0'));
            if (cmd.p.voice.voice_no < 1 || cmd.p.voice.voice_no > 15 ||
                data[2] < '0' || data[2] > '9') {
                cmd.sta = ANHUI_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.voice.volume     = (uint8_t)(data[2] - '0');
            cmd.p.voice.params     = &data[3];
            cmd.p.voice.params_len = (uint16_t)(declared - 3U);
            break;

        case ANHUI_PCMD_VOICE_REC_PLAY:
        case ANHUI_PCMD_VOICE_RECORD:
            /* 播放/录制语音：段号(2B ASCII 数字) */
            if (declared != 2 || !anhui_is_ascii_digit(data[0]) || !anhui_is_ascii_digit(data[1])) {
                cmd.sta = ANHUI_PARSE_ERR_PARAM;
                break;
            }
            cmd.p.voice_seg.seg_no = (uint8_t)((data[0] - '0') * 10 + (data[1] - '0'));
            break;

        default:
            cmd.sta = ANHUI_PARSE_ERR_CMD;
            break;
    }

    return cmd;
}
