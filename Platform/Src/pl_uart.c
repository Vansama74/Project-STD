/**
 * @file        pl_uart.c
 * @brief       UART 平台层抽象（hw_pl_initcall 优先级 3）
 *
 * 封装 USART1 (RS485)、USART3 (RS232-0)、USART6 (RS232-1)，
 * 提供不透明句柄、中断驱动发送和 DMA 乒乓双缓冲接收。
 * UART 及 DMA 中断处理全部内聚于此文件。
 *
 * TX（中断驱动 + 让出等待）: HAL_UART_Transmit_IT + 完成信号量（TC 中断经
 *   HAL_UART_TxCpltCallback 释放），每 UART 发送互斥串行化，超时/出错 AbortTransmit。
 *
 * RX 架构（乒乓双缓冲 + circular DMA）:
 *   - DMA circular 覆盖 2×block 缓冲，HT/TC 中断各冲刷半块（段长恒 = block）；
 *   - IDLE 中断冲刷块内残余字节（短帧低延迟出队，段长 1..block）；
 *   - ISR 经 rx_cb(data, len) 通知上层，上层在下一块被覆盖前拷入 RB。
 *   累计计数（recv_total/posted）保证增量不重不漏：HT/TC 各 +block，
 *   IDLE 用 pos % block 求当前累计，通知失败（队列满）不推进 posted，
 *   下次事件自动合并补发。
 */

#include "pl_uart.h"
#include "usart.h"
#include "dma.h"
#include "initcall.h"
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "cmsis_os2.h"

/* DMA 句柄（定义在 Core/Src/dma.c） */
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart3_rx;
extern DMA_HandleTypeDef hdma_usart6_rx;

typedef struct {
    UART_HandleTypeDef *huart;
    pl_uart_rx_cb_t rx_cb;
    void *rx_cb_ctx;
    pl_uart_dir_fn_t dir_cb;   /* Device 层注入的 RS485 方向控制，无则为 NULL */
    uint8_t *rx_buf;           /* 双块缓冲基址（2 × block_size，.bss 静态） */
    uint16_t rx_buf_size;      /* circular 周期 = 2 × block_size */
    uint16_t block_size;       /* 半块 = 总长一半（HT 边界） */
    volatile uint32_t recv_total; /* 接收累计：HT/TC 各 +block_size */
    volatile uint32_t posted;     /* 已通知上层的累计（%rx_buf_size 即缓冲偏移） */
    volatile bool rx_running;     /* circular RX 已启动 */

    /* TX：中断驱动发送的同步对象（首次发送时惰性创建，控制块静态免占堆） */
    osMutexId_t tx_lock;              /* 同一 UART 的多任务发送串行化（优先级继承） */
    osSemaphoreId_t tx_done;          /* TC 中断释放（ISR 安全路径），发送任务获取 */
    StaticSemaphore_t tx_lock_cb;     /* 互斥控制块（.bss） */
    StaticSemaphore_t tx_done_cb;     /* 完成信号量控制块（.bss，二值） */
} uart_ctx_t;

static uart_ctx_t g_uart_ctx[PL_UART_MAX];

/* ---- 内部：DMA 当前写入位置 ---- */
static inline uint16_t _rx_pos(uart_ctx_t *ctx)
{
    return (uint16_t)(ctx->rx_buf_size - __HAL_DMA_GET_COUNTER(ctx->huart->hdmarx));
}

/* ---- 内部：把 [posted, cur_total) 的新字节通知上层 ----
 * 先回调成功再推进 posted：通知失败（队列满）时下次事件合并补发，不丢数据。
 * 段不跨越块边界（HT/TC 中断先于位置越界触发）。 */
static void _uart_flush(uart_ctx_t *ctx, uint32_t cur_total)
{
    if (cur_total == ctx->posted || ctx->rx_cb == nullptr)
        return;

    uint16_t off = (uint16_t)(ctx->posted % ctx->rx_buf_size);
    uint16_t len = (uint16_t)(cur_total - ctx->posted);
    if (len > (uint16_t)(ctx->rx_buf_size - off))
        len = (uint16_t)(ctx->rx_buf_size - off); /* 防御性钳制（理论不可达） */

    if (len == 0U)
        return;

    ctx->rx_cb(ctx->rx_buf + off, len, ctx->rx_cb_ctx);
    ctx->posted = cur_total;
}

/* ---- 内部：启动/重启 circular RX ----
 * DMA circular 覆盖整缓冲自动回绕，HT/TC 各冲刷半块。
 * 重启（错误恢复）时丢弃未通知的块内残余，计数归零重来。 */
static int32_t _uart_rx_restart(uart_ctx_t *ctx)
{
    UART_HandleTypeDef *huart = ctx->huart;

    /* 乒乓双缓冲依赖 DMA 自动回绕：流模式切 circular 并重新初始化。
     * Mode 字段持久化，后续 DeInit/MspInit 重配仍保持 circular。 */
    huart->hdmarx->Init.Mode = DMA_CIRCULAR;
    if (HAL_DMA_Init(huart->hdmarx) != HAL_OK)
        return -1;

    ctx->recv_total = 0;
    ctx->posted     = 0;

    if (HAL_UART_Receive_DMA(huart, ctx->rx_buf, ctx->rx_buf_size) != HAL_OK)
        return -1;

    __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);
    return 0;
}

/* ---- 内部：UART 空闲中断处理（冲刷块内残余字节） ---- */
static void uart_idle_handle(uart_ctx_t *ctx)
{
    if (!(__HAL_UART_GET_FLAG(ctx->huart, UART_FLAG_IDLE))) return;
    __HAL_UART_CLEAR_IDLEFLAG(ctx->huart);

    if (!ctx->rx_running) return;
    /* 当前累计 = 已过 HT/TC 边界基数 + 块内位置（pos % block_size） */
    uint32_t cur = ctx->recv_total + (_rx_pos(ctx) % ctx->block_size);
    _uart_flush(ctx, cur);
}

/* ---- 内部：按 huart 反查上下文 ---- */
static uart_ctx_t *_ctx_of(UART_HandleTypeDef *huart)
{
    for (uint8_t i = 0; i < PL_UART_MAX; i++)
        if (g_uart_ctx[i].huart == huart)
            return &g_uart_ctx[i];
    return nullptr;
}

/* ---- HAL 弱回调覆盖：circular DMA 的 HT/TC 事件 ---- */

/** TC：后半块就绪（pos == rx_buf_size → 累计 = recv_total） */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uart_ctx_t *ctx = _ctx_of(huart);
    if (ctx == nullptr || !ctx->rx_running || ctx->block_size == 0U)
        return;
    ctx->recv_total += ctx->block_size;
    _uart_flush(ctx, ctx->recv_total);
}

/** HT：前半块就绪（pos == block_size → 累计 = recv_total） */
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
    uart_ctx_t *ctx = _ctx_of(huart);
    if (ctx == nullptr || !ctx->rx_running || ctx->block_size == 0U)
        return;
    ctx->recv_total += ctx->block_size;
    _uart_flush(ctx, ctx->recv_total);
}

/** ORE/FE/NE 等错误：HAL 会停 DMAR，circular RX 自动重启恢复 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    uart_ctx_t *ctx = _ctx_of(huart);
    if (ctx != nullptr && ctx->rx_running && ctx->rx_buf != nullptr && ctx->block_size > 0U)
        _uart_rx_restart(ctx);
}

/** TC（发送完成，含末字节移出）：释放完成信号量，唤醒等待中的发送任务
 *  ISR 安全：cmsis_os2.c 的 osSemaphoreRelease 内含 IS_IRQ() 分支
 *  （xSemaphoreGiveFromISR + portYIELD_FROM_ISR）；本 UART 中断优先级 7，
 *  数值 ≥ configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5)，允许调用 FromISR API。 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    uart_ctx_t *ctx = _ctx_of(huart);
    if (ctx != nullptr && ctx->tx_done != nullptr)
        (void)osSemaphoreRelease(ctx->tx_done);
}

/* ---- initcall ---- */
void pl_uart_init(void)
{
    MX_USART1_UART_Init();
    MX_USART3_UART_Init();
    MX_USART6_UART_Init();

    g_uart_ctx[PL_UART1].huart = &huart1;
    g_uart_ctx[PL_UART3].huart = &huart3;
    g_uart_ctx[PL_UART6].huart = &huart6;
}
hw_pl_initcall(pl_uart_init); /* 优先级 2: 在 device 驱动之前 */

/* ---- 公开 API ---- */
pl_uart_handle_t pl_uart_get_handle(uint8_t id)
{
    return (id < PL_UART_MAX) ? &g_uart_ctx[id] : NULL;
}

/* ---- 内部：惰性创建 TX 同步对象（互斥 + 完成信号量） ----
 * pl_uart_init 在 RTOS 启动前执行，不能创建 RTOS 对象，故首次发送时创建；
 * 控制块在 uart_ctx_t 内（.bss 静态），不占 FreeRTOS 堆。
 * 判空与赋值整体置于内核锁内：同一 UART 可能被两个任务同时首次发送
 * （如定时心跳 + 协议回帧），分开会重复初始化同一控制块、破坏正在阻塞的等待链；
 * 锁内只调用非阻塞的创建 API。
 * @return 0=可用；-1=内核未运行或创建失败（调用方回退阻塞发送） */
static int32_t _tx_sync_ensure(uart_ctx_t *ctx)
{
    if (ctx->tx_lock != nullptr && ctx->tx_done != nullptr)
        return 0;
    if (osKernelGetState() != osKernelRunning)
        return -1;

    int32_t locked = osKernelLock(); /* 0=本函数加锁；1=已被外层锁定（同样禁止切换） */
    if (locked < 0)
        return -1;

    if (ctx->tx_lock == nullptr) {
        const osMutexAttr_t attr = {
            .name    = "pl_uart_tx",
            .cb_mem  = &ctx->tx_lock_cb,
            .cb_size = sizeof(ctx->tx_lock_cb),
        };
        ctx->tx_lock = osMutexNew(&attr);
    }
    if (ctx->tx_done == nullptr) {
        const osSemaphoreAttr_t attr = {
            .name    = "pl_uart_txdone",
            .cb_mem  = &ctx->tx_done_cb,
            .cb_size = sizeof(ctx->tx_done_cb),
        };
        ctx->tx_done = osSemaphoreNew(1U, 0U, &attr); /* 二值，仅 TC 中断释放 */
    }

    if (locked == 0)
        (void)osKernelUnlock(); /* locked!=0 表示外层持有，此处解锁会提前恢复调度 */

    return (ctx->tx_lock != nullptr && ctx->tx_done != nullptr) ? 0 : -1;
}

/* ---- 内部：阻塞轮询发送（RTOS 未运行 / 同步对象不可用时的回退路径） ---- */
static int32_t _send_blocking(uart_ctx_t *ctx, const uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    if (ctx->dir_cb) ctx->dir_cb(true);
    HAL_StatusTypeDef st = HAL_UART_Transmit(ctx->huart, (uint8_t *)buf, (uint16_t)len, timeout_ms);
    if (ctx->dir_cb) ctx->dir_cb(false);
    return (st == HAL_OK) ? (int32_t)len : -1;
}

int32_t pl_uart_send(pl_uart_handle_t h, const uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    uart_ctx_t *ctx = (uart_ctx_t *)h;
    if (!ctx || !ctx->huart || buf == nullptr || len == 0U || len > UINT16_MAX)
        return -1;

    /* 回退路径：内核未运行 / 同步对象创建失败（现有调用点全在任务上下文） */
    if (_tx_sync_ensure(ctx) != 0)
        return _send_blocking(ctx, buf, len, timeout_ms);

    /* 同一 UART 可能被多个协议任务发送（回帧）：互斥串行化，帧与帧不交织 */
    if (osMutexAcquire(ctx->tx_lock, osWaitForever) != osOK)
        return _send_blocking(ctx, buf, len, timeout_ms);

    /* 丢弃陈旧完成计数（上次中止与 TC 中断的竞态尾巴），使本次等待只认本次 TC */
    (void)osSemaphoreAcquire(ctx->tx_done, 0U);

    if (ctx->dir_cb) ctx->dir_cb(true);

    HAL_StatusTypeDef st = HAL_UART_Transmit_IT(ctx->huart, (uint8_t *)buf, (uint16_t)len);
    if (st == HAL_OK) {
        /* 等待 TC：任务阻塞让出 CPU（不再忙等饿死低优先级任务） */
        if (osSemaphoreAcquire(ctx->tx_done, timeout_ms) != osOK)
            st = HAL_TIMEOUT;
    }
    if (st != HAL_OK) {
        /* 未发出/超时：中止在途发送（复位 gState）并丢弃刚入队的完成计数 */
        (void)HAL_UART_AbortTransmit(ctx->huart);
        (void)osSemaphoreAcquire(ctx->tx_done, 0U);
    }

    if (ctx->dir_cb) ctx->dir_cb(false); /* TC 后（或中止后）必拉低 DE */
    (void)osMutexRelease(ctx->tx_lock);

    return (st == HAL_OK) ? (int32_t)len : -1;
}

void pl_uart_set_rx_cb(pl_uart_handle_t h, pl_uart_rx_cb_t cb, void *ctx_arg)
{
    uart_ctx_t *ctx = (uart_ctx_t *)h;
    if (ctx) {
        ctx->rx_cb     = cb;
        ctx->rx_cb_ctx = ctx_arg;
    }
}

void pl_uart_set_dir_cb(pl_uart_handle_t h, pl_uart_dir_fn_t cb)
{
    uart_ctx_t *ctx = (uart_ctx_t *)h;
    if (ctx) ctx->dir_cb = cb;
}

int32_t pl_uart_start_rx(pl_uart_handle_t h, uint8_t *buf, uint16_t len)
{
    uart_ctx_t *ctx = (uart_ctx_t *)h;
    if (!ctx || !ctx->huart || buf == nullptr || len < 2U) return -1;

    /* len 为 2×block 总长；半块 = len/2（HT 边界） */
    ctx->rx_buf      = buf;
    ctx->rx_buf_size = len;
    ctx->block_size  = (uint16_t)(len / 2U);
    ctx->rx_running  = true;

    return _uart_rx_restart(ctx);
}

int32_t pl_uart_set_baud(pl_uart_handle_t h, uint32_t baud)
{
    uart_ctx_t *ctx = (uart_ctx_t *)h;
    if (!ctx || !ctx->huart || baud == 0U) return -1;

    UART_HandleTypeDef *huart = ctx->huart;

    /* 与发送互斥：DeInit 会复位外设，在途 TX 必须先中止（运行态切波特率场景）。
     * 拿不到锁（内核未运行 / 同步对象尚未创建）时不存在在途发送，直接继续。 */
    bool tx_locked = false;
    if (ctx->tx_lock != nullptr && osKernelGetState() == osKernelRunning &&
        osMutexAcquire(ctx->tx_lock, osWaitForever) == osOK)
        tx_locked = true;
    (void)HAL_UART_AbortTransmit(huart);
    if (ctx->tx_done != nullptr)
        (void)osSemaphoreAcquire(ctx->tx_done, 0U); /* 丢弃陈旧完成计数 */

    /* 先停 DMA RX（若已启动），DeInit 后按新波特率 Init，最后重挂 RX。
     * DeInit 顺序：DMA Stop → HAL_UART_DeInit（MspDeInit 释放 GPIO/DMA/NVIC）
     * → 改 BaudRate → HAL_UART_Init（MspInit 重新配置）。 */
    int32_t ret = 0;
    if (huart->hdmarx != NULL)
        HAL_UART_DMAStop(huart);
    HAL_UART_DeInit(huart);
    huart->Init.BaudRate = baud;
    if (HAL_UART_Init(huart) != HAL_OK) {
        ret = -1;
    } else if (ctx->rx_buf != NULL && ctx->rx_buf_size > 0U) {
        if (_uart_rx_restart(ctx) != 0) ret = -1;
    }

    if (tx_locked) (void)osMutexRelease(ctx->tx_lock);
    return ret;
}

/* ================================================================
 *  UART 及 DMA 中断服务例程（由 startup 向量表直接跳转）
 * ================================================================ */

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
    uart_idle_handle(&g_uart_ctx[PL_UART1]);
}

void USART3_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart3);
    uart_idle_handle(&g_uart_ctx[PL_UART3]);
}

void USART6_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart6);
    uart_idle_handle(&g_uart_ctx[PL_UART6]);
}

/* DMA RX 中断 */
void DMA1_Stream1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart3_rx);
}
void DMA2_Stream1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart6_rx);
}
void DMA2_Stream2_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart1_rx);
}