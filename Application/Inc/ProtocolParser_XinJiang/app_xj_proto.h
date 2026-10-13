/**
 * @file    app_xj_proto.h
 * @brief   新疆费额显示器协议公共定义
 *
 * 契约（改错静默致现场故障）：① 帧长字段 = 整帧总长；② 'T' 16B/行、'D' 按行号；
 * ③ 颜色 = RGB 各 ≥128 位掩码；④ 亮度 0 = 恢复光敏自动、1~255 → 8 档；
 * ⑤ 只绑 RS485/RS232；⑥ 自备 RS485/RS232 槽体（见 doc/19）；
 * ⑦ 队列/帧缓冲/栈置 CCMRAM；⑧ 心跳 15s 超时 + §3.2 应答。
 * 详见 doc/19_新疆费显协议/README.md §1、§2、§3、§4
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_dispatch.h"
#include "app_render.h" /* display_color_t（'T' 颜色映射结果与枚举恒等） */

/* ---- 帧结构常量（FF DC + Len(1B) + Cmd(1B) + 载荷 + BCC(1B)）---- */
#define XJ_STX0 (0xFFU)         /**< 帧头 1（[0]） */
#define XJ_STX1 (0xDCU)         /**< 帧头 2（[1]）：与 RLS `FF FE` / MM `FF E0` 在此分道 */
#define XJ_LEN_OFFSET (2U)      /**< 长度字段偏移（= 整帧总长，含帧头与 BCC） */
#define XJ_CMD_OFFSET (3U)      /**< 命令字偏移 */
#define XJ_DATA_OFFSET (4U)     /**< 载荷起始偏移 */
#define XJ_FRAME_LEN_MIN (5U)   /**< 无载荷命令整帧长（2 头 + 长度 + 命令 + BCC） */
#define XJ_FRAME_LEN_MAX (106U) /**< 最大整帧长：'T' 6 行 = 10+6×16（PDF §5.2 采样实证）。
                                 *   文档表 4 行（74）/ 采样 6 行（106）都收，屏外行由显示层裁剪 */
#define XJ_QUEUE_DEPTH (3U)     /**< 帧队列深度（RS485/RS232 两 mask 共用一队列） */
#define XJ_PAYLOAD_MAX (XJ_FRAME_LEN_MAX)
#define XJ_MSG_SIZE (sizeof(frame_msg_t) + XJ_PAYLOAD_MAX)

/* 契约：帧上限 ≤ 串口 RB 容量；且 < 治超帧长字段 0xDC(220)——保双方首字节 0xFF
 * 重叠时快拒不卡链。改错静默出错帧/链阻塞，见 doc/19 §1（帧格式）、§3.2（同槽共存）。 */

/* ---- 'T' 文本显示字段（0 基偏移：[4]亮度 [5]显示行 [6]R [7]G [8]B [9..]内容）---- */
#define XJ_T_TEXT_OFFSET (9U)                 /**< 内容区起始偏移（[9]；整帧长 = 10 + 16N） */
#define XJ_T_LINE_BYTES (16U)                 /**< 协议一行 16 字节（8 个汉字） */
#define XJ_T_LINE_MAX (6U)                    /**< 最多 6 行（文档表=4 行 64px 屏高；PDF §5.2
                                               *   采样帧为 96B=6 行，故按 6 收，多出行裁掉） */
#define XJ_T_CONTENT_MAX (XJ_T_LINE_BYTES * XJ_T_LINE_MAX)
/* 契约：'T' 整帧长 = 内容起始 9 + 16N + BCC 1 = XJ_FRAME_LEN_MAX。改错静默出错帧，见 doc/19 §1.2 / §2.3。 */

/* ---- 颜色/亮度映射 ---- */
#define XJ_RGB_HIGH (128U) /**< 单通道 ≥ 该值 = 该分量点亮（契约 ③） */

/* ---- 语音（'V'/'S'）字段边界 ---- */
#define XJ_V_FRAME_LEN_64 (71U)  /**< 文档「64 字节内容」形态（probe 两种长度都容忍） */
#define XJ_V_FRAME_LEN_65 (72U)  /**< 文档 Length=72 形态 */
#define XJ_S_FRAME_LEN (8U)      /**< §4.4 其他播报（预制编号）整帧长 */
#define XJ_S_QUOTE1_FRAME_LEN (12U) /**< §4.1 语音报价 1：「X 型车,请交费 N 元」 */
#define XJ_S_QUOTE2_FRAME_LEN (17U) /**< §4.2 语音报价 2：车型 + 总重 + 金额 */
#define XJ_S_QUOTE3_FRAME_LEN (18U) /**< §4.3 语音报价 3：总重 + 超重 */
#define XJ_VOL_MIN (0x30U)       /**< 音量字节下限（0x30 最小） */
#define XJ_VOL_MAX (0x37U)       /**< 音量字节上限（0x37 最大） */

/* ---- 报价语音载荷（'S' 报价 1/2/3 内容字段均为 ASCII 数字，非 GBK）----
 *  §4.1 [6]车型 [7..10]金额 | §4.2 [6]车型 [7..9]总重整数 [10..11]总重小数 [12..15]金额
 *  §4.3 [6]车型 [7..9]总重整数 [10..11]总重小数 [12..14]超重整数 [15..16]超重小数 */
#define XJ_S_ASCII_MIN (0x30U) /**< 载荷数字字符下界（'0'） */
#define XJ_S_ASCII_MAX (0x39U) /**< 载荷数字字符上界（'9'） */
#define XJ_S_PAYLOAD_OFFSET (6U) /**< 'S' 报价载荷起始偏移（[6]，= [4]语言 [5]音量之后） */
/* 契约：报价整帧长 = 载荷起始 6 + 各形态载荷长 + BCC 1（报价1 5B / 报价2 10B / 报价3 11B）。
 * 改错静默出错帧（probe 不认领），见 doc/19 §1.2 / §2.6。 */

/* ---- 'D' 行/全屏显示（§3.1.1 行显示 Len=23、§3.1.2 全屏 Len=71；PDF §5.2 采样另有 103）----
 *  [4]亮度 [5]显示行 [6..] 内容（行显示 16B / 全屏 16N 字节）——与 'T' 的
 *  [4]亮度 / [6]起内容同构，故复用 'T' 的渲染路径。
 *  全屏形态按「数据 = 16N」放行：N=4 → 71（文档表）、N=6 → 103（PDF §5.2 采样，
 *  两份采样帧的 BCC 均按文档规则自洽，故必须收；屏外行由显示层裁剪）。 */
#define XJ_D_TEXT_OFFSET (6U)     /**< 内容区起始偏移（[6]） */
#define XJ_D_LINE_FRAME_LEN (23U) /**< §3.1.1 行显示整帧长（6 + 16 + 1） */
#define XJ_D_FULL_LINE_MIN (2U)   /**< 全屏最少 2 行（N=1 归行显示） */
#define XJ_D_FULL_LINE_MAX (XJ_T_LINE_MAX) /**< 全屏最多行数（同 'T' 上限） */
#define XJ_D_FULL_FRAME_LEN_MIN (XJ_D_TEXT_OFFSET + XJ_T_LINE_BYTES * XJ_D_FULL_LINE_MIN + 1U)
#define XJ_D_FULL_FRAME_LEN_MAX (XJ_D_TEXT_OFFSET + XJ_T_LINE_BYTES * XJ_D_FULL_LINE_MAX + 1U)
#define XJ_D_FULL_FRAME_LEN (71U) /**< §3.1.2 文档表形态（6 + 64 + 1），落在 [MIN, MAX] 内 */
#define XJ_D_LINE_FIELD_MIN (1U)  /**< 行显示的显示行号下界（1~4） */
#define XJ_D_LINE_FIELD_MAX (4U)  /**< 行显示的显示行号上界（1~4） */
/* 契约：'D' 行显示整帧长 = 6 + 16 + 1；全屏形态 6 + 16N + 1（N=2~6 ⇒ 39~103），
 * 文档表形态 71B 须落在该区间内。改错静默出错帧，见 doc/19 §1.2 / §2.3。 */

/* ---- 心跳故障屏（编译开关：=0 时完全无心跳计数、无维护屏渲染；见 doc/19 §2.5）---- */
#ifndef XJ_FAULT_SCREEN
#define XJ_FAULT_SCREEN (1)
#endif
#define XJ_FAULT_TIMEOUT_MS (15000U) /**< 超时阈值（文档 5s 的 3 倍，容忍上位机节拍抖动） */
#define XJ_FAULT_POLL_MS (500U)      /**< 处理任务队列等待节拍（超时判定粒度，不新建任务） */

/* ---- 命令字白名单（PDF 2020 全文 §3.1 命令集 + 摘录版 'V'）----
 *  未收录：'B' 下载位图（PDF §3.1.12，帧头 `FE DB` + 双字节长度 1093，
 *  超出本模块单字节长度字段模型且依赖 128×64 bitmap 存储硬件，见 doc/19 §8）。 */
typedef enum {
    XJ_PCMD_TEXT          = 'T', /**< §3.1.1 彩色文本显示（含本帧亮度） */
    XJ_PCMD_HEARTBEAT     = 'O', /**< 心跳（判据 = 周期收到本帧） */
    XJ_PCMD_LANE_GREEN    = 'E', /**< §3.1.4 通行绿灯亮、红灯关 */
    XJ_PCMD_LANE_RED      = 'F', /**< §3.1.5 通行红灯亮、绿灯关 */
    XJ_PCMD_LANE_BOTH_ON  = 'G', /**< §3.1.6 通行信号灯红绿灯全亮 */
    XJ_PCMD_LANE_BOTH_OFF = 'H', /**< §3.1.7 通行信号灯红绿灯全关 */
    XJ_PCMD_ALARM_ON      = 'J', /**< §3.1.8 报警打开 */
    XJ_PCMD_ALARM_OFF     = 'K', /**< §3.1.9 报警器关闭 */
    XJ_PCMD_VOICE_TTS     = 'V', /**< 'V' 语音 TTS 播报（GBK 文本直送语音板，摘录版扩展） */
    XJ_PCMD_VOICE_PRESET  = 'S', /**< §4 语音报价 1/2/3（Len 12/17/18）+ §4.4 其他播报（Len 8） */
    XJ_PCMD_CLEAR         = 'C', /**< §3.1.2/§3.2.2 重置屏幕（整屏清黑） */
    XJ_PCMD_LINE_OR_FULL  = 'D', /**< §3.1.1 行显示（Len 23）/ §3.1.2 全屏显示（Len 71） */
    XJ_PCMD_QUERY         = 'Z', /**< §3.1.10 查询命令（无操作，仅触发 §3.2.3 应答） */
    XJ_PCMD_BITMAP_SHOW   = 'P', /**< §3.1.11 位图显示（图号不匹配则清屏） */
    XJ_PCMD_INVALID       = 0xFF, /**< 非法/不支持命令字（probe 即快拒） */
} xj_pcmd_t;

/** 'S' 语音帧形态（按整帧长判别，PDF §4.1~§4.4） */
typedef enum {
    XJ_S_KIND_INVALID = 0,   /**< 帧长不属任何已知 'S' 形态 */
    XJ_S_KIND_QUOTE1  = 1,   /**< §4.1 报价 1：车型 + 金额（4 位） */
    XJ_S_KIND_QUOTE2  = 2,   /**< §4.2 报价 2：车型 + 总重 + 金额（4 位） */
    XJ_S_KIND_QUOTE3  = 3,   /**< §4.3 报价 3：车型 + 总重 + 超重 */
    XJ_S_KIND_PRESET  = 4,   /**< §4.4 其他播报：预制编号 */
} xj_s_kind_t;

/** 帧解析结果（纯函数输出；parsed=false 时执行层不动作） */
typedef struct {
    bool parsed;            /**< true = 帧结构/长度字段/BCC/字段校验全部通过 */
    xj_pcmd_t cmd;          /**< 命令字（parsed=false 时为 XJ_PCMD_INVALID） */
    const uint8_t *text;    /**< 'T'/'D' 内容区 / 'V' 文本（GBK，指向 raw 不拷贝） */
    uint16_t text_len;      /**< text 字节数（'T' 为 16N、'D' 为 16/64、'V' 为 64/65） */
    uint8_t brightness;     /**< 'T'/'D' 亮度 0~255（0 = 恢复光敏自动调光） */
    display_color_t color;  /**< 'T' 颜色位掩码映射；COLOR_BLACK = 三通道全 <128（不画） */
    uint8_t line;           /**< 'D' 显示行号（1~4；全屏 Len=71 形态恒为 0） */
    bool d_is_full;         /**< 'D' = true 全屏显示（Len 71）、false 行显示（Len 23） */
    xj_s_kind_t s_kind;     /**< 'S' 形态（按帧长判别），非 'S' 命令为 XJ_S_KIND_INVALID */
    uint8_t volume;         /**< 'V'/'S' 原始音量字节（0x30~0x37，越界由执行层钳位） */
    uint8_t preset_no;      /**< 'S' §4.4 预制编号原始字节（0xA0~0xB6，未知编号忽略） */
    uint8_t payload[16];    /**< 'S' 报价载荷原样拷贝（§4.1/2/3 数字字段，最长 11B） */
    uint8_t payload_len;    /**< payload 有效字节数 */
    uint8_t bitmap_no;      /**< 'P' 下载位图图号（0~255） */
    const channel_t *src_ch;/**< 收到本帧的通道（§3.2 应答回送用；仅参考，不缓存生命周期） */
} xj_parsed_cmd_t;

/**
 * @brief  新疆费额显示器协议帧处理任务入口（帧队列 → 解析 → 执行 + 心跳节拍）。
 * @param  argument  未使用。
 */
void xj_proto_handle_task(void *argument);

/** @brief 新疆费额显示器协议注册入口（sw_app_initcall 自注册）。 */
void xj_proto_init(void);
