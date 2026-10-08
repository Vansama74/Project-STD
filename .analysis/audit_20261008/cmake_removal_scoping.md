# 「放弃本地 CMake 编译、仅用 EIDE、删除 cmake 口径相关文件」— 取证与问题清单

- 日期：2026-10-08（UTC+8）
- 范围：`/home/yystation/Program/3833024/Project-STD-main`（workspace；另抽查了同级 `STM32F407-Bootloader-master` / `STM32F407-Recovery-master` 与用户目录下疑似 CMake 工程）
- 性质：**只读轮**。未删除/修改任何仓库文件，未执行 make / cmake / EIDE 构建，未 make clean，未改 git 配置。唯一写入 = 本报告。
- 目的：为「删除 CMake 口径相关文件」提供穷举清单、影响面分类与待用户裁决问题；**不做任何删除决定**。

---

## 0. 结论速览（TL;DR）

| 问题 | 结论 |
|---|---|
| 工作区有无真正 CMake 工程文件（CMakeLists.txt / \*.cmake / CMakePresets.json / cmake/ 目录）？ | **没有。穷举结果 = 0 个**（含未跟踪文件、build/ 产物、git 历史、全仓文本 grep 大小写不敏感） |
| 工作区有无任何「cmake」字样引用（脚本/文档/配置）？ | **0 处**（`grep -rilI 'cmake' .` 含 build/、.cache/ 全文 0 命中） |
| 与「CMake 口径」字面沾边的实际存在物 | 只有**编译数据库 `compile_commands.json` ×2**（均在 `build/`，均未被 git 跟踪）+ 其消费方 `.clangd`。它们不是 CMake 产物：`build/Debug/` 那份由 **EIDE 构建**产生（证据见 §3.2） |
| 本仓库的「本地编译」到底是什么？ | **make（Makefile + 口径指纹 `.build_stamp`）**。如果用户说的「本地 CMake 编译」是「本地命令行编译」的口语化表述，被指对象应是 Makefile/make，量化影响见 §3.3（73 个 `.analysis` 脚本中 20 个含真实 make 调用，29 个依赖 build/ 产物或 make 日志） |
| A 类（CMake 专属、删除零影响）清单 | **空集** |
| 未跟踪、删后 git 不可恢复的候选 | `build/Debug/compile_commands.json`、`build/Release/compile_commands.json`、`build/Release/builder.params`、`.analysis/myanmar/build_nodisp/Makefile.mm_nodisp`（均为未跟踪；build/ 被 `.gitignore` 忽略）→ 若确要删，建议先归档（md5 见 §4.4） |
| EIDE 是否引用 CMake？ | 否。`.eide/eide.yml`、`build/*/builder.params`、`.eide/files.options.yml` 均 0 处 cmake |
| 直接可执行的「删 CMake 文件」动作 | **无可执行对象** —— 需用户澄清所指（见 §5 Q1） |

---

## 1. 取证方法与原始证据

全部为只读命令，关键命令与结果：

```bash
# 1) 文件名穷举（全深度，排除 .git）
find . -not -path './.git/*' \( -iname '*cmake*' -o -iname 'CMakeLists.txt' -o -iname '*.cmake' \
  -o -iname 'CMakePresets.json' -o -iname 'CMakeUserPresets.json' -o -iname 'compile_commands.json' \
  -o -iname '*toolchain*' \) | sort
# → 仅命中：./build/Debug/compile_commands.json、./build/Release/compile_commands.json（文件名均不含 cmake）

find . -type d -iname '*cmake*'            # → 空
grep -rilI 'cmake' .                       # 含 build/ 与 .cache/，忽略二进制 → 0 命中
grep -rl 'CMakeLists' --exclude-dir=.git . # → 空
git ls-files | grep -iE 'cmake|compile_commands|toolchain'   # → 空
git status --porcelain | grep -iE 'cmake|compile_commands|toolchain'  # → 空
git log --all --name-only --pretty=format: | sort -u | grep -i cmake  # → 空（历史从未含 cmake 文件）
git log -S'cmake' --oneline --all                                    # → 空（历史从未写过 cmake 字样）
```

健全性对照（证明 grep 本身有效，非命令失效）：同一台命令下 `grep -rilI 'bear'` 命中 4 文件（如 `doc/CLAUDE.md`、`.analysis/9k23881580/make_stamp_fix_report.md`），`grep -rilI 'compile_commands'` 命中 20+ 文件（Makefile、文档、build 日志等）。详见 §3。

工作区外抽查（只读）：

- `/home/yystation/Program/3833024/`（父目录，depth 4）`find -iname '*cmake*'` → **空**；两个兄弟工程（Bootloader / Recovery）也无 CMake 文件，其 `build/Debug` 均为 EIDE 产物目录。
- 用户目录范围内唯一与 STM32F407 相关的 CMake 工程 = `/home/yystation/EnvTools/STM32Cube/stm32f407zgt6/`（`CMakeLists.txt` + `CMakePresets.json` + `cmake/` 目录，2026-06-23，CubeMX CMake 模板，**独立工程，非本仓库**）；另有 `/home/yystation/Program/4627084/.../05Souce_code/USER/CMakeLists.txt`（另一个项目）与回收站内其它项目的 CMake 残留（`~/.local/share/Trash/files/05Souce_code*/`、`_restructure_backup_20260903/`）。
- IDE 侧：`.cursor/rules/` 为空目录；`~/.config/Cursor/User/settings.json` 无 cmake 配置；已装扩展中无 cmake-tools。

> 结论：**本仓库不存在任何 CMake 构建入口或 CMake 文件。** 「放弃本地 CMake 编译」在本工作区没有字面对应物。

---

## 2. A) CMake 相关文件穷举清单

### 2.1 真·CMake 文件（预期类别）＝ 空集

| 预期类别 | 实际命中 |
|---|---|
| `CMakeLists.txt` | 0 |
| `*.cmake` | 0 |
| `CMakePresets.json` / `CMakeUserPresets.json` | 0 |
| `cmake/` 目录 / `.cmake` 目录 | 0 |
| toolchain 文件（`*toolchain*.cmake` 等） | 0 |
| 文件名含 cmake 的其它条目 | 0 |

### 2.2 近邻候选项正面清单（名字不含 cmake、但属于「编译口径/编译数据库」语境）

| # | 路径 | 是否 git 跟踪 | 大小 / mtime | 实际用途与判定 | 分类 |
|---|---|---|---|---|---|
| 1 | `build/Debug/compile_commands.json` | 否（`/build` 在 `.gitignore`） | 376,681 B / 2026-10-08 09:40:13（md5 `3997248791f965a64412e791ba6bd171`） | **编译数据库**。内容为当前工作区路径 + EIDE Debug 口径（206 源、`-c -xc -std=gnu23 …`、incDirs 与 `builder.params` 逐序一致、紧凑 JSON）；mtime 与同日 EIDE 构建（`unify_builder.log` 09:40:14 `build successfully`）吻合 ⇒ **EIDE 构建产出**，不是 CMake | B：保留（clangd 依赖） |
| 2 | `.clangd` | **是**（含入 git；工作区无修改） | 2,188 B / 2026-10-08 09:10:47 | clangd 配置，第 41 行 `CompilationDatabase: ./build/Debug` —— **候选项 1 的唯一消费者**；另含 `--target=arm-none-eabi` 与全套 `-I` | B：保留（除非放弃 clangd） |
| 3 | `build/Release/compile_commands.json` | 否 | 285,112 B / 2026-08-11 11:02:21（md5 `817ad0389f5169d1e69d87aca9daf099`） | **陈旧残留**：`directory` = `/home/yystation/Desktop/Project-STD-main`（旧机/旧路径），include 全为已删除的 `Drivers/BSP/*`、`Drivers/RTT/*`、`LWIP/App` 等；`.clangd` 只指向 Debug ⇒ **无消费方**；`.analysis/cleanup_20260929/round2_report.md:61` 已记录「保留（原本存在，按要求不主动新建/不删）」 | B′：无消费方，可归档后清理（待裁，非 CMake） |
| 4 | `build/Release/builder.params` | 否 | 14,874 B / 2026-08-11（md5 `6595d107e263d7b2a1a5905e1b40545b`） | EIDE Release 目标参数（`rootDir=/home/yystation/Desktop/Project-STD-main`，`link-scatter` 也指 Desktop）——陈旧；`eide.yml targets.Release` 本身即陈旧（doc/16:413、doc/构建开关总表.md:79 有记载） | B′：同上 |
| 5 | `Makefile`（`compile_commands` 目标） | 是（M） | 23,883 B / 2026-09-29 10:27 | 另一条编译数据库生成路径：`Makefile:511, 519-527`（Python 内联生成 indent=2 JSON），`Makefile:520` 建根软链、`551` clean 删除；当前根软链 `compile_commands.json` **不存在**；当前 Debug 文件非此格式 ⇒ 不是它生成的 | B：与「仅 EIDE」口径强相关（见 §5 Q3） |
| 6 | `build/Debug/.build_stamp` | 否 | 12,668 B / 2026-09-29 10:33 | make 口径指纹（DISP/PROTO/FACTORY/DEFINES/源列表/Makefile cksum），make 增量与口径切换机制核心 | B：仅当停 make 才涉及 |
| 7 | `.vscode/settings.json` | 是 | 41 B | `makefile.configureOnOpen: false`（与 make 相关，与 cmake 无关） | C：文档/配置同步面 |
| 8 | `Project_STD.code-workspace` | 是（M） | 1,801 B / 2026-09-28 | `makefile.launchConfigurations`（含旧机路径 `/home/mqj/workspace/...`）；`C_Cpp.default.configurationProvider: cl.eide`（EIDE 已是 IntelliSense 提供者） | C：文档/配置同步面 |
| 9 | `.eide/eide.yml` + `build/*/builder.params` + `.eide/files.options.yml` | 是（M） | 35,722 B / 2026-10-08 09:40 | EIDE 工程与目标定义；**0 处 cmake**；EIDE 是最近一次实际构建入口 | B：保留 |
| 10 | `.analysis/myanmar/build_nodisp/Makefile.mm_nodisp` | 否（`.analysis/myanmar/` 整体未跟踪） | 20,637 B / 2026-09-29 10:29（md5 `fb98e501b7ce659dda0a2e006e9a2fec`） | 由 `.analysis/myanmar/check_eide_targets.py --emit-make` **以仓库 Makefile 为模板**生成的「EIDE 目标等价口径」临时 Makefile（脚本 :161 对 `BUILD_DIR ?= build/$(CONFIG)` 有硬断言）——make 在此充当 **EIDE 口径复现工具** | B：生成物，依赖 Makefile 存在 |
| 11 | `build/Debug/.cache/clangd/`（445 文件，4.0 MB） | 否 | 2026-10-08 | clangd 后台索引缓存（由 `.clangd` 决定重建），可安全重建 | B′：缓存，可清理 |

### 2.3 需要用户澄清的「其它 CMake 可能指代」

1. 本机 `~/EnvTools/STM32Cube/stm32f407zgt6/`（CMakeLists + CMakePresets，近 4 个月未动，独立模板工程）；
2. `~/Program/4627084/...` 与回收站内的 CMake 工程（其它项目）；
3. 用户 IDE 里可能存在的 CMake Tools 扩展/工作区（本 workspace 未发现任何 cmake 配置）；
4. 或者，用户将「本地 CMake 编译」用作了 **「本地命令行/make 编译」的口语替代**（本仓库唯一的本地构建就是 make，见 §3.3）。

---

## 3. B) 引用与依赖

### 3.1 「cmake」字样引用 = 0

对以下所有位置做了大小写不敏感检索，命中数均为 0：

- 根目录与各级 `Makefile`（`grep -in cmake Makefile` → 空）；
- `.vscode/`（settings/tasks/launch）、`.eide/`（eide.yml、files.options.yml）、`Project_STD.code-workspace`、`.cursor/`、`Compiler/`；
- `tool/`（仅 `flash_all.sh`）、`.analysis/`（全部 .sh/.py/.md/.json/.log）、`doc/`（54 个 md）、`*.md`、`.mxproject`、`Project_STD.ioc`；
- `build/` 全部产物（含 `builder.params`、`ref.json`、`unify_builder.log`、`statistic.json`）。

### 3.2 `compile_commands.json` 的生产者与消费者

**生产者（3 条路径，实际生效的是第 1 条）**

| # | 产出方式 | 证据 | 是否现行 |
|---|---|---|---|
| 1 | **EIDE 构建** | `build/Debug/compile_commands.json` mtime 09:40:13 ↔ `build/Debug/unify_builder.log` 最后一条 09:40:14 `build successfully`；内容与同目录 `builder.params`（`dumpPath/outDir=build/Debug`、206 源、incDirs 顺序）一致；历史记录 doc/16:402、doc/CLAUDE.md:1067 提到 EIDE 有 `--only-dump-compilerdb` 调用 | **是**（当前文件即 EIDE 产出） |
| 2 | `make compile_commands` | `Makefile:519-527`（indent=2 格式 + `Makefile:520` 根软链；`551` clean 删除）；当前文件为紧凑 JSON ⇒ 不是它写的 | 可用但当前未用 |
| 3 | `bear --output build/Debug/compile_commands.json -- make -B -j8` | 仅文档记载：`doc/CLAUDE.md:220`（`.analysis/9k23881580/make_stamp_fix_report.md:397` 引述） | 文档口径 |

**消费者**

| 消费方 | 位置 | 说明 |
|---|---|---|
| clangd | `.clangd`（`CompilationDatabase: ./build/Debug`） | 当前唯一真实消费者；`.clangd` 为 git 跟踪文件 |
| 其它 | 无 | `.vscode/`、`.cursor/`、`tool/`、`.analysis/` 无任何 compile_commands 消费代码（仅 build 日志文本提及） |

**其它相关**：根目录软链 `compile_commands.json` 当前不存在（`ls` 报 No such file）；`make clean` 会尝试删除它（`Makefile:551`，`rm -rf` 对其无害）。

### 3.3 make 依赖量化（供「是否连 make 一起停用」参考，**本轮不做决定**）

**脚本侧**

- `.analysis/` 共 **73** 个脚本（27 个 `.sh` + 46 个 `.py`）；其中 **24** 个含 `make` 字样，**20** 个含非注释的 make 命令：

| make 调用行数 | 脚本 |
|---|---|
| 5 | `.analysis/cleanup_20260929/round2_build.sh`、`.analysis/yn_ol/ab_measure.sh` |
| 4 | `.analysis/yn_ol/round2_verify.sh`、`.analysis/22_1665/archive/scripts/sync_docs.py` |
| 3 | `.analysis/22_1665/analyze_board_flash.py`、`.analysis/22_1665/check_flash_artifact.sh`、`.analysis/9k23881580/build_sweep.sh`、`.analysis/9k23881580/build_sweep_triage.sh`、`.analysis/myanmar/check_eide_targets.py`、`.analysis/yn_ol/final_verify.sh` |
| 2 | `.analysis/cleanup_20260929/build_and_record.sh`、`.analysis/heap/verify_builds.sh`、`.analysis/22_1665/archive/scripts/_round22_compress_claude_md.py` |
| 1 | `.analysis/22_1665/link_variant.sh`、`.analysis/22_1665/round26_two_hubs.py`、`.analysis/22_1665/verify_all.sh`、`.analysis/cleanup_20260929/round2_ab_reconstruct.py`、`.analysis/heap/heap_ledger.py`、`.analysis/yn_ol/guard_test.sh`、`.analysis/22_1665/archive/scripts/check_round21.py` |

- 另有 **29** 个脚本（含 `tool/flash_all.sh`）引用了 `build/Debug` 产物（`Project_STD.elf/.hex`）或 make 构建日志，其中关键链路：
  - `.analysis/22_1665/verify_all.sh`：一键入口 17 项检查；**全量模式**经 `link_variant.sh` → `pick_log.sh`（从 `build_round*.log` 抓「驱动编译命令 + 整项目链接命令」），**依赖先跑过一次 make**；`--fast` 跳过该变体；
  - `.analysis/22_1665/check_round30_ab.sh` / `check_round32_ab.sh` / `check_round34_ab.sh`：同样从 make 构建日志取命令行做宿主 A/B；
  - `.analysis/myanmar/check_eide_targets.py --emit-make`：**以仓库 Makefile 为模板**生成等价口径 Makefile（对 `BUILD_DIR ?= build/$(CONFIG)` 有硬断言）——make 在此的角色是「EIDE 口径复现器」；
  - `.analysis/heap/verify_builds.sh`、`.analysis/yn_ol/*.sh`、`.analysis/9k23881580/build_sweep*.sh`、`.analysis/cleanup_20260929/*`：真实 `make clean` + `make -j8 [PROTO=… DISP=… FACTORY=…]` 多口径重编。

**工具侧**

- `tool/flash_all.sh`（唯一 tool 脚本）：**0 处 make**；只读 `Project-STD-main/build/Debug/Project_STD.hex`（:67）与两个兄弟工程 hex（:65-66）。EIDE 的 `outDir/dumpPath = build/Debug` 与 make 同路径 ⇒ **flash_all.sh 对 EIDE 产物天然兼容**，不受「停 make」直接影响（但要保证 EIDE 构建后产物确实在该路径）。

**文档侧**（make 字样出现次数，`doc/` 下 18 个 md 命中）

| 次数 | 文档 |
|---|---|
| 52 | `doc/构建开关总表.md`（§4 口径指纹/构建卫生、§3 EIDE 对照） |
| 39 | `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` |
| 32 | `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` |
| 28 | `doc/CLAUDE.md`（构建系统全节 + :220 编译数据库） |
| 15 | `doc/16_缅甸2D费显协议/README.md`（§9.1 EIDE/make 对照） |
| 11 | `doc/15_云南治超屏协议/README.md` |
| 9 | `doc/14_贵州治超屏协议/README.md` |
| 5 | `doc/04_青海高速费显协议/PartB_…`、`doc/03_重庆高速二代费显协议/PartB_…` |
| 4 | `doc/01_显示系统/22-1703模组驱动分析与迁移记录.md` |
| 3 | `doc/13_安徽费显协议/README.md`、`doc/08_协议模块接入规则/04_接入检查清单.md` |
| 2 | `doc/11_云南费显协议/README.md`、`doc/04_青海高速费显协议/PartA_…` |
| 1 | `doc/17_代码瘦身轮_20260929.md`、`doc/07_LDI与IAP配置解耦/03_interim_build_rules.md`、`doc/03_重庆高速二代费显协议/PartA_…` |

`bear` 字样引用：仅 2 处（`doc/CLAUDE.md:220`、`.analysis/9k23881580/make_stamp_fix_report.md:397`）。

### 3.4 EIDE 侧观察

- `.eide/eide.yml`、`build/Debug/builder.params`、`build/Release/builder.params`、`.eide/files.options.yml`：**0 处 cmake**。
- EIDE 已是最近一次实际构建入口：`build/Debug/unify_builder.log` 2026-10-08 09:40:14 `build successfully`；`compiler.log` 显示 206 个 C 文件、`Rebuild`、RAM 91.65% / CCMRAM 92.84% / FLASH 32.25%。
- EIDE 具备编译数据库导出能力（历史提到 `--only-dump-compilerdb`；当前 `compile_commands.json` 的实际来源即 EIDE 构建）⇒ **「仅用 EIDE」不必然丢失 clangd 工作流**，前提是保留 `.clangd` 的 `CompilationDatabase` 指向。
- EIDE 的 `targets.Release` 为陈旧配置（incList 为已删除的 `Drivers/BSP/*`；doc/构建开关总表.md:79、doc/16:413 已记载），非可用入口；`build/Release/` 下产物（候选项 3、4）即该历史时代残留。

---

## 4. C) 删除影响面判定

### 4.1 分类表

| 分类 | 条目 | 删除影响 | 处置建议（本轮不执行） |
|---|---|---|---|
| **A 类：CMake 专属、删除零影响** | **空集** | — | 无对象可删；需用户澄清（Q1） |
| **B 类：与其它工具共享，需保留或转换** | ① `build/Debug/compile_commands.json`（clangd 消费，EIDE 产出）② `.clangd`（clangd 配置）③ `.eide/eide.yml` + `builder.params`（EIDE 工程本体，勿动）④ `Makefile` + `build/Debug/.build_stamp` + Makefile `compile_commands` 目标（本地 CLI 与 20 个分析脚本的构建/复现基础）⑤ `tool/flash_all.sh`（消费 build/Debug 产物，与 EIDE 路径兼容）⑥ `.analysis/myanmar/build_nodisp/Makefile.mm_nodisp`（由 Makefile 模板生成） | 逐项见 §3 | ①②③⑤**保留**；④是否降级见 Q3；⑥可随 make 口径一并处置 |
| **B′ 类：无消费方、可归档后清理（非 CMake）** | ① `build/Release/compile_commands.json`（Desktop 时代残留）② `build/Release/builder.params` ③ `build/Debug/.cache/clangd/`（可重建缓存） | 无脚本/配置消费；③为缓存 | 如用户希望「清爽化」，建议**先归档再删**（§4.4）；注意 ①② 曾被 2026-09-29 轮记录为「保留」 |
| **C 类：若口径变更需同步的文档/配置** | `.clangd`、`Project_STD.code-workspace`（`makefile.launchConfigurations` 含旧机路径）、`.vscode/settings.json`（`makefile.configureOnOpen`）、`doc/CLAUDE.md`（构建系统节 + :220 bear 口径）、`doc/构建开关总表.md`（§3/§4 与 `BUILD_DIR`）、以及 §3.3 文档表中全部 18 个 md（若 make 也停用） | 视裁决口径（仅删 cmake = 无文档需改；停 make = 大面积更新） | 建议与删除动作同一轮做（Q5） |

### 4.2 「删除后需要同步更新」的精确定位（若最终口径 = 停 make）

- `doc/CLAUDE.md`：构建系统节（`make -j8 …` 四口径示例、口径指纹 `.build_stamp`、EIDE 覆写 build/Debug 的 R1~R3 纪律）、:220 `bear` 编译数据库、`make clean`、烧录前 `arm-none-eabi-size` 核账等；
- `doc/构建开关总表.md`：§0 开关清单（部分以 make 命令描述）、§3 EIDE 对照、§4 口径指纹机制、:127 `BUILD_DIR`；
- `doc/01/22-1665`、`doc/06-04`、`doc/14/15/16`、`doc/17` 中的 `make …` 复现命令与构建基线数字的「复现方式」表述；
- `Project_STD.code-workspace`：`makefile.launchConfigurations`（陈旧 `/home/mqj` 路径，早已失效，可顺手清理）；
- `.vscode/settings.json`：`makefile.configureOnOpen: false`（无功能影响，可保留）。

### 4.3 EIDE 侧 CMake 引用核查

- `.eide/eide.yml`：0 cmake；`outDir: build`，`targets: Debug / Myanmar / Release`。
- `build/Debug/builder.params`：`rootDir = 当前工作区`，`dumpPath/outDir = build/Debug`，`sourceList` 206 项，`EIDE_BUILDER_DIR = ~/.cursor/extensions/cl.eide-3.27.2-universal/res/tools/linux/unify_builder` —— 全链路无 CMake。
- 结论：**删除 CMake 不影响 EIDE**（因为本来就没有）；EIDE 当前已可直接承担编译 + 产出 compile_commands.json + 供 clangd 使用。

### 4.4 未跟踪文件归档建议（备查，md5 已算好）

建议归档目录（本轮**不创建、不复制、不删除**）：`.analysis/audit_20261008/cmake_archive/`

| 文件 | md5 | 大小 |
|---|---|---|
| `build/Debug/compile_commands.json` | `3997248791f965a64412e791ba6bd171` | 376,681 B |
| `build/Release/compile_commands.json` | `817ad0389f5169d1e69d87aca9daf099` | 285,112 B |
| `build/Release/builder.params` | `6595d107e263d7b2a1a5905e1b40545b` | 14,874 B |
| `build/Debug/.build_stamp` | `c27dc4a95382d7318fdd910666a327b6` | 12,668 B |
| `.analysis/myanmar/build_nodisp/Makefile.mm_nodisp` | `fb98e501b7ce659dda0a2e006e9a2fec` | 20,637 B |

> 注：`build/` 被 `.gitignore` 忽略，这些文件**删除后 git 无法恢复**；`.analysis/myanmar/` 整体未跟踪同理。

---

## 5. D) 问题清单（待用户裁决）

**Q1（最高优先，阻塞执行）：本仓库没有 CMake 文件，请澄清「cmake 口径相关文件」具体指什么？**
- 证据：§2.1（0 命中，含未跟踪与 git 历史）；唯一沾边的是 `compile_commands.json` ×2（§2.2 #1/#3）。
- 可能解释：① 用户把「本地命令行构建（make）」称作 CMake → 请直接回答 Q3；② 用户指工作区外某个 CMake 工程（§2.3 列表）→ 请指定路径；③ 用户指 `compile_commands.json` 等编译数据库 → 见 Q2。
- **若无人指认具体路径，本轮无可执行删除动作。**

**Q2（B 类保留）：`compile_commands.json` / clangd 工作流是否保留？如何生成？**
- 现状：`.clangd` 只认 `./build/Debug`；该文件当前由 **EIDE 构建自动产出**（亦可 `make compile_commands`，或文档口径的 bear）。
- 请裁决：保留 clangd（默认建议）还是彻底停用；若保留，生成方式是否需要写进文档（建议明确「EIDE 构建产出」）。

**Q3（最大口径问题）：「仅使用 EIDE 进行编译」是否意味着 make / Makefile 也要停用？**
- 量化：`.analysis` 73 个脚本中 20 个含真实 make 调用（§3.3 表）；29 个脚本 + `tool/flash_all.sh` 依赖 `build/Debug` 产物或 make 日志；`verify_all.sh` 全量、`check_round30/32/34_ab.sh`、`link_variant.sh`、`check_eide_targets.py --emit-make` 均以 make 为复现/取证工具；`doc/` 18 个 md 含 make 命令（其中 `doc/CLAUDE.md`、`doc/构建开关总表.md` 是纪律性描述）。
- 请裁决：① 只放弃「用 CMake」（本仓库本就是 0，无动作）而**保留 make**；还是 ② 连 make 一起停用（则 §3.3 的 20 个脚本、A/B 取证链、文档需同步改造，建议改为「宿主直调 arm-none-eabi-gcc（EIDE 原命令复现，已有先例 `check_eide_command.sh`）+ EIDE CLI 构建」）。

**Q4：未跟踪文件是否先归档到 `.analysis/audit_20261008/cmake_archive/`？（§4.4 清单）**
- 建议：是（先归档后删；归档后即使误删也可恢复）。清单与 md5 已备好，待确认后执行。

**Q5：文档同步范围与时机？**
- 若裁决仅「清理陈旧的 `build/Release` 产物 + 编译数据库口径」→ 涉及 `doc/CLAUDE.md:220`、`doc/构建开关总表.md`、`.clangd` 说明、`Project_STD.code-workspace`（旧机路径）等少量；
- 若裁决停 make → 需更新 §3.3 文档表所列 18 个 md 及脚本，工作量显著，建议单独立轮。
- 请裁决：本轮一并做，还是另开一轮？

**Q6（附带）：`build/Release/` 陈旧产物（compile_commands.json / builder.params，2026-08-11，指向 Desktop 旧路径）是否顺手清理？**
- 无任何消费方；曾被 2026-09-29 轮记录为「按要求保留」。建议归档后清理（§4.4）。

**Q7（附带）：工作区外的 CMake 工程是否在本次指令范围内？**
- `~/EnvTools/STM32Cube/stm32f407zgt6/`（CMakeLists + Presets）、`~/Program/4627084/...`、回收站残留 —— 均非本仓库；若用户想清理它们，请确认（且注意回收站/其它工程删除不可逆，需单独评估）。

**Q8（附带）：EIDE `targets.Release` 陈旧配置（incList 指向已删除的 `Drivers/BSP/*`）是否顺带修复/删除该目标？**
- 属 EIDE 口径整理（与「仅用 EIDE」同主题），但需在 EIDE GUI 内改并 `EIDE: Reload Project`，纯文件编辑会被回写冲掉。

---

## 6. 附录：原始证据摘录

- `find`（全深度、排除 .git）文件名含 cmake → 0；`find . -type d -iname '*cmake*'` → 0；`grep -rilI 'cmake' .`（含 build/.cache）→ 0。
- `git ls-files` 过滤 `cmake|compile_commands|toolchain` → 0；`git status --porcelain` 过滤 → 0；`git log --all` 文件名/S-pick → 0。
- 未跟踪清单（`git status --porcelain` 的 `??` 项）：`.analysis/audit_20261008/`、`.analysis/cleanup_20260929/`、`.analysis/myanmar/`、`Application/Inc|Src/ProtocolParser_Myanmar/`、`Application/Src/app_factory_test_mm.c`、`doc/16_缅甸2D费显协议/`、`doc/17_代码瘦身轮_20260929.md` —— **无 cmake 条目**。
- `build/Debug/compile_commands.json` 首条：`{"directory":"/home/yystation/Program/3833024/Project-STD-main","file":"…/app_mqtt.c","command":"…arm-none-eabi-gcc -c -xc -std=gnu23 -I…"}`（EIDE 特征）。
- `build/Release/compile_commands.json` 首条：`{"directory":"/home/yystation/Desktop/Project-STD-main", …, "-IDrivers/BSP/Components/dp83848", …, "-DAPP_PROTOCOL=1", "-Os"}`（陈旧 + 旧代码结构）。
- `build/Debug/builder.params`：`rootDir`/`dumpPath`/`outDir` = 当前工作区/`build/Debug`；`sourceList` 206；`EIDE_BUILDER_DIR` = EIDE 扩展目录。
- `build/Debug/unify_builder.log`：2026-10-08 09:40:14 `build successfully`。
- `.clangd` 第 41 行：`CompilationDatabase: ./build/Debug`。
- `Makefile:519-527` `compile_commands` 目标；`Makefile:551` clean；根软链不存在。
- `.analysis/22_1665/pick_log.sh`：从 `build_round*.log` 抓 `-c -o build/Debug/Device/Display/dev_display_22_1665.o`（依赖 make 日志）。
- `.analysis/myanmar/check_eide_targets.py:161`：`assert "BUILD_DIR ?= build/$(CONFIG)" in src`（以仓库 Makefile 为模板）。
- `tool/flash_all.sh:65-67`：`DFL_BOOT/DFL_RECV/DFL_APP` 三个 hex 路径；无 make 调用。

---

*报告完（只读轮，未做任何删除/修改）。*
