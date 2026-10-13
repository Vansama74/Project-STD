# 2026-10-12 全项目临时文件盘点与清理记录

任务：盘点并清理全项目临时文件，重点回答 `build/xj_*`（15 个新疆轮 A/B 抽样构建目录）「有没有用」，
保守清理（宁留不删）、不动源码/文档正文（除必要注记）、**未 commit、未改 git 配置**。

- 执行时间：2026-10-12 17:53~18:05（临时物第一轮）+ 18:23 起收尾（按用户对两项待裁决的拍板：恢复 7 个 git 跟踪文件、删除缅甸无显示口径构建产物）
- 范围：`build/`、`.analysis/`、全仓 `__pycache__` / `*.o` / `*.d` / `*.tmp` / `*.bak` / `core.*` 等
- 原始清单：`deletions_manifest.txt`（删除前逐项大小）；动作记录：`deletions_executed.txt`

---

## 0. 结论摘要

**那 15 个 `build/xj_*` 目录没有保留价值**：它们是 2026-10-12 新疆协议接入/排查各轮
「A/B 等价口径」的**全量构建中间产物**（各 10~12MB，内含 .o/.d + elf/hex/bin/map），
**可再生成**；其结论性证据（构建日志、`size -A` 段尺寸、`nm` 转储、断言、报告、等价 Makefile）
已全部归档在 `.analysis/xinjiang/**`。构建线索本身（`make … BUILD_DIR=build/xj_*`）就写在
`doc/06 §8.4.5/§8.4.6`、`doc/19 §6` 和 `.analysis/xinjiang/**` 里——重跑即重建。
`.analysis/xinjiang/implementation_report.md` ⑦ 表第 8 行原文即写明「留作证据，可 `rm -rf build/xj_b*` 清理」。

**回收**：第一轮 `du -sh build` **187M → 26M**、`du -sh .analysis` **99M → 96M**；
收尾轮按用户裁决删除 `.analysis/myanmar/build_nodisp/EIDEMyanmarNoDisp/`（8.9 MiB）⇒
最终 `du -sh build .analysis` = **27M / 86M**（`build` 记 27M 是收尾期间**外部 EIDE 于 18:23 重编
`build/Debug`** 产物刷新所致 +≈100 KB，非本任务新增/删除；`.analysis` 96M → 86M 为本轮删除所得）。

---

## 1. 对用户 15 个 `xj_*` 目录的逐条结论（均已删除）

| 目录 | 口径 | 删除前大小(disk) | 文件数 | 结论 |
|---|---|---|---|---|
| `build/xj_b0` | ALL/1_263 工作区基线 | 12M | 489 | 中间产物，证据在 `.analysis/xinjiang/`（`size_b0.txt`/`build_b0.log`） |
| `build/xj_b1` | B0 + 掩码 32→64 | 12M | 489 | 同上（`size_b1.txt`/`build_b1.log`） |
| `build/xj_b2` | B1 + 新疆模块 | 12M | 495 | 同上（`size_b2.txt`/`build_b2.log`；`XJ_FAULT_SCREEN` 正对照 ELF 出处） |
| `build/xj_b2_nofault` | B2 + `XJ_FAULT_SCREEN=0` | 12M | 495 | 同上（`size_b2_nofault.txt`/`build_b2_nofault.log`） |
| `build/xj_b2cq` | `PROTO=CQ` | 12M | 485 | 同上（`size_b2cq.txt`/`build_b2_cq.log`） |
| `build/xj_eide_dbg` | EIDE Debug 等价（新疆+开关 0） | 9.9M | 423 | 同上（`eide_20261012/size_after.txt`/`build_after.log`） |
| `build/xj_eide_dbg_before` | 同上「改前」 | 9.9M | 417 | 同上（`size_before.txt`/`build_before.log`） |
| `build/xj_eide_dbg_faulton` | 同上「开关 1」对照 | 11M | 423 | 同上（`size_after_faulton.txt`/`build_after_faulton.log`） |
| `build/xj_eide_mm` | Myanmar 等价口径 | 9.8M | 419 | 同上（`size_myanmar.txt`/`build_myanmar.log`） |
| `build/xj_fix_all` | 15:36 修复轮 ALL | 12M | 495 | 同上（`size_fix_all.txt`/`build_fix_all.log`） |
| `build/xj_fix_cq` | 15:36 修复轮 CQ | 12M | 485 | 同上（`size_fix_cq.txt`/`build_fix_cq.log`） |
| `build/xj_r2_all` | 第二轮 ALL | 12M | 495 | 同上（`r2_evidence/size_xj_r2_all.txt`/`build_r2_all.log`） |
| `build/xj_r2_cq` | 第二轮 CQ | 12M | 485 | 同上（`r2_evidence/size_xj_r2_cq.txt`/`build_r2_cq.log`） |
| `build/xj_r2_dbg` | 用户目标口径（自备 RB，改后） | 9.5M | 403 | 同上（`r2_evidence/size_xj_r2_dbg.txt`/`build_r2_dbg_fix.log`） |
| `build/xj_r2_dbg_nofix` | 同上「改前」（摘 RB_PROVIDE_WEAK） | 9.6M | 403 | 同上（`size_xj_r2_dbg_nofix.txt`/`build_r2_dbg_nofix.log`） |

合计表观大小 **149,113,763 B ≈ 142.2 MiB**（du -sh 块口径合计 167.7M）。
`build/` 下**再无其它 `xj_*`**（清理前仅 `Debug` + 上表 15 个）。

---

## 2. 盘点（改前）

```
du -sh build .analysis      →  build 187M / .analysis 99M
build/ 一级：Debug(26M) + 15×xj_*(9.5~12M)
.analysis/ 一级（改前，由大到小）：yn_ol 28M / myanmar 20M / 22_1665 14M / cleanup_20260929 9.4M /
                                 xinjiang 8.4M / 9k23881580 7.6M / comment_pass_20261008 7.9M /
                                 audit_20261008 2.7M / heap 2.4M / debug_20261012 36K
```

全仓扫描结果（`build/`、`.git/` 外）：

| 类别 | 位置 | 大小 | 处置 |
|---|---|---|---|
| `__pycache__` | 3 处（见 §3） | 90 KB | 删（规则 6） |
| `.o` / `.d` / `.strip.o` | `.analysis/**` 5 组 633 个 | ~4.7 MB | 删 215 个 / 留 418 个（见 §3、§4） |
| `*.tmp` / `core.*` / `nohup.out` / `.DS_Store` / `*.orig` / `*.rej` / `*.swp` | 无 | — | 无需处理 |
| `*.bak` | 1 个：`.analysis/22_1665/archive/prev_round/dev_display_22_1665.c.20260917.bak` | 36 KB | **保留**——外貌像临时物，实为「驱动源码归档」（规则 3） |
| `build/Release/` | **不存在**（2026-10-08 归档删除后未再出现） | — | 无需处理 |
| 根 `.cache/clangd/index` | 12 KB | — | 保留（clangd 在用） |

---

## 3. 引用核实（`doc/**` + `.analysis/**` 全量 grep，逐个目录名）

- **`doc/06 §8.4.5/§8.4.6`、`doc/19 §2.2/§6/§6.1/§6.2/§6.3/§6.3.1`、`.analysis/xinjiang/**` 的引用
  分两类**：
  1. **复现命令**（`make … BUILD_DIR=build/xj_*`）——目录是输出目标，命令本身留在文档即满足复现，
     重跑自动重建目录，不构成「必须留目录」；
  2. **测量记录**（`size_*.txt` / `build_*.log` / `nm_*.txt` / `assertions.txt`）——记录中的路径是
     「当时测的是哪个文件」，文本证据本身已归档，不依赖产物存在。
- **唯一「以产物本身为证据」的引用**：`doc/19 §5`（`XJ_FAULT_SCREEN=0` 正对照
  `build/xj_b2/Project_STD.elf` = 1/1）与 `.analysis/xinjiang/eide_20261012/{README.md,assertions.txt}`
  （四个 `build/xj_eide_*` 目录名）——判据结果（0/0、1/1）本身已记录在 `assertions.txt`；
  按处置规则「已清理 + 报告中写明重跑方法 + 最小注记」处理（注记见 §8）。
- 其余 `build/xj_*` 名字仅出现在 `.analysis/xinjiang/*.log`（构建日志内的编译行路径），属日志内容，无需处理。

**收尾补充——`EIDEMyanmarNoDisp` 引用核实**（`grep -rn "EIDEMyanmarNoDisp" doc/ .analysis/ --include=*.md|*.py|*.sh`）：
绝大多数命中只是**配置名 `CONFIG=EIDEMyanmarNoDisp`**（数字结论、复现命令）或**只引 `Makefile.mm_nodisp`**
（`doc/06 §8.4.5/§8.4.6`、`doc/16 §9.3/Q8/§修订表`、`doc/17`、`.analysis/cleanup_20260929/*`、
`.analysis/myanmar/README.md`、`.analysis/audit_20261008/*` 等）→ **无需改动**。
**唯一把产物目录当证据路径**的是 `doc/16_缅甸2D费显协议/README.md` §9.3「产物落
`.analysis/myanmar/build_nodisp/EIDEMyanmarNoDisp/`」→ 已补一行最小注记（见 §8）。
`.analysis/comment_pass_20261008/archive/refine_round2/doc/16_…/README.md` 是**归档快照**（历史副本），
按「不回改历史归档」纪律**不动**。

---

## 4. 删除清单（本次实际删除，逐条）

### 4.1 `build/xj_*`（15 个目录 / 6,905 个文件；表观 142.2 MiB）

见 §1 表（每个目录的 disk 大小与文件数）。删除动作记录见 `deletions_executed.txt`。

### 4.2 `__pycache__`（3 个目录 / 90 KB）

| 路径 | 大小 | git 跟踪 |
|---|---|---|
| `.analysis/comment_pass_20261008/scripts/batch01b/__pycache__/` | 6,886 B | 未跟踪 |
| `.analysis/22_1665/__pycache__/` | 39,252 B | **2 个 .pyc 被 git 跟踪**（commit `df3fdbb`） |
| `.analysis/myanmar/__pycache__/` | 45,959 B | 未跟踪 |

### 4.3 `.analysis/**` 编译中间产物 `*.o` / `*.d`（215 个文件 / ≈2.31 MiB）

| 目录 | 删除 | 大小 | git 跟踪 | 说明 |
|---|---|---|---|---|
| `.analysis/myanmar/ab_baseline/` | 14 个（.o + .d） | 237.7 KB | 未跟踪 | `.dis`（11）与 3 个 `*_orig.c`、`size_*.txt` 保留 |
| `.analysis/comment_pass_20261008/ab/` | 180 个（.o/.d/.strip.o） | 1,680.2 KB | 未跟踪 | `.dis`(60)/`.build.log`(60)/`.token`(102)/pre,cur 源码副本 全保留 |
| `.analysis/cleanup_20260929/round2_ab/reconstruct/` | 16 个（.o + .d） | 331.2 KB | 未跟踪 | `*.pre.c`（改动前源码）与 `*.dis` 全保留 |
| `.analysis/yn_ol/guard_test/` | 5 个（.o） | 116.5 KB | **被 git 跟踪**（commit `df3fdbb`） | `merged_prod.err`（判定文本）保留 |

**注**：以上 7 个 git 跟踪文件（2 个 `.pyc` + 5 个 `.o`）初次删除后 `git status` 出现 7 行 ` D`
（工作区删除、未暂存、未提交）；**收尾轮按用户裁决已恢复**（`git restore -- .analysis/22_1665/__pycache__
.analysis/yn_ol/guard_test`）⇒ 工作区回到 `HEAD`、` D` 归零。恢复后
`.analysis/22_1665/__pycache__/`（2 个 `.pyc`）与 `.analysis/yn_ol/guard_test/*.o`（5 个）重新存在，
内容与 `HEAD` 一致（不产生 git 改动，见 §6 待裁决 4）。

**未删（受保护）**：`.analysis/cleanup_20260929/round2_ab/before/*.o`（4 个，规则 3 指名保护
`before/**`；注记：其实际内容是 `.o` 而非「源码副本」，按保护条款原样保留）。

---

## 5. 保留清单（含理由）

**「被文档引用 / 证据」类**（点名保留）：

| 路径 | 理由 |
|---|---|
| `build/Debug/**`（elf/hex/bin/objlist/builder.params/compile_commands.json/.cache/clangd/index 453 项） | 日常 EIDE 构建产物 + clangd 在用（规则 4） |
| `.analysis/xinjiang/**`（全部 8.4M） | 新疆轮结论证据：`size_*.txt`、`build_*.log`、`r2_evidence/`（含 `nm_*.txt`/`Debug.r2.mk`）、`eide_20261012/`（含 **`eide.yml.before_xj` 备份**、`assertions.txt`、等价 mk、日志）、`doc_v2/`（PDF 分页 + 图片）、`xj_frame_sim.py`、3 份报告 |
| `.analysis/yn_ol/fix_verify/final_shipping.elf` 等 elf/hex/map | 文档记载的**烧录镜像**与 A/B 对照件 |
| `.analysis/9k23881580/artifacts_1754_eide/Project_STD.{elf,hex}` | 现场构建产物证据 |
| `.analysis/myanmar/eide_writeback_20260924/after_fix_build/**` | EIDE 回写事件证据归档 |
| `.analysis/myanmar/ab_baseline/{*.dis, *_orig.c, size_*.txt}` | A/B 基线文本证据；**`check_guard_disasm.py` 的默认输入是 `.dis`（只读文本），删 `.o` 不影响复跑**（已核实脚本 line 182） |
| `.analysis/myanmar/build_nodisp/Makefile.mm_nodisp` | 无显示等价口径工具（**必须留**；同级 `EIDEMyanmarNoDisp/` 产物目录已按裁决删除，重跑本 Makefile 即重建——见 §6） |
| `.analysis/cleanup_20260929/round2_ab/{before_*.o.dis, after_*.o.dis, reconstruct/*.pre.c, reconstruct/*.dis}` | 注释轮 A/B 的文本证据（段级/反汇编一致性的原始出处） |
| `.analysis/yn_ol/guard_test/merged_prod.err` + `.analysis/yn_ol/guard_test.out.txt` | 守卫互斥判定文本（`multiple definition of g_brace_proto_guard`） |
| `.analysis/22_1665/archive/prev_round/dev_display_22_1665.c.20260917.bak` | **驱动源码归档**（外形像 `.bak` 临时物，实为证据，规则 3） |
| `.analysis/comment_pass_20261008/ab/**` 非 .o/.d 文件（.dis/.log/.token/h/c） | 注释精简轮的机器级证据与源码副本 |
| `.analysis/audit_20261008/landing_20261008/archive_stale/**`（340 KB） | 2026-10-08 审计归档（`build_Release` 清单/CACHES.md 等）——待裁决项 2 |
| `.analysis/debug_20261012/dip2_font_select.md`（32 KB） | 今日报告（非临时物） |
| 根 `.cache/clangd/index` | clangd 在用 |

**源码 / 文档**：`Application/**`、`Device/**`、`Platform/**`、`Kernel/**`、`Core/**`、`Compiler/**`、
`Makefile`、`.eide/eide.yml`、`doc/**`、`tool/**` —— 全部未动（仅 §8 注记 4 处）。

---

## 6. 原待裁决项与最终裁决（2026-10-12 收尾）

用户于收尾对两项拍板，均按裁决执行完毕：

| # | 项 | 最终裁决 | 执行结果 |
|---|---|---|---|
| 1 | `.analysis/myanmar/build_nodisp/EIDEMyanmarNoDisp/` | **删除** | ✅ **已删除**——`rm -rf` 前实测 **9,331,997 B（8.9 MiB 表观）/ 419 个文件**（日志口径 414 个 .o/.d + elf/hex/bin/map）；同级 **`Makefile.mm_nodisp`（20,637 B）保留**，`.analysis/myanmar/build_nodisp/` 下现只剩它一个文件 |
| 4 | 7 个被 git 跟踪的删除项（2 个 `.pyc` + 5 个 `guard_test/*.o`，commit `df3fdbb`） | **恢复** | ✅ **已恢复**——`git restore -- .analysis/22_1665/__pycache__ .analysis/yn_ol/guard_test`；工作区相对 `HEAD` 的 7 行 ` D` **已归零** |
| 2 | `.analysis/audit_20261008/landing_20261008/archive_stale/`（340 KB，6 文件） | 未裁决 → 维持保留 | 保留（体积小、审计证据）✓ |
| 3 | `.analysis/debug_20261012/`（36 KB） | 未裁决 → 维持保留 | 保留（报告，非临时物）✓ |
| 5 | `.analysis/cleanup_20260929/round2_ab/before/*.o`（4 个，100 KB） | 未裁决 → 维持保留 | 保留（规则 3 保护条款；实际内容是 .o）✓ |

---

## 7. 可再生成清单（重跑命令）

| 已删对象 | 重建命令 |
|---|---|
| `build/xj_b0` / `xj_b1` / `xj_b2` | `make -j8 BUILD_DIR=build/xj_b0`（默认 `PROTO=ALL DISP=1_263 FACTORY=STD`；b1/b2 同，改目录名） |
| `build/xj_b2cq` | `make -j8 PROTO=CQ BUILD_DIR=build/xj_b2cq` |
| `build/xj_b2_nofault` | `make -j8 BUILD_DIR=build/xj_b2_nofault DEFINES="-DUSE_HAL_DRIVER -DSTM32F407xx -DSTD_ALL_PROTO -DXJ_FAULT_SCREEN=0"` |
| `build/xj_fix_all` / `xj_fix_cq` | 同 b2 / b2cq 口径，`BUILD_DIR=build/xj_fix_{all,cq}` |
| `build/xj_r2_all` / `xj_r2_cq` | `make -j8 PROTO=ALL\|CQ DISP=1_263 FACTORY=STD BUILD_DIR=build/xj_r2_{all,cq}` |
| `build/xj_r2_dbg{,_nofix}` | `make -f .analysis/xinjiang/r2_evidence/Debug.r2.mk CONFIG=EIDEDebugXJ -B -j8 BUILD_DIR=build/xj_r2_dbg{,_nofix}`（nofix 需临时摘掉 `app_xj_proto.c` 两行 `RB_PROVIDE_WEAK`，见 `.analysis/xinjiang/round2_pdf_fulltext_and_rb.md` §③） |
| `build/xj_eide_dbg{,_before,_faulton}` / `xj_eide_mm` | `make -f .analysis/xinjiang/eide_20261012/{Debug.before,Debug.after,Debug.after_faulton,Myanmar.after}.mk -B -j8 BUILD_DIR=build/xj_eide_…`（mk 内 `BUILD_DIR ?=` 默认指向 `build_nodisp`，须显式传参；命令形同 doc/19 §6.3.1） |
| `.analysis/myanmar/build_nodisp/EIDEMyanmarNoDisp/` | `python3 .analysis/myanmar/check_eide_targets.py --emit-make .analysis/myanmar/build_nodisp/Makefile.mm_nodisp` 后 `make -f .analysis/myanmar/build_nodisp/Makefile.mm_nodisp CONFIG=EIDEMyanmarNoDisp -B -j8`（生成物 `BUILD_DIR` 默认即指向该目录） |
| `.analysis/**/__pycache__` | 跑对应 Python 脚本自动生成 |
| `ab_baseline/*.o`（14） | 由改动前源码重编（`app_rls_cmd`/`ah_mqtt`/`ah_mqtt_cmd` 三个用目录内 `*_orig.c`；其余见 `.analysis/myanmar/README.md`）。**`check_guard_disasm.py` 复跑不需要它们**（只读 `.dis`） |
| `comment_pass_20261008/ab/**/*.o｜.d`（180） | `bash .analysis/comment_pass_20261008/scripts/<批次>/{ab_check_*.sh,code_only_diff.sh}` |
| `round2_ab/reconstruct/*.o｜.d`（16） | `python3 .analysis/cleanup_20260929/round2_ab_reconstruct.py`，再 `bash .analysis/cleanup_20260929/round2_ab_compare.sh` |
| `yn_ol/guard_test/*.o`（5） | `bash .analysis/yn_ol/guard_test.sh` |

---

## 8. 文档/报告注记（本次清理导致的最小改动，共 6 处）

| 文件 | 位置 | 注记内容 |
|---|---|---|
| `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` | §8.4.5 采样口径下 | `build/xj_*` 已清理；证据 = `.analysis/xinjiang/size_b*.txt`/`build_b*.log`；重跑上列命令重建 |
| 同上 | §8.4.6 采样口径下 | `build/xj_r2_*`/`xj_r2_dbg*` 已清理；证据 = `.analysis/xinjiang/r2_evidence/` |
| `doc/19_新疆费显协议/README.md` | §5 正对照句（`build/xj_b2/Project_STD.elf`） | 该 ELF 属中间产物已清理；复现 = `make -j8 BUILD_DIR=build/xj_b2`；判据记录 = `eide_20261012/assertions.txt` |
| 同上 | §6「B0/B1/B2 口径」下 | §6/§6.1~§6.3 全部 `BUILD_DIR=build/xj_*` 已清理；证据 = `.analysis/xinjiang/` |
| `.analysis/xinjiang/eide_20261012/README.md`、`.analysis/xinjiang/implementation_report.md`（⑦#8）、`.analysis/myanmar/README.md`（`ab_baseline/` 行） | — | 三处「产物/基线仍留存」的表述改为「已清理 + 重跑方法/证据位置」 |
| `doc/16_缅甸2D费显协议/README.md` | §9.3「2026-09-29 第二轮（现行）」实测尾（原文「产物落 `.analysis/myanmar/build_nodisp/EIDEMyanmarNoDisp/`、elf md5 …」） | 补一行括号注：「**该产物目录已于 2026-10-12 清理，重跑 `Makefile.mm_nodisp` 即重建**」；数字结论 / elf md5 / `CONFIG=` 名称一并保留 |

> 说明：其余引用（`doc/19 §6/§6.1/§6.3` 的 `make … BUILD_DIR=…` 命令行）是**复现命令**而非产物指针，
> 保持原文（重跑会自动重建目录）。

---

## 9. 复查结果（收尾后）

```
du -sh build .analysis      →  build 27M（仅 Debug；相较第一轮 +≈100 KB = 收尾期间外部 EIDE 18:23 重编）/ .analysis 86M
du -sb build .analysis      →  22,401,741 B / 87,006,974 B
ls build/                   →  Debug（唯一一级项）
build/xj_*                  →  0 个 ✓
__pycache__（全仓）          →  1 个 = .analysis/22_1665/__pycache__（收尾恢复所得的 2 个 git 跟踪 .pyc）✓
.analysis/*.o|*.d           →  9 个 = round2_ab/before/**（4，受保护）+ yn_ol/guard_test/*.o（5，收尾恢复）✓
.analysis/yn_ol/guard_test/ →  5 个 .o 已回位 + merged_prod.err 原样 ✓
.analysis/xinjiang/**       →  完整（含 eide_20261012/eide.yml.before_xj、r2_evidence/、3 份报告、doc_v2/）✓
.analysis/myanmar/build_nodisp/ →  只剩 Makefile.mm_nodisp（EIDEMyanmarNoDisp/ 已按裁决删除）✓
.analysis/audit_20261008/landing_20261008/archive_stale/ → 未动 ✓
.analysis/debug_20261012/    →  未动 ✓
build/Debug/Project_STD.{elf,hex,bin,objlist} + builder.params + compile_commands.json + .cache/clangd/index → 存在于 build/Debug（收尾期间外部 EIDE 已重编刷新）✓
```

`git status --short`（54 行）：**M 32 / D 0 / ?? 22**。
- **M 32** = 清理前既有的 31 个在飞改动（源头码/文档）+ 本任务在 `.analysis/myanmar/README.md` 补的 1 处注记；
  收尾本轮对 `doc/16_…/README.md` 的新增注记落在**本就已 M 的同一文件**内，不新增条目。
- **D 0** = 收尾 `git restore` 后 7 行 ` D` 已全部消失（§6 裁决 4）。
- **?? 22** = 与第一轮一致（`build_nodisp/` 仍因保留 `Makefile.mm_nodisp` 而整体未跟踪；删除其子目录不改 ?? 数）。
- **不 commit、不改 git 配置、未执行其它 git 子命令**。

---

## 10. 声明

**未改动任何源码/头文件/文档正文（除 §8 的 6 处最小注记）；未烧录、未 commit、未改 git 配置、
未执行 `git clean`/`git checkout`。** 收尾轮只执行了两条命令：
`git restore -- .analysis/22_1665/__pycache__ .analysis/yn_ol/guard_test`（恢复 7 个跟踪文件）与
`rm -rf .analysis/myanmar/build_nodisp/EIDEMyanmarNoDisp/`（删产物）。`build/Debug/` 与
`.analysis/xinjiang/**` 完整（`build/Debug` 的 elf/hex/bin 在收尾期间被**外部 EIDE 于 18:23 重编刷新**，
非本任务所致；本任务未对 `build/` 做任何写操作）。
可逆性：§6/§7 全部删除物均可用 §7 命令重建；恢复的 7 个跟踪文件内容 = `HEAD`（无净改动）。
