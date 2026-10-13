# 脚本清理轮（2026-10-13）：未被使用的脚本

> **任务**：清理项目中「没有被使用」的脚本文件（`*.py` / `*.sh`）。
> **本轮实际删除 0 个文件**——原因见 §3（未跟踪的脚本全部有引用；零引用的脚本全部被 git 跟踪，按纪律不擅自删）。
> **未动**：源码（.c/.h）、`doc/**` 正文、`.analysis/**` 的 README/报告/日志/归档等非脚本证据文件、`build/**`、`Middlewares/**`、`.eide/**`、`.cursor/**`、`Makefile`、`tool/flash_all.sh`；未动并行任务的 `.analysis/comment_pass_20261013/`。
> **未 commit、未改 git 配置。**

---

## 0. 结论摘要

| 项 | 数量 |
|---|---|
| 扫描到的脚本总数 | **102**（其中 git 跟踪 94 / 未跟踪 8） |
| 有引用 → **保留** | **98**（其中 91 个被非 archive 文档/脚本/日志引用、7 个仅被 `archive/**` 引用） |
| 全仓 0 引用 | 4（**全部被 git 跟踪**） |
| **删除** | **0** |
| **待用户裁决** | **4**（git 跟踪 + 0 引用 + 可再生产，清单与建议见 §5） |
| `.analysis` 体积 | **86M → 86M**（未删除任何文件，回收空间 0） |
| 仓库根 / `tool/` | 根目录无脚本；`tool/` 只有 `tool/flash_all.sh`（用户点名必留，113 处引用） |

一句话结论：**没有「无引用且未被 git 跟踪」的脚本可安全删除**；真正可清理的是 4 个 git 跟踪的零引用脚本
（其中 3 个是 `ab_measure.sh` 每次运行自动重生成的链接命令快照，1 个是被同轮 `round2_ab_compare.sh` 取代的
objdump 脚本），是否删除需用户点头。

> **快照边界与并行任务**：清单快照 = **2026-10-13 09:13**（此时全仓 102 个脚本）。
> 并行任务目录 `.analysis/comment_pass_20261013/` 按要求**不纳入、不触碰**（其 `scan_candidates.py`
> 于 09:17 新建；计入则总数 103）。本任务未修改任何源码。

---

## 1. 扫描口径与方法

### 1.1 脚本清单收集

```bash
cd /home/yystation/Program/3833024/Project-STD-main
find . -type f \( -name '*.py' -o -name '*.sh' \) \
  -not -path './build/*' -not -path './Middlewares/*' -not -path './.git/*' \
  -printf '%s\t%TY-%Tm-%Td %TH:%TM\t%p\n' | sort -k3      # → 102 个
```

- 覆盖范围：`.analysis/**`（全部 101 个）、`tool/**`（1 个）、仓库根（0 个）——无其它目录含脚本。
- 每项记录：路径、字节数、最后修改时间（已进 §6 总表）。

### 1.2 「被引用」判定

- 逐项在**全部文本文件**（1374 个：`doc/**`、`.analysis/**` 的 README/报告/日志/其它脚本、`tool/**`、`Makefile`、
  `doc/CLAUDE.md` 等）中检索该脚本的 **basename** 与**相对仓库根路径**（含 `.analysis/` 之后的路径片段）；
  basename 匹配加词边界约束（两侧不得是字母/数字/`_`/`.`/`/`/`\`/`-`），避免把 `xxx.sh.bak` 之类误判。
- 排除脚本自身；命中即计一次，并按引用文件归类（`doc` / `.analysis` / `其它` / `archive`）。
- 二进制或 >8MiB 的文件不进语料（共 29 个：elf / png / docx / ods / .o / .pyc / tar.gz）——逐个核对后确认
  均为二进制产物，不含脚本引用信息。
- **交叉核对**：对「0 引用」与「仅 archive 引用」项再用
  `rg --no-ignore --hidden -l -F '<脚本名>' .`（**不过滤 .gitignore、不跳过隐藏目录、覆盖 `build/**` 与
  `Middlewares/**`**）复核一遍，结论一致。

### 1.3 git 跟踪判定

```bash
git ls-files --error-unmatch <相对路径>   # 命中 = 跟踪
```

结果：**102 = 跟踪 94 + 未跟踪 8**。未跟踪的 8 个全部位于 `?? ` 未跟踪目录内
（`.analysis/comment_pass_20261008/scripts/refine_round2/` 6 个、`.analysis/xinjiang/` 2 个）。

### 1.4 判定链（与任务要求一一对应）

1. 被任何入口/文档/脚本/日志引用 → **保留**；
2. 仅被 `archive/**` 引用、或全仓 0 引用 → **候选**；
3. 候选若被某文档写为「证据生成脚本 / 复跑路径」→ **保留**（依据见 §4.4）；
4. 剩余候选若被 git 跟踪 → **待裁决**（不擅自删）；未被跟踪 → 删除。

---

## 2. 分类统计

**按引用状态**

| 分类 | 数量 | 说明 |
|---|---|---|
| 有引用（非 archive 来源） | 91 | 保留 |
| 仅 `archive/**` 引用 | 7 | 经 §4.4 安全排除 → 保留 |
| 全仓 0 引用 | 4 | 全部 git 跟踪 → 待裁决 |

**按目录分布（102）**

| 目录 | 数量 | 处置 |
|---|---|---|
| `.analysis/comment_pass_20261008/`（含 batch01a/01b/01c、refine_round1/2） | 24 | 全部保留（其中 6 个未跟踪的 refine_round2 脚本被 `comment_pass_20261008/README.md`「复跑入口」逐条点名） |
| `.analysis/22_1665/` 顶层 | 20 | 全部保留（`verify_all.sh` 入口 + 13 个成员 + `macro_override.py`（成员 `link_variant.sh` 的换几何器）+ 5 个独立工具） |
| `.analysis/22_1665/archive/scripts/` | 20 | 全部保留（archive README §3 逐个登载 + 7 个属「仅 archive 引用」经安全排除保留） |
| `.analysis/yn_ol/` | 12 | 保留 9；**待裁决 3**（`ab_*.link_no_ynol.sh`） |
| `.analysis/cleanup_20260929/` | 10 | 保留 9；**待裁决 1**（`round2_ab_objdump.sh`） |
| `.analysis/myanmar/` | 4 | 全部保留 |
| `.analysis/audit_20261008/` | 4 | 全部保留 |
| `.analysis/9k23881580/` | 3 | 全部保留 |
| `.analysis/xinjiang/` | 2 | 全部保留（`xj_frame_sim.py` 62.7KB、`eide_20261012/apply_yaml_edits.py`） |
| `.analysis/heap/` | 2 | 全部保留 |
| `tool/` | 1 | `tool/flash_all.sh` 保留（用户点名必留） |

---

## 3. 删除清单：0 个文件

判定过程与理由：

1. **未跟踪的 8 个脚本全部有引用**（逐个核对，见 §6 总表与下列证据）——按判定链第 1 条保留：
   - `.analysis/comment_pass_20261008/scripts/refine_round2/` 6 个：被同一目录的
     `README.md:64-73`（复跑入口清单）与 `refine_round2.md:166-176`（复跑命令块）以**全路径**点名，例如
     `bash .analysis/comment_pass_20261008/scripts/refine_round2/ab_check_refine.sh`；
     其中 `check_contracts.py` / `check_doc_pointers.py` / `long_comments.py` 另被
     `doc/19_新疆费显协议/README.md:344` 与 `.analysis/xinjiang/implementation_report.md:120-122` 点名（复跑全绿记录）。
   - `.analysis/xinjiang/xj_frame_sim.py`：42 处引用，含 `doc/19:14/341`、`doc/05-01`、`doc/06-04`、`doc/CLAUDE.md`（新疆模块的宿主推演 329/329）。
   - `.analysis/xinjiang/eide_20261012/apply_yaml_edits.py`：被 `.analysis/xinjiang/implementation_report.md` 与
     `.analysis/xinjiang/eide_20261012/README.md` 记录为该次 EIDE 目标编辑的辅助脚本（doc/19 §5.2 的清单条目）。
2. **零引用的 4 个脚本全部被 git 跟踪** → 按纪律「一律不擅自删」，转入待裁决（§5）。
3. 没有出现「未跟踪 + 零引用」的脚本，因此**本轮删除集为空，回收空间 0B**；
   `du -sh .analysis` 复核 = **86M（改前）→ 86M（改后）**。

---

## 4. 保留清单（98）与理由

完整逐项清单（路径 / 大小 / 最后修改 / git 状态 / 引用数 / 引用来源 / 主要引用 / 处置）见 **§6 总表**。
按理由分组如下（每条给出可核对的证据点）：

### 4.1 复跑入口与它们的成员（保留理由：入口脚本本身被文档点名，成员被入口调用）

- `.analysis/22_1665/verify_all.sh`——22-1665 驱动一键入口，被 `doc/01`、`doc/CLAUDE.md` 等文档共 6 处记为
  `verify_all.sh --fast` / 全量 20 项全绿。其成员（脚本内 `$HERE/` 引用）13 个：
  `check_driver.py`、`check_equivalence.py`、`check_host_differential.py`、`check_compile_matrix.sh`、
  `check_eide_command.sh`、`check_flash_artifact.sh`、`check_round30_ab.sh`、`check_round32_ab.sh`、
  `check_round34_ab.sh`、`link_variant.sh`（+ `macro_override.py` 供换几何）、`round25_two_modules.py`、
  `round26_two_hubs.py`、`round27_wiring_b_and_etc.py`。
- 其它入口脚本：`.analysis/yn_ol/final_verify.sh`、`round2_verify.sh`、`guard_test.sh`、`ab_measure.sh`
  （`doc/15:281/308`「复跑脚本」）、`.analysis/heap/verify_builds.sh`（`doc/06-04` 三口径复编）、
  `.analysis/cleanup_20260929/build_and_record.sh`、`round2_build.sh`、`round2_ab_compare.sh`。

### 4.2 当前文档直接点名的自检/推演/复算脚本（保留理由：doc/** 明文记载）

- `doc/CLAUDE.md` / `doc/16`：`.analysis/myanmar/check_eide_targets.py`（269 处引用）、`mm_frame_sim.py`（157/157）、
  `check_display_guards.py`（16/16 + 95/95）、`check_guard_disasm.py`。
- `doc/19` / `doc/CLAUDE.md`：`.analysis/xinjiang/xj_frame_sim.py`（329/329）、
  `.analysis/audit_20261008/landing_20261008/check_channel_gate.py`（15/15）。
- `doc/06-04 §8`：`.analysis/heap/heap_ledger.py`（「复算入口（唯一权威脚本）」）、`verify_builds.sh`。
- `doc/15`：`.analysis/yn_ol/yn_ol_frame_sim.py`（184/184）、`ab_measure.sh`、`guard_test.sh`、`final_verify.sh`、`round2_verify.sh`。
- `doc/14`：`.analysis/9k23881580/glyph_clip_sim.py`（字形裁剪复现）。
- `doc/16`（§9/§13）：`.analysis/myanmar/` 的 EIDE 目标校验与出厂测试断言脚本。
- `tool/flash_all.sh`：`doc/CLAUDE.md` 记载的 J-Link 一键烧录纪律脚本（113 处引用，用户点名必留）。

### 4.3 各轮取证/审计脚本（保留理由：被本目录 README/报告或审计文档登载为证据来源）

- `.analysis/audit_20261008/`：`scripts/comment_stats.py`、`scripts/compare_stats.py`、
  `landing_20261008/check_channel_gate.py`、`landing_20261008/archive/check_eide_targets.py`（归档快照）。
- `.analysis/cleanup_20260929/`：`stats.py`、`ab_objdump.sh`、`check_token_equivalence.py`、`comment_blocks.py`、
  `cc_check.sh`、`round2_ab_reconstruct.py`、`round2_ab_compare.sh`、`build_and_record.sh`、`round2_build.sh`
  （`report.md` / `round2_report.md` / `STATE.md` 逐个登载）。
- `.analysis/9k23881580/`：`build_sweep.sh`、`build_sweep_triage.sh`（治超端口判定报告的证据脚本）。
- `.analysis/22_1665/` 顶层其余：`macro_override.py`（换几何的源码替换器）、`analyze_board_flash.py`
  （现场 J-Link dump 解析）、`pick_log.sh`、`reconstruct_pre_round30.py`、`obj_fingerprint.py`、`comment_density.py`。
- `.analysis/comment_pass_20261008/` 24 个：注释整改批 1a/1b/1c 与 refine 两轮的 A/B、契约、doc 指针核查脚本，
  被 `comment_pass_20261008/README.md`「复跑入口」逐条列出（含 6 个未跟踪的 refine_round2 脚本）。

### 4.4 「仅 `archive/**` 引用」的 7 个 → 安全排除后保留（保留理由：archive 文档明文写为归档复跑工具）

`rg` 复核确认它们的**全部**引用都来自 `.analysis/22_1665/archive/` 内部；但同一批文档把它们写明为
**归档的历史工具、可复跑、并记录了预期结果**，属任务要求的「证据生成脚本/复跑路径」安全排除项：

| 脚本（`…/22_1665/archive/scripts/`） | 字节 | 保留依据 |
|---|---|---|
| `check_switch_matrix.sh` | 5772 | `archive/README.md:23` 记「通过 26 / 未通过 9 = **预期**（枚举第十一轮已删除的开关，这正是它被归档的原因）」（复跑结果已文档化） |
| `dump_glyph.py` | 2958 | `archive/reports/round22_cleanup.md:27` 列「一次性反解/生成工具 → **归档**（第十二~十四轮的模型来源，**留档可复跑**）」 |
| `solve_wiring.py` | 6560 | 同上（round22_cleanup.md:27） |
| `solve_wiring2.py` | 4763 | 同上（round22_cleanup.md:27） |
| `gen_probe7_screens.py` | 7287 | 同上（round22_cleanup.md:27；其产物 `archive/screens/probe7_screens.md` 在被引用） |
| `gen_expected_screens.py` | 5054 | 同上（round22_cleanup.md:27） |
| `fix_doc_header.py` | 1539 | 同上（round22_cleanup.md:27） |

> 说明：同目录的另外 13 个归档脚本（`check_round14/15/17/18/20/21.py`、`check_multichain.py`、
> `check_model_matrix.sh`、`verify.py`、`level_cell_order.py`、`reverse_engineer.py`、`sync_docs.py`、
> `_round22_compress_claude_md.py`）有 `doc/**` 或 archive 文档引用（部分明确记载 2026-09-17 复跑
> 58/58、23/23、31/31、96/96），已在 §2 的有引用桶内。

---

## 5. 待裁决清单（4 项，全部 git 跟踪 + 全仓 0 引用）

> 处置建议需用户裁决后执行；本轮**未删除、未 `git rm`**。

### 5.1 `.analysis/cleanup_20260929/round2_ab_objdump.sh`（2877B，2026-09-29 10:33，跟踪）

- **引用扫描**：全仓 0 引用（含 `build/**`、`Middlewares/**` 交叉核对）；其产物
  `round2_ab/{before,after}_{app_boot,app_ldi,app_cq_proto}.o.dis`（6 个文件）同样 0 引用。
- **性质**：第二轮注释精简的机器码 A/B 脚本之一（改动前快照 vs 改动后对象，section md5 + `objdump -d` 逐行）。
- **被取代的证据**：同轮报告点名的复跑路径是 `round2_ab_reconstruct.py` + `round2_ab_compare.sh`
  （`.analysis/cleanup_20260929/round2_report.md:124/168/235` 三处），二者同样做「section 逐字节 + 反汇编逐行」，
  且已在 §6 表中为「保留」。本脚本是同轮更早的尝试、未被报告收录。
- **建议**：**可删**（A/B 证据链不依赖它）；若想保留产物画像，可只删脚本、留 6 个 `.dis`（或一并删）。

### 5.2 `.analysis/yn_ol/ab_all_1263.link_no_ynol.sh`（13644B）/ `ab_all_221665.link_no_ynol.sh`（13650B）/ `ab_cq_1263.link_no_ynol.sh`（13412B）（2026-09-17 16:15，跟踪）

- **引用扫描**：全仓 0 引用（含 `build/**`、`Middlewares/**` 交叉核对）。
- **性质**：不是手写脚本，而是 `.analysis/yn_ol/ab_measure.sh` **运行时自动生成的链接命令行快照**
  （`ab_measure.sh:32` 把 `arm-none-eabi-gcc …` 写进 `$OUT/ab_$tag.link_no_ynol.sh`，`:33` 立刻 `bash` 它）。
  三口径复跑 `bash .analysis/yn_ol/ab_measure.sh`（`doc/15:281/308` 记载的复跑入口）会**重新生成**同名文件。
- **建议**：**可删**（可再生产，删除不影响任何文档记载的复跑路径）；其同批产物 `ab_*.map` / `ab_*_no_ynol.elf`
  同为无引用中间物，可一并评估（不在本轮脚本范围内）。

### 5.3 处置建议（供用户选择）

```bash
# 方案 A（推荐，最小）：仅删 4 个零引用 git 跟踪脚本
git rm .analysis/cleanup_20260929/round2_ab_objdump.sh
git rm .analysis/yn_ol/ab_all_1263.link_no_ynol.sh \
       .analysis/yn_ol/ab_all_221665.link_no_ynol.sh \
       .analysis/yn_ol/ab_cq_1263.link_no_ynol.sh
# 方案 B：再连同 6 个无引用 .dis 产物一起清（非脚本，需用户另行批准）
# 方案 C：维持现状（全部保留）——本轮默认结果
```

---

## 6. 全量脚本总清单（102 项）

> 列义：`引用数` = 全仓文本文件中对「basename 或相对路径」的命中次数（不含自引用）；
> `引用来源` = 命中文件的分类（`doc` / `.analysis` / `其它` / `仅 archive` / `无`）；
> `主要引用` = 命中次数最高的引用方（最多 3 个）；`处置` 依 §1.4 判定链。
>
> **快照边界**：本清单为 **2026-10-13 09:13** 的 `find` 快照（102 个）。
> 并行任务目录 `.analysis/comment_pass_20261013/`（含 `scan_candidates.py`，09:17 新建）按任务要求
> **不纳入、不触碰**——若计入，全仓脚本总数为 103。
| # | 脚本（相对仓库根） | 字节 | 最后修改 | git | 引用数 | 引用来源 | 主要引用（按命中次数） | 处置 |
|---|---|---|---|---|---|---|---|---|
| 1 | `.analysis/22_1665/analyze_board_flash.py` | 6873 | 2026-09-17 16:57 | 跟踪 | 10 | .analysis | `.analysis/22_1665/README.md`；`.analysis/audit_20261008/cmake_removal_scoping.md` | 保留 |
| 2 | `.analysis/22_1665/archive/scripts/_round22_compress_claude_md.py` | 8651 | 2026-09-17 16:33 | 跟踪 | 4 | .analysis | `.analysis/audit_20261008/cmake_removal_scoping.md` | 保留 |
| 3 | `.analysis/22_1665/archive/scripts/check_model_matrix.sh` | 9130 | 2026-09-17 16:56 | 跟踪 | 23 | doc + .analysis | `doc/构建开关总表.md`；`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`；`.analysis/22_1665/README.md` | 保留 |
| 4 | `.analysis/22_1665/archive/scripts/check_multichain.py` | 18384 | 2026-09-17 16:56 | 跟踪 | 24 | doc + .analysis | `doc/构建开关总表.md`；`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`；`.analysis/22_1665/README.md` | 保留 |
| 5 | `.analysis/22_1665/archive/scripts/check_round14.py` | 6511 | 2026-09-17 16:35 | 跟踪 | 12 | doc | `doc/构建开关总表.md` | 保留 |
| 6 | `.analysis/22_1665/archive/scripts/check_round15.py` | 12965 | 2026-09-17 16:35 | 跟踪 | 14 | doc | `doc/构建开关总表.md` | 保留 |
| 7 | `.analysis/22_1665/archive/scripts/check_round17.py` | 24423 | 2026-09-17 16:35 | 跟踪 | 20 | doc | `doc/构建开关总表.md`；`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | 保留 |
| 8 | `.analysis/22_1665/archive/scripts/check_round18.py` | 24195 | 2026-09-17 16:35 | 跟踪 | 18 | doc | `doc/构建开关总表.md`；`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | 保留 |
| 9 | `.analysis/22_1665/archive/scripts/check_round20.py` | 32192 | 2026-09-17 16:56 | 跟踪 | 24 | doc + .analysis | `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`；`.analysis/22_1665/README.md` | 保留 |
| 10 | `.analysis/22_1665/archive/scripts/check_round21.py` | 23922 | 2026-09-17 16:56 | 跟踪 | 12 | doc + .analysis | `.analysis/audit_20261008/cmake_removal_scoping.md`；`.analysis/22_1665/README.md`；`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | 保留 |
| 11 | `.analysis/22_1665/archive/scripts/check_switch_matrix.sh` | 5772 | 2026-09-17 16:21 | 跟踪 | 7 | 仅 archive | `.analysis/22_1665/archive/reports/round22_cleanup.md`；`.analysis/22_1665/archive/README.md` | 保留 |
| 12 | `.analysis/22_1665/archive/scripts/dump_glyph.py` | 2958 | 2026-09-17 10:25 | 跟踪 | 5 | 仅 archive | `.analysis/22_1665/archive/reports/round22_cleanup.md`；`.analysis/22_1665/archive/README.md` | 保留 |
| 13 | `.analysis/22_1665/archive/scripts/fix_doc_header.py` | 1539 | 2026-09-17 10:46 | 跟踪 | 4 | 仅 archive | `.analysis/22_1665/archive/reports/round22_cleanup.md`；`.analysis/22_1665/archive/README.md` | 保留 |
| 14 | `.analysis/22_1665/archive/scripts/gen_expected_screens.py` | 5054 | 2026-09-17 11:12 | 跟踪 | 3 | 仅 archive | `.analysis/22_1665/archive/reports/round22_cleanup.md`；`.analysis/22_1665/archive/reports/round18_explain_and_field.md` | 保留 |
| 15 | `.analysis/22_1665/archive/scripts/gen_probe7_screens.py` | 7287 | 2026-09-17 11:12 | 跟踪 | 4 | 仅 archive | `.analysis/22_1665/archive/reports/round22_cleanup.md`；`.analysis/22_1665/archive/screens/probe7_screens.md` | 保留 |
| 16 | `.analysis/22_1665/archive/scripts/level_cell_order.py` | 2446 | 2026-09-17 10:33 | 跟踪 | 6 | doc | `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | 保留 |
| 17 | `.analysis/22_1665/archive/scripts/reverse_engineer.py` | 6174 | 2026-09-17 10:39 | 跟踪 | 5 | doc | `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | 保留 |
| 18 | `.analysis/22_1665/archive/scripts/solve_wiring.py` | 6560 | 2026-09-17 10:31 | 跟踪 | 3 | 仅 archive | `.analysis/22_1665/archive/reports/round22_cleanup.md`；`.analysis/22_1665/archive/reports/round18_explain_and_field.md` | 保留 |
| 19 | `.analysis/22_1665/archive/scripts/solve_wiring2.py` | 4763 | 2026-09-17 10:31 | 跟踪 | 3 | 仅 archive | `.analysis/22_1665/archive/reports/round22_cleanup.md`；`.analysis/22_1665/archive/reports/round18_explain_and_field.md` | 保留 |
| 20 | `.analysis/22_1665/archive/scripts/sync_docs.py` | 8123 | 2026-09-17 10:45 | 跟踪 | 7 | .analysis | `.analysis/audit_20261008/cmake_removal_scoping.md` | 保留 |
| 21 | `.analysis/22_1665/archive/scripts/verify.py` | 28724 | 2026-09-17 16:21 | 跟踪 | 10 | doc | `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | 保留 |
| 22 | `.analysis/22_1665/check_compile_matrix.sh` | 12356 | 2026-10-08 11:11 | 跟踪 | 76 | doc + .analysis | `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`；`.analysis/22_1665/README.md`；`.analysis/22_1665/round32_ccm_limit_removed.md` | 保留 |
| 23 | `.analysis/22_1665/check_driver.py` | 22000 | 2026-10-08 11:12 | 跟踪 | 119 | doc + .analysis | `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`；`.analysis/22_1665/round33_structure_alignment_and_detabling_scope.md`；`.analysis/22_1665/verify_all.sh` | 保留 |
| 24 | `.analysis/22_1665/check_eide_command.sh` | 12274 | 2026-10-08 11:11 | 跟踪 | 89 | doc + .analysis | `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`；`.analysis/22_1665/round24_resolution.md`；`.analysis/22_1665/README.md` | 保留 |
| 25 | `.analysis/22_1665/check_equivalence.py` | 31882 | 2026-09-17 21:27 | 跟踪 | 84 | doc + .analysis | `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`；`.analysis/22_1665/round29_finalize.md`；`.analysis/22_1665/README.md` | 保留 |
| 26 | `.analysis/22_1665/check_flash_artifact.sh` | 3653 | 2026-09-17 19:08 | 跟踪 | 87 | doc + .analysis | `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`；`.analysis/22_1665/round27_wiring_b_and_etc.md`；`.analysis/22_1665/README.md` | 保留 |
| 27 | `.analysis/22_1665/check_host_differential.py` | 21229 | 2026-09-17 21:27 | 跟踪 | 78 | doc + .analysis | `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`；`.analysis/22_1665/README.md`；`.analysis/22_1665/round24_resolution.md` | 保留 |
| 28 | `.analysis/22_1665/check_round30_ab.sh` | 5964 | 2026-09-20 09:06 | 跟踪 | 43 | doc + .analysis | `.analysis/22_1665/README.md`；`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`；`.analysis/22_1665/round30_header_and_macros.md` | 保留 |
| 29 | `.analysis/22_1665/check_round32_ab.sh` | 5961 | 2026-09-20 09:07 | 跟踪 | 31 | doc + .analysis | `.analysis/22_1665/round32_ccm_limit_removed.md`；`.analysis/22_1665/README.md`；`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | 保留 |
| 30 | `.analysis/22_1665/check_round34_ab.sh` | 8292 | 2026-10-08 11:12 | 跟踪 | 32 | doc + .analysis | `.analysis/22_1665/round34_defenses_removed.md`；`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`；`.analysis/22_1665/README.md` | 保留 |
| 31 | `.analysis/22_1665/comment_density.py` | 3857 | 2026-09-20 08:59 | 跟踪 | 14 | doc + .analysis | `.analysis/22_1665/round34_defenses_removed.md`；`.analysis/22_1665/README.md`；`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | 保留 |
| 32 | `.analysis/22_1665/link_variant.sh` | 4469 | 2026-09-20 09:06 | 跟踪 | 151 | doc + .analysis | `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`；`.analysis/22_1665/README.md`；`.analysis/22_1665/round29_finalize.md` | 保留 |
| 33 | `.analysis/22_1665/macro_override.py` | 4739 | 2026-09-17 21:25 | 跟踪 | 37 | doc + .analysis | `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`；`.analysis/22_1665/README.md`；`.analysis/22_1665/check_round30_ab.sh` | 保留 |
| 34 | `.analysis/22_1665/obj_fingerprint.py` | 3506 | 2026-09-20 08:58 | 跟踪 | 14 | doc + .analysis | `.analysis/22_1665/round34_defenses_removed.md`；`.analysis/22_1665/README.md`；`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | 保留 |
| 35 | `.analysis/22_1665/pick_log.sh` | 1466 | 2026-09-20 09:06 | 跟踪 | 20 | doc + .analysis | `.analysis/22_1665/README.md`；`.analysis/22_1665/round34_defenses_removed.md`；`.analysis/audit_20261008/cmake_removal_scoping.md` | 保留 |
| 36 | `.analysis/22_1665/reconstruct_pre_round30.py` | 7995 | 2026-09-17 21:24 | 跟踪 | 3 | doc + .analysis | `.analysis/22_1665/README.md`；`.analysis/22_1665/round30_header_and_macros.md`；`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | 保留 |
| 37 | `.analysis/22_1665/round25_two_modules.py` | 18598 | 2026-09-17 20:26 | 跟踪 | 34 | doc + .analysis | `.analysis/22_1665/round25_two_modules.md`；`.analysis/22_1665/README.md`；`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | 保留 |
| 38 | `.analysis/22_1665/round26_two_hubs.py` | 29580 | 2026-09-17 20:33 | 跟踪 | 44 | doc + .analysis | `.analysis/22_1665/README.md`；`.analysis/22_1665/round26_two_hubs.md`；`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | 保留 |
| 39 | `.analysis/22_1665/round27_wiring_b_and_etc.py` | 23394 | 2026-09-17 20:33 | 跟踪 | 37 | doc + .analysis | `.analysis/22_1665/round27_wiring_b_and_etc.md`；`.analysis/22_1665/README.md`；`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | 保留 |
| 40 | `.analysis/22_1665/verify_all.sh` | 11007 | 2026-10-08 11:16 | 跟踪 | 166 | doc + .analysis | `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`；`.analysis/22_1665/README.md`；`doc/构建开关总表.md` | 保留 |
| 41 | `.analysis/9k23881580/build_sweep.sh` | 1090 | 2026-09-14 17:05 | 跟踪 | 7 | .analysis | `.analysis/9k23881580/gz_ol_udp_service_port_report.md`；`.analysis/audit_20261008/cmake_removal_scoping.md` | 保留 |
| 42 | `.analysis/9k23881580/build_sweep_triage.sh` | 1274 | 2026-09-14 17:41 | 跟踪 | 6 | .analysis | `.analysis/9k23881580/field_triage_2432_and_port.md`；`.analysis/audit_20261008/cmake_removal_scoping.md` | 保留 |
| 43 | `.analysis/9k23881580/glyph_clip_sim.py` | 13344 | 2026-09-14 17:34 | 跟踪 | 15 | doc + .analysis | `doc/14_贵州治超屏协议/README.md`；`.analysis/9k23881580/field_triage_2432_and_port.md` | 保留 |
| 44 | `.analysis/audit_20261008/landing_20261008/archive/check_eide_targets.py` | 21423 | 2026-10-08 11:02 | 跟踪 | 167 | doc + .analysis | `doc/16_缅甸2D费显协议/README.md`；`doc/CLAUDE.md`；`.analysis/myanmar/README.md` | 保留 |
| 45 | `.analysis/audit_20261008/landing_20261008/check_channel_gate.py` | 6175 | 2026-10-08 11:21 | 跟踪 | 24 | doc + .analysis | `.analysis/audit_20261008/landing_20261008.md`；`.analysis/xinjiang/round2_pdf_fulltext_and_rb.md`；`doc/CLAUDE.md` | 保留 |
| 46 | `.analysis/audit_20261008/scripts/comment_stats.py` | 8567 | 2026-10-08 09:23 | 跟踪 | 15 | .analysis | `.analysis/audit_20261008/comments_trim_feasibility.md`；`.analysis/audit_20261008/comment_standard_mapping.md` | 保留 |
| 47 | `.analysis/audit_20261008/scripts/compare_stats.py` | 2908 | 2026-10-08 09:24 | 跟踪 | 3 | .analysis | `.analysis/audit_20261008/comments_trim_feasibility.md` | 保留 |
| 48 | `.analysis/cleanup_20260929/ab_objdump.sh` | 4117 | 2026-09-29 09:28 | 跟踪 | 3 | .analysis | `.analysis/cleanup_20260929/report.md`；`.analysis/cleanup_20260929/STATE.md` | 保留 |
| 49 | `.analysis/cleanup_20260929/build_and_record.sh` | 1141 | 2026-09-29 09:05 | 跟踪 | 5 | .analysis | `.analysis/audit_20261008/cmake_removal_scoping.md`；`.analysis/cleanup_20260929/report.md`；`.analysis/cleanup_20260929/STATE.md` | 保留 |
| 50 | `.analysis/cleanup_20260929/cc_check.sh` | 2005 | 2026-09-29 10:32 | 跟踪 | 6 | .analysis | `.analysis/cleanup_20260929/round2_report.md`；`.analysis/cleanup_20260929/report.md`；`.analysis/cleanup_20260929/STATE.md` | 保留 |
| 51 | `.analysis/cleanup_20260929/check_token_equivalence.py` | 8673 | 2026-09-29 09:37 | 跟踪 | 17 | doc + .analysis | `.analysis/cleanup_20260929/round2_report.md`；`.analysis/audit_20261008/comments_trim_feasibility.md`；`.analysis/audit_20261008/comment_standard_mapping.md` | 保留 |
| 52 | `.analysis/cleanup_20260929/comment_blocks.py` | 2142 | 2026-09-29 10:30 | 跟踪 | 2 | .analysis | `.analysis/cleanup_20260929/round2_report.md`；`.analysis/cleanup_20260929/STATE.md` | 保留 |
| 53 | `.analysis/cleanup_20260929/round2_ab_compare.sh` | 1758 | 2026-09-29 10:35 | 跟踪 | 19 | doc + .analysis | `.analysis/audit_20261008/comments_trim_feasibility.md`；`.analysis/cleanup_20260929/round2_report.md`；`.analysis/cleanup_20261012_workspace/README.md` | 保留 |
| 54 | `.analysis/cleanup_20260929/round2_ab_objdump.sh` | 2877 | 2026-09-29 10:33 | 跟踪 | 0 | 无 | — | 待裁决 |
| 55 | `.analysis/cleanup_20260929/round2_ab_reconstruct.py` | 14442 | 2026-09-29 10:34 | 跟踪 | 19 | .analysis | `.analysis/cleanup_20260929/round2_report.md`；`.analysis/audit_20261008/comments_trim_feasibility.md`；`.analysis/cleanup_20261012_workspace/README.md` | 保留 |
| 56 | `.analysis/cleanup_20260929/round2_build.sh` | 2149 | 2026-09-29 10:33 | 跟踪 | 11 | .analysis | `.analysis/cleanup_20260929/round2_report.md`；`.analysis/audit_20261008/cmake_removal_scoping.md`；`.analysis/audit_20261008/comments_trim_feasibility.md` | 保留 |
| 57 | `.analysis/cleanup_20260929/stats.py` | 4103 | 2026-09-29 09:21 | 跟踪 | 3 | .analysis | `.analysis/cleanup_20260929/report.md`；`.analysis/cleanup_20260929/STATE.md` | 保留 |
| 58 | `.analysis/comment_pass_20261008/scripts/ab_check_yn_ol.sh` | 6576 | 2026-10-08 11:57 | 跟踪 | 7 | .analysis | `.analysis/comment_pass_20261008/batch01a_yn_ol.md`；`.analysis/comment_pass_20261008/README.md` | 保留 |
| 59 | `.analysis/comment_pass_20261008/scripts/batch01b/ab_check_gz_ol.sh` | 6363 | 2026-10-08 12:13 | 跟踪 | 7 | .analysis | `.analysis/comment_pass_20261008/batch01b_gz_ol.md`；`.analysis/comment_pass_20261008/README.md` | 保留 |
| 60 | `.analysis/comment_pass_20261008/scripts/batch01b/check_doc_pointers.py` | 2839 | 2026-10-08 12:13 | 跟踪 | 22 | doc + .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/batch01b_gz_ol.md`；`.analysis/comment_pass_20261008/batch01c_mm.md` | 保留 |
| 61 | `.analysis/comment_pass_20261008/scripts/batch01b/code_only_diff.sh` | 1901 | 2026-10-08 12:13 | 跟踪 | 25 | .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/batch01c_mm.md`；`.analysis/comment_pass_20261008/refine_round2.md` | 保留 |
| 62 | `.analysis/comment_pass_20261008/scripts/batch01b/diff_comment_only.py` | 5014 | 2026-10-08 12:14 | 跟踪 | 15 | .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/batch01c_mm.md`；`.analysis/comment_pass_20261008/refine_round2.md` | 保留 |
| 63 | `.analysis/comment_pass_20261008/scripts/batch01c/ab_check_mm.sh` | 7234 | 2026-10-08 12:27 | 跟踪 | 7 | .analysis | `.analysis/comment_pass_20261008/batch01c_mm.md`；`.analysis/comment_pass_20261008/README.md` | 保留 |
| 64 | `.analysis/comment_pass_20261008/scripts/batch01c/check_doc_pointers.py` | 2538 | 2026-10-08 12:27 | 跟踪 | 22 | doc + .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/batch01c_mm.md`；`.analysis/comment_pass_20261008/refine_round2.md` | 保留 |
| 65 | `.analysis/comment_pass_20261008/scripts/batch01c/code_only_diff.sh` | 1770 | 2026-10-08 12:27 | 跟踪 | 25 | .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/batch01c_mm.md`；`.analysis/comment_pass_20261008/refine_round2.md` | 保留 |
| 66 | `.analysis/comment_pass_20261008/scripts/batch01c/diff_comment_only.py` | 4711 | 2026-10-08 12:27 | 跟踪 | 15 | .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/batch01c_mm.md`；`.analysis/comment_pass_20261008/refine_round2.md` | 保留 |
| 67 | `.analysis/comment_pass_20261008/scripts/batch01c/stats_comments.py` | 3162 | 2026-10-08 12:24 | 跟踪 | 8 | .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/batch01c_mm.md` | 保留 |
| 68 | `.analysis/comment_pass_20261008/scripts/check_doc_pointers.py` | 2956 | 2026-10-08 12:04 | 跟踪 | 22 | doc + .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/batch01a_yn_ol.md`；`.analysis/comment_pass_20261008/batch01c_mm.md` | 保留 |
| 69 | `.analysis/comment_pass_20261008/scripts/code_only_diff.sh` | 1836 | 2026-10-08 12:02 | 跟踪 | 25 | .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/batch01c_mm.md`；`.analysis/comment_pass_20261008/refine_round2.md` | 保留 |
| 70 | `.analysis/comment_pass_20261008/scripts/refine_round1/ab_check_refine.sh` | 7471 | 2026-10-08 13:52 | 跟踪 | 7 | .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/refine_round2.md`；`.analysis/comment_pass_20261008/scripts/refine_round2/ab_check_refine.sh` | 保留 |
| 71 | `.analysis/comment_pass_20261008/scripts/refine_round1/check_contracts.py` | 2831 | 2026-10-08 13:55 | 跟踪 | 11 | doc + .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/refine_round2.md`；`.analysis/comment_pass_20261008/scripts/refine_round2/check_contracts.py` | 保留 |
| 72 | `.analysis/comment_pass_20261008/scripts/refine_round1/check_doc_pointers.py` | 3708 | 2026-10-08 13:50 | 跟踪 | 18 | doc + .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/batch01c_mm.md`；`.analysis/comment_pass_20261008/refine_round2.md` | 保留 |
| 73 | `.analysis/comment_pass_20261008/scripts/refine_round1/code_only_diff.sh` | 2678 | 2026-10-08 13:50 | 跟踪 | 23 | .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/batch01c_mm.md`；`.analysis/comment_pass_20261008/refine_round2.md` | 保留 |
| 74 | `.analysis/comment_pass_20261008/scripts/refine_round1/diff_comment_only.py` | 6629 | 2026-10-08 13:51 | 跟踪 | 13 | .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/batch01c_mm.md`；`.analysis/comment_pass_20261008/refine_round2.md` | 保留 |
| 75 | `.analysis/comment_pass_20261008/scripts/refine_round1/long_comments.py` | 2065 | 2026-10-08 13:47 | 跟踪 | 8 | doc + .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/refine_round2.md`；`.analysis/comment_pass_20261008/scripts/refine_round2/long_comments.py` | 保留 |
| 76 | `.analysis/comment_pass_20261008/scripts/refine_round2/ab_check_refine.sh` | 7469 | 2026-10-08 14:49 | 未跟踪 | 9 | .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/refine_round2.md`；`.analysis/comment_pass_20261008/scripts/refine_round1/ab_check_refine.sh` | 保留 |
| 77 | `.analysis/comment_pass_20261008/scripts/refine_round2/check_contracts.py` | 5558 | 2026-10-08 14:49 | 未跟踪 | 19 | doc + .analysis | `.analysis/comment_pass_20261008/refine_round2.md`；`.analysis/comment_pass_20261008/README.md`；`.analysis/xinjiang/implementation_report.md` | 保留 |
| 78 | `.analysis/comment_pass_20261008/scripts/refine_round2/check_doc_pointers.py` | 4847 | 2026-10-08 14:51 | 未跟踪 | 24 | doc + .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/refine_round2.md`；`.analysis/xinjiang/implementation_report.md` | 保留 |
| 79 | `.analysis/comment_pass_20261008/scripts/refine_round2/code_only_diff.sh` | 2677 | 2026-10-08 14:49 | 未跟踪 | 27 | .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/refine_round2.md`；`.analysis/comment_pass_20261008/batch01c_mm.md` | 保留 |
| 80 | `.analysis/comment_pass_20261008/scripts/refine_round2/diff_comment_only.py` | 6628 | 2026-10-08 14:49 | 未跟踪 | 17 | .analysis | `.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/refine_round2.md`；`.analysis/comment_pass_20261008/batch01c_mm.md` | 保留 |
| 81 | `.analysis/comment_pass_20261008/scripts/refine_round2/long_comments.py` | 2065 | 2026-10-08 14:49 | 未跟踪 | 8 | doc + .analysis | `.analysis/xinjiang/implementation_report.md`；`.analysis/comment_pass_20261008/README.md`；`.analysis/comment_pass_20261008/refine_round2.md` | 保留 |
| 82 | `.analysis/heap/heap_ledger.py` | 15362 | 2026-09-28 13:51 | 跟踪 | 49 | doc + .analysis | `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md`；`doc/CLAUDE.md`；`.analysis/comment_pass_20261008/batch01a_yn_ol.md` | 保留 |
| 83 | `.analysis/heap/verify_builds.sh` | 2304 | 2026-09-17 18:44 | 跟踪 | 15 | doc + .analysis | `.analysis/audit_20261008/cmake_removal_scoping.md`；`doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md`；`doc/CLAUDE.md` | 保留 |
| 84 | `.analysis/myanmar/check_display_guards.py` | 8809 | 2026-09-29 08:47 | 跟踪 | 101 | doc + .analysis | `doc/16_缅甸2D费显协议/README.md`；`doc/CLAUDE.md`；`doc/19_新疆费显协议/README.md` | 保留 |
| 85 | `.analysis/myanmar/check_eide_targets.py` | 18059 | 2026-10-08 11:09 | 跟踪 | 269 | doc + .analysis | `doc/16_缅甸2D费显协议/README.md`；`doc/CLAUDE.md`；`.analysis/audit_20261008/cmake_removal_scoping.md` | 保留 |
| 86 | `.analysis/myanmar/check_guard_disasm.py` | 12199 | 2026-09-29 09:27 | 跟踪 | 62 | doc + .analysis | `doc/16_缅甸2D费显协议/README.md`；`.analysis/myanmar/README.md`；`.analysis/cleanup_20260929/report.md` | 保留 |
| 87 | `.analysis/myanmar/mm_frame_sim.py` | 36133 | 2026-09-29 09:26 | 跟踪 | 133 | doc + .analysis | `doc/16_缅甸2D费显协议/README.md`；`doc/CLAUDE.md`；`.analysis/myanmar/README.md` | 保留 |
| 88 | `.analysis/xinjiang/eide_20261012/apply_yaml_edits.py` | 5027 | 2026-10-12 15:43 | 未跟踪 | 2 | .analysis | `.analysis/xinjiang/implementation_report.md`；`.analysis/xinjiang/eide_20261012/README.md` | 保留 |
| 89 | `.analysis/xinjiang/xj_frame_sim.py` | 62744 | 2026-10-12 17:41 | 未跟踪 | 42 | doc + .analysis | `doc/19_新疆费显协议/README.md`；`.analysis/xinjiang/round2_pdf_fulltext_and_rb.md`；`.analysis/xinjiang/implementation_report.md` | 保留 |
| 90 | `.analysis/yn_ol/ab_all_1263.link_no_ynol.sh` | 13644 | 2026-09-17 16:15 | 跟踪 | 0 | 无 | — | 待裁决 |
| 91 | `.analysis/yn_ol/ab_all_221665.link_no_ynol.sh` | 13650 | 2026-09-17 16:15 | 跟踪 | 0 | 无 | — | 待裁决 |
| 92 | `.analysis/yn_ol/ab_cq_1263.link_no_ynol.sh` | 13412 | 2026-09-17 16:15 | 跟踪 | 0 | 无 | — | 待裁决 |
| 93 | `.analysis/yn_ol/ab_measure.sh` | 1899 | 2026-09-17 16:04 | 跟踪 | 24 | doc + .analysis | `doc/15_云南治超屏协议/README.md`；`.analysis/yn_ol/doc15_new_sections.md`；`.analysis/audit_20261008/cmake_removal_scoping.md` | 保留 |
| 94 | `.analysis/yn_ol/board_tcp_test.py` | 34389 | 2026-09-18 10:01 | 跟踪 | 6 | .analysis | `.analysis/yn_ol/board_tcp_test.md` | 保留 |
| 95 | `.analysis/yn_ol/final_verify.sh` | 1184 | 2026-09-17 16:17 | 跟踪 | 12 | doc + .analysis | `.analysis/audit_20261008/cmake_removal_scoping.md`；`doc/15_云南治超屏协议/README.md` | 保留 |
| 96 | `.analysis/yn_ol/fix_verify/mcu_watch.py` | 4747 | 2026-09-18 10:36 | 跟踪 | 6 | doc + .analysis | `.analysis/yn_ol/fix_and_verify.md`；`doc/15_云南治超屏协议/README.md` | 保留 |
| 97 | `.analysis/yn_ol/fix_verify/rtt_log.py` | 9007 | 2026-09-18 10:56 | 跟踪 | 27 | doc + .analysis | `.analysis/yn_ol/fix_and_verify.md`；`.analysis/yn_ol/fix_verify/repro1_cap.txt`；`doc/15_云南治超屏协议/README.md` | 保留 |
| 98 | `.analysis/yn_ol/fix_verify/yn_test.py` | 11161 | 2026-09-18 10:50 | 跟踪 | 12 | doc + .analysis | `.analysis/yn_ol/fix_and_verify.md`；`doc/15_云南治超屏协议/README.md` | 保留 |
| 99 | `.analysis/yn_ol/guard_test.sh` | 2696 | 2026-09-17 16:09 | 跟踪 | 24 | doc + .analysis | `.analysis/yn_ol/doc15_new_sections.md`；`.analysis/cleanup_20261012_workspace/README.md`；`.analysis/audit_20261008/cmake_removal_scoping.md` | 保留 |
| 100 | `.analysis/yn_ol/round2_verify.sh` | 1972 | 2026-09-17 21:23 | 跟踪 | 15 | doc + .analysis | `.analysis/yn_ol/round2_channel_dispatch_diag.md`；`.analysis/audit_20261008/cmake_removal_scoping.md`；`doc/15_云南治超屏协议/README.md` | 保留 |
| 101 | `.analysis/yn_ol/yn_ol_frame_sim.py` | 48439 | 2026-09-29 09:27 | 跟踪 | 99 | doc + .analysis | `doc/15_云南治超屏协议/README.md`；`.analysis/comment_pass_20261008/refine_round2.md`；`.analysis/audit_20261008/landing_20261008.md` | 保留 |
| 102 | `tool/flash_all.sh` | 14332 | 2026-08-14 20:07 | 跟踪 | 113 | doc + .analysis | `.analysis/audit_20261008/cmake_removal_scoping.md`；`doc/CLAUDE.md`；`.analysis/9k23881580/gz_ol_font_size_bugfix.md` | 保留 |

---

## 7. 复核记录与声明

### 7.1 复核

| 复核项 | 命令 | 结果 |
|---|---|---|
| `.analysis` 体积 | `du -sh .analysis` | 改前 **86M** / 改后 **86M**（未删除文件，回收 0B） |
| 删除集 | — | 空；未执行任何 `rm` / `git rm` |
| `git status --short` | 改前 56 行 → 改后 66 行 | 本任务只新增 `.analysis/cleanup_20261013_scripts/README.md`（未跟踪）；**无任何删除项**（无 ` D ` / `D ` 条目）。同段新增的 8 个 ` M ` 源文件（`app_anhui_proto.h` / `app_anhui_proto_cmd.c` / `app_rs485.c` / `app_yn_proto_cmd.c` / `syscalls.c` / `dev_display.c` / `dev_display.h` / `lwipopts.h`，mtime 09:19~09:20）与 `?? .cursor/` 来自**并行任务**（非本任务所为——本任务全程只读源码，未写任何源码） |
| 源码 / doc / 配置 | — | 本任务未改动（`.c` / `.h` / `doc/**` / `Makefile` / `.eide/**` 均原样；见上一行：源文件的 ` M ` 属并行任务） |
| 并行任务目录 | — | `.analysis/comment_pass_20261013/` **未触碰**（清单快照与判定均不含它，仅只读可见） |

### 7.2 声明

- **未 commit**、**未改 git 配置**（未执行任何 `git config`，未使用网络操作）；
- **未删除任何文件**（本轮删除集为空，原因见 §3）；
- **未动源码**（`*.c` / `*.h`）、**未动 `doc/**` 正文**、**未动** `.analysis/**` 的非脚本证据文件；
- 本目录只新增本文件（`README.md`）。

### 7.3 附：判定用的临时分析脚本（不在工作区内）

扫描/统计用的三个临时脚本放在 `/tmp`（`scan_refs.py`、`condense.py`、`make_report_data.py`），
**不属于工作区、不随仓库提交**；如后续要复跑，可按 §1 的命令重做，或从本节说明复刻（口径 = basename +
相对路径、词边界、语料 = 全部文本文件）。

---

## 8. 续接轮复核（2026-10-13 第二轮）

> 背景：本轮为「被中断会话」的续接轮（全仓编译期断言清理 + 注释/脚本复核），
> 按上级任务要求「沿用本目录已采用的做法，保持一致」——即：**先做引用分析（文档引用 / 脚本互调 /
> Makefile / EIDE / README 索引），零引用的 git 跟踪脚本不擅自删除、列待用户裁决**。

| 复核项 | 结果 |
|---|---|
| 脚本总数 | **107**（本目录快照 102 + 并行任务 `.analysis/comment_pass_20261013/` 新增 5：`scan_candidates.py` / `sec_ab.py` / `static_assert_round2.py` / `doc_sync_round2.py` / `ab_round2_objects.py`） |
| 新增 5 个脚本的引用 | 全部被 `.analysis/comment_pass_20261013/report.md` §⑧（本轮报告）索引 → **保留**，非零引用 |
| §5 的 4 个候选复核（2026-10-13 第二轮） | 逐项 `grep -rlF <basename>` 全仓复扫：**功能引用仍为 0**；唯一命中 = 本文件 §5 自身（候选清单/证据描述）与 `round2_ab_objdump.sh` 的同名文件条目 ⇒ 判定不变，仍是「git 跟踪 + 0 引用 + 可再生产」 |
| 处置 | **仍不删除**（与 2026-10-12 用户裁决「恢复 7 个 git 跟踪文件」的先例、本目录 §5「未删除、未 `git rm`」的做法一致）；4 项待用户裁决，一键处置命令见 §5.3 |
| 被删脚本的文档指针 | **无**（本轮删除集为空，无需修正任何文档指针） |


---

## 9. 用户裁决执行（2026-10-13 第三轮）：4 个候选脚本删除

> 用户对 §5 待裁决 4 项拍板：**直接删除**。本轮执行，删除前按 §1.2 口径全仓复扫确认仍零引用。

### 9.1 删除前复扫（2026-10-13）

```bash
for n in round2_ab_objdump.sh ab_all_1263.link_no_ynol.sh ab_all_221665.link_no_ynol.sh ab_cq_1263.link_no_ynol.sh; do
  grep -rlF "$n" . --exclude-dir=.git --exclude-dir=build --exclude-dir=Middlewares
done
```

| 脚本 | 功能引用 | 命中 |
|---|---|---|
| `.analysis/cleanup_20260929/round2_ab_objdump.sh` | 0 | 自身 + 本 README §5 + `.analysis/comment_pass_20261013/report.md`（待裁决清单描述） |
| `.analysis/yn_ol/ab_all_1263.link_no_ynol.sh` | 0 | 本 README §5 |
| `.analysis/yn_ol/ab_all_221665.link_no_ynol.sh` | 0 | 本 README §5 |
| `.analysis/yn_ol/ab_cq_1263.link_no_ynol.sh` | 0 | 本 README §5 |

（命中均为「候选清单/证据描述」自身，不构成功能引用；无 Makefile / EIDE / README「复跑入口」/ 脚本互调引用。）

### 9.2 执行记录

- 删除命令（**未用 `git rm`**，保持索引不动、删除以工作区 ` D ` 形式待用户审阅；**未 commit**）：

```bash
rm .analysis/cleanup_20260929/round2_ab_objdump.sh
rm .analysis/yn_ol/ab_all_1263.link_no_ynol.sh \
   .analysis/yn_ol/ab_all_221665.link_no_ynol.sh \
   .analysis/yn_ol/ab_cq_1263.link_no_ynol.sh
```

- 脚本总数：**107 → 103**（本目录口径；本 README 不计入）。
- 被删脚本的文档指针：**无任何文档/脚本以之为复跑入口或证据生成脚本**（§9.1 表），故无需修正文档指针；
  本 README §5 / §8 的「待裁决」表述由本节取代（历史记录行保留，不改写）。
- 未处置（用户未批准的 §5.3 方案 B）：`round2_ab_objdump.sh` 的 6 个 `.dis` 产物
  （`.analysis/cleanup_20260929/round2_ab/{before,after}_{app_boot,app_ldi,app_cq_proto}.o.dis`）
  与 `ab_*.map` / `ab_*_no_ynol.elf` 中间物**保留**（非脚本，零引用但未列入本次裁决）。
- 复跑路径不受影响：`ab_measure.sh` 运行时仍会**自动重新生成**三口径 `ab_*.link_no_ynol.sh`
  （`.analysis/yn_ol/ab_measure.sh:32`，`doc/15:281/308` 记载的复跑入口照常可用）。
