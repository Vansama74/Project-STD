# 02 存储分区宪法（RAM）

> **状态：冻结**。本仓库 **SRAM/CCM 归属** 的最高决策。  
> **修订**：走 README §1。  
> **协议谁绑哪个 RB**：doc/05；本文件只定 **物理段归属**。

---

## §1 三条原则

### ① SRAM = 固定大小数据

| 主体 | 宪法位置 | 现行 | 动作 |
|------|----------|------|------|
| ucHeap 36KB | SRAM `.bss` | **SRAM** | ✅ Phase A 已回退 |
| 协议帧队列 | SRAM | SRAM | 维持 |
| 协议 RB | SRAM | SRAM（provide **1536/768/768**） | 维持（05 已落地；尺寸以 05 为准） |

### ② CCM = 显存 / BSRR（性能路径）

| 主体 | 宪法位置 | 现行 |
|------|----------|------|
| pixel_map / hub75_buff / g_bsrr / row_dst | CCM | CCM |
| RTOS 任务栈 + TCB（静态分配，CPU 独占、无 DMA） | CCM（2026-09-17 追加） | CCM（23 个任务 + 空闲/定时器任务，共 27076B，见 [04 §8](./04_current_memory_occupancy.md)） |

### ③ CCM 预留大屏余量

1-969 ≈38208B（58.3%）；堆回退后余 ≈26.7KB。  
堆不得长期占 CCM。

---

## §2 方向方向

1. **Phase A 回退**：✅ `configAPPLICATION_ALLOCATED_HEAP`→0；删 `freertos_heap_ccm.c`；`heap_4` static ucHeap → SRAM（现行 **36KB**）。  
2. **显存留 CCM**：不翻案。  
3. **RB 留 SRAM**：行为归 05；禁止再把调度 RB 迁回 CCM「给堆让位」。  
4. **1-969 不是另案**：堆回退后自然容纳。

配套瘦身：RTT Up 4KB→2KB（见 [05_migration_plan.md](./05_migration_plan.md)）。

---

## §3 必须保留的事实

### DMA/ETH 门禁（永留 SRAM）

ram_heap、RX_POOL、PBUF_POOL、ETH 描述符、UART RX DMA、`s_dma_bounce`；`_ptr_in_ccm` 检测保留。

### 构建隔离

占用权威：EIDE Debug（**1-577 3×3**；exclude 1-260 / 1-969 / RLS / AH）→ [04](./04_current_memory_occupancy.md)。  
Makefile（1-260 + RLS/AH 全编）数字不得与之混用。

---

## §4 决策矩阵

| 决策项 | 裁定 |
|--------|------|
| ucHeap | **SRAM**（36KB） |
| RB | **SRAM**（已到位） |
| 显存 | **CCM** |
| 帧队列 | **SRAM** |

优先级：DMA 门禁 > 数据性质 > 性能。

---

## §5 长期验收（迁移完成后）

- [x] EIDE 大屏链接成功（Phase A 验证口径 1-969；现行 1-577 3×3，CCM 同为 38208）；更新 04  
- [x] `ucHeap` 不在 `0x1000…`  
- [ ] ETH/UART/字库冒烟  
- [x] `xPortGetMinimumEverFreeHeapSize` 可读化（2026-09-17）：`[diag] heap … free/min` 随开机横幅与
      逐通道探针常驻输出（见 04 §8.5）；**现场重标定**仍待实机执行（把 `min` 与 §8.3 预算表对账）。

---

## §6 修订（2026-09-17）：CCM 允许承载「RTOS 任务栈 / TCB」

**背景**：全协议 dev 构建任务数 30+，启动期 ucHeap 需求 38032B > 可用 36856B
（赤字 1176B），现场在 `app_rs485_start` 处 `pvPortMalloc` 失败（详见 04 §8）。

**裁定**：把「全生命周期只创建一次」的任务栈 + `StaticTask_t` 静态分配到
`.ccmram`（`Platform/Inc/pl_task_static.h` → `xTaskCreateStatic`），
23 个应用任务 + 空闲/定时器任务共 **27076B** 进 CCM，**ucHeap 释放 25800B**。

**与本文既有条款的关系（无冲突）**：

* §1③「**堆**不得长期占 CCM」——本轮**没有**把 ucHeap 迁入 CCM，
  `configAPPLICATION_ALLOCATED_HEAP` 仍为 0、`ucHeap` 仍在 SRAM（`nm` 实证
  `0x20015c3c`）；迁移对象是**任务栈/TCB**（CPU 独占数据，非堆）。
* §3「DMA/ETH 门禁」——任务栈从不交给 DMA，CCM 的非 DMA 可达性对它是无关属性。
* §1② 的 CCM 判别标准从「显存/BSR」扩展为「**CPU 独占、且非 DMA/ETH 可达需求**」，
  队列体（CQ/GZ_OL/YN_OL，2026-08-20/09-14/09-17 先例）与本轮任务栈同属此类。

**量化事实**：CCM 全片占用 `ALL/1_263 = 38332B / 65536B`（余 27204B）、
`CQ/1_263 = 36076B`（余 29460B）、`ALL/22_1665 = 39036B`（余 26500B）；
SRAM 余量 `ALL = 2136B`（修复前 400B）、`CQ = 6408B`。**新增 CCM 占用前先看余量**。
