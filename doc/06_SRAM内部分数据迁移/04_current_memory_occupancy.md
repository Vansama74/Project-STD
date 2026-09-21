# 04 当前 SRAM / CCMRAM 占用分布（实测）

> **角色**：本目录 **唯一占用实测账本**  
> **状态**：现行　|　采样：**EIDE Debug（GCC 15.2）** / 编入 **1-577 3×3**、排除 1-260 / 1-969 / RLS / AH_MQTT / **2026-08-14 16:37**（重建复核，数字未漂移）  
> **最新采样**：**2026-09-18 第三十二轮**（22_1665 删除自设 16KB CCM 预算；工作区宏值 8×2 = 128×32，见 **§8.4.2**）——
> `PROTO=ALL DISP=22_1665`（8×2）**`.text 175248 / .rodata 203296 / .data 1656 / .bss 124836 / .ccmram 58876
> （本模组 23040 = 1408×16 + 256×2）/ ._user_heap_stack 2564` ⇒ SRAM 合计 129056B（余 2016B）、CCM 余 6660B**；
> 同轮 `PROTO=ALL DISP=1_263` 余 2016B、`PROTO=CQ DISP=1_263` 余 6296B。
> 上一条采样（**2026-09-18 YN_OL TCP 现场修复轮**，见 **§8.4.1**）为
> `PROTO=ALL DISP=22_1665`（1×5）**`.text 174640 / .rodata 203152 / .data 1680 / .bss 124816 / .ccmram 44156 /
> ._user_heap_stack 2560` ⇒ SRAM 合计 129056B（余 2016B）**；`PROTO=ALL DISP=1_263` 余 2020B、`PROTO=CQ DISP=1_263` 余 6304B。
> 2026-09-14 全量重采见 §7；**1-263 现值已为 `MODULE_ROWS=7 × 32 / MODULE_COLS=2 × 32 = 224×64`（CCM 29568B）**，
> 本页 §0~§3、§5、§6 中按「1-263 = 224×128 / CCM 59136B」记账的数字为**历史口径**（`CLAUDE.md` 同处标注 `[待更新: 需用户裁决]` 已由该轮落地解决），**两套数字严禁混用**。  
> **迁移**：[05_migration_plan.md](./05_migration_plan.md)（Phase A + 后续瘦身已执行）  
> **RB / queue 语义**：doc/05

**结论**：SRAM **≈120820B（92.2%）**；CCM **38208B（58.3%）**。`ucHeap` @ SRAM **36KB**；CCM 仅显存。全局约 159KB / 192KB，空闲约 **37.5KB**。

---

## §0 采样口径

| 项 | 值 |
|----|-----|
| 构建 | **EIDE Debug**（Arm GNU Toolchain 15.2 / GCC）；`outDir=build` → `build/Debug/Project_STD.elf`（对象在 `build/Debug/.obj/`） |
| 模组（CCM） | 编入 **1-577 3×3**（`.eide/eide.yml` exclude 1-260 / 1-969）；CCM **38208B**，与 1-969 同量级 |
| 协议选编 | 编入 **IAP + LDI + 青海**；exclude **RLS / AH_MQTT** |
| 段合计 | `.data` 1612 + `.bss` 115620 + `._user_heap_stack` 3588 = **120820** |
| CCM | `.ccmram` **38208** |
| 结果 | **链接成功** |

与「仅 1-260 小屏」数字 **严禁混用**。**勿用 `make` 重采**：Makefile 现编 1-260 + RLS/AH（见 `03` §1），覆盖 elf 后本表数字不复现。

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

## 修订

- 2026-08-14：Phase A 后首测（130280 / 38208）。  - 2026-08-14 14:45：对齐 RB 1536/768/768、DMA 640、queue 深度表；重采 SRAM ≈120820。  
- 2026-08-14 16:37：更正采样口径为 **EIDE Debug**（原误标 Makefile/GCC Debug）；标注 RLS/AH 未编入与 Makefile 重采警告。
- 2026-08-14：新增 §5 四川三协议增量账（Makefile 口径：text +7624 / data +8 / bss +1144；SRAM 124192B=94.7%，未超 124KB 上限；CCM 增量 0；ucHeap 多 4 任务栈 4KB）。
- 2026-08-15：四川三协议显示语义对齐参考项目 9K1F212701（MTC 帧长双格式/治超 8 行与 0x80 全屏/ETC 6 行与 0x30 复位）+ TEST 键修复；重采 Makefile 口径 text +8264 / data +16 / bss +1208（治超行状态 64→128B、颜色状态 +2B）；SRAM 合计 **124264B（94.8%）**，未超上限。
- 2026-08-15（同日修订）：治超帧长度上限 0x1E→FF（对齐 9K1F212701，0xFE 显式排除防 RLS 吞帧）+ `SC_OL_PAYLOAD_MAX` 32→249 + MTC 单行行号 '1'~'4'→'1'~'6'；bss 重采 **125948（+2080）**、SRAM ≈**124915B（95.3%）**；build/Debug 曾发现陈旧 ELF（仅链四川协议），`make clean` 全量重建后恢复。
- 2026-08-15（ETC 全屏修复）：ETC 显示帧变长 0x0D 定界对齐 9K1F212701 etc.c（payload 64→149、全屏数据上限 56→145、全屏渲染先清屏再按屏宽换行）；bss 增量 +344（queue_buf +255、任务帧缓冲 +85、对齐 +4），重采 bss **126292（+2424）**、SRAM ≈**125259B（95.6%）**，未超上限。
- 2026-08-15（治超行帧变长修复）：81~88 行数据改变长（=总长-6，≤24B 截断、不足不补空格、先清行再渲染，对齐 9K1F212701 makefonttolatt_oneline）；`s_ol_lines` 8×16→8×24 + `s_ol_line_len[8]`（+72B）；亮度映射改 (val+1)/32；修复应答帧 BCC 计算 off-by-one；重采 bss **126364（+2496）**、SRAM ≈**125331B（95.6%）**，未超上限。
- 2026-08-17（发布前审查修复）：治超 payload 249→255 消除长帧队列截断越界读（bss +18）；MTC '8' 亮度兼容二进制/ASCII、'A' 4B/5B 判别加 b2 门限；ETC 黄闪 10 秒自动关闭计时（+4B）+ 车道关闭文案补全「请择道行驶」（rodata）+ 滚屏帧 0x0D 扫描起点改索引 6（rt/st 恰为 0x0D 不再误定界）；ETC/MTC 行渲染与 MTC '4'/治超 80 全屏渲染补先清后画（对齐 9K1F212701 MakeSixteenLattAll/OneLine 清屏语义）；ETC 0x40 亮度改 00~07→硬件 1~8（协议文档 §6，00 最暗）；重采 text **328280（+8616）**、bss **126396（+2528）**、SRAM ≈**125363B（95.7%）**，未超上限。
- 2026-08-17（MTC '}' 定界变长修复）：'{' 帧族废除定长双长度与 '78' BCC 校验（删除 BCC 函数与定长 switch），probe 改 '}' 扫描；修复上位机 `{3 1 1234 } 3 }` 单行乱码。queue payload 仍 74B、深度 3 → bss **不变**；text 重采 **327624（-656）**；SRAM ≈**125363B（95.7%）**，未超上限。宿主推演 `~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/sc_mtc_frame_sim.py`。
- 2026-08-17（ETC 心跳停用 + MTC '4' 全屏换行修复）：ETC 心跳计时任务 #if 0 停用、不再创建 → ucHeap 任务栈 **-1KB**（4→3）；「ETC车道关闭」5 分钟超时显示与黄闪 10 秒自动关闭一并失效（0A 38 开启后须 0A 39 显式关闭；0A 50 帧解析保留）；MTC '4' 全屏改整屏 word_wrap 渲染（原 16B/行切 ≤4 行，超宽溢出被裁、超 64B 丢弃）；data/bss **不变**（1620/126396），text 重采 **327408（-216）**；SRAM ≈**125363B（95.7%）**，未超上限。
- 2026-08-17（新增山东协议）：ProtocolParser_ShanDong 六文件接入 Makefile（EIDE Debug 目标排除）；§6 增量账：text **+1464**、bss **+1160**（queue 801 + cb 80 + 帧缓冲 267 + 状态 12）、data 不变；ucHeap 任务栈 +1KB（3→4）；SRAM ≈**126523B（96.5%）**，未超 124KB（126976B）上限。
- 2026-08-18（框架/驱动层缺陷修复）：UART RX 单缓冲 IDLE → **乒乓双缓冲 circular**（HT/TC+IDLE 增量冲刷，s_rs485_buf/s_rs232_0_buf 640→1280×2，bss +1280B）；channel_send 与 frame_dispatch_task 增加通道指针回验；W25Qxx 加 SPI 互斥；TCP Server 改串行单客户端、UDP/TCP 通道静态化（+32B）。重采（Makefile 口径）：bss **130036（+1312）**、text **334256（+1184）**、data 不变。
- 2026-08-17（默认显示注册机制 + 山东版本应答 PROGRAM_CODE）：新增 `app_default_display.c`（注册制默认显示，回退欢迎画面）+ `app_sd_proto_default.c`（山东上电画面注册）+ `app_boot.h`（PROGRAM_CODE 共用，开机画面与版本应答）；`'2'` 版本应答改裸 ASCII PROGRAM_CODE（删除占位 `SD_FX_1.0`）；重采 text **329112（+240）**、bss **127564（+8：s_default_fn 4B + 尾部对齐 4B）**、data 不变；SRAM ≈**126531B（96.5%）**，未超上限。
- 2026-08-17（文本常量字节数组 → UTF-8 字符串字面量）：山东/四川ETC 显示文本、四川MTC 固定标签与语音表、青海文明用语表全部改为 UTF-8 字面量（语音发送前 UTF8ToGBK 运行时转 GBK；显示改 FONT_ENC_UTF8）；山东上电画面 COLOR_GREEN→COLOR_YELLOW；青海文明用语 '0' 同时修正为 doc/04 裸机文本「贵州高速公路」（原数组字节有损）；青海自检/费额语音补 UTF8ToGBK 转换（原以 UTF-8 字节直送 GBK 语音板）；重采 text **329456（+344，rodata +149 + 转换代码 ~+195）**、bss **127564（+0）**、data 不变；SRAM ≈**126531B（96.5%）**，未超上限。
- 2026-08-17（dev_display_fill 越界下溢修复）：`dev_display_fill` 起点越界（x≥rows / y≥cols）早退丢弃，消除 `screen_* - x/y` uint16 下溢巨值写穿 pixel_map（CCMRAM）→ HardFault 的缺陷（山东 '3' 行号 ≥3 在小屏构建触发：y=row*16 ≥ 32 屏高；故障地址 0x10010000 = CCMRAM 末尾+1）；该修复同时保护青海/ETC/MTC 行渲染与 app_render 字形擦除路径；重采 text **329472（+16）**、bss/data 不变；SRAM ≈**126531B（96.5%）**，未超上限。
- 2026-08-17（新增贵州协议）：ProtocolParser_GuiZhou 四文件接入 Makefile（EIDE Debug 目标排除，与青海同列）；§6 增量账：text **+3600**、bss **+1160**（queue 801 + cb 80 + 帧缓冲 267 + 状态 12）、data 不变；ucHeap 任务栈 +1KB（4→5）；SRAM ≈**127691B（97.4%）**，未超 124KB（126976B）上限。宿主推演 `~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/gz_frame_sim.py`。
- 2026-08-24（新增云南协议）：ProtocolParser_YunNan 五文件接入 Makefile（EIDE Debug 目标排除，与青海/山东/贵州同列；PROTO=CQ 口径保留——`{` 帧族随既有纪律编入）；§6 对照构建增量账：text **+3120**、bss **+1168**（queue 801 + cb 80 + 帧缓冲 267 + 状态 12 + 对齐 8）、data 不变；ucHeap 常驻任务栈 +1KB（5→6），自检/熄灭任务瞬态；SRAM ≈**128891B（98.3%）**，未超 124KB（126976B）上限。构建验证：PROTO=ALL text 352916/data 1656/bss 137940；PROTO=CQ text 339060/data 852/bss 134464，均链接通过。
- 2026-08-24（云南协议修订，用户决定 1~10）：移除 `app_yn_proto_default.c`（上电效果 default 文件，Makefile/eide.yml 同步删除，不再注册默认显示）；'2' 自检改老化循环显示 + 每 5s 语音「系统正在自检」（可被下一帧打断，新增自检单字缓冲 static buf 256B）；'8' 亮度 0x00 恢复光敏自动；0x01 全屏点亮扩 01红~07白 七色；0x02 应答改 PROGRAM_CODE（删硬编码 YN_FX_P5_1.0）。重采：**PROTO=ALL text 352860（-56）/ data 1656 / bss 138196（+256）**；**PROTO=CQ text 338996（-64）/ data 852 / bss 134720（+256）**，均链接通过。
- 2026-08-20（新增重庆CQ协议）：ProtocolParser_ChongQing 四文件 + cJSON 接入 Makefile（恒定编入，CQ 源在 LDI 之后）；`PROTO=CQ` 剔除 LDI 目录 + `-DPROTO_CHONGQING`。Makefile 全协议口径重采：**text 348240（+12056，含 cJSON ~8.5K 代码）**、**data 1652（+32，cJSON hooks/全局指针）**、**bss 136456（+6436 = CCM +6377 + SRAM ~59）**。CQ 静态体构成（**全部置 `.ccmram`**）：queue_buf 3156 + queue_cb 80 + 任务帧缓冲 1052 + JSON 缓冲 1045 + 文本缓冲 1044 = **6377B CCM**；SRAM 净增 ~59B（mask/计数器/hooks/UDP_CQ 通道实例），全协议构建 SRAM 余量仍 >1.7KB，链接通过。ucHeap 任务栈 +2KB（cq_proto_handle_task / cq_proto_timer_task 各 1KB）。cJSON 解析树为 ucHeap 瞬时分配（钩子 pvPortMalloc/pvPortFree）。**CCM 例外说明**：本项目 CCM 惯例为「仅显存」，CQ 帧队列因 SRAM 余量不足（全协议构建仅余 ~1.9KB）破例入 CCM——缓冲为 CPU 独占访问（无 DMA/ETH），Makefile 1-260 口径 CCM 2432+6377=8809B / EIDE 1-577 口径 38208+6377=44585B，均不超 64KB。
- 2026-08-20（cJSON 升级官方 v1.7.18）：`Middlewares/Third_Party/cJSON/` 由裸机拷贝 v1.x 老版（编译 3 条 -Wmisleading-indentation 警告）替换为 DaveGamble/cJSON **v1.7.18** 官方源（GitHub codeload tarball）；parse 层类型判定改 `cJSON_IsString/IsNumber/IsObject` 辅助函数（`app_cq_proto_parse.c`），`cJSON_InitHooks` 换绑 FreeRTOS 堆不变（hooks 字段名仍 malloc_fn/free_fn），`cJSON_Parse`/GBK 字节直传用法不变。重采（Makefile 口径，cJSON 编译 warning 清零，仅余 HAL flash_ex 3 条既有 unused-parameter）：全协议 text **350004（+1764，cJSON 代码 ~8.5K→~10.2K）**、data **1656（+4）**、bss **136460（+4）**；`PROTO=CQ` text **335796（+1772）**、data **852（+4）**、bss **132984（+4）**。cJSON 解析树瞬时堆仍 ucHeap 内 2~4KB 量级（cJSON 结构体尺寸不变，钩子仍 pvPortMalloc/pvPortFree，解析后 cJSON_Delete 归还）。
- 2026-09-04（新增安徽协议）：ProtocolParser_Anhui 四文件接入 Makefile（EIDE Debug 目标**编入不排除**——帧头 0x5A 串口槽唯一，无 '{' 守卫）；动态显示（0x86~0x89 滚动）留空 TODO、0x96/0x97 录制语音占位。增量：text **+~3.6KB**、SRAM **+~1168B**（queue_buf 807 + cb 80 + 帧缓冲 269 + mask/句柄 12，静态 SRAM 与青海/贵州/云南同）、CCM **+0**；ucHeap 任务栈 +1KB。构建：**Makefile 1-263 模组下 SRAM 溢出 1136B**（预算先于本任务耗尽）——链接脚本 `_Min_Heap_Size` 1KB→512B、`_Min_Stack_Size` 2.5KB→2KB、RTT `BUFFER_SIZE_UP` 2KB→1KB 合计让出 2048B 后 PROTO=ALL text **356500**/data 1656/bss 194020（含 CCM 65516）、PROTO=CQ text **342636**/data 852/bss 190544，均链接通过（详见 doc/13 §6）。
- 2026-09-04（安徽内存回 SRAM，用户裁决）：安徽协议队列/控制块/任务帧缓冲由 `.ccmram` 移回静态 SRAM（与青海/贵州/云南一致，CQ 才是 CCMRAM 例外）；Makefile 模组保持 1-263（1-969 路线否决）；`Device/Display/dev_display_1_969.c` 还原 HEAD（1×1）。EIDE Debug 收录安徽四文件（virtualFolder + Debug incList，excludeList 不动）。链接期下限与 RTT 让位见上条。重采（`make clean` 全量）：PROTO=ALL text **356500** / data **1656** / bss **194020**（`.ccmram` 65516 = 1-263 显存 59136 + CQ 6377 + 3 对齐；`.bss` 125944、`._user_heap_stack` 2560 → SRAM ≈ **130160B（99.3%）**，余量 ~912B）；PROTO=CQ text **342636** / data **852** / bss **190544**（`.bss` 122468），均链接通过。
- 2026-09-07（安徽动态显示落地 app_scroll）：新增 `Application/Src/app_scroll.c`（通用动态滚动模块，Makefile SRC_APPLICATION + `.eide/eide.yml` Application/files 收录）；app_render 导出 `app_render_draw_glyph_clipped`/`app_render_glyph_width_px`；安徽 0x86~0x89 接线。增量：text **+1912**、bss **+376**（s_slots[4] 360B + 句柄 12B；无 CCM 对象）、data 不变；scroll_task 栈 1KB 惰性（ucHeap）。重采（Makefile 1-263 单模块 32×32 口径）：PROTO=ALL text **358348** / data **1656** / bss **137756**（SRAM ≈ 130536B，余量 ≈ 536B）；PROTO=CQ text **344508** / data **852** / bss **134280**，均 0 新增告警链接通过（详见本文件上方安徽动态显示增量账与 doc/01_显示系统/动态滚动显示实现记录.md）。
- 2026-09-08（④a 脏矩形 prepare + ⑤ 安徽侧渲染串行）：`dev_display_t` 基类新增 `dirty_rect_*` 字段（4×uint16 + bool + 填充 = **+12B**，每模组静态实例 1 份——1-263 实例带初始化器在 `.data`）；`dev_display.c` 新增 `dev_display_commit_frame_rect` + 待消费矩形暂存 **10B**（`.bss`）；app_scroll 新增 `s_render_mutex` 句柄 **4B**（372B→376B，`.bss`）——名义 +26B，链接后经段对齐空隙吸收，实测 text **+672**、data **+8**、bss **+8**（RAM 合计 +16B，CCM +0）。重采（同口径）：PROTO=ALL text **359036** / data **1664** / bss **137764**（SRAM ≈ 130544B，余量 ≈ 528B）；PROTO=CQ text **345180** / data **860** / bss **134288**，均 0 error（基线 18:24 产物为回绕修订后口径，增量与 doc/13 §6、doc/01 记录一致）。
- 2026-09-14（新增 22-1703 显示模组 + 贵州治超协议，全量重采）：① `Device/Display/dev_display_22_1703.c`（P10 32x16，料号 2200001703，1/4 扫描，224×64，合并写 BSRR；Makefile 新增 `DISP` 开关，与 1-263 编译期二选一——两模组显存 + CQ + 治超 = 68729B > 64KB 不可同编）；② `ProtocolParser_GuiZhou_Overload`（"TCLY" 帧族，RS485+RS232，队列/控制块/任务帧缓冲 **1136B 置 CCMRAM**——ALL 口径 SRAM 仅余 496B，CPU 独占访问无 DMA，CQ 先例）。Makefile 四口径重采（`arm-none-eabi-size -A`；**原流程为「每口径删产物强制重链接」，2026-09-14 已由 Makefile 口径指纹 stamp 取代**——切口径自动全量重编 + 重链接，见 doc/构建开关总表.md §4）：**PROTO=ALL DISP=1_263 text 163652 / rodata 197760 / data 1664 / ccmram 37084 / bss 126348 / heap_stack 2564（SRAM 130576B，余量 496B）；PROTO=CQ DISP=1_263 text 150476 / data 860 / ccmram 37084 / bss 122872（余量 4776B）；PROTO=ALL DISP=22_1703 text 163924 / rodata 197840 / data 1744 / ccmram 39164 / bss 126348（余量 416B）；PROTO=CQ DISP=22_1703 text 150732 / data 940 / ccmram 39164 / bss 122872（余量 4696B）**，全部链接通过、零新增告警。**采样更正**：`PROTO=ALL` 两行原记 text 164068 / rodata 197800（1-263）与 164324 / 197880（22_1703）系「对象沿用上一 CQ 口径」的污染态（+416 text / +40 rodata，符号级归因见 doc/构建开关总表.md §4.1），**RAM 各段不变**。本轮增量（干净重采，A/B 对照）：治超协议 text **+2128**（CQ 口径 +2144）/ rodata **+216** / bss +16 / **ccmram +1136**；22-1703 相对 1-263 text **+272** / rodata +80 / data +80 / **ccmram +2080**（合并表 2848 − g_bsrr 768）。**口径更正**：工作区 1-263 现为 `MODULE_ROWS=7 / MODULE_COLS=2 = 224×64`（CCM 29568B），本页早期各节「224×128 / 59136B / CCM 65516」为历史口径，详情见新增 §7。
- 2026-09-14（后续，贵州治超增绑 UDP 10011）：`gz_ol_proto_init` 第三个 mask 绑 `CH_ID_UDP`（挂 RJ45 共享槽 RB，**只 acquire 不 provide**，网口槽 1536B 仍由 IAP/LDI/CQ 提供）。四口径重采见 §7.1：`PROTO=ALL`/`PROTO=CQ` `DISP=1_263` text **163716 / 150524**、bss **126352 / 122876**（相对增绑前 **text +64 / +48、bss +4**），rodata/data/ccmram +0，SRAM 合计与余量逐字节不变（**496B / 4776B / 416B / 4696B**），四口径全部链接通过、零新增告警；默认口径 elf md5 `ef0eeaa2ede395242990ec8b49b7acd0`（重复 make 0 编译 0 链接、md5 不变）。⚠ 22_1703 两行的 `ccmram`（11420）系另一路 `dev_display_22_1703.c` 改动（显存降到 512B×2，模组 CCM 贡献 3904B）所致，与本模块无关，待该驱动定稿后重采（`[待更新]`，见 §7.1）。
- 2026-09-14（后续，贵州治超**专用 UDP 业务口**落地 = `CH_ID_UDP_GZOL`，用户裁决「`0x40` 端口语义忠实复现」）：`Application/Src/Channel/app_udp.c` 新增 `udp_gzol_task`/`udp_gzol_connect_task`/`_udp_gzol_read_port`（端口 = Sector1 `net_cfg.port`，与 TCP 业务口同号不同协议栈；空/0 回退 9528 + RTT 告警；与 10011/CQ 同号跳过绑定 + RTT 告警）；`app_dispatch.h` `CH_ID_MAX 8→9`；`app_boot.c` 在 `app_net_boot_apply()` 后 `app_udp_gzol_start()`；`gz_ol_proto_init` 增第四 mask（**10011 绑定保留**，两通道并存、应答各走收包通道独立 src 快照）。四口径重采见 §7.2：相对上一节诊断态 text **+752**、rodata **+296**、data **+8/+12**、bss **+40**（通道实例 20 + 告警闩锁 2 + 信号量 4 + mask 4 + `ch_proto_map[]/channels[]` 各 +4 + 对齐）、ccmram **+0**；SRAM 余量 496→**448B**（ALL）/ 4776→4720B（CQ），四口径全部链接通过、仅 3 条既有 HAL 告警。**LwIP 池维持 8/8**（常驻 netconn 5、峰值 8 = 池容量；UDP_PCB 常驻 3/8，净增 1 取自既有池、无静态增量）；ucHeap **+2KB**（两任务各 1KB，水位待现场 `xPortGetMinimumEverFreeHeapSize()` 标定）。默认口径 elf md5 **`5e3b1d73393307419d45bfa19215ca2d`**（重复 make 0 编译 0 链接）。详见 doc/14 §13 与 `.analysis/9k23881580/gz_ol_udp_service_port_report.md`。
- 2026-09-14（复测轮：字形裁剪 + 自证诊断设施，四口径重采）：① **字形越界语义改「按屏幕交集裁剪绘制」**
  （`dev_display.c` `dev_display_draw_bitmap`：起点越界仍丢弃、部分越界逐像素画可见部分；
  `app_render.c` 折行后行终止判定 `cur_y + line_h > cfg->h` → `cur_y >= cfg->h`）；
  ② **自证诊断设施** `APP_DIAG_BANNER`（`Application/Inc/app_diag.h`，默认 1，`make APP_DIAG=0` 关）：
  开机横幅（`fw`/`built`/`tree` + 口径 + `display screen=WxH` + `driver linked?` + 字库芯片 + `netcfg`
  + 四个 UDP 端口）+ 延迟体检（各通道实测端口与 `bind=OK/SKIPPED/FALLBACK/FAIL`）+ GZ_OL
  `0x20`/`0x40` 逐帧证据（`GZ_OL_RTT_DIAG` 跟随总开关）。**A/B 增量：text +2464 / rodata +2656
  （Flash +5120B）、data/bss/ccmram +0**；唯一行为差异 = 开机默认画面延后 2.5s（`osDelay(2500)`）。
  四口径重采（含诊断，见 §7.2 表；**复测轮 ② 加写诊断后已再次重采，见本条之后的记录**）：
  `PROTO=ALL` `DISP=1_263` text **167060** / rodata **200712** /
  data 1672 / ccmram 37084 / bss 126400 / heap_stack 2560 → SRAM **130632B（99.7%，余 440B）**；
  `PROTO=CQ` `DISP=1_263` text 153884 / rodata 200056 / data 872 / bss 122924（余 4712B）；
  `DISP=22_1703` 两口径 text 167316 / 154140、rodata 200792 / 200144、data 1752 / 952、
  **ccmram 39164**（该驱动文件当前 7×4 = 224×64 态；1×1 实验态为 11420，差值 27744B 可反推
  板上镜像几何）、bss 126400 / 122924 → SRAM 余 360B / 4632B；四口径全部链接通过、零新增告警。
  默认口径 elf md5 `57092a95337eb32d8b49e9583bd366a7`（⚠ 横幅内嵌 `__DATE__/__TIME__`，
  **重新编译必然改 md5**，镜像身份看 RTT 横幅）。本轮**未改任何业务逻辑代码**（§7.2 表已按新
  采样值替换，`[待更新]` 标记清零）；判读口径见 doc/14 §12.7/§12.8，报告
  `.analysis/9k23881580/field_triage_2432_and_port.md`。

- 2026-09-14（复测轮 ②：「`0x40` 改端口 10028 无响应」链路复核 + 诊断增强，四口径重采）：
  代码链逐段复核**无缺陷**（`0x40` 解析 → `net_cfg.port` 落库（`udp_port` 保留）→ 启动应用 →
  `CH_ID_UDP_GZOL` 读端口 → bind → 分发 → 单播回源）；唯一代码侧「端口静默回滚」机制 =
  `app_net_boot.c` `accept_write`（记录 CRC 坏 / IP 全 0 / port=0 → 默认值同时覆盖 IP 与 port）。
  本轮**仅加写 3 处只读诊断**（0x40 原始载荷 16B dump、横幅 `expect gzol` 字段、启动
  `netcfg INVALID` 告警），**`make APP_DIAG=0` 时与加写前逐字节同尺寸**。四口径重采（§7.2 表
  已替换）：text **167236** / rodata **201032** / data 1672 / ccmram 37084 / bss 126400 /
  heap_stack 2560 → SRAM **130632B（99.7%，余 440B）**（ALL `DISP=1_263`）；
  `PROTO=CQ` text 154060 / rodata 200384 / data 872 / bss 122924（余 4712B）；
  `DISP=22_1703` 两口径 text 167492 / 154316、rodata 201120 / 200464、data 1752 / 952、
  ccmram 39164、bss 126400 / 122924（余 360B / 4632B）。**诊断增量 A/B 更新为
  text +2640 / rodata +2976（Flash +5616B）、RAM +0B**（原 +2464/+2656；本轮诊断加写
  +176/+320）。默认口径 elf md5 `e71dec9267613fccd04add1bb9366154`（横幅内嵌编译时间，
  重编必改）。报告 `.analysis/9k23881580/port_10028_diagnosis.md`；现场镜像物证（17:54 EIDE
  产物）备份 `.analysis/9k23881580/artifacts_1754_eide/`。
- **2026-09-17（新增云南治超屏协议 YN_OL；**同日用户裁决轮（8 条）后重测**）**：`Application/Src/ProtocolParser_YunNan_Overload/`
  （`{` 帧族，YN_1.3.0；'6'/'7'/'9' 文档明示治超屏不开发 → probe 快拒；属 `{` 帧族 → 定义
  `g_brace_proto_guard` 互斥守卫，量产与其它 `{` 族目录二选一——**与云南常规这一对已 `arm-none-eabi-ld -r` 实测报
  `multiple definition`**，裁决 7）。**绑 RS485 + RS232 + TCP Server + TCP Client 四通道**（裁决 1：现场可能用网口控制）。
  队列体 + 控制块 + 任务帧缓冲 **1152B 置 `.ccmram`**（`s_yn_ol_queue_buf` 801 + `s_yn_ol_queue_cb` 80 +
  `msg_buf` 267 + 对齐 4；ALL 口径 SRAM 余量仅数百 B，CPU 独占访问无 DMA，CQ/GZ_OL 先例）。
  TCP 侧与 CQ JSON 的 `{` 同首字节竞争**由通道掩码隔离解决**（`app_dispatch` 按 `ch_proto_map[ch_id]` 过滤 →
  CQ 只绑 UDP/UDP_CQ、TCP 链上不被调用；本模块**不绑 10011** → CQ 量产行为零削弱；**未触碰 CQ 源码**）。
  新增 **`0x49` 屏体参数持久化**（裁决 5）：W25Qxx **独立 4KB 扇区 `capacity - 12288`** 12B 记录
  （magic `0x594E4F4C` + version + font_type + font_size + CRC32，仿 `app_ldi_cfg.c` 范式；LDI 占
  `capacity-4096`、app_render 显示持久化占 `capacity-8192`，三者各占一扇区 —— 因 `dev_w25qxx._write`
  对含非 `0xFF` 的目标区间做**整扇区读-改-写**，共享扇区会互擦）；SRAM 侧仅 **12B** 记录缓冲
  `s_yn_ol_cfg_rec`，**无 4KB 扇区镜像**（CCM 无新增）。上电默认画面已删除（裁决 6：原
  `app_yn_ol_proto_default.c` 与 5s 任务移除，走 STD 默认显示链路）。
  **增量（宿主重链同 `.o` 列表 A/B 实测，三口径一致）**：text **+3520**、rodata **+680~688**、data **+0**、
  `ccmram` **+1152**、bss **+36**（SRAM 合计 **+40B / +32B / +40B**）。
  **三口径段尺寸**（`arm-none-eabi-size -A`，Makefile GCC Debug，2026-09-17 裁决轮 `make clean` 全量重编）：
  `PROTO=ALL` `DISP=1_263` text **170596** / rodata **201736** / data 1672 / ccmram **11164** / bss 126436 /
  heap_stack 2564 → SRAM **130672B（余 400B）**，elf md5 `5095ba07ac53213aff135abf4d1706fd`；
  `PROTO=CQ` `DISP=1_263` text 157420 / rodata 201088 / data 872 / ccmram 11164 / bss 122960 / heap_stack 2560
  （余 **4680B**），elf md5 `a81cac0c45f62685e8aa369779e17fcb`；
  `PROTO=ALL` `DISP=22_1665` text 172460 / rodata 202896 / data 1672 / ccmram **10460** / bss 126452 /
  heap_stack 2564（余 **384B**），elf md5 `6556d01416d421f4c4f9a82aaac811e1`。
  **A/B 基线（无本模块）**：ALL/1_263 `text 167076 / ccmram 10012 / bss 126400 / 余 440B`、
  CQ/1_263 `text 153900 / ccmram 10012 / 余 4712B`、ALL/22_1665 `text 168940 / ccmram 9308 / 余 424B`
  （与既有文档基线一致，增量可归因）。
  > **注（2026-09-17 第二十二轮 22_1665 资料整理后复核）**：上表 `ALL/DISP=22_1665` 的段尺寸与
  > SRAM 余量（**384B**）**逐字节未变**；仅 elf md5 会随横幅内嵌 `__DATE__/__TIME__` 与源码树哈希变化
  > （注释整理使 tree → **`4bed24a7`**，机器码不变）——**镜像身份以 RTT 横幅 `fw=/built=/tree=` 为准**，
  > 段尺寸核对用 `arm-none-eabi-size -A`（见 `doc/构建开关总表.md` §4.2）。**SRAM 余量未被压负**（400B / 4680B / 384B 均 > 0）；
  > **注（2026-09-17 第二十三轮 22_1665 驱动重构后复核）**：上表 `ALL/DISP=22_1665` 行的绝对值已随驱动重构变化——
  > **`.text=170692` / `.rodata=201744` / `.data=1672` / `.ccmram=10460`（本模组 1792B 不变）/ `.bss=126436` /
  > `._user_heap_stack=2564` → SRAM 合计 130672B（余 **400B**）**，tree `f0fa31df`（较重构前 text −1768 / rodata −1152 /
  > bss −16，均来自删除驱动内 RTT 串、探针与上电自检、链表运行时状态数组）；`ALL/DISP=1_263` 与 `CQ/DISP=1_263` 不受影响。
  > 详见 `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` §0.7 与 `.analysis/22_1665/archive/reports/round23_refactor.md`。**SRAM 余量未被压负**（400B / 4680B / 384B 均 > 0）；
  > **注（2026-09-17 第二十四轮 22_1665 几何参数化后复核）**：上表 `ALL/DISP=22_1665` 行进一步变为
  > **`.text=170772` / `.rodata=201768` / `.data=1672` / `.ccmram=10460`（本模组 1792B 不变）/ `.bss=126436` /
  > `._user_heap_stack=2564` → SRAM 合计 130672B（余 **400B**）**，tree **`35b296d8`**（较第二十三轮 text +80 /
  > rodata +24：ASCII 断言消息 + 数据线表 `half` 字段 + `prepare` 模块索引算术；**data/bss/ccmram/SRAM 零变化**）。
  > **本模组 CCM 通式改为 `1408 × M + 384` 字节**（M = 模块总数，上限 16KB ⇒ **M ≤ 11**；1×1 = 1792B；
  > 扩屏实测整项目 ccmram：2×1 = 11868 / 2×2 = 14684 / 3×2 = 17500）。**SRAM 余量未被压负**（400B / 4680B / 384B 均 > 0）。
  > **⚠ 该行是第二十四轮的「模型 A + 自设 16KB 上限」历史口径**——现行（第二十九轮起）= 模型 B
  > `1408·M + 256·COLS`、**无自设上限**（唯一内存守卫 = ≤ 整片 CCMRAM 区域 64KB，第三十二轮，见 §8.4.2）。
  > **⚠ 上表「SRAM 余 400B / 4680B / 384B」与 `ccmram 10460 / 11164` 是 2026-09-17 堆修复之前的快照**——
  > 该轮把 23 个启动期任务的「栈 + TCB」下沉 `.ccmram`（见 §8），现行数字为
  > **ALL/1_263 ccmram 38332 / SRAM 余 2136B；CQ/1_263 ccmram 36076 / 余 6408B；ALL/22_1665 ccmram 39036 / 余 2136B**。
  三口径零新增告警（3 条既有 HAL）。ucHeap：`yn_ol_handle_task` **2026-09-17 起已静态下沉 CCMRAM（不再占 ucHeap）**；
  计入 ucHeap 的仅 1 个惰性任务（`yn_ol_selftest` 256×4 收 '2' 时创建自退；**原 `yn_ol_blank` 上电 5s 任务已随上电画面删除**）。
  宿主推演 `.analysis/yn_ol/yn_ol_frame_sim.py` **172 用例通过 / 失败 0**（含 §9 TCP 绑定与 CQ 链式竞争 38、
  §10 `0x49` 持久化契约 24、§11 四通道应答回源 7、§12 源码断言 6）；接入记录 doc/15（§8 = 8 条裁决结果表）。
- **2026-09-17 晚（YN_OL 联调第二轮：通道层/调度层诊断 + 通知投递缺陷修复）**：`app_tcp_server.c` /
  `app_tcp_client.c` 新增门控通道日志（`TCP_SRV_RTT_DIAG` / `TCP_CLI_RTT_DIAG`）、`app_dispatch.c` 新增
  `{` 帧 probe 决策日志（`PROBE_DBG_MIN_MS` 限速）并把通道通知投递改为「立即 + `DISPATCH_NOTIFY_RETRY_MS`
  （20ms）重试 → 仍失败 `notify_drop++` + 门控告警」（新增 `app_dispatch_notify_drops()` getter，
  `app_boot` post-boot 行打印基线）。**不新增任务/队列/动态对象 ⇒ §8.3 ucHeap 预算表不变**；
  默认口径同口径再采 `PROTO=ALL` `DISP=1_263` = text **174996** / rodata **204848** / data 1680 /
  bss 124772 / ccmram 38332 / heap_stack 2564（SRAM 合计 129016B，**余 2056B**；上一轮联调基线为
  text 173556 / rodata 203840 / bss 124732 / 余 2104B），`APP_DIAG=0` 对照 = text 169124 / rodata 198672 /
  bss 124736 / 余 2104B（即新增诊断部分 text +1392 / rodata +1008 / data +8 / bss +36；通知修复常开部分
  text +48 / bss +4，`APP_DIAG=0` 时不消失）。三口径（含 `DISP=22_1665` 工作区 1×5 与 `APP_DIAG=0` 对照）
  全量重编 exit=0、零新增告警；`APP_DIAG=0` 后新增诊断字符串 0 条。详见 `doc/15` §11.3~§11.5、
  `doc/构建开关总表.md` §1/§5 与 `.analysis/yn_ol/round2_channel_dispatch_diag.md`。
  ⚠ 本轮同时实测到 `doc/构建开关总表.md` §4.2 R1 的一个更强形态：**EIDE 覆写 `build/Debug/` 产物后，
  即使 EIDE 口径与 make 口径不同，`make -j8` 也 0 编译 0 链接、静默复用 EIDE 的 elf**（必须
  `make clean && make`）。

---

## §8 FreeRTOS 堆预算表与任务栈 CCMRAM 下沉（2026-09-17，现场堆耗尽修复）

> **角色**：`ucHeap`（`configTOTAL_HEAP_SIZE = 36KB`）的**永久预算账**。
> 任何新增任务 / 动态队列 / 信号量 / netconn **之前**先看本表，再看现场 RTT `[diag] heap …`。
> **复算入口（唯一权威脚本）**：`.analysis/heap/heap_ledger.py`
> （`--markdown` 出表 / `--ab` 云南治超 A/B / `--eide` EIDE 口径）；三口径复编脚本
> `.analysis/heap/verify_builds.sh`，输出留档 `.analysis/heap/verify_builds.out.txt`、
> `.analysis/heap/ledger_all.txt` / `ledger_ab.txt` / `ledger_eide.txt`。

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
| `factory_monitor_task` | 1KB | `app_factory_test.c`（sw3） | 1144B |
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
`cmsis_os2.c` 的 `__WEAK` 版把缓冲放在自己的 `.bss`（SRAM 1736B）；`app_boot.c` 以**强定义覆盖**，
改置 `.ccmram`（`s_idle_task_stack/tcb` 612B + `s_timer_task_stack/tcb` 1124B）
⇒ **SRAM `.bss` −1732B**（槽位对齐后），这是**本轮到 SRAM 侧的唯一净收益**。

**CCM 侧成本核算**：23 任务 = 22×1024 + 512（栈）+ 23×100（`StaticTask_t`）= **25340B**；
加空闲/定时器 **1736B** = **27076B**；`.ccmram` 全片实测见 §8.4（ALL/1_263 **38332B / 65536B**，余 **27204B**）。
`.ccmram` 上限守卫：**第三十四轮起驱动内无任何编译期守卫**（22_1665 的 17 条 `_Static_assert` 全删，
含「本模组数组 ≤ 整片 CCMRAM 区域 64KB」——见 §8.4.3），**只剩链接期溢出兜底**
（`region .ccmram overflowed by N bytes`）；**新增下沉前先看剩余量**。

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
  **其他消费者 35836B**（= 58876 − 23040；含 §8.2 静态任务栈 25340 + 内核缓冲 1736 + CQ 6377 +
  GZ_OL 1136 + YN_OL 1152 + 对齐）⇒ **本模组实用上限 ≈29700B**：COLS=2 时 M ≈ 20
  （10×2 = 160×32，28672B、整片余 1028B）为上限量级〔**估算，以链接期为准**〕；9×2（25856B、余 3844B）较稳。
* **⚠ 时序软约束（非编译期、本轮未改定时参数）**：8×2 每帧 1024 时钟 ⇒ `_22_1665_scan`
  ≈**0.36~0.48ms**（占 TIM3 固定 **500µs** 帧周期的 **72%~96%**）、`prepare`（整帧提交重算）
  ≈**1.0~1.2ms**（跨帧掉帧）——**须上机 DWT 实测复核**；超标的可选方向（增大 TIM3 周期降刷新率 /
  后续去掉 `chain_dst` 全量预计算表）**本轮均未实施**（见 doc/01 §0.7 与附录 A.19）。
* 三口径链接通过、零新增告警（仍 3 条既有 HAL `-Wunused-parameter`）；`verify_all.sh` 全量
  **20 项全绿**（含新增 `check_round32_ab.sh` 与换口径重链 `mod8x2`）。日志
  `.analysis/22_1665/build_round32{,_1_263,_cq,32b_22_1665}.log`、报告 `.analysis/22_1665/round32_ccm_limit_removed.md`。

### §8.4.3 2026-09-20 第三十四轮复采（22_1665 取消编译期防御 + 精简注释；工作区宏值 8×4 = 128×64）

**改动性质**：22_1665 驱动**删除全部 17 条 `_Static_assert`**（含第三十二轮保留的「本模组数组 ≤ 整片
CCMRAM 区域 64KB」物理守卫）+ 仅断言使用的 2 个派生宏 + **精简注释**（441 → 372 行）。
**零代码、零新增/减少静态量** ⇒ **驱动对象逐 section 逐字节不变**（20 个段 md5 全同 + 反汇编 366 行 0 差异），
本节的 SRAM/CCM 账**完全不变**；变的只有「错误几何的兜底方式」：由编译期断言改为**注释契约 + 链接期报错**。

| 口径（Makefile GCC Debug，`APP_DIAG_BANNER` 默认开，第三十四轮实测） | `.text` | `.rodata` | `.data` | `.bss` | `.ccmram` | `._user_heap_stack` | SRAM 合计 | **SRAM 余** |
|---|---|---|---|---|---|---|---|---|
| `PROTO=ALL` `DISP=1_263` | **174976** | **203040** | 1656 | 124836 | 38332 | 2564 | **129056** | **2016B** |
| `PROTO=CQ` `DISP=1_263` | **161796** | **202384** | 856 | 121360 | 36076 | 2560 | **124776** | **6296B** |
| `PROTO=ALL` `DISP=22_1665`（8×4） | — | — | — | — | 81916（本模组 **46080** = `1408×32 + 256×4`） | — | — | **链接期溢出 16380B，无 elf** |
| `PROTO=CQ` `DISP=22_1665`（8×4） | — | — | — | — | 79660（本模组 46080） | — | — | **链接期溢出 14124B，无 elf** |

* **前两行与第三十二轮记录逐值相同**（`ALL/1_263` `.text 174976 / .rodata 203040 / .ccmram 38332`、
  `CQ/1_263` `.text 161796 / .rodata 202384 / .ccmram 36076`）——两口径**不编 22_1665 驱动**，印证「本轮的
  断言/注释删除不产生代码」；四口径均**零新增告警**（仍 3 条既有 HAL `-Wunused-parameter`）。
* **⚠ 工作区宏值 8×4 的现实（预存状态，非本轮引入）**：本模组 **46080B** + 其他消费者 **35836B**
  （= 静态任务栈 25340 + 内核缓冲 1736 + CQ 6377 + GZ_OL 1136 + YN_OL 1152 + 对齐）= **81916B > 64KB**
  ⇒ `PROTO=ALL` 溢出 **16380B**；`PROTO=CQ`（其他消费者 33580B）溢出 **14124B**。
  第三十三轮已按算术预测、本轮实测确认（`ld` 报错文本在改动前/后**逐字节一致**）。
  **⇒ `DISP=22_1665` 在 8×4 下没有任何 make 口径能产出 elf/hex**，只有 **EIDE Debug 子集**能装
  （57484B / 余 8052B，第三十三轮实测）。**要让 make 出 22_1665 产物**：几何减到 8×2（23040B，
  ALL 口径 58876B，`link_variant.sh mod8x2` 实测链接通过）或让出 CCM（CQ/治超队列 / 静态任务栈）——
  均属**待用户决策**（`doc/01` §6）。
* **验证入口**：`bash .analysis/22_1665/verify_all.sh`（全量 **20 项全绿 / 1 项跳过**：`check_flash_artifact.sh`
  因上述「无 22_1665 make 产物」显式记为「跳过（前置不满足）」）；`check_compile_matrix.sh` C 节新增
  「47×1 = 66432B ⇒ 链接期 `overflowed by 896 bytes`」的链接期兜底回归；A/B 脚本 `check_round34_ab.sh`（5/5）。

### §8.5 永久诊断与错误处理（本轮一并落地）

| 设施 | 位置 | 作用 |
|---|---|---|
| 开机横幅 `[diag] heap at banner free=… min=…` | `app_boot.c` `app_diag_boot_banner` | 一条 RTT 行给出当前余量与**开机以来最小余量** |
| 逐通道 `[diag] heap after <chan>_start free=… min=…` | `app_boot.c` `init_task` | 每个通道启动后各打一次，堆耗尽可**不经调试器**定位到具体启动点 |
| `[err] pvPortMalloc FAILED: request=… free=… min=… ucHeap=…` | `app_boot.c` `vApplicationMallocFailedHook` | 失败请求字节数由 `heap_4.c` 诊断补丁 `xLastFailedAllocSize` 导出；打印后关中断死循环**保留调用栈供调试器取证**（无人时 IWDG ≈33s 复位） |
| `[err] task '<name>' create FAILED (heap exhausted): free=… min=…` | `Platform/Inc/pl_task_guard.h` `pl_task_create_checked()` | 通道/引擎启动点判空并报错（此前静默丢弃返回值 ⇒ RS485/RS232 无声失效） |

**现场验证步骤**（RTT 通道 0）：

1. 复位后在 RTT 看横幅 `[diag] heap at banner …` 与各 `after …_start` 行；
   `min` 一旦接近 0 即说明启动期擦边（本轮的判据）；
2. 在 `app_rs485_start` 下断点，确认 `pl_task_create_checked(osThreadNew(...))` 返回值**非 NULL**；
3. 需要看堆稳态时，在 `app_diag_boot_late()` 的 `post-boot` 行读取 `min`（启动期最低水位）。

### §8.6 纪律

1. **新增任务/动态队列/信号量/netconn 前**：先跑 `python3 .analysis/heap/heap_ledger.py` 复算，
   再在 RTT 看 `[diag] heap … min`；本表 §8.3 不更新即视为未评审。
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

