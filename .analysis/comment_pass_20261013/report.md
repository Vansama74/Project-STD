# 代码卫生清理轮报告（2026-10-13）

范围：① 业务代码 RTT 调试清理（保留底层）；② 新疆模块编译期检查清除；③ 新增 Cursor 规则；
④ 全项目注释审查与精简。**只改注释与上述指定代码点，未 commit、未改 git 配置、未烧录。**

构建口径：`make -j16 BUILD_DIR=build/cmt_before/Debug`（PROTO=ALL DISP=1_263 FACTORY=STD）
与 `... PROTO=CQ`；改后同口径重编到 `build/cmt_after*`。工具链 Arm GNU 15.2.1。

---

## ① RTT 清理明细（A 项）

### 现状复核（全仓 grep，`Application/Device/Platform/Kernel/Core/Compiler`）

`SEGGER_RTT_*` / `RTT_printf` / `pl_rtt_*` / `APP_DIAG` / `*_RTT_DIAG` / `[diag]` `[disp]` `[tcp_*]`
`[gz_ol]` `[yn_ol]` `[mm]` `[xj]` / `dump` / `probe_dbg` —— **业务代码命中 0 处**
（唯一命中 = `Platform/Src/pl_rtt.c:11` 的 `SEGGER_RTT_Init()`，属底层，按要求保留）。
即 2026-09-29 瘦身轮的删除成果完整，本轮**无 RTT 调用可删**。

### 本轮修正的过时/不正确注释（4 处，均为「与实态不符」而非新增删除）

| 文件:行 | 原述（过时） | 现状更正 |
|---|---|---|
| `Core/Src/syscalls.c:37` | 「RTT 调试通道已移除（2026-09-29）…替代此前由 SEGGER_RTT_Syscalls_GCC.c 承接」 | 「printf 无落地点…业务代码不用 printf；调试走调试器」（RTT 底层已于同日第二轮恢复） |
| `Platform/Inc/lwipopts.h:41` | 「原输出经 printf → _write 落到 SEGGER RTT（RTT 设施已随本轮移除）」 | 改为「LwIP 日志整体关闭（开关宏 = 0，见 doc/17）」 |
| `Application/Src/Channel/app_rs485.c:111` | 「判空仅作防御 + RTT 报告」 | 「判空属防御性冗余」（`pl_task_create_checked` 已无打印） |
| `Application/Src/ProtocolParser_YunNan/app_yn_proto_cmd.c:227` | 「判空 + RTT；…」 | 「判空：…」（同上） |
| `Device/Display/dev_display.c:49`、`Device/Inc/dev_display.h:142` | `dev_display_guard` 文档写「限速告警（≥1s 一行）…灌满 RTT 上行缓冲」 | 改为实际行为：返回 false、调用方安全跳过；`what` 仅作调用点标识（**不存在 `[disp]` 打印**） |

### 例外判定（A.3：存疑保留）

- **`dev_display_guard()` 本体保留**：它是「无显示驱动构建」的运行时降级防护（返回 bool，非调试设施），
  且 2026-10-08 的 EIDE 结构不变量与 `doc/16` §10 依赖它；其**告警打印早已不存在**。
- `pl_tim_dbg_freeze()`（`Platform/Src/pl_tim.c`，`__HAL_DBGMCU_FREEZE_TIM3/4`）保留：属「调试器挂起时冻结
  定时器」的硬件行为，不影响正常运行时功能与体积（**存疑项，未删**）。
- 保留的只读观察 API（2026-09-29 已列「待裁决」，本轮**未删**）：
  `app_dispatch_qfull_drops/resync_count/notify_drops`、`app_render_chip_info_get`、
  `app_render_glyph_probe`、`app_udp_gzol_bind_status(_str)`、`g_tcp_server_connected`、`s_mm_lamp_state`。

### 保留的 RTT 底层清单（未改、未删，mtime 复核）

| 项 | 状态 |
|---|---|
| `Middlewares/Third_Party/SEGGER_RTT/{SEGGER_RTT.c, SEGGER_RTT_printf.c, SEGGER_RTT_Syscalls_GCC.c}` | 未动（mtime 2026-07-16） |
| `Platform/Src/pl_rtt.c` | 未动（mtime 2026-09-29） |
| `Makefile` `SRC_RTT` + `-I Middlewares/Third_Party/SEGGER_RTT` | 未动（mtime 10-12 15:17，非本轮） |
| `.eide/eide.yml` RTT 收录（3 源 + 2 处 includePath） | 未动（mtime 09:06，非本轮；`git diff` 内容为其它任务在飞改动） |

### 段尺寸变化

**零变化**（见 §⑤ A/B）。`doc/06-04` 与 `doc/CLAUDE.md` 的现行基线数字**不需要同步**；
`doc/CLAUDE.md`「业务代码零 RTT API 调用」表述经 grep 复核**与实态一致，未改**。

---

## ② 新疆模块编译期检查（B 项）

### 用户点名位置的性质判定

`Application/Src/ProtocolParser_XinJiang/app_xj_proto_cmd.c:250-259`（当前工作区版本）内容 =
**预制语音编号/文案两张表 + 表注释**，不是编译期检查。按行距离最近的检查是 **`265-266` 的
`_Static_assert(XJ_PRESET_COUNT == …)`**（编号表与文案表等长同序）。结合文件在 2026-10-12 两轮
改动中行号整体位移，判定用户所指 = **该类防御性编译期检查**（非该两行为断言本身）。

### 处置（用户裁决：业务逻辑内不得有防御性编译期检查）

新疆模块共 **12 条** `_Static_assert`（`app_xj_proto.h` 11 + `app_xj_proto_cmd.c` 1），**全部删除**，
逐条改写为「一句人话契约 + doc 指针」（源码位置见下），细则记入 **`doc/19_新疆费显协议/README.md` §11**：

| 源码位置 | 现契约文字（一行） |
|---|---|
| `app_xj_proto.h` 帧常量区 | 「契约：帧上限 ≤ 串口 RB 容量；且 < 治超帧长字段 0xDC(220)——保双方首字节 0xFF 重叠时快拒不卡链。改错静默出错帧/链阻塞，见 doc/19 §1、§3.2。」 |
| `app_xj_proto.h` 'T' 段 | 「契约：'T' 整帧长 = 内容起始 9 + 16N + BCC 1 = XJ_FRAME_LEN_MAX。改错静默出错帧，见 doc/19 §1.2 / §2.3。」 |
| `app_xj_proto.h` 'S' 段 | 「契约：报价整帧长 = 载荷起始 6 + 各形态载荷长 + BCC 1（5/10/11B）。改错静默出错帧（probe 不认领），见 doc/19 §1.2 / §2.6。」 |
| `app_xj_proto.h` 'D' 段 | 「契约：'D' 行显示整帧长 = 6 + 16 + 1；全屏形态 6 + 16N + 1（N=2~6 ⇒ 39~103），文档表形态 71B 须落在该区间内。改错静默出错帧，见 doc/19 §1.2 / §2.3。」 |
| `app_xj_proto_cmd.c` 预制语音表 | 「契约：编号表与文案表逐项同序等长（同下标 = 同一条文案）。改错播错语音，见 doc/19 §2.6。」 |

**语义确认**：删除后全量重编通过（`PROTO=ALL` / `PROTO=CQ`）；断言不产码，段级逐字节一致（§⑤）。
**严格边界遵守**：未触碰 `Device/Display/dev_display_22_1665.c` 的 2 条守卫（通道上限 / ≤64KB）。

### 全项目其它同类编译期检查清单（未处理，交用户决定是否扩展）

完整清单（file:line + 类型 + 内容）：`.analysis/comment_pass_20261013/static_assert_detail.txt`
（共 **73 条 `_Static_assert`/`static_assert` + 1 条 `#warning`**，分布于 23 个文件）：

| 文件 | 条数 | 性质（一句话） |
|---|---|---|
| `Application/Src/LDI/app_ldi_cmd.c` | 22 | LDI 协议结构体字节布局/偏移锁定（D1H network 65B 等） |
| `Application/Inc/Config/app_board_net_cfg.h` | 11 | Sector1 记录 72B 布局 + 各字段偏移（三固件二进制兼容） |
| `Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h` | 5 | 帧长 ≤ 三槽 RB + 0x49 记录 12B |
| `Application/Src/app_dispatch.c` | 4 | `g_rb_provide` 表长度 / 名称表 / 槽序 / RB 容量与产品口径一致 |
| `Device/Display/dev_display_22_1665.c` | 2 | **用户明确要求保留**：通道表上限 + 本模组 ≤ 64KB |
| `Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h` | 3 | 帧长 ≤ 三槽 RB |
| 12 个协议模块（QH/SD/GZ/YN/ANHUI/ETC/MTC/SC_OL/CQ/MM/RLS/…） | 各 1~2 | 各协议帧长上限 ≤ 对应 RB 容量 |
| `Application/Src/IAP/app_iap_cmd.c` | 1 | IAP 命令表条目数与 `IAP_CMD_COUNT` 同步 |
| `Application/Src/LDI/app_ldi.c` | 2 | `ldi_device_t` / `ldi_cmd_type_t` = 1 字节 |
| `Application/Inc/LDI/app_ldi_cfg.h` | 1 | `app_flash_ldi_module_cfg_t` = 12B |
| `Application/Inc/LDI/app_ldi_cmd.h` | 1 | `ldi_device_info_t` = 17B |
| `Application/Inc/protocol_select.h` | 1（`#warning`） | 该头文件已废弃（协议切换改走 EIDE 目录编入） |

建议（供裁决）：协议「帧长 ≤ RB」一族属**改错必报错**、零信息量的自证型检查（同族共 ~20 条），
若要扩展删除，优先从这一族开始；布局/偏移一族（`app_board_net_cfg.h`、`app_ldi_cmd.c`、
`app_ldi_cfg.h`、`app_ldi_cmd.h`）涉及三固件二进制兼容，建议保留。

---

## ③ Cursor 规则（C 项）

- 新增 **`.cursor/rules/no-rtt-debug.mdc`**（此前 `.cursor/rules/` 为空目录）：
  frontmatter `description` + **`alwaysApply: true`**（确保对全部会话生效；未用 globs，
  避免规则只在特定文件被 Auto Attach 才加载）。
- 内容摘要（中文）：用户未明确要求时不得在业务代码新增 — `SEGGER_RTT_*` 调用与 `SEGGER_RTT.h` include；
  `pl_rtt` 打印封装 / `*_RTT_DIAG` / `APP_DIAG` / `[xxx]` 诊断打印与限速告警宏；调试用 dump/探针/自检函数
  与仅调试的 include。需要临时观察 → 优先调试器（Watch / 断点 / `g_fault_*`）；确需打印 →
  **先征得用户明确同意**并说明回退方式。RTT 底层（三源文件、`pl_rtt.c`、`SRC_RTT` 与 `-I`、EIDE 收录）
  属基础设施**不得删改**。附纪律背景指针（doc/17、doc/18）。
- `doc/CLAUDE.md` 改动行（2 处，最小改动）：
  1. 「代码风格」新增一条：注释规范判据改写为「**保留**：文件头 ≤10 行、函数/参数/返回值/单位/范围、
     表/宏/枚举字段语义、一句话硬约束；**禁止**：调试说明、日期化历史修复记录、轮次叙事、A/B 测量与
     段尺寸/md5、踩坑与事故叙述、被注释掉的代码块——一律写入对应 `doc/xx_*/README.md`」；
  2. 「代码风格」再新增一条：「**业务代码默认禁止新增 RTT 调试**（Cursor 规则
     `.cursor/rules/no-rtt-debug.mdc`）…RTT 底层不得删除/改动」；并把「调试」段中
     「需要临时打点时直接加 `SEGGER_RTT_printf(0,…)`」改写为「改用调试器；确需打点须先征得用户同意」。

---

## ④ 注释清理（D 项）

### 范围与方法

- 范围：`Application/**`、`Device/**`、`Platform/**`、`Kernel/**` 的 .c/.h；`Core/**`、`Compiler/**`
  仅自研部分（`syscalls.c`、`stm32f4xx_it.c`；CubeMX/第三方原样）。
- 方法：先脚本扫描（`.analysis/comment_pass_20261013/scan_candidates.py`，关键词 = 日期 / 轮次 /
  修复 / 探针 / RTT / A/B / md5 / 段尺寸 / 历史口径 / 坑 / `#if 0` / 实测 / 归档），
  再逐文件读上下文处置；**只改注释**（`#if 0` 与注释掉的调用按「停用开关」保留 + 一句用途）。
- 候选规模：**61 文件 / 239 行 → 29 文件 / 53 行**（剩余 = 误报或有意保留，见下）。
  扫描前后清单：`candidates_before.txt` / `candidates_after.txt`。

### 统计（改动文件 56 个；代码 51 / 文档 3 / 规则 1 / 本目录 1）

- **初始为 clean 的代码文件**：注释行 **3576 → 3511（净 −65 行）**（逐文件增减见文末附表口径）。
- 新疆模块（untracked，无 HEAD 基线）：头文件 −7 注释行（`_Static_assert` 12 → 契约 5 行）、
  `app_xj_proto_cmd.c` −1；另 `app_xj_proto.c` 去现场事故叙事。
- 起始即在飞（git status 已 `M`）的文件（`app_dispatch.c/.h`、`dev_display_1_263.c`、
  `app_mm_proto_cmd.c`、`doc/CLAUDE.md` 等）其行数变化含其它任务改动，**不计入上述 −65**。

### 已处理（按模块）

LDI（`app_ldi.c`/`app_ldi_cmd.c`）、`app_net_boot.c`、板级配置（`app_board_net_cfg.c/.h`）、
IAP（`app_iap_cmd.c`）、调度（`app_dispatch.c/.h`）、TCP Server/Client、UDP 三实例、RS485、
滚动（`app_scroll.c/.h`）、渲染（`app_render.c/.h`）、测试（`app_test.c`）、
安徽（cmd/parse/头）、贵州（cmd/parse/voice）、云南（cmd/parse/头）、四川 ETC（3 文件）、
MTC（cmd）、治超（cmd）、重庆（proto/cmd/头）、缅甸（cmd）、山东（cmd）、RLS（cmd）、
新疆（3 文件）、显示（`dev_display.c`、1_263、1_969、22_1665、22_1703、头）、
存储（`dev_w25qxx.c`）、Platform（`lwipopts.h`、`pl_sys.c/.h`、`pl_eth.c`）、
Core（`syscalls.c`、`stm32f4xx_it.c`）。

### 被移出信息的落点

见 `.analysis/comment_pass_20261013/notes_from_code.md`（两类：①「已确认文档已有 → 注释直接删」22 行表；
②「本轮新补/记于此」4 项，主要是 `doc/19` §11 与 `doc/18`/规则）。

### 未处理（有意保留，宁留不删）

以下 53 行仍会被扫描命中，但经逐条判断**属应保留项**：

1. `ProtocolParser_SiChuang_ETC/*` 的 `#if 0` 心跳任务与其「恢复方法」（**停用开关语义**，保留一句用途）；
2. `dev_display_22_1665.c` 的 `-D` 行为开关说明、`A/B/C/D` 行址线（关键词误报）、现场逐脚定标事实；
3. 协议文档版本日期（如「贵州常规费显协议（2020-06-17 修订）」= 源文档版本，非本工程历史）；
4. 「现场 DIP1 置 OFF」「待联调校准」「待厂商确认」等**操作约束 / 待确认标记**（§2.7 纪律，禁止删除）；
5. `pl_task_guard.h` / `FreeRTOSConfig.h` / `pl_hub75.h` 等**误报**（示例代码行、内核注释、A/B 数据线名）；
6. `Kernel/Src/ring_buffer.c` 的「C 整数提升」一句（属紧贴代码的硬约束 why，保留）；
7. `app_render.c` 字库只读观察 API 说明（该 API 属 2026-09-29「待裁决保留」，注释同步改为不带调试措辞）。

---

## ⑤ 构建 A/B 与脚本（零回归证据）

### A/B（各自独立 `BUILD_DIR`，全量重建）

| 口径 | 段 | 改前（cmt_before） | 改后（cmt_after） | 差 |
|---|---|---|---|---|
| ALL / 1_263 / STD | .text / .rodata / .data / .ccmram / .bss / heap_stack | 172056 / 196264 / 1656 / 41324 / 125388 / 2564 | **完全相同** | 0 |
| CQ / 1_263 | 同上 | 158652 / 195592 / 852 / 39068 / 121900 / 2564 | **完全相同** | 0 |

- **段级逐字节**（`sec_ab.py`，排除 `.debug*`/`.comment`/`.ARM.attributes`）：两口径各 **15/15 段一致**；
  新疆三个 `.o` 亦为 **45/45、51/51 段一致**（断言删除零产码）。
- **strip-debug elf md5**：`ALL f0a40384f2763d7489f3b3c05f911a69`（改前=改后）、
  `CQ 123036c44d4c918c7008cad38a0e5697`（改前=改后）⇒ **程序镜像逐字节一致**。
- 未 strip 的 elf md5 **必然不同**（`34b44391…` / `dda3afab…` vs 改前 `37bbb9d1…` / `6f7023cb…`）：
  仅 `.debug_*` 行号信息变化（`debug_info` −3/+0 B 级、`debug_abbrev` −23 B 等），**非代码差异**。
- **告警**：两口径全量重建均 **3 条**（既有 HAL `stm32f4xx_hal_flash_ex.c` `-Wunused-parameter`）⇒ 零新增。

### 脚本通过数

| 脚本 | 结果 |
|---|---|
| `.analysis/xinjiang/xj_frame_sim.py` | **322/329**，7 条失败**全部是「源码断言存在性」检查**（断言已按本裁决删除）：帧长 ≤ RS485/RS232 RB（编译期）、'T' 口径自证、上限 < 0xDC、'D' 全屏 39/103/区间——**功能推演 322 条全绿**。该脚本位于 `.analysis/xinjiang/`（本任务禁改，属并行任务地盘）⇒ **需由该目录的负责轮次更新这 7 条检查**（改为查「契约注释存在」或直接删除） |
| `.analysis/myanmar/mm_frame_sim.py` | 157/157 |
| `.analysis/yn_ol/yn_ol_frame_sim.py` | 184/184 |
| `.analysis/myanmar/check_display_guards.py` | 16/16 硬断言 + 调用点 101/101 |
| `.analysis/audit_20261008/landing_20261008/check_channel_gate.py` | 15/15 |
| `.analysis/comment_pass_20261008/scripts/refine_round2/check_doc_pointers.py` | 101/101 |
| `.analysis/comment_pass_20261008/scripts/refine_round2/check_contracts.py` | 全部符合（GZ_OL 5 条 / MM 7 条等） |
| `.analysis/myanmar/check_eide_targets.py` | 14/15（唯一失败 = 预存 I6：Debug 定义 `PROTO_CHONGQING` 但 CQ 模块未编，非本轮） |

---

## ⑥ 文档更新清单

| 文档 | 改动 |
|---|---|
| `doc/19_新疆费显协议/README.md` | 新增 **§11 源码契约**（12 条断言 → 文字契约逐条映射 + 保留 22_1665 两条守卫的声明）；§9 变更记录追加本轮行 |
| `doc/18_注释规范.md` | 抬头范围扩至 Platform/Kernel 与 Core/Compiler 自研；§1 目的改「函数/参数/表 + 一句话硬约束」；§2 **新增第 9 条「移出源码的九类内容 + 去处」**；§3/§4/§7 同步收紧（禁止调试说明/历史/叙事）；§10 追加 2026-10-13 行 |
| `doc/CLAUDE.md` | ①「代码风格」注释规范行改写（保留项/禁止项 + 去 doc 归口）；②「代码风格」新增「业务代码默认禁止新增 RTT 调试」条（指向 Cursor 规则 + 底层不可删改）；③「调试」段临时打点口径改为「先征得用户同意」 |
| `.cursor/rules/no-rtt-debug.mdc` | 新增（C 项） |
| `.analysis/comment_pass_20261013/` | 新增：`report.md`（本文件）、`notes_from_code.md`、`scan_candidates.py`、`candidates_before.txt` / `candidates_after.txt`、`static_assert_list.txt` / `static_assert_detail.txt`、`sec_ab.py`、`touched_files.txt` |

**未改**：`doc/06-04`（段尺寸零变化，基线无需同步）；`doc/CLAUDE.md` 中的「零 RTT API 调用」表述
（经复核与实态一致）。`.analysis/**` 既有内容、`Middlewares/**`、`.eide/eide.yml`、`Makefile` 未动。

---

## ⑦ 声明

**未 commit、未改 git 配置、未烧录、未运行 EIDE、未改 `.analysis/**` 既有内容、
未改 `Middlewares/**`、未改 `.eide/eide.yml` 与 `Makefile`。**
构建产物仅落在新建的 `build/cmt_before*` / `build/cmt_after*` / `build/cmt_check`（可整目录删除）。

### 遗留项

1. `xj_frame_sim.py` 的 7 条「源码断言存在性」检查需随本裁决更新（脚本在禁改的 `.analysis/xinjiang/` 内）。
2. 全项目其它 73 条编译期检查（§② 清单）是否按同一裁决清理，待用户决定；建议从「帧长 ≤ RB」族起步。
3. 保留的只读观察 API 与 `pl_tim_dbg_freeze` 是否清理，待用户决定（宁留不删）。

---

# ⑧ 续接轮（2026-10-13 第二轮）：全仓编译期断言清理 + 复核

> 前一会话在「汇总回复」阶段因上下文超限中断（provider error，会话末尾 turn_ended=error）。
> 本轮为续接：把六项要求里**尚未落地**的部分做完并验证——重点是「③ 编译期检查全仓清理」，
> 以及 ⑤ 注释巡检与 ④ 脚本清理的**完整性复核**。**未 commit、未改 git 配置、未烧录、
> 未运行 EIDE、未改 `.eide/eide.yml` 与 `Makefile`。**

## ⑧.1 前一会话已完成 vs 本轮新完成

| 项 | 前一会话（已核） | 本轮（新完成） |
|---|---|---|
| ① RTT 清理 | 业务代码 0 调用（仅 `pl_rtt.c` 底层）；6 处过时注释纠偏；底层 mtime 复核未动 | **0 新增**（复扫结论不变，见 §⑧.4） |
| ② Cursor 规则 | `.cursor/rules/no-rtt-debug.mdc` 已建（`alwaysApply: true`，内容完整） | 核验通过（读全文逐条对照任务要求） |
| ③ 编译期检查 | 仅新疆 12 条 | **全仓再删 64 条（19 文件）→ 契约注释**，见 §⑧.2 |
| ④ 脚本清理 | 引用分析完成；0 删除；4 个 git 跟踪零引用脚本列待裁决 | 全仓复扫（107 个脚本）→ **结论不变**；`.analysis/cleanup_20261013_scripts/README.md` 追加 §8 复核 |
| ⑤ 注释巡检 | 全项目滚动清理（56 文件改动；候选 61 文件/239 行 → 29 文件/53 行） | 复扫 `scan_candidates.py`：**仍 29 文件/53 行，无新增候选**（仅 2 条行号位移） |
| 遗留 1 | `xj_frame_sim.py` 7 条「断言存在性」用例失效（322/329） | **已修复**：改「契约注释存在 + 宿主侧复算」两类 → **331/331** |
| 遗留 2 | 其它 73 条编译期检查待裁决 | **按本轮任务要求全仓删除**（例外见 §⑧.6） |

## ⑧.2 编译期断言清理（全仓）

- **删除 64 条**（`_Static_assert` / `static_assert`），涉及 **19 个文件**；每条改写成
  「一行契约注释（含越限后果）+ doc 指针」。**另 1 个文件仅注释同步**（`app_iap.c` 的
  「与 static_assert 保持同步」表述）。
- 分族：协议帧上限 ≤ RB 25（9 个串口模块 18 + GZ_OL 3 + YN_OL 3 + MM 1）；定长/表长自证 3
  （MM 定长 20B、YN_OL `0x49` 记录 12B、IAP 命令表）；Sector1 记录布局 10（`app_board_net_cfg.h`）；
  LDI 结构体布局 26（`app_ldi_cmd.c` 22 + `app_ldi_cmd.h` 1 + `app_ldi_cfg.h` 1 + `app_ldi.c` 2）。
- 连同第一轮新疆 12 条：**全项目共清理 76 条**；现存 6 条 = 用户点名/文档记载的例外（§⑧.6）。
- 工具：`.analysis/comment_pass_20261013/static_assert_round2.py`（逐条「恰好命中一次」校验）；
  改前 19 文件完整归档 `archive_round2/before/`（附 `before_md5.txt`）。
- 清理后 grep：`Application/Device/Platform/Kernel/Core/Compiler` 仅剩 **6 条**（`app_dispatch.c` 4 +
  `dev_display_22_1665.c` 2）+ 2 处注释提及（`app_iap.c:91`、`dev_display_22_1665.c:14`，均为说明性文字）。

## ⑧.3 A/B（行为零变化证据）

**全量重建（`make -B -j16 BUILD_DIR=build/cmt2_{before,after}/{Debug} …`，默认口径
`PROTO=ALL DISP=1_263 FACTORY=STD`；CQ 口径同法）**

| 口径 | 段 | 改前 | 改后 | 差 |
|---|---|---|---|---|
| ALL | .text/.rodata/.data/.ccmram/.bss/heap_stack | 172056/196264/1656/41324/125388/2564 | **完全相同** | 0 |
| CQ | 同上 | 158652/195592/852/39068/121900/2564 | **完全相同** | 0 |

- **strip-debug elf md5**：ALL `f0a40384f2763d7489f3b3c05f911a69`（改前 = 改后）；
  CQ `123036c44d4c918c7008cad38a0e5697`（改前 = 改后）⇒ **程序镜像逐字节一致**；未 strip 的
  elf md5 必然不同（仅 `.debug_*` 行号），非代码差异。
- **段级逐字节**（`sec_ab.py`）：两口径各 **15/15 段一致**。
- **逐对象 A/B**（`ab_round2_objects.py`，14 个 .c：strip-debug md5 + 逐段 `objdump -s` + 全量
  `objdump -d`）：**353/353 段一致、14/14 反汇编逐行一致、strip-md5 全同**。6 个头文件无独立 .o，
  其影响由「全 elf 段级 15/15」与被包含对象覆盖。
- **diff 复核**：改动仅含「断言语句删除」+「注释行新增」（脚本逐行分类：非注释新增 0，
  非「断言/注释」删除 0——唯二报出的两行是 `_Static_assert` 的消息字符串续行，属断言本体）。
- **告警**：两口径全量重建均 **3 条**（既有 HAL `stm32f4xx_hal_flash_ex.c` `-Wunused-parameter`）⇒ 零新增。
- **增量复核**：同一 `BUILD_DIR` 连续 `make -j16 BUILD_DIR=build/cmt2_after/Debug …` 两跑均 **0 次 gcc 调用、0 次重链**（`.build_stamp` 口径指纹稳定）；最终 elf strip-debug md5 与上表一致。
- 构建目录：`build/cmt2_before` / `cmt2_after` / `cmt2_before_cq` / `cmt2_after_cq`（可整目录删除；
  按 `doc/18` §8 纪律未覆写 EIDE 共用的 `build/Debug`）。

## ⑧.4 RTT 残留复查（最终）

```
grep -rn "SEGGER_RTT|RTT_printf|pl_rtt_" Application Device Platform Kernel Core Compiler --include=*.c --include=*.h
→ 仅 Platform/Src/pl_rtt.c：6 include / 9 函数定义 / 11 SEGGER_RTT_Init() / 13 hw_pl_initcall
grep -rn "APP_DIAG|_RTT_DIAG|\[diag\]|probe_dbg" … → 0 命中
```
业务代码 **0 残留**；底层（`pl_rtt.c`、`Middlewares/Third_Party/SEGGER_RTT/**`、Makefile `SRC_RTT`+`-I`、
`.eide/eide.yml` 收录）**未动**。

## ⑧.5 脚本与宿主校验复跑

| 脚本 | 结果 |
|---|---|
| `.analysis/xinjiang/xj_frame_sim.py` | **331/331**（修复 7 条失效用例；原 322/329） |
| `.analysis/myanmar/mm_frame_sim.py` | **158/158**（原「定长 20B 编译期断言」检查改「契约注释 + 宿主复算」） |
| `.analysis/yn_ol/yn_ol_frame_sim.py` | **184/184** |
| `.analysis/myanmar/check_display_guards.py` | **16/16 + 101/101** |
| `.analysis/audit_20261008/landing_20261008/check_channel_gate.py` | **15/15** |
| `.analysis/comment_pass_20261008/scripts/refine_round2/check_doc_pointers.py` | **106/106**（新增契约指针全部可解析） |
| `.analysis/comment_pass_20261008/scripts/refine_round2/check_contracts.py` | **全部符合**（YN_OL 4 / GZ_OL 5 / MM 7 条，引用一致） |
| `.analysis/myanmar/check_eide_targets.py` | 14/15（唯一失败 = 预存 I6：Debug 定义 `PROTO_CHONGQING` 但 CQ 模块未编，交用户 GUI 二选一） |
| `.analysis/22_1665/verify_all.sh --fast` | **14 项全绿 / 0 失败 / 1 跳过**（跳过项 = 预存前置不满足：8×4 宏值下 make 装不下，属 2026-10-08 已登记状态） |

## ⑧.6 保留的编译期断言（用户点名/文档记载的例外，未动）

| 位置 | 条数 | 依据 |
|---|---|---|
| `Device/Display/dev_display_22_1665.c` | 2 | `doc/01` §0.3「2026-10-08 用户裁决恢复」；`check_driver.py`（61/61）依赖其存在 |
| `Application/Src/app_dispatch.c` | 4 | 用户点名（RB 表长/槽序/尺寸 1536/768/768）；`doc/05` as-built 在册 |

## ⑧.7 文档更新（本轮）

| 文档 | 改动 |
|---|---|
| `doc/19_新疆费显协议/README.md` | 新增 **§11.1 全项目扩展**（分族计数表 + 例外清单 + 零回归证据）；§9 变更记录追加本轮行；§7/顶部横幅推演计数 329 → **331/331** |
| `doc/18_注释规范.md` | §2 新增 **第 10 条**「业务代码不用编译期断言，约束改契约注释；例外 6 条」；§10 变更记录追加行 |
| `doc/08_协议模块接入规则/README.md` · `01_通用规则与模块骨架.md` · `04_接入检查清单.md` | 「payload ≤ RB 必须 `_Static_assert`」→ 契约注释（含骨架代码、检查清单两项、反面清单一行） |
| `doc/CLAUDE.md` | ① 代码风格新增「业务代码不使用编译期断言做防御」条；② Sector1「布局由 static_assert 锁定」→ 布局契约指针；③ XJ 帧上限口语句；④ MM/XJ 宿主推演计数（158 / 331） |
| `doc/16_缅甸2D费显协议/README.md` | 顶部横幅/§7.1 表/§7.2 清单/§14 脚本行计数与断言表述；§12 修订记录追加本轮行 |
| `doc/13` · `doc/11` · `doc/12/03` · `doc/03 PartB` | 「`_Static_assert` ≤ RB」→「源码契约注释」（各 1 处） |
| `doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md` | §2.2/§15 布局锁定表述（历史修订记录行不动） |
| `.analysis/myanmar/README.md` | `mm_frame_sim.py` 计数 157 → **158/158** |
| `.analysis/cleanup_20261013_scripts/README.md` | 追加 **§8 续接轮复核**（107 个脚本、4 候选结论不变、无删除） |
| 本目录新增 | `static_assert_round2.py`、`doc_sync_round2.py`、`ab_round2_objects.py`、`archive_round2/`（19 文件 + md5）、本轮日志/证据 |

## ⑧.8 待用户裁决（本轮未动）

1. **4 个 git 跟踪 + 0 引用脚本**是否删除：`.analysis/cleanup_20260929/round2_ab_objdump.sh`、
   `.analysis/yn_ol/ab_{all_1263,all_221665,cq_1263}.link_no_ynol.sh`（处置命令见
   `.analysis/cleanup_20261013_scripts/README.md` §5.3；按 2026-10-12 先例本轮未删）。
2. **保留的 6 条编译期断言**（§⑧.6）是否也清理（本轮按「文档记载的用户裁决守卫」保留）。
3. `Application/Inc/protocol_select.h` 的 `#warning`（废弃头、全仓无 include 者）是否随同清理。
4. 只读观察 API（`app_dispatch_qfull_drops` 等）与 `pl_tim_dbg_freeze` 是否清理（2026-09-29 起挂账）。

## ⑧.9 声明

**未 commit、未改 git 配置、未烧录、未运行 EIDE、未改 `.eide/eide.yml`、未改 `Makefile`、
未改 `Middlewares/**`、未改 RTT 底层；未回退任何在飞改动。** 新增构建产物仅落
`build/cmt2_*`（可删）；本轮全部脚本/日志/归档落在 `.analysis/comment_pass_20261013/`。

---

# ⑨ 裁决执行轮（2026-10-13 第三轮）：四项遗留裁决落地

> 用户对 §⑧.8 的 4 项待裁决拍板：① 4 个零引用 git 跟踪脚本 → **直接删除**；② 保留的 6 条编译期断言
> → **全部删除**，改写为契约注释 + doc 指针；③ `Application/Inc/protocol_select.h` 废弃头 → **清理**；
> ④ `pl_tim_dbg_freeze` 与只读观察 API → **清理**。本轮逐项执行并验证。
> **未 commit、未改 git 配置、未烧录、未运行 EIDE、未改 `.eide/eide.yml` 与 `Makefile`、未改 RTT 底层、
> 未回退任何在飞改动。**

## ⑨.1 四项结果

### ① 4 个零引用 git 跟踪脚本删除（完成）

| 脚本 | 删除前复扫（basename 全仓 `grep -rlF`） | 处置 |
|---|---|---|
| `.analysis/cleanup_20260929/round2_ab_objdump.sh` | 功能引用 **0**（命中 = 本目录报告/脚本清理 README 的候选清单描述） | `rm`（**未用 `git rm`**，索引不动、删除以 ` D ` 待审阅） |
| `.analysis/yn_ol/ab_all_1263.link_no_ynol.sh` | 功能引用 **0**（同上） | `rm` |
| `.analysis/yn_ol/ab_all_221665.link_no_ynol.sh` | 功能引用 **0** | `rm` |
| `.analysis/yn_ol/ab_cq_1263.link_no_ynol.sh` | 功能引用 **0** | `rm` |

- **文档指针对照**：4 个脚本无任何文档/脚本以之为复跑入口（`doc/**` 0 命中；Makefile / EIDE / `.analysis`
  非 archive 文档 0 命中）⇒ **无需修正文档指针**；执行记录与复跑影响（`ab_measure.sh` 仍会重新生成
  三口径快照）见 `.analysis/cleanup_20261013_scripts/README.md` **§9**（本轮新增）。
- 未处置（用户未批准的 §5.3 方案 B）：`round2_ab_objdump.sh` 的 6 个 `.dis` 产物与 `ab_*.map` /
  `ab_*_no_ynol.elf` 中间物**保留**。

### ② 6 条编译期断言全部删除 → 契约注释 + doc 指针（完成）

| 文件 | 删除内容 | 现契约注释（源码） |
|---|---|---|
| `Device/Display/dev_display_22_1665.c` | 守卫 ①`2U * MODULE_COLS <= HUB75_CHANNEL_MAX`；守卫 ②`_22_1665_CCM_BYTES <= 64U*1024U` + **仅断言使用的派生宏 `_22_1665_CCM_BYTES`** | 契约 ①：`2 × MODULE_COLS ≤ HUB75_CHANNEL_MAX`（⇔ COLS ≤ 5）+ 越限 = 越界读 `_22_1665_lines`（-O3 UB 告警 / -fsyntax-only 静默），见 doc/01 §0.3；契约 ②：通式 `1408·M + 256·COLS` ≤ 64KB + 越限 = **链接期** `region CCMRAM overflowed`，见 doc/01 §0.3 |
| `Application/Src/app_dispatch.c` | 4 条（`g_rb_provide` 表长 / `names` 表长 / `rb_slot_t` 连续 / RB 尺寸 1536·768·768） | 一行契约注释（两条表逐槽对齐 `rb_slot_t`、槽号连续、容量 = 产品口径；改错 = 错槽取体/越界读表），见 `doc/05-01` §2.1 |

- 文件头「物理约束」段同步改写（0 条编译期断言 = 2026-10-13 用户裁决；其余约束 = 契约注释）；
- **全项目业务代码断言清零**：`grep -rn "_Static_assert|static_assert" Application Device Platform Kernel Core Compiler` ⇒ **0 命中**（前两轮共 76 条 + 本轮 6 条 = 82 条）。

### ③ `Application/Inc/protocol_select.h` 删除（完成）

- 删除前复核：源码 0 处 `#include`；`Makefile` / `.eide/eide.yml` / `.clangd` / `.vscode` / 工程文件
  **0 命中**；`doc/**` 0 命中（命中仅 `.analysis` 历史归档与报告描述）⇒ **无真实引用**，直接删除
  （git 跟踪文件，可随时从 HEAD 恢复）。

### ④ `pl_tim_dbg_freeze` 与只读观察 API 清理（完成）

| 删除对象 | 位置 | 说明 |
|---|---|---|
| `pl_tim_dbg_freeze()` 声明 + 实现 + 2 处调用 | `Platform/Inc/pl_tim.h` / `Platform/Src/pl_tim.c` / `Device/Display/dev_display.c`（`dev_display_init`） | 调试器挂起时冻结 TIM3 的 **debug-only** 设施（发布运行路径零影响；原实现的 TIM4 调用本就是空操作）。⚠ 副作用：调试器 halt 时 TIM3 不再自动冻结（观感变化），需要时可在调试器里直接写 `DBGMCU_APB1_FZ` 寄存器 |
| `app_dispatch_qfull_drops` / `app_dispatch_resync_count` / `app_dispatch_notify_drops` + 计数域与自增 | `Application/Inc/app_dispatch.h` / `Application/Src/app_dispatch.c` | 计数域 3×uint32（含对齐 −8B bss）+ 3 处热路径自增（`frame_dispatch_task` −52B / `app_channel_dispatch` −16B）。**调度行为不变**：队列满丢帧 / 1 字节重同步 / 通知重试路径原样保留 |
| `app_render_chip_info_get` / `app_render_glyph_probe` / `app_render_glyph_addr_for` + 2 个结构体 | `Application/Inc/app_render.h` / `Application/Src/app_render.c` | 「字库只读观察」整节删除（函数体早已被 `--gc-sections` 丢弃；rodata 字符串同步消失） |
| `app_udp_gzol_bind_status` / `_str` + `app_udp_gzol_bind_sta_t` + `s_udp_gzol_bind_sta/_err/_ok_cnt` + 孤儿注释 | `Application/Inc/Channel/app_udp.h` / `Application/Src/Channel/app_udp.c` | 绑定结果记录（−7B bss / `udp_gzol_task` −56B）；**绑定/回退/跳过逻辑一字未改** |
| `g_tcp_server_connected` + 2 处写入 | `Application/Src/Channel/app_tcp_server.c` | −4B bss / `tcp_server_task` −16B；连接生命周期逻辑未改 |
| `s_mm_lamp_state` + 写入 + 注释引用 | `Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c` | −3B bss / `mm_apply_lamp` −12B / `mm_apply_init` −12B；`_mm_drive` 与映射表一字未改（宿主推演 158/158 复跑通过） |

- 全部对象**逐个确认零业务消费者**（含 `grep` 源码/头文件/构建系统）；`pl_tim_dbg_freeze` 的 2 处调用点
  属**同一调试设施本体**（非业务逻辑），随 API 一并删除并在此明确记录（发布运行路径行为不变）。

## ⑨.2 构建 A/B（全量重编，各自独立 `BUILD_DIR`）

| 口径 | 段 | 改前（`build/cmt2_after*` = §⑧ 基线） | 改后（`build/cmt3_after*`） | 差 |
|---|---|---|---|---|
| ALL / 1_263 / STD | `.text` / `.rodata` / `.data` / `.ccmram` / `.bss` / heap | 172056 / 196264 / 1656 / 41324 / 125388 / 2564 | **171832 / 196264 / 1656 / 41324 / 125364 / 2564** | text **−224** / bss **−24**，其余 ±0 |
| CQ / 1_263 | 同上 | 158652 / 195592 / 852 / 39068 / 121900 / 2564 | **158444 / 195592 / 852 / 39068 / 121876 / 2564** | text **−208** / bss **−24**，其余 ±0 |

- 告警：两口径全量重建均 **3 条**（既有 HAL `stm32f4xx_hal_flash_ex.c -Wunused-parameter`）⇒ 零新增；
  日志归档 `logs/round3_build_all.log` / `logs/round3_build_cq.log`，段尺寸归档 `logs/round3_sizes.txt`。
- **逐项归因（`ab_round3_objects.py`，期望清单逐对象核对 7/7 全绿，ALL/CQ 同批差值）**：
  - **断言删除 = 0**：隔离 A/B（`ab_round3_asserts.py`）把 6 条断言逐字插回后同 flags 复编 ⇒
    `dev_display_22_1665.c` **21/21 段**、`app_dispatch.c` **33/33 段**逐字节一致 + 反汇编逐行一致 +
    strip-debug md5 相同（22_1665 在默认口径不参与构建，由该隔离证明覆盖）；
  - **未引用 API = 0 差异**：`app_render_chip_info_get` / `app_render_glyph_probe` / `app_render_glyph_addr_for`、
    3 个 dispatch getter、`app_udp_gzol_bind_status(_str)`（含其 rodata 字符串）早已被 `--gc-sections` 丢弃；
  - **实际差值来自活代码**（逐对象实测）：`pl_tim_dbg_freeze` −32 + `dev_display_init` 调用 −20 +
    `udp_gzol_task` −56 + `tcp_server_task` −16 + `mm_apply_lamp` −12 + `mm_apply_init` −12 +
    `frame_dispatch_task` −52 + `app_channel_dispatch` −16 = **216B**（+链接对齐 ⇒ ALL −224 / CQ −208）；
  - **`.bss −24`**：`g_dispatch` −8 / `g_tcp_server_connected` −4 / `s_mm_lamp_state` −3 /
    `s_udp_gzol_bind_sta` −1 + `_err` −2 + `_ok_cnt` −4（共 −22）+ 对齐 −2。
- `doc/06-04` 新增 **§8.4.7** 复采小节（上表 + 归因全文）。

## ⑨.3 脚本同步与复跑（全部全绿）

| 脚本 | 改动点 | 复跑结果 |
|---|---|---|
| `.analysis/22_1665/check_driver.py` | 7 条「断言存在性/断言形态」检查 → 「0 条断言 + 契约注释 ①② 在册 + 仅断言用宏零残留 + 链接期后果」检查（check 总数不变） | **61/61** ×4 几何 |
| `.analysis/22_1665/check_compile_matrix.sh` | B 节两条 `bad`（COLS=6 / ROWS=47）改 `ok`（实测重采）；C 节**新增负向对照** `47×1 → 链接期 region CCMRAM overflow`；头部说明同步 | **26/26**（原 25/25） |
| `.analysis/22_1665/check_eide_command.sh` | `bad_case`×2 改为 `warn_case`（COLS=6 → `-O3` `-Waggressive-loop-optimizations`，实测）+ `ok_case`（ROWS=47 零告警编过）；`bad_case` helper 保留备用并注明 | **20/20** |
| `.analysis/22_1665/check_round34_ab.sh` | 源码级自证：现行断言期望 2 → **0**；`_22_1665_CCM_BYTES` 检查反转（须零残留）+ 新增「契约 ② 注释在册」 | **6/6**（原 5/5） |
| `.analysis/22_1665/verify_all.sh` | 头部「2026-10-08 恢复守卫」说明段改写为 2026-10-13 现行期望 | **14 项全绿 / 0 失败 / 1 跳过**（`--fast`） |
| `.analysis/myanmar/check_guard_disasm.py` | 新增 `ROUND3_REMOVED_CALLS` reviewed 条目（`dev_display_init` 的 2 处调试冻结调用删除）——防「以含该删除的构建重跑」时误报 LOST | 原操作**零丢失**（对象来源仍为未重建的 `build/Debug`，见 §⑨.5 注） |
| `.analysis/cleanup_20261013_scripts/README.md` | 新增 **§9 用户裁决执行**（删除前复扫表 + 执行命令 + 影响说明） | — |
| `.analysis/22_1665/README.md` | 现行口径块（0 条断言 / 385 行）、脚本计数（check_driver 61/61、matrix 26/26、round34 6/6）、操作步骤 #3 上限口径 | — |

其余复跑（未改脚本，结论不变）：`xj_frame_sim.py` **331/331**、`mm_frame_sim.py` **158/158**、
`check_display_guards.py` **16/16 + 101/101**、`check_channel_gate.py` **15/15**、
`check_doc_pointers.py` **106/106**、`check_contracts.py` **全部符合**、
`check_eide_targets.py` **14/15**（唯一失败 = 预存 I6，与本轮无关）。

## ⑨.4 文档同步（本轮）

| 文档 | 改动 |
|---|---|
| `doc/01_显示系统/22-1665…md` | 顶部新增 2026-10-13 横幅（现行）+ 08 条标历史；§0.3 合法性块（守卫 → 契约 ①②）、后果段、上限口径段改写；排障表行；附录 **新增 A.21（2026-10-08 落地轮，补齐原悬空指针）+ A.22（本轮）**；A.20 标历史 |
| `doc/CLAUDE.md` | 11 处：22_1665 段（2 处横幅/表格）、CCMRAM 段、initcall 表、dev_display 表、RB `_Static_assert`、GZ_OL 观察 getter、notify 计数、缅甸影子状态、断言纪律条（例外清零） |
| `doc/19` / `doc/16` / `doc/18` / `doc/14` / `doc/15` | 断言表述/观察 API 表述更新 + 变更记录各追加一行；doc/16 §3/§4.1/§14.2 的「影子状态」同步 |
| `doc/05-01` / `doc/05-02` | RB 契约注释表述 + 缅甸 SRAM 13B + 新增「变更记录（2026-10-13）」条 |
| `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` | 断言约束段改写 + YN_OL 联调条加〔2026-10-13 注〕+ **新增 §8.4.7 复采** |
| `doc/构建开关总表.md` | 22_1665 守卫段（08 条标「已删」+ 新增 13 条现行）、`DISPATCH_NOTIFY_RETRY_MS` 行、2026-09-17 联调条 ③、断言计数行 |
| 本目录 | 新增 `ab_round3_asserts.py`（断言隔离 A/B）、`ab_round3_objects.py`（逐对象期望清单）、`logs/round3_*`（构建日志 + 段尺寸） |

**未改**：`doc/17`（历史轮记录）、`.analysis` 既有归档与历史报告、`.cursor/rules/**`、`Makefile`、
`.eide/eide.yml`、`Middleware/**`、RTT 底层。

## ⑨.5 遗留项与说明

1. `doc/17_代码瘦身轮_20260929.md` 的「待裁决观察 API」清单为**历史记录**，保留当时写法（未改）。
2. `check_guard_disasm.py` 的对象来源是 `build/Debug`（与 EIDE 共用、本轮按纪律未重建）⇒ 本轮复跑仍读
   2026-09-29 的旧对象；已按删除点补 reviewed 条目，待下次重建 `build/Debug` 后该条目生效（机器级证据
   本轮由 `ab_round3_objects.py` 承担）。
3. `Makefile` 的 `compile_commands` 目标存在**预存缺陷**（recipe 内多行 `python3 -c` 续行产生 `SyntaxError`，
   与本轮无关）——本轮改用 `.build_stamp` 的 `CFLAGS=` 取精确编译参数，未改该目标。
4. 保留的只读观察变量还剩 `g_dev_display_commit_count`（`Device/Display/dev_display.c`，「提交帧计数」）——
   不在用户本轮点名清单内、且被 `dev_display.h` 以 `extern` 暴露；是否清理待用户决定。
   ⇒ **2026-10-13 小项裁决：已清理**（零读取方确认 + 对象级/构建级 A/B 全绿，见 **§⑩**）。
5. **无烧录、无提交**；构建产物仅落 `build/cmt3_after*`（可整目录删除）。

---

## ⑩ 2026-10-13 小项裁决（第四轮）：`g_dev_display_commit_count` 清理

范围：删除 `Device/Inc/dev_display.h` 以 `extern` 暴露的「提交帧计数」`g_dev_display_commit_count`
（1 处声明 + 1 处定义 + 2 处自增）。**零业务读取方**确认后执行；未改 Makefile / `.eide/eide.yml`、
未 commit、未改 git 配置、未烧录；未触碰「被删脚本遗留的中间产物」（`.analysis/yn_ol/*.dis|*.map|*_no_ynol.elf`）。

### ⑩.1 引用清单与零读取方确认（改前全仓 grep）

| 位置 | 性质 |
|---|---|
| `Device/Inc/dev_display.h:146` | `extern volatile uint32_t g_dev_display_commit_count;`（该头仅两条 extern，另一条 = 保留的 `g_dev_display_scan_count`） |
| `Device/Display/dev_display.c:36` | 定义（`volatile uint32_t`，4B `.bss`） |
| `Device/Display/dev_display.c:65` | `dev_display_commit_frame()` 内 `++`（位于 `osKernelLock/Unlock` 窗口内） |
| `Device/Display/dev_display.c:109` | `dev_display_commit_frame_rect()` 内 `++`（同上） |

- **读取方 = 0**：`Application/**`、`Device/**`、`Platform/**`、`Kernel/**`、`Core/**`、`Compiler/**`、
  `Makefile`、`.eide/eide.yml` 全无引用。
- **唯一历史读取方** = 贵州治超 `0x20` 的 post-render RTT 诊断行，已随 2026-09-29 瘦身轮删除
  （历史记载 = `.analysis/9k23881580/field_triage_2432_and_port.md` §①.3 表；本轮在该行加注）。
  `doc/**` grep `commit_count` / 「提交帧计数」**0 命中** ⇒ 无需同步。
- 提交函数（`dev_display_commit_frame` / `dev_display_commit_frame_rect`）的调用方不受影响：
  删除的只有计数语句，锁 / 指针 / 脏矩形 / `dirty` 语义逐字未动。

### ⑩.2 A/B 与构建证据

**① 对象级真实 A/B**（`dev_display.o`：归档改前 `ed72f0568b3307dd7973c1c7072663b2` → 现行 `f1bfdae811cec0750ac692a4d6e71e8e`；
ALL 与 CQ 两口径该对象完全相同）

| 段 / 函数 | 改前 | 改后 | Δ |
|---|---|---|---|
| `.text.dev_display_commit_frame` | 60B（20 条指令 + 4 项常量池） | 48B（16 条 + 3 项） | **−12** |
| `.text.dev_display_commit_frame_rect` | 312B（127 条） | 300B（123 条） | **−12** |
| `.bss.g_dev_display_commit_count` | 4B | — | **段消失** |
| 其余 12 个函数（`scan_task` 103 条、`dev_display_draw_bitmap` 72 条…） | — | — | **逐行一致** |

- 指令级差异 = 每个提交函数少 4 条 16 位指令（`ldr r2,[pc,…]` / `ldr r3,[r2]` / `adds r3,#1` / `str r3,[r2]`）
  + 常量池少 1 个地址项（4B）= −12B/函数；其余差异仅为 PC 相对偏移与常量池重编址。
- **② 头文件行号位移的隔离证明**：`dev_display.h` 删 1 行 ⇒ 其后函数原型声明的 DWARF `decl_line` 前移 1。
  重建改前状态（覆盖目录 = `Device/Inc` 全量软链接 + 改前 `dev_display.h` 实体；`dev_display.c` 插回 3 处）
  后逐 TU 复编 ⇒ **35 个重编 TU codegen 差异 0**（strip-debug md5 全等；差异段仅 `.debug_info` + `.debug_line`）。
  现网实测：21 个 md5 变化 / 14 个不变（变化全部为调试信息，无机器码变化）。
- **③ 段尺寸（增量重编，`build/cmt3_after/Debug` 与 `build/cmt3_after_cq/Debug`）**

| 口径 | `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | heap |
|---|---|---|---|---|---|---|
| ALL 改前 → 改后 | 171832 → **171816（−16）** | 196264（±0） | 1656（±0） | 41324（±0） | 125364 → **125356（−8）** | 2564（±0） |
| CQ 改前 → 改后 | 158444 → **158412（−32）** | 195592（±0） | 852（±0） | 39068（±0） | 121876 → **121868（−8）** | 2564（±0） |

- **归因**（`ab_round4_commit_count.py` Part 3：用归档改前 `dev_display.o` 重链 ⇒ 重建 elf 的
  `.text` 与改前基线**逐值相同** 171832 / 158444，符号位移分布只有三段 ⇒ 无其它改动）：
  对象级 −24B；`.text` 镜像级 = −24 + 对齐填充差（ALL **+8** / CQ **−8**），唯一跳变点 =
  FreeRTOS 端口段的 16 字节对齐边界 `pxPortInitialiseStack`(36B) → `SVC_Handler`（前隙 ALL 4B→12B、
  CQ 8B→0B）；`.bss −8` = 删 4B 计数 + 后随 8 字节对齐边界（`g_dispatch` 起点）少 4B 填充。
  符号位移分布：ALL = {0, −24, −16}、CQ = {0, −24, −32}。
- **构建通过 & 告警**：ALL 重编 **36** TU + 1 次链接、CQ 重编 **34** TU + 1 次链接，**0 条告警**
  （增量只重编 app/device 侧 TU；3 条既有 HAL 告警来自 `stm32f4xx_hal_flash_ex.c`，只在全量构建时出现
  —— 本轮全量交叉构建实测仍 **3 条**）。重复 `make` = 0 编译 0 链接（增量纪律保持）。
- **增量 == 全量**：误设 `BUILD_DIR=build/cmt3_after`（漏 `/Debug`）触发的一次 245 TU 全量构建，
  其 elf 与增量产物 **md5 相同**（ALL `13a7da8bb34e649f56fc5d0fd11f3c3c`）；多余树已删除，elf 副本留档
  `ab_round4/all_1263/Project_STD.strayfull.elf`。CQ elf md5 = `5c1eabc7120223275d9ed8a9794ae53c`。
- **回归**：`.analysis/myanmar/check_display_guards.py` **16/16 + 101/101**（与上一轮一致）。

### ⑩.3 脚本与归档

| 项 | 说明 |
|---|---|
| `ab_round4_commit_count.py` | Part 1 对象级真实 A/B（段 + 逐函数 + 指令差）/ Part 2 重建隔离（35 TU）/ Part 3 链接期归因（重链改前 elf + 符号位移）—— **全绿**，日志 `logs/round4_ab.log` |
| `ab_round4/` | 改前 `dev_display.o` 归档（ALL/CQ 各一份）、全量 elf 副本、重链改前 elf + map、`dev_display_o_before/after.dis`、段尺寸快照 |
| `logs/round4_build_{all,cq}_debug.log` | 两口径增量重编日志（36 / 34 TU + 1 链接，0 告警） |
| `logs/round4_build_all.log` | 全量交叉构建（245 TU、3 条既有 HAL 告警）；其产物树已删 |

### ⑩.4 文档同步

| 文档 | 改动 |
|---|---|
| 本报告 ⑨.5 #4 | 加「⇒ 2026-10-13 小项裁决：已清理（见 §⑩）」 |
| `.analysis/9k23881580/field_triage_2432_and_port.md` §①.3 表 | 该行加注「2026-10-13 已随裁决删除」+ 指针 |
| `doc/**` | 0 命中，无需更新（grep 见 ⑩.1） |

### ⑩.5 遗留

- `g_dev_display_scan_count`（同文件、同 `extern` 暴露、同为只读观察量）**不在本轮点名范围**、保留不动
  （`.analysis/9k23881580/dev_display_22_1703_analysis.md` 仍将其列为 DWT 实测候选观察量）；是否清理待用户决定。
  ⇒ **2026-10-13 小项裁决：已清理**（零读取方确认 + 对象级/构建级 A/B 全绿，见 **§⑪**）。
- `.analysis/**` 历史归档（elf/map 内的符号痕迹）按用户裁决保留；`ab_round3_objects.py` 属上一轮定点记录
  （基线为改动前对象、期望清单口径），**本轮不再复跑**。
- 本轮未烧录、未提交、未改 git 配置、未改 `.eide/eide.yml` 与 Makefile。

---

## ⑪ 2026-10-13 小项裁决（第五轮）：`g_dev_display_scan_count` 清理

范围：删除 `Device/Inc/dev_display.h` 以 `extern` 暴露的「扫描帧计数」`g_dev_display_scan_count`
（1 处声明 + 1 处定义 + 1 处自增）。**零业务读取方**确认后执行；未改 Makefile / `.eide/eide.yml`、
未 commit、未改 git 配置、未烧录；未触碰「被删脚本遗留的中间产物」（`.analysis/yn_ol/*.dis|*.map|*_no_ynol.elf`）。

### ⑪.1 引用清单与零读取方确认（改前全仓 grep）

| 位置 | 性质 |
|---|---|
| `Device/Inc/dev_display.h:146` | `extern volatile uint32_t g_dev_display_scan_count;`（上一轮删 `commit_count` 后，该头仅剩此 1 条 extern；本轮删除后 **extern 观测量全清**） |
| `Device/Display/dev_display.c:36` | 定义（`volatile uint32_t`，4B `.bss`） |
| `Device/Display/dev_display.c:172` | `scan_task` 体内 `++` —— 位于帧提交块之后、`dev->ops->scan()` 调用之前（**不在任何 `ops->scan` 实现内**） |

- **读取方 = 0**：`Application/**`、`Device/**`、`Platform/**`、`Kernel/**`、`Core/**`、`Compiler/**`、
  `Makefile`、`.eide/eide.yml` 全无引用（`grep -rn scan_count` 排除 `.analysis`/`build` ⇒ rc=1）。
- **唯一历史用途** = 9k23881580 → 22_1703 迁移的 **M5「CPU 预算实测」候选观察量**
  （`.analysis/9k23881580/dev_display_22_1703_analysis.md`，2026-09 快照、方案未实施）——本轮在该文档两处加注。
  `doc/**` grep `scan_count` / 「扫描帧计数」**0 命中** ⇒ 无需同步（见 ⑪.4）。
- `scan_task` 的其余逻辑（事件等待 / 锁窗口 / 脏矩形取出与复位 / prepare / OE-LAT 原子窗口 / 行号推进）逐字未动；
  删除的只有计数语句。

### ⑪.2 A/B 与构建证据

**① 对象级真实 A/B**（`dev_display.o`：归档改前 `f1bfdae811cec0750ac692a4d6e71e8e` → 现行 `e0804bdc463d2ed00983f52c1456ccf7`；
ALL 与 CQ 两口径该对象完全相同）

| 段 / 函数 | 改前 | 改后 | Δ |
|---|---|---|---|
| `.text.scan_task` | 308B（103 条指令） | 296B（99 条） | **−12** |
| `.bss.g_dev_display_scan_count` | 4B | — | **段消失**（对象 `.bss` 36 → 32B） |
| 其余 13 个函数（`dev_display_commit_frame_rect` 123 条、`dev_display_draw_bitmap` 72 条…） | — | — | **逐行一致** |

- 指令级差异 = 少 4 条 16 位指令（`ldr r2,[pc,#…]` 取地址 / `ldr r3,[r2,#0]` / `adds r3,#1` / `str r3,[r2,#0]`）
  + 常量池少 1 个地址项（4B）= −12B；其余差异仅为 PC 相对偏移与分支目标重编址（随函数缩短整体前移）。
- **② 头文件行号位移的隔离证明**：`dev_display.h` 删 2 行（extern + 空行）⇒ 其后函数原型声明的 DWARF `decl_line` 前移 2。
  重建改前状态（覆盖目录 = `Device/Inc` 全量软链接 + 改前 `dev_display.h` 实体；`dev_display.c` 插回 2 处）后逐 TU 复编
  ⇒ **35 个重编 TU codegen 差异 0**（strip-debug md5 全等；差异段仅 `.debug_info` + `.debug_line`）。
  for-trust：`dev_display.c` before 变体 vs 归档真实改前对象 —— strip-debug md5 相同 / 反汇编逐行一致 /
  机器码段零差异 / 调试段差异 ⊆ `.debug*` / 临时路径字符串在对象内。
  现网实测：21 个 md5 变化 / 14 个不变（变化全部为调试信息，无机器码变化）。
- **③ 段尺寸（增量重编，`build/cmt3_after/Debug` 与 `build/cmt3_after_cq/Debug`）**

| 口径 | `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | heap |
|---|---|---|---|---|---|---|
| ALL 改前 → 改后 | 171816 → **171800（−16）** | 196264（±0） | 1656（±0） | 41324（±0） | 125356（±0） | 2564（±0） |
| CQ 改前 → 改后 | 158412 → **158412（±0）** | 195592（±0） | 852（±0） | 39068（±0） | 121868（±0） | 2564（±0） |

- **构建通过 & 告警**：ALL 重编 **36** TU + 1 次链接、CQ 重编 **34** TU + 1 次链接，**0 条告警**
  （增量只重编 app/device 侧 TU；3 条既有 HAL 告警来自 `stm32f4xx_hal_flash_ex.c`，只在全量构建出现——
  本轮未做全量）。重复 `make` = **0 编译 0 链接**，elf md5 不变（两口径各复跑 1 次）。改后 elf md5（含 `__DATE__/__TIME__`，
  仅信息性）= ALL `62d7919132c1319c21cb36f9a24065b0` / CQ `59528b2d416f3136dbe757c8f88758ab`。
- **归因**（`ab_round5_scan_count.py` Part 3：用归档改前 `dev_display.o` 重链 ⇒ 重建 elf 的
  `.text` 与改前基线**逐值相同** 171816 / 158412；按位置对齐 2366 / 2207 对符号算位移）：
  - **对象级**：`.text` −12B、`.bss` −4B。
  - **ALL `.text` −16 = −12 + 填充 −4**：唯一填充跳变 = cJSON `skip_utf8_bom` 之后的 4 字节对齐边界
    （`parse_number` 前）：`*fill* 0x4 → 0`；其后符号恒 −16（位移集合 {0, −12, −16}）。
  - **CQ `.text` ±0 = −12 + 填充 +4 + 填充 +8**：同一 `parse_number` 边界 `*fill* 0 → 0x4`，
    加 FreeRTOS `SVC_Handler` 的 16 字节对齐前隙 `*fill* 0 → 0x8`（位移集合 {0, −12, −8}）。
  - **`.bss` 两口径均 ±0**（对象 −4B 被链接期填充吸收）：`dev_display.o` 的 `.bss` 簇整体前移
    （首符号 `s_pending_rect_valid` 由 `0x…478` → `0x…472`：前一段结束于 `0x…472` 且它 1 字节对齐，
    不再需要为 4B 对齐的计数补 2B），`g_dispatch` 之前新增 `*fill* 0x4` ⇒ `g_dispatch` / `_msg_dispatch_buf`
    地址不变（位移集合 {0, −6, −4}）。
  - SRAM 合计（`.data + .bss + ._user_heap_stack`）= ALL 129576B / CQ 125284B，与改前逐字节相同。

### ⑪.3 脚本与归档

| 项 | 说明 |
|---|---|
| `ab_round5_scan_count.py` | Part 1 对象级真实 A/B（段 + 逐函数 + 指令差）/ Part 2 重建隔离（35 TU）/ Part 3 链接期归因（重链改前 elf + 符号位移）—— **全绿**，日志 `logs/round5_ab.log` |
| `ab_round5/` | 改前 `dev_display.o` 归档（ALL/CQ 各一份）、改后 elf 副本、重链改前 elf + map、`dev_display_o_before/after.dis`、改前对象 md5 清单（485 条）、段尺寸前后快照 |
| `logs/round5_build_{all,cq}_debug.log` | 两口径增量重编日志（36 / 34 TU + 1 链接，0 告警） |
| `logs/round5_sizes.txt` | 段尺寸前后对照 |

- **保留纪律**：`.analysis/**` 历史归档内的二进制/文本痕迹（`yn_ol/*.map|*.elf`、`9k23881580/artifacts_*/Project_STD.elf`、
  `myanmar/eide_writeback_*`、`ab_round4/*`）**一律未动**；上一轮的中间产物（`.dis` / `ab_*.map` / `ab_*_no_ynol.elf`）保留。
- **回归**：`.analysis/myanmar/check_display_guards.py` **16/16 + 101/101**（与上一轮一致）。

### ⑪.4 文档同步

| 文档 | 改动 |
|---|---|
| 本报告 ⑩.5 | 加「⇒ 2026-10-13 小项裁决：已清理（见 §⑪）」 |
| `.analysis/9k23881580/dev_display_22_1703_analysis.md`（M5 段 2 处） | 加注「2026-10-13 已随小项裁决删除；实测改用调试器（断点/DWT）或临时计数」+ 指针 |
| `.analysis/comment_pass_20261013/ab_round4_commit_count.py` | 顶部加注：其 `HEADER_ANCHOR` 锚定的是第四轮头文件（含 `scan_count` extern），第五轮删除后不可直接复跑 |
| `doc/**` | 0 命中（grep `scan_count` / 「扫描帧计数」），无需更新 |

### ⑪.5 遗留

- `Device/Inc/dev_display.h` 的 `extern` 观测量至此**全部清零**（`commit_count` §⑩、`scan_count` 本轮）；
  `dev_display.c` 内仍保留的 `static` 内部状态（`s_*`）均为实现必需，不属观测量。
- 本轮未烧录、未提交、未改 git 配置、未改 `.eide/eide.yml` 与 Makefile；构建产物仅落 `build/cmt3_after*`（可整目录删除）。
