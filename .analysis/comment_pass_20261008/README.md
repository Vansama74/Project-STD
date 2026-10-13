# 注释工程批次目录（2026-10-08 起）

- **规范**：`doc/18_注释规范.md`（批 0）
- **执行纪律**：只动注释（代码 token 零改动）、A/B 机器码逐字节对照、全量重编零新增告警、
  **批次产物不提交**（工作区改动供用户审核）；构建全部用 `BUILD_DIR=/tmp/...`，不覆写 `build/`
- **基线提交**：`c624915bf605f59a113351bbe1184c783f9625f1`（2026-10-08，开工前唯一一次 git 写操作）
  与 `c61c8954061de3fcc40cac012842d29034931b09`（批 1a-1c + refine 一轮收尾，53 文件 / `+7026 −240`）

## 批次索引

| 批次 | 范围 | 报告 | 状态 |
|---|---|---|---|
| 批 0 | 注释规范文档 + `doc/CLAUDE.md` 指针行 | `doc/18_注释规范.md` | 完成 |
| 批 1a | 云南治超模块（YN_OL，6 文件） | `batch01a_yn_ol.md` | 完成（待用户抽审） |
| 批 1b | 贵州治超模块（GZ_OL，7 文件）+ `doc/14` 迁移 | `batch01b_gz_ol.md` | 完成（待用户抽审） |
| 批 1c | 缅甸 2D 费显模块（MM，4 文件）+ `doc/16` 迁移 | `batch01c_mm.md` | 完成（待用户抽审） |
| refine 一轮 | 三模块契约行收紧（4~8 条 / ≤30 字）+ doc/14/15/16/18 写法 | `logs/refine_round1_*.log`、`archive/refine_round1/`、`scripts/refine_round1/` | 完成并已提交（`c61c895`） |
| refine 二轮 | 契约行再精简（19 → 16 条：删 2 / 改写 12 / 保留 4）+ doc 写法统一 + doc/18 规则同步 | `refine_round2.md`、`logs/refine_round2_*.log`、`archive/refine_round2/`、`scripts/refine_round2/` | 完成（**未提交**，留工作区审核） |

## 目录结构

```
archive/batchNN/      改前源码归档（= 基线提交内容；双保险，git 已提交一次）
archive/refine_roundN/ 各 refine 轮改前源码归档（= 该轮开工时的 git HEAD）
scripts/              批 1a 脚本（A/B、token 流比较、doc 指针检查；可复跑）
scripts/batch01b/     批 1b 脚本（同上 + `diff_comment_only.py` git diff 仅注释判定）
scripts/batch01c/     批 1c 脚本（同上 + `stats_comments.py` 行数/注释行/文件头统计）
scripts/refine_roundN/ refine 轮脚本（A/B、token 流、diff 仅注释、契约核查、doc 指针、长行）
ab/batchNN/           A/B 中间产物（pre/cur 编译对象、反汇编、token 快照）
ab/refine_roundN/     同上（refine 轮）
logs/                 各脚本与构建的完整输出（证据）
```

> **若将来要提交本目录**：先把 `ab/**/*.o`、`ab/**/*.d`（编译中间产物）排除（同 `.analysis/` 既有纪律：
> 构建产物不入库）；`archive/`、`scripts/`、`logs/*.log`、报告 `.md` 属证据，可入库。

## 复跑命令

```bash
# 批 1a（YN_OL）
bash .analysis/comment_pass_20261008/scripts/ab_check_yn_ol.sh      # A/B（段级 md5 + 反汇编）
bash .analysis/comment_pass_20261008/scripts/code_only_diff.sh      # 去注释 token 流一致
python3 .analysis/comment_pass_20261008/scripts/check_doc_pointers.py  # doc 指针可解析
python3 .analysis/yn_ol/yn_ol_frame_sim.py                          # 宿主推演 184/184

# 批 1b（GZ_OL）
bash .analysis/comment_pass_20261008/scripts/batch01b/ab_check_gz_ol.sh     # A/B（4 TU）
bash .analysis/comment_pass_20261008/scripts/batch01b/code_only_diff.sh     # token 流（7 文件）
python3 .analysis/comment_pass_20261008/scripts/batch01b/diff_comment_only.py   # git diff 仅注释判定
python3 .analysis/comment_pass_20261008/scripts/batch01b/check_doc_pointers.py  # doc 指针 7/7
python3 ~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/gz_ol_frame_sim.py    # 宿主推演 122/122
make PROTO=ALL DISP=1_263 -j8 BUILD_DIR=/tmp/comment_pass/all1263_b01b      # 全量重编（段尺寸基线）

# 批 1c（MM）
bash .analysis/comment_pass_20261008/scripts/batch01c/ab_check_mm.sh         # A/B（3 TU：2 模块源 + 厂测）
bash .analysis/comment_pass_20261008/scripts/batch01c/code_only_diff.sh      # token 流（4 文件：.h + 3 .c）
python3 .analysis/comment_pass_20261008/scripts/batch01c/diff_comment_only.py   # git diff 仅注释判定
python3 .analysis/comment_pass_20261008/scripts/batch01c/check_doc_pointers.py  # doc 指针 7/7
python3 .analysis/comment_pass_20261008/scripts/batch01c/stats_comments.py cur  # 行数/注释行/文件头
python3 .analysis/myanmar/mm_frame_sim.py                                    # 宿主推演 157/157
make PROTO=ALL DISP=1_263 -j8 BUILD_DIR=/tmp/comment_pass/all1263_b01c      # 全量重编（段尺寸基线）

# refine 一轮（三模块契约行收紧；已提交）
bash .analysis/comment_pass_20261008/scripts/refine_round1/ab_check_refine.sh     # A/B（10 TU）
python3 .analysis/comment_pass_20261008/scripts/refine_round1/check_contracts.py  # 契约 5/6/8 条
python3 .analysis/comment_pass_20261008/scripts/refine_round1/long_comments.py    # 长行清单

# refine 二轮（契约再精简 + doc 写法统一；未提交）
bash .analysis/comment_pass_20261008/scripts/refine_round2/ab_check_refine.sh      # A/B（10 TU）
bash .analysis/comment_pass_20261008/scripts/refine_round2/code_only_diff.sh       # token 流（17 文件）
python3 .analysis/comment_pass_20261008/scripts/refine_round2/diff_comment_only.py # git diff 仅注释
python3 .analysis/comment_pass_20261008/scripts/refine_round2/check_contracts.py   # 契约 4/5/7 条 + 引用一致
python3 .analysis/comment_pass_20261008/scripts/refine_round2/check_doc_pointers.py # 指针 101/101
make PROTO=ALL DISP=1_263 -j8 BUILD_DIR=/tmp/comment_pass/all1263_refine2          # 段尺寸逐项一致
```
