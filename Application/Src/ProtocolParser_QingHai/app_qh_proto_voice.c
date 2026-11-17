/**
 * @file    app_qh_proto_voice.c
 * @brief   青海治超站费显协议（V1.0）语音段播报
 *
 * V1.0 命令 '5' 的参数是**语音段号序列**（原文对应 0.wav ~ 45.wav），
 * 而本板语音板是 RS232 TTS（`dev_rs232_voice_play` 接收 GBK 文本），无法按 wav 编号取段。
 * 故此处把段号映射为等价中文文本，按顺序拼接成一句后整体 TTS 播报。
 *
 * 与协议示例自洽性校验：`7B 35 04 1A 0F 0A 0E 7D` → 段 26/15/10/14
 * → 「您好」+「请交费」+「十」+「元」= 「您好请交费十元」，与原文示例一致。
 */

#include "app_qh_proto_voice.h"

#include <stdbool.h>
#include <string.h>

#include "dev_rs232_voice.h"
#include "text_cvt.h"

/* 语音段 0~45 的等价文本（UTF-8 字面量，播报前运行时转 GBK）。 */
static const char *const s_seg_text[] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",   /* 0~9  数字 */
    "十", "百", "千", "万", "元",                        /* 10~14 */
    "请交费", "收费", "点", "吨", "超重",                /* 15~19 */
    "储值卡", "公交专线车", "公务车", "记账卡", "军警车", /* 20~24 */
    "免费车", "您好", "系统正在加电自检",                /* 25~27 */
    "谢谢，雨雪雾天气，行车请注意安全",                  /* 28 */
    "谢谢", "谢谢合作", "谢谢合作祝您一路平安",           /* 29~31 */
    "谢谢合作走好", "谢谢走好", "型车", "型货车", "型客车", /* 32~36 */
    "再见", "正常", "总重", "余额",                      /* 37~40 */
    "您已超载，请卸载",                                  /* 41 */
    "合法装载，欢迎驶入",                                /* 42 */
    "违法超限，拒绝驶入",                                /* 43 */
    "您已被列入黑名单，拒绝驶入",                        /* 44 */
    "合法大件运输车辆，欢迎驶入",                        /* 45 */
};

#define QH_SEG_MAX   ((uint16_t)(sizeof(s_seg_text) / sizeof(s_seg_text[0])))
#define QH_TTS_BUF   128U /* UTF-8 拼接缓冲；超长截断（TTS 文本远短于该值） */

/**
 * @brief 播报语音段组合（V1.0 命令 '5'）。
 * @param segs     语音段号数组，每字节一个段号。
 * @param seg_cnt  段号个数。
 */
void qh_voice_segments(const uint8_t *segs, uint16_t seg_cnt)
{
    if (!segs || seg_cnt == 0U) return;

    char text[QH_TTS_BUF];
    uint16_t used = 0;

    for (uint16_t i = 0; i < seg_cnt; i++) {
        uint8_t seg = segs[i];
        if (seg >= QH_SEG_MAX) continue; /* 未定义段号：跳过，不影响整句 */

        const char *piece = s_seg_text[seg];
        uint16_t plen     = (uint16_t)strlen(piece);
        if (used + plen >= QH_TTS_BUF) break; /* 缓冲满：截断 */
        memcpy(&text[used], piece, plen);
        used = (uint16_t)(used + plen);
    }

    if (used == 0U) return;

    /* 语音板要求 GBK：UTF-8 源文运行时转换后播报 */
    uint8_t gbk[QH_TTS_BUF];
    uint32_t gbk_len = sizeof(gbk);
    UTF8ToGBK(text, used, (char *)gbk, &gbk_len);
    if (gbk_len > 0U) {
        dev_rs232_voice_play(gbk, (uint16_t)gbk_len);
    }
}