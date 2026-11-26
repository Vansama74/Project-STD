/**
 * @file    app_board_net_cfg.h
 * @brief   板级系统配置存储（内部 Flash Sector 1, 0x08004000, 16KB）
 *
 * 本模块是 Sector1 配置记录的**唯一布局归属**，LDI 与 IAP 协议共同依赖：
 *  - 网络配置（ip/mask/gw 共享 + port/udp_port 双端口）：LDI 0AH / IAP 4B02 写
 *    port（TCP 口），CQ setip 写 udp_port（UDP 口）；Bootloader / Recovery / 主固件
 *    共享同一套上电 IP；
 *  - 固件信息 + 升级状态（app_info / update_sta）：IAP 升级流程专用，与 net_cfg
 *    同扇区同记录，由本模块统一做读-改-写（16KB 整扇区擦除），避免双模块擦写
 *    所有权冲突。
 *
 * 记录布局与 Bootloader / Recovery 各自的 Drivers/BSP/Config/config_info.h 二进制
 * 兼容（布局契约见下方注释）。写入守卫语义（空/损坏自愈、升级中间态
 * 仅放行 net_cfg 字段更新、同值跳过擦写）见 doc/07_LDI与IAP配置解耦。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/* ---- 地址与常量定义（Sector1 配置区，仅本模块持有） ---- */
#define ADDR_CONFIG_SECTOR 0x08004000

#define APP_BOARD_NET_CFG_MAGIC 0x0d000721

/* ---- Sector1 记录布局 ---- */

/* 网络配置
 * 字段语义（TCP/UDP 端口彻底分离）：
 *  - port     = TCP 业务口（LDI 0AH 写 / IAP 0x02 写 / 0x01 报 / TCP Server 监听，
 *               LDI/IAP 生态，出厂默认 9528）；
 *  - udp_port = CQ UDP 业务口（CQ setip 写 / CQ UDP 通道读，出厂默认 20103）。
 * ip/mask/gw 两套口共享（setip 与 0AH 都写，现状不变）。 */
__attribute__((aligned(4))) typedef struct {
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gw[4];
    uint32_t port;
    uint32_t udp_port;
} app_board_net_cfg_t;

/* main app 信息 */
__attribute__((aligned(4))) typedef struct {
    uint32_t size;  /* Main App 字节长度 */
    uint32_t crc32; /* Main App CRC32 值 */
    char version[32];
} app_board_fw_info_t;

/* 升级状态机 */
typedef enum {
    APP_BOARD_UPDATED  = 0,
    APP_BOARD_UPDATING = 1,
    APP_BOARD_FAILED   = 2,
} app_board_update_sta_t;

/* 存储在 0x08004000 */
__attribute__((aligned(4))) typedef struct {
    uint32_t magic;                   /* 魔数，判断配置区是否有效 */
    uint32_t update_sta;              /* 升级状态机 */
    app_board_fw_info_t app_info;     /* main_app 状态 */
    app_board_net_cfg_t net_cfg;      /* 网络配置 */
    uint32_t config_crc;              /* 本结构体自身的 CRC32 校验 */
} app_board_sys_info_t;

/* ---- 布局契约：与 Bootloader / Recovery config_info.h 二进制兼容 ----
 * sys_info 72B = magic(0) + update_sta(4) + app_info(8, 40B) + net_cfg(48, 20B) +
 * config_crc(68)；net_cfg 内 port 偏移 12、udp_port 偏移 16（CRC 覆盖前 68B）。
 * 改错 = 三固件读同一扇区错位（Bootloader 判 App 损坏），
 * 详见 doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md §15。 */

/* ---- LDI 身份镜像（lane/cert/host 的第二副本，doc/07 §18） ----
 * 位于 Sector1 内 72B 系统记录之后的空闲区（偏移 0x100），36B：
 *   magic(0) + version(4) + lane(8,5B) + cert(13,8B) + host_ip(21,4B) +
 *   host_port(25,2B) + reserved(27,2B) + crc32(32)（CRC 覆盖前 32B）。
 * Bootloader / Recovery 只读前 72B，本区域对其不可见；但其整扇区擦除会连带
 * 清掉镜像——由 ldi_ctx_init 上电自愈从 Sector 11 记录重新派生（doc/07 §18.3）。 */
#define APP_BOARD_ID_MIRROR_OFFSET (0x100U) /**< 镜像区在 Sector1 内的字节偏移 */

#define APP_BOARD_ID_MIRROR_MAGIC (0x0D0F0A05U) /**< 独立魔数，不与 sys_info / LDI 记录混用 */
#define APP_BOARD_ID_MIRROR_VERSION (1U)        /**< 镜像布局版本，仅本值被接受 */

__attribute__((aligned(4))) typedef struct {
    uint32_t magic;       /**< APP_BOARD_ID_MIRROR_MAGIC；0xFFFFFFFF = 未写入 */
    uint32_t version;     /**< APP_BOARD_ID_MIRROR_VERSION；不匹配按无效处理 */
    uint8_t lane_hex[5];  /**< 车道 HEX 编号（0AH 帧头 lane_code） */
    uint8_t cert[8];      /**< 设备验证信息（0AH 帧头 cert_info） */
    uint8_t host_ip[4];   /**< 上位机 IP（0AH net.host_ip） */
    uint16_t host_port;   /**< 上位机端口（0AH net.host_port） */
    uint16_t reserved;    /**< 预留（保持 crc32 偏移 4 字节对齐） */
    uint32_t crc32;       /**< CRC32 覆盖 magic..reserved（不含自身） */
} app_board_id_mirror_t;

/**
 * @brief 读当前 Sector1 整记录（内存映射拷贝，无有效性判定）
 *
 * 供 IAP 命令读 app_info / update_sta 等非网络字段；网络配置读取请用
 * app_board_net_cfg_get。记录无效时读出内容为擦除态或损坏残留，调用方自行把关。
 */
void app_board_net_cfg_read(app_board_sys_info_t *out);

/**
 * @brief 读网络配置（记录有效时）
 *
 * @return 0 成功且 out 已填充；-1 记录无效（magic 不匹配或 CRC 错）
 */
int32_t app_board_net_cfg_get(app_board_net_cfg_t *out);

/**
 * @brief 同步设备 IP/掩码/网关/TCP 口/UDP 口 到内部 Flash（LDI 0AH / IAP 4B02 / CQ setip 时调用）
 *
 * 字段语义：port = TCP 业务口（LDI/IAP 生态），udp_port = CQ UDP
 * 业务口。调用方按协议口径填对应端口、另一端口保留现值（先 get，失败用出厂默认）——
 * LDI 0AH / IAP 4B02 写 port、保留 udp_port；CQ setip 写 udp_port、保留 port。
 *
 * 分支语义（见 doc/07_LDI与IAP配置解耦 02 §9/§10）：
 *  - 空扇区：完整初始化，镜像老 Bootloader Init_Config_Info 首次上电语义
 *    （magic + update_sta=updated + app_info 全 0 且 crc32=0xFFFFFFFF + net_cfg + CRC32）；
 *  - 损坏记录（magic 无效或 CRC 错）：按空扇区处理，完整初始化后写新配置（自愈）；
 *  - 有效记录且 update_sta != updated（升级中间态）：仅更新 net_cfg，update_sta 与
 *    app_info 原样保留（Bootloader 条件 D 判定不受影响）；
 *  - 有效记录且 update_sta == updated：仅更新 net_cfg（ip/mask/gw/port/udp_port），其余
 *    字段原样保留；与现扇区 net_cfg 完全一致（含 port 与 udp_port）时跳过擦写。
 *
 * @return 0 成功（含同值跳过）；<0 为擦/写错误码
 */
int32_t app_board_net_cfg_update(const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4],
                                 uint32_t port, uint32_t udp_port);

/**
 * @brief app_board_net_cfg_update 的镜像扩展版：同一次读-改-写里连带更新身份镜像
 *
 * 分支语义与 app_board_net_cfg_update 完全一致；额外：
 *  - mirror == NULL：保留现镜像（有且合法则原样带回重写，无/损坏则留白，
 *    由 ldi_ctx_init 上电自愈补写）——IAP 4B02 / CQ setip 等纯网络写入口用此口径；
 *  - mirror != NULL：本次擦写后镜像落为新值（0AH 网络+身份同帧下发时用，
 *    与网络字段一次擦除完成，避免二次擦写）。
 * 同值跳过判定加入镜像一致条件：net_cfg 与镜像都一致才跳过擦写。
 *
 * @return 0 成功（含同值跳过）；<0 为擦/写错误码
 */
int32_t app_board_net_cfg_update_ex(const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4],
                                    uint32_t port, uint32_t udp_port,
                                    const app_board_id_mirror_t *mirror);

/**
 * @brief 读 LDI 身份镜像（magic+version+CRC 全过才返回真值）
 *
 * @return true = 镜像有效且 out 已填充；false = 镜像空/损坏
 */
bool app_board_net_cfg_id_mirror_get(app_board_id_mirror_t *out);

/**
 * @brief 组包身份镜像（magic/version/CRC 填充，供 update_ex / id_mirror_update 调用方使用）
 */
void app_board_net_cfg_id_mirror_pack(app_board_id_mirror_t *m, const uint8_t lane[5],
                                      const uint8_t cert[8], const uint8_t host_ip[4],
                                      uint16_t host_port);

/**
 * @brief 更新 LDI 身份镜像（lane/cert/host_ip/host_port → Sector1 偏移 0x100）
 *
 * 读-改-写：擦除整扇区后，系统记录（72B）**按现读原样重写**（含升级中间态的
 * update_sta/app_info，Bootloader 条件 D 判定不受影响），镜像落为新值。镜像已
 * 与新值一致（且合法）时跳过擦写（不做磨损均衡）。
 *
 * @return 0 成功（含同值跳过）；<0 为擦/写错误码
 */
int32_t app_board_net_cfg_id_mirror_update(const uint8_t lane[5], const uint8_t cert[8],
                                           const uint8_t host_ip[4], uint16_t host_port);

/**
 * @brief 同步固件版本号到内部 Flash（app_info.version[32]，IAP 0x03「上报固件状态」用）
 *
 * 只允许改 app_info.version 并重算 config_crc，**绝不动** app_info.size / app_info.crc32 /
 * net_cfg / update_sta 的既有值（size/crc32 由 Recovery 升级流程写入，改动会触发 Bootloader
 * 条件 D 判 App 损坏）。
 *
 * 分支语义（与 app_board_net_cfg_update 同构，见 doc/07_LDI与IAP配置解耦 02 §13）：
 *  - 空扇区：完整初始化（magic + update_sta=updated + app_info 全 0 且 crc32=0xFFFFFFFF +
 *    net_cfg 全 0）+ 填 version。调用点安排在 sw_board_init()（含 ldi_ctx_init 从双库
 *    记录回写 net_cfg）之后，正常路径不会走到此分支；若走到（如 ldi_ctx_init 未回写），
 *    net_cfg 会被写成 0.0.0.0/port0，由后续 0AH / 上电同步恢复；
 *  - 损坏记录（magic 无效或 CRC 错）：按空扇区处理（自愈），完整初始化后填 version；
 *  - 有效记录且 update_sta != updated（升级中间态）：拒绝覆盖，返回 -1；
 *  - 有效记录且 update_sta == updated：仅更新 app_info.version，其余字段原样保留；与现扇区
 *    version 一致时跳过擦写（不做磨损均衡）。
 *
 * @param version 版本字符串（如 PROGRAM_CODE），须满足 strlen(version) < 32
 * @return 0 成功（含同值跳过）；<0 失败（-1 = 升级中间态拒绝覆盖，-2 = version 过长，
 *         其余为擦/写错误码）
 */
int32_t app_board_net_cfg_fw_version_update(const char *version);