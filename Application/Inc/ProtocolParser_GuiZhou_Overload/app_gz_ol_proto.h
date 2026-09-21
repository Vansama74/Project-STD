/**
 * @file    app_gz_ol_proto.h
 * @brief   贵州治超屏协议（"TCLY" 帧族，源项目 9K23881580）公共定义
 *
 * 帧格式（4 字节引导串 + 长度字段定界 + 尾部 0x00 结束符，**无校验**）：
 *   [0..3]   引导串 54 43 4C 59（ASCII "TCLY"）
 *   [4..7]   包序号（文档规定 00 00 00 00；本实现不读不校验，与源固件一致）
 *   [8..9]   数据包长度：2 字节小端 = **整帧总长**（含引导串与结束符）
 *   [10..11] 保留位（文档规定 00 00；不读不校验）
 *   [12..15] 命令字：4 字节容器，**仅低字节有效**（文档「不足四个字节后面补 0x00」）
 *   [16..N]  基本数据包（变长，含义随命令字）
 *   [N+1]    结束符 0x00
 * 帧总长 = 17 + 载荷字节数。
 *
 * 命令字（源固件 getCmdNo 实现 + 文档）：
 *   0x10 获取故障信息 → 应答 2 字节状态（本实现 00 00 = 无故障，见 README Q2）
 *   0x20 立即显示：X(2 LE) Y(2 LE) 屏宽(2) 屏高(2) 字体名(4) 字号(2) 颜色(3) 文本(变长)
 *        字号（16/24/32）大于可用高度时**按可见部分裁剪绘制**（2026-09-14 用户裁决：
 *        `dev_display_draw_bitmap()` 越界由整块丢弃改为按屏幕交集裁剪；渲染层不再因
 *        「行框越出区域」整行跳过）——单模组 32×16 台架上 24/32 点阵露出上部；
 *        224×64 整机字形全在屏内、表现零变化。屏宽决定自动换行宽度。
 *   0x30 清屏（全屏清黑）
 *   0x40 修改 IP：ip(4) mask(4) gw(4) port(2 **高字节在前 BE16**) → 应答 0x50 帧后软复位
 *        **port 字节序（2026-09-15 用户裁决，doc/14 §13.5.1/§14）**：本字段**有意偏离**
 *        源固件/协议文档的低字节在前口径，改为**高字节在前**——写 10028 发 `27 2C`；
 *        `2C 27` 则解析为 11303（与改前互换）。**仅此一个字段**：帧长、0x20 的 X/Y/屏宽/屏高
 *        仍为小端 LE16。发送规则 = `[port >> 8, port & 0xFF]`，`0x50`/`0x70` 应答同序。
 *        port 语义（2026-09-14 用户裁决，doc/14 §13）：写入 Sector1 `net_cfg.port`
 *        = GZ_OL 专用业务口（CH_ID_UDP_GZOL）监听口 + TCP 业务口（同号不同协议栈），
 *        重启后生效；空/0 回退 9528，与 10011/CQ 业务口同号时该实例跳过绑定（RTT 告警）。
 *   0x50 修改 IP 返回信息（文档）／源固件把它当"搜索设备"触发 → 本实现兼容：入站
 *        0x50 与 0x60 均触发 0x70 应答（单播回源），见 README §待裁决 Q3
 *   0x60 搜索设备 → 应答 0x70 帧（ip4+mask4+gw4+port2 **BE16**，共 14B 载荷）
 *   0x70 搜索设备返回信息（仅出站应答命令字，无入站处理）
 *   0x80 创迪扩展全屏纯色：R(1) G(1) B(1) → 整屏填充；副作用：关闭光敏自动调光 + 亮度 8
 *   其他 静默丢弃（协议未定义错误应答）
 *
 * 传输：RS485 + RS232 + **UDP 专用业务口 CH_ID_UDP_GZOL**（2026-09-14 用户裁决：
 * 端口 = Sector1 `net_cfg.port`，与 TCP 业务口同号不同协议栈；`0x40` 改端口对该口
 * 生效，语义对齐源固件「设备自身 UDP 服务口」；出厂默认 9528）+ **UDP 10011**
 * （CH_ID_UDP，2026-09-14 增绑，保留——既有上位机仍可发 10011，应答走收包通道单播
 * 回源）。源固件设备侧出厂端口为 10028 且搜索应答走广播 → 上位机按 10028 发帧收不到，
 * 须发 9528（或 `0x40` 设定的新端口），详见 doc/14 README §11/§13。RJ45 槽 RB（1536B）
 * 由 IAP/LDI/CQ weak 提供，本模块只 acquire 不 provide。
 * 波特率：协议要求 **9600 8N1** → 现场 DIP1 必须置 OFF（9600）。
 * 文本编码：GBK（源固件字库为 GBK 点阵，渲染 FONT_ENC_GBK 直通）。
 *
 * 命名：模块前缀 gz_ol_ / 宏前缀 GZ_OL_（与贵州常规费显 ProtocolParser_GuiZhou
 * 的 gz_ / GZ_ 严格区分，两者帧族天然互斥、可同编共存）。
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
#define GZ_OL_GUIDE_LEN (4U)
#define GZ_OL_HEAD_LEN (16U)    /**< 引导4 + 序号4 + 长度2 + 保留2 + 命令4 */
#define GZ_OL_LEN_OFFSET (8U)   /**< 长度字段偏移（2B 小端，整帧长） */
#define GZ_OL_CMD_OFFSET (12U)  /**< 命令字偏移（4B 容器，低字节有效） */
#define GZ_OL_DATA_OFFSET (16U) /**< 基本数据包偏移 */
#define GZ_OL_TAIL (0x00U)      /**< 结束符 */

/** 帧长上下界：17 = 16 帧头 + 0 载荷 + 1 尾（0x10/0x30/0x50/0x60 最小帧） */
#define GZ_OL_FRAME_LEN_MIN (17U)
/** 入队/解析上限：实测合法最大帧 158B（FONT16 + 124B 文本上限），256 留 1.6x 余量；
 *  结构合法但更长的帧由 probe 返回 SKIP 整帧消费（不污染重同步路径） */
#define GZ_OL_FRAME_LEN_MAX (256U)
#define GZ_OL_PAYLOAD_MAX (GZ_OL_FRAME_LEN_MAX)
#define GZ_OL_QUEUE_DEPTH (3U)
#define GZ_OL_MSG_SIZE (sizeof(frame_msg_t) + GZ_OL_PAYLOAD_MAX) /* 264 */

_Static_assert(GZ_OL_PAYLOAD_MAX <= RB_SIZE_RS485, "GZ_OL frame must fit RS485 RB");
_Static_assert(GZ_OL_PAYLOAD_MAX <= RB_SIZE_RS232, "GZ_OL frame must fit RS232 RB");
_Static_assert(GZ_OL_PAYLOAD_MAX <= RB_SIZE_RJ45, "GZ_OL frame must fit RJ45 RB (UDP 10011)");

/* ---- 0x20 立即显示载荷布局 ---- */
#define GZ_OL_DISPLAY_PREFIX_LEN (17U) /**< X2 Y2 屏宽2 屏高2 字体名4 字号2 颜色3 */
#define GZ_OL_FONT_NAME_LEN (4U)       /**< 字体名字段固定 4 字节（GBK 两字） */

/** 搜索/修改 IP 应答载荷长度：ip4 + mask4 + gw4 + port2（**高字节在前 BE16**） */
#define GZ_OL_NET_PAYLOAD_LEN (14U)

/* ---- 命令字（仅低字节有效）---- */
typedef enum {
    GZ_OL_PCMD_FAULT_QUERY = 0x10, /**< 获取故障信息（应答 2B 状态） */
    GZ_OL_PCMD_DISPLAY     = 0x20, /**< 立即显示 */
    GZ_OL_PCMD_CLEAR       = 0x30, /**< 清屏 */
    GZ_OL_PCMD_SET_IP      = 0x40, /**< 修改 IP（应答 0x50 帧后软复位） */
    GZ_OL_PCMD_SET_IP_ACK  = 0x50, /**< 修改 IP 返回／兼容源固件的搜索触发 */
    GZ_OL_PCMD_SEARCH      = 0x60, /**< 搜索设备（应答 0x70 帧） */
    GZ_OL_PCMD_SEARCH_ACK  = 0x70, /**< 搜索设备返回信息（仅出站） */
    GZ_OL_PCMD_FULL_COLOR  = 0x80, /**< 创迪扩展全屏纯色 */
    GZ_OL_PCMD_INVALID     = 0xFF, /**< 非法命令字 */
} gz_ol_pcmd_t;

/** 解析状态 */
typedef enum {
    GZ_OL_PARSE_OK = 0,
    GZ_OL_PARSE_ERR_FRAME, /**< 引导串/长度字段/结束符不符 */
    GZ_OL_PARSE_ERR_CMD,   /**< 命令字非法（静默丢弃） */
    GZ_OL_PARSE_ERR_PARAM, /**< 参数长度或取值非法（整帧丢弃） */
} gz_ol_parse_sta_t;

/** 0x20 立即显示参数 */
typedef struct {
    uint16_t x;             /**< 显示起点 X（像素，屏幕左上角为原点） */
    uint16_t y;             /**< 显示起点 Y（像素） */
    const char *text;       /**< 显示内容（GBK） */
    uint16_t text_len;      /**< 显示内容字节数 */
    font_size_t font_size;  /**< 字号（16/24/32，其余整帧丢弃） */
    font_type_t font_type;  /**< 字型（宋体/仿宋/楷体/黑体，未命中回退宋体） */
    display_color_t color;  /**< 显示颜色（RGB→8 色映射，见 README Q7） */
} gz_ol_display_t;

/** 0x40 修改 IP 参数 */
typedef struct {
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gw[4];
    uint16_t port; /**< **高字节在前 BE16**（2026-09-15 用户裁决：有意偏离源固件/协议文档；
                    *   写 10028 发 `27 2C`，`2C 27` = 11303）；语义见 README §13：
                    *   写 Sector1 net_cfg.port（GZ_OL 专用 UDP 业务口 + TCP 业务口） */
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
 * @brief  解析 "TCLY" 帧（纯函数，不阻塞、无副作用）。
 * @param  raw      原始帧数据。
 * @param  raw_len  原始帧长度（= probe 给出的整帧长）。
 * @return 解析结果；sta 标识结果，非 OK 时执行层不动作。
 */
gz_ol_parsed_cmd_t gz_ol_parse_frame(const uint8_t *raw, uint16_t raw_len);

/**
 * @brief  执行解析后的命令。
 * @param  ch   帧来源通道（应答单播回源）。
 * @param  cmd  解析结果（sta != OK 时直接返回）。
 */
void gz_ol_execute_cmd(channel_t *ch, const gz_ol_parsed_cmd_t *cmd);

/**
 * @brief  恢复光敏自动调光（复现源固件「收到数据即开启自动调光」语义）。
 * @note   0x80 全屏纯色会挂起光敏任务，此后收到任意帧即恢复；由帧处理任务调用。
 */
void gz_ol_resume_auto_dim(void);

/**
 * @brief  贵州治超屏协议帧处理任务入口（帧队列 → 解析 → 执行）。
 * @param  argument  未使用。
 */
void gz_ol_proto_handle_task(void *argument);

/**
 * @brief  贵州治超屏协议注册入口（sw_app_initcall 自注册）。
 */
void gz_ol_proto_init(void);
