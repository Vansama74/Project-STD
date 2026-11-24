#pragma once

#include "cmsis_os2.h"
#include "app_dispatch.h"
#include "app_ldi_cfg.h"
#include "ring_buffer.h"

#define DEVICE_NUM      (4U) // 本设备使用到的功能模块数量

#define LDI_TX_BUF_SIZE (512U) // 响应帧拼装缓冲区上限

/* 创迪发现口固定值（设计原则：禁止修改、不提供修改接口，0AH/4B02 均不携带此端口） */
#define LDI_DISCOVERY_PORT (10011U)

/* 设备配置功能端口出厂默认值（TCP 业务端口，协议文档 doc/02 附件2：9528） */
#define LDI_DEFAULT_CONFIG_PORT (9528U)

/**
 * 外设功能模块类型编码
 * 依据《车道设备接口规范》第2.1节定义
 * 编码范围 E1H~EBH，与协议帧中 DeviceType 字段一一对应
 */
typedef enum : uint8_t {
    LDI_DEV_TYPE_RSU          = 0xE1, // 路侧单元
    LDI_DEV_TYPE_LPR          = 0xE2, // 高清车牌图像识别设备
    LDI_DEV_TYPE_VTR          = 0xE3, // 车型识别设备
    LDI_DEV_TYPE_BARRIER      = 0xE4, // 电动栏杆机
    LDI_DEV_TYPE_VD           = 0xE5, // 车辆检测器
    LDI_DEV_TYPE_DISPLAY      = 0xE6, // 信息显示屏
    LDI_DEV_TYPE_LANE_SIGNAL  = 0xE7, // 通行信号灯
    LDI_DEV_TYPE_ALARM        = 0xE8, // 报警器
    LDI_DEV_TYPE_VMS          = 0xE9, // LED情报板（含引导屏）
    LDI_DEV_TYPE_CANOPY_LIGHT = 0xEA, // 雨棚信号灯
    LDI_DEV_TYPE_FOG_LIGHT    = 0xEB, // 雾灯
    LDI_DEV_TYPE_VOICE        = 0xF4, // 语音播报
    LDI_DEV_TYPE_COUNT        = 13,   // 本项目支持的设备类型数量
} ldi_device_t;

/**
 * 本固件实际实现的模块列表（单一真源）
 *
 * 用法：X(device_type, device_index)。消费者 = `ldi_device_available()` 的
 * switch 生成（app_ldi_device.c）。新增/删除实现的模块只改这一张列表。
 * 注意：这里列的是「固件实现了该模块」的判定依据，不等同于 g_ldi.cfg.modules
 * （后者是平台 0BH 下发的运行配置，可裁剪）。
 */
#define LDI_MODULE_LIST(X)         \
    X(LDI_DEV_TYPE_DISPLAY, 1)     \
    X(LDI_DEV_TYPE_LANE_SIGNAL, 1) \
    X(LDI_DEV_TYPE_ALARM, 1)       \
    X(LDI_DEV_TYPE_VOICE, 1)

/**
 * 通用通讯数据帧
 * 依据《车道设备接口规范》第3.4节定义
 * 服务端与客户端通讯的统一帧格式，所有数据均为大端序
 *
 * 帧布局:
 *   STX(2B) | VER(1B) | SEQ(1B) | LEN(4B) | DATA(N) | CRC(2B)
 *
 * CRC-16/XMODEM 校验范围覆盖 VER ~ DATA 全部字节，多项式 0x8408，初始值 0xFFFF
 */
typedef struct [[gnu::packed]] {
    uint8_t stx[2];     // 帧开始标志, 固定值 0xFFFF
    uint8_t ver;        // 协议版本号, 当前版本 0x00
    uint8_t seq;        // 帧序列号: 服务端主动=0x0X, 客户端主动=0xX0, X=1~9自动递增
    uint8_t len[4];     // DATA域长度, 高2字节保留, 低2字节为实际长度
    uint8_t data_crc[]; // DATA内容 + CRC16校验值(2字节, 帧尾), 柔性数组成员
} ldi_frame_t;

/**
 * 数据帧通用头部（标准版，4 字节时间戳）
 * 除 1BH/1CH 等少数命令外，大多数配置类/通用类命令的 DATA 域以此头部开头
 *
 * 字段布局 (共20字节):
 *   CmdType(1B) | UnixTime(4B) | LaneHex(5B) | CertInfo(8B) | Reserve(2B)
 */
typedef struct [[gnu::packed]] {
    uint8_t cmd_type;          // 指令代码, 如 0AH=IP设置请求, B0H=参数配置应答
    uint8_t unix_timestamp[4]; // Unix时间戳, 格林威治1970-01-01起的总秒数, 大端
    uint8_t lane_code[5];      // 车道HEX编号: 网络号(2B,BCD) + 站点号(2B,BCD) + 车道号(1B)
    uint8_t cert_info[8];      // 设备验证信息, 由平台生成的唯一设备验证码
    uint8_t reserve[2];        // 保留字节, 默认填 00H
} ldi_req_head_t;

/**
 * 控制/查询命令专用头部（8 字节时间戳，毫秒精度）
 * 仅 1BH/B1H 使用，其他命令使用 ldi_req_head_t
 *
 * 字段布局 (共24字节):
 *   CmdType(1B) | UnixTime(8B) | LaneHex(5B) | CertInfo(8B) | Reserve(2B)
 */
typedef struct [[gnu::packed]] {
    uint8_t cmd_type;          // 指令代码, 1BH=控制查询请求, B1H=控制查询应答
    uint8_t unix_timestamp[8]; // Unix时间戳, 毫秒精度, 大端
    uint8_t lane_code[5];      // 车道HEX编号
    uint8_t cert_info[8];      // 设备验证信息
    uint8_t reserve[2];        // 保留字节, 默认填 00H
} ldi_ctrl_head_t;

/**
 * LDI 协议命令码枚举
 * 涵盖配置接口(4.x)、通用接口(5.x)、功能接口(6.x) 三类命令
 */
typedef enum : uint8_t {
    // ---- 配置接口命令 (出厂配置模式, 设备为服务端) ----
    LDI_CMD_SET_IP_REQ = 0x0A, // 设备IP信息设置请求
    LDI_CMD_SET_IP_RSP = 0xA0, // 设备IP信息设置应答

    LDI_CMD_SET_PARA_REQ = 0x0B, // 设备参数配置请求
    LDI_CMD_SET_PARA_RSP = 0xB0, // 设备参数配置应答

    LDI_CMD_REBOOT_REQ = 0x0D, // 设备重启请求
    LDI_CMD_REBOOT_RSP = 0xD0, // 设备重启应答

    LDI_CMD_GET_IP_REQ = 0x1D, // 设备IP信息采集请求
    LDI_CMD_GET_IP_RSP = 0xD1, // 设备IP信息采集应答

    LDI_CMD_GET_PARA_REQ = 0x1E, // 设备参数采集请求
    LDI_CMD_GET_PARA_RSP = 0xE1, // 设备参数采集应答

    // ---- 通用接口命令 (工作模式, 设备为客户端) ----
    LDI_CMD_STA_RPT_REQ = 0x0C, // 设备状态监控上报 (间隔5秒)
    LDI_CMD_STA_RPT_RSP = 0xC0, // 监控上报应答 (携带软件版本信息)

    LDI_CMD_CERT_REQ = 0x0E, // 设备验证申请 (上电后定时3秒重发直至成功)
    LDI_CMD_CERT_RSP = 0xE0, // 设备验证应答 (00H=认证成功)

    LDI_CMD_UPDATE_REQ = 0x0F, // 设备升级包下载请求
    LDI_CMD_UPDATE_RSP = 0xF0, // 设备升级包下载应答 (含MD5+文件流)

    LDI_CMD_INIT_REQ = 0x1A, // 设备初始化请求 (验证成功后由服务端发起)
    LDI_CMD_INIT_RSP = 0xA1, // 设备初始化应答

    // ---- 功能接口命令 (工作模式) ----
    LDI_CMD_CTRL_REQ = 0x1B, // 控制/查询请求 (服务端→设备, 支持多模块组合)
    LDI_CMD_CTRL_RSP = 0xB1, // 控制/查询应答

    LDI_CMD_FUNC_RPT_REQ = 0x1C, // 设备功能数据上报 (设备→服务端)
    LDI_CMD_FUNC_RPT_RSP = 0xC1, // 上报确认应答

    // ---- 设备搜索 (UDP 广播) ----
    LDI_CMD_SEARCH_REQ = 0x21, // 设备搜索请求 (上位机→设备, UDP广播)
    LDI_CMD_SEARCH_RSP = 0x12, // 设备搜索应答 (设备→上位机, UDP广播)
} ldi_cmd_type_t;

/* ---- 协议上下文 ---- */

typedef enum {
    LDI_ST_UNINIT, // 未认证：只主动发 0EH(验证申请)；接受 E0H 与全部配置类命令(0AH/0BH/0DH/1DH/1EH/21H)
    LDI_ST_AUTHED, // 已认证：在 UNINIT 基础上接受 1AH(初始化)，并开始 5 秒 0CH 上报
    LDI_ST_READY,  // 初始化完成：全部命令可用
} ldi_state_t;

typedef struct {
    ldi_state_t state;
    uint8_t rsp_seq; // 响应序号（探头保存，回显 host 请求帧）
    uint8_t rpt_seq; // 主动上报序号计数器（→ 0x10, 0x20...）

    app_flash_ldi_cfg_info_t cfg; // RAM 镜像 — LDI 运行态配置（网络四项真源在 Sector1）
    bool cfg_valid;

    uint32_t last_cert_tick; // 上次发送 0EH 的 RTOS tick（3 秒间隔）
    uint32_t last_rpt_tick;  // 上次发送 0CH 的 RTOS tick（5 秒间隔）

    /* ---- 会话锚点（认证成功时锚定发起认证的通道）----
     * 断链复位的判据：锚点非空且其通道已销毁/换实例/不再 UP → 复位一次。
     * 无锚点（从未认证成功）时不做任何状态拉平。两字段仅比较与判空，
     * 实际发送前用 sess_ch_id 现查 app_channel_get，避免持有失效实例。 */
    uint8_t sess_ch_id;       // 完成认证的通道 ID（CH_ID_*），0 = 无会话
    const channel_t *sess_ch; // 会话通道实例地址（仅比较/判空，不解引用）
    uint32_t sess_gen;        // 锚定时的通道注册代次（识别实例复用重连，见 app_channel_link_gen）

    osMutexId_t tx_lock;             // 保护 tx_buf，两个任务共享
    uint8_t tx_buf[LDI_TX_BUF_SIZE]; // 响应帧拼装缓冲区
} ldi_ctx_t;

extern ldi_ctx_t g_ldi;

void ldi_ctx_init(ldi_ctx_t *self);

/** @brief 将组好的响应帧交给独立 TX 任务发送（拷贝入队即返回，不在调用方阻塞） */
void ldi_tx_enqueue(channel_t *ch, const uint8_t *frame, uint16_t len);

/**
 * @brief 以 Sector1 net_cfg 为单一真源刷新 LDI 运行态网络字段（IP/掩码/网关/配置端口）
 *
 * LDI 各网络上报路径（0x12 搜索应答 / 0xD1 IP 采集应答）入口调用，保证与
 * IAP 0x01（直接读 Sector1）同源；记录无效时保留现值。见 app_ldi.c 实现注释。
 */
bool ldi_sync_net_from_board_cfg(void);

/**
 * @brief 本机身份是否未配置（lane_hex 与 cert 全 0）
 *
 * lane_hex/cert 只由 0AH/0BH 在帧头字段非全 0 时写入，全 0 = 从未被平台配置过。
 * 身份校验（probe 与 E0H）在此状态下豁免，避免零身份把平台所有帧 SKIP。
 */
bool ldi_identity_unconfigured(void);

/* ---- API ---- */

extern osMessageQueueId_t g_ldi_msg_queue;
extern osThreadId_t g_ldi_task_handle;
extern const osThreadAttr_t ldi_task_attr;
extern osThreadId_t g_ldi_timer_task_handle;
extern const osThreadAttr_t ldi_timer_task_attr;
extern osThreadId_t g_ldi_tx_task_handle;
extern const osThreadAttr_t ldi_tx_task_attr;

void ldi_handle_task(void *argument);
void ldi_timer_task(void *argument);
proto_probe_sta_t ldi_probe_frame(const channel_t *ch, const ring_buffer_t *buff, uint32_t *total_len, uint8_t *aux);
uint8_t ldi_get_device_index(ldi_device_t device_type);
void ldi_set_device_index(ldi_device_t device_type, uint8_t device_index);

/* ---- 响应帧头部构建（从内部状态，不从请求拷贝）---- */
void ldi_build_rsp_head(ldi_req_head_t *head, uint8_t cmd_type);
void ldi_build_ctrl_rsp_head(ldi_ctrl_head_t *head, uint8_t cmd_type);
