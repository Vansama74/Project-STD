# Makefile 口径指纹 stamp 修复 —— 独立复核（验证）报告

**复核人**：构建验证（独立于实施者）
**日期**：2026-09-14
**范围**：只做验证，未修改任何业务源码 / Makefile / `.eide/eide.yml`；未做 git 网络操作；未 commit。
**验证对象**：`Makefile` 的 `build/$(CONFIG)/.build_stamp` 口径指纹机制（实施者报告 `.analysis/9k23881580/make_stamp_fix_report.md`）。

---

## 0. 结论摘要

| 项 | 结论 |
|---|---|
| 口径指纹修复是否成立 | **成立**。四条口径的段尺寸与 elf md5 **与实施者声称值逐项完全一致**；切口径必全量重编+重链接；同口径重复 make 零编译零链接；`make clean` 后从零构建可复现默认口径 md5；头文件依赖精确到 4 个 TU |
| 复核者先前看到的「异常 elf」是什么 | **EIDE 的 Debug 构建产物**，不是 make 的任何口径。它被 EIDE 以更新的 mtime 覆盖到 make/EIDE 共用的 `build/Debug/Project_STD.elf` |
| 「`make -n` 显示所有 `.o` 过期」的原因 | **dry-run 假象**：stamp 带 `FORCE` 先决条件，`make -n` 不执行配方、假设 stamp 已改动，于是把所有依赖 stamp 的目标判为过期。真实 `make` 因 stamp 内容 `cmp` 相同而 mtime 不变，**0 编译 0 链接**（实测 A8） |
| 是否发现修复缺陷 | **未发现本轮修复本身的功能缺陷**（无需改 Makefile）。发现 3 条**遗留风险/陷阱**（见 §6），其中最重要的是 EIDE 覆盖共享产物后 `make` 会 **no-op 并保留错误口径 elf** |
| 当前 `build/Debug/Project_STD.*` | **默认口径（`PROTO=ALL` `DISP=1_263`）的 make 产物**，elf md5 `2f02963213367a45279cd5f45ab33746`，**可直接烧录** |

---

## 1. 当前 elf 来源判定（复核者先前看到的 15:11:34 产物）

### 1.1 判定：EIDE Debug 构建产物（非 make 任何口径）

判定依据（多条独立证据互相印证）：

| # | 证据 | 内容 |
|---|---|---|
| 1 | **EIDE 独有产物文件** | `build/Debug/` 下存在 `Project_STD.lnp`、`Project_STD.objlist`、`Project_STD.map.view`、`compiler.log`、`builder.params`、`ref.json`、`statistic.json`、`.obj/` 目录、`.lock` —— 这些是 EIDE（cl.eide 扩展）统一构建器的产物，make 从不生成 |
| 2 | **EIDE 对象目录** | `.lnp` 链接参数里全部对象为 `./build/Debug/.obj/.../*.o`（EIDE 专用目录），而非 make 的 `./build/Debug/<源码路径>.o` |
| 3 | **构建日志** | `build/Debug/compiler.log` 头部 `Builder Mode: Rebuild`、`The amount of C files: 210`；`unify_builder.log` 有 `[2026-09-14 15:11:21] [done] build successfully !` 与 `15:11:34 [done]` 两条 |
| 4 | **`.map.view` 编译器路径** | `compilerPath: '/home/yystation/EnvTools/embedded-toolchain/arm-gnu-toolchain/bin/arm-none-eabi-gcc'`，是 EIDE 目标配置的编译器（make 用同名系统命令，二者同版本，**故编译器版本无法区分**，须靠产物形态区分） |
| 5 | **源收录集与 make 任一口径都不同** | `Project_STD.objlist` 含 `ProtocolParser_GuiZhou_Overload` + 四川三协议 + LDI + IAP + `app_scroll.o`，显示驱动为 **`dev_display_22_1703.o`**；**不含** CQ / Anhui / QingHai / GuiZhou(常规) / ShanDong / YunNan / RLS / AH_MQTT |
| 6 | **符号级确认**（对备份 elf `arm-none-eabi-nm`） | 有 `dev_display_22_1703_init`、`gz_ol_proto_init`、`sc_etc/mtc/ol_proto_init`、`ldi_module_init`、`iap_module_init`；**无** `dev_display_1_263_init`、`cq_proto_init`、`anhui_proto_init`、`qh_proto_init`、`gz_proto_init`、`sd/yn_proto_init`、`rls_module_init`、`ah_mqtt_module_init` |
| 7 | **CCM 尺寸自洽** | EIDE elf `.ccmram = 32784`，恰 = 22-1703 显存 31648 + 贵州治超队列入 CCM 1136。若为 1-263 口径应为 37084（29568+6377+1136），若含 CQ 也非此值 |

### 1.2 EIDE elf 实测段尺寸（备份 `eide_elf_backup.elf`）

`arm-none-eabi-size -A`：`.text=150628  .rodata=106512  .data=1712  .ccmram=32784  .bss=117624`
默认 `size` 汇总列：`text=257844  data=1712  bss=152968`，elf md5 `a52adb36ff09c3679382b33570f7f18d`，大小 1981736 B，mtime `15:11:34.886`。

> **关于复核者读到的 `text 257492`**：`arm-none-eabi-size`（不带 `-A`）的 `text` 列是**所有只读段之和**（`.text` + `.rodata` + `.init/.fini/.ARM` 等），并非 `.text` 段本身。本报告口径一律用 `size -A` 的逐段值，与实施者报告、文档账目一致。EIDE elf 的只读汇总为 257844，与 257492 的细小差异来自采样时点（可能在上一次 EIDE 构建产物上读取）。

### 1.3 并发构建检查

`ps -ef | grep -E 'make|arm-none-eabi-gcc'` **无匹配**（grep EXIT=1）。确认无并发构建后，才开始本报告 §2 的构建序列。EIDE 侧 `unify_builder.log` 显示 15:11:34 已完成、无进行中任务。

---

## 2. 重跑验证：命令 / EXIT / 计数 / 耗时 / 段尺寸 / md5

所有命令在 `/home/yystation/Program/3833024/Project-STD-main` 下执行；`段尺寸` 取 `arm-none-eabi-size -A build/Debug/Project_STD.elf`；`md5` 取 `md5sum` 该 elf。`编译`/`链接` 计 `Compiling`/`Linking` 行数。原始日志见 `/tmp/stampverify/<tag>.log`。

| 步骤 | 命令 | EXIT | 编译 | 链接 | 耗时 | .text | .rodata | .data | .ccmram | .bss | elf md5 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| A1 | `make -j8`（默认） | 0 | 0 | 0 | 0.11s | 150628 | 106512 | 1712 | 32784 | 117624 | `a52adb36…`（**EIDE 残留，未变**） |
| A2 | `make -j8`（再跑） | 0 | 0 | 0 | 0.10s | 150628 | 106512 | 1712 | 32784 | 117624 | `a52adb36…`（未变） |
| A3 | `make -j8 PROTO=CQ` | 0 | **232** | **1** | 1.67s | 150476 | 197104 | 860 | 37084 | 122872 | `36ef6fb1…` |
| A4 | `make -j8`（回默认） | 0 | **237** | **1** | 1.57s | 163652 | 197760 | 1664 | 37084 | 126348 | `2f029632…` |
| A5 | `make -j8 DISP=22_1703` | 0 | **237** | **1** | 1.53s | 163924 | 197840 | 1744 | 39164 | 126348 | `3cecd9a3…` |
| A6 | `make -j8 DISP=22_1703 PROTO=CQ` | 0 | **232** | **1** | 1.51s | 150732 | 197192 | 940 | 39164 | 122872 | `7ba2b3ef…` |
| A7 | `make -j8`（回默认） | 0 | **237** | **1** | 1.55s | 163652 | 197760 | 1664 | 37084 | 126348 | `2f029632…` |
| A8 | `make -j8`（增量 no-op） | 0 | 0 | 0 | 0.11s | 163652 | 197760 | 1664 | 37084 | 126348 | `2f029632…`（未变） |
| A9a | `make clean` | 0 | — | — | 0.12s | — | — | — | — | — | —（`build/Debug` 已删除） |
| A9b | `make -j8`（clean 后从零） | 0 | **237** | **1** | 1.78s | 163652 | 197760 | 1664 | 37084 | 126348 | `2f029632…` |
| H1 | `touch Application/Inc/app_scroll.h` → `make -j8` | 0 | **4** | **1** | 0.37s | 163652 | 197760 | 1664 | 37084 | 126348 | `2f029632…` |
| H2 | `make -j8`（头依赖后增量） | 0 | 0 | 0 | 0.10s | 163652 | 197760 | 1664 | 37084 | 126348 | `2f029632…`（未变） |

完整 md5：默认 `2f02963213367a45279cd5f45ab33746`；CQ `36ef6fb13a7c41906b507c8d10d4ed8a`；22_1703 `3cecd9a32bfab51f11f095151e85d3d1`；22_1703+CQ `7ba2b3ef8ed918fbf9e3b1a6802816e6`。

**补充证据**：

- **stamp 幂等**：A8 前后 `.build_stamp` mtime 均为 `2026-09-14 15:15:05.668096614`（未被触碰）；A8 `Compiling` 行为空。
- **stamp 内容与当前 Makefile 一致**：用 make 展开 `BUILD_STAMP_TEXT`（`make --eval` 打印）与磁盘 `.build_stamp` 逐字节比对 **完全一致**（同为 12247 B，md5 均 `df71161323760f6cb1439d816d32c29b`）—— 即该 stamp 未被 EIDE 改动，实施者所述「cksum 与当前一致」属实。
- **头依赖只重编 4 个 TU**（H1）：`Application/Src/app_scroll.c`、`Application/Src/RLS/app_rls_cmd.c`、`Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c`、`Application/Src/ProtocolParser_Anhui/app_anhui_proto_cmd.c` —— 与实施者报告 §3.4 完全一致。
- **口径守卫**：`make -n DISP=bogus -j8` → `Makefile:67: *** DISP must be 1_263 or 22_1703, got 'bogus'`，**EXIT=2**（与声称一致）。
- **告警**：A9b 从零构建仅出现 `Drivers/STM32F4xx_HAL_Driver/...` 既有 `-Wunused-parameter` 告警，无新增告警、无 error。

---

## 3. 与「声称值」的差异清单

实施者声称的四条口径（`arm-none-eabi-size -A` + md5）与本轮实测：

| 口径 | 声称 .text/.rodata/.data/.ccmram/.bss | 实测 | 结论 |
|---|---|---|---|
| `ALL` `1_263` | 163652 / 197760 / 1664 / 37084 / 126348，md5 `2f0296…` | 完全一致（A4/A7/A9b） | ✅ 一致 |
| `CQ` `1_263` | 150476 / 197104 / 860 / 37084 / 122872，md5 `36ef6f…` | 完全一致（A3） | ✅ 一致 |
| `ALL` `22_1703` | 163924 / 197840 / 1744 / 39164 / 126348，md5 `3cecd9…` | 完全一致（A5） | ✅ 一致 |
| `CQ` `22_1703` | 150732 / 197192 / 940 / 39164 / 122872，md5 `7ba2b3…` | 完全一致（A6） | ✅ 一致 |

**差异清单：四条口径零差异。** 复核者先前观察到的「与任一行都不符」的产物 **不属于 make 的这四条口径**，而是 §1 所述 EIDE Debug 产物（22-1703 + GZ_OL + 四川三协议，无 CQ），是**共享目录被异构构建覆盖**的干扰，**不是** stamp 修复的采样污染，也不是实施者报告的残留错误。

> 关于报告 §4 提到的「基线污染态（`PROTO=ALL` 原记 164068/164324）」：本轮干净重采得的 163652/163924 与声称值一致，实施者对该污染的归因（陈旧 CQ 对象混链 → `cq_render_fault_screen` 等符号被 `--gc-sections` 保留，+416 text/+40 rodata）在数值上与干净 A/B 结果自洽。本轮不重做该符号级取证。

---

## 4. 修复缺陷判定

**未发现本轮修复的代码缺陷**，无需对 `Makefile` 做最小修复。逐项核对：

| 修复目标 | 验证证据 | 判定 |
|---|---|---|
| 切口径必全量重编+重链接 | A3/A4/A5/A6/A7 各 232~237 编译 + 1 链接；往返切换（A4→A3→A4、A5→A6→A5→A7）均正确 | ✅ |
| 同口径重复 make 零编译零链接、md5 不变 | A2、A8、H2 均 0/0，md5 与 mtime 不变；stamp mtime 不变 | ✅ |
| 不再需要手工删产物 | 全序列未删任何产物（除最后 `make clean` 的显式验证） | ✅ |
| `make clean` 后从零构建正确 | A9b 237/1，md5 与干净默认态一致 | ✅ |
| 头文件依赖（`-MMD -MP`） | H1 精确 4 个 TU，H2 归零 | ✅ |
| `-MMD` 不改变目标码 | 实施者已用 md5 证明；本轮段尺寸/md5 与声称一致，间接互证 | ✅ |
| 非法 `DISP` 守卫 | EXIT=2 | ✅ |

**两点澄清（均非缺陷）**：

1. **`make -n` 的「全量过期」假象**：stamp 规则带 `FORCE` 先决条件，在 dry-run 下 make 不执行 stamp 配方、却按「先决条件已重建」推进，于是把所有 `.o`/elf 判为过期。**真实构建**因 `cmp` 相同、stamp mtime 不变而为 no-op（A8 实证）。这是正常的 make 语义，不构成缺陷；但容易误导排障者把它当成「stamp 失效」，建议在 `doc/构建开关总表.md` §4 补一句提示。
2. **`PROTO` 无非法值守卫**：`make -n PROTO=bogus -j8` **EXIT=0**，即任意非 `CQ` 的 `PROTO` 值都静默按 `ALL` 处理（`Makefile:48 ifeq ($(PROTO),CQ)`）。与 `DISP` 的显式 `$(error)` 不对称。此为**既有行为**，非本轮引入，本轮未改。

---

## 5. 最终工作区状态

- **口径**：默认 `PROTO=ALL` `DISP=1_263`（GCC Debug）。
- **产物**：`build/Debug/Project_STD.elf` mtime `2026-09-14 15:15:28.870`，大小 2055252 B；
  - **elf md5 = `2f02963213367a45279cd5f45ab33746`**（与实施者声称的默认口径 md5 逐字节一致）
  - hex md5 `64c1ea647658d5ddb98acb5fe91e4ca8`（1023509 B）、bin md5 `a84bb558732a9831ad90ab45c311f8ad`（363856 B），mtime 同为 15:15:28
- **段尺寸**：`.text=163652 .rodata=197760 .data=1664 .ccmram=37084 .bss=126348`（SRAM 130576 B / CCM 37084 B，与内存账一致）
- **来源确认**：**这就是默认口径的 make 产物**（H1 的增量链接产出，非 EIDE），**可直接烧录**。
- **EIDE 残留**：`make clean` 已把 EIDE 的 `.obj/`、`.lnp`、`.objlist`、`map.view`、`compiler.log`、`builder.params`、`ref.json`、`statistic.json`、`.lock` 一并删除（见 §6 风险 R1）。当前 `build/Debug/` 只剩 make 产物。
- **未改动**：`Makefile`、业务源码、`.eide/eide.yml` 均未被我修改（`git status` 与复核前一致；`Makefile` 的 84 插入为实施者改动）。仅 `touch` 了未跟踪的 `Application/Inc/app_scroll.h`（只改 mtime、内容不变，不影响 git 状态）。

---

## 6. 遗留风险（本轮修复未覆盖，建议后续处理）

| # | 风险 | 证据 | 影响 / 建议 |
|---|---|---|---|
| **R1** | **EIDE 覆盖共享产物后 `make` 会 no-op 并保留错误口径 elf** | A1/A2：EIDE 于 15:11:34 以更新 mtime 覆写 `build/Debug/Project_STD.elf` 后，`make -j8` 因 elf(15:11) 新于所有 `.o`(14:56) 而 **0/0，静默保留 EIDE 的 22-1703 非 CQ elf**，md5 与默认口径完全不同 | **这是本次「异常 elf」的根因，也是最高风险**：EIDE 构建后若直接 `make`/烧录，会得到错误口径固件而不报错。stamp 只跟踪 make 自身口径变量，**无法感知异构构建对同名产物的覆写**（mtime-only 覆写不可检测）。建议：① 纪律——EIDE 构建后必须 `make clean && make`；② 更稳妥——给 EIDE 换独立输出目录，与 make 的 `build/$(CONFIG)` 分离。本报告未自动修复（属架构性改动，超出最小修复范围） |
| **R2** | `make clean` 连带删除 EIDE 构建缓存 | A9a 后 `.obj/`、`builder.params` 等全部消失 | 既有行为（`rm -rf $(BUILD_DIR)`）。后果是 EIDE 下次须全量重建（耗时）。与实施者报告 §7-3 同源；本轮验证**实际触发**了该后果 |
| **R3** | `PROTO` 无非法值守卫 | `make -n PROTO=bogus` EXIT=0（静默按 ALL） | 与 `DISP` 不对称，`PROTO=cq` 小写、`PROTO=CQ ` 带空格等笔误会静默走 ALL 口径且生成可烧录 elf。建议按 `DISP` 模式加 `$(error)` |
| **R4** | `make -n` 干跑显示全量过期，误导排障 | §2 说明 1 | 建议文档补提示，避免后续复核再被误判为 stamp 失效 |
| **R5** | 口径切换 = 全量重编（正确性代价，非缺陷） | 每次切换 232~237 TU、约 1.5~1.9s（`-j8`） | 属预期；如需部分增量需按口径分对象目录，见实施者报告 §7-4 |
| **R6** | 切换后旧口径孤儿 `.o` 残留 | 例如从 22_1703 回 1_263 时 `dev_display_22_1703.o` 不再被链接（不在 `OBJ_ALL`）但仍在磁盘 | 不影响正确性（elf 只用 `$(OBJ_ALL)`），仅占空间；`make clean` 可清除 |

---

## 7. 复核方法与原始证据索引

- 备份：`/tmp/stampverify/eide_elf_backup.elf`（被覆写的 EIDE elf）、`/tmp/stampverify/eide_stamp_backup.txt`。
- 逐步日志：`/tmp/stampverify/{A1_make_default,A2_make_default_again,A3_make_CQ,A4_make_back_default,A5_make_22_1703,A6_make_22_1703_CQ,A7_make_back_default,A8_make_incremental,A9a_make_clean,A9b_make_from_clean,H1_touch_app_scroll,H2_touch_incremental,guard_disp,guard_proto}.log`。
- 关键命令：`arm-none-eabi-size -A build/Debug/Project_STD.elf`；`md5sum build/Debug/Project_STD.elf`；`make --eval=…` 展开 stamp 文本；`arm-none-eabi-nm` / `arm-none-eabi-readelf -p .comment`。
- 关联文档：`doc/构建开关总表.md` §4（机制权威）、`doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` §7（内存账）。
