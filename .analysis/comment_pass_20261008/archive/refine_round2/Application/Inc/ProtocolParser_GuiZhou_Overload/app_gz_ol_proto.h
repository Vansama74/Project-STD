/**
 * @file    app_gz_ol_proto.h
 * @brief   贵州治超屏协议（"TCLY" 帧族）公共定义
 *
 * 契约（改错静默致现场故障）：① 0x40/0x50/0x70 端口字段高字节在前 BE16；
 * ② 帧头 0x54 全槽唯一：无需 EIDE 互斥；③ 写 `port`：TCP 与 GZ_OL 专用口共读；
 * ④ 队列/控制块/帧缓冲置 CCMRAM（1136B）；⑤ 字面 `\n` 转 0x0A 须按 GBK 结构行走；
 * ⑥ 9600 8N1：现场 DIP1 必须置 OFF。
 * 详见 doc/14_贵州治超屏协议/README.md §1、§3、§4、§6、§13
 */
#pragma once

#include <stdint.h>

#include "app_dispatch.h"
#include "app_render.h" /* font_size_t / font_type_t / display_color_t（渲染参数类型） */

/* ---- 帧结构常量 ---- */
#define GZ_OL_GUIDE0 (0x54U) /**< 'T' */
#define GZ_OL_GUIDE1 (0x43U) /**< 'C' */
#define GZ_OL_GUIDE2 (0x4CU) /**< 'L' */
#define GZ_OL_GUIDE3 (0x59U) /**< 'Y' */
#define GZ_OL_GUIDE_LEN (4U)  /**< 引导串长度（ASCII "TCLY"） */
#define GZ_OL_HEAD_LEN (16U)    /**< 引导4 + 序号4 + 长度2 + 保留2 + 命令4 */
#define GZ_OL_LEN_OFFSET (8U)   /**< 长度字段偏移（2B 小端，整帧长） */
#define GZ_OL_CMD_OFFSET (12U)  /**< 命令字偏移（4B 容器，低字节有效） */
#define GZ_OL_DATA_OFFSET (16U) /**< 基本数据包偏移 */
#define GZ_OL_TAIL (0x00U)      /**< 结束符 */

/** 帧长下界：17 = 16 帧头 + 0 载荷 + 1 尾（0x10/0x30/0x50/0x60 最小帧） */
#define GZ_OL_FRAME_LEN_MIN (17U)
/** 入队/解析上限 256B（实测合法最大 158B 的 1.6× 余量）；更长结构合法帧由 probe 整帧 SKIP */
#define GZ_OL_FRAME_LEN_MAX (256U)
#define GZ_OL_PAYLOAD_MAX (GZ_OL_FRAME_LEN_MAX)                  /**< 单帧载荷上限（同帧长上限） */
#define GZ_OL_QUEUE_DEPTH (3U)                                   /**< 帧队列深度（四 mask 共用） */
#define GZ_OL_MSG_SIZE (sizeof(frame_msg_t) + GZ_OL_PAYLOAD_MAX) /* 264 */

_Static_assert(GZ_OL_PAYLOAD_MAX <= RB_SIZE_RS485, "GZ_OL frame must fit RS485 RB");
_Static_assert(GZ_OL_PAYLOAD_MAX <= RB_SIZE_RS232, "GZ_OL frame must fit RS232 RB");
_Static_assert(GZ_OL_PAYLOAD_MAX <= RB_SIZE_RJ45, "GZ_OL frame must fit RJ45 RB (UDP 10011)");

/* ---- 0x20 立即显示载荷布局 ---- */
#define GZ_OL_DISPLAY_PREFIX_LEN (17U) /**< X2 Y2 屏宽2 屏高2 字体名4 字号2 颜色3 */
#define GZ_OL_FONT_NAME_LEN (4U)       /**< 字体名字段固定 4 字节（GBK 两字） */

/** 搜索/修改 IP 应答载荷长度：ip4 + mask4 + gw4 + port2（BE16） */
#define GZ_OL_NET_PAYLOAD_LEN (14U)

/* ---- 命令字（仅低字节有效）---- */
typedef enum {
    GZ_OL_PCMD_FAULT_QUERY = 0x10, /**< 获取故障信息（应答 2B 状态 00 00） */
    GZ_OL_PCMD_DISPLAY     = 0x20, /**< 立即显示 */
    GZ_OL_PCMD_CLEAR       = 0x30, /**< 清屏（载荷长度不限） */
    GZ_OL_PCMD_SET_IP      = 0x40, /**< 修改 IP（应答 0x50 帧后软复位） */
    GZ_OL_PCMD_SET_IP_ACK  = 0x50, /**< 修改 IP 返回／兼容源固件的搜索触发 */
    GZ_OL_PCMD_SEARCH      = 0x60, /**< 搜索设备（应答 0x70 帧） */
    GZ_OL_PCMD_SEARCH_ACK  = 0x70, /**< 搜索设备返回信息（仅出站） */
    GZ_OL_PCMD_FULL_COLOR  = 0x80, /**< 创迪扩展全屏纯色 */
    GZ_OL_PCMD_INVALID     = 0xFF, /**< 非法命令字 */
} gz_ol_pcmd_t;

/** 解析状态（非 OK 时执行层不动作；ERR_CMD 静默丢弃，ERR_PARAM 整帧丢弃） */
typedef enum {
    GZ_OL_PARSE_OK = 0,
    GZ_OL_PARSE_ERR_FRAME, /**< 引导串/长度字段/结束符不符 */
    GZ_OL_PARSE_ERR_CMD,   /**< 命令字非法（静默丢弃） */
    GZ_OL_PARSE_ERR_PARAM, /**< 参数长度或取值非法（整帧丢弃） */
} gz_ol_parse_sta_t;

/** 0x20 立即显示参数 */
typedef struct {
    uint16_t x;             /**< 显示起点 X（像素，左上为原点；≥屏宽时执行层钳 0） */
    uint16_t y;             /**< 显示起点 Y（像素；≥屏高时执行层钳 0） */
    const char *text;       /**< 显示内容（GBK；指向任务帧缓冲私有拷贝，可原地转换） */
    uint16_t text_len;      /**< 显示内容字节数 */
    font_size_t font_size;  /**< 字号（仅 16/24/32，其余整帧丢弃） */
    font_type_t font_type;  /**< 字型（未命中回退宋体 FONT_ST） */
    display_color_t color;  /**< 颜色（RGB→8 色映射，见 doc/14 §1 载荷表颜色行） */
} gz_ol_display_t;

/** 0x40 修改 IP 参数（端口字段 BE16，见契约 ①） */
typedef struct {
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gw[4];
    uint16_t port; /**< 写 `net_cfg.port`（契约 ③）；10028 发 `27 2C`（§13） */
} gz_ol_setip_t;

/** 0x80 全屏纯色参数（RGB 三字节） */
typedef struct {
    uint8_t r, g, b;
} gz_ol_rgb_t;

/** 解析结果 */
typedef struct {
    gz_ol_pcmd_t cmd;
    gz_ol_parse_sta_t sta;
    uint16_t data_len;
    const uint8_t *data; /**< 基本数据包（= &raw[GZ_OL_DATA_OFFSET]） */
    union {
        gz_ol_display_t display;
        gz_ol_setip_t setip;
        gz_ol_rgb_t rgb;
    } p;
} gz_ol_parsed_cmd_t;

/**
 * @brief  解析 "TCLY" 帧（纯函数：只读入参、无副作用、不阻塞）。
 * @param  raw      原始帧（probe 认领的完整帧）。
 * @param  raw_len  帧长（必须等于帧内长度字段）。
 * @return 解析结果——`sta` 标识结果（OK/FRAME/CMD/PARAM），非 OK 时执行层不动作。
 */
gz_ol_parsed_cmd_t gz_ol_parse_frame(const uint8_t *raw, uint16_t raw_len);

/**
 * @brief  执行解析后的命令（应答一律单播回源通道；协议未定义错误应答）。
 * @param  ch   帧来源通道。
 * @param  cmd  解析结果（`sta != GZ_OL_PARSE_OK` 时直接返回）。
 */
void gz_ol_execute_cmd(channel_t *ch, const gz_ol_parsed_cmd_t *cmd);

/**
 * @brief  恢复光敏自动调光（复现源固件「收到数据即开启」语义）。
 * @note   由帧处理任务每帧调用；0x80 的挂起副作用在下帧被解除，未挂起时空操作（§1）。
 */
void gz_ol_resume_auto_dim(void);

/**
 * @brief  贵州治超屏协议帧处理任务入口（帧队列 → 解析 → 执行）。
 * @param  argument  未使用。
 */
void gz_ol_proto_handle_task(void *argument);

/** @brief 协议注册入口（`sw_app_initcall` 自注册；四通道四 mask 共用一静态队列，§3）。 */
void gz_ol_proto_init(void);
