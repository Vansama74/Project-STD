# 多协议共享 RB — 设计规范

> 〔2026-10-13〕本仓库 Makefile 已移除，构建仅 EIDE；本文中以 make 为入口的命令一律作废（历史记录），口径迁移见 doc/构建开关总表.md §7。

> ⚠ **2026-10-13 用户裁决：`.analysis/` 目录已从仓库移除且禁止再建。** 本文中所有 `.analysis/**` 引用
> （宿主推演 / 校验脚本 / 报告 / 证据 / 备份）**一律作废、仅作历史线索保留**，需要时用
> `git show 069be96:<路径>` 取回（在仓库外运行）。下文脚本结果列为历史记录。

> **状态**：现行　|　2026-08-14  
> **落地对照**：[02_as_built_status.md](./02_as_built_status.md)  
> **内存位置**：RB 体默认 SRAM、堆/CCM 归属见 [`../06_SRAM内部分数据迁移/02_memory_policy.md`](../06_SRAM内部分数据迁移/02_memory_policy.md)

---

## 1. 验收目标

| 编号 | 验收项 | 判定标准 |
|------|--------|----------|
| A-1 | 单固件多地区协议 | 同一镜像可同编多个兼容解析器（如 LDI + 青海 + RLS） |
| A-2 | 工程目录选编 | EIDE target 的 include/exclude 选编协议目录（**Makefile 已移除**）；不以 `APP_PROTOCOL` 宏为主 |
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
| ETH（RJ45） | `RB_SLOT_RJ45` | **1536** | TCP_SERVER / TCP_CLIENT / UDP(10011) / UDP_CQ(20103) / UDP_GZOL(`net_cfg.port`) / MQTT |
| USART1 RS485 | `RB_SLOT_RS485` | **768** | `CH_ID_RS485` |
| USART3 RS232 | `RB_SLOT_RS232` | **768** | `CH_ID_RS232` |
| USART6 | — | — | **不进 RB**（语音 TX 旁路） |

> **CH_ID 枚举（2026-09-14）**：0 RS485 / 1 RS232 / 2 TCP_SERVER / 3 TCP_CLIENT / 4 UDP(10011) /
> 5 MQTT / 6 RS232_1（禁绑）/ 7 UDP_CQ / **8 UDP_GZOL**（贵州治超专用业务口，端口 = Sector1
> `net_cfg.port`，与 TCP 业务口同号不同协议栈）；`CH_ID_MAX = 9`，`ch_proto_map[]`/`channels[]`
> 同步扩容。新增一条 UDP 逻辑通道 = +1 常驻 netconn +1 UDP PCB + 1 任务栈（ucHeap），
> 池核算见 `lwipopts.h` 注释与 doc/06-04。
>
> **TCP Client 远端（2026-09-18 通道层解耦）**：`CH_ID_TCP_CLIENT` 编译期默认 `0.0.0.0:0`，
> IP 全 0 或 `port==0` 视为未配置——任务仍由 `app_boot` 启动，但**不** `netconn_new` / **不** connect
> （去重打 `[tcp_cli] idle (remote unset)`）。远端只经 `app_tcp_client_set_remote` 注入
> （现行唯一调用方 = LDI 装载：Sector 11 配置记录里 host 有效才调用；该记录 2026-11-24 起由内部 Flash 承载，原为 W25）。协议绑本通道只消费入站（YN_OL 先例），
> **不**隐含配置远端。排除 LDI 的镜像不再对历史硬编码 `192.168.2.17:9529` 外连。

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
  if (any_wait)       break;       /* 禁止 skip（带时间预算，见下） */
  else if (any_fake)  rb_skip(1);  /* 重同步 */
```

**WAIT 头阻塞预算**（2026-09-17 晚）：`any_wait` 并非无限等——调度器记录**头部前缀指纹**
（前 `min(4, avail)` 字节 + 字节数），若 `FRAME_WAIT_BUDGET_MS`（默认 **1000ms**）内头部
**没有前进**，即判「残帧卡头」→ 打 `[disp]` 告警后强制 `rb_skip(1)` + `resync_byte++`。
**进度判据是头部前缀而非 `avail` 增长**：残帧卡头时新数据到达会让 `avail` 增长，若按
`avail` 重置预算则强制重同步永不触发、整条 RB 永久不消费（现场「首帧后不再受控需重启」的根因）。
预算取值 > 9600bps 下最长单帧（RLS 530B ≈ 552ms），避免拦腰打断合法长帧。

**空闲超时也触发预算清理（2026-10-15）**：上述检查原先只在「被通知唤醒后」执行 ⇒ 总线空闲
（残帧卡头后无新字节）时预算永不到期，残帧可被后续无关字节（如周期心跳的 `0x0D`）补齐成一个
「看似合法」的帧被误认领。现 `frame_dispatch_task` 的等待由 `osWaitForever` 改为
`DISPATCH_IDLE_POLL_MS`（默认 **100ms**，可 `-D` 覆盖）空闲滴答，**超时且无通知时**对在册 RB
兑现同一预算判据（同一 `_wait_budget_poll`，手指纹/1000ms 门槛/按 RB 分槽口径不变）；有通知的
正常路径、probe 契约与 FAKE/WAIT/SKIP 语义零变化。

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
| 山东 | `{` + 命令字('1'~'5','7','8') + 二进制 len + `}`（与青海同构、无 BCC；'3'/'4'/'5' 语义与青海一致，'1'/'2'/'7'/'8' 语义分歧） |
| 贵州 | `{` + 命令字('1'~'9','A','B',0x01,0x02) + 二进制 len + `}`（与青海完全同构、无 BCC；13 命令含 0x01 全屏点亮 / 0x02 版本号两个二进制命令字） |
| 云南 | `{` + 命令字('1'~'9','A','B',0x01,0x02) + 二进制 len + `}`（与青海/贵州完全同构、无 BCC；13 命令，'6'=单行清除（贵州为固定格式）、0x02 版本号应答 PROGRAM_CODE） |
| 四川 ETC | `0x0A` + 命令位(00/01/36~39/40/50) … `0x0D`（46 归属 MTC） |
| 四川 MTC | `{`(0x7B) + 命令 + 参数 + `}`（**'}' 定界变长**，无长度字段、无 BCC，扫描上限 74B，9K1F212701 语义）；`0A 46 0A` / `0A 46 0D` 双帧型 |
| 四川治超 | `0xFF` + 长度(07~FF，0xFE 排除) … `0xFF`（BCC 异或）；长度上限对齐 9K1F212701 容纳 0x80 全屏长数据段，0xFE 显式排除与 RLS `FF FE` 区分；81~88 行数据变长（=总长-6，≤24B 截断） |
| 安徽 | `0x5A` + 屏号(01) + 命令(81~89,92,94~97) + 数据长(1B) + 数据 + CRC(不校验，文档注明无检验) + `0xA5`（长度字段定界，帧 ≤261）；与 IAP `0x5A5A5A5A` 同首字节但分处不同槽（安徽串口 / IAP 网口） |
| 贵州治超（TCLY） | `54 43 4C 59`（ASCII "TCLY"）+ 包序号(4) + **长度(2B 小端 = 整帧总长)** + 保留(2) + 命令字(4，低字节有效) + 载荷 + `0x00` 结束符（**无校验**；probe 做首字节快拒 + 4 字节引导串全匹配 + 长度/尾字节双重校验）；帧 17~256（结构合法超长帧 → `PROTO_PROBE_SKIP` 整帧消费）；命令 0x10/0x20/0x30/0x40/0x50/0x60/0x70(出站)/0x80；首字节 `0x54` 在串口槽与网口槽均唯一 |
| 云南治超（YN_1.3.0） | `{` + 命令字（`'1'`~`'5'`/`'8'`/`'A'`，`0x42`~`0x46`/`0x50` 六行清除，`0x47`~`0x49`/`0x51` IP 配置，`0x01`/`0x02` 二进制）+ 二进制 len + `}`（与云南常规完全同构、无 BCC；**'6'/'7'/'9' 治超屏明示不开发 → probe 快拒**；`0x46`~`0x51` 为 `{` 族独有命令字，`0x42` 与云南常规 `'B'` 同字节歧义）；**绑 RS485 + RS232 + TCP Server + TCP Client 四通道**（2026-09-17 裁决；TCP 侧与 CQ JSON 的 `{` 同首字节竞争由**通道掩码隔离**解决——CQ 不绑 TCP，见 doc/15 §1） |
| 重庆CQ | JSON `{` 花括号深度定界（'"' 内跳过，深度>16/累计>1044 → FAKE）+ `FF FF` 12B 二进制精确匹配（重启/搜索两帧），CRC16-XMODEM（大端，帧内校验）；业务口 UDP_CQ(20103) + 搜索口 UDP(10011) |
| 缅甸2D费显（MM） | `0xFF 0xE0` + 命令方式字(1B) + 内容 12B + 结束码 5B（**定长 20 字节**，无校验/无长度字段/无应答）；只解析 `0x90`/`0x0A`/`0x0B` 三条车道灯命令（DATA0 1/0），文档 `0x10`/`0x40`/`0x80` 由 probe 整帧 SKIP；**只绑 RS232**，首字节 `0xFF` 与四川治超重叠 → 「地址 `0xE0` 强判 + 非本模块命令须结束码正签名 `[15..18]==00 00 00 00`」双向不误吞；末字节 `[19]` 不判别（兼容同源 8051 的未比对 XOR） |
| **新疆费额显示器（XJ，协议文档 = PDF 全文「费额显示器通讯协议 2020 年」）** | `FF DC` + **长度(1B = 整帧总长，含头与 BCC)** + 命令字(1B ASCII) + 载荷 + BCC(1B，除自身外全字节 XOR)；帧长 **5~106**（'T' 文本 10+16N N≤6、'D' 全屏 7+16N N=2~6；2026-10-12 第二轮由 74 放宽到 106），无转义；**入向无应答要求，但 `'D'/'C'/'Z'/'P'` 须回 5B 应答**；14 命令 `'T'/'O'/'D'/'E'/'F'/'G'/'H'/'J'/'K'/'Z'/'P'/'V'/'S'/'C'`（`'B'` 下载位图不实现、不进白名单）；**绑 RS485 + RS232 双槽**（**自备两槽体 `RB_PROVIDE_WEAK` + acquire**——2026-10-12 第二轮起；原「只 acquire 不 provide」在单协议目标下两槽无提供者 ⇒ 模块静默不注册、串口通道被门控跳过），首字节 `0xFF` 与四川治超重叠 → 「第二字节 `0xDC` + 长度 ≤106 + 命令字白名单 + BCC」四重隔离（治超帧长 0xDC=220 > 106 立即 FAKE、不 WAIT），详见 doc/19 §3 |
| **广西费显（GX）** | `0xAA` + 地址(1B，v1 不判) + `0xBB` + 双字节命令码 + 数据 + `0xFF`；**无长度字段**：`51 54` 显示靠扫描 `0xFF` 定界（文本 ≤192B、帧 ≤204B，`[5]` 为数据累加和但 v1 不校验），其余 7 组命令按「命令码 + 固定长度」定界（`4B 4C` 7B / `4B 5A` 8B / `56 4F` 8B 或 14B / `01 01` 7B / `02 02` 5B / `03 03` 103B / `04 04` 6B）；**回 `0x1B` = 51 54 / 4B 4C / 4B 5A / 56 4F / 03 03，原路回**；**绑 RS485 + RS232 双槽、自备两槽体**（同新疆口径）；首字节 `0xAA` 在 RS485/RS232/RJ45 三槽对现有协议均唯一 ⇒ **免 EIDE 互斥、不定义 `g_brace_proto_guard`**，详见 doc/21 §3 |

---

## 4. RB / 绑定规划（与代码一致）

| 槽 | 尺寸 | 提供方（weak） | bind 通道 | 协议 |
|----|------|----------------|-----------|------|
| RJ45 | 1536 | IAP / LDI / AH_MQTT | UDP | IAP |
| RJ45 | 同上（共享） | 同上 | TCP_S / TCP_C / UDP | LDI |
| RJ45 | 同上 | 同上 + CQ | MQTT | AH_MQTT（initcall 可关） |
| RJ45 | 同上 | CQ（weak 合并） | UDP_CQ / UDP | 重庆CQ（双 mask 共用一队列；CQ 源收录序在 LDI 之后） |
| RJ45 | 同上（共享） | 同上（IAP / LDI / CQ；**贵州治超只 acquire、不 provide**） | UDP（10011） | 贵州治超（2026-09-14 增绑，第三 mask 挂 RJ45 槽；与 IAP/LDI/CQ 首字节互斥，见兼容矩阵） |
| RJ45 | 同上（共享） | 同上（IAP / LDI / CQ；**云南治超只 acquire、不 provide**） | **TCP_S / TCP_C** | 云南治超（2026-09-17 裁决增绑；**只走 TCP、不绑 UDP 10011**——与 CQ 的 `{` 同首字节竞争由通道掩码隔离，见兼容矩阵与 doc/15 §1） |
| RS485 | 768 | QH / RLS / SC_ETC / SC_MTC / SC_OL / SD / GZ / YN / ANHUI / GZ_OL / **YN_OL** / **XJ** / **GX**（后三者自备槽体） | RS485 | 青海、RLS、四川三协议、山东、贵州、云南、安徽、贵州治超、**云南治超**、**新疆**、**广西（自备槽体 `RB_PROVIDE_WEAK` + acquire）** |
| RS232 | 768 | QH / SC_ETC / SC_MTC / SC_OL / SD / GZ / YN / ANHUI / GZ_OL / **YN_OL** / **MM** / **XJ** / **GX**（后三者自备槽体） | RS232 | 青海、四川三协议、山东、贵州、云南、安徽、贵州治超、**云南治超**、**缅甸2D费显（只绑本槽）**、**新疆**、**广西（自备槽体 `RB_PROVIDE_WEAK` + acquire）** |

**已取消（相对历史 8 槽方案）**：

- IAP 占用 RS485 / 「系统独立 RB」双写  
- LDI 绑定 RS485 / RS232  
- 每逻辑网口独占 2KB CCM 槽  

> **出站镜像不改变本表**：`app_ldi_etc_bridge.c`（LDI → 四川 ETC 转义，2026-10-15）把 LDI 命令
> 成功执行的结果翻译成 ETC 帧，**从 RS485 / RS232 两个通道发出**（`app_channel_get` + `channel_send`）。
> 它**不是协议绑定**：不注册 probe、不 acquire RB、不建队列、不占串口槽体，也不进入链路探测链
> （串口槽的「提供方 / bind 通道 / 协议」三列均不变）；因此串口通道仍须由四川 ETC 等串口协议绑定启动，
> 否则发送时通道指针为空而静默跳过。详见 [`../20_LDI到四川ETC协议转义/README.md`](../20_LDI到四川ETC协议转义/README.md)。

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
| 四川 MTC | `SC_MTC_QUEUE_DEPTH` | **3** | 82 | **246** | payload 74（'}' 定界扫描上限；'4' 全屏 ≤68B / '78' 语音 ≤74B） |
| 四川治超 | `SC_OL_QUEUE_DEPTH` | **3** | 263 | **789** | payload 255（=长度字段上限 FF，覆盖 0x80 全屏长数据段） |
| 山东 | `SD_QUEUE_DEPTH` | **3** | ~267 | ~801 | payload 259（len 字段 1B 上限，与青海同构） |
| 贵州 | `GZ_QUEUE_DEPTH` | **3** | ~267 | ~801 | payload 259（与青海同构，13 命令含 0x01/0x02） |
| 云南 | `YN_QUEUE_DEPTH` | **3** | ~267 | ~801 | payload 259（与青海/贵州同构，13 命令含 0x01/0x02；'3' 单行 FONT_24 渲染） |
| 云南治超 | `YN_OL_QUEUE_DEPTH` | **3** | ~267 | ~801 | payload 255（参数区 1B 长度字段上限，帧 ≤259，与云南常规同构；'6'/'7'/'9' 治超屏不开发 → probe 快拒）；**队列体+cb+任务帧缓冲置 CCMRAM**（1152B；2026-09-17 接入，ALL 口径 SRAM 余量仅数百 B，CPU 独占访问无 DMA，CQ/GZ_OL 先例） |
| 安徽 | `ANHUI_QUEUE_DEPTH` | **3** | 269 | **807** | payload 261（数据长 1B 上限 255 → 6+255；长度字段定界，CRC 不校验）；**队列体+cb+任务帧缓冲为静态 SRAM**（与青海/贵州/云南同，用户裁决 2026-09-04；+1156B SRAM，CQ 才是 CCMRAM 例外） |
| 重庆CQ | `CQ_QUEUE_DEPTH` | **3** | 1052 | **3156** | payload 1044（JSON 花括号定界上限）；队列体+任务帧缓冲+JSON/文本缓冲**置 CCMRAM**（SRAM 全协议构建仅余 ~1.9KB，CPU 独占访问无 DMA） |
| 贵州治超 | `GZ_OL_QUEUE_DEPTH` | **3** | 264 | **792** | payload 256（"TCLY" 帧族，实测合法最大帧 158B，1.6× 余量；结构合法超长帧由 probe SKIP）；**队列体+cb+任务帧缓冲置 CCMRAM**（1136B 合计；2026-09-14 `PROTO=ALL` 口径 SRAM 余量仅 496B，CPU 独占访问无 DMA，CQ 先例） |
| 缅甸2D费显（MM） | `MM_QUEUE_DEPTH` | **3** | 28 | **84** | payload **20**（= 定长帧长，本协议无变长帧）；**队列体+cb+任务帧缓冲 + 处理任务栈/TCB 置 CCMRAM**（合计 1320B 实测增量；2026-09-24 `PROTO=ALL` 口径 SRAM 余量仅约 2KB，CPU 独占访问无 DMA，CQ/GZ_OL/YN_OL 先例）；SRAM 侧仅 mask/句柄等约 13B（原灯位影子状态已于 2026-10-13 裁决清理） |
| 新疆费显（XJ） | `XJ_QUEUE_DEPTH` | **3** | 114 | **342** | payload **106**（'T' 文本 10+16N N≤6 / 'D' 全屏 7+16N N=2~6；2026-10-12 第二轮由 74 放宽到 106）；**队列体+cb+任务帧缓冲 + 处理任务栈/TCB 置 CCMRAM**（模块符号级跨度 **1668B** = 队列体 342 + cb 80 + 帧缓冲 114 + 任务栈 1024 + TCB 100 + 对齐 8；SRAM 余量紧，CPU 独占访问无 DMA，CQ/GZ_OL/YN_OL/MM 先例）；**自备 RS485/RS232 两槽 `RB_PROVIDE_WEAK` 槽体后再 acquire**（2026-10-12 第二轮口径；多协议口径下与同槽其它提供者 weak 合并、零额外 SRAM，单协议目标必需）；SRAM 侧仅 mask×2/句柄/心跳时刻/标志 **32B** + 自备槽体 **1568B**（data 32 + bss 1536，多协议口径下被同槽 weak 合并、不占）；2026-10-12 接入，详见 doc/19 §4 |
| 广西费显（GX） | `GX_QUEUE_DEPTH` | **3** | 212 | **636** | payload **204**（`51 54` 文本 192B + 头部 11B + 结束码；其余命令定长 ≤103B）；**队列体+cb+任务帧缓冲 + 处理任务栈/TCB 置 CCMRAM**（**实测 2056B** = 队列体 636 + cb 80 + 帧缓冲 212 + 任务栈 1024 + TCB 100 + 对齐 4；A/B 双构建实测 `.text +4824` / `.rodata +464` / `.data +8` / `.ccmram +2056` / `.bss +128`）；**自备 RS485/RS232 两槽 `RB_PROVIDE_WEAK` 槽体后再 acquire**（同新疆口径；多协议构建 weak 合并、零额外 SRAM）；SRAM 侧 mask×2/句柄/定时状态/字号字型/开机界面缓存 **136B**（bss 128 + data 8，实测） + 自备槽体 **1536B**（多协议口径下被同槽 weak 合并、不占）；2026-10-20 接入，详见 doc/21 §4 |


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

> **〔2026-11-17 青海协议升级 V1.0 的口径变更〕** 青海 probe 命令字白名单由 2019 版的
> `'1'~'9'` / `'A'` / `'B'` 收缩为 **`'1'~'5'` + `0x3A` / `0x3B` / `0x3C`**；
> 通道由 RS485+RS232 扩为 **RS485 + RS232 + UDP(10011)**（RJ45 槽与 IAP `0x5A` / LDI `0xFF`
> 共用，三方首字节快拒互斥）。
> 因此下表凡以「青海 probe 命令集 = `'1'~'9'`/`'A'`/`'B'`」为前提的冲突描述，
> 应读作 **`'1'~'5'` 仍冲突、`'6'~'9'`/`'A'`/`'B'` 已不再冲突**：
> 山东 `'7'`/`'8'`、贵州与云南 `'7'`/`'9'`/`'A'`/`'B'`、四川 MTC `7B 41`/`7B 42` 均解耦。
> 编译期 `g_brace_proto_guard` 互斥纪律不变（命令字收敛不替代排他编译）。
> 详见 `doc/04_青海高速费显协议/PartB_ProtocolParser_QingHai接入.md` §B.3 / §B.8。

| 组合 | 同物理 RB | 建议 |
|------|-----------|------|
| LDI + 青海 | 不同物理口（RJ45 vs RS485） | 允许 |
| LDI + RLS + 青海（RS485 上后两者） | RS485 链式 | 允许（帧头可分） |
| 青海 + 重庆 JSON | **禁止**同 RS485 | 同分 `{` |
| LDI + 重庆 BIN | 高风险 | 同 `FFFF`，须更强 probe 或互斥 |
| 任意协议 + `CH_ID_RS232_1` | **禁止** | 语音专用 |
| 四川 MTC + 青海（`{` 帧头重叠） | RS485/RS232 链式 | ⚠️ **EIDE 目录排除纪律**：完整青海帧由 QH probe 先认领（qh_proto_init 先注册，源码收录序 QH 在前）；MTC probe '}' 定界变长扫描（上限 74B，2026-08-17 对齐 9K1F212701）对「青海帧数据段含 0x7D（GBK 尾字节可命中）且半帧到达」存在截断认领残余风险，依赖排他编译，量产必须二选一 |
| 四川治超 + RLS（`FF` 帧头重叠） | RS485 链式 | 允许：治超第二字节=长度 07~FF 但**显式排除 0xFE(254)**，RLS 固定 0xFE，双向第二字节快拒成立（2026-08-15 长度上限对齐 9K1F212701 后必须显式排除，否则治超 probe 会把 RLS 帧当长帧 WAIT 卡死） |
| 四川 ETC + 四川 MTC（`0A` 帧头重叠） | RS485/RS232 链式 | 允许：ETC 命令位 ∈ {00,01,36~39,40,50}，MTC 仅 46（0A 46 0A/0D），命令位互斥 |
| 山东 + 青海（`{` 帧族完全同构） | RS485/RS232 链式 | ⚠️ **EIDE 目录排除纪律**：山东命令字 '1'~'5','7','8' 全部落入青海 probe 命令集（'1'~'9','A','B'），全协议构建下青海 probe 先认领（源码收录序 qh 在前）；'3'/'4'/'5' 语义巧合一致，'1'/'2'/'7'/'8' 语义分歧（山东 '1'=全屏单色 vs 青海 '1'=主机查询、'2'=版本 vs 自检、'7'=亮度 vs 文明语音、'8'=外设 vs 亮度），量产必须二选一 |
| 山东 + 四川 MTC（`{` 帧头重叠） | RS485/RS232 链式 | ⚠️ 同青海行纪律：MTC '}' 定界变长扫描会认领山东帧，量产互斥 |
| 贵州 + 青海（`{` 帧族完全重叠） | RS485/RS232 链式 | ⚠️ **EIDE 目录排除纪律**：贵州命令字 '1'~'9','A','B' 全部落入青海 probe 命令集，全协议构建下青海 probe 先认领（源码收录序 qh 在前）；'1'/'3'/'4'/'5'/'7'/'8'/'9'/'B' 语义基本一致（'7'=文明语音、'8'=亮度、'9'=音量、'B'=费额语音），'2'/'6'/'A' 细节差异（贵州 '2'=自检黄屏+语音、'6'=固定格式行文与青海不同、'A'=红优先 vs 青海同红优先但贵州语义自协议文档），量产必须二选一 |
| 贵州 0x01/0x02（二进制命令字） | RS485/RS232 链式 | 允许：0x01 全屏点亮 / 0x02 版本号为二进制命令字，不属于青海/山东/四川MTC 的 ASCII 命令集，各 probe 首命令字快拒，贵州 probe 无冲突认领 |
| 云南 + 青海（`{` 帧族完全重叠） | RS485/RS232 链式 | ⚠️ **EIDE 目录排除纪律**：云南命令字 '1'~'9','A','B' 全部落入青海 probe 命令集，全协议构建下青海 probe 先认领（源码收录序 qh 在前）；与贵州同构但 '6' 单行清除（贵州固定格式）、'8' 0x00自动/1~8 档（贵州 0~5 档）语义差异，量产必须二选一 |
| 云南 0x01/0x02（二进制命令字） | RS485/RS232 链式 | 允许：0x01 全屏点亮 / 0x02 版本号为二进制命令字，不属于青海/山东/四川MTC 的 ASCII 命令集，各 probe 首命令字快拒，云南 probe 无冲突认领 |
| **云南治超 + `{` 帧族（QH/SD/GZ/SC_MTC/YN）** | RS485/RS232 链式 | ⚠️ **编译期互斥守卫**：本协议与其他 `{` 族同编（无 `STD_ALL_PROTO`）即链接报 `multiple definition of 'g_brace_proto_guard'`（`arm-none-eabi-ld -r` 实测）；命令字层：`'1'`~`'5'`/`'8'`/`'A'` 与云南常规/青海等重叠、`0x42`~`0x45` 与 MTC 重叠（**`0x42` 与云南常规 `'B'` 同字节歧义**：本协议=第一行清除 / 常规=费额语音）→ 全协议 dev 构建由先注册者认领；**量产必须 EIDE 目录二选一**。本协议独有命令字 `0x46`~`0x51` 在全协议构建下由本 probe 独占认领（宿主推演 `yn_ol_frame_sim.py` §7 已验）。详见 doc/15 §4 |
| **云南治超 + 云南常规费显（同地区、同 `{` 帧族）** | RS485/RS232/TCP 链式 | ⚠️ **量产必须二选一**（同帧头 + 命令字大面积重叠 + `0x42` 同字节歧义）；`arm-none-eabi-ld -r` 对两者 `.o` 合并**实测报 `multiple definition of 'g_brace_proto_guard'`**（加 `-DSTD_ALL_PROTO` 复编后退出码 0）；命名前缀已区分（`yn_` vs `yn_ol_`）。见 doc/15 §8 裁决 7 |
| **云南治超 + 网口各协议（IAP/LDI/CQ/GZ_OL/贵州治超）** | RJ45 共享槽，**本协议只绑 TCP_S/TCP_C**（2026-09-17 裁决 1） | 允许：TCP 链上 CQ probe **不被调用**（CQ 只绑 `CH_ID_UDP`/`CH_ID_UDP_CQ`，`app_dispatch` 按 `ch_proto_map[ch_id]` 过滤）→ 本协议帧无被 CQ 抢占风险；本模块**不绑 UDP 10011** → CQ 的 10011 行为零削弱；TCP 链同槽另有 LDI（首字节 0xFF 快拒）→ 本协议独占 `{` 首字节。反向安全：CQ JSON 第二字节 `'"'` 不在本 probe 命令字白名单 → FAKE 放行；半帧 → WAIT 且调度器继续探测下一协议 → 不卡链。**未触碰 CQ 源码**（doc/15 §1） |
| 安徽 + 串口各协议（青海/山东/贵州/云南/四川MTC/ETC/治超/RLS） | RS485/RS232 链式 | 允许：安徽首字节 0x5A 与既有串口首字节 0x7B/0x0A/0xFF 互斥，双向首字节快拒成立；安徽为 5A/A5 帧族**非 '{' 帧族，不定义 `g_brace_proto_guard`**，无 EIDE 目录排除要求 |
| 安徽 + IAP（`5A` 首字节重叠） | **不同槽**（安徽 RS485/RS232 vs IAP RJ45/UDP） | 允许：物理 RB 不同不相交；IAP 仅绑定 UDP，串口槽无 IAP 帧 |
| 安徽 + `CH_ID_RS232_1` | — | **禁止**（语音专用，安徽只 bind RS485 + RS232） |
| 广西（`0xAA`）+ 串口各协议 / 网口各协议 | RS485/RS232 链式（网口不绑） | **允许**：首字节 `0xAA` 与现有全部协议首字节（`0x7B` / `0x0A` / `0x5A` / `0x54` / `0xFF` / `0x5A5A5A5A` / `0xFFFF`）互斥，双向首字节快拒成立；本协议**非 `{` 帧族，不定义 `g_brace_proto_guard`**，无 EIDE 目录排除要求；残余风险 = 残帧重同步恰好落在 `0xAA` 且后续为 `0xBB` + 白名单命令码（对全部协议同样存在），由定长/结束码二次校验缓解 |
| 广西 + `CH_ID_RS232_1` | — | **禁止**（语音专用，广西只 bind RS485 + RS232） |
| 贵州治超（TCLY）+ 串口各协议（青海/山东/贵州/云南/四川MTC/ETC/治超/RLS/安徽） | RS485/RS232 链式 | **允许**：首字节 `0x54` 与既有串口首字节 `0x7B`/`0x0A`/`0xFF`/`0x5A` 全部互斥，双向首字节快拒成立（宿主推演 `gz_ol_frame_sim.py` §5 四协议两两 FAKE 已验）；4 字节引导串全匹配 + 长度/尾字节双重校验进一步降低误认领；贵州治超为**非 `'{'` 帧族，不定义 `g_brace_proto_guard`**，**无 EIDE 目录排除要求** |
| 贵州治超 + `CH_ID_RS232_1` | — | **禁止**（语音专用，贵州治超只 bind RS485 + RS232 + UDP 10011 + UDP 专用口 `CH_ID_UDP_GZOL`） |
| 贵州治超 + 贵州常规费显（同名地区、不同帧族） | RS485/RS232 链式 | **允许**：`0x54` vs `0x7B` 互斥；两者命名前缀已区分（`gz_` vs `gz_ol_`） |
| 贵州治超 + `CH_ID_UDP_GZOL`（专用业务口，与 TCP 业务口同号） | 与 TCP Server 同端口号、**不同协议栈**（UDP vs TCP） | **允许**：端口号空间按协议栈隔离，二者互不冲突；该通道只服务 GZ_OL（唯一绑定者），与 10011/CQ 口同号时由实例侧跳过绑定兜底 |
| 贵州治超 + IAP / LDI / 重庆CQ（`CH_ID_UDP` 10011 同槽，2026-09-14 增绑后） | RJ45（UDP 共享，同一 1536B RB） | **允许**：首字节 `0x54` 与 IAP `0x5A`/LDI `0xFF`/CQ `'{'` 或 `FF FF`+12B 全部互斥，**双向首字节快拒成立**——IAP probe 非 `0x5A` 即 FAKE（`app_iap.c:107-114`）、LDI probe 非 `0xFF` 即 FAKE（`app_ldi.c:408-415`）、CQ probe 仅认 `'{'`（花括号深度扫描）与 `FF FF` 起始的 12B 精确匹配（其余首字节一律 FAKE，`cq_probe_frame`）、本 probe 非 `0x54` 即 FAKE；CQ 无首字节为 `0x54` 的帧型。**不占第二份网口缓冲**（贵州治超只 `acquire` 不 `provide` RJ45 RB；`PROTO=ALL` 与 `PROTO=CQ` 两口径该槽均由 IAP/LDI/CQ 提供）。注册序（Makefile 收录序）：IAP → LDI → CQ → … → GZ_OL，互不干扰。证据已归档。**专用口与 10011 同槽同理**：`CH_ID_UDP_GZOL` 亦挂 RJ45 槽、probe 同函数，两 mask 共用同一队列；收包帧按 `frame_msg_t.ch` 携带的收包通道回源，不串台 |
| `{` 帧族任意两两（QH/SD/GZ/SC_MTC/YN） | RS485/RS232 链式 | ⚠️ **编译期互斥守卫**：五个协议主文件各定义同名强符号 `g_brace_proto_guard`（`__attribute__((used))` 防 `--gc-sections` 丢弃，`#ifndef STD_ALL_PROTO` 包裹）——EIDE 量产构建多个编入即链接报 `multiple definition of 'g_brace_proto_guard'`，无法绕过；〔历史〕Makefile 全协议开发构建经 `-DSTD_ALL_PROTO` 豁免共存（**现由 EIDE 的 `DevAll` 等价 target 承担，宏名同**，见 doc/构建开关总表.md §7）。EIDE excludeList 目录排除纪律仍为操作层首选 |
| **缅甸2D费显（MM）+ 四川治超（`0xFF` 首字节重叠）** | RS232 链式（MM **只绑 RS232**；治超 RS485+RS232） | **允许，无需 EIDE 互斥**：① MM 第二字节必须 `0xE0`，治超帧长字段非 `0xE0` 时 MM 立即 FAKE；② 治超帧长**恰为 `0xE0`** 时其 probe 判 WAIT（链式契约继续试下一协议）→ MM 对**非本模块命令**要求「结束码正签名 `[15..18]==00 00 00 00`」才 SKIP，正签名不成立则 FAKE（不消费字节）⇒ 治超长帧不被截断；③ 反向：MM 命令字 `{0x90,0x0A,0x0B}` 与治超命令集合 `{80~88,94,96,98,99,A0~A8,B6,B8,B9}` **无交集** ⇒ MM 不误吞治超帧。残余风险 = 治超长帧 `[15..18]` 恰为 4 个 `0x00`（`(1/256)^4` 量级，宿主推演 §6.5 已标注）。证据：`.analysis/myanmar/mm_frame_sim.py` §6（12 条用例） |
| **缅甸2D费显（MM）+ 四川 ETC / 安徽 / `{` 帧族 / RLS** | RS232 链式 | **允许**：MM 首字节 `0xFF`，与 ETC `0x0A` / 安徽 `0x5A` / `{` 族 `0x7B` 首字节互斥；RLS 亦 `0xFF` 但只绑 RS485（不同槽）。MM **非 `{` 帧族 ⇒ 不定义 `g_brace_proto_guard`、无需 EIDE 目录排除**。⚠ **RLS 与本模块在「干接点引脚层」另有冲突，见下一行** |
| **缅甸2D费显（MM）干接点输出 vs RLS 干接点输入（同一组引脚 SW1/SW2/SW3 = PE12/PE11/PE10）** | 无传输层冲突，**引脚归属互斥** | ⚠️ **同编时行为降级（2026-09-24 用户裁决；极性 2026-09-28 现场实测翻转为高有效，影响面随之更新）**：MM 初始化时把三脚接管为**高有效推挽输出**（点亮 = 输出高；`pl_gpio_release_exti` **按线**摘除 EXTI＝IMR/EMR/双边沿/PR/SYSCFG EXTICR，**不调 `HAL_NVIC_DisableIRQ`、不碰其它 EXTI 线**；详见 doc/16 §4.2）。此后 `dev_key_get_state()`（= `!pl_gpio_read`，直读 GPIO、低 = 「闭合」）读到的是**本板自驱电平**：**灯灭（输出低）→ 对应 SW 位读「闭合」**、某灯点亮（输出高）→ 该位读「未闭合」⇒ **上电默认全灭态 = 三路 SW 全读「闭合」**，`app_rls_cmd.c` 的 `rls_dry_contact_poll` 在 `PROTO=ALL` 等共存构建下约 2 个轮询拍（~200ms）后持续渲染「车道关闭」（优先级 车道关闭 > ETC专用 > ETC/人工）。**纪律：缅甸量产构建排除 RLS**——〔2026-10-13 更新：原 `targets.Myanmar`（继承 Debug 排除表已排除 `rls`）**已删除**；按 `doc/构建开关总表.md` §7.2 第 5 条点选组缅甸固件时保持 `rls` 排除即可〕；Makefile `PROTO=ALL` 开发构建下该现象为**已知预期** |
| 缅甸2D费显（MM）+ `CH_ID_RS232_1` | — | **禁止**（语音专用；MM 只 bind `CH_ID_RS232`） |
| **新疆费显（XJ）+ 四川治超（`0xFF` 首字节重叠）** | RS485 + RS232 链式（XJ **绑两槽**；治超同） | **允许，无需 EIDE 互斥**：四重隔离 —— ① 治超帧长字段非 `0xDC` 时 XJ 第二步即 FAKE；② 治超帧长**恰为 `0xDC`(220)** 时 XJ 长度上界 `>106` 立即 FAKE（**不 WAIT**，不把等待预算摊到别的协议头上）；③ 治超命令字 `0x80~0xB9` 无 ASCII 字母，XJ 命令白名单快拒；④ BCC 第三重校验。反向：治超对本协议帧按长度字段（5~106）判 WAIT（链式继续试下一协议）→ XJ probe READY 认领（**XJ 在收录序最末 = probe 最低优先级**——Makefile 时代如此，EIDE 侧由 objlist 路径字典序决定、本模块仍排全部协议之后）。残余误判概率 = `14/256 × 1/256 ≈ 2.1e-4`，且要求治超长度字段恰为 `0xDC` 且 ≤106（与「长度 = 0xDC = 220」互斥）⇒ 实际为 0。证据：`.analysis/xinjiang/xj_frame_sim.py` §8（11 条用例） |
| **新疆费显（XJ）+ RLS / MM / ETC / 安徽 / `{` 族 / GZ_OL** | RS485/RS232 链式 | **允许**：XJ 第二字节强判 `0xDC`（RLS `FE` / MM `E0` 在此分道）、其余协议首字节（`0A`/`5A`/`7B`/`54`）与 `0xFF` 互斥，双向快拒成立。XJ **非 `{` 帧族 ⇒ 不定义 `g_brace_proto_guard`、无需 EIDE 目录排除** |
| **新疆费显（XJ）+ `CH_ID_RS232_1`** | — | **禁止**（语音专用；XJ 只 bind `CH_ID_RS485` + `CH_ID_RS232`） |
| 新疆费显（XJ）+ IAP/LDI/CQ/GZ_OL/YN_OL（网口槽） | **不同槽** | 允许：XJ 不绑任何网口通道（文档为 RS232；RS485 为现场备选） |
| 重庆CQ + LDI | RJ45（UDP/UDP_CQ/TCP 共享） | ⚠️ **产品互斥**（EIDE target 的 excludeList 目录排除二选一——原 Makefile `PROTO=CQ` 剔除 LDI 目录的做法已随 Makefile 移除，配方见 doc/构建开关总表.md §7）；全协议 dev 构建下二者共存于 RJ45：CQ JSON `{` 与 LDI `FF FF` 首字节互斥可分；**10011 口 12B 二进制帧（CQ 重启/搜索请求）先经 LDI probe：CQ 12B 帧 len=00 00 00 02 且 CRC16-XMODEM 恰好通过 LDI 校验，LDI probe 对 `data_len==2` 显式 FAKE 放行（2026-08-21 修复，此前沿身份校验路径 SKIP 吞帧致 CQ 12B 在 10011 无响应），CQ probe 仍认领——「LDI 先探测、FAKE 放行、CQ 收单」，无功能受限**。另：dev 共存构建 CQ 业务口固定 20103、不读 Sector1 net_cfg.udp_port（dev 构建无写入方；方案 B 2026-08-21 起 CQ 构建读 udp_port、TCP 口 port 不再被 setip 污染），见 doc/03 PartB B.11 |
| 重庆CQ + IAP | RJ45（UDP 共享） | 允许：CQ 首字节 `{` 或 `FF FF`+12B 精确匹配，IAP 首字节 `0x5A` 快拒；CQ probe 对非匹配 `FF` 前缀 FAKE，互不误伤 |
| 重庆CQ + `{` 帧族（QH/SD/GZ/SC_MTC） | 不同物理口（RJ45 vs RS485/RS232） | 允许：不同 RB 槽不冲突；CQ 非串口 '{' 帧族协议，**不定义** `g_brace_proto_guard`（守卫仅约束串口 '{' 帧族四协议） |

> 说明：`{` 帧族互斥由编译期守卫在链接期兜底强制；excludeList 目录排除为操作层首选（编译更早失败、意图明确）。〔历史〕原 Makefile 全协议构建（`-DSTD_ALL_PROTO`）下守卫失效，共存风险按上表各行的 probe 注册序纪律管控；**现等价物 = EIDE `DevAll` target 加 `STD_ALL_PROTO` 宏**（doc/构建开关总表.md §7）。

---

## 7. 与 RAM 文档的边界

- RB **行为**以本文件为准；RB **放 SRAM**、堆回退、显存占 CCM → **仅 06**。  
- 多协议任务栈挤堆的历史问题（已回退）→ 归档见 `doc/00_变更历史.md`，结构方案不在本目录改代码。

---

## 8. 非目标

- 不刷机动态下载未编入的解析器  
- 不保证帧头冲突协议在同一字节流上零误判  
- 不把 `APP_PROTOCOL` 作为主切换手段  
