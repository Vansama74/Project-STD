# STD 项目的 A/B 协议链式解析规划

## 1. 背景

STD 项目当前已经具备统一的通道接入层、协议分发层和协议自注册机制。现有实现中，`app_dispatch` 已完成以下能力：

- 通道接收后写入共享 ring buffer
- `frame_dispatch_task()` 从 ring buffer 中探测完整帧
- 每个协议模块通过 `sw_app_initcall(...)` 自注册
- 协议模块通过 `app_proto_register()`、`app_proto_bind_channel()`、`app_proto_set_frame_queue()` 完成接入

在现有架构基础上，可以进一步演进为 **A/B 协议链式解析**：

- 一帧数据到达后，先交给协议 A 探测
- A 判断失败时，再交给协议 B
- 若 A 只是“数据未收完整”，必须继续等待，不能让 B 抢走该帧
- 最终只能有一个协议消费该帧，避免重复执行

该方案适合：

- 同一物理通道上需要兼容多种协议
- 新旧协议过渡期
- 协议帧结构相近，需要逐级尝试匹配
- 希望保留当前通道层、分发层、协议层的分层架构

---

## 2. 目标

### 2.1 功能目标

- 支持同一帧数据按优先级依次尝试多个协议模块
- 支持协议 A 失败后自动切换到协议 B
- 支持“数据未收完整”的等待态，避免误判
- 支持协议模块在 EIDE 中加入链接后，通过各自 `xx_proto.init` 自注册
- 支持每个协议在自注册时声明自己的 `priority`
- 支持当多个协议 `priority` 相同或冲突时，调度器可按随机/动态方式打散相对顺序，避免固定偏置

### 2.2 工程目标

- 尽量少改通道层代码
- 尽量不改原有协议模块的业务执行逻辑
- 在 STM32F407 平台上控制 RAM 占用
- 避免引入过多额外任务和队列

---

## 3. 现有架构现状

结合当前代码，STD 项目已具备 A/B 链式解析的基础：

### 3.1 `app_dispatch` 已有的关键对象

- `g_dispatch.ch_queue`
- `g_dispatch.proto_rb[]`
- `g_dispatch.proto_probe[]`
- `g_dispatch.frame_queue[]`
- `g_dispatch.ch_proto_map[]`
- `g_dispatch.registered_mask`

### 3.2 现有接收与分发链路

从 `Application/Src/app_dispatch.c` 可见，现有接收链路为：

```text
通道任务
  -> app_channel_dispatch()
  -> ring buffer 写入
  -> g_dispatch.ch_queue 通知
  -> frame_dispatch_task()
  -> probe()
  -> frame_queue
  -> 协议处理任务
```

### 3.3 现有协议模块示例

- 青海协议：`Application/Src/ProtocolParser_QingHai/app_qh_proto.c`
- LDI 协议：`Application/Src/LDI/app_ldi.c`

这两个协议模块都已经具备：

- 申请 ring buffer
- 注册 probe
- 绑定通道
- 创建处理任务

因此，A/B 链式解析可以在**协议调度层**实现，而不必重构底层通信栈。

---

## 4. 链式解析的核心定义

### 4.1 基本流程

```text
一帧数据到达
  -> 尝试协议 A
      -> A 成功：结束
      -> A 失败：尝试协议 B
          -> B 成功：结束
          -> B 失败：继续下一协议或丢弃
```

### 4.2 状态语义

协议探测函数建议统一使用以下状态：

- `PROTO_PROBE_WAIT`：帧未收完整，继续等待
- `PROTO_PROBE_FAKE`：这帧不属于本协议，允许下一个协议尝试
- `PROTO_PROBE_READY`：本协议已确认，可接管该帧
- `PROTO_PROBE_SKIP`：帧结构完整但不属于当前设备，可跳过整帧（可选扩展）

其中最关键的是：

- `WAIT` 不能让后续协议接管
- `FAKE` 才允许切换到下一协议
- `READY` 立即停止链条

---

## 5. 具体类图 / 时序图

### 5.1 类图（逻辑结构图）

```text
+-------------------+          +-------------------------+
|    channel_t      |          |      dispatch_ctx_t     |
|-------------------|          |-------------------------|
| ch_id             |          | ch_queue                |
| state             |          | proto_rb[]              |
| ops --------------+--------->| proto_probe[]           |
+-------------------+          | frame_queue[]           |
                               | ch_proto_map[]          |
                               | registered_mask         |
                               +-----------+-------------+
                                           |
                                           |
                                           v
                               +-------------------------+
                               |   ring_buffer_t         |
                               |-------------------------|
                               | buf                     |
                               | size                    |
                               | mutex                   |
                               +-------------------------+

+-------------------+          +-------------------------+
|  protocol A/B     |          |    frame_msg_t          |
|-------------------|          |-------------------------|
| sw_app_initcall   |          | ch                      |
| init()            |          | data[]                  |
| probe()           |          | data_len                |
| execute()         |          +-------------------------+
| task()            |
+-------------------+
```

### 5.2 时序图（接收路径）

```text
UART/UDP/TCP 通道任务
    -> app_channel_dispatch(ch, data, len)
    -> 写入共享 ring buffer
    -> 唤醒 frame_dispatch_task

frame_dispatch_task
    -> 读取通道对应协议掩码
    -> 按优先级尝试协议 A
        -> A.probe(...)
            -> READY: 读取完整帧，投递 A.frame_queue
            -> WAIT: 保持等待
            -> FAKE: 尝试协议 B
    -> 若 A 失败，再尝试协议 B
        -> B.probe(...)
            -> READY: 读取完整帧，投递 B.frame_queue
            -> WAIT: 保持等待
            -> FAKE: 继续下一个协议

协议处理任务 A/B
    -> osMessageQueueGet(frame_queue)
    -> parse
    -> execute
```

---

## 6. 接口设计稿

本节将 A/B 链式解析从“规划”进一步收敛为“接口设计”。设计目标是：

- 保持现有 `app_dispatch` 框架不大改
- 让协议模块在各自 `xx_proto.init()` 中完成自注册
- 让协议优先级成为一等公民
- 对于相同 priority 的协议，给出明确、可复现的注册解决方案，而不是随机/动态打散

### 6.1 注册接口定义

建议新增注册接口：

```c
proto_mask_t app_proto_register_ex(proto_probe_fn_t probe,
                                   ring_buffer_t *rb,
                                   uint16_t priority,
                                   proto_reg_flags_t flags);
```

#### 参数说明

- `probe`
  - 类型：`proto_probe_fn_t`
  - 说明：协议探测函数指针
  - 要求：返回 `PROTO_PROBE_WAIT` / `PROTO_PROBE_FAKE` / `PROTO_PROBE_READY` / `PROTO_PROBE_SKIP`

- `rb`
  - 类型：`ring_buffer_t *`
  - 说明：协议共享的接收缓冲区
  - 要求：由 `app_proto_acquire_buf()` 返回

- `priority`
  - 类型：`uint16_t`
  - 说明：协议优先级，数值越大优先级越高
  - 取值范围：`0 ~ 65535`
  - 推荐约定：
    - `0`：最低优先级
    - `100`：普通协议
    - `200` 以上：主协议 / 强优先协议

- `flags`
  - 类型：`proto_reg_flags_t`
  - 说明：注册行为标志位
  - 当前建议至少保留：
    - `PROTO_REG_DEFAULT = 0x00`
    - `PROTO_REG_EXCLUSIVE = 0x01`：同优先级下，优先级组内只允许单协议占位；若发生冲突，需显式调整 priority 或注册顺序

### 6.2 priority 的数据类型与排序规则

#### 数据类型

`priority` 建议采用 `uint16_t`，原因如下：

- 足够表达协议优先级分层
- 比 `uint8_t` 更灵活，便于未来扩展
- 比 `uint32_t` 更节省结构体空间

#### 排序规则

排序原则如下：

1. **按 priority 从大到小排序**
2. **priority 相同的协议进入同一组**
3. **同组内按 tie-break 规则决定先后顺序**

### 6.3 同 priority 时的注册解决方案

本方案**不采用随机/动态打散策略**。对于 priority 相同的协议，采用**显式注册顺序 + 唯一注册位**的方式进行解决。

#### 设计原则

- priority 仍然作为第一排序条件，数值越大优先级越高
- priority 相同的协议，不做随机打散
- priority 相同的协议，按注册先后顺序进入稳定排序
- 若同优先级协议必须并存，则应通过显式注册顺序和唯一注册位避免冲突
- 若某个协议组要求“独占优先级段”，则同 priority 的重复注册应视为配置冲突并在初始化阶段告警

#### 推荐实现

在系统启动或协议注册时，为每个协议生成一个递增的 `reg_seq`：

- `reg_seq` 由框架在 `app_proto_register_ex()` 内自动分配
- 排序时先比较 `priority`
- `priority` 相同，再比较 `reg_seq`
- `reg_seq` 越小，注册越早，排序越靠前

这样可以保证：

- priority 高的协议始终优先
- 同优先级协议的顺序可复现、可调试
- 协议模块只需通过 init 顺序控制谁先注册

#### 同优先级冲突处理建议

对于某些需要严格区分的协议组，建议采用以下策略之一：

1. **显式调整 priority**：最推荐，避免同级冲突
2. **固定注册顺序**：由 `xx_proto.init()` 的初始化顺序决定
3. **独占注册位**：若某协议已占据该优先级段，则后注册协议直接返回失败并输出配置错误

#### 不采用的策略

- 不采用启动时随机
- 不采用每帧轮换
- 不采用运行期动态重排

原因是这些策略会增加调试复杂度，不利于协议接管行为的稳定复现。

### 6.4 推荐的注册流程

协议模块在自己的 `xx_proto.init()` 中完成如下流程：

1. `app_proto_acquire_buf()` 获取 ring buffer
2. `app_proto_register_ex()` 注册探测器和 priority
3. `app_proto_bind_channel()` 绑定通道
4. `app_proto_set_frame_queue()` 设置协议队列
5. `osThreadNew()` 创建处理任务

### 6.5 多协议模块共享 ring buffer 的实现规划

A/B 链式解析的核心前提之一，是**多个协议模块共享同一份 ring buffer**，而不是每个协议各自维护一份接收缓存。

#### 设计目标

- 同一物理通道收到的数据，只写入一次
- 多个协议模块可以对同一份 ring buffer 进行 probe
- 避免 A/B 协议各自复制一份数据，节省 RAM
- 允许多个协议绑定同一通道，但只共享一份接收缓存

#### 当前代码基础

结合当前 `app_dispatch.c`，项目已经具备共享 ring buffer 的雏形：

- `app_proto_acquire_buf(id, size)` 会按 `id` 复用同一份静态 ring buffer
- `g_dispatch.proto_rb[]` 保存协议对应的 ring buffer 指针
- `app_channel_dispatch()` 会按通道掩码，把接收到的数据写入匹配的 ring buffer
- `frame_dispatch_task()` 会从 ring buffer 中探测完整帧

因此，实现共享 ring buffer 的关键不是“新增缓存”，而是**让多个协议模块显式绑定同一个 rb_id**。

#### 推荐实现方式

协议模块在各自 `xx_proto.init()` 中调用：

```c
ring_buffer_t *rb = app_proto_acquire_buf(1, 512);
```

其中 `id=1` 表示同一组通用协议共享同一个 ring buffer。这样：

- 青海协议与 LDI 协议如果希望链式兼容，可以绑定同一个 `rb_id`
- `app_channel_dispatch()` 只需要把数据写入一次
- `frame_dispatch_task()` 可在同一份数据上按优先级依次 probe

#### 共享规则

建议约束如下：

- **同一通道、同一兼容组的协议共享同一 `rb_id`**
- 共享同一 `rb_id` 的协议应保证 probe 逻辑严格、不会误吞别人的帧
- 如果两个协议格式差异很大，可分配不同 `rb_id`，避免无效探测开销
- 共享 ring buffer 仅共享“接收数据”，不共享“协议队列”与“处理任务”

#### 内存收益

共享 ring buffer 的直接收益：

- 避免同一帧被复制到多份缓存
- 降低 CCMRAM/SRAM 压力
- 减少缓存一致性和调试复杂度

#### 实施注意事项

- 共享 ring buffer 不能替代协议队列：每个协议的 `frame_queue` 仍应独立
- 共享 ring buffer 不能让多个协议同时“消费”同一帧，最终消费权仍由优先级决定
- 如果共享组中某协议的 probe 太宽松，会影响其他协议的命中率，因此必须严格定义 `WAIT/FAKE/READY`

### 6.8 `app_proto_register_ex()` 的接口草案

为了便于后续编码，这里给出建议接口草案：

```c
/**
 * @brief 注册协议探测器，并指定优先级与注册属性。
 * @param probe     协议探测函数
 * @param rb        协议共享 ring buffer
 * @param priority  协议优先级，数值越大越优先
 * @param flags     注册标志位
 * @return          协议掩码；0 表示注册失败
 */
proto_mask_t app_proto_register_ex(proto_probe_fn_t probe,
                                   ring_buffer_t *rb,
                                   uint16_t priority,
                                   proto_reg_flags_t flags);
```

#### 参数约束建议

- `probe` 不能为空
- `rb` 不能为空
- `priority` 不能越界（`uint16_t` 自然约束）
- `flags` 用于描述注册策略，不参与协议内容解析

#### 建议的内部排序字段

框架内部建议为每个注册协议维护以下信息：

- `priority`：主排序字段
- `reg_seq`：注册序号，保证同优先级时排序稳定
- `flags`：注册行为控制位
- `rb`：共享接收缓存指针
- `probe`：探测函数指针

### 6.9 `app_dispatch` 相关数据结构设计稿

结合当前 `Application/Src/app_dispatch.c` 的实现，若要支持多协议链式解析与共享 ring buffer，建议在 `dispatch_ctx_t` 基础上补充一层“协议注册元数据”，将**协议注册信息**与**运行时分发信息**分离。

#### 6.9.1 设计目标

- 让协议注册信息可排序、可复现、可调试
- 让共享 ring buffer 的分组信息显式化
- 让 `frame_dispatch_task()` 可以按“优先级 + 注册顺序”稳定遍历协议
- 避免把排序逻辑散落到各个协议模块中

#### 6.9.2 建议新增的注册信息结构体

```c
typedef struct {
    proto_mask_t      mask;        /* 协议掩码 */
    proto_probe_fn_t  probe;       /* 探测函数 */
    ring_buffer_t    *rb;          /* 共享 ring buffer */
    osMessageQueueId_t frame_queue;/* 协议帧队列 */
    channel_mask_t    ch_mask;     /* 绑定的通道集合 */
    uint16_t          priority;    /* 协议优先级 */
    uint16_t          reg_seq;     /* 注册序号，用于稳定排序 */
    uint16_t          rb_id;       /* 共享 ring buffer 分组编号 */
    uint8_t           flags;       /* 注册标志位 */
    uint8_t           state;       /* 注册状态：空闲/已注册/已失效 */
} proto_reg_info_t;
```

#### 字段说明

- `mask`
  - 协议的唯一掩码，由框架分配
- `probe`
  - 协议探测函数
- `rb`
  - 当前协议绑定的共享 ring buffer
- `frame_queue`
  - 当前协议的帧队列
- `ch_mask`
  - 当前协议绑定的通道集合
- `priority`
  - 主排序字段，越大越先尝试
- `reg_seq`
  - 注册顺序序号，用于同优先级时稳定排序
- `rb_id`
  - 共享 ring buffer 分组号，用于描述哪些协议共用同一份接收缓存
- `flags`
  - 注册行为控制位，例如是否独占优先级段
- `state`
  - 注册元数据状态，便于调试和动态失效处理

#### 6.9.3 建议的派生排序视图

为了避免每次分发都临时排序，建议在 `dispatch_ctx_t` 内维护一个**排序后的协议索引视图**：

```c
typedef struct {
    uint8_t   order[PROTO_MAX_COUNT]; /* 排序后的协议索引 */
    uint8_t   count;                  /* 当前有效协议数量 */
    bool      dirty;                  /* 注册变更后置脏，需要重建排序视图 */
} proto_order_view_t;
```

用途：

- `app_proto_register_ex()` / `app_proto_bind_channel()` 更新后把 `dirty` 置位
- `frame_dispatch_task()` 在发现 `dirty` 时重建排序视图
- 重建后按照 `priority -> reg_seq` 固定排序

#### 6.9.4 建议扩展 `dispatch_ctx_t`

在当前 `g_dispatch` 的基础上，建议增加以下字段：

```c
typedef struct {
    ring_buffer_t    *buf_pool[RB_CNT_MAX];
    proto_reg_info_t  proto_info[PROTO_MAX_COUNT];
    proto_order_view_t order_view;
    uint32_t          registered_mask;
    uint32_t          ch_proto_map[MAX_CHANNELS];
    osMessageQueueId_t frame_queue[PROTO_MAX_COUNT];
    ring_buffer_t     *proto_rb[PROTO_MAX_COUNT];
    proto_probe_fn_t   proto_probe[PROTO_MAX_COUNT];
    osMessageQueueId_t ch_queue;
} dispatch_ctx_t;
```

说明：

- `proto_info[]` 是协议元数据主表
- `order_view` 是调度阶段读取的排序快照
- `proto_rb[]` / `proto_probe[]` / `frame_queue[]` 可以继续保留，作为对外兼容的快捷索引

#### 6.9.5 共享 ring buffer 分组表

如果需要更清晰地表达“哪些协议共享同一份 ring buffer”，建议增加分组表：

```c
typedef struct {
    uint16_t rb_id;                       /* 分组编号 */
    ring_buffer_t *rb;                    /* 共享 ring buffer */
    uint32_t proto_mask;                  /* 该组内协议掩码集合 */
    uint16_t member_count;                /* 成员数 */
} rb_group_info_t;
```

该结构体的作用是：

- 让 `app_dispatch` 很容易知道哪些协议共享同一份接收缓存
- 帮助 `frame_dispatch_task()` 做“按 rb 分组去重探测”
- 方便调试时打印“当前组内成员”

#### 6.9.6 推荐的排序规则实现

排序建议使用以下比较顺序：

1. `priority` 降序
2. `rb_id` 固定组内优先（可选）
3. `reg_seq` 升序
4. `mask` 升序作为最后稳定项

这样可以保证：

- 高优先级协议先尝试
- 同优先级协议排序稳定
- 共享同一 rb 的协议能在组内按固定规则去重

---

## 7. `app_dispatch` 的伪代码

结合当前 `Application/Src/app_dispatch.c` 的实现，链式解析可以抽象成如下伪代码：

```c
void frame_dispatch_task(void *argument)
{
    while (1) {
        ch = wait_channel_notification();
        if (!ch) continue;

        proto_mask = g_dispatch.ch_proto_map[ch->ch_id];

        for each rb in protocol_ring_buffers_covered_by_this_channel:
            lock(rb);

            while (rb_has_data(rb)) {
                parsed = false;
                all_wait = true;

                for each protocol in priority_order:
                    if protocol not bound to this channel:
                        continue;
                    if protocol does not use this rb:
                        continue;

                    state = protocol.probe(ch, rb, &frame_len, &aux);

                    if (state == READY) {
                        if (rb_available >= frame_len) {
                            read full frame from rb;
                            push frame to protocol.frame_queue;
                            parsed = true;
                            all_wait = false;
                            break;
                        }
                    } else if (state == SKIP) {
                        skip full frame bytes;
                        parsed = true;
                        all_wait = false;
                        break;
                    } else if (state == FAKE) {
                        all_wait = false;
                    } else if (state == WAIT) {
                        // keep waiting
                    }
                }

                if (!parsed) {
                    if (all_wait)
                        break;      // wait for more bytes
                    else
                        skip 1 byte; // false header, resync
                }
            }

            unlock(rb);
        }
    }
}
```

### 7.1 伪代码与当前实现的对应关系

当前 `app_dispatch.c` 中已经具备如下结构：

- `g_dispatch.registered_mask`
- `g_dispatch.proto_rb[]`
- `g_dispatch.proto_probe[]`
- `g_dispatch.frame_queue[]`
- `rb_lock()` / `rb_unlock()`
- `rb_avail()` / `rb_read()` / `rb_skip()`

因此，只需要在分发循环里把“单协议探测”升级成“按优先级链式探测”即可。

---

## 8. A/B 两个协议的注册示例

下面示例以“青海协议 A、LDI 协议 B”为例说明。示例仅体现注册方式，不改动原有架构。

### 8.1 协议 A：青海协议注册示例

文件：

```text
Application/Src/ProtocolParser_QingHai/app_qh_proto.c
```

示例注册流程：

```c
void qh_proto_init(void)
{
    ring_buffer_t *rb = app_proto_acquire_buf(1, 512);
    s_qh_mask = app_proto_register(qh_proto_probe_frame, rb);
    if (s_qh_mask == 0)
        return;

    app_proto_bind_channel(s_qh_mask, CH_ID_RS485);
    app_proto_bind_channel(s_qh_mask, CH_ID_RS232);

    s_qh_queue = osMessageQueueNew(2, sizeof(frame_msg_t) + FRAME_DATA_MAX_LEN, &s_qh_queue_attr);
    app_proto_set_frame_queue(s_qh_mask, s_qh_queue);

    osThreadNew(qh_proto_handle_task, NULL, &s_qh_task_attr);
}
```

### 8.2 协议 B：LDI 注册示例

文件：

```text
Application/Src/LDI/app_ldi.c
```

示例注册流程：

```c
[[maybe_unused]] static void ldi_module_init(void)
{
    ring_buffer_t *rb = app_proto_acquire_buf(1, 512);
    s_ldi_mask = app_proto_register(ldi_probe_frame, rb);
    if (s_ldi_mask == 0)
        return;

    app_proto_bind_channel(s_ldi_mask, CH_ID_RS485);
    app_proto_bind_channel(s_ldi_mask, CH_ID_TCP_SERVER);
    app_proto_bind_channel(s_ldi_mask, CH_ID_TCP_CLIENT);
    app_proto_bind_channel(s_ldi_mask, CH_ID_UDP);

    ldi_ctx_init(&g_ldi);
    ldi_device_init();

    s_ldi_queue = osMessageQueueNew(...);
    app_proto_set_frame_queue(s_ldi_mask, s_ldi_queue);

    osThreadNew(ldi_handle_task, NULL, &ldi_task_attr);
    osThreadNew(ldi_timer_task, NULL, &ldi_timer_task_attr);
}
sw_app_initcall(ldi_module_init);
```

### 8.3 A/B 并行时的注册约束

如果要做链式解析，建议协议注册遵循：

- 同一通道可以绑定多个协议
- 协议分发顺序由 priority 决定
- probe 函数必须严格区分 `WAIT` / `FAKE` / `READY`
- 一帧只允许一个协议最终消费

### 8.4 多协议共通道的风险约束

结合现有工程实现，多个协议共同绑定同一个 `RS485/RS232` 通道时，必须额外控制以下风险：

- **协议抢帧**：多个协议同时认为一帧属于自己，必须依赖 priority 和单消费机制避免重复执行。
- **WAIT/FAKE 误判**：半包不能被后续协议抢走；不属于本协议的帧必须明确返回 FAKE，而不是 WAIT。
- **重复接收与缓存膨胀**：同一通道的数据不能为每个协议各自复制一份，应通过共享 ring buffer 只写一次。
- **优先级偏置**：若没有稳定排序规则，某个协议会长期压制其它协议，导致兼容协议形同虚设。
- **调试复杂度增加**：同一帧可能在多个协议 probe 之间切换，需要保留清晰的日志、断点和统计信息。
- **半双工发送冲突**：尤其在 RS485 上，多个协议同时回复可能引起方向控制或总线冲突，因此发送路径仍应统一走通道层。
- **误命中问题**：格式相似的协议必须严格校验帧头、长度、帧尾与命令合法性，避免宽松探测导致抢帧。
- **错误恢复困难**：若帧错位，需要明确是跳过 1 字节还是整帧恢复同步，否则可能影响后续连续帧。

因此，多协议共通道模式必须建立在“共享缓存 + 优先级仲裁 + 单协议消费 + 严格探测”四个前提之上。

---

## 9. RAM 约束分析

STM32F407 项目中，RAM 需要同时容纳：

- FreeRTOS heap
- LwIP heap
- 多个任务栈
- 显示缓冲区
- UART 接收缓冲区
- 协议模块上下文与队列

因此 A/B 链式解析方案应尽量遵守以下原则：

### 9.1 尽量不新增独立长期任务

如果每个协议都各自保留处理任务，会带来：

- 额外任务栈
- 额外消息队列
- 更多上下文切换

在 RAM 紧张时不推荐。

### 9.2 优先采用“单分发任务 + 多协议尝试”

推荐模式：

- 一个 `frame_dispatch_task`
- 一个公共 ring buffer
- 多个协议按优先级尝试 probe
- 成功者接管

这样 RAM 使用更稳定，调度也更简单。

### 9.3 协议任务尽量轻量化

若某协议必须保留独立任务，应尽量：

- 减少任务栈
- 减少队列深度
- 避免在任务中进行复杂复制

### 9.4 多协议共享通道时的内存与任务建议

在当前 STD 工程里，若青海协议与 LDI 协议等多个协议共享同一个 485/232 通道，建议遵循：

- **接收缓存共享**：同一兼容组使用同一 `rb_id`，避免重复分配 ring buffer。
- **处理任务独立**：每个协议保留自己的 `frame_queue` 与处理任务，避免业务逻辑互相干扰。
- **协议初始化轻量化**：初始化阶段仅做注册、绑定、队列创建与任务创建，不在 init 中做重活。
- **共享通道但不共享执行上下文**：共享的是输入字节流，不共享协议状态机、业务上下文或输出动作。
- **避免多个协议同时重回复**：特别是 RS485 上，回复动作应由最终接管协议独立完成，避免总线冲突。

---

## 10. 任务调度考虑

### 10.1 推荐调度模型

```text
通道任务
  -> 写入环形缓冲区
  -> 唤醒 frame_dispatch_task
  -> frame_dispatch_task 按优先级尝试协议 A/B
  -> 成功协议进入各自执行路径
```

### 10.2 调度特点

- 协议尝试在一个任务中串行完成，避免竞态
- A 失败后 B 继续尝试，逻辑清晰
- 适合帧长度有限、协议探测成本较低的场景

### 10.3 风险

- 若 probe 过重，可能增加单帧分发延迟
- 若 A 的判定过宽，可能抢走 B 的帧
- 若 `WAIT` / `FAKE` 语义混乱，容易误解析

---

## 11. 推荐的落地步骤

### 11.1 第一阶段：设计协议优先级链

- 为协议注册接口增加 priority
- 在分发器中按 priority 排序
- 要求每个协议模块在自己的 `xx_proto.init()` 中完成自注册，并同时声明 priority
- 对于 priority 相同的协议，采用固定的注册顺序或独占注册位策略，不使用随机/动态打散
- 保持现有协议模块代码不变

### 11.2 第二阶段：定义状态语义

- 严格定义 `WAIT / FAKE / READY`
- 所有协议的 probe 函数统一遵守

### 11.3 第三阶段：先验证两个协议并行

建议先用两个协议模块验证：

- 协议 A：青海协议
- 协议 B：LDI 协议

在同一通道上测试：

- A 成功时不触发 B
- A 失败时 B 接管
- 半包场景返回 WAIT

### 11.4 第四阶段：优化 RAM 与任务结构

若验证通过，再评估是否：

- 合并部分协议任务
- 减少消息队列深度
- 复用缓冲区

---

## 12. 适用与不适用场景

### 12.1 适用场景

- 青海协议与 LDI 协议共存
- 旧设备兼容新协议
- 调试期需要同时支持多种上位机帧格式
- 一个通道可能接收到不同来源、不同版本协议的数据

### 12.2 不适用场景

如果系统只会长期固定一种协议，且永远不会共存，那么 A/B 链式解析会略显复杂。

在这种情况下，直接用工程排除方式切换协议，维护成本更低。

---

## 13. 总结

A/B 协议链式解析是可行的，而且与 STD 项目现有的分层架构兼容。

推荐的最终形态是：

- 一个公共接收缓存
- 一个帧分发任务
- 多个协议按优先级依次 probe
- WAIT 继续等待，FAKE 交给下一个协议，READY 立即接管
- 一帧只允许一个协议最终消费

从 RAM 与任务调度角度看，这种模式比“每个协议一个独立解析链路”更适合 STM32F407 这类资源有限平台。
