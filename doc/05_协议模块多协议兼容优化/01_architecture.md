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
> （现行唯一调用方 = LDI 装载：W25 有效 host 才调用）。协议绑本通道只消费入站（YN_OL 先例），
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
| RS485 | 768 | QH / RLS / SC_ETC / SC_MTC / SC_OL / SD / GZ / YN / ANHUI / GZ_OL / **YN_OL** / **XJ**（2026-10-12 第二轮起自备槽体） | RS485 | 青海、RLS、四川三协议、山东、贵州、云南、安徽、贵州治超、**云南治超**、**新疆（自备槽体 `RB_PROVIDE_WEAK` + acquire）** |
| RS232 | 768 | QH / SC_ETC / SC_MTC / SC_OL / SD / GZ / YN / ANHUI / GZ_OL / **YN_OL** / **MM** / **XJ**（2026-10-12 第二轮起自备槽体） | RS232 | 青海、四川三协议、山东、贵州、云南、安徽、贵州治超、**云南治超**、**缅甸2D费显（只绑本槽）**、**新疆（自备槽体 `RB_PROVIDE_WEAK` + acquire）** |

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

变更记录（2026-08-14）：LDI 2→4、青海对齐协议后深度 3、AH 1→3；IAP/RLS 维持 2。
变更记录（2026-08-14）：新增四川三协议（ETC/MTC/治超）各深度 3。
变更记录（2026-08-15）：四川 ETC payload 64→149（0x0D 定界上限对齐 9K1F212701 etc.c，全屏数据无固定 56B）。
变更记录（2026-08-17）：四川治超 payload 249→255（=长度字段上限 FF，消除 250~255 长帧被队列截断的越界读）；MTC '8' 亮度参数兼容二进制 0~8 与 ASCII '0'~'8' 双格式；MTC 'A' 4B/5B 判别加 b2≤2 门限，消除颜色帧 BCC='}' 误判。
变更记录（2026-08-17）：MTC '{' 帧族改「'}' 定界变长」扫描（对齐 9K1F212701 mtc.c 逐字节扫 '}'），废除每命令定长双长度（'3'→20/21B 等）与 '78' BCC 校验；字段偏移按变长重算（'3' 文本=raw_len-4、'4'=raw_len-3、'6' 数字串=raw_len-4、'78' 文本=raw_len-4）；修复上位机 `{3 1 1234 } 3 }`（10B）单行乱码（定长 probe 跨帧拼凑 20B 认领含 '}'/'{' 垃圾文本）；payload 上限仍 74B（队列约束）。宿主推演 `~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/sc_mtc_frame_sim.py`。
变更记录（2026-08-17）：新增山东协议（SD）深度 3、payload 259（与青海同构；命令字 '1'~'5','7','8'，无 '6'）。
变更记录（2026-08-17）：新增贵州协议（GZ）深度 3、payload 259（与青海同构；命令字 '1'~'9','A','B' + 0x01/0x02 二进制命令字）。
变更记录（2026-08-24）：新增云南协议（YN）深度 3、payload 259（与青海/贵州同构；命令字 '1'~'9','A','B' + 0x01/0x02；'6'=单行清除、'3'/'4' 按 24 点阵 FONT_24 渲染、0x02 应答 PROGRAM_CODE、无上电效果 default 文件）。
变更记录（2026-08-24 修订，用户决定 1~10）：YN 语义定稿——'2' 自检改老化循环显示 + 每 5s 语音「系统正在自检」（可被下一帧打断）；'3'/'6' 行号 '1'~'5' 全接受（行 5 执行但不落屏）；'8' 亮度 0x00=恢复光敏自动、'1'~'8' 手动档；0x01 全屏点亮扩至 01红~07白 七色；0x02 应答 PROGRAM_CODE；移除上电效果 default 文件（不注册默认显示）。
变更记录（2026-09-04）：新增安徽协议（ANHUI）深度 3、payload 261（数据长 1B 上限，CRC 文档注明不校验）；帧头 0x5A 串口槽唯一（IAP 5A5A5A5A 在网口槽不相交）；**队列体+cb+任务帧缓冲为静态 SRAM（+1156B，与青海/贵州/云南同——用户裁决，CQ 才是 CCMRAM 例外）**；SRAM 预算经链接期下限收紧 + RTT 缓冲 1KB 让位（doc/13 §6）；0x96/0x97 录制语音占位（语音板无段号接口），详见 doc/13。
变更记录（2026-09-07）：安徽动态显示（0x86~0x89）落地——接入通用滚动模块 app_scroll（`Application/Src/app_scroll.c` + app_render 导出 `app_render_draw_glyph_clipped` 逐像素裁剪绘制）；mode 1=从右往左/2=从左往右/3=从下往上/4=从上往下、mode=0/全 0 帧停止该行、循环滚动、stay_ms v1 忽略；静态 SRAM +372B、scroll_task 1KB 栈惰性（doc/13 §9、doc/06-04、doc/01_显示系统/动态滚动显示实现记录.md）。
变更记录（2026-08-20）：新增重庆CQ（JSON `{` + 12B 二进制）深度 3、payload 1044、静态体 3156B **置 CCMRAM**；与 IAP/LDI 共享 RJ45 RB，CQ 源收录序在 LDI 之后（probe 注册序）。
变更记录（2026-08-21）：CQ 业务端口语义修复——`PROTO_CHONGQING` 才读 Sector1 net_cfg.port（无效/0 回退 20103），dev 共存构建固定 20103（不读 LDI 语义的 net_cfg.port）；搜索应答 port 字段改报 `app_udp_cq_get_port()`；10011 口 12B 帧「LDI 先探测、FAKE 放行、CQ 收单」实态修正（见 §6 兼容矩阵与 doc/03 PartB B.11）。
变更记录（2026-08-21 方案 B）：Sector1.net_cfg 新增 `udp_port`（CQ UDP 业务口专有），TCP/UDP 端口彻底分离——`PROTO_CHONGQING` 改读 net_cfg.udp_port（setip 写入，默认 20103），dev 共存构建固定 20103；net_cfg.port 恒为 TCP 业务口不再被 setip 污染（见 doc/03 PartB B.7.1/B.11、doc/07 §15）。
变更记录（2026-09-14）：新增贵州治超屏协议（GZ_OL，"TCLY" 帧族：`54 43 4C 59` + 长度 2B 小端 = 整帧长 + 命令字 4B + 载荷 + `0x00`，无校验）深度 3、payload 256；首字节 `0x54` 全槽唯一 → **无需 EIDE 目录互斥、不定义 `g_brace_proto_guard`**；队列体+cb+任务帧缓冲 1136B **置 CCMRAM**（ALL 口径 SRAM 余量仅 496B）。同轮新增 P10 模组驱动 `dev_display_22_1703.c`（与 1-263 编译期二选一，Makefile `DISP` 开关）。详见 doc/14。
变更记录（2026-09-14，GZ_OL 增绑 UDP 10011）：贵州治超新增第三个 mask 绑 `CH_ID_UDP`（挂 RJ45 共享槽，**只 acquire 不 provide**——网口槽 1536B 仍由 IAP/LDI/CQ 提供，不新增第二份缓冲）；网口槽 4 协议首字节互斥复核通过（见上表 RJ45 行）；增绑增量 text +64 / bss +4（+1 个 mask 变量），rodata/data/ccmram +0，SRAM 余量 496B 不变；源固件设备侧 UDP 口为 10028 + 搜索应答广播，本实现为 10011 + 单播回源（联调须改上位机目标端口，详见 doc/14 §11）。
变更记录（2026-09-14，GZ_OL 专用 UDP 业务口落地）：新增第 8 号逻辑通道 `CH_ID_UDP_GZOL`（`CH_ID_MAX 8→9`）——GZ_OL `0x40` 改端口的生效对象：端口 = Sector1 `net_cfg.port`（与 TCP 业务口同号不同协议栈，出厂 9528；空/0 回退 9528 + RTT 告警，与 10011/CQ 同号则跳过绑定 + RTT 告警）；实例镜像 CQ 业务口（`app_udp.c` `udp_gzol_task`/`udp_gzol_connect_task`，**只 acquire 不 provide** RJ45 槽）；`app_boot.c` 在 `app_net_boot_apply()` 之后 `app_udp_gzol_start()`；**10011 绑定保留**（第四 mask），两通道并存且应答各走收包通道独立 src 快照；`MEMP_NUM_NETCONN(8)` 常驻 5、峰值 8（含广播回退临时 conn）→ **维持 8 不扩池**，UDP_PCB 常驻 3/8；静态增量 text +752 / rodata +296 / data +8 / bss +40，ccmram +0，SRAM 余量 496→448B，ucHeap +2 任务（2×1KB，水位待现场标定）。详见 doc/14 §13 与 `.analysis/9k23881580/gz_ol_udp_service_port_report.md`。
变更记录（2026-09-17）：新增云南治超屏协议（YN_OL，YN_1.3.0 `{` 帧族：`{` + 命令字 + 二进制 len + 参数 + `}`，与云南常规完全同构、无校验）深度 3、payload 255（帧 ≤259）；命令集 `'1'`~`'5'`/`'8'`/`'A'` + `0x42`~`0x46`/`0x50`（六行清除）+ `0x47`~`0x49`/`0x51`（IP 配置）+ `0x01`/`0x02`，**'6'/'7'/'9' 文档明示治超屏不开发 → probe 快拒**；**属 `{` 帧族 → 定义 `g_brace_proto_guard` 互斥守卫**（`arm-none-eabi-ld -r` 双编实测报 multiple definition），量产 EIDE 必须与其它 `{` 族目录二选一。
**2026-09-17 用户裁决轮（8 条）**：① **绑 RS485 + RS232 + TCP Server + TCP Client 四通道**（现场可能用网口控制）——TCP 侧与 CQ JSON 的 `{` 同首字节竞争**由通道掩码隔离解决、不靠源码收录序**（CQ 只绑 `CH_ID_UDP`/`CH_ID_UDP_CQ`，TCP 链上 CQ probe 不被调用；本模块**不绑 10011** → CQ 量产行为逐字节不变；**未触碰 CQ 源码**），RJ45 槽**只 acquire 不 provide**（网口 1536B 仍由 IAP/LDI/CQ 提供）；② 端口字段高字节在前 BE16 转正为已裁决；③ `0x47` 应答 0x51 + 100ms 软复位维持；④ 亮度 `0x00`/`'0'` 自动 + `'1'`~`'8'` 手动维持；⑤ **`0x49` 屏体参数持久化**——W25Qxx **独立 4KB 扇区 `capacity - 12288`** 12B 记录（magic + version + 字体/字宽 + CRC32，仿 LDI 范式；LDI 占 `capacity-4096`、app_render 占 `capacity-8192`，三者各占一扇区避免整扇区读-改-写互擦）+ 上电装载；⑥ **上电默认画面不实现**（原 `app_yn_ol_proto_default.c` 及 5s 任务已删除，走 STD 默认显示链路）；⑦ 与云南常规量产必二选一（`ld -r` 实测）；⑧ 默认字号沿用 STD 工程默认 FONT_16/FONT_ST。队列体+cb+任务帧缓冲 1152B **置 CCMRAM**；A/B 增量实测 text **+3520** / rodata +680~688 / data +0 / ccmram +1152 / bss +36，SRAM 余量 440→**400B**（ALL/1_263）/ 4712→**4680B**（CQ）/ 424→**384B**（ALL/22_1665），三口径零新增告警；宿主推演 `yn_ol_frame_sim.py` **172 用例通过**。详见 doc/15。
变更记录（2026-09-24）：新增**缅甸 2D 费显协议（MM）** 深度 3、payload **20**（定长帧长，本协议无变长帧；`MM_MSG_SIZE = sizeof(frame_msg_t) + 20`）；**只绑 `CH_ID_RS232`**（用户需求「只使用 232」；不绑 RS485/网口/`CH_ID_RS232_1`），RS232 槽体沿用既有 weak 提供（本模块 `RB_PROVIDE_WEAK` 仅作极端裁剪构建兜底，实测非链接获胜者 ⇒ **不新增 RB 体**）；**队列体+cb+任务帧缓冲 + 处理任务栈/TCB 置 CCMRAM**（1320B 实测增量，CQ/GZ_OL/YN_OL 先例）。**帧 = 定长 20 字节**（`FF` + `E0` + 命令 + 12B 内容 + 5B 结束码），无校验/无长度字段/无应答；只解析 `0x90`/`0x0A`/`0x0B` 三条车道灯命令（DATA0 1=开 / 0=关），文档 `0x10`/`0x40`/`0x80` 由 probe 整帧 `SKIP`。**首字节 `0xFF` 与四川治超（SC_OL）重叠**（同槽）：SC_OL 按长度字段 `0xE0=224` 判 WAIT（链式契约继续试下一协议）→ 本 probe READY(20) 认领；反向靠「SC_OL 命令字集合与本模块 `{0x90,0x0A,0x0B}` 无交集」+「非本模块命令须结束码正签名 `byte[15..18]==00 00 00 00`（缺失则 FAKE 不消费）」⇒ **双向不误吞、不卡链**（残余风险 = 治超长帧 `[15..18]` 恰为 4 个 0x00，概率 `(1/256)^4` 量级，宿主推演 §6.5 已标注）。⇒ **无需 EIDE 目录互斥、不定义 `g_brace_proto_guard`**（非 `{` 帧族）。上电三路全灭（同源 8051 `C1=C2=C3=0`；**该上电语义已于 2026-09-29 按用户需求改为「上电默认显示红叉 = 红灯 S1/PE12 点亮」，见文末「再修订（2026-09-29）」**）。阶段一实测增量 = text **+672** / rodata **+416** / data +0 / ccmram **+1320** / bss **+16**（SRAM 余量 2016 → **2000B**，CCM 余 25884B），零新增告警；宿主推演 `.analysis/myanmar/mm_frame_sim.py` **77/77**（阶段一：干接点引脚未查清 ⇒ 输出层为未接线占位，**该占位已由阶段二取代**，见下条）。  
变更记录（2026-10-08，审计 C3-D）：**网络/串口通道改为「按协议绑定启动」**——`app_dispatch.c/.h` 新增只读查询 `app_dispatch_channel_has_proto(ch_id)`（越界返回 false），`app_boot.c` 的七条通道启动（TCP Server/Client、UDP 10011、UDP_CQ、UDP_GZOL、RS485、RS232）各加一道门控：**有绑定照常启动、无绑定跳过**。依据 = 绑定关系全部由 `app_proto_bind_channel()` 在 `sw_board_init()`（sw_app_initcall）中声明，**早于通道启动**（35 处 bind 全部在协议 init 内、经 initcall 注册）；未绑定的通道启动后数据无人消费（`ch_proto_map==0` 时静默丢弃 + 零日志，2026-09-18 前的 `[disp]` 告警已删），跳过可同时省下 netconn 配额（审计 C7 池峰值 = 容量 8）与 DMA RX。**零行为回归**：有绑定的通道逐条走原路径（口径无关：连接集/显示模组选择不改此语义）；`app_rs232_1_start()` 为 USART6 语音 TX 桩（无通道任务、协议禁止 bind）保持无条件调用。实测（2026-10-08 四口径预/后同树对照）：增量全部在 `.text`（`app_dispatch` +32、`init_task` +68），其余段 ±0；零新增告警。**配套（C3-C）**：LDI 12H/D1H 上报改以 Sector1 为单一真源（见 doc/07 §16）。
变更记录（2026-09-24，缅甸阶段二：干接点真实驱动 + EIDE 独立目标）：**用户裁决 S1/S2/S3 = PE12/PE11/PE10**（复用原 SW1/SW2/SW3 端子，方向由输入改**输出**）、**低电平有效、推挽、上电三路全灭、关灯=断开**；Application 层仍不碰 HAL——Platform 新增 `pl_gpio_config_output()`（推挽输出）+ `pl_gpio_release_exti()`（**按线**摘 EXTI：`IMR/EMR/RTSR/FTSR` + 写 1 清 `PR` + 条件清 `SYSCFG->EXTICR`；**不调 `HAL_NVIC_DisableIRQ`**，TEST 键 `EXTI9_5` 与其它线不受影响；HAL 源码依据 = `HAL_GPIO_Init` 的 EXTI 分支被 `Mode & EXTI_MODE` 守卫、`HAL_GPIO_DeInit` 只条件清 IMR/EMR/双边沿+EXTICR 且**全文件不写 `PR`**）。**副作用（预期）**：SW1/SW2/SW3 输入语义失效，`dev_key_get_state()` 读自驱电平 ⇒ **RLS 干接点轮询在共存构建下可能误判车道状态**（新兼容矩阵行见 §6；量产排除 RLS 即无影响）。**映射单一改动点** = `app_mm_proto_cmd.c` 的 `s_mm_lamp_map`（port/pin/active_low）+ `_mm_drive()`；抓帧诊断每帧一行（`MM_RTT_DIAG` 门控）。**实测**（`PROTO=ALL DISP=1_263`）：text 176064 / rodata 203544 / ccmram 39652 / bss 124852（SRAM 余 2000B、CCM 余 25884B）；`PROTO=CQ`：text 162868 / rodata 202888 / ccmram 37396 / SRAM 余 6280B；**零新增告警**。**A/B 增量**（同树摘除两文件）：text **+1088**（模块 866 + 新 Platform 接口 224）/ rodata **+504** / data +0 / ccmram **+1320** / bss **+16**；相对阶段一占位实现 = text +416 / rodata +88、其余 ±0。**EIDE**：`files` 收录缅甸两文件 + **Debug `excludeList` 增缅甸模块（Debug 行为不变——当时口径；**2026-09-28 用户裁决起 Debug 现行 = 编入缅甸、排除 LDI + 四川三**，见 doc/16 §9.1）** + **新建 `targets.Myanmar`**（Debug 模板，差异仅「不排除缅甸」+「incList 增 `Application/Inc/ProtocolParser_Myanmar`」，云南治超等继承排除）；等价口径实测链接通过（1_969 ⇒ CCM 54292 余 11244B；22_1665(8×4) ⇒ CCM 62164 余 3372B，**EIDE 子集不存在 Makefile ALL 口径的 8×4 预存溢出**）。宿主推演 77 → **103/103**；新增 `.analysis/myanmar/check_eide_targets.py`（**19/19**，含与 EIDE 上次构建 `builder.params` 源清单逐条对拍）。详见 doc/16 §4/§9。**后续修订（2026-09-28）**：其中「低电平有效」极性经**现场实测翻转**为**高电平有效**（复位后红灯即亮 + 65 脚 PE12 实测 3.3V ⇒ 3.3V = 点亮；映射表三条 `active_low` 改 `false`，其余代码零改动），宿主推演 **108/108**、副作用口径随之更新（全灭态 = 三路 SW 读「闭合」）——见 doc/16 §4.1/§4.3 与修订记录。**再修订（2026-09-28 同日，TEST 键轮）**：新增 **TEST 键切换 `0x0A` ⇄ `0x0B` 箭头落点**（`mm_arrow_map_toggle()` / `_mm_effective_lamp()`，红灯 `0x90` 不受影响、开机复位不持久化）+ **缅甸版出厂测试文件 `Application/Src/app_factory_test_mm.c`**（与标准版 `app_factory_test.c` **二选一编译**：Makefile `FACTORY=STD|MM` / EIDE `excludeList`；同名对外符号 ⇒ 同编即 `multiple definition` 天然互斥）；评测不涉及协议分发层/队列/通道 —— **probe 判据、RB、队列、兼容矩阵均零改动**（宿主推演 108 → **142/142**）。详见 doc/16 §13。**再修订（2026-09-29，上电默认红叉轮）**：① **上电默认态改为「红叉亮」**——`mm_apply_init()` 接管完成后 `mm_apply_lamp(MM_LAMP_RED, true)`（红灯 S1/PE12 输出高、两绿箭头灭），**取代** 2026-09-24 的「三路全灭」；唯一动作点 / 极性表达式 / 接管顺序（熄灭→点亮在切成输出之后）全部不变 ⇒ **兼容矩阵零变化**（RLS 误判结论仍为「车道关闭」，只是命中路径由「三位全闭合」变为「SW3 闭合」）。② **新增开机默认画面「红色叉（×）」**（`app_mm_proto.c` 的 `_mm_default_red_cross()`，**仅当 `dev_display_get() != nullptr` 时**注册；整屏清黑 + 两条对角线，`COLOR_RED`，几何取自 `dev_display_get()`）—— 无显示构建不注册、零告警；注册单槽首个生效 ⇒ `PROTO=ALL` 开发构建画面仍由山东先占槽（不变）。③ 构建（`PROTO=ALL DISP=1_263`，全量 `-B`）：`FACTORY=STD` text **176576** / rodata **203672**（SRAM 余 1992B）、`FACTORY=MM` text **175184** / rodata **203640**（SRAM 余 2256B），增量 **text +240 / rodata +24**、零新增告警；无显示等价口径链接通过（text 137632 / rodata 197000 / ccmram 16084，SRAM 余 11128B）。④ 宿主推演 142 → **159/159**（新增 §11 红叉几何 6 条 + 源码断言 9 条）、`check_display_guards.py` **16/16 + 99/99**。详见 doc/16 §14。**〔2026-10-13：`targets.Myanmar` target 已整块删除（EIDE 重组：`Base` 基线 + `Debug` 同基线 + `Release` 损坏）；缅甸固件改按 `doc/构建开关总表.md` §7.2 第 5 条在 GUI 点选组出；上列 EIDE 相关口径（含「Debug 现行 = 编入缅甸」）随之作废，源码零改动〕**

变更记录（2026-10-12，新疆费显协议接入 + 调度框架掩码扩容 32→64）：**① 掩码扩容**——`app_proto_register()` 在 `PROTO=ALL` 口径已被调用 **32 次**，`registered_mask`（原 `uint32_t`，`PROTO_MAX_COUNT=32`）满位 ⇒ 新增协议注册返回 0 并静默退出（现场现象 =「完全没有反应」）。改动：`PROTO_MAX_COUNT 32→64`、`proto_mask_t` → `uint64_t`、`registered_mask`/`ch_proto_map[]` 随类型、新增 `bit_ctz64()`（`Kernel/Inc/bit_utils.h` + `.c`，`x==0` 与 `bit_ctz` 同语义回 0）、全部位移改 `((proto_mask_t)1U << i)`；`app_channel_dispatch()` 的 RB 去重表改按缓冲池槽数 `RB_CNT_MAX`(3) 开表（按 64 指针开表会把 1KB 通道任务栈帧从 140B 抬到 **268B**，实测；按槽数开表后 64B）。**只加宽不改逻辑**：`mask=0` 仍是「无协议」、槽位仍按最低空闲位分配、既有 35 处注册的 mask 值逐位不变。实测（`PROTO=ALL DISP=1_263 FACTORY=STD`，全量重编）`.text +592` / `.bss +588`（`g_dispatch` +424 + 35 个 `s_*_mask` 4→8B +140 + 对齐）/ `._user_heap_stack +4`，其余段 ±0，零新增告警。**② 新疆费显协议（XJ，协议文档「费额显示器通讯协议 2024年」）**：帧 `FF DC` + 长度(1B = 整帧总长) + 命令字(1B ASCII) + 载荷 + BCC(1B，除自身外全字节 XOR)，帧长 5~74，无转义、**无应答**；9 命令 `'T' 'O' 'E' 'F' 'J' 'K' 'V' 'S' 'C'`（`'D'` 0x44 与「彩色行协议」为文档缺章 → 不进白名单）；`'T'` 严格 16 字节/行（忽略显示行字段、GBK 不跨边界劈开、先整屏清黑、FONT_16/FONT_ST）、颜色 = RGB 各 ≥128 位掩码（与 `display_color_t` 恒等）、亮度 0 = 恢复光敏自动 / 1~255 → 8 档、心跳 15s 超时红字「系统维护中」（开关 `XJ_FAULT_SCREEN`，默认 1；=0 时无计数无渲染）、`'V'`/`'S'` 语音（`'S'` 用固定中文文案表；维语/音色为硬件缺口）。**通道 = RS485 + RS232 双槽两独立 mask**（同一 probe/队列），**只 acquire 不 provide RB**（取不到即静默跳过，两槽皆不可用则整模块不激活），禁绑 `CH_ID_RS232_1`；队列体+cb+帧缓冲+任务栈/TCB **置 CCMRAM**。**免 EIDE 目录互斥**（非 `{` 帧族）：与四川治超的 `0xFF` 首字节重叠由「第二字节 `0xDC` + 长度 ≤74 + 命令字白名单 + BCC」四重隔离。实测模块增量 = text **+1856** / rodata **+248** / data ±0 / ccmram **+1544** / bss **+32**；SRAM 余量（ALL/1_263/STD）2088 → **1464B**、`PROTO=CQ` 口径 **5756B**，`XJ_FAULT_SCREEN=0` 再省 text −288 / rodata −16。宿主推演 `.analysis/xinjiang/xj_frame_sim.py` **246/246**，`check_channel_gate.py` **15/15**、`check_display_guards.py` **16/16 + 100/100**。详见 doc/19 与 `.analysis/xinjiang/implementation_report.md`。**〔同日晚第二轮已修正（本行保留首轮状态）：帧长 74→106、命令 9→14（PDF 全文）、自备 RS485/RS232 两槽体、payload 74→106（队列 342 / `.ccmram` 1668B）；现行口径见上文 §3.3/§4.1 与紧随其后的 2026-10-13 修订行、doc/19 §2/§4/§6〕**
变更记录（2026-10-13，文档补齐，非代码变更，本轮）：本文件新疆（XJ）口径由 2026-10-12 首轮滞留态就地补齐为现行态 —— ① 帧长 5~74、9 命令 → **5~106、14 命令**（`'D'`/`'G'`/`'H'`/`'Z'`/`'P'` 已实现、`'B'` 不进白名单；长度 = 整帧总长含头与 BCC 1B）；② 「只 acquire」→ **自备 RS485/RS232 两槽 `RB_PROVIDE_WEAK` 槽体 + acquire**（2026-10-12 第二轮落地；单协议目标必需，否则模块静默不注册、串口通道被门控跳过）；③ payload 74 / `.ccmram` 1544B → **msg 114（= `sizeof(frame_msg_t)+106`）、队列体 3×114=342、模块 `.ccmram` 跨度 1668B**（SRAM 侧 32B + 自备槽体 1568B：data 32 + bss 1536）；④ 调度框架掩码 32→64（`PROTO_MAX_COUNT 64` / `proto_mask_t` → `uint64_t` / `bit_ctz64()`，机制已在 2026-10-12 条目记录）；⑤ 兼容矩阵 XJ×治超行同步改正（长度上界 106 / 白名单 14 / 残余概率 2.1e-4）。权威 = `doc/19_新疆费显协议/README.md` §2/§2.1/§4/§6。**源码未经本轮改动**。
变更记录（2026-10-13，用户裁决执行轮：编译期断言清零 + 只读观察状态清理）：① 业务代码编译期断言**全部移除**（本轮清 `dev_display_22_1665.c` 2 条 + `app_dispatch.c` 4 条 RB 表/尺寸断言；连同前两轮共 82 条）⇒ 约束 = 一行契约注释 + doc 指针 + 链接期兜底；② `app_dispatch` 的 `qfull_drop`/`resync_byte`/`notify_drop` 三个诊断计数域与 getter 删除（`dispatch_ctx_t` −12B 域 ⇒ `g_dispatch` −8B；`app_channel_dispatch`/`frame_dispatch_task` 的计数自增一并删除，**调度行为不变**：队列满丢帧 / 1 字节重同步 / 通知重试路径原样保留）；③ `g_tcp_server_connected`、`app_udp_gzol_bind_status(_str)`、`app_render_*` 观察 API、`pl_tim_dbg_freeze`、`s_mm_lamp_state` 等只读观察 / 调试设施删除（零业务消费者，判读改用协议层实发实收）。实测（同树 A/B，全量重编）：`PROTO=ALL` `.text −224 / .bss −24`（CQ −208 / −24），零新增告警；逐对象期望清单 7/7 与断言隔离 A/B 2/2 见 `.analysis/comment_pass_20261013/report.md` 裁决执行轮。

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
| 贵州治超（TCLY）+ 串口各协议（青海/山东/贵州/云南/四川MTC/ETC/治超/RLS/安徽） | RS485/RS232 链式 | **允许**：首字节 `0x54` 与既有串口首字节 `0x7B`/`0x0A`/`0xFF`/`0x5A` 全部互斥，双向首字节快拒成立（宿主推演 `gz_ol_frame_sim.py` §5 四协议两两 FAKE 已验）；4 字节引导串全匹配 + 长度/尾字节双重校验进一步降低误认领；贵州治超为**非 `'{'` 帧族，不定义 `g_brace_proto_guard`**，**无 EIDE 目录排除要求** |
| 贵州治超 + `CH_ID_RS232_1` | — | **禁止**（语音专用，贵州治超只 bind RS485 + RS232 + UDP 10011 + UDP 专用口 `CH_ID_UDP_GZOL`） |
| 贵州治超 + 贵州常规费显（同名地区、不同帧族） | RS485/RS232 链式 | **允许**：`0x54` vs `0x7B` 互斥；两者命名前缀已区分（`gz_` vs `gz_ol_`） |
| 贵州治超 + `CH_ID_UDP_GZOL`（专用业务口，与 TCP 业务口同号） | 与 TCP Server 同端口号、**不同协议栈**（UDP vs TCP） | **允许**：端口号空间按协议栈隔离，二者互不冲突；该通道只服务 GZ_OL（唯一绑定者），与 10011/CQ 口同号时由实例侧跳过绑定兜底 |
| 贵州治超 + IAP / LDI / 重庆CQ（`CH_ID_UDP` 10011 同槽，2026-09-14 增绑后） | RJ45（UDP 共享，同一 1536B RB） | **允许**：首字节 `0x54` 与 IAP `0x5A`/LDI `0xFF`/CQ `'{'` 或 `FF FF`+12B 全部互斥，**双向首字节快拒成立**——IAP probe 非 `0x5A` 即 FAKE（`app_iap.c:107-114`）、LDI probe 非 `0xFF` 即 FAKE（`app_ldi.c:408-415`）、CQ probe 仅认 `'{'`（花括号深度扫描）与 `FF FF` 起始的 12B 精确匹配（其余首字节一律 FAKE，`cq_probe_frame`）、本 probe 非 `0x54` 即 FAKE；CQ 无首字节为 `0x54` 的帧型。**不占第二份网口缓冲**（贵州治超只 `acquire` 不 `provide` RJ45 RB；`PROTO=ALL` 与 `PROTO=CQ` 两口径该槽均由 IAP/LDI/CQ 提供）。注册序（Makefile 收录序）：IAP → LDI → CQ → … → GZ_OL，互不干扰。证据见 `.analysis/9k23881580/gz_ol_udp_binding_report.md` §2。**专用口与 10011 同槽同理**：`CH_ID_UDP_GZOL` 亦挂 RJ45 槽、probe 同函数，两 mask 共用同一队列；收包帧按 `frame_msg_t.ch` 携带的收包通道回源，不串台 |
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
- 多协议任务栈挤堆的历史问题 → [03](./03_freertos_heap_side_effect.md)，结构方案不在本目录改代码。

---

## 8. 非目标

- 不刷机动态下载未编入的解析器  
- 不保证帧头冲突协议在同一字节流上零误判  
- 不把 `APP_PROTOCOL` 作为主切换手段  
