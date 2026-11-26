#include "main.h"
#include "pl_net_adapt.h"
#include "pl_eth.h"
#include "pl_rtc.h"
#include "pl_sys.h"
#include <string.h>
#include "SEGGER_RTT.h" /* 2026-11-20 临时根因诊断（定位后清理） */
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"

/* ---- 宏定义 ---- */
#define TIME_WAITING_FOR_INPUT (portMAX_DELAY)  /**< 等待接收数据的超时时间 */
#define ETHIF_TX_TIMEOUT (2000U)                /**< 等待发送完成的超时时间 */
#define INTERFACE_THREAD_STACK_SIZE (256 * 4)   /**< EthIf 线程栈大小 */
#define IFNAME0 's'                              /**< 网络接口名 */
#define IFNAME1 't'

#define ETH_DMA_TRANSMIT_TIMEOUT (20U)          /**< DMA 发送超时 */
#define ETH_TX_BUFFER_MAX        ((ETH_TX_DESC_CNT) * 2U) /**< 最大发送缓冲区数 */

/* ---- 零拷贝实现说明 ----
 * Rx：从 LwIP Rx 内存池分配缓冲区，直接交给 ETH HAL DMA 使用
 * Tx：从 LwIP 内存堆分配缓冲区，直接交给 ETH HAL DMA 使用
 *
 * 约束：
 *   1. ETH DMA 描述符必须连续排列（默认 4 个），
 *      可在 stm32xxxx_hal_conf.h 中重定义 ETH_RX_DESC_CNT / ETH_TX_DESC_CNT
 *   2. Rx 缓冲区数量应在 ETH_RX_DESC_CNT ~ 2*ETH_RX_DESC_CNT 之间
 *      所有 Rx 缓冲区使用相同大小 ETH_RX_BUF_SIZE
 *      地址和大小需对齐到 L1-CACHE 行（32 字节）
 */

/* ---- 类型定义 ---- */
typedef enum {
    RX_ALLOC_OK    = 0x00, /**< Rx 缓冲区分配成功 */
    RX_ALLOC_ERROR = 0x01  /**< Rx 缓冲区分配失败（池耗尽） */
} RxAllocStatusTypeDef;

typedef struct {
    struct pbuf_custom pbuf_custom;
    uint8_t buff[(ETH_RX_BUF_SIZE + 31) & ~31] __ALIGNED(32); /**< 32 字节对齐的 DMA 缓冲区 */
} RxBuff_t;

/* ---- 内存池声明 ---- */
#define ETH_RX_BUFFER_CNT 12U
LWIP_MEMPOOL_DECLARE(RX_POOL, ETH_RX_BUFFER_CNT, sizeof(RxBuff_t), "Zero-copy RX PBUF pool");

/* RX_POOL 的 memp 分配/释放必须互斥：lwIP memp 层非线程安全。
 * 分配发生在 EthIf 线程（RxAllocateCallback / 描述符重建），
 * 释放发生在 tcpip/应用线程（pbuf_free_custom）。
 * 台架 2026-11-19 定级：高负载并发 alloc/free 损坏自由链表
 * （链表成自环、所有描述符指向同一缓冲、网络永久死亡）。 */
/* 2026-11-20 定级：原 taskENTER_CRITICAL 在池操作里长期屏蔽中断——冻结现场
 * （ISPR 显示 TIM3/TIM4/ETH/SysTick 全部待决、PC 在 vListInsert 打转）证明
 * 屏蔽期间系统整体停摆。改为带优先级继承的互斥量：池操作即使死锁，中断仍被
 * 服务、SysTick/喂狗继续，不再整机冻结。 */
static osMutexId_t s_rx_pool_mtx;
#define RX_POOL_LOCK()                                                    \
    do {                                                                  \
        if (s_rx_pool_mtx != nullptr)                                     \
            (void)osMutexAcquire(s_rx_pool_mtx, osWaitForever);           \
    } while (0)
#define RX_POOL_UNLOCK()                                                  \
    do {                                                                  \
        if (s_rx_pool_mtx != nullptr)                                     \
            (void)osMutexRelease(s_rx_pool_mtx);                          \
    } while (0)

/* ETH DMA 控制互斥量：HAL_ETH_Stop_IT/Start_IT 与 DMAOMR.SR/轮询请求写
 * 均非线程安全——链路线程与 EthIf 重建并发调用会撕裂 DMA 状态机
 * （台架 2026-11-21 僵尸态定级：gState=STARTED 矛盾态 + 描述符环半武装，
 * 此后所有 Start_IT 被 HAL 拒绝，网口永久失联直到重启）。锁顺序固定：
 * ETH_CTRL 在外、RX_POOL 在内，全工程无反向嵌套。 */
static osMutexId_t s_eth_ctrl_mtx;
#define ETH_CTRL_LOCK()                                                   \
    do {                                                                  \
        if (s_eth_ctrl_mtx != nullptr)                                    \
            (void)osMutexAcquire(s_eth_ctrl_mtx, osWaitForever);          \
    } while (0)
#define ETH_CTRL_UNLOCK()                                                 \
    do {                                                                  \
        if (s_eth_ctrl_mtx != nullptr)                                    \
            (void)osMutexRelease(s_eth_ctrl_mtx);                         \
    } while (0)

/* ---- 变量定义 ---- */
static uint8_t RxAllocStatus;                           /**< Rx 缓冲区分配状态 */

/* ---- ETH DMA 故障恢复 ------------------------------------------------
 * STM32F4 的 ETH DMA 在下列错误后会停止收发，且 HAL 自身不会自愈：
 *   · RBUS / RPSS：RX 描述符不可用 / RX 进程停止（DMA 进入 Suspended）；
 *   · FBES（Fatal Bus Error）：HAL 关闭 NISE/AISE 中断并把 gState 置
 *     HAL_ETH_STATE_ERROR，此后 HAL_ETH_ReadData 因 gState != STARTED
 *     直接返回 HAL_ERROR —— RX 描述符永远不再被重新挂载。
 * 共同后果：ARP / ICMP 全部无响应（PING 不通），只有复位才恢复。
 *
 * 恢复动作（eth_dma_recover）放在 ethernetif_input 线程执行：
 * HAL_ETH_Stop_IT / Start_IT 内部各有 HAL_Delay(ETH_REG_WRITE_DELAY)，
 * 合计约 5ms 忙等，不能在中断上下文调用。ISR 只置标志 + 放信号量。 */
#define ETH_DMA_FAULT_MASK  (ETH_DMASR_FBES | ETH_DMASR_RPSS | ETH_DMASR_RBUS)

static volatile uint8_t g_eth_dma_fault;                /**< 1 = 需在任务上下文重启 ETH DMA */
static uint32_t s_eth_dmabmr;                           /**< 稳态 DMABMR 配置（软件复位后恢复） */
static uint32_t s_eth_maccr;                            /**< 稳态 MACCR 配置（软件复位后恢复） */
volatile uint32_t g_eth_tx_busy_retries;   /**< TX 描述符忙重试次数（调试器可读） */
volatile uint32_t g_eth_tx_drops;          /**< TX 有界重试仍失败而丢弃的帧数 */
volatile uint32_t g_eth_rx_recoveries;     /**< RX 看门狗触发 DMA 重启次数 */
volatile uint32_t g_eth_rx_pool_exhausts;  /**< RX_POOL 分配失败（池耗尽）次数 */
volatile uint32_t g_eth_rx_rbu_events;     /**< DMA 报 RBU（接收缓冲不可用）次数 */
volatile uint32_t g_eth_tx_wedges;         /**< TX 挂起（TPS=6/TBUS）看门狗恢复次数 */
volatile uint32_t g_eth_last_rx_tick;      /**< 最近一次帧完成时刻（看门狗判"RX 卡死"用） */
volatile uint32_t g_eth_reboot_recoveries; /**< 重症挂起触发受控整机重启的次数（启动回读 RTC 标记） */

/* ---- 2026-11-20 临时根因诊断（RTT，定位后清理） ----
 * 目的：区分"网口死亡"的三种候选——①判据盲区不命中；②tcpip 线程死；
 * ③重启后 ETH/netif 没起来。th/eh 不涨即对应对线程死。 */
volatile uint32_t g_eth_tcpip_hb; /**< tcpip 线程心跳：EthWdg 每 30s 投递回调递增 */
volatile uint32_t g_ethif_hb;     /**< EthIf 线程心跳：每次唤醒递增 */
volatile uint32_t g_ethwdg_hb;    /**< EthWdg 任务心跳：每轮轮询递增（喂狗门控判据） */
static volatile uint8_t s_eth_degraded; /**< 连续重启超限后降级（不再整机重启） */

static void eth_diag_tcpip_cb(void *ctx)
{
    (void)ctx;
    g_eth_tcpip_hb++;
}
static uint32_t s_wedge_since;             /**< 首次检出挂起的 tick（0=无挂起；RX 恢复后清零） */
static uint32_t s_wedge_rx_snapshot;       /**< 首次检出时的 last_rx_tick 快照（判"RX 真恢复"用） */
static uint16_t s_wedge_first_reason;      /**< 首次检出的原因位（受控重启标记用，防恢复动作闪动丢原因） */
static uint32_t s_last_rebuild_tick;       /**< 最近一次完整重建的 tick（重建限频） */

/* 重症时间线（2026-11-19 裁决：5s 预算内恢复） */
#define ETH_WEDGE_REBUILD_MS (2000U)       /**< 挂起持续 ≥2s：升级 SR 完整重建 */
#define ETH_WEDGE_REBOOT_MS  (4000U)       /**< 挂起持续 ≥4s：受控整机重启（+启动 ~1s ≈ 5s 恢复） */
#define ETH_REBUILD_MIN_INTERVAL_MS (1000U) /**< 两次完整重建的最小间隔：防"重建→IRQ 再触发→重建"自持循环（2026-11-20 冻结定级） */

/* 重症重启标记：RTC 备份寄存器（VBAT 域，复位不丢）。
 * 占用约定（2026-11-20 定案）：DR0 = Recovery 强制升级、DR1 = Bootloader 崩溃计数
 * （>MAX_CRASH_COUNT 即判崩溃过频跳 Recovery）。**EthWdg 禁用 DR0/DR1**
 * ——曾用 DR1 写 0xA5xxxxxx 标记，Bootloader 按崩溃计数读取后直接判死跳
 * Recovery（网口"死"但 ping 通的现场形态）。 */
#define ETH_WEDGE_RTC_MARK_REG  RTC_BKP_DR4
#define ETH_WEDGE_RTC_DMASR_REG RTC_BKP_DR5
#define ETH_WEDGE_RTC_CNT_REG   RTC_BKP_DR6 /**< 连续受控重启计数（健康 10min 后清零） */
#define ETH_WEDGE_RTC_MAGIC     (0xA5U)
#define ETH_WEDGE_MAX_REBOOTS   (3U)        /**< 连续重启上限，超过转降级（不无限重启） */

/* ---- 看门狗事件环（只记录真实故障，J-Link 事后导出：tool/eth_wedge_dump） ---- */
#define ETH_WEDGE_RING_SIZE (64U)
#define ETH_WEDGE_RX_UNREAD  (1U << 0)  /**< RPS=3 挂起且存在未读帧描述符 */
#define ETH_WEDGE_RBUS       (1U << 1)  /**< 接收缓冲不可用 */
#define ETH_WEDGE_RPSS       (1U << 2)  /**< 接收进程停止 */
#define ETH_WEDGE_TUS        (1U << 3)  /**< 发送欠载 */
#define ETH_WEDGE_TBUS_BUSY  (1U << 4)  /**< 发送缓冲不可用且有真在途发送（OWN=1） */
#define ETH_WEDGE_POOL_LATCH (1U << 5)  /**< 池状态锁存 ERROR */
#define ETH_WEDGE_FATAL      (1U << 6)  /**< 致命总线错误（FBES） */
#define ETH_WEDGE_RX_CLOSING (1U << 7)  /**< RPS=4 关闭描述符卡死（无帧完成>2s） */
#define ETH_WEDGE_REBOOT     (1U << 8)  /**< 本次记录来自重症受控重启（RTC 标记回读） */
#define ETH_WEDGE_RX_STOPPED (1U << 9)  /**< RX 进程停止（RPS=0）且 ≥2 次轮询无新帧：僵尸态（台架 2026-11-21 定级） */

typedef struct {
    uint32_t tick;          /**< RTOS tick（ms，/1000 = RunningTime 秒） */
    uint32_t dmasr;         /**< DMASR 快照 */
    uint16_t reason;        /**< 触发原因位（ETH_WEDGE_*，bit8=重症重启回读，bit9=RX 僵尸态） */
    uint8_t  alloc_status;  /**< RxAllocStatus */
    uint8_t  pool_free;     /**< RX_POOL 空闲计数（尽力而为） */
    uint32_t recoveries;    /**< g_eth_rx_recoveries 快照 */
} eth_wedge_evt_t;          /**< 16 字节/条，环共 1KB */

volatile eth_wedge_evt_t g_eth_wedge_ring[ETH_WEDGE_RING_SIZE];
volatile uint32_t        g_eth_wedge_wr; /**< 环形写入位置（满则覆盖最旧） */

/** @brief 记录一次真实看门狗事件（空闲误判不进入） */
static void eth_wedge_record(uint16_t reason, uint32_t dmasr)
{
    eth_wedge_evt_t *ev = (eth_wedge_evt_t *)&g_eth_wedge_ring[g_eth_wedge_wr % ETH_WEDGE_RING_SIZE];
    g_eth_wedge_wr++;
    ev->tick         = osKernelGetTickCount();
    ev->dmasr        = dmasr;
    ev->reason       = reason;
    ev->alloc_status = RxAllocStatus;
    ev->recoveries   = g_eth_rx_recoveries;
    /* 池空闲计数（尽力而为，只走链表计数；memp_tab_RX_POOL 本身即自由链表头） */
    uint8_t n = 0U;
    RX_POOL_LOCK();
    struct memp *m = memp_tab_RX_POOL;
    while ((m != NULL) && (n < ETH_RX_BUFFER_CNT)) {
        n++;
        m = m->next;
    }
    RX_POOL_UNLOCK();
    ev->pool_free = n;
}

/** @brief 重症受控重启标记写入：{magic(8) | reason低字节(16) | 0(16)} + DMASR 全量 */
static void eth_wedge_mark_reboot(uint16_t reason, uint32_t dmasr)
{
    pl_rtc_handle_t h = pl_rtc_get_handle();
    /* 标记格式（2026-11-21 扩宽）：[31:24]=0xA5 魔数，[23:8]=16 位原因。
     * 原 8 位原因字段（[23:16]）容不下 bit9（ETH_WEDGE_RX_STOPPED），
     * 扩宽后全部 ETH_WEDGE_* 位（0~9）均可入标记。 */
    pl_rtc_bkup_write(h, ETH_WEDGE_RTC_MARK_REG,
            (ETH_WEDGE_RTC_MAGIC << 24) | ((uint32_t)(reason & 0xFFFFU) << 8));
    pl_rtc_bkup_write(h, ETH_WEDGE_RTC_DMASR_REG, dmasr);
}

/** @brief 启动回读重症标记：转事件环首条（reason 带 REBOOT 位）+ 计数，并清除 */
static void eth_wedge_consume_reboot_mark(void)
{
    pl_rtc_handle_t h = pl_rtc_get_handle();
    uint32_t mark = pl_rtc_bkup_read(h, ETH_WEDGE_RTC_MARK_REG);
    if ((mark >> 24) != ETH_WEDGE_RTC_MAGIC) {
        return;
    }
    uint32_t dmasr = pl_rtc_bkup_read(h, ETH_WEDGE_RTC_DMASR_REG);
    SEGGER_RTT_printf(0, "R t=%u mark=%08X dsr=%08X\n",
            (unsigned)osKernelGetTickCount(), (unsigned)mark, (unsigned)dmasr);
    eth_wedge_record((uint16_t)(((mark >> 8) & 0xFFFFU) | ETH_WEDGE_REBOOT), dmasr);
    g_eth_reboot_recoveries++;
    /* 消费后清除，避免下次正常重启误记 */
    pl_rtc_bkup_write(h, ETH_WEDGE_RTC_MARK_REG, 0U);
    pl_rtc_bkup_write(h, ETH_WEDGE_RTC_DMASR_REG, 0U);
}

/** @brief 是否存在"未读帧"描述符：OWN=0、未挂缓冲、DESC0 含帧标志/长度 */
static uint32_t eth_rx_has_unread(void);

/** @brief 重症受控重启：原因写入 RTC 备份寄存器（复位不丢） */
static void eth_wedge_mark_reboot(uint16_t reason, uint32_t dmasr);

/** @brief 启动时回读重症标记 → 事件环 + 计数，并清除 */
static void eth_wedge_consume_reboot_mark(void);

/** @brief ETH 看门狗任务（独立线程，500ms 轮询 DMA 状态并恢复） */
static void eth_watchdog_task(void *argument);

/* 看门狗任务静态资源（不占 FreeRTOS 堆） */
static StaticTask_t s_eth_wdg_tcb;
static uint32_t s_eth_wdg_stack[INTERFACE_THREAD_STACK_SIZE / 4];
osThreadId_t g_eth_wdg_tid;   /**< 调试器可读：NULL=创建失败 */

ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT];      /**< ETH Rx DMA 描述符表 */
ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT];      /**< ETH Tx DMA 描述符表 */

static uint32_t eth_rx_has_unread(void)
{
    for (uint32_t i = 0U; i < ETH_RX_DESC_CNT; i++) {
        ETH_DMADescTypeDef *d = &DMARxDscrTab[i];
        if ((READ_BIT(d->DESC0, ETH_DMARXDESC_OWN) == 0U)
                && (d->BackupAddr0 == 0U)
                && ((d->DESC0 & (ETH_DMARXDESC_FS | ETH_DMARXDESC_LS)) != 0U)) {
            return 1U;
        }
    }
    return 0U;
}

/** @brief 是否存在真在途 TX 描述符（OWN=1，DMA 持有）。
 *
 *  注意不能用 TxDescList.BuffersInUse：它表示"未释放"（完成的包要等下一次
 *  ReleaseTxPacket 才清零），空闲时恒 ≥1，会把发送空闲常态误判成在途。 */
static uint32_t eth_tx_has_inflight(void)
{
    for (uint32_t i = 0U; i < ETH_TX_DESC_CNT; i++) {
        if (READ_BIT(DMATxDscrTab[i].DESC0, ETH_DMATXDESC_OWN) != 0U) {
            return 1U;
        }
    }
    return 0U;
}

osSemaphoreId RxPktSemaphore = NULL;        /**< 收到数据包信号量 */
osSemaphoreId TxPktSemaphore = NULL;        /**< 发送完成信号量 */

ETH_HandleTypeDef heth;                     /**< 全局 ETH 句柄 */
ETH_TxPacketConfig TxConfig;                /**< 发送包配置 */


/* ---- HAL 回调函数 ---- */
void pbuf_free_custom(struct pbuf *p);

/** @brief ETH RX 完成回调：释放信号量通知 ethernetif_input 线程取包 */
void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *handlerEth)
{
    (void)handlerEth;
    g_eth_last_rx_tick = osKernelGetTickCount();
    osSemaphoreRelease(RxPktSemaphore);
}

/** @brief ETH TX 完成回调：释放信号量通知发送完成 */
void HAL_ETH_TxCpltCallback(ETH_HandleTypeDef *handlerEth)
{
    (void)handlerEth;
    osSemaphoreRelease(TxPktSemaphore);
}

/** @brief ETH DMA 错误回调：标记故障并唤醒 ethernetif_input 线程重启 DMA
 *
 * HAL 在中断里已按分支归档错误码：非致命分支写 ETS|RWTS|RBUS|AIS 并清对应位，
 * FBES 分支写 FBES|TPS|RPS 并关 NISE/AISE。RPSS(bit8) 两种分支都不清，
 * 因此从 DMASR 直接补读一次。
 */
void HAL_ETH_ErrorCallback(ETH_HandleTypeDef *handlerEth)
{
    uint32_t err = HAL_ETH_GetDMAError(handlerEth);
    err |= (uint32_t)(handlerEth->Instance->DMASR & ETH_DMASR_RPSS);

    if ((err & ETH_DMASR_RBUS) != 0U) {
        g_eth_rx_rbu_events++;
    }

    if ((err & ETH_DMA_FAULT_MASK) == 0U) {
        return; /* ETS / RWTS 等非致命状态：HAL 已清位，无需重启 DMA */
    }

    g_eth_dma_fault = 1U;
    osSemaphoreRelease(RxPktSemaphore);
}

/* ================================================================
 *  底层驱动接口（LwIP → ETH MAC）
 * ================================================================ */

static bool g_eth_hw_ready = false; /**< ETH MAC 硬件初始化完成标志 */
static pl_phy_link_fn_t g_phy_get_link = NULL; /**< Device 层注册的 PHY 链路状态查询回调 */

void pl_eth_set_phy_link_fn(pl_phy_link_fn_t fn)
{
    g_phy_get_link = fn;
}

/**
 * @brief   ETH MAC + PHY 硬件初始化（initcall 阶段调用）
 *
 * 执行 HAL_ETH_Init（触发 MspInit 配置 GPIO/时钟/NVIC），不涉及具体 PHY 型号。
 * 幂等：若已初始化则跳过。low_level_init() 中也会检测此标志跳过硬件的重复初始化。
 *
 * @note  PHY 初始化由 Device 层在 dev_eth_init() 中调用 pl_eth_mac_hw_init 后完成。
 *
 * MAC 地址基于芯片 UID 生成（OUI 本地管理位 + 5 字节 UID）。
 */
void pl_eth_mac_hw_init(void)
{
    if (g_eth_hw_ready) return;

    static uint8_t mac[6];
    uint32_t uid0        = HAL_GetUIDw0();
    uint32_t uid1        = HAL_GetUIDw1();
    mac[0]               = 0x02; /* 本地管理地址 */
    mac[1]               = (uid0 >> 24) & 0xFF;
    mac[2]               = (uid0 >> 16) & 0xFF;
    mac[3]               = (uid0 >> 8) & 0xFF;
    mac[4]               = (uid0 >> 0) & 0xFF;
    mac[5]               = (uid1 >> 24) & 0xFF;

    heth.Instance            = ETH;
    heth.Init.MACAddr        = mac;
    heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
    heth.Init.TxDesc         = DMATxDscrTab;
    heth.Init.RxDesc         = DMARxDscrTab;
    heth.Init.RxBuffLen      = 1536;

    HAL_ETH_Init(&heth);
    /* 记录初始 DMABMR（链接任务首轮 link-up 后会再保存稳态 MACCR+DMABMR） */
    s_eth_dmabmr = heth.Instance->DMABMR;
    s_eth_maccr  = heth.Instance->MACCR;

    memset(&TxConfig, 0, sizeof(ETH_TxPacketConfig));
    TxConfig.Attributes   = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
    TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
    TxConfig.CRCPadCtrl   = ETH_CRC_PAD_INSERT;

    g_eth_hw_ready = true;
}

/**
 * @brief   LwIP 网络接口初始化（由 netif_add 回调调用）
 *
 * 仅执行 LwIP/RTOS 相关配置：内存池、netif 参数、信号量、EthIf 线程。
 * 硬件初始化由 pl_eth_mac_hw_init() 在 initcall 阶段提前完成，
 * 此处检测 g_eth_hw_ready 做幂等保护。
 */
static void low_level_init(struct netif *netif)
{
    osThreadAttr_t attributes;
    uint32_t duplex, speed = 0;
    int32_t PHYLinkState = 0;
    ETH_MACConfigTypeDef MACConf = {0};

    /* 硬件可能已由 initcall 初始化，幂等保护 */
    if (!g_eth_hw_ready)
        pl_eth_mac_hw_init();

    /* 初始化零拷贝 RX 内存池 */
    LWIP_MEMPOOL_INIT(RX_POOL);

    /* RX 池互斥量（优先级继承）：须在 eth 启动、任何分配/释放之前创建 */
    if (s_rx_pool_mtx == nullptr) {
        const osMutexAttr_t rx_pool_mtx_attr = {
            .name      = "rx_pool_mtx",
            .attr_bits = osMutexPrioInherit,
        };
        s_rx_pool_mtx = osMutexNew(&rx_pool_mtx_attr);
    }

    /* ETH DMA 控制互斥量：须在任何 Stop/Start 调用（含下方启动分支）之前创建 */
    if (s_eth_ctrl_mtx == nullptr) {
        const osMutexAttr_t eth_ctrl_mtx_attr = {
            .name      = "eth_ctrl_mtx",
            .attr_bits = osMutexPrioInherit,
        };
        s_eth_ctrl_mtx = osMutexNew(&eth_ctrl_mtx_attr);
    }

#if LWIP_ARP || LWIP_ETHERNET

    /* 设置 MAC 地址长度 */
    netif->hwaddr_len = ETH_HWADDR_LEN;

    /* 设置 MAC 地址 */
    netif->hwaddr[0] = heth.Init.MACAddr[0];
    netif->hwaddr[1] = heth.Init.MACAddr[1];
    netif->hwaddr[2] = heth.Init.MACAddr[2];
    netif->hwaddr[3] = heth.Init.MACAddr[3];
    netif->hwaddr[4] = heth.Init.MACAddr[4];
    netif->hwaddr[5] = heth.Init.MACAddr[5];

    /* MTU */
    netif->mtu = ETH_MAX_PAYLOAD;

    /* 接受广播和 ARP 流量 */
#if LWIP_ARP
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
#else
    netif->flags |= NETIF_FLAG_BROADCAST;
#endif /* LWIP_ARP */

    /* 创建信号量：RX 用于通知帧到达，TX 用于通知发送完成 */
    RxPktSemaphore = osSemaphoreNew(1, 0, NULL);
    TxPktSemaphore = osSemaphoreNew(1, 0, NULL);

    /* 创建 EthIf 线程处理 MAC 收发 */
    memset(&attributes, 0x0, sizeof(osThreadAttr_t));
    attributes.name       = "EthIf";
    attributes.stack_size = INTERFACE_THREAD_STACK_SIZE;
    attributes.priority   = osPriorityRealtime;
    /* 判空：EthIf 起不来 = 全部网络协议（LDI/IAP/CQ/GZ_OL/YN_OL）无声失效 */
    osThreadId_t ethif_tid = osThreadNew(ethernetif_input, netif, &attributes);
    if (ethif_tid == nullptr) {
        /* 创建失败（堆耗尽）：无以太网输入线程；保留判空点供调试器定位 */
    }

    /* 创建 ETH 看门狗任务：独立于 EthIf 线程轮询 DMA 状态并恢复。
     * 台架 2026-11-19 定级：看门狗不能寄宿在 EthIf 线程——风暴过载会把
     * 该线程堵在 tcpip 邮箱投递上，看门狗随之失效（RBUS 置位 8s+ 无恢复、
     * 事件环零记录）。本任务永不接触网络数据路径，只读寄存器+写恢复。
     * 静态创建：不依赖 FreeRTOS 堆（堆耗尽时动态创建会静默失败）。 */
    memset(&attributes, 0x0, sizeof(osThreadAttr_t));
    attributes.name       = "EthWdg";
    /* AboveNormal（2026-11-20 定级）：Low 在 fuzz/风暴负载下会被持续就绪的
     * Normal/Realtime 任务饿死——台架实测 RBUS 楔死时 g_eth_rx_recoveries=0
     * （看门狗从未运行）。看门狗只做 500ms 寄存器轮询+恢复，开销极小。 */
    attributes.priority   = osPriorityAboveNormal;
    attributes.cb_mem     = &s_eth_wdg_tcb;
    attributes.cb_size    = sizeof(s_eth_wdg_tcb);
    attributes.stack_mem  = s_eth_wdg_stack;
    attributes.stack_size = sizeof(s_eth_wdg_stack);
    g_eth_wdg_tid = osThreadNew(eth_watchdog_task, NULL, &attributes);
    /* 判空：看门狗起不来 = 网络停摆无自愈，调试器读 g_eth_wdg_tid 定位 */
    /* 重症重启标记回读已移入看门狗任务首次运行（RTC 依赖，此处时机过早） */

    /* PHY 已在 pl_eth_mac_hw_init() 初始化，此处仅检测链路状态 */
    {
        PHYLinkState = g_phy_get_link();

        /* 根据链路状态配置 MAC 双工/速率 */
        if (PHYLinkState == PL_ETH_LINK_DOWN) {
            netif_set_link_down(netif);
            netif_set_down(netif);
        } else {
            switch (PHYLinkState) {
                case PL_ETH_LINK_100M_FD:
                    duplex = ETH_FULLDUPLEX_MODE;
                    speed  = ETH_SPEED_100M;
                    break;
                case PL_ETH_LINK_100M_HD:
                    duplex = ETH_HALFDUPLEX_MODE;
                    speed  = ETH_SPEED_100M;
                    break;
                case PL_ETH_LINK_10M_FD:
                    duplex = ETH_FULLDUPLEX_MODE;
                    speed  = ETH_SPEED_10M;
                    break;
                case PL_ETH_LINK_10M_HD:
                    duplex = ETH_HALFDUPLEX_MODE;
                    speed  = ETH_SPEED_10M;
                    break;
                default:
                    duplex = ETH_FULLDUPLEX_MODE;
                    speed  = ETH_SPEED_100M;
                    break;
            }

            /* 更新 MAC 配置并启动 */
            HAL_ETH_GetMACConfig(&heth, &MACConf);
            MACConf.DuplexMode = duplex;
            MACConf.Speed      = speed;
            HAL_ETH_SetMACConfig(&heth, &MACConf);

            ETH_CTRL_LOCK();
            HAL_ETH_Start_IT(&heth);
            /* 保存稳态寄存器配置：eth_dma_recover 软件复位后据此恢复 */
            s_eth_dmabmr = heth.Instance->DMABMR;
            s_eth_maccr  = heth.Instance->MACCR;
            netif_set_up(netif);
            netif_set_link_up(netif);
            ETH_CTRL_UNLOCK();
        }
    }
#endif /* LWIP_ARP || LWIP_ETHERNET */
}

/**
 * @brief   底层发送：将 pbuf 链式数据通过 DMA 发送到 ETH MAC
 * @param   netif  LwIP 网络接口
 * @param   p      待发送的 pbuf 链
 * @return  ERR_OK 发送成功；ERR_BUF DMA 忙时等待重试；ERR_IF 其他错误
 *
 * @note    若 DMA 队列满返回 ERR_MEM 可能导致协议栈行为异常，
 *          本实现通过 TxPktSemaphore 等待描述符可用后重试。
 */
static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    uint32_t i                                  = 0U;
    struct pbuf *q                              = NULL;
    err_t errval                                = ERR_OK;
    ETH_BufferTypeDef Txbuffer[ETH_TX_DESC_CNT] = {0};

    memset(Txbuffer, 0, ETH_TX_DESC_CNT * sizeof(ETH_BufferTypeDef));

    /* 遍历 pbuf 链，填充 DMA 发送缓冲区描述符 */
    for (q = p; q != NULL; q = q->next) {
        if (i >= ETH_TX_DESC_CNT)
            return ERR_IF;

        Txbuffer[i].buffer = q->payload;
        Txbuffer[i].len    = q->len;

        if (i > 0) {
            Txbuffer[i - 1].next = &Txbuffer[i];
        }

        if (q->next == NULL) {
            Txbuffer[i].next = NULL;
        }

        i++;
    }

    TxConfig.Length   = p->tot_len;
    TxConfig.TxBuffer = Txbuffer;
    TxConfig.pData    = p;

    pbuf_ref(p);

    /* 任务上下文先回收已完成包（正常路径不再只依赖忙分支回收） */
    (void)HAL_ETH_ReleaseTxPacket(&heth);

    uint8_t tries = 0U;
    for (;;) {
        if (HAL_ETH_Transmit_IT(&heth, &TxConfig) == HAL_OK) {
            errval = ERR_OK;
            break;
        }
        if ((HAL_ETH_GetError(&heth) & HAL_ETH_ERROR_BUSY) == 0U) {
            pbuf_free(p);
            return ERR_IF;
        }
        /* DMA 忙：等待完成信号并回收，最多重试 3 次（约 6s），绝不无限阻塞 */
        (void)osSemaphoreAcquire(TxPktSemaphore, ETHIF_TX_TIMEOUT);
        (void)HAL_ETH_ReleaseTxPacket(&heth);
        g_eth_tx_busy_retries++;
        if (++tries >= 3U) {
            pbuf_free(p);
            g_eth_tx_drops++;
            return ERR_BUF;
        }
    }
    return errval;
}

/**
 * @brief   底层接收：从 ETH HAL 读取已分配的 pbuf
 * @param   netif  LwIP 网络接口
 * @return  指向接收包的 pbuf 指针；NULL 表示无数据或内存分配失败
 */
static struct pbuf *low_level_input(struct netif *netif)
{
    (void)netif;
    struct pbuf *p = NULL;

    if (RxAllocStatus == RX_ALLOC_OK) {
        HAL_ETH_ReadData(&heth, (void **)&p);
    }

    return p;
}

/**
 * @brief   EthIf 线程：阻塞等待 RX 信号量，取出 pbuf 投递给 LwIP 协议栈
 * @param   argument  指向 netif 结构的指针
 *
 * 循环内：
 *   1. osSemaphoreAcquire 等待帧到达
 *   2. low_level_input 提取 pbuf
 *   3. netif->input 投递到 LwIP 协议栈
 *   4. 如果池耗尽（RxAllocStatus == ERROR），HAL 回调会释放信号量触发本线程
 *      → 调用 HAL_ETH_GetRxDataBuffer 重建描述符
 */
/**
 * @brief  重启 ETH DMA（仅任务上下文调用）
 *
 * HAL 在 Fatal Bus Error 分支把 gState 置 HAL_ETH_STATE_ERROR 并关闭
 * NISE/AISE，而 HAL_ETH_Stop_IT 只接受 STARTED ⇒ 先把状态退回 STARTED
 * 再走标准 Stop/Start；Start 内部重建 RX 描述符并重新打开中断。
 * 不触碰 RxAllocStatus：池耗尽时由 pbuf_free_custom 在缓冲归还后置回
 * RX_ALLOC_OK 并再次唤醒本线程，保持既有恢复链路。
 */
static void eth_dma_recover(void)
{
    /* 全程持 ETH 控制锁：与链路线程/看门狗轻量恢复的 Stop/Start/SR 写串行，
     * 防止并发撕裂 DMA 状态机（台架 2026-11-21 僵尸态定级）。锁内另取
     * RX_POOL 锁回收池缓冲，锁顺序 ETH_CTRL → RX_POOL 固定。 */
    ETH_CTRL_LOCK();
    if (heth.gState == HAL_ETH_STATE_ERROR) {
        heth.gState = HAL_ETH_STATE_STARTED;
    }
    if (heth.gState == HAL_ETH_STATE_STARTED) {
        (void)HAL_ETH_Stop_IT(&heth);
    }
    /* DMA/MAC 软件复位：HAL 的 Stop/Start 不含 SR 复位，卡死状态机
     * （RPS=4 关闭描述符卡死等）能扛过重建（台架 2026-11-19 定级：
     * 无 SR 时 16 次重建全部无效）。复位后寄存器回默认值，需恢复稳态配置。 */
    SET_BIT(heth.Instance->DMABMR, ETH_DMABMR_SR);
    uint32_t t0 = HAL_GetTick();
    while (READ_BIT(heth.Instance->DMABMR, ETH_DMABMR_SR) > 0U) {
        if ((HAL_GetTick() - t0) > 100U) {
            break; /* 复位超时（异常硅片态）：放弃等待，继续重建流程 */
        }
    }
    heth.Instance->DMABMR = s_eth_dmabmr;
    heth.Instance->MACCR  = s_eth_maccr;
    /* 描述符链基址恢复（2026-11-26 定级）：SR 软件复位会把 DMARDLAR/DMATDLAR
     * 清零，而 HAL_ETH_Start_IT 不重写它们（只有 HAL_ETH_Init 写）——此前重建
     * 后 DMA 从地址 0 取描述符，必然总线错误（EBS 位组）→ RX 停 → 重建"永远
     * 无效"→ 只能受控重启（现场故障板断联的直接原因）。补上后重建可原地复活
     * 深停态 DMA（≤2s），TCP 连接经 lwIP 重传得以保留，无需重启。 */
    WRITE_REG(heth.Instance->DMATDLAR, (uint32_t)heth.Init.TxDesc);
    WRITE_REG(heth.Instance->DMARDLAR, (uint32_t)heth.Init.RxDesc);
    /* 回收仍挂在 RX 描述符上的零拷贝池缓冲：Start 重建描述符环不会自动归还，
     * 不回收则每次恢复泄漏最多 ETH_RX_DESC_CNT 个池缓冲，几次后池耗尽。
     * 临界区：与 tcpip/应用线程的释放互斥。 */
    for (uint32_t i = 0U; i < ETH_RX_DESC_CNT; i++) {
        uint32_t buff = DMARxDscrTab[i].BackupAddr0;
        if (buff != 0U) {
            DMARxDscrTab[i].BackupAddr0 = 0U;
            RX_POOL_LOCK();
            LWIP_MEMPOOL_FREE(RX_POOL, (struct pbuf_custom *)(buff - offsetof(RxBuff_t, buff)));
            RX_POOL_UNLOCK();
        }
    }
    /* 台架 2026-11-19 定级：回收是直接归还，不会走 pbuf_free_custom 的
     * RxAllocStatus 复位分支 —— 状态锁存会永久停在 ERROR，看门狗与接收闸门
     * 双重被挡导致网络死亡。此处归还后必须同步复位锁存。 */
    if (RxAllocStatus != RX_ALLOC_OK) {
        RxAllocStatus = RX_ALLOC_OK;
    }
    /* 半帧残留：已组链的 pbuf 需经 pbuf_free（pbuf_free_custom 回池）归还，
     * 直接置 NULL 会永久泄漏池缓冲（每次重建最多 3 块，反复重建累积，
     * 台架 2026-11-20 定级）。 */
    if (heth.RxDescList.pRxStart != NULL) {
        pbuf_free(heth.RxDescList.pRxStart);
    }
    heth.RxDescList.pRxStart = NULL;
    heth.RxDescList.pRxEnd   = NULL;
    /* TX 侧恢复（2026-11-26 定级）：SR 后 TX 环残留 OWN 位与在途 pbuf——
     * DMA 重启后 TX 进程悬挂（TPS=6），设备收帧不应答（ping 全超时）。
     * 环全量重初始化（对齐 HAL_ETH_Init 的 ETH_DMATxDescListInit），
     * 在途 pbuf 直接归还（DMA 不会再完成它们、TxFreeCallback 不会触发）。
     * 与 tcpip 线程的发送路径经 ETH_CTRL_LOCK 串行，此处环写安全。
     * 注意 IRQ 在此刻已由 Stop_IT 关闭，不会并发触发 TxFreeCallback。 */
    for (uint32_t i = 0U; i < ETH_TX_DESC_CNT; i++) {
        ETH_DMADescTypeDef *dmatxdesc = heth.Init.TxDesc + i;
        WRITE_REG(dmatxdesc->DESC0, 0x0U);
        WRITE_REG(dmatxdesc->DESC1, 0x0U);
        WRITE_REG(dmatxdesc->DESC2, 0x0U);
        WRITE_REG(dmatxdesc->DESC3, 0x0U);
        SET_BIT(dmatxdesc->DESC0, ETH_DMATXDESC_TCH);
        if (i < (ETH_TX_DESC_CNT - 1U)) {
            WRITE_REG(dmatxdesc->DESC3, (uint32_t)(heth.Init.TxDesc + i + 1U));
        } else {
            WRITE_REG(dmatxdesc->DESC3, (uint32_t)(heth.Init.TxDesc));
        }
        SET_BIT(dmatxdesc->DESC0, ETH_DMATXDESC_CHECKSUMTCPUDPICMPFULL);
        /* 注意：PacketAddress[i] 只置 NULL、不 pbuf_free——未被 ReleaseTxPacket
         * 行走到的历史条目可能是已释放的悬垂指针，无条件 free 会双重释放
         * 损坏 lwIP 池链表（台架 2026-11-26 定级：池全灭 + FBES 风暴）。
         * 在途 pbuf 的泄漏（每次重建最多 1 包）由后续受控重启路径兜底。 */
        heth.TxDescList.PacketAddress[i] = NULL;
    }
    heth.TxDescList.CurTxDesc           = 0U;
    heth.TxDescList.BuffersInUse        = 0U;
    heth.TxDescList.releaseIndex        = 0U;
    heth.TxDescList.CurrentPacketAddress = NULL;
    /* RX 描述符链固件侧状态复位（对齐 HAL_ETH_Init 的 ETH_DMARxDescListInit，
     * 2026-11-26 定级）：SR 后 Start_IT 的重武装计数（RxBuildDescCnt）与遍历
     * 游标（RxDescIdx）若沿用楔死前残留值，ReadData 的遍历上限
     * （8 - RxBuildDescCnt）会被卡死为 0/1，帧永久堆积在环上无人消费
     * （收帧正常、不应答的现场形态）。 */
    heth.RxDescList.RxDescIdx      = 0U;
    heth.RxDescList.RxDescCnt      = 0U;
    heth.RxDescList.RxBuildDescIdx = 0U;
    heth.RxDescList.RxBuildDescCnt = 0U;
    heth.RxDescList.ItMode         = 0U;
    if (heth.gState == HAL_ETH_STATE_READY) {
        (void)HAL_ETH_Start_IT(&heth);
        /* 清粘滞状态位（写 1 清零），避免看门狗对同一故障反复触发 */
        heth.Instance->DMASR = (ETH_DMASR_RPSS | ETH_DMASR_RBUS | ETH_DMASR_TBUS
                | ETH_DMASR_TPSS | ETH_DMASR_ETS | ETH_DMASR_FBES | ETH_DMASR_AIS
                | ETH_DMASR_TUS);
        /* RX 轮询踢脚（2026-11-26 定级）：SR 后 DMA 接收进程停在挂起态
         * （RPS=3）——描述符环已武装但 DMA 不主动轮询，写一次接收轮询请求
         * （DMARPDR，值无关紧要）使 DMA 立即重读描述符链，恢复接收。
         * TX 同理（DMATPDR）：包已入环（OWN=1）但发送进程挂起（TPS=6）
         * 不发送——写一次发送轮询请求恢复发送。 */
        WRITE_REG(heth.Instance->DMARPDR, 0U);
        WRITE_REG(heth.Instance->DMATPDR, 0U);
    }
    ETH_CTRL_UNLOCK();
}

/**
 * @brief ETH 看门狗任务（独立线程，500ms 轮询）
 *
 * 台架 2026-11-19 四层定级 + 空闲误判定级 + 线程阻塞定级：
 * 真实故障判据（空闲态不计数、不动作、不进事件环）：
 *   RX: RPSS（进程停止）/ RBUS（缓冲不可用）/
 *       RPS=3 挂起且（池锁存 ERROR 或 存在未读帧描述符）
 *   TX: TUS（欠载）/ TBUS 且有真在途发送（存在 OWN=1 的 TX 描述符）
 * TPS=6+TBUS 是发送空闲常态、RPS=3 也可能出现在健康空闲，
 * 单独出现一律视为误判（2026-11-19 实测安静期 161 次虚增）。
 * 轻量恢复（不重建描述符环、不触碰池）：
 *   清 TUS → 接收停止则置 DMAOMR.SR 重启接收 → 轮询请求 →
 *   释放 RX 信号量让 EthIf 线程走正常 ReadData 路径重挂描述符。
 * 致命错误（FBES）置 g_eth_dma_fault，由 EthIf 线程上下文执行完整重建。
 * 注意：本任务不得调用 HAL_ETH_ReleaseTxPacket（与 tcpip 线程并发损坏 TX 链表）。
 */
static void eth_watchdog_task(void *argument)
{
    (void)argument;
    uint32_t s_diag_div = 0U; /* 临时诊断节拍计数（30s 一行） */
    /* 首次运行（调度器已启动、外设已初始化）：消费上次重症重启的 RTC 标记 */
    eth_wedge_consume_reboot_mark();
    SEGGER_RTT_printf(0, "[ethwdg] up t=%u\n", (unsigned)osKernelGetTickCount());
    for (;;) {
        osDelay(500);
        g_ethwdg_hb++; /* 喂狗门控判据：看门狗任务本身被饿死也会被发现 */

        /* 无链路（物理断线/未插线）期间跳过全部楔死检测：此时 DMA 已被
         * 链路线程停止，RPSS/RBUS 等标志是停机常态而非故障；若按楔死处理
         * 触发重建，会形成"重建→IRQ 再触发→重建"的自持循环（Realtime
         * 空转饿死系统 → 门控停喂 → IWDG 兜底，台架 2026-11-20 多次冻结
         * 实锤）。链路状态由 ethernet_link_thread 全权管理。 */
        struct netif *nif_lk = netif_default;
        if ((nif_lk == NULL) || !netif_is_link_up(nif_lk)) {
            continue;
        }

        uint32_t dmasr = heth.Instance->DMASR;
        uint32_t rps   = (dmasr & ETH_DMASR_RPS_Msk) >> ETH_DMASR_RPS_Pos;
        uint32_t no_rx = osKernelGetTickCount() - g_eth_last_rx_tick;
        uint32_t rx_closing_stuck = ((rps == 4U) && (no_rx > 2000U));
        /* RPS=0 = 接收进程已停（Stop/复位后）。健康空闲恒为 RPS=2；链路恢复
         * 的 Start_IT 窗口内 RPS=0 是瞬态（<10ms）。要求连续 2 次轮询（1s）
         * 均停且无新帧才判僵尸态，排除启动/恢复瞬态（台架 2026-11-21 定级）。 */
        static uint32_t s_rx0_polls;
        if ((rps == 0U) && (no_rx > 2000U)) {
            s_rx0_polls++;
        } else {
            s_rx0_polls = 0U;
        }
        uint32_t rx_stopped = (s_rx0_polls >= 2U);
        uint32_t rx_wedge = ((dmasr & ETH_DMASR_RPSS) != 0U)
                || ((dmasr & ETH_DMASR_RBUS) != 0U)
                || rx_closing_stuck
                || rx_stopped
                || ((rps == 3U) && (no_rx > 2000U)
                    && (RxAllocStatus != RX_ALLOC_OK || eth_rx_has_unread() != 0U));
        uint32_t tx_wedge = ((dmasr & ETH_DMASR_TUS) != 0U)
                || (((dmasr & ETH_DMASR_TBUS) != 0U)
                    && (eth_tx_has_inflight() != 0U));

        /* ---- 临时诊断：每 60s 一行状态 + tcpip 线程心跳探针 ----
         * th 不涨 = tcpip 线程死；eh 不涨 = EthIf 线程死；nf 无 UP 位 =
         * netif 没起来（候选③）；rxa 持续增长且 dsr 标志干净 = 候选①盲区。 */
        /* 2026-11-26：周期由 60s 改为 10min——RTT 上行缓冲仅 1KB 且
         * NO_BLOCK_SKIP，未接调试器时 60s 一行的 E 诊断约 8 分钟即写满缓冲，
         * 之后全部打印（含 W/G 故障行）被静默丢弃（现场"日志静默窗口"根因）。
         * 放缓后 8h 才填满，故障行基本都能留到接调试器读取。 */
        if ((++s_diag_div % 1200U) == 0U) {
            (void)tcpip_try_callback(eth_diag_tcpip_cb, NULL);
            uint8_t pfree = 0U;
            RX_POOL_LOCK();
            struct memp *pm = memp_tab_RX_POOL;
            while ((pm != NULL) && (pfree < ETH_RX_BUFFER_CNT)) {
                pfree++;
                pm = pm->next;
            }
            RX_POOL_UNLOCK();
            struct netif *nif = netif_default;
            SEGGER_RTT_printf(0,
                    "E t=%u dsr=%08X rps=%u tps=%u rxa=%u al=%u pf=%u rr=%u tw=%u df=%u rb=%u dg=%u th=%u eh=%u nf=%08X\n",
                    (unsigned)osKernelGetTickCount(), (unsigned)dmasr,
                    (unsigned)rps,
                    (unsigned)((dmasr & ETH_DMASR_TPS_Msk) >> ETH_DMASR_TPS_Pos),
                    (unsigned)no_rx, (unsigned)RxAllocStatus, (unsigned)pfree,
                    (unsigned)g_eth_rx_recoveries, (unsigned)g_eth_tx_wedges,
                    (unsigned)g_eth_dma_fault, (unsigned)g_eth_reboot_recoveries,
                    (unsigned)s_eth_degraded,
                    (unsigned)g_eth_tcpip_hb, (unsigned)g_ethif_hb,
                    (unsigned)(nif != NULL ? nif->flags : 0U));
        }

        if (g_eth_dma_fault == 0U && !rx_wedge && !tx_wedge) {
            /* 健康满 10 分钟：清零连续重启计数（下次重症重新给 3 次机会） */
            static uint8_t s_cnt_cleared;
            if (s_cnt_cleared == 0U && osKernelGetTickCount() > 600000U) {
                pl_rtc_handle_t hc = pl_rtc_get_handle();
                pl_rtc_bkup_write(hc, ETH_WEDGE_RTC_CNT_REG, 0U);
                s_cnt_cleared = 1U;
                SEGGER_RTT_printf(0, "C t=%u\n", (unsigned)osKernelGetTickCount());
            }
            /* 只有 RX 真正恢复（检出后有新帧完成）才清零重症计时。
             * 挂起标志在 RBUS 等位的 IRQ 清除间隙会闪动，逐次清零会把
             * 时间线重置、永远到不了重症阈值（台架 2026-11-19 定级）。 */
            if (s_wedge_since != 0U && g_eth_last_rx_tick != s_wedge_rx_snapshot) {
                s_wedge_since = 0U;
            }
            continue;
        }
        uint16_t reason = 0U;
        if ((dmasr & ETH_DMASR_RBUS) != 0U) {
            reason |= ETH_WEDGE_RBUS;
        }
        if ((dmasr & ETH_DMASR_RPSS) != 0U) {
            reason |= ETH_WEDGE_RPSS;
        }
        if (rx_closing_stuck) {
            reason |= ETH_WEDGE_RX_CLOSING;
        }
        if (rx_stopped != 0U) {
            reason |= ETH_WEDGE_RX_STOPPED;
        }
        if ((rps == 3U) && (eth_rx_has_unread() != 0U)) {
            reason |= ETH_WEDGE_RX_UNREAD;
        }
        if ((dmasr & ETH_DMASR_TUS) != 0U) {
            reason |= ETH_WEDGE_TUS;
        }
        if (((dmasr & ETH_DMASR_TBUS) != 0U)
                && (eth_tx_has_inflight() != 0U)) {
            reason |= ETH_WEDGE_TBUS_BUSY;
        }
        if (RxAllocStatus != RX_ALLOC_OK) {
            reason |= ETH_WEDGE_POOL_LATCH;
        }
        if (g_eth_dma_fault != 0U) {
            reason |= ETH_WEDGE_FATAL;
        }
        /* 临时诊断：原因变化时才打印，避免挂起期每 500ms 刷屏 */
        static uint16_t s_diag_last_reason;
        if (reason != s_diag_last_reason) {
            s_diag_last_reason = reason;
            SEGGER_RTT_printf(0, "W t=%u r=%04X dsr=%08X rxa=%u al=%u\n",
                    (unsigned)osKernelGetTickCount(), (unsigned)reason,
                    (unsigned)dmasr, (unsigned)no_rx, (unsigned)RxAllocStatus);
        }
        eth_wedge_record(reason, dmasr);
        if (RxAllocStatus != RX_ALLOC_OK) {
            RxAllocStatus = RX_ALLOC_OK;
        }
        g_eth_rx_recoveries++;
        if (tx_wedge) {
            g_eth_tx_wedges++;
        }
        /* 重症时间线（2026-11-19 裁决：挂起 ≥2s 升级 SR 重建，≥4s 受控整机重启） */
        uint32_t now_t = osKernelGetTickCount();
        if (s_wedge_since == 0U) {
            s_wedge_since = now_t;
            s_wedge_rx_snapshot = g_eth_last_rx_tick;
            /* 首因锁存（2026-11-21）：挂起期间 reason 位会随恢复动作闪动
             * （如重建短暂拉起 RPS 清零判据位），重启标记必须记录首次检出的
             * 原始原因，保证事后可追溯。 */
            s_wedge_first_reason = reason;
        }
        uint32_t persist = now_t - s_wedge_since;
        if (persist >= ETH_WEDGE_REBOOT_MS && s_eth_degraded == 0U) {
            /* 重症兜底：原因写入 RTC 备份寄存器（复位不丢）后整机复位。
             * 启动时回读标记 → 事件环 + g_eth_reboot_recoveries。
             * 现场语义：可以死，但不能一直死（5s 预算内恢复）。
             * 连续重启 ≥3 次仍异常 → 降级：不再整机重启（留在轻量恢复循环，
             * 网络可能仍不可用，但设备保持在线可诊断，避免无限重启）。 */
            pl_rtc_handle_t h = pl_rtc_get_handle();
            uint32_t cnt = pl_rtc_bkup_read(h, ETH_WEDGE_RTC_CNT_REG);
            if (cnt >= ETH_WEDGE_MAX_REBOOTS) {
                s_eth_degraded = 1U;
                SEGGER_RTT_printf(0, "D t=%u cnt=%u r=%04X\n",
                        (unsigned)now_t, (unsigned)cnt, (unsigned)reason);
            } else {
                pl_rtc_bkup_write(h, ETH_WEDGE_RTC_CNT_REG, cnt + 1U);
                eth_wedge_mark_reboot(s_wedge_first_reason, dmasr);
                pl_system_reset();
                /* 不复返 */
            }
        }
        if (persist >= ETH_WEDGE_REBUILD_MS
                || g_eth_dma_fault != 0U || rx_closing_stuck) {
            /* 升级：置标记并唤醒 EthIf 线程执行完整重建（含 DMA 软件复位）。
             * （EthIf 线程经 trypost 投递从不阻塞，委托可靠；
             *  本任务直接重建曾引发崩溃重启循环，已回退。） */
            g_eth_dma_fault = 1U;
            (void)osSemaphoreRelease(RxPktSemaphore);
        } else {
            /* 轻量恢复的 DMA 寄存器写与链路线程/重建串行（R1：防并发撕裂） */
            ETH_CTRL_LOCK();
            if ((dmasr & ETH_DMASR_TUS) != 0U) {
                /* 欠载：先清 TUS（写 1 清零，HAL 中断分支不清该位） */
                heth.Instance->DMASR = ETH_DMASR_TUS;
            }
            if ((dmasr & ETH_DMASR_RPSS) != 0U) {
                /* 接收进程停止：轮询请求无效，置 SR 重新启动接收 */
                heth.Instance->DMAOMR |= ETH_DMAOMR_SR;
            }
            WRITE_REG(heth.Instance->DMARPDR, 0U);
            WRITE_REG(heth.Instance->DMATPDR, 0U);
            ETH_CTRL_UNLOCK();
            /* 唤醒 EthIf 线程：走正常 do-while ReadData 路径重挂描述符
             * （信号量已满时释放为无副作用失败） */
            (void)osSemaphoreRelease(RxPktSemaphore);
        }
    }
}

void ethernetif_input(void *argument)
{
    struct pbuf *p      = NULL;
    struct netif *netif = (struct netif *)argument;

    for (;;) {
        g_ethif_hb++; /* 临时诊断：EthIf 线程存活心跳 */
        osStatus_t rx_st = osSemaphoreAcquire(RxPktSemaphore, 1000U);
        if (rx_st != osOK) {
            continue;
        }
        /* DMA 致命故障：本线程负责完整重建（看门狗任务只做标记与唤醒）。
         * 限频：重建后若 IRQ 立刻再触发（楔死状态机自持），至少间隔
         * ETH_REBUILD_MIN_INTERVAL_MS 才允许下一次重建，防止本线程
         * （Realtime）陷入"重建→再触发→重建"自持循环饿死全系统
         * （台架 2026-11-20 拔线冻结实锤）。 */
        if (g_eth_dma_fault != 0U) {
            g_eth_dma_fault = 0U;
            uint32_t now_t = osKernelGetTickCount();
            if ((now_t - s_last_rebuild_tick) >= ETH_REBUILD_MIN_INTERVAL_MS) {
                s_last_rebuild_tick = now_t;
                eth_dma_recover();
            }
            continue;
        }
        /* 突发配额让出（2026-11-20 定级）：本线程 osPriorityRealtime，
         * 异常帧/风暴洪泛时 RX 信号量持续就绪，若一次排空到底会饿死
         * Normal 的 tcpip 线程（ping 不答）与 Low 的喂狗任务（IWDG 复位）。
         * 每 16 帧让出 1ms，保证协议栈与低优先级任务始终有 CPU。 */
        uint32_t burst   = 0U;
        uint32_t drained = 0U;
        do {
            p = low_level_input(netif);
            if (p != NULL) {
                drained++;
                if (netif->input(p, netif) != ERR_OK) {
                    pbuf_free(p);
                }
            }
            if (++burst >= 16U) {
                burst = 0U;
                osDelay(1);
            }
        } while (p != NULL);
        /* 空唤醒（无帧可取）同样让出：信号量被 IRQ 风暴持续释放时，
         * 本线程（Realtime）会在外循环形成无让出空转，饿死全部低优先级
         * 任务（台架 2026-11-20 拔线冻结根因之一）。 */
        if (drained == 0U) {
            osDelay(1);
        }
    }
}

#if !LWIP_ARP
/** @brief ARP 关闭时的自定义输出函数（当前为占位实现） */
static err_t low_level_output_arp_off(struct netif *netif, struct pbuf *q, const ip4_addr_t *ipaddr)
{
    err_t errval;
    errval = ERR_OK;

    return errval;
}
#endif /* LWIP_ARP */

/**
 * @brief   LwIP 网络接口注册入口（传递给 netif_add 的回调）
 * @param   netif  LwIP 网络接口
 * @return  ERR_OK 初始化成功；ERR_MEM 内存不足
 *
 * 设置 name、output/linkoutput 函数指针，调用 low_level_init 初始化硬件。
 */
err_t ethernetif_init(struct netif *netif)
{
    LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
    netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

    // MIB2_INIT_NETIF(netif, snmp_ifType_ethernet_csmacd, LINK_SPEED_OF_YOUR_NETIF_IN_BPS);

    netif->name[0] = IFNAME0;
    netif->name[1] = IFNAME1;

    /* output 直接使用 etharp_output，省去一次函数调用 */
#if LWIP_IPV4
#if LWIP_ARP || LWIP_ETHERNET
#if LWIP_ARP
    netif->output = etharp_output;
#else
    netif->output = low_level_output_arp_off;
#endif /* LWIP_ARP */
#endif /* LWIP_ARP || LWIP_ETHERNET */
#endif /* LWIP_IPV4 */

#if LWIP_IPV6
    netif->output_ip6 = ethip6_output;
#endif /* LWIP_IPV6 */

    netif->linkoutput = low_level_output;

    /* 初始化底层硬件及 RTOS 资源 */
    low_level_init(netif);

    return ERR_OK;
}

/**
 * @brief   自定义 pbuf 释放回调：将 Rx 缓冲区归还内存池
 * @param   p  待释放的 pbuf
 *
 * 若因 Rx 池耗尽触发错误，归还缓冲区后释放 RX 信号量
 * 通知 ethernetif_input 线程调用 HAL_ETH_GetRxDataBuffer 重建描述符。
 */
void pbuf_free_custom(struct pbuf *p)
{
    struct pbuf_custom *custom_pbuf = (struct pbuf_custom *)p;
    /* 临界区：与 EthIf 线程的分配互斥，防止自由链表被并发破坏 */
    RX_POOL_LOCK();
    LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf);
    RX_POOL_UNLOCK();

    if (RxAllocStatus == RX_ALLOC_ERROR) {
        RxAllocStatus = RX_ALLOC_OK;
        osSemaphoreRelease(RxPktSemaphore);
    }
}

/**
 * @brief   获取当前系统毫秒时间戳（供 LwIP 定时器使用）
 * @return  系统启动以来的毫秒数
 */
u32_t sys_now(void)
{
    return HAL_GetTick();
}

/* ================================================================
 *  ETH MSP 初始化与反初始化（HAL 回调）
 * ================================================================ */

/**
 * @brief   ETH 外设 MSP 初始化：使能时钟、配置 GPIO 复用、使能 NVIC
 * @param   ethHandle  ETH 句柄（HAL_ETH_Init 自动传入）
 *
 * RMII 引脚映射：
 *   PC1  → ETH_MDC       PA1  → ETH_REF_CLK
 *   PA2  → ETH_MDIO      PA7  → ETH_CRS_DV
 *   PC4  → ETH_RXD0      PC5  → ETH_RXD1
 *   PG11 → ETH_TX_EN     PG13 → ETH_TXD0
 *   PG14 → ETH_TXD1
 */
void HAL_ETH_MspInit(ETH_HandleTypeDef *ethHandle)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if (ethHandle->Instance == ETH) {
        /* 使能外设时钟 */
        __HAL_RCC_ETH_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOG_CLK_ENABLE();

        /* GPIOC：MDC、RXD0、RXD1 */
        GPIO_InitStruct.Pin       = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

        /* GPIOA：REF_CLK、MDIO、CRS_DV */
        GPIO_InitStruct.Pin       = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        /* GPIOG：TX_EN、TXD0、TXD1 */
        GPIO_InitStruct.Pin       = GPIO_PIN_11 | GPIO_PIN_13 | GPIO_PIN_14;
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
        HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

        /* NVIC 中断配置：ETH IRQ 优先级 5 */
        HAL_NVIC_SetPriority(ETH_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(ETH_IRQn);
    }
}

/** @brief ETH 外设 MSP 反初始化：关闭时钟、回收 GPIO、关闭 NVIC */
void HAL_ETH_MspDeInit(ETH_HandleTypeDef *ethHandle)
{
    if (ethHandle->Instance == ETH) {
        /* 关闭外设时钟 */
        __HAL_RCC_ETH_CLK_DISABLE();

        /* 回收 ETH 相关 GPIO */
        HAL_GPIO_DeInit(GPIOC, GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5);
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7);
        HAL_GPIO_DeInit(GPIOG, GPIO_PIN_11 | GPIO_PIN_13 | GPIO_PIN_14);

        /* 关闭 NVIC */
        HAL_NVIC_DisableIRQ(ETH_IRQn);
    }
}

/* ================================================================
 *  PHY MDIO 接口函数
 * ================================================================ */

/** @brief 初始化 MDIO 接口（配置 MDIO 时钟分频） */
int32_t pl_eth_phy_io_init(void)
{
    /* MDIO GPIO 配置已在 HAL_ETH_MspInit 中完成 */
    HAL_ETH_SetMDIOClockRange(&heth);
    return 0;
}

/** @brief 反初始化 MDIO 接口（当前为空操作） */
int32_t pl_eth_phy_io_deinit(void)
{
    return 0;
}

/** @brief 通过 MDIO 读取 PHY 寄存器 */
int32_t pl_eth_phy_io_read_reg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal)
{
    if (HAL_ETH_ReadPHYRegister(&heth, DevAddr, RegAddr, pRegVal) != HAL_OK) {
        return -1;
    }
    return 0;
}

/** @brief 通过 MDIO 写入 PHY 寄存器 */
int32_t pl_eth_phy_io_write_reg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal)
{
    if (HAL_ETH_WritePHYRegister(&heth, DevAddr, RegAddr, RegVal) != HAL_OK) {
        return -1;
    }
    return 0;
}

/** @brief 获取系统时间戳（供 Device 层 PHY 驱动内部定时使用） */
int32_t pl_eth_phy_io_get_tick(void)
{
    return HAL_GetTick();
}

/**
 * @brief   链路状态监控线程：每 100ms 轮询 PHY 链路状态，更新 MAC 和 netif
 * @param   argument  指向 netif 结构的指针
 *
 * 检测链路变化：
 *   - 链路断开 → 停止 ETH、netif down
 *   - 链路恢复 → 根据 PHY 协商结果配置 MAC 速率/双工、启动 ETH、netif up
 */
void ethernet_link_thread(void *argument)
{
    ETH_MACConfigTypeDef MACConf = {0};
    int32_t PHYLinkState         = 0;
    uint32_t linkchanged = 0U, speed = 0U, duplex = 0U;

    struct netif *netif = (struct netif *)argument;

    for (;;) {
        PHYLinkState = g_phy_get_link();

        if (netif_is_link_up(netif) && (PHYLinkState == PL_ETH_LINK_DOWN)) {
            /* 链路断开：Stop/Start 与重建/轻量恢复串行（R1：防并发撕裂） */
            ETH_CTRL_LOCK();
            HAL_ETH_Stop_IT(&heth);
            netif_set_down(netif);
            netif_set_link_down(netif);
            ETH_CTRL_UNLOCK();
        } else if (!netif_is_link_up(netif) && (PHYLinkState > PL_ETH_LINK_DOWN)) {
            /* 链路恢复，获取协商结果 */
            switch (PHYLinkState) {
                case PL_ETH_LINK_100M_FD:
                    duplex      = ETH_FULLDUPLEX_MODE;
                    speed       = ETH_SPEED_100M;
                    linkchanged = 1;
                    break;
                case PL_ETH_LINK_100M_HD:
                    duplex      = ETH_HALFDUPLEX_MODE;
                    speed       = ETH_SPEED_100M;
                    linkchanged = 1;
                    break;
                case PL_ETH_LINK_10M_FD:
                    duplex      = ETH_FULLDUPLEX_MODE;
                    speed       = ETH_SPEED_10M;
                    linkchanged = 1;
                    break;
                case PL_ETH_LINK_10M_HD:
                    duplex      = ETH_HALFDUPLEX_MODE;
                    speed       = ETH_SPEED_10M;
                    linkchanged = 1;
                    break;
                default:
                    break;
            }

            if (linkchanged) {
                /* 链路恢复：Stop/Start 与重建/轻量恢复串行（R1：防并发撕裂） */
                ETH_CTRL_LOCK();
                HAL_ETH_GetMACConfig(&heth, &MACConf);
                MACConf.DuplexMode = duplex;
                MACConf.Speed      = speed;
                HAL_ETH_SetMACConfig(&heth, &MACConf);
                HAL_ETH_Start_IT(&heth);
                netif_set_up(netif);
                netif_set_link_up(netif);
                ETH_CTRL_UNLOCK();
            }
        }

        osDelay(100);
    }
}

/**
 * @brief   ETH HAL Rx 缓冲区分配回调（零拷贝模式）
 *
 * 从 RX_POOL 内存池分配一个含 pbuf 的 RxBuff_t，
 * 设置自定义释放函数 pbuf_free_custom。
 * 若池耗尽，标记 RxAllocStatus 为 ERROR，HAL_ETH_RxCpltCallback 会触发恢复。
 */
void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
    /* 临界区：与 tcpip/应用线程的释放互斥，防止自由链表被并发破坏 */
    RX_POOL_LOCK();
    struct pbuf_custom *p = LWIP_MEMPOOL_ALLOC(RX_POOL);
    RX_POOL_UNLOCK();
    if (p) {
        *buff                   = (uint8_t *)p + offsetof(RxBuff_t, buff);
        p->custom_free_function = pbuf_free_custom;
        /* pbuf_alloced_custom 必须在每次分配后调用，因为 LwIP/app 可能修改 ref 等字段 */
        pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF, p, *buff, ETH_RX_BUF_SIZE);
    } else {
        RxAllocStatus = RX_ALLOC_ERROR;
        g_eth_rx_pool_exhausts++;
        *buff         = NULL;
    }
}

/**
 * @brief   ETH HAL Rx 链接回调：将零拷贝缓冲区链接为 pbuf 链
 *
 * 每次 DMA 完成一个分片时调用，将 buffer 转为 pbuf 并链接到链尾。
 * 最后遍历整个链更新 tot_len。
 */
void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
    struct pbuf **ppStart = (struct pbuf **)pStart;
    struct pbuf **ppEnd   = (struct pbuf **)pEnd;
    struct pbuf *p        = NULL;

    /* 从 buff 地址反推 pbuf 指针 */
    p          = (struct pbuf *)(buff - offsetof(RxBuff_t, buff));
    p->next    = NULL;
    p->tot_len = 0;
    p->len     = Length;

    /* 链接到链尾 */
    if (!*ppStart) {
        *ppStart = p;        /* 首帧 */
    } else {
        (*ppEnd)->next = p;  /* 追加到链尾 */
    }
    *ppEnd = p;

    /* 更新链上所有 pbuf 的 tot_len */
    for (p = *ppStart; p != NULL; p = p->next) {
        p->tot_len += Length;
    }
}

/** @brief ETH HAL Tx 释放回调：发送完成后释放 pbuf */
void HAL_ETH_TxFreeCallback(uint32_t *buff)
{
    pbuf_free((struct pbuf *)buff);
}

/* ---- ETH ISR ---- */
void ETH_IRQHandler(void)
{
    HAL_ETH_IRQHandler(&heth);
}
