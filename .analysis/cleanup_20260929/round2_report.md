# 代码瘦身轮 · 第二轮修正报告（2026-09-29）

> **范围**：按用户 4 条裁决执行——① RTT **只清理 API 调用、不清理底层**；② 自研诊断设施保持删除；
> ③ `build/` 清理 + 防再污染；④ 长注释外移（全项目口径）。
> **硬前提**：不改任何业务逻辑（本轮 = 恢复底层 + 注释 + 构建工具 + 清理）。
> **第一轮**报告：`.analysis/cleanup_20260929/report.md`（RTT 移除 + 注释精简）；as-built 记录 `doc/17`（§5 = 本轮）。
> **结论速览**：四口径构建全绿（仅 3 条既有 HAL 告警）；业务代码 **0 处 RTT API 调用**、RTT 底层符号在镜像内；
> `build/` 只剩 `Debug` + `Release`；注释精简有 token 级 + 机器码 A/B 双重证据（section 逐字节 + 反汇编逐行一致）；
> **1 项待用户裁决**（见 §7.1：`SEGGER_RTT_Syscalls_GCC.c` 自带的 printf 重定向）。

---

## 1. RTT 底层恢复（决定 1）

### 1.1 恢复清单

| 对象 | 做法 | 证据 |
|---|---|---|
| `Platform/Src/pl_rtt.c` | `git show HEAD:Platform/Src/pl_rtt.c` 管道写回（**未用 `git checkout`/未整文件覆盖他人改动**） | md5 **`3e432cf868fb6a916b1f56476851feb9`** = `git HEAD:` 版本 = 第一轮备份 `backup/sources_before.tar.gz` 内副本，三者一致；文件 = 201B（`SEGGER_RTT_Init()` + `hw_pl_initcall`） |
| `Platform/Inc/pl_rtt.h` | **不存在**（`git ls-tree HEAD` 与备份 tar 均无此文件） | 无需恢复；`pl_rtt.c` 只 include `SEGGER_RTT.h` + `initcall.h` |
| Makefile（4 处） | `SRC_RTT` 三源文件块（`SEGGER_RTT.c` / `SEGGER_RTT_printf.c` / `SEGGER_RTT_Syscalls_GCC.c`）、`-I Middlewares/Third_Party/SEGGER_RTT`、`Platform/Src/pl_rtt.c`、`$(SRC_RTT)` | 逐行内容与 `git HEAD` **完全相同**（`diff <(git show HEAD:Makefile \| grep …) <(grep … Makefile)` 仅多出 2 行新注释）；未带回任何其它无关改动 |
| `.eide/eide.yml` | **无需改动**——复核发现第一轮**并未**从 yml 删除 RTT 条目（SEGGER 三个 `files`、两个 `incList` 路径、`pl_rtt.c` 条目一直在册） | 与 `HEAD` 逐条比对：SEGGER 5 条 / `pl_rtt` 1 条，现行 ⊇ HEAD（多出的 1 条是缅甸目标 `incList`，属 2026-09-24 那轮） |
| `Core/Src/syscalls.c` | **未动**（维持无输出 `__io_putchar`，符合「不恢复 printf 重定向」的字面口径） | `git diff` 相对第一轮无变化 |
| `Platform/Inc/lwipopts.h` | **未动**（LwIP 调试维持 `LWIP_DBG_TYPES_ON 0` 编出） | 同上 |

### 1.2 验证（三项）

1. **业务代码 0 处 RTT API 调用**：
   - `grep -rn "SEGGER_RTT" Application Device Kernel Platform | grep -v Platform/Src/pl_rtt.c` ⇒ **0 行**；
   - `grep -rc "SEGGER_RTT_" …` ⇒ 唯一非零文件 = `Platform/Src/pl_rtt.c:1`（即其自身的 `SEGGER_RTT_Init`）。
2. **`nm` 实证底层已编入**（`build/Debug/Project_STD.elf`，默认口径）：
   ```
   08041330 T pl_rtt_init               08098da0 r __hw_initcall_1_pl_rtt_init   ← initcall 注册恢复
   08056734 T SEGGER_RTT_Init           080566fc T SEGGER_RTT_Write
   08056684 T SEGGER_RTT_WriteNoLock    0805673c T _write_r
   2000ebe0 00000400 b _acUpBuffer      2000ebd0 00000010 b _acDownBuffer
   2000efe0 000000a8 B _SEGGER_RTT
   ```
   `SEGGER_RTT_printf*` 符号 **0 个**（`--gc-sections` 丢弃未被引用的 printf 库函数，属预期）。
3. **启动路径行为**：`pl_rtt_init` 经 `hw_pl_initcall(1)` 在 RTOS 前执行（与瘦身前一致）；`syscalls.c` 仍无输出。
   段尺寸回升属预期，见 §4。

### 1.3 段尺寸代价（相对第一轮 RTT 移除态）

`text +464 / rodata +40 / bss +1208`（= `_acUpBuffer` 1024 + `_acDownBuffer` 16 + `_SEGGER_RTT` 168；`data`/`ccmram` ±0）。
四口径逐一列在 §4。

---

## 2. `build/` 与临时工程产物清理（决定 3）

### 2.1 清理前清单（`build_inventory_before.txt` 留档）

| 目录 | 体积 | 处置 |
|---|---|---|
| `build/Debug` | 25M | **保留**（make/EIDE 共用默认产物目录） |
| `build/EIDEMyanmar` | 10M | 删除 |
| `build/EIDEMyanmar1665` | 11M | 删除 |
| `build/EIDEMyanmarNoDisp` | 9.9M | 删除 |
| `build/MMAblation` | 12M | 删除 |
| `build/Release` | 340K | **保留**（原本存在，按要求不主动新建/不删） |
| `build/Testing` | 16K | 删除（无源码/工具链引用） |

**删除合计 ≈ 42.9MB**，`rm -rf` 直删、无备份需求（均为可再生构建产物）。

### 2.2 临时工程产物

- 根目录**无** `Makefile.mm_nodisp`（第一轮已把它放在 `.analysis/myanmar/` 下）；
- 本轮把该**陈旧生成物删除并重生成**到 `.analysis/myanmar/build_nodisp/Makefile.mm_nodisp`（24K）；
- `.analysis/` 下其它中间件（日志/尺寸/反汇编）本就在分析目录内，未越界；
- 根目录复查：只剩源码、工具链配置与文档（无临时 Makefile / `builder.params` 之类——`builder.params` 只在 `build/<target>/` 内）。

### 2.3 防再污染（构建工具最小改动）

| 改动 | 内容 |
|---|---|
| `Makefile` | `BUILD_DIR  = build/$(CONFIG)` → **`BUILD_DIR ?= build/$(CONFIG)`**（+3 行注释）：命令行/环境变量可覆盖，不改任何既有语义（默认值不变） |
| `.analysis/myanmar/check_eide_targets.py` | `--emit-make` 生成物**注入** `BUILD_DIR ?= .analysis/myanmar/build_nodisp/$(CONFIG)`（带 `assert` 防 Makefile 形态漂移）、自动建父目录、打印推荐复现命令；`--emit-make` 用法文档同步 |
| `.analysis/myanmar/README.md` | 文件索引与「运行」段改为新路径/新命令；`Makefile.mm_nodisp` 行标注「产物不落 `build/`」 |
| `doc/16` §9 | 复现命令改新路径 + 新实测数字（见 §4） |
| `doc/06-04` | 无显示口径条目改指新路径；新增 §8.4.4 |
| `doc/构建开关总表.md` | §3 变量表新增 `BUILD_DIR` 行（含防污染用途说明） |

### 2.4 复查

清理 + 四口径构建完成后：

```
build/            →  Debug（12M，重建后）+ Release（340K）   ← 仅此两项
.analysis/myanmar/build_nodisp/  →  Makefile.mm_nodisp + EIDEMyanmarNoDisp/（11M，等价口径产物）
```

---

## 3. 长注释外移与继续精简（决定 2、4）

### 3.1 `app_boot.c` 两处（用户点名，已完成）

| 原位置 | 外移到 | 文件内留下的指针 |
|---|---|---|
| `app_test_run()` 恒注释的原因 + 三条替代入口（原 11 行） | **`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` §5.1**（新增小节，含 `app_test_pixel_scan` / `app_test_led_mapping` / TEST 键出厂检测三条路径） | 4 行：结论（`for(;;)` 独占屏幕、出厂口径一律不改本文件）+ 两个 doc 指针 |
| FreeRTOS 内核静态任务缓冲 → CCMRAM（原 19 行） | **`doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` §8.2**（扩充「另外两处（内核静态任务）」段：1736B 构成、`nm` 地址实证、CCM 合法性、调用时机、`timers.c` 240B 未下沉与 `configUSE_TIMERS=0` 评估） | 8 行：结论（强定义覆盖 `__WEAK`、账本与安全性论证见 §8.2、调用时机） |

同轮附带（文档/代码一致性）：`doc/01` §0.6 与 §5 的「看 RTT `[diag]` 横幅」判读法已失效（自研诊断不回退）→
改写为「比对 `arm-none-eabi-size -A` / elf md5 / IAP 0x03 + 屏上验收图案」；`doc/06-04` §8.5 的诊断设施表
标注已删除、给出「加一行 `SEGGER_RTT_printf` 临时打点」的现行替代；§8.6 纪律第一条同步。

### 3.2 其余继续精简（本轮实际改动 4 文件 / 6 处）

| 文件 | 改动点 | 行数 | 注释字符 |
|---|---|---|---|
| `Application/Src/app_boot.c` | 上述两处外移 | 243 → **225**（−18） | 2665 → **1747**（−918） |
| `Device/Display/dev_display_22_1703.c` | 文件头重写（历史沿革 → doc 指针；保留几何/位流/档位 D+/改宏代价等 as-built 不变量） | 346 → **327**（−19） | 5245 → **4544**（−701） |
| `Application/Src/ProtocolParser_ChongQing/app_cq_proto.c` | 文件头 probe 注册序长叙述 → doc/03 PartB Q23 指针；**顺带修正过时字段名** `net_cfg.port` → **`net_cfg.udp_port`**（与 `_udp_cq_read_port` 实现一致） | 362 → **359**（−3） | 4653 → **4465**（−188） |
| `Application/Src/LDI/app_ldi.c` | `ldi_ctx_init` 职责块去日期化 + 三分支语义改 doc/07 §10/§11 指针 | 576 → **575**（−1） | 6323 → **6282**（−41） |
| （合计） | | **−41 行** | **−1848 字符** |

**逐字只动注释的证据**（三条独立方法）：

1. **token 级「只删不改」**（复用第一轮工具 `check_token_equivalence.py`，基线 = 第一轮备份 tar）：
   5 文件全部 `OK(仅删除)`；其中 `app_ldi.c` / `app_cq_proto.c` / `dev_display_22_1703.c` / `pl_rtt.c`
   **删除 token = 0**（纯注释变更），`app_boot.c` 的删除 token 数仍为 **1136**（与第一轮相同 ⇒ 本轮未新增删除）。
2. **非注释差异行扫描**：`diff -u` 重建的改动前源码 vs 现行源码，过滤掉注释行后**每个文件 0 行差异**。
3. **机器码 A/B**（`.analysis/cleanup_20260929/round2_ab_reconstruct.py` + `round2_ab_compare.sh`）：
   把本轮 6 处替换逐条反转，在 `round2_ab/reconstruct/` 重建改动前源码，用**与 make 完全相同的编译命令**
   分别编译 pre/cur ⇒ **4 文件全部：section（排除 `.debug_*`）逐字节一致 + 反汇编逐行一致**（105 段 / 1801 行）。

### 3.3 已复核但**未**继续删的文件（结论：注释基本是契约/why，删了就丢信息）

按第一轮报告 §2.3 的剩余清单逐个复核（按体积序）：`app_ldi_cmd.c`（12.2K，13 个长块 = LDI 帧结构图例 +
module 尺寸查表注意项 + 响应帧构造规则，全为契约）、`app_render.c`（readaddr 公式推导 = X/Y/Z 修正量的 why）、
`app_udp.c`（三类实例端口语义）、`app_yn_ol_proto_cmd.c`（持久化/互斥/应答契约）、`pl_eth.c`（无 ≥12 行长块）、
`dev_dp83848.c`（Doxygen 寄存器语义）、`dev_display_22_1665.c`（第三十四轮刚精简过，已无长叙述）。
⇒ 结论：**共 6 处真实冗余已清**；其余为「硬件/协议不变量、纪律警示、非显而易见的原因」，按规则保留。
剩余可选项：若用户要求继续，建议只挑「与 doc 段落逐字重复」的文本（需逐条人工判，收益有限）。

---

## 4. 四口径构建结果（全量 `make clean` + `make -j8`，仅 3 条既有 HAL 告警）

| 口径 | text | rodata | data | ccmram | bss | heap | SRAM 合计 / 余量 | elf md5 |
|---|---|---|---|---|---|---|---|---|
| `PROTO=ALL` `DISP=1_263` `FACTORY=STD` | **167752**（+464） | **195776**（+40） | 1656（0） | 39652（0） | **124768**（+1208） | 2560 | **128984 / 2088B**（−1208） | `09c574f0f2aed808510792d0c316fb13` |
| `FACTORY=MM` | **166216**（+464） | **195664**（+40） | 1656（0） | 39652（0） | **124504**（+1208） | 2560 | 128720 / **2352B** | `43863e6b44ca38ed58935bd4d74ba5c9` |
| `PROTO=CQ` `DISP=1_263` | **154492**（+448） | **195112**（+40） | 852（0） | 37396（0） | **121292**（+1208） | 2560 | 124704 / **6368B** | `dc0cf186c34af8fa7a6606aa732346c4` |
| 无显示等价口径（207 源文件，产物落 `.analysis/`） | **131928**（+448） | **191968**（+40） | 1584（0） | 16084（0） | **115704**（+1208） | 2560 | 119848 / **11224B** | `ce7fd155adac088b782f5b18cafe23d0` |

- 括号内 = 相对第一轮（RTT 移除态）的差值；**全部来自 RTT 底层恢复**（见 §1.3）。
- 告警：四口径各 **3 条**，全为既有 HAL `stm32f4xx_hal_flash_ex.c` 的 `-Wunused-parameter`，**零新增**。
- 日志/尺寸/md5 原始件：`.analysis/cleanup_20260929/round2_build/*.log|.size.txt|.md5.txt`。
- 构建脚本：`.analysis/cleanup_20260929/round2_build.sh`（含收尾把默认口径重建成 `build/Debug` 产物）。
- 权威段尺寸账已同步 `doc/06-04` **§8.4.4**；`doc/CLAUDE.md` 内存布局/CCM/调试三节与 `doc/构建开关总表.md`
  的「烧录前核对」数字一并更新为本表默认口径值。

---

## 5. 脚本复跑（按影响范围选取）

| 脚本 | 结果 | 与第一轮对比 |
|---|---|---|
| `.analysis/22_1665/verify_all.sh --fast` | **14 项全绿 / 0 失败 / 1 跳过** | 同第一轮；跳过项 = `check_flash_artifact.sh`（前置不满足，见 §7） |
| `.analysis/myanmar/mm_frame_sim.py` | **157/157** | 同第一轮（第一轮由 159 收敛为 157；本轮未改其断言） |
| `.analysis/myanmar/check_display_guards.py` | **16/16 硬断言 + 95/95 调用点** | 同第一轮 |
| `.analysis/myanmar/check_guard_disasm.py` | **原操作丢失 0**（92 函数 / 27 改动） | 判据不变（函数数随 A/B 基线与源码变化） |
| `.analysis/myanmar/check_eide_targets.py` | **40/45**（5 条 = Debug 现行口径差异，非回归） | 同第一轮；`--emit-make` 输出路径已改（§2.3） |
| `.analysis/yn_ol/yn_ol_frame_sim.py` | **184/184** | 同第一轮 |
| `.analysis/cleanup_20260929/check_token_equivalence.py` | **5/5 OK(仅删除)** | 本轮新增复核（§3.2 证据 ①） |
| `round2_ab_reconstruct.py` + `round2_ab_compare.sh` | **4/4 文件 section 逐字节 + 反汇编逐行一致** | 本轮新增工具 |

**因本轮改动而更新的断言/锚点**：

- `check_eide_targets.py`：`emit_make()` 增加 `BUILD_DIR` 改写（含 Makefile 形态 `assert`）、输出目录自动创建、
  推荐命令打印；用法 docstring 改为 `.analysis/myanmar/build_nodisp/` 路径；
- `.analysis/cleanup_20260929/cc_check.sh`：补回 `-I Middlewares/Third_Party/SEGGER_RTT`（RTT 源文件回归编译）；
- 文档侧脚本计数锚点：`mm_frame_sim 159→157`、`check_display_guards 99/99→95/95`、`check_guard_disasm`
  函数数说明（doc/16 §10 表、doc/CLAUDE.md 缅甸节、`.analysis/myanmar/README.md`）。

---

## 6. 文档更新清单

| 文档 | 更新点 |
|---|---|
| `doc/17_代码瘦身轮_20260929.md` | 标题/前言改「RTT **底层保留/已恢复** + 自研诊断设施移除 + 注释精简」；§1 表更正 `pl_rtt.c`（已恢复）与 `.eide/eide.yml`（**实际未删**）两处；新增 **§5 第二轮修正**（RTT 恢复清单/验证、build 清理、注释外移、新基线、A/B 证据） |
| `doc/CLAUDE.md` | 瘦身轮条目重写（底层保留/已恢复、业务零调用、诊断保持删除、printf 注记）；内存布局「实测」段换第二轮现行值（第一轮转历史）+ SRAM/CCM 行更新；Platform 层图 `pl_rtt` 恢复；调试节重写（底层已备、如何临时打点）；FreeRTOS 堆「诊断」条改现状；缅甸节补第二轮段尺寸/脚本计数/生成物路径 |
| `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` | 头部「最新采样」换第二轮（第一轮转「上一条」）；新增 **§8.4.4**（第二轮四口径 + RTT 增量归因 + `nm` 证据 + A/B）；§8.2 扩充内核静态任务缓冲账本（承接 `app_boot.c` 外移内容）；§8.5 诊断设施表标注已删 + 现行排障步骤；§8.6 纪律去 RTT 依赖；修订列表新增第二轮条目 |
| `doc/构建开关总表.md` | §0 标题与前言改「删除项 + RTT 底层保留」并新增 `BUILD_DIR` 能力说明；§3 变量表新增 `BUILD_DIR` 行；§4 烧录前核对数字换第二轮默认口径值 |
| `doc/16_缅甸2D费显协议/README.md` | 顶部横幅（RTT 恢复/零调用 + 现行段尺寸 `FACTORY=MM` 166216）；§9 等价口径复现命令改新路径 + 新增第二轮实测行；§10 脚本结果表计数改 95/95、92 函数；脚本计数锚点 157/157；变更表新增「第二轮」行 |
| `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | §0.6 现场核对方式改为无 `[diag]` 版（镜像身份三手段）；§5 前言同步 + **新增 §5.1 整屏逐点走位自检**（承接 `app_boot.c` 外移内容）；§5 表格里两处 `[diag]` 引用改写 |
| `.analysis/myanmar/README.md` | 生成物路径/命令更新；脚本计数 157/157、95/95 |

（`.analysis/**/archive/**` 与 doc 历史归档正文**未动**；本轮只改/新增「当前状态」类条目。）

---

## 7. 需用户操作项与遗留清单

### 7.1 ⚠ 待用户裁决：printf 重定向（1 项，本轮唯一口径歧义）

- **现象**：恢复的三个 SEGGER 源文件里，`SEGGER_RTT_Syscalls_GCC.c` 的职责本身就是「让 printf 走 RTT」——它提供
  **强 `_write` / `_write_r`**。因此恢复该文件后，**printf（仅 newlib/第三方/`LWIP_PLATFORM_ASSERT` 等错误路径，
  业务代码 0 处调用）会写进 RTT 通道 0**；`Core/Src/syscalls.c` 的弱 `_write` + 无输出 `__io_putchar` 仍原样保留，
  但被强符号覆盖（`nm` 可见 `_write_r` @ `0x0805673c`）。
- **口径来源**：与决定 1 的两句并存——「SEGGER RTT **三个源文件**加回编译清单」（仓库的真实第三个文件就是
  `SEGGER_RTT_Syscalls_GCC.c`，用户所列 `SEGGER_RTT_ASM_ARMv7M.S` 本仓不存在）与「**不恢复** printf 重定向
  （`Core/Src/syscalls.c` 维持无输出 `__io_putchar`）」。本轮按字面执行：文件全部加回、`syscalls.c` 一字未动。
- **两种收尾（任选其一，都是一行）**：
  - **A（现状，= 瘦身前形态）**：保持三文件。printf 输出回到 RTT 通道 0（业务代码无 printf ⇒ 实际只影响错误路径），
    调试时 printf 打点开箱即用；
  - **B（严格无 printf 重定向）**：把 `Middlewares/Third_Party/SEGGER_RTT/SEGGER_RTT_Syscalls_GCC.c` 从 Makefile
    `SRC_RTT` 移除（`SEGGER_RTT.c` + `SEGGER_RTT_printf.c` 保留，`pl_rtt_init` 与 RTT 读通道不受影响）。
    代价 = 段尺寸略降（`_write`/`_write_r` 与 printf 链可能被 gc 掉）。

### 7.2 其它遗留（非阻塞）

1. **`check_flash_artifact.sh` 仍为「跳过」**（`verify_all.sh --fast` 的 1 项跳过）：第一轮删掉 `[diag]` 横幅后，
   镜像内**不再有 `tree=` 指纹字符串**，该脚本「是当前树的 make 构建」判据无法成立（另叠加工作区 22_1665 宏值 8×4
   使 `DISP=22_1665` 的 make 产物本就装不下）。若需要恢复该判据，可改为「`build/Debug` 的 elf/bin/hex 三者同一构建
   + `arm-none-eabi-size -A` 对 §8.4.4 权威值」——属脚本增强，**本轮未擅自改**（未在裁决范围内）。
2. **EIDE 侧需确认**：`.eide/eide.yml` 本轮**未改**（RTT 条目本就在册）⇒ **无需** `EIDE: Reload Project`；
   唯一动作 = 若在 EIDE 里构建，注意其产物落 `build/Debug` 后按既有纪律 `make clean && make`。
3. **注释精简剩余清单**：见 §3.3 的复核结论（已无高价值冗余；如继续需逐条人工判）。
4. **第一轮遗留未变**：只读观察 API（`app_dispatch_*` 计数访问器 / `app_render_*_probe` / `app_udp_gzol_bind_status` 等）
   仍待裁决；`heap_4.c` 的 `xLastFailedAllocSize` 补丁无打印消费者（第三方未动）。
5. **未烧录、未提交、未改 git 配置、未执行任何 git 网络操作**（仅只读 `status/diff/show`）。

---

## 8. 证据文件索引（本轮新增）

| 路径 | 内容 |
|---|---|
| `.analysis/cleanup_20260929/round2_report.md` | 本报告 |
| `round2_build.sh` / `round2_build/` | 四口径构建脚本 + 日志/尺寸/md5 |
| `round2_ab_reconstruct.py` / `round2_ab_compare.sh` | 注释精简 A/B（重建改动前源码 → 同 flags 重编 → section/反汇编比对） |
| `round2_ab/before/`、`round2_ab/reconstruct/` | 第一轮构建对象快照、pre/cur 对象与反汇编 |
| `build_inventory_before.txt` | `build/` 清理前清单与体积 |
| `comment_blocks.py` | 长注释块定位工具（供后续注释精简定点使用） |
| `cc_check.sh`（已更新） | 单文件语法检查（补回 SEGGER include） |
