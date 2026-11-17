/**
 * @file    app_anhui_proto_voice.c
 * @brief   安徽费显协议语音播报（0x95 播放声音）
 *
 * 语音编号 01~14 按协议文档模板句 + 参数构造 UTF-8 文本，运行时 UTF8ToGBK
 * 转 GBK 经 dev_rs232_voice_play（TTS 语音板，旁路 USART6）。
 * 语音编号 15 为自由文本播报：参数区即全部播报内容（GBK），原样透传语音板。
 *
 * 金额读数按文档示例转换：「1030.42」→「一千零三十点四二元」，
 * 小数部分「00」不播（「1030.00」→「一千零三十元」）。
 * 车型 0x30（ASCII '0'）不播车型短语；用户类型/卡片类型按文档编码表取名。
 */

#include "app_anhui_proto_voice.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "dev_rs232_voice.h"
#include "text_cvt.h"

/* ---- 读数表（UTF-8 字面量） ---- */
static const char *const s_digit_char[] = {"零", "一", "二", "三", "四", "五", "六", "七", "八", "九"};
static const char *const s_unit_char[]  = {"千", "百", "十", ""};

/* 用户类型（1 字节 ASCII：'0'~'4'），文档：0x30 正常车 / 0x31 免费车 /
 * 0x32 军警车 / 0x33 公务车 / 0x34 公交专线车 */
static const char *const s_user_type[] = {"正常车", "免费车", "军警车", "公务车", "公交专线车"};

/* 卡片类型（1 字节 ASCII：'0'~'1'），文档：0x30 储值卡 / 0x31 记账卡 */
static const char *const s_card_type[] = {"储值卡", "记账卡"};

/** 向 UTF-8 输出缓冲追加字符串（容量满即停止，返回新写位置）。 */
static uint16_t _anhui_append(char *out, uint16_t cap, uint16_t pos, const char *s)
{
    uint16_t len = (uint16_t)strlen(s);
    if (pos + len >= cap)
        return pos;
    memcpy(out + pos, s, len);
    return (uint16_t)(pos + len);
}

/** 向 UTF-8 输出缓冲追加单个中文数字字符。 */
static uint16_t _anhui_append_digit(char *out, uint16_t cap, uint16_t pos, uint8_t d)
{
    if (d > 9)
        return pos;
    return _anhui_append(out, cap, pos, s_digit_char[d]);
}

/**
 * @brief  整数中文读数（1~4 位，高位在前）。
 * @param  digits  数字串（'0'~'9' ASCII）。
 * @param  len     位数（1~4）。
 * @param  out     输出 UTF-8 缓冲。
 * @param  cap     缓冲容量。
 * @param  pos     已写入位置。
 * @return 新写位置。
 * @note   规则：前导零不读；中间零读「零」一次（「1030」→「一千零三十」）；
 *         十位起首的 1 不读「一」（「10」→「十」）；全 0 读「零」。
 */
static uint16_t _anhui_num_reading(const uint8_t *digits, uint8_t len, char *out,
                                   uint16_t cap, uint16_t pos)
{
    bool started = false, zero_pending = false;
    uint8_t unit_idx = (uint8_t)(4U - len); /* 对齐千位起（len=4 → 千/百/十/个） */

    for (uint8_t i = 0; i < len; i++) {
        uint8_t d = (uint8_t)(digits[i] - '0');
        if (d == 0) {
            if (started)
                zero_pending = true;
            continue;
        }
        if (zero_pending) {
            pos = _anhui_append(out, cap, pos, "零");
            zero_pending = false;
        }
        bool leading_ten = (i == 0 && d == 1 && len == 2); /* 「10」读「十」不读「一十」 */
        if (!leading_ten)
            pos = _anhui_append_digit(out, cap, pos, d);
        pos = _anhui_append(out, cap, pos, s_unit_char[unit_idx + i]);
        started = true;
    }

    if (!started)
        pos = _anhui_append(out, cap, pos, "零"); /* 全 0 */
    return pos;
}

/**
 * @brief  金额中文读数（7 字节 ASCII「千百十个.角分」）。
 * @param  amount  金额串（如 "1030.42"）。
 * @param  out     输出 UTF-8 缓冲。
 * @param  cap     缓冲容量。
 * @param  pos     已写入位置。
 * @return 新写位置（格式非法时不追加，原样返回）。
 * @note   文档示例：「1030.42」→「一千零三十点四二元」；
 *         小数部分「00」不播；「.40」→「点四」、「.02」→「点零二」。
 */
static uint16_t _anhui_amount_reading(const uint8_t *amount, char *out, uint16_t cap, uint16_t pos)
{
    /* 校验格式：4 数字 + '.' + 2 数字 */
    for (uint8_t i = 0; i < 7; i++) {
        if (i == 4) {
            if (amount[i] != '.')
                return pos;
        } else if (amount[i] < '0' || amount[i] > '9') {
            return pos;
        }
    }

    pos = _anhui_num_reading(amount, 4, out, cap, pos);

    uint8_t jiao = (uint8_t)(amount[5] - '0');
    uint8_t fen  = (uint8_t)(amount[6] - '0');
    if (jiao != 0 || fen != 0) {
        pos = _anhui_append(out, cap, pos, "点");
        if (jiao == 0) {
            pos = _anhui_append(out, cap, pos, "零");
        } else {
            pos = _anhui_append_digit(out, cap, pos, jiao);
        }
        if (fen != 0)
            pos = _anhui_append_digit(out, cap, pos, fen);
    }

    pos = _anhui_append(out, cap, pos, "元");
    return pos;
}

/** 校验是否为 ASCII 数字串（全部 '0'~'9'，长度 len）。 */
static bool _anhui_is_digits(const uint8_t *p, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        if (p[i] < '0' || p[i] > '9')
            return false;
    }
    return true;
}

/** 追加「X型车」短语（车型 0x30 不播车型）。 */
static uint16_t _anhui_append_vehicle(char *out, uint16_t cap, uint16_t pos, uint8_t vehicle)
{
    if (vehicle == '0')
        return pos;
    if (vehicle < '1' || vehicle > '9')
        return pos;
    pos = _anhui_append_digit(out, cap, pos, (uint8_t)(vehicle - '0'));
    pos = _anhui_append(out, cap, pos, "型车，");
    return pos;
}

/** 追加「X型」短语（车型 0x30 不播车型；用于模板 5「几型,」）。 */
static uint16_t _anhui_append_vehicle_short(char *out, uint16_t cap, uint16_t pos, uint8_t vehicle)
{
    if (vehicle == '0')
        return pos;
    if (vehicle < '1' || vehicle > '9')
        return pos;
    pos = _anhui_append_digit(out, cap, pos, (uint8_t)(vehicle - '0'));
    pos = _anhui_append(out, cap, pos, "型，");
    return pos;
}

/**
 * @brief  按语音编号构造模板句并播报。
 * @param  voice_no    语音编号 01~15。
 * @param  params      参数区（编号 01~14：变长 ASCII 数字串；编号 15：GBK 自由文本）。
 * @param  params_len  参数区长度。
 * @note   参数长度/格式不符时静默放弃播报。
 */
void anhui_voice_play(uint8_t voice_no, const uint8_t *params, uint16_t params_len)
{
    char text[160];
    uint16_t pos   = 0;
    const uint16_t cap = (uint16_t)sizeof(text);

    if (voice_no == 15) {
        /* 编号 15（自由文本播报）：参数区即全部播报内容（协议 ANSI = GBK），
         * 不套模板句，原样透传 TTS 语音板（与 CQ voice1、MTC '7' 自由文本直送同路径）。
         * 超过语音板单帧文本上限 DEV_RS232_VOICE_MAX_TEXT(200B) 截断；
         * 截断处可能切开 GBK 双字节字符（尾字符读错）——与 CQ voice1 同限制，
         * 待上位机确认最长文本口径。 */
        if (!params || params_len == 0)
            return;
        uint16_t len = params_len;
        if (len > DEV_RS232_VOICE_MAX_TEXT)
            len = DEV_RS232_VOICE_MAX_TEXT;
        dev_rs232_voice_play(params, len);
        return;
    }

    if (!params || params_len == 0) {
        /* 无参数模板（8~14）也允许参数区为空；其余模板要求参数 */
        switch (voice_no) {
            case 8:  pos = _anhui_append(text, cap, pos, "您好，免费车"); break;
            case 9:  pos = _anhui_append(text, cap, pos, "您好"); break;
            case 10: pos = _anhui_append(text, cap, pos, "谢谢"); break;
            case 11: pos = _anhui_append(text, cap, pos, "谢谢合作，走好"); break;
            case 12: pos = _anhui_append(text, cap, pos, "再见"); break;
            case 13: pos = _anhui_append(text, cap, pos, "谢谢，祝您一路平安"); break;
            case 14: pos = _anhui_append(text, cap, pos, "谢谢，雨雪雾天行车注意安全"); break;
            default: return; /* 1~7 必须有参数 */
        }
    } else {
        switch (voice_no) {
            case 1: /* 你好，几型车，您应交多少元 — 车型(1B) + 金额(7B) */
                if (params_len < 8)
                    return;
                pos = _anhui_append(text, cap, pos, "你好，");
                pos = _anhui_append_vehicle(text, cap, pos, params[0]);
                pos = _anhui_append(text, cap, pos, "您应交");
                pos = _anhui_amount_reading(&params[1], text, cap, pos);
                break;

            case 2: /* 你好，几型车，总重多少吨，您应交多少元 — 车型(1B)+重量(4B)+金额(7B) */
                if (params_len < 12)
                    return;
                pos = _anhui_append(text, cap, pos, "你好，");
                pos = _anhui_append_vehicle(text, cap, pos, params[0]);
                pos = _anhui_append(text, cap, pos, "总重");
                if (_anhui_is_digits(&params[1], 4))
                    pos = _anhui_num_reading(&params[1], 4, text, cap, pos);
                pos = _anhui_append(text, cap, pos, "吨，您应交");
                pos = _anhui_amount_reading(&params[5], text, cap, pos);
                break;

            case 3: /* 总重多少吨，超重多少吨 — 重量(4B) + 超重(4B) */
                if (params_len < 8)
                    return;
                pos = _anhui_append(text, cap, pos, "总重");
                if (_anhui_is_digits(params, 4))
                    pos = _anhui_num_reading(params, 4, text, cap, pos);
                pos = _anhui_append(text, cap, pos, "吨，超重");
                if (_anhui_is_digits(&params[4], 4))
                    pos = _anhui_num_reading(&params[4], 4, text, cap, pos);
                pos = _anhui_append(text, cap, pos, "吨");
                break;

            case 4: /* 几型车，收费多少元 — 车型(1B) + 金额(7B) */
                if (params_len < 8)
                    return;
                pos = _anhui_append_vehicle(text, cap, pos, params[0]);
                pos = _anhui_append(text, cap, pos, "收费");
                pos = _anhui_amount_reading(&params[1], text, cap, pos);
                break;

            case 5: /* 某某（用户类型），几型,收费多少元 — 用户类型(1B)+车型(1B)+金额(7B) */
                if (params_len < 9 || params[0] < '0' || params[0] > '4')
                    return;
                pos = _anhui_append(text, cap, pos, s_user_type[params[0] - '0']);
                pos = _anhui_append(text, cap, pos, "，");
                pos = _anhui_append_vehicle_short(text, cap, pos, params[1]);
                pos = _anhui_append(text, cap, pos, "收费");
                pos = _anhui_amount_reading(&params[2], text, cap, pos);
                break;

            case 6: /* 某某卡，某某（用户类型），收费多少元 — 卡片类型(1B)+用户类型(1B)+金额(7B) */
                if (params_len < 9 || params[0] < '0' || params[0] > '1' ||
                    params[1] < '0' || params[1] > '4')
                    return;
                pos = _anhui_append(text, cap, pos, s_card_type[params[0] - '0']);
                pos = _anhui_append(text, cap, pos, "，");
                pos = _anhui_append(text, cap, pos, s_user_type[params[1] - '0']);
                pos = _anhui_append(text, cap, pos, "，收费");
                pos = _anhui_amount_reading(&params[2], text, cap, pos);
                break;

            case 7: /* 余额多少元 — 金额(7B) */
                if (params_len < 7)
                    return;
                pos = _anhui_append(text, cap, pos, "余额");
                pos = _anhui_amount_reading(params, text, cap, pos);
                break;

            case 8:  pos = _anhui_append(text, cap, pos, "您好，免费车"); break;
            case 9:  pos = _anhui_append(text, cap, pos, "您好"); break;
            case 10: pos = _anhui_append(text, cap, pos, "谢谢"); break;
            case 11: pos = _anhui_append(text, cap, pos, "谢谢合作，走好"); break;
            case 12: pos = _anhui_append(text, cap, pos, "再见"); break;
            case 13: pos = _anhui_append(text, cap, pos, "谢谢，祝您一路平安"); break;
            case 14: pos = _anhui_append(text, cap, pos, "谢谢，雨雪雾天行车注意安全"); break;

            default:
                return;
        }
    }

    /* 语音板要求 GBK：UTF-8 文本运行时转换后播报 */
    uint8_t gbk[DEV_RS232_VOICE_MAX_TEXT];
    uint32_t gbk_len = sizeof(gbk);
    UTF8ToGBK(text, pos, (char *)gbk, &gbk_len);
    dev_rs232_voice_play(gbk, (uint16_t)gbk_len);
}
