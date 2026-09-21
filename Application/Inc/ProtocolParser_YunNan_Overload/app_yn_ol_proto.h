/**
 * @file    app_yn_ol_proto.h
 * @brief   云南治超屏协议（YN_1.3.0，`{` 帧族）公共定义
 *
 * 协议文档：《车道费额显示器通信协议》YN_1.3.0（2021-11-24，治超屏裁剪版；
 *          文档开头明示「以下协议用于治超屏上面不开发语音相关协议和固定格式显示协议」）。
 * 模块命名：`yn_ol_` / 宏 `YN_OL_`（与云南常规费显 ProtocolParser_YunNan 的
 *          `yn_` / `YN_` 严格区分；两者帧族相同、命令字高度重叠，属 `{` 帧族互斥关系）。
 *
 * ── 帧格式（长度字段定界，**无校验**）───────────────────────────────────────
 *   [0]      帧头 '{'（0x7B）
 *   [1]      命令字（ASCII '1'~'5'/'8'/'A' 或二进制 0x42~0x51/0x01/0x02）
 *   [2]      参数长度：**二进制字节值**（= 参数区字节数，不是 ASCII 数字）
 *   [3..N]   参数区（变长，含义随命令字）
 *   [N+1]    帧尾 '}'（0x7D）
 *   帧总长 = 参数长度 + 4（与云南常规协议完全同构）。
 *
 * ── 命令表（治超屏裁剪版；'6'/'7'/'9' 文档明示不开发 → 本模块不接受、probe 快拒）──
 *   '1'      主机查询      → 应答 `{ '1' 01 00 }`（正常；异常应答无触发条件不回）
 *   '2'      自检          → 固定汉字信息与数字 1~9 交替显示（语音部分不开发）
 *   '3'      单行任意显示  → 颜色 + 行号('1'~'5') + GBK 文本（行高 = 当前字号）
 *                            **颜色/行号接受 ASCII 或二进制两种编码**（超集容错：
 *                            ASCII '0'~'2'/'1'~'5' 或二进制 0x00~0x02/0x01~0x05；
 *                            越界仍整帧丢弃。2026-09-17 晚联调轮，见 doc/15 §11.1）
 *   '4'      全屏可编辑    → 颜色 + X + Y + GBK 文本（X/Y 为像素坐标，word_wrap=true）
 *   '5'      全屏清除
 *   '8'      亮度设定      → '0'/0x00 = 恢复光敏自动；'1'~'8' = 手动档（挂起光敏）
 *   'A'      外设控制      → bit0 绿灯 / bit1 红灯 / bit2 黄闪报警（红优先）
 *   0x42~0x46 第一~五行清除（行号 1~5）
 *   0x50     第六行清除（文档第 20 条；2021-11-24 版本标注「除 20」但本模块保留实现，
 *            屏高不足时行起点越界自然不落屏——与云南常规 '6' 行 5 的处理口径一致）
 *   0x47     修改 IP       → ip4 + mask4 + gw4 + port2 → 写 Sector1 net_cfg.port
 *                            （`udp_port` 保留；端口字段**高字节在前 BE16**，见下）
 *                            → 应答 0x51 回显 → osDelay(100) → 软复位（重启生效）
 *   0x48     查询 IP       → 读 Sector1 net_cfg → 应答 0x51（14B 载荷，单播回源）
 *   0x49     设置屏体参数  → x0 字体(0宋/1仿/2楷/3黑) + x1 字宽(0-16/1-24/2-32 点阵)
 *                            → **持久化**（W25Qxx 独立 4KB 扇区，掉电保留；上电装载）
 *                              并立即作为后续 '3'/'4'/行清除的字号/字型，
 *                              见文件尾「0x49 持久化（W25Qxx 记录）」
 *   0x51     查询 IP 返回值（仅出站应答命令字；入站静默丢弃）
 *   0x01     全屏点亮      → DATA0: 01红/02绿/03黄（文档三色；04~07 蓝/紫/青/白
 *                            按 display_color_t 枚举直通保留，与云南常规则一致）
 *   0x02     获取版本号    → 串口/网络回裸 ASCII PROGRAM_CODE；屏幕居中显示「版本:PROGRAM_CODE」
 *   '6'/'7'/'9'/'B' 及其它 → probe 快拒 / 解析整帧丢弃（文档未定义错误应答 → 静默）
 *
 * ── 端口字段字节序（**已裁决：高字节在前 BE16**，2026-09-17）──────────────
 *   0x47 请求与 0x51 应答的 PORT0/PORT1 取**高字节在前 BE16**（写 9528 发 `25 38`）。
 *   依据：用户裁决（对齐同厂贵州治超 GZ_OL 的端口字段口径 doc/14 §13.5.1，以及本
 *   固件 LDI 12H / IAP 0x01 的既有高字节在前口径）。协议文档未写字节序。
 *   现场若发现上位机按低字节在前发送，改 `app_yn_ol_proto_parse.c` 的端口解析与
 *   `app_yn_ol_proto_cmd.c` 的回显即可（两处均为显式字节拼装）。
 *
 * ── 通道与波特率（2026-09-17 用户裁决：追加 TCP 双通道）──────────────────
 *   绑定 `CH_ID_RS485` + `CH_ID_RS232` + `CH_ID_TCP_SERVER` + `CH_ID_TCP_CLIENT`；
 *   每逻辑通道独立 mask、四个 mask 共用同一静态队列（与青海/山东/贵州/云南常规同模式）。
 *
 *   **RJ45 槽 probe 竞争的实际结论（源码 + 调度器证据）**：本协议首字节 '{' 与重庆 CQ 的
 *   JSON 帧同首字节，而 `cq_probe_frame` 对 '{' 做花括号深度扫描（`7B 31 00 7D` 会被它
 *   当作 4 字节完整 JSON 认领）→ **同槽必然互吞**。解法不在收录序，而在**通道掩码**：
 *   `app_dispatch.c` `frame_dispatch_task` 取 `proto = ch_proto_map[ch->ch_id]`，
 *   只有绑定在该通道上的协议才进 probe 链——而 CQ 只绑 `CH_ID_UDP` + `CH_ID_UDP_CQ`，
 *   **不绑 TCP Server/Client**。因此：
 *     · TCP 双通道上 CQ probe **根本不会被调用** → 本协议帧无被抢占风险（含半帧）；
 *     · 本模块**不绑 `CH_ID_UDP`（10011）** → CQ 的 UDP 行为零影响（不削弱 CQ 量产）。
 *   TCP 通道上同槽的其它协议为 LDI（首字节 0xFF 快拒）与 IAP（绑 UDP，不在 TCP 链上）→
 *   本协议独占 '{' 首字节。反向安全由本 probe 的命令字白名单保证：CQ JSON 第二字节恒为
 *   '"'（0x22）不在白名单 → 本 probe 对 CQ 帧 FAKE 放行；CQ 二进制帧首字节 0xFF 亦 FAKE。
 *   （收录序核对：Makefile 中 CQ 源文件在 YunNan_Overload 之前 → 若两者真同槽，CQ 先注册
 *   先认领；正因如此本模块**只走 TCP 通道**，不以收录序为依赖。见 doc/15 §4/§7。）
 *
 *   波特率：文档要求 9600~115200（默认 9600）→ 由全局 DIP1 选择（OFF=9600）。
 *   文本编码：GBK 直通（`FONT_ENC_GBK`）。
 *
 * ── 上电效果（**不实现**，2026-09-17 用户裁决）──────────────────────────
 *   文档「上电后显示『祝您一路平安』稍候熄灭」不再实现：原 `app_yn_ol_proto_default.c`
 *   及其 `app_default_display_register` 注册、5s 熄灭惰性任务已删除——上电画面由 STD
 *   现有默认显示链路给出（`app_default_display.c`；本模块不注册则回退系统默认画面）。
 *
 * ── 帧头冲突纪律（结论：与既有串口协议**首字节互斥**，但属 `{` 帧族需 EIDE 互斥）──
 *   首字节 0x7B 与青海/山东/贵州常规/云南常规/四川 MTC 相同（`{` 帧族）→
 *   **必须定义 `g_brace_proto_guard` 编译期互斥守卫**；**本协议与云南常规 `app_yn_proto`
 *   这一对已用 `arm-none-eabi-ld -r` 双编实测报 `multiple definition`** → 量产（EIDE）
 *   必二选一。全协议 Makefile 构建（`-DSTD_ALL_PROTO`）下 guard 失效，共存由 probe
 *   注册序与文档纪律约束（0x46~0x51 为本协议独有命令字，见 doc/15 §4）。
 *
 * ── 内存纪律 ────────────────────────────────────────────────────────────
 *   队列体 + 控制块 + 任务帧缓冲置 **CCMRAM**（`PROTO=ALL` 口径 SRAM 余量仅数百 B，
 *   放不下 ~1.1KB 静态体；缓冲为 CPU 独占访问、无 DMA/ETH，CCM 适用——CQ/GZ_OL 先例）；
 *   0x49 持久化记录缓冲为 12B 静态 SRAM（落盘路径经 `dev_storage_write` 内部整扇区
 *   读-改-写，无需本模块持有整扇区镜像）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_dispatch.h"
#include "app_render.h" /* font_size_t / font_type_t / display_color_t */

/* ---- 帧结构常量 ---- */
#define YN_OL_STX (0x7BU)          /**< 帧头 '{' */
#define YN_OL_ETX (0x7DU)          /**< 帧尾 '}' */
#define YN_OL_HEAD_LEN (3U)        /**< '{' + 命令字 + 参数长度 */
#define YN_OL_CMD_OFFSET (1U)      /**< 命令字偏移 */
#define YN_OL_LEN_OFFSET (2U)      /**< 参数长度偏移（二进制字节值） */
#define YN_OL_DATA_OFFSET (3U)     /**< 参数区偏移 */
#define YN_OL_FRAME_OVERHEAD (4U)  /**< 3 字节头 + 1 字节尾 */
#define YN_OL_FRAME_LEN_MIN (4U)   /**< 空参数最小帧（如 7B 35 00 7D） */

/** 参数区上限（字段为 1B → 最大 255）→ 帧总长上限 259，与云南常规协议同构 */
#define YN_OL_PAYLOAD_MAX (255U)
/** 入队/解析上限（= 帧总长上限 259） */
#define YN_OL_FRAME_LEN_MAX (YN_OL_PAYLOAD_MAX + YN_OL_FRAME_OVERHEAD)
/** 队列深度 3：`{` 帧族一包多帧粘包惯例（与青海/贵州/云南常规一致） */
#define YN_OL_QUEUE_DEPTH (3U)
#define YN_OL_MSG_SIZE (sizeof(frame_msg_t) + YN_OL_FRAME_LEN_MAX)

_Static_assert(YN_OL_FRAME_LEN_MAX <= RB_SIZE_RS485, "YN_OL frame must fit RS485 RB");
_Static_assert(YN_OL_FRAME_LEN_MAX <= RB_SIZE_RS232, "YN_OL frame must fit RS232 RB");
_Static_assert(YN_OL_FRAME_LEN_MAX <= RB_SIZE_RJ45, "YN_OL frame must fit RJ45 RB (UDP 10011)");

/* ---- 参数布局常量 ---- */
#define YN_OL_ONE_LINE_PREFIX_LEN (2U)  /**< 颜色 + 行号 */
#define YN_OL_FULL_SCREEN_PREFIX_LEN (3U) /**< 颜色 + X + Y */
#define YN_OL_SCREEN_PARAM_LEN (2U)     /**< x0 字体 + x1 字宽 */
#define YN_OL_NET_PAYLOAD_LEN (14U)     /**< ip4 + mask4 + gw4 + port2 */

/* ---- 命令字 ---- */
typedef enum {
    YN_OL_PCMD_HOST_QUERY   = '1',  /**< 主机查询（应答状态） */
    YN_OL_PCMD_SELF_CHECK   = '2',  /**< 自检（显示部分） */
    YN_OL_PCMD_ONE_LINE     = '3',  /**< 单行任意显示 */
    YN_OL_PCMD_FULL_SCREEN  = '4',  /**< 全屏可编辑显示 */
    YN_OL_PCMD_CLEAR        = '5',  /**< 全屏清除 */
    YN_OL_PCMD_BRIGHTNESS   = '8',  /**< 显示亮度设定 */
    YN_OL_PCMD_PERIPHERAL   = 'A',  /**< 外设控制（绿灯/红灯/黄闪） */
    YN_OL_PCMD_CLEAR_ROW1   = 0x42, /**< 第一行清除 */
    YN_OL_PCMD_CLEAR_ROW2   = 0x43, /**< 第二行清除 */
    YN_OL_PCMD_CLEAR_ROW3   = 0x44, /**< 第三行清除 */
    YN_OL_PCMD_CLEAR_ROW4   = 0x45, /**< 第四行清除 */
    YN_OL_PCMD_CLEAR_ROW5   = 0x46, /**< 第五行清除 */
    YN_OL_PCMD_SET_IP       = 0x47, /**< 修改 IP（应答 0x51 后软复位） */
    YN_OL_PCMD_GET_IP       = 0x48, /**< 查询 IP（应答 0x51） */
    YN_OL_PCMD_SCREEN_PARAM = 0x49, /**< 设置屏体参数（字体 + 字宽） */
    YN_OL_PCMD_CLEAR_ROW6   = 0x50, /**< 第六行清除（文档第 20 条） */
    YN_OL_PCMD_GET_IP_ACK   = 0x51, /**< 查询 IP 返回值（仅出站；入站丢弃） */
    YN_OL_PCMD_FILL_ALL     = 0x01, /**< 全屏点亮（01红/02绿/03黄，04~07 扩展） */
    YN_OL_PCMD_VERSION      = 0x02, /**< 获取版本号 */
    YN_OL_PCMD_INVALID      = 0xFF, /**< 非法/不支持命令字 */
} yn_ol_pcmd_t;

/** 解析状态 */
typedef enum {
    YN_OL_PARSE_OK = 0,
    YN_OL_PARSE_ERR_FRAME, /**< 帧头/帧尾/长度字段不符 */
    YN_OL_PARSE_ERR_CMD,   /**< 命令字非法或不支持（静默丢弃） */
    YN_OL_PARSE_ERR_PARAM, /**< 参数长度或取值非法（整帧丢弃） */
} yn_ol_parse_sta_t;

/** 执行结果状态码（`yn_ol_execute_cmd` 返回值；逐帧诊断 `[yn_ol] exec ... ret=` 用）
 *  语义：0 = 已执行（屏幕动作可能因坐标越界而无可见效果，属正常协议语义）；
 *        <0 = 未执行/未生效原因，现场可据此一眼区分「帧被拒」与「执行了但看不到」。 */
typedef enum {
    YN_OL_EXEC_OK         = 0,  /**< 已执行（含「执行但不落屏」） */
    YN_OL_EXEC_NO_DISPLAY = -1, /**< 无显示实例（dev_display_get()==NULL） */
    YN_OL_EXEC_BAD_PARAM  = -2, /**< 参数越界（防御性边界，解析已限定） */
    YN_OL_EXEC_NOT_RUN    = -3, /**< 解析失败未执行（sta != OK） */
    YN_OL_EXEC_NOT_MATCH  = -4, /**< 命令字未匹配到执行分支（理论不可达） */
} yn_ol_exec_ret_t;

/** '3' 单行显示参数 */
typedef struct {
    uint8_t color;         /**< 协议颜色索引 0 红 / 1 绿 / 2 黄 */
    uint8_t row;           /**< 行号 0~4（协议 '1'~'5'） */
    const uint8_t *text;   /**< 文本（GBK） */
    uint16_t text_len;     /**< 文本字节数 */
} yn_ol_one_line_t;

/** '4' 全屏可编辑显示参数 */
typedef struct {
    uint8_t color;         /**< 协议颜色索引 0 红 / 1 绿 / 2 黄 */
    uint8_t x;             /**< X 坐标（像素，最左为 0） */
    uint8_t y;             /**< Y 坐标（像素，最上为 0） */
    const uint8_t *text;   /**< 文本（GBK，可含 0x0A 换行） */
    uint16_t text_len;     /**< 文本字节数 */
} yn_ol_full_screen_t;

/** 0x47 修改 IP 参数 */
typedef struct {
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gw[4];
    uint16_t port; /**< **高字节在前 BE16**（已裁决，见文件头「端口字段字节序」） */
} yn_ol_setip_t;

/** 0x49 屏体参数 */
typedef struct {
    font_type_t font_type; /**< 0 宋体 / 1 仿宋 / 2 楷体 / 3 黑体 */
    font_size_t font_size; /**< 字宽 0→FONT_16 / 1→FONT_24 / 2→FONT_32 */
} yn_ol_screen_param_t;

/* ================================================================
 *  0x49 持久化（W25Qxx 记录；2026-09-17 用户裁决 Q5 = 需要持久化）
 *
 *  记录布局（12B，与 LDI 的 magic + 载荷 + CRC32 范式一致）：
 *    magic(4) + version(2) + font_type(1) + font_size(1) + crc32(4)
 *  CRC32 覆盖 magic..font_size（前 8B），不含 crc32 自身。
 *
 *  存储位置：`dev_storage_capacity(w25) - 12288`（**倒数第三个 4KB 扇区**），
 *  与 LDI 记录（`capacity - 4096`）和 app_render 的显示持久化记录
 *  （`capacity - 8192`，`app_render.c:372`）各占独立扇区。选址依据（实测核对）：
 *    · 字库数据最大绝对地址 ≈ 8,181,856B（32 点阵 GBK 黑体末字，
 *      `_flash_addr_cfg` 公式 + `_packed_glyph_bytes(32,GBK)=128`）→ 只用到第 1997 扇区；
 *      第 1998~2045 扇区全空（W25Q64 共 2048 个 4KB 扇区）。
 *    · `capacity - 4096`（扇区 2047）= LDI 配置记录（`app_ldi_cfg.c`）；
 *      `capacity - 8192`（扇区 2046）= app_render 显示持久化（`app_render.c`）。
 *    · 故取 2045（`capacity - 12288`）——**独立扇区、零共享、无整扇区保全负担**。
 *  写语义：`dev_w25qxx._write` 内部按 4KB 扇区读-改-写（目标区间含非 0xFF 字节时
 *  先擦该扇区再回写整扇区），本模块只需写 12B 记录，无需自备整扇区镜像；
 *  本扇区仅本模块使用 → 自擦自写不波及 LDI / app_render 记录。
 */
#define YN_OL_CFG_MAGIC (0x594E4F4CU)   /**< "YNOL"（小端存 'Y''N''O''L'） */
#define YN_OL_CFG_VERSION (1U)          /**< 记录版本；不匹配即视为无效回默认 */
#define YN_OL_CFG_SECTOR_OFFSET (12288U) /**< 扇区位置 = capacity - 12288 */

/** 0x49 屏体参数持久化记录（12B；布局由 _Static_assert 锁定） */
typedef struct {
    uint32_t magic;     /**< YN_OL_CFG_MAGIC */
    uint16_t version;   /**< YN_OL_CFG_VERSION */
    uint8_t font_type;  /**< font_type_t（0~3） */
    uint8_t font_size;  /**< font_size_t（FONT_16/24/32） */
    uint32_t crc32;     /**< CRC32 覆盖前 8B */
} yn_ol_screen_cfg_record_t;

/** 记录结构尺寸（magic4 + version2 + font1 + size1 + crc4） */
#define YN_OL_CFG_RECORD_SIZE (12U)
_Static_assert(sizeof(yn_ol_screen_cfg_record_t) == YN_OL_CFG_RECORD_SIZE,
               "yn_ol_screen_cfg_record_t must be 12B");

/** 解析结果 */
typedef struct {
    yn_ol_pcmd_t cmd;
    yn_ol_parse_sta_t sta;
    uint16_t data_len;   /**< 参数区字节数 */
    const uint8_t *data; /**< 参数区（= &raw[YN_OL_DATA_OFFSET]） */
    union {
        yn_ol_one_line_t one_line;
        yn_ol_full_screen_t full_screen;
        yn_ol_setip_t setip;
        yn_ol_screen_param_t screen;
        uint8_t clear_row;   /**< 行号 0~5（0x42~0x46 / 0x50） */
        uint8_t brightness;  /**< 0 = 自动；1~8 = 手动档（'8'） */
        uint8_t peripheral;  /**< 外设位图（'A'） */
        uint8_t fill_color;  /**< 全屏点亮颜色枚举值（0x01） */
    } p;
} yn_ol_parsed_cmd_t;

/**
 * @brief  命令字字节 → 命令枚举（probe 白名单与 parse 共用单一真源）。
 * @param  c  命令字字节。
 * @return 对应枚举；不支持/非法返回 YN_OL_PCMD_INVALID。
 */
yn_ol_pcmd_t yn_ol_cmd_from_byte(uint8_t c);

/**
 * @brief  解析 `{` 帧（纯函数，不阻塞、无副作用）。
 * @param  raw      原始帧数据。
 * @param  raw_len  原始帧长度（= probe 给出的整帧长）。
 * @return 解析结果；sta 标识结果，非 OK 时执行层不动作。
 */
yn_ol_parsed_cmd_t yn_ol_parse_frame(const uint8_t *raw, uint16_t raw_len);

/**
 * @brief  执行解析后的命令。
 * @param  ch   帧来源通道（应答单播回源）。
 * @param  cmd  解析结果（sta != OK 时直接返回 YN_OL_EXEC_NOT_RUN）。
 * @return yn_ol_exec_ret_t：0 = 已执行；<0 = 未执行/未生效原因。
 * @note   返回值仅用于逐帧 RTT 诊断（`[yn_ol] exec ... ret=`）；协议行为不变
 *         （协议未定义错误应答 → 失败仍静默）。
 */
int yn_ol_execute_cmd(channel_t *ch, const yn_ol_parsed_cmd_t *cmd);

/**
 * @brief  当前生效的屏体字号（0x49 设置 / 上电从 W25Qxx 装载）。
 * @note   默认 **FONT_16** —— 沿用 STD 工程既有默认口径（青海/贵州/云南常规等
 *         `{` 帧族协议的默认字号同为 FONT_16，见 `app_qh_proto_cmd.c` /
 *         `app_gz_proto_cmd.c` / `app_yn_proto_cmd.c`）。
 */
font_size_t yn_ol_current_font_size(void);

/**
 * @brief  当前生效的屏体字型（0x49 设置 / 上电从 W25Qxx 装载）。
 * @note   默认 **FONT_ST**（宋体）—— 沿用 STD 工程既有默认口径（同青海/贵州/云南常规）。
 */
font_type_t yn_ol_current_font_type(void);

/**
 * @brief  上电装载 0x49 屏体参数记录（模块 `sw_app_initcall` 初始化时调用）。
 *
 * 读 `capacity - 12288` 处的 12B 记录：magic + version + 字段范围 + CRC32 全部通过
 * 才覆盖运行态；否则保持默认值并（`YN_OL_RTT_DIAG` 开启时）打一条 RTT 告警。
 * **不阻塞、不重启**：记录为空/损坏即静默回默认。
 *
 * @return true = 装载成功（运行态已被记录覆盖）；false = 无有效记录（保持默认）。
 */
bool yn_ol_screen_cfg_load(void);

/**
 * @brief  保存当前 0x49 屏体参数到 W25Qxx 记录（`0x49` 执行时调用）。
 * @return true = 写入成功；false = 存储不可用或写入失败（运行态仍已生效，
 *         仅掉电不保留；调用方打 RTT 告警，不阻塞、不重启）。
 */
bool yn_ol_screen_cfg_save(void);

/**
 * @brief  云南治超屏协议帧处理任务入口（帧队列 → 解析 → 执行）。
 * @param  argument  未使用。
 */
void yn_ol_proto_handle_task(void *argument);

/**
 * @brief  云南治超屏协议注册入口（sw_app_initcall 自注册）。
 */
void yn_ol_proto_init(void);
