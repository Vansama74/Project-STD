# 04 当前 SRAM / CCMRAM 占用分布（实测）

> 〔2026-10-13〕本仓库 Makefile 已移除，构建仅 EIDE；本文中以 make 为入口的命令一律作废（历史记录），口径迁移见 doc/构建开关总表.md §7。

> **角色**：本目录 **唯一占用实测账本**  
> **状态**：现行　|　采样：**EIDE Debug（GCC 15.2）** / 编入 **1-577 3×3**、排除 1-260 / 1-969 / RLS / AH_MQTT / **2026-08-14 16:37**（重建复核，数字未漂移）  
> 〔2026-10-13 注〕上列采样口径为 **2026-08-14 当时的 EIDE Debug**（其显示/协议选编早已变更）；同日 target 重组后
> **Base/Debug = 基线**（核心 + IAP + 标准厂测 + RTT + 显示基类，协议与显示驱动按需点选，见 `doc/构建开关总表.md` §7）
> —— 采样数字仅对标注日期/口径成立，勿当成当前 target 构型。
> **最新采样**：**2026-09-29 代码瘦身轮第二轮**（自研诊断设施保持删除、**SEGGER RTT 底层恢复** +
> 注释精简；业务行为零变化，全量重建，仅 3 条既有 HAL 告警）——`PROTO=ALL DISP=1_263 FACTORY=STD`
> **`.text 167752 / .rodata 195776 / .data 1656 / .bss 124768 / .ccmram 39652 / ._user_heap_stack 2560`
> ⇒ SRAM 合计 128984B（余 2088B）、CCM 余 25884B**；`FACTORY=MM` `.text 166216 / .rodata 195664 /
> .bss 124504 / heap 2560`（余 2352B）；`PROTO=CQ DISP=1_263` `.text 154492 / .rodata 195112 /
> .data 852 / .ccmram 37396 / .bss 121292`（余 6368B）；无显示等价口径 `.text 131928 / .rodata 191968 /
> .ccmram 16084 / .bss 115704 / heap 2560`（余 11224B）；产物落 `.analysis/myanmar/build_nodisp/`。
> 相对第一轮（RTT 移除态）差值：text **+464** / rodata **+40** / bss **+1208**（= `_acUpBuffer` 1024 +
> `_acDownBuffer` 16 + `_SEGGER_RTT` 168）/ data、ccmram **±0**；明细见 **§8.4.4** 与
> `.analysis/cleanup_20260929/round2_report.md`。
> 上一条采样（**2026-09-29 代码瘦身轮第一轮**：RTT 调试设施整体移除 + 注释精简；仅 3 条既有 HAL 告警）——
> `PROTO=ALL DISP=1_263 FACTORY=STD` **`.text 167288 / .rodata 195736 /
> .data 1656 / .bss 123560 / .ccmram 39652 / ._user_heap_stack 2560` ⇒ SRAM 合计 127776B（余 3296B）、
> CCM 余 25884B**；`FACTORY=MM` `.text 165752 / .rodata 195624 / .bss 123296 / heap 2560`；
> `PROTO=CQ DISP=1_263` `.text 154044 / .rodata 195072 / .data 852 / .ccmram 37396 / .bss 120084`；
> 无显示等价口径 `.text 131480 / .rodata 191928 / .ccmram 16084 / .bss 114496 / heap 2560`（余 13912B）。
> 相对瘦身前同口径差值：text **−9288** / rodata **−7936** / bss **−1300**（SEGGER RTT 的 1KB 上行缓冲
> 与诊断静态态）/ data **0**（CQ −4）/ ccmram **±0**；明细见 `.analysis/cleanup_20260929/report.md`。
> 上一条采样（**2026-09-18 第三十二轮**，22_1665 删除自设 16KB CCM 预算；工作区宏值 8×2 = 128×32，见 **§8.4.2**）——
> `PROTO=ALL DISP=22_1665`（8×2）**`.text 175248 / .rodata 203296 / .data 1656 / .bss 124836 / .ccmram 58876
> （本模组 23040 = 1408×16 + 256×2）/ ._user_heap_stack 2564` ⇒ SRAM 合计 129056B（余 2016B）、CCM 余 6660B**；
> 同轮 `PROTO=ALL DISP=1_263` 余 2016B、`PROTO=CQ DISP=1_263` 余 6296B。
> 再上一条采样（**2026-09-18 YN_OL TCP 现场修复轮**，见 **§8.4.1**）为
> `PROTO=ALL DISP=22_1665`（1×5）**`.text 174640 / .rodata 203152 / .data 1680 / .bss 124816 / .ccmram 44156 /
> ._user_heap_stack 2560` ⇒ SRAM 合计 129056B（余 2016B）**；`PROTO=ALL DISP=1_263` 余 2020B、`PROTO=CQ DISP=1_263` 余 6304B。
> 2026-09-14 全量重采见 §7；**1-263 现值已为 `MODULE_ROWS=7 × 32 / MODULE_COLS=2 × 32 = 224×64`（CCM 29568B）**，
> 本页 §0~§3、§5、§6 中按「1-263 = 224×128 / CCM 59136B」记账的数字为**历史口径**（`PROJECT_MANUAL.md` 同处标注 `[待更新: 需用户裁决]` 已由该轮落地解决），**两套数字严禁混用**。  
> **迁移**：[05_migration_plan.md](./05_migration_plan.md)（Phase A + 后续瘦身已执行）  
> **RB / queue 语义**：doc/05

**结论**：SRAM **≈120820B（92.2%）**；CCM **38208B（58.3%）**。`ucHeap` @ SRAM **36KB**；CCM 仅显存。全局约 159KB / 192KB，空闲约 **37.5KB**。

---

## §0 采样口径

| 项 | 值 |
|----|-----|
| 构建 | **EIDE Debug**（Arm GNU Toolchain 15.2 / GCC）；`outDir=build` → `build/Debug/Project_STD.elf`（对象在 `build/Debug/.obj/`）〔**2026-08-14 采样时口径**〕 |
| 模组（CCM） | 编入 **1-577 3×3**（`.eide/eide.yml` exclude 1-260 / 1-969）；CCM **38208B**，与 1-969 同量级 |
| 协议选编 | 编入 **IAP + LDI + 青海**；exclude **RLS / AH_MQTT** |
| 段合计 | `.data` 1612 + `.bss` 115620 + `._user_heap_stack` 3588 = **120820** |
| CCM | `.ccmram` **38208** |
| 结果 | **链接成功** |

与「仅 1-260 小屏」数字 **严禁混用**。**勿用其它口径重采**：本表数字来自当轮特定口径的构建产物（原 Makefile 现编 1-260 + RLS/AH，见 `03` §1），换口径覆盖 `build/Debug` 产物后本表数字不复现。〔2026-10-13：Makefile 已移除，构建仅 EIDE；本表为历史采样。〕

| 段 | 大小 |
|----|------|
| `.data` | 1612B |
| `.bss`（含 ucHeap 36KB） | 115620B |
| `._user_heap_stack` | 3588B |
| **SRAM≈** | **120820B（92.18%）**；空闲 **≈10252B** |
| `.ccmram` | **38208B（58.30%）**；空闲 **≈27328B** |

---

## §1 CCMRAM（仅显存）

| 对象（1-577 3×3 / 同量级 1-969） | 大小 |
|----------------------------------|------|
| pixel_map + hub75_buff + row_dst + g_bsrr | **38208** |
| **合计** | **38208** |

CCM 中 **0 字节** ucHeap / RB / UART DMA。

> ⚠ **例外（非显存对象）**：重庆 CQ 帧队列/缓冲 6377B（2026-08-20）与贵州治超协议
> 队列/任务帧缓冲 1136B（2026-09-14）为 CPU 独占访问的协议缓冲，因 SRAM 余量不足破例置
> CCM——明细见本页 §7 与文末修订。

---

## §2 SRAM

### 关键符号（nm，2026-08-14 16:37，EIDE Debug 产物）

| 对象 | 大小 | 说明 |
|------|------|------|
| `ucHeap` | **36864（36KB）** | FreeRTOS heap_4 @ SRAM |
| `_acUpBuffer` | 1024 | RTT Up（已 4→2KB，2026-09-04 再 2→1KB 回 SEGGER 默认——安徽入 SRAM 让位，doc/13 §6） |
| `rb_provide_rj45_buf` | **1536** | RJ45 RB |
| `rb_provide_rs485_buf` | **768** | RS485 RB |
| `rb_provide_rs232_buf` | **768** | RS232 RB |
| `s_rs485_buf` / `s_rs232_0_buf` | **1280** / **1280** | UART DMA 乒乓双缓冲（640×2）；无 RS232_1 |
| `s_iap_queue_buf` | **2104** | 深度 2 |
| `s_ldi_queue_buf` | **2080** | 深度 **4** |
| `s_qh_queue_buf` | **801** | 深度 **3** |
| ETH/LwIP 池 | ram_heap / RX_POOL / PBUF 等 | 永留 SRAM（大户） |
| LwIP memp 池（netconn/udp_pcb） | 8×netconn + 8×udp_pcb（2026-08-21 起 `lwipopts.h` 显式 `MEMP_NUM_NETCONN=8`/`MEMP_NUM_UDP_PCB=8`，原 opt.h 默认 4） | 实测 bss 增量 **304B**；**新增 UDP 端口协议时随通道数核算本行**（每通道常驻 1 netconn + 1 udp_pcb，广播复用常驻 conn 不占池） |

`s_rls_queue_buf`（**1076**，深度 2）与 AH_MQTT 队列（~1623，任务内 static）为**源码口径**，**不在本采样 elf 内**（EIDE Debug 排除 RLS/AH；仅 Makefile 构建时才计入 bss）。

### 协议 RB（SRAM）

| 槽 | 容量 | 可用（size−1） |
|----|------|----------------|
| RJ45 | 1536 | 1535 |
| RS485 | 768 | 767 |
| RS232 | 768 | 767 |

### 协议帧 queue（SRAM 静态；2026-08-14 变更）

| 协议 | 深度 | 缓冲约 | 变更记录 | 本采样 |
|------|------|--------|----------|--------|
| IAP | 2 | 2104 | 维持 | ✅ 编入 |
| LDI | 4 | 2080 | **2→4** | ✅ 编入 |
| 青海 | 3 | 801 | payload 259 + **深度 3** | ✅ 编入 |
| RLS | 2 | 1076 | 维持 | — EIDE 排除 |
| AH_MQTT | 3 | ~1623 | **1→3**（任务内；initcall 关则未激活路径） | — EIDE 排除 |
| 山东 | 3 | 801 | 新增（payload 259，与青海同构） | — EIDE 排除 |
| 贵州 | 3 | 801 | 新增（payload 259，与青海同构；13 命令含 0x01/0x02） | — EIDE 排除 |
| 云南 | 3 | 801 | 新增（payload 259，与青海/贵州同构；13 命令含 0x01/0x02） | — EIDE 排除 |
| 安徽 | 3 | 807 | 新增（payload 261 = 数据长 1B 上限；长度字段定界、CRC 文档注明不校验）；**队列体+cb+任务帧缓冲为静态 SRAM**（与青海/贵州/云南同，用户裁决 2026-09-04；+1156B SRAM = queue_buf 807 + cb 80 + 帧缓冲 269） | ✅ 编入（SRAM） |
| 重庆CQ | 3 | 3156 | 新增（payload 1044，JSON `{` 定界 + 12B 二进制）；**队列体+任务帧缓冲+JSON/文本缓冲置 CCMRAM**（SRAM 全协议构建仅余 ~1.9KB；CPU 独占访问无 DMA，见文末修订） | ✅ 编入（CCM） |
| 贵州治超 | 3 | 792 | 新增（"TCLY" 帧族，payload 256）；**队列体+cb+任务帧缓冲 1136B 置 CCMRAM**（ALL 口径 SRAM 余量仅 496B，CPU 独占访问无 DMA） | — EIDE 排除 |
| 云南治超 | 3 | 801 | 新增（YN_1.3.0 `{` 帧族，payload 255 = 参数区 1B 长度上限、帧 ≤259，与云南常规同构；'6'/'7'/'9' 文档明示治超屏不开发 → probe 快拒）；**队列体+cb+任务帧缓冲置 CCMRAM**（1152B = queue_buf 801 + cb 80 + 帧缓冲 267 + 对齐）；属 `{` 帧族 → 定义 `g_brace_proto_guard` 守卫，量产与其它 `{` 族目录二选一 | — EIDE 排除 |
| 新疆费显 XJ | 3 | 246 | 新增（协议文档「费额显示器通讯协议 2024年」，`FF DC` + 长度 = 整帧总长 + 命令 + BCC；payload 74 = 'T' 文本 4 行上限 10+4×16；9 命令最小集，`'D'`/彩色行不实现）；**队列体+cb+任务帧缓冲 + 任务栈/TCB 置 CCMRAM**（实测增量 1544B = queue_buf 246 + cb 80 + 帧缓冲 82 + 栈 1024 + TCB 100 + 对齐）；**只 acquire 不 provide RB**（RS485/RS232 槽体由既有协议提供）；SRAM 侧 32B（mask×2/句柄/心跳时刻/标志） | — EIDE 未收录（待用户裁决） |
| 广西费显 GX | 3 | 636 | 新增（`AA + addr + BB + 双字节命令 + data + FF`；payload 204 = 显示文本 192B + 头 11B + 结束码；其余命令定长 ≤103B）；**队列体+cb+任务帧缓冲 + 任务栈/TCB 置 CCMRAM**（**实测 2056B** = queue_buf 636 + cb 80 + 帧缓冲 212 + 栈 1024 + TCB 100 + 对齐 4；A/B 双构建实测 `.text +4824` / `.rodata +464` / `.data +8` / `.ccmram +2056` / `.bss +128`）；**自备 RS485/RS232 两槽 `RB_PROVIDE_WEAK` 槽体后再 acquire**（多协议口径 weak 合并、零额外 SRAM，单协议目标必需）；SRAM 侧 136B（bss 128 + data 8；mask×2/句柄/定时状态/字号字型/开机界面缓存）+ 自备槽体 1536B（多协议口径被同槽 weak 合并、不占）；2026-10-20 接入，详见 doc/21 §4 | — EIDE 基线排除（按需点选） |

> **重庆CQ 内存例外（2026-08-20）**：CQ 队列 `s_cq_queue_buf` 3156B + `s_cq_queue_cb` 80B +
> 任务帧缓冲 1052B + JSON 缓冲 1045B + 文本缓冲 1044B = **6377B 置 `.ccmram`**。
> 理由：全协议 dev 构建 SRAM 仅余 ~1.9KB，CQ 帧队列 3236B 无法入 SRAM；
> 这些缓冲为 CPU 独占访问（osMessageQueue 静态内存经 CPU memcpy 读写、无 DMA/ETH），
> CCMRAM 可用（Makefile 1-260 显存 2432B；EIDE 1-577 口径 38208B + 6377B 亦不超 64KB）。
> cJSON 解析树为 **FreeRTOS 堆瞬时分配**（钩子 pvPortMalloc/pvPortFree，ucHeap 36KB
> 内支出，解析后 `cJSON_Delete` 归还）。`cq_proto_handle_task`/`cq_proto_timer_task`
> 各 1KB 栈（ucHeap，地区协议处理任务 +1KB、定时任务 +1KB）。

> **安徽内存记账（2026-09-04 修订，用户裁决：与青海/贵州/云南同 SRAM）**：安徽队列
> `s_anhui_queue_buf` 807B + `s_anhui_queue_cb` 80B + 任务帧缓冲 269B + mask/句柄 12B
> = **~1168B 静态 SRAM**（`.bss`），无 `.ccmram` 对象。`anhui_handle_task` 1KB 栈（ucHeap）。
> SRAM 预算为此收紧三处（合计让出 2048B，详见 doc/13 §6）：链接脚本 `_Min_Heap_Size`
> 1KB→512B、`_Min_Stack_Size` 2.5KB→2KB（链接期下限，运行时 heap/stack 共享同一区）；
> RTT `BUFFER_SIZE_UP` 2KB→1KB（回 SEGGER 默认）。修复后 1-263 口径 PROTO=ALL：
> text 356500 / data 1656 / bss 194020（含 CCM 65516）；`.data`+`.bss`+`._user_heap_stack`
> ≈ 130160B（99.3%），余量 ~912B。PROTO=CQ：text 342636 / data 852 / bss 190544，均链接通过。

> **安徽动态显示增量账（2026-09-07，app_scroll 落地，Makefile 1-263 单模块 32×32 口径实测）**：
> 基线（本改动前同树构建）：PROTO=ALL text 356436 / data 1656 / bss 137380；
> 落地后：**PROTO=ALL text 358348（+1912）/ data 1656（+0）/ bss 137756（+376）**、
> **PROTO=CQ text 344508 / data 852 / bss 134280**，均 0 新增告警链接通过。
> 增量构成（nm 实测）：
> - 静态槽位：`s_slots[4]` **360B**（4×90B：控制块 26B + 文本 64B）+ 事件/互斥/任务句柄 **12B** = **372B .bss**；
> - 任务栈（ucHeap，非 bss）：`scroll_task` **1×1KB 惰性支出**（首次动态显示启动才创建）；
> - 字模缓冲为调用栈内 128B（`app_render_draw_glyph_clipped` 局部数组，不入静态）；
> - text +1912：app_scroll 模块（节拍/推进/渲染/引擎惰性创建）+ app_render 裁剪绘制导出 + 安徽 cmd 接线。
> SRAM 合计（`.data` 1656 + `.bss` 126316 + `._user_heap_stack` 2564）：**≈130536B（99.6%）**，
> 余量 ≈ 536B，未超上限。CCM 增量 **0**（8876B 不变）。

语义与 Put 丢帧行为 → **doc/05** `01` §4.1 / `02` §2.1。

`size` 的 bss 列含 `.ccmram`，勿直接当 SRAM。

---

## §3 数字链（相对 Phase A 刚完成时）

| 步骤 | SRAM 约 | CCM |
|------|---------|-----|
| Phase A 后（旧账，RB 仍 2304/512） | ~130280 / 99.4% | 38208 / 58.3% |
| + RB/DMA/queue 瘦身与修订（本采样） | **~120820 / 92.2%** | **38208 / 58.3%** |

主要释放：删 RS232_1 DMA、DMA 2048→640、RJ45 RB 2304→1536；RS485/232 RB 512→768 略增；LDI/QH queue 加深略增。**净效果 SRAM 明显留白**。

> 注（2026-08-18）：UART RX 已改乒乓双缓冲（s_rs485_buf/s_rs232_0_buf 640→1280×2），Makefile 口径实测 bss 130036（原 128724，+1312 = 双缓冲 1280 + 静态通道实例 32）、text 334256（+1184，框架/协议防御代码）。EIDE 口径总量以 EIDE 重采为准。

---

## §5 四川三协议增量账（Makefile 口径实测）

采样：**Makefile GCC Debug**（`make -B -j8`，2026-08-14；编 1-260 + 全协议含 RLS/AH）。
注意与 §0 口径不同（EIDE Debug 编 1-577 3×3 且排除 RLS/AH），两口径数字**严禁混用**。

| 段 | 基线（未编三协议） | 编入三协议后 | 增量 |
|----|--------------------|--------------|------|
| text | 319664 | **328280** | **+8616** |
| data | 1604 | **1620** | **+16** |
| bss（含 .ccmram 2432） | 123868 | **126396** | **+2528** |

SRAM 合计（`.data + .bss(不含 ccmram) + ._user_heap_stack`）：**≈125363B（95.7%）**，未超 124KB（126976B）上限。

增量构成（nm 实测）：
- 帧 queue 静态体：`s_sc_etc_queue_buf` **471**（2026-08-15 ETC payload 64→149，0x0D 定界上限对齐 9K1F212701） + `s_sc_mtc_queue_buf` 246 + `s_sc_ol_queue_buf` **789**（2026-08-17 治超 payload 249→255 = 长度字段上限 FF，消除 250~255 长帧队列截断越界读） = **1506**；`*_queue_cb` 3×80=240
- 任务内帧缓冲（static bss）：ETC 157 + MTC 82 + OL **263** = **502**
- 状态：`s_ol_lines` **200**（8 行 ×24B + `s_ol_line_len` 8B；2026-08-15 治超行数据改变长 ≤24B，原 8×16B=128B）、`s_etc_uptime/activity_s` 8、各 mask/句柄 36、杂项 ~20
- 任务栈（ucHeap，非 bss）：3 处理任务 = **3×1KB**（2026-08-17 ETC 心跳计时任务停用 -1KB，原 4×1KB；从 36KB ucHeap 支出）
- 数据段 +8：`s_mtc_color`/`s_mtc_font_size` 等初值静态；+8：`s_etc_color`/`s_ol_color`（显示颜色跟随通行灯状态）

CCM 增量 **0**（无新 CCM 对象）。

---

## §6 山东协议增量账（Makefile 口径实测）

采样：**Makefile GCC Debug**（`make -j8`，2026-08-17；编 1-260 + 全协议含 RLS/AH + 山东）。
基线为 §5 末行口径（text 327408 / data 1620 / bss 126396），两次数值精确衔接（增量 = 山东模块贡献）。

| 段 | 基线（§5 末行口径） | 编入山东后 | 增量 |
|----|---------------------|------------|------|
| text | 327408 | **328872** | **+1464** |
| data | 1620 | **1620** | **+0** |
| bss（size 工具口径，含 .ccmram 2432） | 126396 | **127556** | **+1160** |

SRAM 合计（相对 §5 基线增量 +1160）：**≈126523B（96.5%）**，未超 124KB（126976B）上限。

增量构成（nm 实测）：
- 帧 queue 静态体：`s_sd_queue_buf` **801**（3×267，payload 259）+ `s_sd_queue_cb` **80** = 881
- 任务内帧缓冲（static bss）：**267**
- 状态：`s_sd_mask`/`s_sd_mask_rs232`/队列句柄 **12**
- 任务栈（ucHeap，非 bss）：`sd_handle_task` **1×1KB**（地区协议处理任务 3→4）
- text +1464：probe/parse/cmd/注册代码 + 版本串 rodata 10B

CCM 增量 **0**（无新 CCM 对象）。

2026-08-17（默认显示注册机制 + 版本应答 PROGRAM_CODE）追加采样（同口径 `make -B -j8`）：

| 段 | 山东基线 | 本任务后 | 增量 |
|----|----------|----------|------|
| text | 328872 | **329112** | **+240** |
| data | 1620 | **1620** | **+0** |
| bss（size 工具口径，含 .ccmram 2432） | 127556 | **127564** | **+8** |

增量构成（nm 实测）：
- `s_default_fn`（`app_default_display.c` 注册槽）**4B bss** + 链接尾部对齐 4B → .bss 净 **+8**（.ccmram 2432 / ._user_heap_stack 3588 不变）
- 新增 `s_sd_default_text` 22B rodata + `_sd_default_show` / `app_default_display_register` / 回退渲染代码 + initcall 条目 8B + 名称字符串 + PROGRAM_CODE 在 `app_sd_proto_cmd.c` 的 rodata 副本；删除 `s_sd_version_text` 10B → text 净 **+240**

SRAM 合计（§5 基线 +1160 +8）：**≈126531B（96.5%）**，未超 124KB（126976B）上限。
CCM 增量 **0**（无新 CCM 对象）。

2026-08-17（文本常量字节数组 → UTF-8 字符串字面量 + 山东上电画面改黄字）追加采样（同口径 `make -B -j8`）：

| 段 | 山东基线 | 本任务后 | 增量 |
|----|----------|----------|------|
| text | 329112 | **329456** | **+344** |
| data | 1620 | **1620** | **+0** |
| bss（size 工具口径，含 .ccmram 2432） | 127564 | **127564** | **+0** |

增量构成（nm 实测）：
- rodata（GBK 字节数组 → UTF-8 字面量，汉字 2B→3B）：`s_sd_default_text` **22→33B（+11，0x21）**；MTC 固定标签 7 个 **+24**（34→58）；MTC 初始化「祝您一路平安」**+7**、自检「系统正在加电自检」**+9**；MTC 固定语音表（`s_mtc_fixed_voices` 指针表 32B + 8 串 206B）**+58**；青海文明用语表（`s_civil_texts` 指针表 16B + 4 串 130B）**+40**（'0' 同时修正为 doc/04 裸机文本「贵州高速公路」，原数组字节有损）→ 合计 **+149**
- code（约 **+195**）：MTC/QH 语音播报前 UTF8ToGBK 运行时转换调用点（`UTF8ToGBK` 本体早已随 `app_render` 链接，无新增）；MTC 初始化/自检的静态 GBK 数组改为栈缓冲 + 运行时转换；`_sc_mtc_render_center` 改 `FONT_ENC_UTF8`
- `s_sc_etc_lane_closed_text`（ETC「车道关闭」23B→34B）：调用方已停用，符号被 --gc-sections 丢弃，**0 增量**
- bss 无变化：转换缓冲全部为函数内栈/局部，未引入静态存储

SRAM 合计不变：**≈126531B（96.5%）**，未超上限。
CCM 增量 **0**。

2026-08-17（dev_display_fill 越界下溢修复）追加采样（同口径 `make -j8`，插桩已清理）：

| 段 | 山东基线 | 本任务后 | 增量 |
|----|----------|----------|------|
| text | 329456 | **329472** | **+16** |
| data | 1620 | **1620** | **+0** |
| bss（size 工具口径，含 .ccmram 2432） | 127564 | **127564** | **+0** |

增量构成：`dev_display_fill` 起点越界早退分支（`x >= screen_rows || y >= screen_cols` 丢弃）≈ +16B code；无新增静态存储。SRAM / CCM 不变。

2026-08-17（贵州协议接入）追加采样（同口径 `make -j8`）：

| 段 | 贵州基线 | 本任务后 | 增量 |
|----|----------|----------|------|
| text | 329472 | **333072** | **+3600** |
| data | 1620 | **1620** | **+0** |
| bss（size 工具口径，含 .ccmram 2432） | 127564 | **128724** | **+1160** |

增量构成（nm 实测）：
- 帧 queue 静态体：`s_gz_queue_buf` **801**（3×267，payload 259）+ `s_gz_queue_cb` **80** = 881
- 任务内帧缓冲（static bss）：`msg_buf` **267**
- 状态：`s_gz_mask`/`s_gz_mask_rs232`/队列句柄 **12**
- 任务栈（ucHeap，非 bss）：`gz_handle_task` **1×1KB**（地区协议处理任务 4→5）
- text +3600：probe/parse/cmd/voice 四文件代码（`gz_parse_frame` 0x20C、`_gz_exec_fixed` 0x268、`gz_execute_cmd` 0xAC 等）+ rodata（文明用语 4 串 UTF-8、颜色/亮度/音量映射表、队列/任务 attr）

SRAM 合计（贵州基线 +1160）：**≈127691B（97.4%）**，未超 124KB（126976B）上限。
CCM 增量 **0**（无新 CCM 对象）。

2026-08-24（云南协议接入）追加采样（同口径 `make clean && make -j8`，对照构建同树剔除云南实测增量；数字为同日用户决定 1~10 修订后重采）：

| 段 | 云南基线（剔除云南） | 修订后 | 增量 |
|----|----------------------|----------|------|
| text | 349796 | **352860** | **+3064** |
| data | 1656 | **1656** | **+0** |
| bss（size 工具口径，含 .ccmram） | 136772 | **138196** | **+1424** |

增量构成（对照构建实测；初版 +3120/+1168，修订后 自检单字缓冲 256B 入 bss、default 文件代码移除）：
- 帧 queue 静态体：`s_yn_queue_buf` **801**（3×267，payload 259）+ `s_yn_queue_cb` **80** = 881
- 任务内帧缓冲（static bss）：`msg_buf` **267**
- 自检单字全屏缓冲（static bss）：`_yn_selftest_fill_char` 内 `buf` **256**
- 状态：`s_yn_mask`/`s_yn_mask_rs232`/队列句柄 **12** + 尾部对齐 8
- 任务栈（ucHeap，非 bss）：`yn_handle_task` **1×1KB**（地区协议处理任务 5→6）；`yn_selftest_task`（1KB）为瞬态任务（上电熄灭任务 `yn_bootoff_task` 已随修订移除）
- text +3064：probe/parse/cmd/voice 四文件代码（含自检老化循环显示序列）+ rodata（文明用语 4 串、PROGRAM_CODE 应答、映射表、队列/任务 attr）

SRAM 合计（云南基线 +1424）：**≈129115B（98.5%）**，未超 124KB（126976B）上限。
CCM 增量 **0**（无新 CCM 对象）。

---

## §7 2026-09-14 全量重采（22-1703 模组 + 贵州治超协议接入，Makefile GCC Debug 口径）

**采样背景**：本轮新增 ① `Device/Display/dev_display_22_1703.c`（P10 32x16 模组，
料号 2200001703，与 1-263 编译期二选一，`DISP` 开关）；② `ProtocolParser_GuiZhou_Overload`
（贵州治超 "TCLY" 帧族，队列置 CCMRAM）。**采样口径为 Makefile GCC Debug**（`arm-none-eabi-size -A`，
`build/Debug/Project_STD.elf`）。**重链接依赖（2026-09-14 修复）**：口径/源列表切换由
Makefile 口径指纹 stamp 保证「全部 `.o` 重编译 + elf 重链接」，**不再需要手工删产物**
（机制见 `doc/构建开关总表.md` §4）；下表 `PROTO=ALL` 两行为修复后干净全量重采值，
原记录的 text/rodata 系污染态采样，更正说明见表下注。

| 口径 | text | rodata | data | **ccmram** | bss | `._user_heap_stack` | SRAM 合计 | SRAM 余量 | CCM 余量 |
|---|---|---|---|---|---|---|---|---|---|
| 改动前基线 `PROTO=ALL`（1-263） | 161524 | 197544 | 1664 | 35948 | 126332 | 2564 | 130560（99.6%） | 512B | 29588B |
| 改动前基线 `PROTO=CQ`（1-263） | 148332 | 196888 | 860 | 35948 | 122856 | 2564 | 126280 | 4792B | 29588B |
| **`PROTO=ALL` `DISP=1_263`** | **163652** | 197760 | 1664 | **37084** | 126348 | 2564 | **130576（99.6%）** | **496B** | 28452B |
| **`PROTO=CQ` `DISP=1_263`** | **150476** | 197104 | 860 | **37084** | 122872 | 2564 | 126296 | 4776B | 28452B |
| **`PROTO=ALL` `DISP=22_1703`** | **163924** | 197840 | 1744 | **39164** | 126348 | 2564 | **130656（99.7%）** | **416B** | 26372B |
| **`PROTO=CQ` `DISP=22_1703`** | **150732** | 197192 | 940 | **39164** | 122872 | 2564 | 126376 | 4696B | 26372B |

**更正（2026-09-14，口径指纹 stamp 修复后干净重采）**：`PROTO=ALL` 两行原记录
text/rodata 为 **164068 / 197800**（1-263）与 **164324 / 197880**（22_1703）——系
「删 elf 后 `make`、对象沿用上一 CQ 口径」的**污染态**（混入 CQ 口径对象后
`CQ_FAULT_SCREEN=1` 故障屏引用链被 `--gc-sections` 保留，+416 text / +40 rodata；
符号级归因见 `doc/构建开关总表.md` §4.1）。**RAM 各段（data / ccmram / bss / SRAM
合计 / 余量）不受影响**，故内存账目与结论不变；仅 text/rodata 与由 text 派生的增量
按上表更正。`PROTO=CQ` 两行与「改动前基线」两行经干净重采逐项复现，无需修正。

四口径**全部链接通过、零新增告警**（`-Wall -Wextra`）。

**CCMRAM 构成（实测符号）**：

| 对象 | DISP=1_263 | DISP=22_1703 |
|---|---|---|
| 模组显存 + row_dst + 查表 | `_1_263_pixel_map` 14336 + `_1_263_hub75_buff` 14336 + `_1_263_row_dst` 128 + `g_bsrr` 768 = **29568** | `_22_1703_pixel_map` 14336 + `_22_1703_hub75_buff` 14336 + `_22_1703_row_dst` 128 + 合并表 `tab_g/b/e/c/f` **2848** = **31648** |
| CQ 例外（2026-08-20） | `s_cq_queue_buf` 3156 + cb 80 + 任务帧缓冲 1052 + JSON 1045 + 文本 1044 = **6377** | 同 |
| 贵州治超协议（2026-09-14，CCM 例外 2） | `s_gz_ol_queue_buf` 792 + `s_gz_ol_queue_cb` 80 + `msg_buf` 264 = **1136**（**SRAM 无法容纳**：ALL 口径余量仅 496B；CPU 独占访问无 DMA/ETH，CQ 先例） | 同 |
| 段对齐 | 3 | 3 |
| **合计** | **37084** | **39164** |

**本轮增量账（相对改动前同口径基线）**：

| 项 | 增量 | 构成（nm 实测） |
|---|---|---|
| 贵州治超协议（text） | **+2128**（`PROTO=CQ` +2144） | probe 220 + parse 350 + cmd 约 600 + default 约 178 + 注册/任务 + initcall 条目 + 字符串常量（弱 RB 提供体与既有协议合并，不重复计入）。干净 A/B：ALL 161524→163652、CQ 148332→150476（**原记 +2544 为污染态采样**，多出的 +416 来自混入的 CQ 口径对象） |
| 贵州治超协议（rodata） | **+216** | 上电三行文案 + 队列/任务 attr + 版本/提示字符串（**原记 +256** 含污染态 +40） |
| 贵州治超协议（bss / ccmram） | **+16 / +1136** | SRAM：mask×2 + 队列句柄 + 状态 = 13B（+3 对齐）；CCM：1136B（上表） |
| 22-1703 模组（相对 1-263，`PROTO=ALL`） | text **+272** / rodata **+80** / data **+80** / ccmram **+2080** | 干净 A/B：163652→163924（**原记 +256 为污染态采样**）；data：`g_22_1703` 48 + `_22_1703_grps[]` 80 → +80（相对 `g_1_263` 48）；CCM：合并表 2848 − 1-263 `g_bsrr` 768 = +2080 |
| 任务栈 | ucHeap 支出 | `gz_ol_handle_task` 1×1KB（36KB ucHeap 内，不计入 bss） |

**禁止事项**：`DISP=1_263` 与 `DISP=22_1703` 的 `.ccmram` 数字**严禁混用**（差 2080B）；
1-263 的「224×128 / 59136B」历史口径同样严禁与本表混用。

### §7.1 2026-09-14（后续）贵州治超增绑 UDP 10011 重采

**变更**：`gz_ol_proto_init` 增第三个 mask 绑 `CH_ID_UDP`（10011，挂 RJ45 共享槽 RB；
**只 acquire 不 provide**，网口槽 1536B 仍由 IAP/LDI/CQ 提供）。重采
（Makefile GCC Debug，`arm-none-eabi-size -A`；口径指纹 stamp 保证切口径全量重编 + 重链接）：

| 口径 | text | rodata | data | ccmram | bss | heap_stack | SRAM 合计 | SRAM 余量 |
|---|---|---|---|---|---|---|---|---|
| `PROTO=ALL` `DISP=1_263` | 163716 | 197760 | 1664 | 37084 | 126352 | 2560 | 130576（99.6%） | **496B** |
| `PROTO=CQ` `DISP=1_263` | 150524 | 197104 | 860 | 37084 | 122876 | 2560 | 126296 | 4776B |
| `PROTO=ALL` `DISP=22_1703` | 163940 | 197840 | 1744 | **11420** ⚠ | 126352 | 2560 | 130656 | 416B |
| `PROTO=CQ` `DISP=22_1703` | 150764 | 197192 | 940 | **11420** ⚠ | 122876 | 2560 | 126376 | 4696B |

- **本模块增量（干净 A/B，1_263 两行）**：text **+64/+48**、bss **+4**（多一个
  `s_gz_ol_mask_udp`），rodata/data/ccmram **+0**；**SRAM 合计与余量逐字节不变**
  （bss +4 由 `._user_heap_stack` 对齐空隙 −4 吸收）。**无新增 RB 缓冲**。
- ⚠ 22_1703 两行的 `ccmram` 与上表历史值（39164）不可比**（本节采样时该驱动处于 1×1 实验态）**：
  另一路的 `Device/Display/dev_display_22_1703.c` 改动（mtime 15:26，当时）把
  `_22_1703_pixel_map`/`_22_1703_hub75_buff` 缩为各 512B（`nm` 实测 0x200，该模组在
  `.ccmram` 的贡献 = 3904B，map 实测 `dev_display_22_1703.o .ccmram = 0xf40`）。差值
  39164 − 11420 = **27744 = 31648 − 3904** 恰好全部来自该驱动改动，与 UDP 增绑无关。
  **该驱动现已回到 7×4 = 224×64（ccmram 39164），最新四口径复采见 §7.2**；两态差值
  27744B 可用于反推板上镜像几何。
- 结论：**SRAM/CCM 余量仍为正**（496B / 4776B / 416B / 4696B），四口径全部链接通过、
  零新增告警。默认口径（`PROTO=ALL` `DISP=1_263`）elf md5 `ef0eeaa2ede395242990ec8b49b7acd0`
  （该 md5 仅在**最后一次构建确为 make** 时有效——EIDE 覆写共享产物的构建卫生纪律见
  `doc/构建开关总表.md` §4.2）。

### §7.2 2026-09-14（后续）GZ_OL 专用 UDP 业务口（`CH_ID_UDP_GZOL`）

**变更**：新增贵州治超专用 UDP 通道实例（`app_udp.c` `udp_gzol_task` /
`udp_gzol_connect_task`，镜像 CQ 实例；端口 = Sector1 `net_cfg.port`，与 TCP 业务口同号
不同协议栈）+ `CH_ID_MAX 8→9` + `app_boot.c` 在其后 `app_udp_gzol_start()`；
`gz_ol_proto_init` 增第四个 mask 绑 `CH_ID_UDP_GZOL`（同挂 RJ45 槽 RB，仍**只 acquire
不 provide**）。**10011 绑定保留**，两通道并存。**重采（2026-09-14 复测轮，Makefile GCC Debug，`arm-none-eabi-size -A`；口径指纹 stamp 保证
全量重编 + 重链接；**含默认开启的 `APP_DIAG_BANNER` 诊断**，与下节「诊断增量」配套阅读）**：

| 口径 | text | rodata | data | ccmram | bss | heap_stack | SRAM 合计 | SRAM 余量 | CCM 余量 |
|---|---|---|---|---|---|---|---|---|---|
| `PROTO=ALL` `DISP=1_263` | 167236 | 201032 | 1672 | 37084 | 126400 | 2560 | **130632（99.7%）** | **440B** | 28452B |
| `PROTO=CQ` `DISP=1_263` | 154060 | 200384 | 872 | 37084 | 122924 | 2564 | 126360 | 4712B | 28452B |
| `PROTO=ALL` `DISP=22_1703` | 167492 | 201120 | 1752 | **39164** | 126400 | 2560 | **130712（99.7%）** | **360B** | 26372B |
| `PROTO=CQ` `DISP=22_1703` | 154316 | 200464 | 952 | **39164** | 122924 | 2564 | 126440 | 4632B | 26372B |

**诊断增量（A/B，同一棵树，默认口径 `make -j8` vs `make -j8 APP_DIAG=0`）**：

| 项 | 诊断 ON | 诊断 OFF | 增量 |
|---|---|---|---|
| text | 167236 | 164596 | **+2640** |
| rodata | 201032 | 198056 | **+2976** |
| data / ccmram / bss | 1672 / 37084 / 126400 | 1672 / 37084 / 126400 | **0 / 0 / 0** |

→ **Flash 合计 +5616B，RAM +0B**（诊断全为只读打印，无新增静态对象；复测轮 ② 在诊断内加写：
0x40 原始载荷 dump / 横幅 `expect gzol` 字段 / 启动 `netcfg INVALID` 告警，共 +176 text / +320 rodata；
**诊断关闭时与加写前逐字节同尺寸**）。唯一行为差异 =
开机默认画面延后 2.5s（`APP_DIAG_BANNER` 块内 `osDelay(2500)`）。**诊断一键关**：
`make APP_DIAG=0`（或 `app_diag.h` 宏改 0），关闭后固件行为与「从未加过诊断」等价。

- **本轮增量（干净 A/B，相对上一节同口径诊断态）**：text **+752 / +752**、rodata
  **+296 / +296**、data **+8 / +12**、bss **+40 / +40**、ccmram **+0**；SRAM 余量
  496→**448B**（ALL）、4776→4720B（CQ）。构成（`nm -S` 实测）：`udp_gzol_task` 292B +
  `udp_gzol_connect_task` 184B + `udp_gzol_ch_send` 66B + `udp_gzol_link_listener` 20B +
  端口读取/接线 + 常量；bss 40B = `s_udp_gzol_ch` 20B + `s_udp_gzol_warned_port` 2B +
  `udp_gzol_disconnect_sem` 4B + `s_gz_ol_mask_gzol` 4B + `ch_proto_map[]`/`channels[]`
  各 +4B（`CH_ID_MAX 8→9`）+ 对齐；data 8B = `g_udp_gzol_port` + 通道模板。
  **无新增 RB 缓冲、无新增 CCM 对象**。
- **LwIP 池（doc/06 纪律）**：`MEMP_NUM_NETCONN`/`MEMP_NUM_UDP_PCB` **维持 8/8**——
  常驻 netconn 5（UDP 10011 + UDP 20103 + UDP `net_cfg.port` + TCP listener + TCP client），
  叠加已连接客户端 6，广播回退临时 conn 峰值 8 = 池容量（UDP_PCB 常驻 3/8）。
  净增 1 常驻 netconn + 1 udp_pcb **取自既有池，无静态 bss 变化**。
- **任务栈（ucHeap，非 bss）**：`udp_gzol_task` + `udp_gzol_connect_task` 各 1×1KB
  → **+2KB**（36KB ucHeap 内）。堆水位尚未标定（doc/05-03 §4 待办）→ 现场须用
  `xPortGetMinimumEverFreeHeapSize()` 核对（步骤见 GZ_OL 交付报告 §4/§7）。
- **`DISP=22_1703` 的 `ccmram = 39164B` = 该文件当前宏（7×4 = 224×64）态**：31648（显存+表）
  + 6377（CQ 例外）+ 1136（贵州治超队列）+ 3（对齐）。**该文件属用户实验态**——宏改成 1×1
  （32×16）时 ccmram = 11420B（差值 **27744B**），可用于**反推板上镜像的屏体几何**
  （判据：`size -A` 的 `.ccmram` + `nm | grep dev_display`）。
- 默认口径 elf md5（本轮最终构建，诊断 ON）：**`57092a95337eb32d8b49e9583bd366a7`**（hex
  `1caefff99d86051d4aba1f661e2c18ec`；重复 `make` 0 编译 0 链接、md5 不变）。
  **注意**：横幅内嵌 `__DATE__/__TIME__`（`app_boot.c`），**重新编译必然改 md5**——
  同口径两次构建 md5 不同属预期，镜像身份以 RTT 横幅 `fw=`/`built=`/`tree=` 为准。

- **本轮增量（干净 A/B，相对上一节同口径诊断态）**：text **+752 / +752**、rodata
  **+296 / +296**、data **+8 / +12**、bss **+40 / +40**、ccmram **+0**；SRAM 余量
  496→**448B**（ALL）、4776→4720B（CQ）。构成（`nm -S` 实测）：`udp_gzol_task` 292B +
  `udp_gzol_connect_task` 184B + `udp_gzol_ch_send` 66B + `udp_gzol_link_listener` 20B +
  端口读取/接线 + 常量；bss 40B = `s_udp_gzol_ch` 20B + `s_udp_gzol_warned_port` 2B +
  `udp_gzol_disconnect_sem` 4B + `s_gz_ol_mask_gzol` 4B + `ch_proto_map[]`/`channels[]`
  各 +4B（`CH_ID_MAX 8→9`）+ 对齐；data 8B = `g_udp_gzol_port` + 通道模板。
  **无新增 RB 缓冲、无新增 CCM 对象**。
- **LwIP 池（doc/06 纪律）**：`MEMP_NUM_NETCONN`/`MEMP_NUM_UDP_PCB` **维持 8/8**——
  常驻 netconn 5（UDP 10011 + UDP 20103 + UDP `net_cfg.port` + TCP listener + TCP client），
  叠加已连接客户端 6，广播回退临时 conn 峰值 8 = 池容量（UDP_PCB 常驻 3/8）。
  净增 1 常驻 netconn + 1 udp_pcb **取自既有池，无静态 bss 变化**。
- **任务栈（ucHeap，非 bss）**：`udp_gzol_task` + `udp_gzol_connect_task` 各 1×1KB
  → **+2KB**（36KB ucHeap 内）。堆水位尚未标定（doc/05-03 §4 待办）→ 现场须用
  `xPortGetMinimumEverFreeHeapSize()` 核对（步骤见 GZ_OL 交付报告 §4/§7）。
- **22_1703 两行 `ccmram = 39164`**：即该驱动文件当前宏（7×4 = 224×64）态，不再是 `11420`
  （1×1 = 32×16 实验态）——**该文件属用户 WIP，宏一变本行就变**，差值 27744B 可作为
  「板上镜像几何」的判据（§7.2 表下注）。
- 默认口径 elf md5（本轮最终构建，诊断 ON）：**`57092a95337eb32d8b49e9583bd366a7`**（hex
  `1caefff99d86051d4aba1f661e2c18ec`；重复 `make` 0 编译 0 链接、md5 不变）。
  ⚠ 横幅内嵌 `__DATE__/__TIME__` → **重新编译必然改 md5**（镜像身份看 RTT 横幅而非 md5）。

---

## §4 复现

**前提**：`build/Debug/Project_STD.elf` 须为 **EIDE Debug** 产物。若先执行 `make`（编 1-260 + RLS/AH）会覆盖该 elf，本表数字不复现。

```bash
arm-none-eabi-size -A build/Debug/Project_STD.elf | grep -E '^\.(data|bss|ccmram|_user)'
arm-none-eabi-nm -S build/Debug/Project_STD.elf | grep -E 'ucHeap|rb_provide_.*_buf|s_.*_queue_buf|s_rs485_buf|s_rs232'
# ucHeap 须在 0x2000…，勿在 0x1000…
```

---

---

## §8 FreeRTOS 堆预算表与任务栈 CCMRAM 下沉（2026-09-17，现场堆耗尽修复）

> **角色**：`ucHeap`（`configTOTAL_HEAP_SIZE = 36KB`）的**永久预算账**。
> 任何新增任务 / 动态队列 / 信号量 / netconn **之前**先看本表，按 §8.3 逐项累加复算。
> 〔2026-10-13：原复算脚本 `.analysis/heap/heap_ledger.py` / `verify_builds.sh` 已随 `.analysis/`
> 目录移除；需要时用 `git show 069be96:<路径>` 取回（在仓库外运行）。〕

### §8.1 现场现象与根因（口径：**用量超限**，非碎片、非泄漏）

**现象**：调试器停在 `vApplicationMallocFailedHook`，调用栈
`pvPortMalloc ← xTaskCreate ← osThreadNew ← app_rs485_start ← init_task`。

**根因（数字链）**：全协议 dev 构建启动期「任务栈 + TCB + 动态 OS 对象」需求 **38032B**
> 初始可用 **36856B**（`configTOTAL_HEAP_SIZE` 36864 − `prvHeapInit()` 的 8B 块头）
⇒ **赤字 1176B**；堆在启动序列的 `app_rs485_start` 处**恰好**耗尽。

**精确到「同一启动点」**（离线账目复现，与现场一致）：

| 项 | 修复前 | 修复后 |
|---|---|---|
| 创建 `rs485_task` **之前**的余量 | **1112B** | **23480B** |
| `rs485_task`（1KB 栈 + TCB）需求 | 1144B | 0（栈/TCB 已落 CCMRAM） |
| 结果 | 1112 < 1144 ⇒ `pvPortMalloc` 返回 NULL ⇒ Hook | 不再申请堆 |

**为什么判定「用量超限」而不是碎片 / 泄漏**：

1. **碎片**：heap_4 在「启动期只有分配、没有释放」的序列里等价于单块首次适配，
   离线模型（`首地址连续 + 每次分配 = align8(请求+8)`）能把失败点**算到与现场同一处**
   （差 32B 即翻车）——若真实原因是碎片，模型不可能对上；
2. **泄漏 / 重复创建**：启动期全部创建点都是**一次性**（`sw_initcall` 各协议一处、
   通道启动各一处），循环内无重复创建；唯一的重复创建是**每连接任务**
   （`udp_connect_task` / `udp_cq_connect_task` / `udp_gzol_connect_task` /
   `tcp_client_conn_task`），每个都以 `osThreadExit()`（= `vTaskDelete(NULL)`）结束，
   FreeRTOS 会把栈与 TCB 归还 ucHeap；`scroll_task` / 自检任务是**惰性且只建一次**；
3. **最后一份压迫者**（A/B，`heap_ledger.py --ab`）：**带**云南治超处理任务时死在
   `rs485_task`，**去掉**它后临界点后移到 `rs232_task`——恰好一个 1KB 任务位（1144B）
   ⇒ 新接入的 YN_OL 处理任务就是压垮 ucHeap 的最后一份。

### §8.2 修复：任务「栈 + TCB」静态下沉 CCMRAM

**手段**：`Platform/Inc/pl_task_static.h` 的 `PL_TASK_STATIC_STORAGE(tag, words)` /
`PL_TASK_STATIC_ATTR(tag, words)` 把栈与 `StaticTask_t` 放进 `.ccmram`（`aligned(8)`），
`osThreadNew` 看到 `cb_mem + stack_mem` 即走 `xTaskCreateStatic`，**完全不碰 ucHeap**
（`configSUPPORT_STATIC_ALLOCATION = 1`）。

**为什么 CCMRAM 合法**：CCM 不可被 DMA/ETH 访问，而任务栈与控制块**只被 CPU 读写**
（上下文切换、函数栈帧），与既有队列体（CQ / GZ_OL / YN_OL）同源理由；`.ccmram` 是
`NOLOAD` 且由 `Compiler/startup.c` 整体清零，满足 FreeRTOS「静态 TCB 缓冲区须已清零」；
`configCHECK_FOR_STACK_OVERFLOW=2` 与 `uxTaskGetStackHighWaterMark` 对静态栈同样有效
（`prvInitialiseNewTask` 对静态栈同样填 0xA5 模式）。

**下沉清单（23 个任务，`nm` 实证全部落在 `0x1000_xxxx`）**：

| 任务 | 栈 | 创建点 | 释放 ucHeap |
|---|---|---|---|
| `scan_task` | 1KB | `dev_display.c`（sw2） | 1144B |
| `factory_monitor_task`（标准版）/ **`mm_factory_monitor_task`（缅甸版，`FACTORY=MM`，二选一）** | 1KB | `app_factory_test.c` / `app_factory_test_mm.c`（sw3） | 1144B |
| `frame_dispatch_task` | 1KB | `app_dispatch.c`（sw3） | 1144B |
| `scroll_task` | 1KB | `app_scroll.c`（惰性，首个滚动帧） | 1144B |
| `light_sensor_task` | 512B | `app_light_sensor.c`（sw3） | 632B |
| `iap_handle_task` | 1KB | `app_iap.c`（sw3） | 1144B |
| `ldi_handle_task` / `ldi_timer_task` | 1KB ×2 | `app_ldi.c`（sw3） | 2288B |
| `cq_proto_handle_task` / `cq_proto_timer_task` | 1KB ×2 | `app_cq_proto.c`（sw3） | 2288B |
| `rls_handle_task` | 1KB | `app_rls.c`（sw3） | 1144B |
| `qh_proto_handle_task` | 1KB | `app_qh_proto.c`（sw3） | 1144B |
| `sc_etc_proto_handle_task` | 1KB | `app_sc_etc_proto.c`（sw3） | 1144B |
| `sc_mtc_proto_handle_task` | 1KB | `app_sc_mtc_proto.c`（sw3） | 1144B |
| `sc_ol_proto_handle_task` | 1KB | `app_sc_ol_proto.c`（sw3） | 1144B |
| `sd_proto_handle_task` | 1KB | `app_sd_proto.c`（sw3） | 1144B |
| `gz_proto_handle_task` | 1KB | `app_gz_proto.c`（sw3） | 1144B |
| `gz_ol_proto_handle_task` | 1KB | `app_gz_ol_proto.c`（sw3） | 1144B |
| `yn_proto_handle_task` | 1KB | `app_yn_proto.c`（sw3） | 1144B |
| `yn_ol_proto_handle_task` | 1KB | `app_yn_ol_proto.c`（sw3，2026-09-17 新接入） | 1144B |
| `anhui_proto_handle_task` | 1KB | `app_anhui_proto.c`（sw3） | 1144B |
| `rs485_task` | 1KB | `app_rs485.c`（chan，现场失败点） | 1144B |
| `rs232_task` | 1KB | `app_rs232.c`（chan） | 1144B |
| **合计** | — | 23 个任务 | **−25800B ucHeap** |

**另外两处（内核静态任务）**：`configSUPPORT_STATIC_ALLOCATION=1` 时 FreeRTOS 的空闲任务与
定时器服务任务走 `vApplicationGetIdleTaskMemory` / `vApplicationGetTimerTaskMemory`，
`cmsis_os2.c` 的 `__WEAK` 版把缓冲放在自己的 `.bss`（SRAM 1736B：Idle_TCB 100 + Idle_Stack 512 +
Timer_TCB 100 + Timer_Stack 1024；`nm` 实测落在 `0x20007e30` / `0x200079cc` 等 SRAM 地址）；
`app_boot.c` 以**强定义覆盖**，改置 `.ccmram`（`s_idle_task_stack/tcb` 612B + `s_timer_task_stack/tcb` 1124B）
⇒ **SRAM `.bss` −1732B**（槽位对齐后），这是**本轮到 SRAM 侧的唯一净收益**。
CCM 侧合法性与任务栈同源（只被 CPU 读写、不经 DMA；`.ccmram` 由 `startup.c` 整体清零，
满足「静态 TCB 缓冲须已清零」）；调用时机 = `vTaskStartScheduler()` 内（先空闲任务、后定时器任务），
早于 `init_task`。驱动 `app_boot.c` 只留一行指针（长注释已本轮外移到本节）。

⚠ **仍未下沉的部分**：`timers.c` 内的定时器队列（`xStaticTimerQueue` + `ucStaticTimerQueueStorage`，
实测 0x50+0xA0 = 240B）是 timers.c 的 static 局部，无法从外部改置；若将来需要再挤 SRAM，
可评估 `configUSE_TIMERS=0`（全项目无 `osTimerNew`/`xTimerCreate` 调用点）。

**CCM 侧成本核算**：23 任务 = 22×1024 + 512（栈）+ 23×100（`StaticTask_t`）= **25340B**；
加空闲/定时器 **1736B** = **27076B**；`.ccmram` 全片实测见 §8.4（ALL/1_263 **38332B / 65536B**，余 **27204B**）。
`.ccmram` 上限约束：**2026-10-13 起驱动与全项目业务代码均无编译期断言**（22_1665 的 17 条 `_Static_assert`
第三十四轮全删、2026-10-08 恢复 2 条、2026-10-13 按用户裁决再删；见 §8.4.3 与 §8.4.7），
**只剩链接期溢出兜底**（`region .ccmram overflowed by N bytes`）；**新增下沉前先看剩余量**。

### §8.3 修复后启动期堆预算表（PROTO=ALL 全协议 dev 构建，单位 B）

口径：`configTOTAL_HEAP_SIZE=36864`，初始可用 **36856**；单次分配代价 = `align8(请求 + 8)`；
`TCB_t`=100 → 块 112；`Queue_t`=80 → 88（互斥/信号量）；`EventGroup_t`=32 → 40；
队列 = 80 + 深度×项宽；`netconn` = recvmbox(6×4) + op_completed = 200。
阶段：pre=调度器前；eth=网络；sw2/sw3=sw_initcall；chan=通道启动；run=运行期（首帧/首连后）。

| # | 阶段 | 创建者 | 对象 | 字节 | 累计 | 余量 |
|---|---|---|---|---|---|---|
| 1 | pre | app_boot | init_task（512 words） | 2168 | 2168 | 34688 |
| 2 | eth | sys_arch | lwip_sys_mutex（sys_init） | 88 | 2256 | 34600 |
| 3 | eth | lwip/mem | mem_mutex（mem_init → sys_mutex_new，!NO_SYS 恒建） | 88 | 2344 | 34512 |
| 4 | eth | sys_arch | tcpip_mbox（TCPIP_MBOX_SIZE=6） | 112 | 2456 | 34400 |
| 5 | eth | sys_arch | lock_tcpip_core（CORE_LOCKING） | 88 | 2544 | 34312 |
| 6 | eth | sys_arch | tcpip_thread（TCPIP_THREAD_STACKSIZE=1024） | 1144 | 3688 | 33168 |
| 7 | eth | pl_eth | RxPktSemaphore | 88 | 3776 | 33080 |
| 8 | eth | pl_eth | TxPktSemaphore | 88 | 3864 | 32992 |
| 9 | eth | pl_eth | ethernetif_input / EthIf（256 words×4） | 1144 | 5008 | 31848 |
| 10 | eth | pl_net | ethernet_link_thread / EthLink（1KB） | 1144 | 6152 | 30704 |
| 11 | sw2 | dev_key | press_sem ×4（SW1/SW2/SW3/TEST） | 352 | 6504 | 30352 |
| 12 | sw2 | dev_display | scan evt flags | 40 | 6544 | 30312 |
| 13 | sw2 | dev_w25qxx | s_w25_mutex | 88 | 6632 | 30224 |
| 14 | sw3 | app_ldi | ldi tx_lock（互斥） | 88 | 6720 | 30136 |
| 15 | sw3 | ring_buffer | RJ45 RB mutex（IAP/LDI/CQ 首个 acquire） | 88 | 6808 | 30048 |
| 16 | sw3 | ring_buffer | RS485 RB mutex | 88 | 6896 | 29960 |
| 17 | sw3 | ring_buffer | RS232 RB mutex | 88 | 6984 | 29872 |
| 18 | sw3 | dev_w25qxx | s_evt（首次 _read 懒建） | 40 | 7024 | 29832 |
| 19 | chan | app_boot | half_sec_task（128 words） | 632 | 7656 | 29200 |
| 20 | chan | app_tcp_server | tcp_server_task（1KB） | 1144 | 8800 | 28056 |
| 21 | chan | app_tcp_client | tcp_client_task（1KB） | 1144 | 9944 | 26912 |
| 22 | chan | app_udp | udp_task（1KB，10011） | 1144 | 11088 | 25768 |
| 23 | chan | app_udp | udp_cq_task（1KB，CQ 业务口） | 1144 | 12232 | 24624 |
| 24 | chan | app_udp | udp_gzol_task（1KB，GZ_OL 业务口） | 1144 | 13376 | 23480 |
| 25 | run | app_tcp_server | netconn recvmbox + op_completed | 200 | 13576 | 23280 |
| 26 | run | app_tcp_server | acceptmbox（netconn_listen） | 112 | 13688 | 23168 |
| 27 | run | app_tcp_client | client_disconnect_sem | 88 | 13776 | 23080 |
| 28 | run | app_tcp_client | netconn recvmbox + op_completed | 200 | 13976 | 22880 |
| 29 | run | app_udp | udp_disconnect_sem | 88 | 14064 | 22792 |
| 30 | run | app_udp | netconn recvmbox + op_completed | 200 | 14264 | 22592 |
| 31 | run | app_udp | udp_connect_task（常驻 1KB） | 1144 | 15408 | 21448 |
| 32 | run | app_udp | udp_cq_disconnect_sem | 88 | 15496 | 21360 |
| 33 | run | app_udp | netconn recvmbox + op_completed | 200 | 15696 | 21160 |
| 34 | run | app_udp | udp_cq_connect_task（常驻 1KB） | 1144 | 16840 | 20016 |
| 35 | run | app_udp | udp_gzol_disconnect_sem | 88 | 16928 | 19928 |
| 36 | run | app_udp | netconn recvmbox + op_completed | 200 | 17128 | 19728 |
| 37 | run | app_udp | udp_gzol_connect_task（常驻 1KB） | 1144 | 18272 | 18584 |
| 38 | run | app_scroll | s_scroll_evt + 双互斥（首个滚动帧懒建） | 216 | 18488 | 18368 |
| 39 | run | app_tcp_client | tcp_client_conn_task（连上才建 1KB） | 1144 | 19632 | 17224 |
| 40 | run | app_yn_proto | yn_selftest_task（'2' 命令触发 1KB） | 1144 | 20776 | 16080 |
| 41 | run | app_yn_ol_proto | yn_ol_selftest_task（'2' 命令触发 1KB） | 1144 | 21920 | 14936 |

**关键结论**：**启动期峰值需求 13376B → 余量 23480B**（修复前 38032B → 赤字 1176B）；
**含全部运行期异步项的需求 21920B → 余量 14936B**。两者都远超「同一启动点 ≥2.5KB」的目标，
也给后续新增协议（再添 1~2 个 1KB 任务 + 队列）留出了余量。
`init_task`（2168B）在启动结束 `osThreadExit()` 时归还，不计入稳态。

### §8.4 三口径实测（2026-09-17 修复后全量重编，Makefile GCC Debug）

`.analysis/heap/verify_builds.sh` → `make clean` + 三口径全量重编，**exit=0 / error=0 /
warning=3（仅既有 HAL `stm32f4xx_hal_flash_ex.c` `-Wunused-parameter`）**：

| 口径 | `.text` | `.rodata` | `.data` | `.bss` | `.ccmram` | `._user_heap_stack` | SRAM 合计 | **SRAM 余** | **CCM 余** |
|---|---|---|---|---|---|---|---|---|---|
| `PROTO=ALL` `DISP=1_263` | 171860 | 202592 | 1672 | 124704 | **38332** | 2560 | 128936 | **2136B** | 27204B |
| `PROTO=CQ` `DISP=1_263` | 158684 | 201936 | 872 | 121228 | **36076** | 2564 | 124664 | **6408B** | 29460B |
| `PROTO=ALL` `DISP=22_1665` | 172036 | 202592 | 1672 | 124704 | **39036** | 2560 | 128936 | **2136B** | 26500B |

* SRAM 合计 = `.data + .bss + ._user_heap_stack`（SRAM 上限 131072B）；CCM 上限 65536B。
* **注（2026-09-17 第二十七/二十八轮 22-1665 复核）**：上表 `PROTO=ALL DISP=22_1665` 行是**堆修复轮的价值**
  （22_1665 驱动当时为模型 A 形态、本模组 3200B）。**第二十七轮（模型 B 落地为默认）后同口径** =
  `.text 172164 / .rodata 202952 / .data 1672 / .bss 124712 / .ccmram 39164（本模组 **3328B** = 1408×2 + 256×2）/
  ._user_heap_stack 2560`，SRAM 合计 128944B（余 2128B）；**第二十八轮（收口整理：注释/命名对齐）段尺寸零变化**
  （同口径复测逐字节相同）。**工作区当前宏值 `MODULE_COLS = 5`（16×80）** ⇒ `.ccmram 44156`（本模组 **8320B** =
  1408×5 + 256×5）/ `.bss 124716` / `._user_heap_stack 2564`，SRAM 128952B（余 2120B）；
  `-D_22_1665_MODULE_COLS=2` 重链即回到 39164B。其余两行的绝对值同样可能随他处在飞改动漂移。
* **相对修复前（ALL/1_263：text 170596 / rodata 201736 / bss 126436 / ccmram 11164 / SRAM 余 400B）**：
  `text +1264` / `rodata +856`（判空守卫 + `heap` 诊断串 + `app_boot` 静态内核任务缓冲的强定义代码 +
  `heap_4` 失败请求量补丁）、`bss **−1732**`（空闲/定时器缓冲改落 CCM）、
  `ccmram **+27168**`（23 任务 25340 + 空闲/定时器 1736 + 对齐 92）、**SRAM 余量 400B → 2136B**。
* **注（2026-09-17 晚 联调第二轮，YN_OL 第二/三轮联调：通道层·调度层诊断 + 通知投递修复）**：
  本轮改动**不新增任何任务 / 队列 / 动态对象**（新增静态量全在 `.bss`，为调度层诊断缓存 36B +
  客户端连接去重状态 4B），**§8.3 的 ucHeap 预算表不受影响**。同口径再采（三跑 exit=0、仅 3 条既有 HAL 告警）：
  `PROTO=ALL` `DISP=1_263` = `.text 174996 / .rodata 204848 / .data 1680 / .bss 124772 / .ccmram 38332 /
  ._user_heap_stack 2564` ⇒ **SRAM 合计 129016B（余 2056B）**；
  `APP_DIAG=0` 对照 = `.text 169124 / .rodata 198672 / .data 1672 / .bss 124736 / .ccmram 38332 /
  ._user_heap_stack 2560` ⇒ SRAM 128968B（余 **2104B**）；
  `PROTO=ALL` `DISP=22_1665`（工作区 1×5）= `.text 175252 / .rodata 205104 / .bss 124776 / .ccmram 44156 /
  ._user_heap_stack 2560` ⇒ 余 **2056B**。
  增量归属与零开销验证（`APP_DIAG=0` 后 elf 内新增诊断字符串 0 条）见 `doc/15` §11.5 与
  `.analysis/yn_ol/round2_channel_dispatch_diag.md`；日志 `.analysis/yn_ol/round2_*.log`。
* 机器级归属证据（`nm`，见 `verify_builds.out.txt`）：25 个静态栈
  （23 应用任务 + `s_idle_task_stack` + `s_timer_task_stack`）全部在 `0x1000_xxxx`；
  `ucHeap` 仍在 `0x20015c3c`（SRAM，36864B）。

### §8.4.1 2026-09-18 复采（YN_OL TCP 现场修复轮：诊断档位收紧 + 机制加固）

改动**不新增任何任务 / 队列 / 动态对象**（新增静态量：调度层每通道限速时间戳 `CH_ID_MAX×4B` +
TCP Server 告警去重标记 1B + 对齐 ⇒ `.bss +40B`），**§8.3 的 ucHeap 预算表不受影响**；
`ucHeap` 仍在 SRAM 36864B（政策未变），实测运行期 `free=20856 / min=18688`（RTT `[mcu]` 轮询 340s 稳定，
`heap` 无下降趋势）。

| 口径（Makefile GCC Debug） | `.text` | `.rodata` | `.data` | `.bss` | `.ccmram` | `._user_heap_stack` | SRAM 合计 | **SRAM 余** |
|---|---|---|---|---|---|---|---|---|
| **`PROTO=ALL` `DISP=22_1665`（1×5）修复前**（`tree=3146c4f0` / elf `301080a0…`） | 175252 | 205104 | 1680 | 124776 | 44156 | 2560 | 129016 | **2056B** |
| **`PROTO=ALL` `DISP=22_1665`（1×5）修复后**（`tree=7df8ac20` / elf `71e94c8f…`） | **174640** | **203152** | 1680 | **124816** | 44156 | 2560 | 129056 | **2016B** |
| Δ | **−612** | **−1952** | 0 | **+40** | 0 | 0 | +40 | −40 |
| `PROTO=ALL` `DISP=1_263`（修复后） | 174400 | 202888 | 1680 | 124812 | 38332 | 2560 | 129052 | **2020B** |
| `PROTO=CQ` `DISP=1_263`（修复后） | 161220 | 202240 | 872 | 121336 | 36076 | 2560 | 124768 | **6304B** |

* **归因（两组对照编译实测）**：① LwIP 调试档位 `LEVEL_ALL → WARNING`（`APP_LWIP_DBG_LEVEL`，
  见 `Platform/Inc/lwipopts.h` + `Makefile`）单独贡献 **`.text −1044` / `.rodata −2216`**；
  ② 本轮新增诊断与机制（`pl_sys_reset_cause()` + 横幅复位行 + TCP keepalive + `netconn_new` 告警 +
  `[disp]` 无协议承载告警）净增 **`.text +432` / `.rodata +264` / `.bss +40`**（对修复前基线、LwIP 档位取 0x00）。
* **价值**：LwIP 的 `tcp_slowtmr/tcp_recved` 等 LEVEL_ALL 信息行实测 5~17 行/秒，1KB RTT 上行缓冲
  （`NO_BLOCK_SKIP`，满则丢）几十秒即被灌满 ⇒ 此后**一切现场诊断静默丢失**。收紧后 RTT 在开机后
  仍有余量打 `[err]`/`[disp]`/`[tcp_srv]` 等关键行（实测 12s 内 LwIP 噪声行 **0**）。
* 三口径全量重编（`PROTO=CQ DISP=1_263` / `PROTO=ALL DISP=1_263` / `PROTO=ALL DISP=22_1665`）**全部链接通过、
  零新增告警**（仍 3 条既有 HAL `-Wunused-parameter`）；日志 `.analysis/yn_ol/fix_verify/build_*.log`。
* 板内 22_1665 显存不变（1×5 = 8320B = `1408×5 + 256×5`），整项目 `.ccmram 44156B`（CCM 余 21380B）。
  （**第三十二轮工作区宏值为 8×2 = 128×32 ⇒ 本模组 23040B、整片 58876B，见 §8.4.2**；
  **第三十四轮实测工作区已被外部改成 8×4 = 128×64 ⇒ 本模组 46080B，`PROTO=ALL`/`CQ` 均链接期溢出，见 §8.4.3**。）

### §8.4.2 2026-09-18 第三十二轮复采（22_1665 删除自设 16KB CCM 预算；工作区宏值 8×2 = 128×32）

**改动性质**：只删驱动自设的 `_Static_assert(_22_1665_CCM_BYTES <= 16384U)`（改为内存物理守卫
`<= 64U * 1024U` = 整片 CCMRAM 区域 64KB）+ 注释；**零代码、零新增静态量** ⇒ 非模组部分的
`.data / .bss` 与 ucHeap 政策不变。**用户第三十一轮把几何宏改成 `8 / 2`（M=16），旧的自设上限
（`chain_dst` 单项在 M=16 时恰为 16384B = 整个旧预算）把它挡在编译期**；本轮放开后
`make DISP=22_1665 -j8`（**增量、未 clean**）一次通过。

| 口径（Makefile GCC Debug，`APP_DIAG_BANNER` 默认开） | `.text` | `.rodata` | `.data` | `.bss` | `.ccmram` | `._user_heap_stack` | SRAM 合计 | **SRAM 余** |
|---|---|---|---|---|---|---|---|---|
| **`PROTO=ALL` `DISP=22_1665`（8×2 = 128×32，现行工作区值）** | **175248** | **203296** | 1656 | 124836 | **58876**（本模组 **23040** = `1408×16 + 256×2`） | 2564 | **129056** | **2016B** |
| `PROTO=ALL` `DISP=1_263`（同轮复采） | 174976 | 203040 | 1656 | 124836 | 38332 | 2564 | 129056 | **2016B** |
| `PROTO=CQ` `DISP=1_263`（同轮复采） | 161796 | 202384 | 856 | 121360 | 36076 | 2560 | 124776 | **6296B** |

* **本模组 CCM 增量（几何 1×5 → 8×2，驱动对象 A/B 实测）**：`.ccmram 8320 → 23040`（**+14720**
  = `1408×11 + 256×(−3)`）、`.text +28`（`prepare` / `scan` / `build_chain_dst` 的常量变化）、
  `.rodata ±0`、`.bss −3`（`_22_1665_bsrr_slot_cnt[5 → 2]`）；与「1×5 修复轮基线」的整项目差
  （text +608 / rodata +144 / data −24 / bss +20）里，其余 ≈`+580 / +150 / −24 / +23` 来自
  **工作区他处在飞改动**（同轮 `DISP=1_263`、`PROTO=CQ` 两口径——**不编本驱动**——的偏差量级一致，可对照）。
* **CCM 上限口径**：整片 64KB 由**链接器兜底**（`.ccmram` 段无 `ASSERT`，溢出即
  `region .ccmram overflowed by N bytes`）；驱动内当时只剩「本模组数组 ≤ 整片区域 64KB」**物理守卫**
  （自设 16KB 预算已删，用户裁决「分辨率只受 CCMRAM 实际空间约束、由宏定义修改」）
  —— **该物理守卫亦已于第三十四轮删除（全部编译期防御取消），见 §8.4.3**。
  **其他消费者 37156B**（2026-10-08 由 65536 + 17700 − 46080 反推；含 §8.2 静态任务栈 25340 + 内核缓冲 1736 +
  CQ 6377 + GZ_OL 1136 + YN_OL 1152 + 对齐；旧值 35836B）⇒ **本模组实用上限 ≈28400B**：COLS=2 时 M ≈ 20
  （10×2 = 160×32，28672B、整片余 1028B）为上限量级〔**估算，以链接期为准**〕；9×2（25856B、余 3844B）较稳。
* **⚠ 时序软约束（非编译期、本轮未改定时参数）**：8×2 每帧 1024 时钟 ⇒ `_22_1665_scan`
  ≈**0.36~0.48ms**（占 TIM3 固定 **500µs** 帧周期的 **72%~96%**）、`prepare`（整帧提交重算）
  ≈**1.0~1.2ms**（跨帧掉帧）——**须上机 DWT 实测复核**；超标的可选方向（增大 TIM3 周期降刷新率 /
  后续去掉 `chain_dst` 全量预计算表）**本轮均未实施**（见 doc/01 §0.7 与附录 A.19）。
* 三口径链接通过、零新增告警（仍 3 条既有 HAL `-Wunused-parameter`）；`verify_all.sh` 全量
  **20 项全绿**（含新增 `check_round32_ab.sh` 与换口径重链 `mod8x2`）。日志
  报告 `.analysis/22_1665/round32_ccm_limit_removed.md`。

### §8.4.3 2026-09-20 第三十四轮复采（22_1665 取消编译期防御 + 精简注释；工作区宏值 8×4 = 128×64）

**改动性质**：22_1665 驱动**删除全部 17 条 `_Static_assert`**（含第三十二轮保留的「本模组数组 ≤ 整片
CCMRAM 区域 64KB」物理守卫）+ 仅断言使用的 2 个派生宏 + **精简注释**（441 → 372 行）。
**零代码、零新增/减少静态量** ⇒ **驱动对象逐 section 逐字节不变**（20 个段 md5 全同 + 反汇编 366 行 0 差异），
本节的 SRAM/CCM 账**完全不变**；变的只有「错误几何的兜底方式」：由编译期断言改为**注释契约 + 链接期报错**。

> **2026-10-08（冲突修复落地轮）实测（现行基线；表内第三十四轮各行为历史值）**：`PROTO=ALL DISP=1_263`
> `.text **167944** / .rodata 195776 / .data 1656 / .ccmram 39652 / .bss 124768 / ._user_heap_stack 2560`
> ⇒ SRAM 合计 **128984B（余 2088B）**、CCM 余 25884B（相对改前基线 **text +192**，其余段 ±0 —— 增量 =
> C3 加固：`app_dispatch_channel_has_proto` 32 + `init_task` 门控 68 + `ldi_sync_net_from_board_cfg` 88 +
> `cmd_search`/`cmd_rep_ip` 8）；`PROTO=CQ DISP=1_263` `.text **154588**`（+96）/ 其余 ±0；
> `DISP=22_1665` 两口径链接期溢出 **17700B（ALL）/ 15444B（CQ）**（本模组 46080B）；
> **EIDE 等价口径实构**：〔2026-10-08 当时口径〕Debug 选择（206 源文件，`--emit-make`）`.text 133112 / .rodata 192232 /
> .ccmram **52972** / .bss 115952`、Myanmar（207 源文件、无显示）`.text 132136 / .ccmram **16084** /
> .bss 115704`；〔2026-10-08 当时〕Debug 选择 + 8×4 = `.ccmram 60844B`（余 4692B，链接通过）。
> **2026-10-13 target 重组后**：`Myanmar` 已删、Base/Debug 基线不编显示驱动/协议模块（见 `doc/构建开关总表.md` §7）。
> 四口径均零新增告警（仍 3 条既有 HAL）。

| 口径（Makefile GCC Debug，`APP_DIAG_BANNER` 默认开，第三十四轮实测） | `.text` | `.rodata` | `.data` | `.bss` | `.ccmram` | `._user_heap_stack` | SRAM 合计 | **SRAM 余** |
|---|---|---|---|---|---|---|---|---|
| `PROTO=ALL` `DISP=1_263` | **174976** | **203040** | 1656 | 124836 | 38332 | 2564 | **129056** | **2016B** |
| `PROTO=CQ` `DISP=1_263` | **161796** | **202384** | 856 | 121360 | 36076 | 2560 | **124776** | **6296B** |
| `PROTO=ALL` `DISP=22_1665`（8×4） | — | — | — | — | 83116（本模组 **46080** = `1408×32 + 256×4`） | — | — | **链接期溢出 17700B，无 elf** |
| `PROTO=CQ` `DISP=22_1665`（8×4） | — | — | — | — | 80980（本模组 46080） | — | — | **链接期溢出 15444B，无 elf** |

* **前两行与第三十二轮记录逐值相同**（`ALL/1_263` `.text 174976 / .rodata 203040 / .ccmram 38332`、
  `CQ/1_263` `.text 161796 / .rodata 202384 / .ccmram 36076`）——两口径**不编 22_1665 驱动**，印证「本轮的
  断言/注释删除不产生代码」；四口径均**零新增告警**（仍 3 条既有 HAL `-Wunused-parameter`）。
* **⚠ 工作区宏值 8×4 的现实（预存状态，非本轮引入）**：本模组 **46080B** + 其他消费者 **37156B**
  （= 静态任务栈 25340 + 内核缓冲 1736 + CQ 6377 + GZ_OL 1136 + YN_OL 1152 + 对齐；2026-10-08 实测反推）= **83116B > 64KB**
  ⇒ `PROTO=ALL` 溢出 **17700B**；`PROTO=CQ`（其他消费者 34900B）溢出 **15444B**。
  第三十三轮已按算术预测、本轮实测确认（`ld` 报错文本在改动前/后**逐字节一致**）。
  **⇒ `DISP=22_1665` 在 8×4 下没有任何 make 口径能产出 elf/hex**，只有 **EIDE 子集**能装
  （〔2026-10-08 当时口径〕Debug 选择 + 8×4 实测 `.ccmram 60844B` / 余 4692B，2026-10-08 实构；旧记录 57484B/余 8052B = 当时更小的子集；**2026-10-13 起 Base/Debug 基线不编显示驱动，要用 22_1665 按 `doc/构建开关总表.md` §7.2 第 8 条点选**）。**要让 make 出 22_1665 产物**：几何减到 8×2（23040B，
  ALL 口径 58876B，`link_variant.sh mod8x2` 实测链接通过）或让出 CCM（CQ/治超队列 / 静态任务栈）——
  均属**待用户决策**（`doc/01` §6）。
* **验证入口**：`bash .analysis/22_1665/verify_all.sh`（全量 **20 项全绿 / 1 项跳过**：`check_flash_artifact.sh`
  因上述「无 22_1665 make 产物」显式记为「跳过（前置不满足）」）；`check_compile_matrix.sh` C 节新增
  「47×1 = 66432B ⇒ 链接期 `overflowed by 896 bytes`」的链接期兜底回归；A/B 脚本 `check_round34_ab.sh`（5/5）。

### §8.4.4 2026-09-29 第二轮复采（RTT 底层恢复 + 注释精简；用户 4 条裁决口径）

**背景**：同日第一轮把 RTT 设施整体移除后，用户裁决「RTT = 只清理 API 调用，不清理底层」⇒
第二轮恢复 SEGGER RTT 三个源文件 + `Platform/Src/pl_rtt.c`（逐字节 = `git HEAD`）与其
`hw_pl_initcall` 注册；自研诊断设施（`app_diag.h` / `APP_DIAG_BANNER` / `*_RTT_DIAG` / `[disp]` 告警）
**保持删除**；业务代码**零 RTT API 调用**。同轮另做：`build/` 清理（删 5 个非 Debug/Release 目录，
43MB）、Makefile `BUILD_DIR ?=`（供脚本把产物落到 `.analysis/`）、长注释外移。

口径：`make clean` 后全量 `make -j8`（Makefile GCC Debug），仅 3 条既有 HAL 告警。

| 口径 | text | rodata | data | ccmram | bss | heap | SRAM 合计 / 余量 |
|---|---|---|---|---|---|---|---|
| `PROTO=ALL` `DISP=1_263` `FACTORY=STD` | **167752**（+464） | **195776**（+40） | 1656 | 39652 | **124768**（+1208） | 2560 | **128984 / 2088B** |
| `FACTORY=MM` | **166216**（+464） | **195664**（+40） | 1656 | 39652 | **124504**（+1208） | 2560 | **128720 / 2352B** |
| `PROTO=CQ` `DISP=1_263` | **154492**（+448） | **195112**（+40） | 852 | 37396 | **121292**（+1208） | 2560 | **124704 / 6368B** |
| 无显示等价口径（`.analysis/myanmar/build_nodisp/`） | **131928**（+448） | **191968**（+40） | 1584 | 16084 | **115704**（+1208） | 2560 | **119848 / 11224B** |

* 括号内 = 相对第一轮（RTT 移除态）差值；**增量全部来自 RTT 底层**：text/rodata = `SEGGER_RTT_Init` +
  `Write`/`WriteNoLock` + 系统调用垫片被引用的部分（`--gc-sections` 已丢弃 `SEGGER_RTT_printf` 等未引用函数）；
  bss **+1208** = `_acUpBuffer` 1024 + `_acDownBuffer` 16 + `_SEGGER_RTT` 168（`nm` 实证：
  `_acUpBuffer` @ `0x2000ebe0`、`_SEGGER_RTT` @ `0x2000efe0`）。
* elf md5：默认口径 `09c574f0f2aed808510792d0c316fb13`、`FACTORY=MM` `43863e6b44ca38ed58935bd4d74ba5c9`、
  `CQ` `dc0cf186c34af8fa7a6606aa732346c4`、无显示口径 `ce7fd155adac088b782f5b18cafe23d0`。
* `nm` 复核：`pl_rtt_init` @ `0x08041330` + `__hw_initcall_1_pl_rtt_init`（initcall 表内）；
  `SEGGER_RTT_Init` / `SEGGER_RTT_Write` / `SEGGER_RTT_WriteNoLock` / `_acUpBuffer` / `_acDownBuffer` /
  `_SEGGER_RTT` 均在。**业务代码 0 处 RTT API 调用**（`Application/ Device/ Kernel/ Platform/`
  下唯一命中 = `pl_rtt.c` 自身）。
* **注释精简 A/B**（4 个被改文件：`app_boot.c` / `app_ldi.c` / `app_cq_proto.c` / `dev_display_22_1703.c`）：
  「重建改动前源码 vs 现行源码」同 flags 重编 ⇒ section（排除 `.debug_*`）**逐字节一致** +
  反汇编**逐行一致**（105 段 / 1801 行）——见 `.analysis/cleanup_20260929/round2_ab_compare.sh`。
* 完整记录：`doc/17` §5。

### §8.4.5 2026-10-12 复采（新疆费显协议接入 + 调度框架掩码 32→64）

采样口径：`make -j8 BUILD_DIR=build/xj_b{0,1,2}`（各自独立目录全量构建，不触碰 `build/Debug/`），
`PROTO=ALL DISP=1_263 FACTORY=STD`（CQ 与 nofault 另测）；零新增告警（仍 3 条既有 HAL
`stm32f4xx_hal_flash_ex.c -Wunused-parameter`）。
〔注（2026-10-12 工作区清理轮）：`build/xj_*` 各抽样目录属中间产物，已 `rm -rf build/xj_*` 删除
（§8.4.6 同）；本表数字的归档证据 = `.analysis/xinjiang/size_b*.txt` / `build_b*.log`，按上列命令重跑即可重建目录。〕

| 口径 | `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | `._user_heap_stack` | SRAM 合计 / 余 | CCM 余 |
|---|---|---|---|---|---|---|---|---|
| **B0** 工作区基线（未改代码） | 167944 | 195776 | 1656 | 39652 | 124768 | 2560 | 128984 / **2088B** | 25884B |
| **B1** = B0 + 掩码扩容 32→64 | 168536 | 195776 | 1656 | 39652 | 125356 | 2564 | 129576 / **1496B** | 25884B |
| **B2** = B1 + 新疆模块 + Makefile 收录 | 170392 | 196024 | 1656 | **41196** | 125388 | 2564 | 129608 / **1464B** | 24340B |
| B1−B0（掩码归因） | +592 | ±0 | ±0 | ±0 | +588 | +4 | +592 | — |
| B2−B1（新疆归因） | +1856 | +248 | ±0 | **+1544** | +32 | ±0 | +32 | −1544 |

* **掩码扩容归因**：`g_dispatch` 488 → 912B（三个 64 项指针表 +384、`registered_mask` 4→8、
  `ch_proto_map[9]` 36→72）；35 个协议模块的 `s_*_mask` 静态量各 4→8B（+140）；余为对齐。
  `.text` 增量来自 64 位 ctz/掩码运算（`frame_dispatch_task +110`、`app_channel_dispatch +40`、
  `app_proto_register +36`、新增 `bit_ctz64 +32` 等）。`app_channel_dispatch` 栈帧 140 → **64B**
  （RB 去重表按缓冲池槽数 `RB_CNT_MAX` 开表；按 64 指针开表会到 268B）。
* **新疆模块归因（B2−B1）**：`.ccmram +1544` = 队列体 246 + `StaticQueue_t` 80 + 任务帧缓冲 82 +
  任务栈 1024 + `StaticTask_t` 100 + 对齐 12；`.bss +32` = mask×2 16 + 队列句柄 8 + 心跳时刻 4 +
  两个标志 + 对齐；`.rodata +248` = 文案/查表/attr；`.text +1856` = probe/解析/执行/任务/init。
* **`PROTO=CQ`（B2-CQ，全量重编）**：`.text 157004 / .rodata 195360 / .data 852 / .ccmram 38940 /
  .bss 121900 / ._user_heap_stack 2564` ⇒ SRAM 合计 **125316B（余 5756B）**、CCM 余 26596B；
  链接通过、零新增告警。
* **`XJ_FAULT_SCREEN=0` 对照（B2-nofault，`DEFINES` 透传）**：`.text 170104 / .rodata 196008 /
  .data 1656 / .ccmram 41196 / .bss 125380 / heap 2564` ⇒ 相对 B2 **text −288 / rodata −16 / bss −8**
  （心跳函数体、维护屏渲染与文案、两个心跳静态量随 `--gc-sections` 整体消失），印证「开关 0 时
  无计数、无渲染」。
* 宿主推演 **247/247**（2026-10-12 复核修正：probe 读长
  `XJ_LEN_OFFSET`→`XJ_LEN_OFFSET + 1U`，消除 `head[2]` 未初始化读；新增覆盖回归断言）；
  结构脚本 `check_channel_gate.py` **15/15**、
  `check_display_guards.py` **16/16 + 100/100**。完整记录见 `doc/19_新疆费显协议/README.md` §6 与
  `.analysis/xinjiang/implementation_report.md`。

### §8.4.6 2026-10-12 第二轮复采（PDF 全文命令面 + 帧长 74→106 + 新疆模块自备串口 RB）

采样口径：`make -j8 PROTO=ALL DISP=1_263 FACTORY=STD BUILD_DIR=build/xj_r2_all` 与
`make -j8 PROTO=CQ DISP=1_263 FACTORY=STD BUILD_DIR=build/xj_r2_cq`（各自独立目录全量重编，不触碰
`build/Debug/`）；零新增告警（仍 3 条既有 HAL `stm32f4xx_hal_flash_ex.c -Wunused-parameter`）。
〔注（2026-10-12 工作区清理轮）：本节 `build/xj_r2_*` / `build/xj_r2_dbg*` 抽样目录已删除（中间产物）；
归档证据 = `.analysis/xinjiang/r2_evidence/`（`size_*.txt` / `build_*.log` / `nm_*.txt` / `Debug.r2.mk`），
按上列命令重跑即可重建目录。〕

| 口径 | `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | heap | SRAM 合计 / 余 | CCM 余 |
|---|---|---|---|---|---|---|---|---|
| **R2-ALL**（ALL / 1_263 / STD） | **172056** | **196264** | 1656 | **41324** | 125388 | 2564 | 129608 / **1464B** | 24212B |
| **R2-CQ**（CQ / 1_263 / STD） | **158652** | **195592** | 852 | **39068** | 121900 | 2564 | 125316 / **5756B** | 26468B |
| R2-ALL − §8.4.5 B2 | +1664 | +240 | ±0 | **+128** | **±0** | ±0 | ±0 | −128 |

* **归因**：`.text/.rodata` = PDF 全文命令面扩面（白名单 9→14、`'S'` 四形态报价、`'D'/'C'/'Z'/'P'` 5B 应答、
  8 条 ETC 文案）+ 帧长口径 74→106（'T' N≤6、'D' 全屏 7+16N），其中**本轮「模块自备 RB」的 getter 仅 +16B text**；
  `.ccmram +128` = 队列 `3×(82→114) = +96` 与任务帧缓冲 `82→114 = +32`；
  **`.bss` 两口径都 ±0** —— 新疆模块新增的两槽 `RB_PROVIDE_WEAK` 定义被同槽他协议 weak 合并，
  其 static 缓冲随 `--gc-sections` 丢弃，**多协议口径无额外 SRAM 代价**。
* **新疆模块 `.ccmram` 跨度（`nm` 符号级）**：任务帧缓冲 114 + 队列体 342 + `StaticQueue_t` 80 +
  任务栈 1024 + `StaticTask_t` 100 = **1660**（+对齐 8）= **1668B**（§8.4.5 记的 1544B 为 payload=74 口径）。
* **同一目标口径 A/B（等价 EIDE Debug = IAP + LDI + 新疆，用户现场口径）**〔**2026-10-12 当时**的 Debug 选择；
  2026-10-13 target 重组后 Base/Debug = 基线（不含 LDI/新疆），该子集需在 GUI 按需点选，见 `doc/构建开关总表.md` §7〕：
  `check_eide_targets.py --target Debug --emit-make` 生成等价 Makefile，`BUILD_DIR=build/xj_r2_dbg`（改后）
  与 `build/xj_r2_dbg_nofix`（临时摘掉两行 `RB_PROVIDE_WEAK` 的改前对照）：

| 段 | 改前（无提供者） | 改后（自备） | Δ |
|---|---|---|---|
| `.text` | 130584 | 130600 | **+16** |
| `.data` | 1592 | 1624 | **+32** |
| `.bss` | 112372 | 113908 | **+1536**（768+768 槽体） |
| `.ccmram` | 15548 | 15548 | 0 |
| SRAM 合计 | 116528 | 118096 | **+1568** ⇒ 余 **12976B** |

  改后 `rb_provide_rs485/rs232` 符号在册、`g_rb_provide` 表 `{RJ45, RS485, RS232}` 三槽全非空
  （改前为 `791c0408 00000000 00000000`）。**这是用户现场「发帧零反应」的根因修复**（旧目标
  「IAP+LDI+新疆」无任何串口协议提供槽体 ⇒ 新疆模块不注册 + 串口通道被门控跳过），
  。

### §8.4.7 2026-10-13 复采（用户裁决执行轮：编译期断言清零 + 只读观察 API / 调试冻结清理）

采样口径：`make -j16 BUILD_DIR=build/cmt3_after/Debug` 与 `... PROTO=CQ`（各自独立目录全量重编，
不触碰 `build/Debug/`）；零新增告警（仍 3 条既有 HAL `stm32f4xx_hal_flash_ex.c -Wunused-parameter`）。

| 口径 | `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | heap | 相对 §8.4.6 |
|---|---|---|---|---|---|---|---|
| **R3-ALL**（ALL / 1_263 / STD） | **171832** | 196264 | 1656 | 41324 | **125364** | 2564 | text **−224** / bss **−24**，其余段 ±0 |
| **R3-CQ**（CQ / 1_263 / STD） | **158444** | 195592 | 852 | 39068 | **121876** | 2564 | text **−208** / bss **−24**，其余段 ±0 |

* **归因（逐对象期望清单 `ab_round3_objects.py` 7/7 全绿 + 断言隔离 A/B `ab_round3_asserts.py` 2/2）**：
  - **编译期断言删除 = 0 codegen**：6 条断言（22_1665 2 条 + `app_dispatch.c` 4 条）逐字插回后同 flags 复编
    ⇒ `dev_display_22_1665.c` 21/21 段、`app_dispatch.c` 33/33 段逐字节一致 + 反汇编逐行一致 + strip-debug md5 相同；
  - **未引用的观察 API 函数体 = 0 差异**（`app_render_chip_info_get` / `app_render_glyph_probe` /
    `app_render_glyph_addr_for`、`app_dispatch_qfull_drops/resync_count/notify_drops`、
    `app_udp_gzol_bind_status(_str)`——早已被 `--gc-sections` 丢弃，其 rodata 字符串同样未进镜像）；
  - **实际差值来自活代码**（两口径逐对象差值相同）：`pl_tim_dbg_freeze` −32 + `dev_display_init` 两处调用 −20 +
    `udp_gzol_task` 三处绑定结果记录 −56 + `tcp_server_task` 两处标志写 −16 + `mm_apply_lamp` / `mm_apply_init`
    −12 / −12 + `frame_dispatch_task` 三处计数自增 −52 + `app_channel_dispatch` 重试计数 −16 = **216B**
    （+链接对齐 ⇒ ALL −224 / CQ −208）；
  - **`.bss −24`** = 6 个观察变量删除：`g_dispatch` 三个 uint32 域 −8（原含 4B 尾对齐）/ `g_tcp_server_connected` −4 /
    `s_mm_lamp_state` −3 / GZOL `s_udp_gzol_bind_sta` −1 + `bind_err` −2 + `bind_ok_cnt` −4（共 −22）+ 对齐 −2。
* **行为面**：删除的均为「只读观察状态 / 调试冻结」——重试路径、通道门控、协议行为、`0x40` 改端口语义等
  全部保留；`check_guard_disasm.py` 的「原操作零丢失」判据已按删除点补 reviewed 条目
  （`.analysis/myanmar/check_guard_disasm.py`，见 `.analysis/comment_pass_20261013/report.md` 裁决执行轮）。

### §8.4.8 2026-10-15 增量（UART TX 中断化：`pl_uart_send` 不再轮询忙等）

口径：**对象级**（EIDE 生成的 `build/Debug/compile_commands.json` 原始编译命令，仅加 `-fsyntax-only` /
改 `-o` 到仓库外，编译 `Platform/Src/pl_uart.c` 与 `git show HEAD:Platform/Src/pl_uart.c`，同 flags 对比；
**未跑 make、未跑 EIDE builder、未在仓库内落任何产物**）。

| 对象 | `.text` | `.rodata` | `.data` | `.bss` | 说明 |
|---|---|---|---|---|---|
| `pl_uart.o`（HEAD 旧） | 1208 | 13 | 0 | 108 | `g_uart_ctx` 仅 RX 字段 |
| `pl_uart.o`（现行新） | **1648** | **40** | 0 | **612** | **+440 / +27 / 0 / +504** |

* **`.bss +504`** = 3 个 UART × (互斥控制块 80 + 完成信号量控制块 80 + 两个句柄 2×4) = 3×168；
  `StaticSemaphore_t` 实测 80B（宿主 `-S` 读 `.word` 常量），**静态分配，不占 FreeRTOS 堆**（堆水位不变）。
* **`.rodata +27`** = 两个 `osMutexAttr_t`/`osSemaphoreAttr_t` 的 `.name` 字符串
  （`pl_uart_tx` / `pl_uart_txdone`，`configQUEUE_REGISTRY_SIZE > 0` 时随对象注册）。
* **`.text +440`** = 新增 `HAL_UART_TxCpltCallback` 56 + `pl_uart_send` 72→336 + `pl_uart_set_baud`
  112→184 + 惰性创建/回退/中止路径等（逐函数尺寸见仓库外对比表）；
  其中 **+20** 来自 `_tx_sync_ensure()` 的并发首次使用加固（判空 + 创建 + 赋值整体入
  `osKernelLock()/osKernelUnlock()`，`osMutexNew`/`osSemaphoreNew` 非阻塞，可在锁内调用）——
  `.rodata`/`.data`/`.bss` 三项与加固前逐字节相同。
* 行为面：发送语义不变（成功 = 已发完 / 失败超时 = 已中止），对外 API 签名不变；
  唯一副作用 = 发送期间任务让出 CPU（这正是修复目标；背景见 `doc/20…/README.md` §11 结案结论）。

#### 整项目段尺寸（EIDE 构建产物，2026-10-15 18:00，**探针回收后的「净版」**）

来源 = `build/Debug/Project_STD.elf`（EIDE 构建；`mtime 2026-10-15 18:03` > `pl_uart.c 17:45`，
`nm` 含新符号 `HAL_UART_TxCpltCallback`；`strings` 查探针串 `[hb]/[tx]/[claim]/[vms]/…` **零命中** ⇒
已是不含临时探针的净版；elf md5 **`4fbcb39dbf6d2e3c489f1c941bf60c69`**。
同日 18:00 的一次重建（md5 `668de585aa4ba5b6d750f26636b82abd`）与本表**段尺寸逐字节相同**——
横幅内嵌 `__DATE__/__TIME__` ⇒ 每次重建 md5 必变，段尺寸才是口径判据）。口径 = EIDE `Debug`
target 现状（`LDI + 四川 ETC/MTC/治超 + dev_display_1_969.c` + 标准厂测 + RTT 底层保留；
以 `.eide/eide.yml` 现状为准）。

| 段 | 字节 | | 段 | 字节 |
|---|---|---|---|---|
| `.isr_vector` | 392 | | `.data` | 1608 |
| `.text` | 148072 | | `.bss` | 116920 |
| `.rodata` | 103872 | | `.ccmram` | **52972** |
| `.hw_initcall` | 160 | | `._user_heap_stack` | 2560 |
| `.sw_initcall` | 120 | | | |

* **SRAM 合计** = `.data + .bss + ._user_heap_stack` = **121088 B**（/131072，余 **9984 B**）。
* **CCM 余量** = 65536 − 52972 = **12564 B**。CCMRAM 主要消费者 = `_1_969` 显示
  （`hub75_buff 18432 + pixel_map 18432 + row_dst 192 + g_bsrr 1152 = 38208`）+ 静态任务栈/TCB
  （约 27 KB）。
* FLASH = **254248 B**（EIDE banner，768KB 的 32.33%）。
* **回归核对**：相对 `HEAD`，本轮源码只动 UART TX 路径（`pl_uart` 对象 `.text +440 / .bss +504`）
  与若干非探针功能改动；**探针删除不改静态数据布局**（探针只加 RTT 调用与只读计时），故本表即结案
  构建的净版数字。

### §8.5 永久诊断与错误处理（**设施已随 2026-09-29 瘦身轮删除；RTT 底层保留**）

| 设施（历史） | 位置 | 原作用 |
|---|---|---|
| ~~开机横幅 `[diag] heap at banner free=… min=…`~~ | ~~`app_boot.c` `app_diag_boot_banner`~~ | 一条 RTT 行给出当前余量与**开机以来最小余量** |
| ~~逐通道 `[diag] heap after <chan>_start free=… min=…`~~ | ~~`app_boot.c` `init_task`~~ | 每个通道启动后各打一次，堆耗尽可**不经调试器**定位到具体启动点 |
| ~~`[err] pvPortMalloc FAILED: …`~~ | ~~`app_boot.c` `vApplicationMallocFailedHook`~~ | 失败请求字节数由 `heap_4.c` 诊断补丁 `xLastFailedAllocSize` 导出（补丁仍在，调试器可读）；**行为保留** = 关中断死循环保现场（无人时 IWDG ≈33s 复位） |
| `pl_task_create_checked()` 判空 | `Platform/Inc/pl_task_guard.h` | **保留**（判空 + 保现场；RTT 打印已删）：通道/引擎启动点此前静默丢弃返回值 ⇒ RS485/RS232 无声失效 |

**现行排障步骤**（无 `[diag]` 输出）：

1. 先按 §8.3 账本逐项累加复算启动期堆预算，并与链接期段尺寸对账（原复算脚本
   `heap_ledger.py` 已随 `.analysis` 移除；需要时从 `069be96` 取回，在仓库外运行）；
2. 需要实测水位时，在 `app_rs485_start` 等启动点加一行 `SEGGER_RTT_printf(0, …)` 打
   `xPortGetFreeHeapSize()` / `xPortGetMinimumEverFreeHeapSize()`（**RTT 底层已备**，业务代码本身零调用）；
3. `vApplicationMallocFailedHook` 命中即关中断停机 ⇒ 调试器读 `g_fault_*` / `xLastFailedAllocSize` 现场。

### §8.6 纪律

1. **新增任务/动态队列/信号量/netconn 前**：先按 §8.3 账本逐项累加复算
   （自研 `[diag] heap … min` 打印已随 2026-09-29 瘦身轮删除；原 `heap_ledger.py` 脚本亦已随
   `.analysis` 移除，改由账本 + 链接期段尺寸核账）；
   本表 §8.3 不更新即视为未评审。
2. **可静态化的任务优先静态化**（`Platform/Inc/pl_task_static.h`，落 CCMRAM）——
   前提是「全生命周期只创建一次」；会被反复创建/退出的任务（每连接任务）保持动态，
   但创建点必须 `pl_task_create_checked` 判空。
3. `ucHeap` **仍留 SRAM**（`configAPPLICATION_ALLOCATED_HEAP=0`，见 §02 宪法）；
   本轮下沉的是**任务栈/TCB**，不是堆——「堆不得长期占 CCM」的约束未被突破。

### §8.7 策略级选项（**待用户裁决，本轮未实施**）

> 本轮修复是**外科式**的（只搬任务栈/TCB），以下两项是更大范围的策略选择，
> 涉及 DMA 审计与宪法条款，**不擅自实施**，列此供裁决。

**选项 A：`ucHeap` 整体迁入 CCMRAM（`configAPPLICATION_ALLOCATED_HEAP=1` + 自定义堆缓冲段属性）**

| 维度 | 事实 |
|---|---|
| 收益 | SRAM 再腾出 **36864B**（SRAM 余量 2136B → ≈39KB），彻底消除「任务多即堆爆」的结构性风险 |
| **容量硬冲突** | CCM 现余 **27204B** < 36864B ⇒ **装不下完整 36KB 堆**；需同时缩 `configTOTAL_HEAP_SIZE` 或把 CQ/GZ_OL/YN_OL 队列体（合计 8665B）迁回 SRAM，才刚好装得下 |
| 政策冲突 | 与 §02 §1③「**堆不得长期占 CCM**」直接冲突；该条正是 Phase A 从 CCM 回退 SRAM 的结论（`freertos_heap_ccm.c` 已删除，见 §02 §5 / 06-03） |
| **DMA 审计门禁（必须先做）** | 一旦堆指针可能进 DMA，即静默数据损坏。需逐个确认 ucHeap 分配物**从不**交给：`pl_spi_transmit/receive_dma`（W25Qxx）、`pl_uart_start_rx`（RS485/RS232 乒乓缓冲）、ETH DMA（`ETH_RxBuffers`）与 `netbuf_ref` 引用的载荷。**现状风险点**：LwIP 的 `ram_heap`/`memp` 池是独立静态池（不走 ucHeap，`MEM_SIZE`/`PBUF_POOL` 在 `.bss`），故 LwIP 侧大概率安全；真正的门禁是「**禁止把 `pvPortMalloc` 的返回值直接交给任何 DMA 外设**」这条纪律能否长期守住 |
| 建议 | **暂不实施**；若未来 SRAM 仍不足，优先用选项 B + 继续静态化，而非动堆 |

**选项 B：按实测水位收缩 `ucHeap`（`configTOTAL_HEAP_SIZE` 36KB → 24~28KB，释放 SRAM 给 Flash 之外的用途）**

| 维度 | 事实 |
|---|---|
| 依据 | 修复后「启动期峰值需求 13376B / 含运行期异步项 21920B」，稳态余量 **14936B**；即 24KB 口径下仍有 ≈3~4KB 余量（`min` 水位会给出权威值） |
| 风险 | CQ 的 cJSON 解析树是 **2~4KB 瞬时**分配（`cJSON_Parse` → `cJSON_Delete`），收缩后必须用「最大 JSON 帧 + 全部连接任务并发」压测 `xPortGetMinimumEverFreeHeapSize()`；`configUSE_TIMERS=0`（全项目无定时器调用点）可另省 1124B CCM + 240B SRAM |
| 收益 | SRAM 余量 2136B → ≈8~12KB，给未来 SRAM 需求留空间；**不触碰 DMA 门禁** |
| 建议 | 可选；实施前必须以 RTT `min` 实测标定，不得仅凭离线账 |

