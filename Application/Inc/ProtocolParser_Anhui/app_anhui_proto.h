/**
 * @file    app_anhui_proto.h
 * @brief   安徽费显协议公共定义
 *
 * 协议来源：2026-S304 费显通信协议（费显标准协议）。
 * 帧格式：5A + 屏号(01) + 命令 + 数据长 + 数据(≤255) + CRC + A5
 *  - CRC 字段协议文档注明「无检验，默认为 0」→ 不做校验；
 *  - 协议文档未定义任何应答 → 单向不回（对齐青海单向模式）；
 *  - 帧长 = 数据长 + 6，数据长 1 字节 → 整帧 ≤ 261 字节；
 *  - 文本编码：协议标注 ANSI（中文 Windows 语境即 GBK），渲染 FONT_ENC_GBK 直通。
 *
 * 权威文档核对（2026-09-08，2026-S304 费显通信协议.doc 第 8/9 节）：
 *  - 动态显示字段布局：运动模式(1B) + 速度(1B，×2MS) + 停留时间(2B，×100MS，
 *    大端——示例「00 10」=16×100ms=1.6s) + 文本区(ANSI)；
 *  - 数据长 = 运动模式至文本区（1+1+2+文本长）；
 *  - 命令 86/87/88/89 = 第 1/2/3/4 行；停止帧 = 数据长 4 且四字节全 0
 *    （文档第 9 节「运态停止」：5A 01 86 04 00 00 00 00 00 A5）；
 *  - 文档第 7 节静态显示四行行首 Y = 00/10/20/30（16 点阵行高）——按 2026-09-15
 *    终裁坐标直用像素：上位机发什么坐标就从什么像素起显示（00/10/20/30 → y=0/16/32/48）；
 *  - 0x85 坐标语义（2026-09-15 终裁，doc/13 §1.2）：x = data[0]、y = data[1]
 *    均直接作为像素坐标渲染（X 在前，与文档表头「（X、Y）」及 0x82/0x83 点
 *    命令同构）；固件不做行号换算——此前「row=Y/16→pixel_y=row×24」与
 *    「data[0]/data[1] 对调」两轮口径均已撤销（RTT 实测四帧同字节证伪，见
 *    doc/13 §1.2）；
 *  - 运动模式取值含义文档未定义（仅示例值 01）——1~4 方向映射为用户拍板约定，
 *    待联调校准（doc/13 §8）。
 */
#pragma once

#include <stdint.h>
#include "app_dispatch.h"

/* ---- 协议固定值 ---- */
#define ANHUI_SCREEN_ID      (0x01U) /* 屏号：本设备只处理 01，其他屏号整帧 SKIP 消费 */
#define ANHUI_HEAD_TAIL_SIZE (6U)    /* 5A+屏号+命令+数据长 + CRC+A5 = 6 */
#define ANHUI_PAYLOAD_MAX    (261U)  /* 整帧上限：数据长 1B 上限 255 → 6+255 */

/* ---- 命令字（二进制字节，枚举值与帧内原始值一致） ---- */
typedef enum {
    ANHUI_PCMD_CLEAR         = 0x81, /* 清屏 */
    ANHUI_PCMD_PIXEL_ON      = 0x82, /* 显示点 */
    ANHUI_PCMD_PIXEL_OFF     = 0x83, /* 关闭点 */
    ANHUI_PCMD_STATIC_TEXT   = 0x85, /* 静态显示 */
    ANHUI_PCMD_SCROLL_1      = 0x86, /* 动态显示 第 1 行 */
    ANHUI_PCMD_SCROLL_2      = 0x87, /* 动态显示 第 2 行 */
    ANHUI_PCMD_SCROLL_3      = 0x88, /* 动态显示 第 3 行 */
    ANHUI_PCMD_SCROLL_4      = 0x89, /* 动态显示 第 4 行 */
    ANHUI_PCMD_BRIGHTNESS    = 0x92, /* 设置亮度 */
    ANHUI_PCMD_IO_CTRL       = 0x94, /* 设置通行灯 / 报警器 */
    ANHUI_PCMD_VOICE         = 0x95, /* 播放声音 */
    ANHUI_PCMD_VOICE_REC_PLAY = 0x96, /* 播放录制语音 */
    ANHUI_PCMD_VOICE_RECORD  = 0x97, /* 录制语音 */
    ANHUI_PCMD_INVALID,
} anhui_pcmd_t;

/* ---- 解析状态 ---- */
typedef enum {
    ANHUI_PARSE_OK = 0,
    ANHUI_PARSE_ERR_FRAME,
    ANHUI_PARSE_ERR_CMD,
    ANHUI_PARSE_ERR_PARAM,
} anhui_parse_sta_t;

/* ---- 参数结构体 ---- */

/** 坐标点参数（显示点/关闭点）。各占一字节；协议文档 128×64 上限不作为
 *  标准（用户裁决 2026-09-04）——坐标范围以模组驱动实际屏幕尺寸为准，
 *  越界由执行层判界丢弃。 */
typedef struct {
    uint8_t x;
    uint8_t y;
} anhui_point_t;

/** 静态显示参数。数据长 = X/Y(2B) + 文本长度，文本为 GBK。
 *  坐标语义（2026-09-15 终裁，doc/13 §1.2）：帧内数据区第 1 字节 = X（水平
 *  像素）、第 2 字节 = Y（垂直像素），与文档表头「（X、Y）」X 在前一致、与
 *  0x82/0x83 点命令完全同构；x/y 均直接作为像素坐标渲染，固件不做任何行号
 *  换算——显示位置 (0,16) 就从 (0,16) 开始显示。此前「行号换算
 *  row=Y/16→pixel_y=row×24」与「data[0]/data[1] 对调」两轮口径均已撤销
 *  （用户裁决 2026-09-15，RTT 实测四帧同字节证伪行号假说，doc/13 §1.2）。 */
typedef struct {
    uint8_t x;
    uint8_t y;
    const uint8_t *text;
    uint16_t text_len;
} anhui_static_text_t;

/** 动态显示参数（0x86~0x89；已接入通用滚动模块 app_scroll，2026-09-07）。
 *  帧数据布局（2026-S304 权威 .doc 第 8 节，2026-09-08 核对）：
 *  运动模式(1B) + 速度(1B，单位 ×2ms) + 停留时间(2B，单位 ×100ms，大端——
 *  文档示例「00 10」= 16×100ms = 1.6s) + 文本(ANSI=GBK)；
 *  数据长 = 运动模式至文本区；停止帧 = 数据长 4 且四字节全 0（文档第 9 节）。
 *  运动模式取值含义文档未定义（仅示例 01）——mode 1~4 方向映射为用户拍板约定，
 *  待联调校准（doc/13 §8）。 */
typedef struct {
    uint8_t row;      /* 行号 0~3（命令 86=第 1 行 … 89=第 4 行，文档第 8 节） */
    uint8_t mode;     /* 运动模式：0=停止；1=从右往左 2=从左往右 3=从下往上 4=从上往下
                       * （1~4 映射为用户拍板约定，文档未定义取值表，待联调校准） */
    uint8_t speed;    /* 速度 ×2ms */
    uint16_t stay_ms; /* 停留时间原始值（×100ms 大端；v1 解析保存、执行忽略，待定） */
    const uint8_t *text;
    uint16_t text_len;
} anhui_scroll_t;

/** 亮度参数：两字节大端，最大 0x03E7(999)。 */
typedef struct {
    uint16_t value;
} anhui_brightness_t;

/** IO 控制参数（0x94）。type：01=通行灯、02=报警器；param：00=灭/关、01=亮/开。 */
typedef struct {
    uint8_t type;
    uint8_t param;
} anhui_io_ctrl_t;

/** 播放声音参数（0x95）。语音编号/音量均为 ASCII 数字串。
 *  编号 01~14：params 为模板句变长 ASCII 参数（按模板解析）；
 *  编号 15：params 为全部播报内容（GBK 自由文本），原样透传语音板。
 *  volume 字段解析保留（协议仍接受该字节），但执行层忽略——语音板不支持
 *  协议调音量（用户裁决 2026-09-04）。 */
typedef struct {
    uint8_t voice_no; /* 语音编号 01~15（两字节 ASCII 数字） */
    uint8_t volume;   /* 音量 0~9（一字节 ASCII 数字，共 10 级；执行层忽略） */
    const uint8_t *params;
    uint16_t params_len;
} anhui_voice_t;

/** 录制语音段号（0x96/0x97，两字节 ASCII 数字）。 */
typedef struct {
    uint8_t seg_no;
} anhui_voice_seg_t;

/* ---- 解析结果 ---- */
typedef struct {
    anhui_pcmd_t cmd;
    anhui_parse_sta_t sta;
    union {
        anhui_point_t point;
        anhui_static_text_t static_text;
        anhui_scroll_t scroll;
        anhui_brightness_t brightness;
        anhui_io_ctrl_t io_ctrl;
        anhui_voice_t voice;
        anhui_voice_seg_t voice_seg;
    } p;
} anhui_parsed_cmd_t;

/**
 * @brief  解析安徽协议原始帧。
 * @param  raw      原始帧数据。
 * @param  raw_len  原始帧长度。
 * @return 解析后的命令结构体。
 */
anhui_parsed_cmd_t anhui_parse_frame(const uint8_t *raw, uint16_t raw_len);

/**
 * @brief  执行安徽协议命令。
 * @param  ch   当前通道。
 * @param  cmd  解析后的命令结构体。
 */
void anhui_execute_cmd(channel_t *ch, const anhui_parsed_cmd_t *cmd);

/**
 * @brief  安徽协议处理任务入口。
 */
void anhui_proto_handle_task(void *argument);

/**
 * @brief  安徽协议初始化入口。
 */
void anhui_proto_init(void);
