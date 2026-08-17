# 多协议共享 RB — 设计规范

> **状态**：现行　|　2026-08-14  
> **落地对照**：[02_as_built_status.md](./02_as_built_status.md)  
> **内存位置**：RB 体默认 SRAM、堆/CCM 归属见 [`../06_SRAM内部分数据迁移/02_memory_policy.md`](../06_SRAM内部分数据迁移/02_memory_policy.md)

---

## 1. 验收目标

| 编号 | 验收项 | 判定标准 |
|------|--------|----------|
| A-1 | 单固件多地区协议 | 同一镜像可同编多个兼容解析器（如 LDI + 青海 + RLS） |
| A-2 | 工程目录选编 | EIDE/Makefile 包含/排除协议目录；不以 `APP_PROTOCOL` 宏为主 |
| A-3 | 启动自注册 | `sw_app_initcall` → acquire / register / bind / 任务；`app_boot` 无协议 `#ifdef` |
| A-4 | 同通道链式解析 | 绑定同通道的协议共享物理 RB，链式 probe，匹配者消费 |
| A-5 | USART6 语音专用 | 禁止任何协议 `bind(CH_ID_RS232_1)`；TTS → `PL_UART6` 旁路 |
| A-6 | 单协议退化 | 只编入一个协议目录时行为退化为单 probe（weak 无他提供者） |

非功能：

| 编号 | 需求 |
|------|------|
| NF-1 | 多协议带来的 FreeRTOS 堆压力可控（静态队列/控栈；结构见 06） |
| NF-2 | **不同物理口**不共享可并发写入的 RB |
| NF-3 | 帧头冲突组合不得同通道同 RB 同时启用 |
| NF-4 | 网口逻辑通道默认共享 RJ45；若需真并发高压隔离，再拆逻辑 RB（见 §2.4） |

---

## 2. 核心决策

### 2.1 一物理通道一 RB

| 物理口 | RB 槽 | 容量 | 逻辑通道（可多） |
|--------|-------|------|------------------|
| ETH（RJ45） | `RB_SLOT_RJ45` | **1536** | TCP_SERVER / TCP_CLIENT / UDP / MQTT |
| USART1 RS485 | `RB_SLOT_RS485` | **768** | `CH_ID_RS485` |
| USART3 RS232 | `RB_SLOT_RS232` | **768** | `CH_ID_RS232` |
| USART6 | — | — | **不进 RB**（语音 TX 旁路） |

规则：

- 同物理口多协议 → **共享**该槽 RB，链式 probe 区分帧。
- 不同物理口 → **独立** RB，禁止混写。
- `app_channel_dispatch`：同 RB 指针 `seen[]` 去重，每包只写一次。
- `frame_dispatch_task`：外循环按 RB 去重；内循环同 RB 上协议链式探测。

```
通道 RX → app_channel_dispatch → 写入物理 RB（去重）
       → ch_queue → frame_dispatch_task
       → 链式 probe → READY → frame_queue[协议]
```

### 2.2 编译期按需（`RB_PROVIDE_WEAK`）

- 协议 TU 内：`RB_PROVIDE_WEAK(rb_provide_xxx, RB_SIZE_xxx)`。
- 多 TU 同名 weak → 链接保留一个 getter 及 static 体。
- 未编入任何提供者 → weak 为 0 → `acquire` 返回 nullptr（不占缓冲）。
- 体落默认 `.bss`（SRAM）；**不**再使用调度路径上的 `RB_DEFINE_CCM` 八槽池。

### 2.3 协议模块组织

| 原则 | 说明 |
|------|------|
| 目录即模块 | `LDI/`、`ProtocolParser_QingHai/`、`RLS/`、`IAP/` … |
| 自注册 | `sw_app_initcall`；四步：acquire → register → bind → `osThreadNew` |
| 多逻辑通道 | **每逻辑通道独立 mask + bind**；共享同一物理 RB；禁止「单 mask 绑多通道」 |
| 框架零分支 | `app_dispatch` 无地区 `#ifdef` |

### 2.4 网口共享 RJ45 的风险与门禁（NF-4）

TCP 为流、UDP 为报；多通道任务均可对 **同一** RJ45 RB 调用 `dispatch`。互斥保证单次 `rb_write` 原子，但**不同套接字的包会在环缓中交错排列**。

| 场景 | 建议 |
|------|------|
| 产品以单网口业务为主（常见：UDP IAP/LDI 或 TCP 二选一） | **维持共享 1536**（省 RAM） |
| TCP 与 UDP/MQTT **同时高压**收包 | 评估拆逻辑 RB，或产品互斥启通道 |

本阶段默认维持共享；变更须同步改 `02` 绑定表与 `06` 占用账。

### 2.5 硬件映射

| PL | 口 | CH_ID | 用途 |
|----|-----|-------|------|
| UART1 | RS485 | `CH_ID_RS485` | 地区协议链式 |
| UART3 | RS232 | `CH_ID_RS232` | 地区协议链式 |
| UART6 | TTL | `CH_ID_RS232_1` | **仅语音 TTS**；禁止协议 bind |
| ETH | — | TCP_S / TCP_C / UDP / MQTT | 共享 `RB_SLOT_RJ45` |

---

## 3. 链式探测契约（A-4）

### 3.1 WAIT / FAKE 决策

```
READY / SKIP → 消费后结束本轮，继续取下一帧
WAIT         → any_wait，继续试下一协议
FAKE         → any_fake，继续试下一协议

无一消费时：
  if (any_wait)       break;       /* 禁止 skip */
  else if (any_fake)  rb_skip(1);  /* 重同步 */
```

### 3.2 probe 强制规则

1. 只 `rb_peek`；禁止 probe 内 `rb_read` / `rb_skip` / 写业务全局  
2. **首字节快拒**：不匹配立刻 FAKE；禁止「长度不够就一律 WAIT」  
3. 仅 READY/SKIP 写 `*total_len` / `*aux`  
4. 无阻塞、无副作用、幂等  
5. **长度/缓冲边界**：对 length 做上下界；`rb_peek` 按目标缓冲截断  

### 3.3 帧头事实

| 协议 | 特征 |
|------|------|
| IAP | `0x5A5A5A5A` + CRC32 |
| LDI | STX `0xFF 0xFF` + CRC16-XMODEM |
| RLS | `0xFF 0xFE` … `0x0D 0x0C` |
| 青海 | `{` … `}`（弱校验） |
| 四川 ETC | `0x0A` + 命令位(00/01/36~39/40/50) … `0x0D`（46 归属 MTC） |
| 四川 MTC | `{`(0x7B) + 命令 + 参数 [+BCC] + `}`（无 BCC/带 BCC 双格式，BCC 不校验，9K1F212701 语义）；`0A 46 0A` / `0A 46 0D` 双帧型 |
| 四川治超 | `0xFF` + 长度(07~FF，0xFE 排除) … `0xFF`（BCC 异或）；长度上限对齐 9K1F212701 容纳 0x80 全屏长数据段，0xFE 显式排除与 RLS `FF FE` 区分；81~88 行数据变长（=总长-6，≤24B 截断） |
| 重庆 JSON（规划） | 同以 `{` → **不得**与青海同 RB |

---

## 4. RB / 绑定规划（与代码一致）

| 槽 | 尺寸 | 提供方（weak） | bind 通道 | 协议 |
|----|------|----------------|-----------|------|
| RJ45 | 1536 | IAP / LDI / AH_MQTT | UDP | IAP |
| RJ45 | 同上（共享） | 同上 | TCP_S / TCP_C / UDP | LDI |
| RJ45 | 同上 | 同上 | MQTT | AH_MQTT（initcall 可关） |
| RS485 | 768 | QH / RLS / SC_ETC / SC_MTC / SC_OL | RS485 | 青海、RLS、四川三协议 |
| RS232 | 768 | QH / SC_ETC / SC_MTC / SC_OL | RS232 | 青海、四川三协议 |

**已取消（相对历史 8 槽方案）**：

- IAP 占用 RS485 / 「系统独立 RB」双写  
- LDI 绑定 RS485 / RS232  
- 每逻辑网口独占 2KB CCM 槽  

### 4.1 协议帧 queue 深度（静态，SRAM）

一协议一队列（LDI 三逻辑通道 mask 共用 `g_ldi_msg_queue`）。与 RB/DMA 正交；加深 queue 不必联调 RB。

| 协议 | 宏 | 深度 | 单槽约 | 静态体约 | 说明 |
|------|-----|------|--------|----------|------|
| IAP | `IAP_QUEUE_DEPTH` | **2** | ~1052 | ~2104 | 停等升级模型够用 |
| LDI | `LDI_QUEUE_DEPTH` | **4** | ~520 | ~2080 | 一包多帧粘包（≥3） |
| 青海 | `QH_QUEUE_DEPTH` | **3** | ~267 | ~801 | payload 上限 259 |
| RLS | `RLS_QUEUE_DEPTH` | **2** | ~538 | ~1076 | |
| AH_MQTT | `AH_MQTT_QUEUE_DEPTH` | **3** | ~541 | ~1623 | initcall 可关；任务内 static |
| 四川 ETC | `SC_ETC_QUEUE_DEPTH` | **3** | 157 | **471** | payload 149（帧 ≤149，0x0D 定界，参考 9K1F212701） |
| 四川 MTC | `SC_MTC_QUEUE_DEPTH` | **3** | 82 | **246** | payload 74（'4' 全屏 68B 上限） |
| 四川治超 | `SC_OL_QUEUE_DEPTH` | **3** | 263 | **789** | payload 255（=长度字段上限 FF，覆盖 0x80 全屏长数据段） |

变更记录（2026-08-14）：LDI 2→4、青海对齐协议后深度 3、AH 1→3；IAP/RLS 维持 2。
变更记录（2026-08-14）：新增四川三协议（ETC/MTC/治超）各深度 3。
变更记录（2026-08-15）：四川 ETC payload 64→149（0x0D 定界上限对齐 9K1F212701 etc.c，全屏数据无固定 56B）。
变更记录（2026-08-17）：四川治超 payload 249→255（=长度字段上限 FF，消除 250~255 长帧被队列截断的越界读）；MTC '8' 亮度参数兼容二进制 0~8 与 ASCII '0'~'8' 双格式；MTC 'A' 4B/5B 判别加 b2≤2 门限，消除颜色帧 BCC='}' 误判。
---

## 5. 协议模块模板

```c
RB_PROVIDE_WEAK(rb_provide_rs485, RB_SIZE_RS485);

void xxx_proto_init(void)
{
    ring_buffer_t *rb = app_proto_acquire_buf(RB_SLOT_RS485, RB_SIZE_RS485);
    if (rb == nullptr) return;
    proto_mask_t m = app_proto_register(xxx_probe_frame, rb);
    if (m == 0) return;
    app_proto_bind_channel(m, CH_ID_RS485);
    /* 多逻辑通道 → 再 register + bind；禁止 CH_ID_RS232_1 */
    osThreadNew(xxx_handle_task, ...);
}
sw_app_initcall(xxx_proto_init);
```

语音：`dev_rs232_voice_*` → `pl_uart_send(PL_UART6)`，旁路 dispatch。

---

## 6. 兼容矩阵（合入门禁）

| 组合 | 同物理 RB | 建议 |
|------|-----------|------|
| LDI + 青海 | 不同物理口（RJ45 vs RS485） | 允许 |
| LDI + RLS + 青海（RS485 上后两者） | RS485 链式 | 允许（帧头可分） |
| 青海 + 重庆 JSON | **禁止**同 RS485 | 同分 `{` |
| LDI + 重庆 BIN | 高风险 | 同 `FFFF`，须更强 probe 或互斥 |
| 任意协议 + `CH_ID_RS232_1` | **禁止** | 语音专用 |
| 四川 MTC + 青海（`{` 帧头重叠） | RS485/RS232 链式 | ⚠️ **EIDE 目录排除纪律**：MTC probe 帧长+'}' 尾字节拒绝青海帧（BCC 不校验，2026-08-15 对齐 9K1F212701 后判别力降为长度+尾字节）、青海 probe 长度+尾字节拒绝 MTC 帧；残余巧合（青海 '3' 帧总长恰 20 且末字节 '}'）依赖排他编译，量产必须二选一 |
| 四川治超 + RLS（`FF` 帧头重叠） | RS485 链式 | 允许：治超第二字节=长度 07~FF 但**显式排除 0xFE(254)**，RLS 固定 0xFE，双向第二字节快拒成立（2026-08-15 长度上限对齐 9K1F212701 后必须显式排除，否则治超 probe 会把 RLS 帧当长帧 WAIT 卡死） |
| 四川 ETC + 四川 MTC（`0A` 帧头重叠） | RS485/RS232 链式 | 允许：ETC 命令位 ∈ {00,01,36~39,40,50}，MTC 仅 46（0A 46 0A/0D），命令位互斥 |

---

## 7. 与 RAM 文档的边界

- RB **行为**以本文件为准；RB **放 SRAM**、堆回退、显存占 CCM → **仅 06**。  
- 多协议任务栈挤堆的历史问题 → [03](./03_freertos_heap_side_effect.md)，结构方案不在本目录改代码。

---

## 8. 非目标

- 不刷机动态下载未编入的解析器  
- 不保证帧头冲突协议在同一字节流上零误判  
- 不把 `APP_PROTOCOL` 作为主切换手段  
