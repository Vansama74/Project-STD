# 多协议共享 RB — 落地状态（对照当前代码）

> **基准**：2026-08-14（含 RB/DMA/queue 修订后）  
> **总体结论：RB / 协议绑定 DoD = READY（已落地）**  
> 设计规范：[01_architecture.md](./01_architecture.md)  
> RAM 占用权威：[`../06_SRAM内部分数据迁移/04_current_memory_occupancy.md`](../06_SRAM内部分数据迁移/04_current_memory_occupancy.md)  
> LDI 选编依赖：[`../07_LDI与IAP配置解耦/`](../07_LDI与IAP配置解耦/README.md)

---

## 1. 验收项对照

| 编号 | 判定 | 现状 |
|------|------|------|
| A-1 单固件多协议 | ✅ | Makefile/EIDE 同编 LDI + 青海 + RLS + IAP；各自 `sw_app_initcall` |
| A-2 目录选编 | ⚠️ | 无 `APP_PROTOCOL`；排除协议目录可裁解析器。**排除整个 `IAP/` 时须保留 `app_iap_cfg.c`**，否则 LDI 链接失败（见 doc/07） |
| A-3 自注册 | ✅ | `app_boot` 无协议分支；四步完整 |
| A-4 同通道链式 | ✅ | `any_wait/any_fake/any_parsed`；RS485：青海+RLS；RJ45：IAP+LDI（+可选 MQTT） |
| A-5 语音 USART6 | ✅ | 零处 `bind(CH_ID_RS232_1)`；voice → `PL_UART6` |
| A-6 单协议退化 | ✅ | weak provide；排除目录后无硬编码互斥（LDI 仍依赖 cfg，见 A-2） |
| NF-2 跨物理口隔离 | ✅ | 三物理槽独立 |
| NF-3 兼容矩阵门禁 | ⚠️ | 编译期检查仍缺（青海 vs 重庆）；运行约定见 `01` §6 |
| NF-4 网口共享策略 | ✅ 已采纳默认 | 共享 RJ45 **1536**；高压并发风险见 `01` §2.4 |

---

## 2. 现行 RB / 绑定表（代码事实）

定义：`Application/Inc/app_dispatch.h`（`rb_slot_t` / `RB_SIZE_*`）；池：`app_dispatch.c`（`g_rb_provide[]`）。

| 槽 | 尺寸 | 协议 | bind |
|----|------|------|------|
| `RB_SLOT_RJ45` | **1536** | IAP | `CH_ID_UDP` |
| 同上 | | LDI | `CH_ID_TCP_SERVER` / `TCP_CLIENT` / `UDP`（每通道独立 mask） |
| 同上 | | AH_MQTT | `CH_ID_MQTT`（initcall 可关） |
| `RB_SLOT_RS485` | **768** | 青海、RLS | `CH_ID_RS485` |
| `RB_SLOT_RS232` | **768** | 青海 | `CH_ID_RS232` |

关键源文件：

| 文件 | 要点 |
|------|------|
| `app_dispatch.c` | provide 表、acquire 判空、写去重、链式分发；`_Static_assert` 尺寸 1536/768/768 |
| `ring_buffer.h` | `RB_PROVIDE_WEAK`；可用容量 = size−1 |
| `app_iap.c` | provide RJ45；**仅 UDP**；`IAP_QUEUE_DEPTH=2` |
| `app_ldi.c` | provide RJ45；**无 485/232**；`LDI_QUEUE_DEPTH=4` |
| `app_qh_proto.c` | provide 485+232；`QH_PAYLOAD_MAX=259`；`QH_QUEUE_DEPTH=3` |
| `app_rls.c` | provide 485；`mem_pool[RLS_PAYLOAD_MAX]`；`RLS_QUEUE_DEPTH=2` |
| `ah_mqtt.c` | provide RJ45；`AH_MQTT_QUEUE_DEPTH=3`；initcall 可关 |
| `app_rs232.c` / `app_boot.c` | RS232_1 旁路；无 DMA RX 缓冲 |

通道启动（`app_boot.c`）：TCP Server/Client、UDP、RS485、RS232；不启 RS232_1 协议 RX。

### 2.1 协议帧 queue 深度（2026-08-14 修订）

| 协议 | 深度 | 静态缓冲约 | 变更 |
|------|------|------------|------|
| IAP | 2 | 2104 | 维持 |
| LDI | **4** | 2080 | 2→4（粘包多帧） |
| 青海 | **3** | 801 | payload 259 + 深度 3 |
| RLS | 2 | 1076 | 维持 |
| AH_MQTT | **3** | ~1623（任务启动后） | 1→3 |

`osMessageQueuePut(..., timeout=0)`：满则丢整帧。深度与 RB/DMA 无强制联调。

---

## 3. 相对历史方案的变更关闭表

| ID | 历史问题 / 旧规划 | 状态 | 说明 |
|----|-------------------|------|------|
| ARCH-1 | 8×2KB @ CCM | ✅ 取代 | 三槽 provide @ SRAM |
| ARCH-2 | IAP RS485+UDP 双槽 | ✅ 关闭 | 仅 UDP |
| ARCH-3 | LDI 绑 RS485 | ✅ 关闭 | 仅网口 |
| ARCH-4 | 每网口独占 RB | ✅ 产品决策 | 改共享 RJ45（NF-4） |
| CR-1 | IAP 盲 WAIT | ✅ | 首字节快拒 |
| CR-2 | RLS peek/长度越界 | ✅ | `mem_pool`≥`RLS_PAYLOAD_MAX`；RB 768 |
| CR-4 / MA-5 | AH_MQTT probe 过松 | ⚠️ 例外 | initcall 默认关 |
| MI-3 | 兼容矩阵编译门禁 | ❌ 可选后续 | |
| MI-CFG | LDI→`app_iap_cfg` 强依赖 | ⚠️ 已知 | 暂不解耦，见 doc/07 |

---

## 4. DoD（本目录：多协议共享 RB）

1. [x] `RB_CNT_MAX==3`，尺寸 **1536/768/768**，`_Static_assert` 在册  
2. [x] 无调度路径 `RB_DEFINE_CCM(g_rb*)` 八槽池  
3. [x] IAP 无 `CH_ID_RS485`；LDI 无 RS485/RS232 bind  
4. [x] 无协议 bind `CH_ID_RS232_1`  
5. [x] 同物理口链式 + 写去重仍在  
6. [x] EIDE/Makefile 无 `APP_PROTOCOL`  
7. [x] 帧 queue 深度：IAP2 / LDI4 / QH3 / RLS2 / AH3（静态缓冲）  

**不在本 DoD**：堆水位标定、IAP 升级实机验证、doc/07 解耦落地。

---

## 5. 建议回归（协议行为）

| 用例 | 期望 |
|------|------|
| RS485 青海 / RLS 帧 | 各自 READY；RLS 满长 ≤530 可 READY |
| UDP IAP 与 LDI | 共享 RJ45，互不永久阻塞 |
| TCP LDI 一包 ≥3 帧 | 深度 4 下应均可入队（处理任务跟得上时） |
| TTS | USART6 出帧；协议口无语音字节 |
| 排除青海目录 Rebuild | 仅余协议；RS485 provide 仍可由 RLS 提供 |
| 排除整个 IAP/ 仅留 LDI | 须保留 `app_iap_cfg.c`，否则链接失败 |

---

## 6. 可选后续（非阻塞）

| 项 | 说明 |
|----|------|
| MI-3 编译期兼容检查 | 禁帧头冲突同编 |
| AH_MQTT 产品启用 | 收紧 probe + 堆预算 |
| doc/07 解耦 | 抽出 board_net_cfg（暂缓） |
| IAP 升级实机 | 停等模型下 queue=2；验证 ACK 节奏 |

---

## 修订

- 2026-08-14：首版落地对照。  
- 2026-08-14：对齐 RB 1536/768/768、queue 深度表、A-2/doc/07、DoD。
