/**
 * @file    app_ldi_etc_bridge.c
 * @brief   LDI → 四川 ETC 费显协议转义钩子实现（组帧 + RS485/RS232 双口发送）
 *
 * LDI 执行层在命令成功执行后调用（钩子声明见 app_ldi_etc_bridge.h）；
 * 本模块把业务内容翻译为四川 ETC 出站帧（0A 命令 载荷 0D）从双口同发。
 * 映射表、默认口径、EIDE 开关与已知限制见 doc/20_LDI到四川ETC协议转义/README.md。
 */

#include "app_ldi_etc_bridge.h"

#include "app_dispatch.h"

/* 四川 ETC 出站帧字节（帧结构见 app_sc_etc_proto_parse.c） */
#define ETC_FRAME_HEAD      (0x0AU)
#define ETC_FRAME_TAIL      (0x0DU)
#define ETC_MODE_STATIC     (0x00U)
#define ETC_CMD_LIGHT_RED   (0x36U)
#define ETC_CMD_LIGHT_GREEN (0x37U)
#define ETC_CMD_ALARM_ON    (0x38U) /* 黄闪开 */
#define ETC_CMD_ALARM_OFF   (0x39U) /* 黄闪关 */
#define ETC_CMD_HEARTBEAT   (0x08U) /* 周期心跳（现场口径，非协议文档 0x50） */
#define ETC_CLEAR_BYTE      (0x20U) /* 显示数据首字节 0x20 = 清屏 */
#define ETC_ROW_FULL        (0x00U)
#define ETC_ROW_LINE_MAX    (0x06U)

/* 数据上限：单行 24B / 全屏 145B（帧总长 ≤149B = 头 3 + 数据 + 尾 1） */
#define ETC_DATA_MAX_LINE (24U)
#define ETC_DATA_MAX_FULL (145U)
#define ETC_FRAME_MAX     (149U)

/** GBK 双字节首字节下限（≥ 0x81 视为与后一字节合成一个汉字） */
#define ETC_GBK_LEAD_MIN (0x81U)

/**
 * @brief  拷贝显示文本并按 ETC 约束过滤/截断。
 * @param  dst  目标缓冲（容量 ≥ max）
 * @param  text 源 GBK 文本
 * @param  len  源有效字节数
 * @param  max  允许写入的最大字节数（单行 24 / 全屏 145）
 * @return 实际写入字节数（可能为 0）
 * @note   0x0D 一律丢弃（与 ETC 帧尾定界符冲突）；
 *         截断点落在 GBK 双字节汉字中间时回退 1 字节（不劈开汉字）。
 */
static uint16_t _etc_copy_text(uint8_t *dst, const uint8_t *text, uint16_t len, uint16_t max)
{
    uint16_t n = 0;

    for (uint16_t i = 0; i < len;) {
        uint8_t c = text[i];

        if (c == ETC_FRAME_TAIL) { /* 0x0D 丢弃，避免破坏帧定界 */
            i++;
            continue;
        }

        if (c >= ETC_GBK_LEAD_MIN) {
            /* 双字节汉字：尾字节缺失或为 0x0D（异常序列）时只丢首字节 */
            if ((uint16_t)(i + 1U) >= len || text[i + 1U] == ETC_FRAME_TAIL) {
                i++;
                continue;
            }
            if ((uint16_t)(n + 2U) > max)
                break; /* 放不下整个汉字 → 截断 */
            dst[n++] = c;
            dst[n++] = text[i + 1U];
            i += 2U;
        } else {
            if ((uint16_t)(n + 1U) > max)
                break;
            dst[n++] = c;
            i++;
        }
    }

    return n;
}

/**
 * @brief  向 RS485 / RS232 双口发送同一份帧。
 * @param  data 帧字节（仅调用期间使用）
 * @param  len  帧长度
 * @note   通道未启动/未注册（指针为空）时静默跳过，发送返回值忽略。
 */
static void _etc_send(const uint8_t *data, uint16_t len)
{
    static const channel_id_t ids[] = {CH_ID_RS485, CH_ID_RS232};

    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        channel_t *ch = app_channel_get(ids[i]);
        if (ch == nullptr)
            continue;
        (void)channel_send(ch, (uint8_t *)data, len);
    }
}

/** @brief 发送 ETC 无载荷命令帧：`0A 命令 0D` */
static void _etc_send_cmd(uint8_t cmd)
{
    const uint8_t frame[3] = {ETC_FRAME_HEAD, cmd, ETC_FRAME_TAIL};

    _etc_send(frame, sizeof(frame));
}

void app_ldi_etc_display(uint8_t row, const uint8_t *text, uint16_t len)
{
    if (text == nullptr || len == 0U || row > ETC_ROW_LINE_MAX)
        return;

    uint16_t max = (row == ETC_ROW_FULL) ? ETC_DATA_MAX_FULL : ETC_DATA_MAX_LINE;

    uint8_t frame[ETC_FRAME_MAX]; /* ≤149B，栈上；文本就地过滤入帧 */
    uint16_t n = _etc_copy_text(&frame[3], text, len, max);
    if (n == 0U)
        return; /* 文本被过滤/截断为空 → 不发空帧 */

    frame[0]       = ETC_FRAME_HEAD;
    frame[1]       = ETC_MODE_STATIC;
    frame[2]       = row;
    frame[3U + n]  = ETC_FRAME_TAIL;

    _etc_send(frame, (uint16_t)(n + 4U));
}

void app_ldi_etc_clear(void)
{
    const uint8_t frame[5] = {
        ETC_FRAME_HEAD, ETC_MODE_STATIC, ETC_ROW_FULL, ETC_CLEAR_BYTE, ETC_FRAME_TAIL};

    _etc_send(frame, sizeof(frame));
}

void app_ldi_etc_lamp(uint8_t color)
{
    switch (color) {
        case 0x01: /* 绿 — 通行 */
            _etc_send_cmd(ETC_CMD_LIGHT_GREEN);
            break;
        case 0x02: /* 红 — 禁行 */
            _etc_send_cmd(ETC_CMD_LIGHT_RED);
            break;
        default: /* 03H 黄无 ETC 对应帧 → 不发 */
            break;
    }
}

void app_ldi_etc_alarm(bool on)
{
    _etc_send_cmd(on ? ETC_CMD_ALARM_ON : ETC_CMD_ALARM_OFF);
}

/** @brief 周期心跳：双口发送 `0A 08 0D`（由 LDI 定时任务每 3 秒调用一次） */
void app_ldi_etc_heartbeat(void)
{
    _etc_send_cmd(ETC_CMD_HEARTBEAT);
}
