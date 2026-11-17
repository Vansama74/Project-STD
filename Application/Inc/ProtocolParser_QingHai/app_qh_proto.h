/**
 * @file    app_qh_proto.h
 * @brief   青海治超站费显协议（V1.0）公共定义
 *
 * 协议基线：《青海常规费显协议-治超站费显通信协议 V1.0》（2025-12-02 创迪添加）
 *
 * 帧格式（字段 1~5）：
 *   '{' (0x7B) + 命令字 (1B) + 参数长度 (1B，二进制字节数) + 参数数据 (变长) + '}' (0x7D)
 *
 * 命令字集合（本文件即 V1.0 全集，不含 2019 版高速协议的 '6'~'9' / 'A' / 'B'）：
 *   '1'  0x31  主机查询
 *   '2'  0x32  点阵显示
 *   '3'  0x33  全屏清除
 *   '4'  0x34  外设控制
 *   '5'  0x35  语音段组合
 *   0x3A       IP / 子网掩码 / 默认网关
 *   0x3B       显示字体大小
 *   0x3C       显示亮度
 *
 * 传输通道：RS485 / RS232（9600-8-N-1）+ 网口 UDP（工程约定 10011，非协议原文 2929）。
 */
#pragma once

#include <stdint.h>
#include "app_dispatch.h"

/**
 * @brief 青海协议 V1.0 支持的命令类型。
 */
typedef enum {
    QH_PCMD_HOST_QUERY = 0, /*!< '1' 主机查询 */
    QH_PCMD_MATRIX_SHOW,    /*!< '2' 点阵显示 */
    QH_PCMD_CLEAR,          /*!< '3' 全屏清除 */
    QH_PCMD_PERIPHERAL,     /*!< '4' 外设控制 */
    QH_PCMD_VOICE_SEG,      /*!< '5' 语音段组合 */
    QH_PCMD_SET_IP,         /*!< 0x3A IP / 掩码 / 网关 */
    QH_PCMD_FONT_SIZE,      /*!< 0x3B 显示字体大小 */
    QH_PCMD_BRIGHTNESS,     /*!< 0x3C 显示亮度 */
    QH_PCMD_INVALID,
} qh_pcmd_t;

/**
 * @brief 青海协议帧解析状态。
 */
typedef enum {
    QH_PARSE_OK = 0,
    QH_PARSE_ERR_FRAME, /*!< 帧头/帧尾/长度不自洽 */
    QH_PARSE_ERR_CMD,   /*!< 命令字不在 V1.0 集合内 */
    QH_PARSE_ERR_PARAM, /*!< 参数长度或取值越界 */
} qh_parse_sta_t;

/**
 * @brief '2' 点阵显示参数。
 * @note  协议：x0 为行号 '0'~'4'（0x30~0x34），'0' = 全屏自动换行，'1'~'4' = 第 1~4 行；
 *        x1 为颜色，单字节二进制：0 = 红 / 1 = 绿 / 2 = 黄（协议原文未标 ASCII）。
 *        `row` 已归一化为 0 = 全屏、1~4 = 行号。
 */
typedef struct {
    uint8_t row; /*!< 0 = 全屏自动换行；1~4 = 行号 */
    uint8_t color;
    const uint8_t *text;
    uint16_t text_len;
} qh_matrix_t;

/**
 * @brief '4' 通行灯 / 报警控制参数。
 * @note  帧 `7B 34 01 x0 7D`，`x0` 为**单字节十六进制**取值：
 *          0x01 通行灯显示红叉、0x02 通行灯显示绿箭、
 *          0x03 报警开、0x04 关闭报警。
 *        原文把取值写成带引号的 '01'/'02'，实为 hex 便于阅读，不是两个 ASCII 字符。
 */
typedef struct { uint8_t ctrl; } qh_periph_t;

/**
 * @brief '5' 语音段组合参数。
 * @note  `segs` 每字节一个语音段号（0~45），按顺序拼接成一句后整体播报。
 */
typedef struct {
    const uint8_t *segs;
    uint16_t seg_cnt;
} qh_voice_seg_t;

/**
 * @brief 0x3A IP 配置参数。
 */
typedef struct {
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gw[4];
} qh_ip_cfg_t;

/**
 * @brief 0x3B 字体大小参数。
 * @note  0 = 自适应 / 1 = 16 点阵 / 2 = 24 点阵 / 3 = 32 点阵。
 */
typedef struct { uint8_t size; } qh_font_t;

/**
 * @brief 0x3C 亮度参数。
 * @note  0 = 设备自动调光 / 1 = 最暗 / 8 = 最亮。
 */
typedef struct { uint8_t level; } qh_brightness_t;

/**
 * @brief 青海协议解析结果（清单）。
 * @note  `cmd_char` 保留原始命令字，供执行器组装应答时回显。
 */
typedef struct {
    qh_pcmd_t cmd;
    qh_parse_sta_t sta;
    uint8_t cmd_char;
    uint16_t data_len;
    const uint8_t *data;
    union {
        qh_matrix_t matrix;
        qh_periph_t peripheral;
        qh_voice_seg_t voice;
        qh_ip_cfg_t ip_cfg;
        qh_font_t font;
        qh_brightness_t brightness;
    } p;
} qh_parsed_cmd_t;

/**
 * @brief  解析青海协议原始帧。
 * @param  raw      原始帧数据。
 * @param  raw_len  原始帧长度。
 * @return 解析后的命令结构体（`sta` 标识解析结果）。
 */
qh_parsed_cmd_t qh_parse_frame(const uint8_t *raw, uint16_t raw_len);

/**
 * @brief  执行青海协议命令（含应答），执行结果由内部统一回复。
 * @param  ch   当前通道。
 * @param  cmd  解析后的命令结构体。
 */
void qh_execute_cmd(channel_t *ch, const qh_parsed_cmd_t *cmd);

/**
 * @brief  青海协议处理任务入口。
 * @param  argument  任务参数，当前未使用。
 */
void qh_proto_handle_task(void *argument);

/**
 * @brief  青海协议初始化入口。
 */
void qh_proto_init(void);