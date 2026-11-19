/**
 * @file    ring_buffer.h
 * @brief   环形缓冲区 — 编译期静态分配，通过 mutex 参数控制锁行为
 *
 * RB_DEFINE(name, size) 在编译期声明实例（栈/全局/static 均可），
 * 数据区和控制结构连续分配，零堆开销。
 *
 * 线程安全：
 *   每个 API 末尾接受 void *mutex 参数：
 *   - 传 rb->mutex：自动加锁/解锁（单步操作）
 *   - 传 nullptr：  跳过锁，调用者自行持锁（多步原子序列）
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief 编译期静态声明一个环形缓冲区实例
 * @param name  实例名
 * @param sz    容量（字节），应为 2 的幂以优化取模
 */
#define RB_DEFINE(name, sz)        \
    static uint8_t name##_buf[sz]; \
    ring_buffer_t name = {.data = name##_buf, .size = sz, .mutex = nullptr}

#define RB_DEFINE_CCM(name, sz)                                                 \
    static uint8_t name##_buf[sz] __attribute__((section(".ccmram")));          \
    ring_buffer_t name = {.data = name##_buf, .size = sz, .mutex = nullptr}

/**
 * @brief 协议 TU 提供通道 RB（weak getter；多协议同槽合并为单一静态体）
 *
 * 未编入任何 provide → 函数符号为 0，acquire 得 nullptr。
 * 编入 ≥1 个 provide → 链接器保留其中一个 getter（及其 static 缓冲）。
 *
 * @note getter 须为裸标识符（如 rb_provide_rj45），勿传宏名（## 不二次展开）。
 */
#define RB_PROVIDE_WEAK(getter, sz)                                                                \
    __attribute__((weak)) ring_buffer_t *getter(void)                                              \
    {                                                                                              \
        static uint8_t getter##_buf[sz];                                                           \
        static ring_buffer_t getter##_rb = {.data = getter##_buf, .size = (sz), .mutex = nullptr}; \
        return &getter##_rb;                                                                       \
    }

/**
 * @brief 带来源标记版本的通道 RB 提供宏（多逻辑通道共享同一物理 RB 时使用）
 *
 * 额外分配与 data 等长的 owner[] 数组，写入时逐字节记录来源通道 ID；
 * 读帧时用 rb_head_owner() 取帧首字节的来源，帧归属不再依赖「通知通道」。
 * 仅共享 RB（如 RJ45：TCP Server/TCP Client/UDP 三通道共用一个）需要。
 */
#define RB_PROVIDE_WEAK_OWNER(getter, sz)                                                          \
    __attribute__((weak)) ring_buffer_t *getter(void)                                              \
    {                                                                                              \
        static uint8_t getter##_buf[sz];                                                           \
        static uint8_t getter##_own[sz];                                                           \
        static ring_buffer_t getter##_rb = {.data   = getter##_buf,                                \
                                            .size   = (sz),                                        \
                                            .mutex  = nullptr,                                     \
                                            .owner  = getter##_own};                               \
        return &getter##_rb;                                                                       \
    }

/** @brief 环形缓冲区结构体 */
typedef struct {
    uint8_t *data;
    uint16_t size;
    volatile uint16_t read_index;
    volatile uint16_t write_index;
    void *mutex;   /**< osMutexId_t，nullptr 表示未绑定锁 */
    uint8_t *owner; /**< 来源标记数组，与 data 逐字节配对；nullptr = 不记录（单通道 RB 无需） */
} ring_buffer_t;

/** @brief 运行时初始化：创建并绑定优先级继承互斥锁 */
void rb_init(ring_buffer_t *rb, const char *name);

/** @brief 手动加锁（多步原子序列开始时调用） */
void rb_lock(const ring_buffer_t *rb);

/** @brief 手动解锁（多步原子序列结束时调用） */
void rb_unlock(const ring_buffer_t *rb);

/* ---- 环形缓冲区 API（mutex 参数控制是否加锁）---- */

/* 状态查询 */
bool rb_empty(const ring_buffer_t *rb, void *mutex);
bool rb_full(const ring_buffer_t *rb, void *mutex);
uint16_t rb_avail(const ring_buffer_t *rb, void *mutex);
uint16_t rb_space(const ring_buffer_t *rb, void *mutex);
void rb_flush(ring_buffer_t *rb, void *mutex);

/* 写入 */
bool rb_putc(ring_buffer_t *rb, uint8_t byte, void *mutex);
uint16_t rb_write(ring_buffer_t *rb, const uint8_t *data, uint16_t len, void *mutex);
/** @brief 带来源标记写入：owner 为通道 ID（uint8_t），写入的每个字节配对记录；
 *  rb->owner 为 nullptr 时退化为普通写入 */
uint16_t rb_write_from(ring_buffer_t *rb, const uint8_t *data, uint16_t len, void *mutex,
                       uint8_t owner);
/** @brief 读指针处（帧首字节）的来源标记；无标记/空缓冲返回 0xFF */
uint8_t rb_head_owner(const ring_buffer_t *rb, void *mutex);

/* 读取 */
bool rb_getc(ring_buffer_t *rb, uint8_t *byte, void *mutex);
uint16_t rb_read(ring_buffer_t *rb, uint8_t *data, uint16_t len, void *mutex);

/* 窥视（不移动读指针） */
bool rb_peekc(const ring_buffer_t *rb, uint16_t offset, uint8_t *byte, void *mutex);
uint16_t rb_peek(const ring_buffer_t *rb, uint16_t offset, uint8_t *dest, uint16_t len, void *mutex);

/* 工具 */
uint16_t rb_contig(const ring_buffer_t *rb, uint16_t offset, void *mutex);
uint16_t rb_skip(ring_buffer_t *rb, uint16_t len, void *mutex);
