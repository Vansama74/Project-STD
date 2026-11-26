/**
 * @file    app_board_net_cfg.c
 * @brief   板级系统配置存储（内部 Flash Sector 1, 0x08004000）
 *
 * app_board_sys_info_t 记录 = magic(4B) | update_sta(4B) | FWInfo(40B) | NetConfig(20B) | CRC32(4B)
 * 总长 72B（18 words），按 word 写入 Flash。
 * NetConfig 字段语义：port=TCP 业务口（LDI/IAP），udp_port=CQ UDP 业务口。
 *
 * 操作流程：
 *   读取 → 直接内存映射 (ADDR_CONFIG_SECTOR) → 空/完整性检查 → 使用
 *   更新 → 读-改-写：空/损坏完整初始化，有效仅改 net_cfg，升级中间态仅放行
 *          net_cfg 字段更新（update_sta/app_info 原样保留），同值跳过擦写
 *          （语义详见 doc/07_LDI与IAP配置解耦）
 */

#include "app_board_net_cfg.h"
#include "dev_flash_int.h"
#include "pl_crc.h"
#include "initcall.h"

#include <string.h>

#define BOARD_CFG_SIZE 0x4000 /* 16KB */

/* 镜像布局不变量（改前须复算，越限后果与判据见 doc/07 02 §18.2；业务代码不写断言，
 * doc/18_注释规范.md §2 第 10 条）：镜像起点不压 72B 记录、终点不出 16KB 扇区、
 * crc32 偏移 4 对齐（现 0x100/36B/32）。 */

/* ---- Sector1 Flash 存储实例 ---- */
static dev_flash_int_t g_board_cfg_flash = {
    .me        = {.capacity = BOARD_CFG_SIZE},
    .base_addr = ADDR_CONFIG_SECTOR,
    .sector    = PL_FLASH_SECTOR_1,
};

/* ---- ops 绑定（hw_dev_initcall） ---- */
static void _app_board_net_cfg_storage_init(void)
{
    g_board_cfg_flash.me.ops = &flash_int_ops;
}
hw_dev_initcall(_app_board_net_cfg_storage_init);

static dev_storage_t *_storage(void)
{
    return &g_board_cfg_flash.me;
}

/* ================================================================
 *  完整性检查
 * ================================================================ */

/** @brief Flash 擦除后全为 0xFF，magic + crc 均为 0xFFFFFFFF 表示从未写入 */
static bool _is_empty(const app_board_sys_info_t *info)
{
    return info->magic == 0xFFFFFFFF && info->config_crc == 0xFFFFFFFF;
}

/** @brief magic 匹配后校验 CRC32（覆盖整个结构体除去 crc32 字段自身） */
static bool _is_valid(const app_board_sys_info_t *info)
{
    if (info->magic != APP_BOARD_NET_CFG_MAGIC)
        return false;

    size_t len     = sizeof(app_board_sys_info_t) - sizeof(info->config_crc);
    uint32_t crc32 = pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)info, len);
    return info->config_crc == crc32;
}

/* ================================================================
 *  读取
 * ================================================================ */

/** @brief 内存映射整记录拷贝（无有效性判定，IAP 读 FWInfo/update_sta 用） */
void app_board_net_cfg_read(app_board_sys_info_t *out)
{
    memcpy(out, (const void *)ADDR_CONFIG_SECTOR, sizeof(*out));
}

/** @brief 读网络配置：记录有效返回 0，无效返回 -1 */
int32_t app_board_net_cfg_get(app_board_net_cfg_t *out)
{
    app_board_sys_info_t info;
    app_board_net_cfg_read(&info);

    if (!_is_valid(&info))
        return -1;

    memcpy(out, &info.net_cfg, sizeof(*out));
    return 0;
}

/* ================================================================
 *  写入（先擦除整扇区，再逐 word 编程）
 * ================================================================ */

static int32_t _erase(void)
{
    return dev_storage_erase(_storage(), 0, 0);
}

static int32_t _write(const app_board_sys_info_t *info)
{
    return dev_storage_write(_storage(), 0, (uint8_t *)info, sizeof(*info));
}

/* ================================================================
 *  LDI 身份镜像（Sector1 偏移 0x100 的第二副本，doc/07 §18）
 * ================================================================ */

/** @brief 镜像原始读（内存映射拷贝，无有效性判定，供读写路径内部用） */
static void _mirror_read_raw(app_board_id_mirror_t *out)
{
    memcpy(out, (const void *)(ADDR_CONFIG_SECTOR + APP_BOARD_ID_MIRROR_OFFSET), sizeof(*out));
}

/** @brief 镜像校验：magic + version + CRC32（覆盖 [0, offsetof(crc32))） */
static bool _mirror_valid(const app_board_id_mirror_t *m)
{
    if (m->magic != APP_BOARD_ID_MIRROR_MAGIC)
        return false;
    if (m->version != APP_BOARD_ID_MIRROR_VERSION)
        return false;

    uint32_t crc = pl_crc32_calc(pl_crc_get_handle(), (const uint8_t *)m,
                                 (uint32_t)offsetof(app_board_id_mirror_t, crc32));
    return m->crc32 == crc;
}

/** @brief 镜像写（目标偏移处须为擦除态） */
static int32_t _write_mirror(const app_board_id_mirror_t *m)
{
    return dev_storage_write(_storage(), APP_BOARD_ID_MIRROR_OFFSET, (uint8_t *)m, sizeof(*m));
}

/**
 * @brief Sector1 统一读-改-写：擦整扇区 → 系统记录 →（可选）镜像
 *
 * 所有 Sector1 擦写路径必须经此函数：任一路径漏带镜像都会把镜像抹成擦除态
 * （擦除是整 16KB 扇区的），镜像的双副本价值即被破坏。镜像参数为 NULL 表示
 * 该路径不拥有身份数据，镜像区域留白，由 ldi_ctx_init 上电自愈补写。
 */
static int32_t _rmw(const app_board_sys_info_t *info, const app_board_id_mirror_t *mirror)
{
    int32_t ret = _erase();
    if (ret < 0)
        return ret;
    ret = _write(info);
    if (ret < 0)
        return ret;
    if (mirror != NULL) {
        ret = _write_mirror(mirror);
        if (ret < 0)
            return ret;
    }
    return 0;
}

/** @brief 现镜像读+校验；合法时输出真值（供保留路径用） */
static bool _mirror_preserve(app_board_id_mirror_t *out)
{
    app_board_id_mirror_t m;
    _mirror_read_raw(&m);
    if (!_mirror_valid(&m))
        return false;
    *out = m;
    return true;
}

/**
 * @brief 同步设备 IP/掩码/网关/TCP 口/UDP 口 到内部 Flash（读-改-net_cfg-写）
 *
 * 分支语义：
 *  - 空扇区：完整初始化，镜像老 Bootloader Init_Config_Info 首次上电语义
 *    （magic + update_sta=updated + app_info 全 0 且 crc32=0xFFFFFFFF + net_cfg + CRC32），
 *    确保 Bootloader/Recovery 能读到该 IP。
 *  - 损坏记录（magic 无效或 CRC 错）：按空扇区处理，完整初始化后写新配置（自愈）。
 *    Bootloader 写记录永远是完整结构，不存在「magic 无效的升级中间态」，
 *    损坏记录无保护价值，覆盖它使 0AH/4B02 可自愈。
 *  - 有效记录且 update_sta != updated（升级中间态）：仅更新 net_cfg
 *    （ip/mask/gw/port/udp_port），update_sta 与 app_info 原样保留（Bootloader 条件 D
 *    判定不受影响）——改 IP 在中间态同样生效，IAP 0x01 上报立即反映新值。
 *  - 有效记录且 update_sta == updated：仅更新 net_cfg（ip/mask/gw/port/udp_port），其余字段
 *    原样保留；与现扇区 net_cfg 完全一致（含 port 与 udp_port）时跳过擦写（不做磨损均衡）。
 *
 * @return 0 成功（含同值跳过）；<0 为擦/写错误码
 */
int32_t app_board_net_cfg_update_ex(const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4],
                                    uint32_t port, uint32_t udp_port,
                                    const app_board_id_mirror_t *mirror)
{
    app_board_sys_info_t info;
    app_board_net_cfg_read(&info);

    /* 写前快照原 net_cfg，用于同值跳过比较 */
    app_board_net_cfg_t old_cfg;
    memcpy(&old_cfg, &info.net_cfg, sizeof(old_cfg));

    bool empty = _is_empty(&info);
    bool valid = !empty && _is_valid(&info);

    /* 镜像目标值裁定：调用方显式提供（0AH）→ 用新值；NULL（IAP/CQ/自愈）→
     * 保留现镜像（合法则原样带回，非法/空白则留白等上电自愈）。 */
    const app_board_id_mirror_t *mir_to_write = NULL;
    app_board_id_mirror_t preserved;
    if (mirror != NULL) {
        mir_to_write = mirror;
    } else if (_mirror_preserve(&preserved)) {
        mir_to_write = &preserved;
    }

    /* 升级中间态（magic/CRC 有效但 update_sta != updated）：update_sta/app_info
     * 原样保留（Bootloader 条件 D 判定依据），但 net_cfg 与升级判定无关——
     * 改 IP 仍应生效，否则中间态下 Sector1 网络配置被冻结、IAP 0x01 上报永远返回旧值。
     * 下方 memcpy(info.net_cfg...) 仅改网络字段，其余字段从 Flash 读出原样保留，
     * 升级流程语义不变。 */
    if (!valid) {
        /* 空/损坏扇区：完整初始化，与老 Bootloader Init_Config_Info 首次上电写入逐字节同构。
         * 注意：update_sta 必须为 updated（APP_BOARD_UPDATED），写 FAILED 会被 Bootloader
         * 判 App 损坏（见 doc/07 §8/§9） */
        info.magic      = APP_BOARD_NET_CFG_MAGIC;
        info.update_sta = APP_BOARD_UPDATED;
        memset(&info.app_info, 0, sizeof(info.app_info));
        info.app_info.crc32 = 0xFFFFFFFF;
    }

    memcpy(info.net_cfg.ip, ip, 4);
    memcpy(info.net_cfg.mask, mask, 4);
    memcpy(info.net_cfg.gw, gw, 4);
    info.net_cfg.port     = port;
    info.net_cfg.udp_port = udp_port;

    /* 同值跳过（Q3 语义扩展）：net_cfg（含 port 与 udp_port）与现扇区一致、且镜像
     * 目标值与现镜像一致（含「无镜像可保留」）才跳过擦写，返回成功（不做磨损均衡）。
     * 条件用 valid 而非 !empty：空/损坏时 old_cfg 为无效快照，必须走写路径。 */
    bool net_same = memcmp(&info.net_cfg, &old_cfg, sizeof(old_cfg)) == 0;
    bool mir_same = true;
    if (mir_to_write != NULL) {
        app_board_id_mirror_t cur_mir;
        _mirror_read_raw(&cur_mir);
        mir_same = _mirror_valid(&cur_mir) && memcmp(mir_to_write, &cur_mir, sizeof(cur_mir)) == 0;
    }
    if (valid && net_same && mir_same)
        return 0;

    info.config_crc = pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)&info,
                                    sizeof(app_board_sys_info_t) - sizeof(info.config_crc));

    return _rmw(&info, mir_to_write);
}

int32_t app_board_net_cfg_update(const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4],
                                 uint32_t port, uint32_t udp_port)
{
    return app_board_net_cfg_update_ex(ip, mask, gw, port, udp_port, NULL);
}

/**
 * @brief 同步固件版本号到内部 Flash（只改 app_info.version + 重算 config_crc）
 *
 * 红线：绝不改动 app_info.size / app_info.crc32 / net_cfg / update_sta 的既有值
 * （size/crc32 由 Recovery 升级流程写入，改动会触发 Bootloader 条件 D 判 App 损坏）。
 *
 * 分支语义（与 app_board_net_cfg_update 同构）：
 *  - 空扇区：完整初始化（magic + update_sta=updated + app_info 全 0 且 crc32=0xFFFFFFFF +
 *    net_cfg 全 0）+ 填 version。调用点安排在 sw_board_init()（含 ldi_ctx_init 从双库
 *    记录回写 net_cfg）之后，正常路径不会走到此分支；若走到（例如 ldi_ctx_init 未回写），
 *    net_cfg 会被写成 0.0.0.0/port0，需由后续 0AH / 上电同步恢复；
 *  - 损坏记录（magic 无效或 CRC 错）：按空扇区处理（自愈），完整初始化后填 version；
 *  - 有效记录且 update_sta != updated（升级中间态）：拒绝覆盖，返回 -1；
 *  - 有效记录且 update_sta == updated：仅更新 app_info.version（其余字段原样保留）；与现
 *    扇区 version 一致时跳过擦写（不做磨损均衡）。
 *
 * @return 0 成功（含同值跳过）；<0 失败（-1 = 升级中间态拒绝覆盖，-2 = version 过长
 *         （strlen(version) >= 32，不做截断，显式拒绝），其余为擦/写错误码）
 */
int32_t app_board_net_cfg_fw_version_update(const char *version)
{
    app_board_sys_info_t info;
    app_board_net_cfg_read(&info);

    /* version 长度校验：>= 32 字节无法完整落库，显式拒绝（不截断） */
    if (version == NULL || strlen(version) >= sizeof(info.app_info.version)) {
        return -2;
    }

    /* 写前快照原 version，用于同值跳过比较 */
    char old_version[sizeof(info.app_info.version)];
    memcpy(old_version, info.app_info.version, sizeof(old_version));

    bool empty = _is_empty(&info);
    bool valid = !empty && _is_valid(&info);

    /* 升级中间态保护：magic/CRC 有效但 update_sta != updated 的记录，
     * 主固件不得覆盖（等待 Bootloader 判定） */
    if (valid && info.update_sta != APP_BOARD_UPDATED) {
        return -1;
    }

    if (!valid) {
        /* 空/损坏扇区：完整初始化，与 app_board_net_cfg_update 空分支同构。
         * 注意：update_sta 必须为 updated（APP_BOARD_UPDATED），写 FAILED 会被 Bootloader
         * 判 App 损坏；net_cfg 置 0（0.0.0.0/port0）——调用点在 ldi_ctx_init 回写 net_cfg
         * 之后，正常不会走到此分支，若走到由后续 0AH / 上电同步恢复 */
        memset(&info, 0, sizeof(info));
        info.magic          = APP_BOARD_NET_CFG_MAGIC;
        info.update_sta     = APP_BOARD_UPDATED;
        info.app_info.crc32 = 0xFFFFFFFF;
    }

    /* 只动 version 字段：清零后拷贝（落库态尾部恒为 0，保证后续同值跳过可比较） */
    memset(info.app_info.version, 0, sizeof(info.app_info.version));
    memcpy(info.app_info.version, version, strlen(version));

    /* Q3 同构：version 与现扇区一致则跳过擦写，返回成功（不做磨损均衡）。
     * 条件用 valid 而非 !empty：空/损坏时 old_version 为无效快照，必须走写路径 */
    if (valid && memcmp(info.app_info.version, old_version, sizeof(old_version)) == 0) {
        return 0;
    }

    info.config_crc = pl_crc32_calc(pl_crc_get_handle(), (uint8_t *)&info,
                                    sizeof(app_board_sys_info_t) - sizeof(info.config_crc));

    /* 统一走 _rmw：擦整扇区后必须把合法镜像原样带回（NULL = 留白等上电自愈） */
    app_board_id_mirror_t preserved;
    return _rmw(&info, _mirror_preserve(&preserved) ? &preserved : NULL);
}

/* ================================================================
 *  LDI 身份镜像公开接口
 * ================================================================ */

bool app_board_net_cfg_id_mirror_get(app_board_id_mirror_t *out)
{
    if (out == NULL)
        return false;

    app_board_id_mirror_t m;
    _mirror_read_raw(&m);
    if (!_mirror_valid(&m))
        return false;

    *out = m;
    return true;
}

void app_board_net_cfg_id_mirror_pack(app_board_id_mirror_t *m, const uint8_t lane[5],
                                      const uint8_t cert[8], const uint8_t host_ip[4],
                                      uint16_t host_port)
{
    if (m == NULL)
        return;

    /* reserved 恒 0，CRC 覆盖 [0, offsetof(crc32)) */
    memset(m, 0, sizeof(*m));
    m->magic   = APP_BOARD_ID_MIRROR_MAGIC;
    m->version = APP_BOARD_ID_MIRROR_VERSION;
    memcpy(m->lane_hex, lane, sizeof(m->lane_hex));
    memcpy(m->cert, cert, sizeof(m->cert));
    memcpy(m->host_ip, host_ip, sizeof(m->host_ip));
    m->host_port = host_port;
    m->crc32 = pl_crc32_calc(pl_crc_get_handle(), (const uint8_t *)m,
                             (uint32_t)offsetof(app_board_id_mirror_t, crc32));
}

int32_t app_board_net_cfg_id_mirror_update(const uint8_t lane[5], const uint8_t cert[8],
                                           const uint8_t host_ip[4], uint16_t host_port)
{
    app_board_sys_info_t info;
    app_board_id_mirror_t mirror;

    app_board_net_cfg_read(&info);
    app_board_net_cfg_id_mirror_pack(&mirror, lane, cert, host_ip, host_port);

    /* 同值跳过：现镜像合法且逐字节一致 → 不擦写（不做磨损均衡）。
     * 系统记录不参与判定——本函数只负责镜像，记录由 _rmw 按现读原样带回
     * （含升级中间态的 update_sta/app_info，Bootloader 条件 D 判定不受影响）。 */
    app_board_id_mirror_t cur;
    _mirror_read_raw(&cur);
    if (_mirror_valid(&cur) && memcmp(&cur, &mirror, sizeof(cur)) == 0)
        return 0;

    return _rmw(&info, &mirror);
}