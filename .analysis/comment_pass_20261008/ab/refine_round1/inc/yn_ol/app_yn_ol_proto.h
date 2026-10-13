/**
 * @file    app_yn_ol_proto.h
 * @brief   云南治超屏协议（YN_1.3.0，`{` 帧族）公共定义
 *
 * 帧 = `{`(0x7B) + 命令字 + 参数长度(1B **二进制**，= 参数区字节数) + 参数 + `}`(0x7D)，
 * 帧 ≤259B、**无校验**；端口字段（0x47/0x51）**高字节在前 BE16**（已裁决）。
 * 属 `{` 帧族 → 定义 `g_brace_proto_guard`，量产与其它 `{` 族协议**二选一**。
 * 队列体/控制块/任务帧缓冲置 **CCMRAM**（1152B，见 §6）。
 * 详见 doc/15_云南治超屏协议/README.md §1、§2、§4、§6、§7
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

/** 执行结果状态码（`yn_ol_execute_cmd` 返回值；供调试器观察，不产生上行报文）。
 *  语义：0 = 已执行（含坐标越界「执行但不落屏」，属正常协议语义）；<0 = 未执行/未生效原因。 */
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
    uint16_t port; /**< **高字节在前 BE16**（写 9528 发 `25 38`；已裁决，见 doc/15_云南治超屏协议/README.md §2） */
} yn_ol_setip_t;

/** 0x49 屏体参数 */
typedef struct {
    font_type_t font_type; /**< 0 宋体 / 1 仿宋 / 2 楷体 / 3 黑体 */
    font_size_t font_size; /**< 字宽 0→FONT_16 / 1→FONT_24 / 2→FONT_32 */
} yn_ol_screen_param_t;

/* 0x49 持久化记录（12B，仿 LDI 记录范式）：magic + version + 字体/字宽 + CRC32（覆盖前 8B）。
 * 落 W25Qxx **独立 4KB 扇区** `capacity - 12288`（不与 LDI `-4096`、app_render `-8192` 共享）；
 * 无自备整扇区镜像（整扇区读-改-写由 dev_w25qxx._write 内部完成）；记录无效/写失败 → 回默认，
 * 不阻塞、不重启。
 * 选址核对、装载/保存失败语义、默认值口径见 doc/15_云南治超屏协议/README.md §7 */
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
 * @brief  执行解析后的命令（失败静默——协议未定义错误应答）。
 * @param  ch   帧来源通道（应答单播回源）。
 * @param  cmd  解析结果（sta != OK 时直接返回 YN_OL_EXEC_NOT_RUN）。
 * @return yn_ol_exec_ret_t（0 = 已执行；<0 = 未执行/未生效原因，供调试器观察）。
 */
int yn_ol_execute_cmd(channel_t *ch, const yn_ol_parsed_cmd_t *cmd);

/**
 * @brief  当前生效的屏体字号（0x49 设置 / 上电从 W25Qxx 装载）。
 * @note   默认 FONT_16 —— 沿用 STD 既有默认口径（同青海/贵州/云南常规），见 doc/15_云南治超屏协议/README.md §7。
 */
font_size_t yn_ol_current_font_size(void);

/**
 * @brief  当前生效的屏体字型（0x49 设置 / 上电从 W25Qxx 装载）。
 * @note   默认 FONT_ST —— 沿用 STD 既有默认口径（同青海/贵州/云南常规），见 doc/15_云南治超屏协议/README.md §7。
 */
font_type_t yn_ol_current_font_type(void);

/**
 * @brief  上电装载 0x49 屏体参数记录（模块 `sw_app_initcall` 初始化时调用）。
 * @return true = 装载成功（运行态已被记录覆盖）；false = 记录为空/损坏（静默保持默认）。
 * @note   记录为空/损坏/写失败一律不阻塞、不重启；语义与选址见 doc/15_云南治超屏协议/README.md §7。
 */
bool yn_ol_screen_cfg_load(void);

/**
 * @brief  保存当前 0x49 屏体参数到 W25Qxx 记录（`0x49` 执行时调用）。
 * @return true = 写入成功；false = 存储不可用或写入失败（运行态仍已生效，仅掉电不保留）。
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
