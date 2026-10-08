# STATE（代码瘦身轮 2026-09-29）— 已完结（第一轮 + 第二轮修正）

## 状态：✅ 两轮全部完成

- **第一轮**：RTT 设施整体移除（API + 底层）+ 注释精简 + 四口径验证 + 文档 + 报告
  → `.analysis/cleanup_20260929/report.md`
- **第二轮（用户 4 条裁决）**：RTT **底层恢复**（业务代码零调用）、自研诊断保持删除、
  `build/` 清理（删 5 目录 43MB）+ 防再污染（`BUILD_DIR ?=`、脚本产物改落 `.analysis/`）、
  长注释外移（`app_boot.c` 两处）+ 继续精简 6 处
  → `.analysis/cleanup_20260929/round2_report.md`、as-built `doc/17` §5

## 基线与工具

- 备份：`backup/sources_before.tar.gz`（456 项，第一轮改动前）；`backup/eide.yml.before`
- 第一轮工具：`stats.py`、`check_token_equivalence.py`、`cc_check.sh`（已补回 SEGGER `-I`）、
  `build_and_record.sh`、`ab_objdump.sh`
- 第二轮工具：`round2_build.sh`（四口径全量构建）、`round2_ab_reconstruct.py` + `round2_ab_compare.sh`
  （注释精简 A/B：重建改动前源码 → 同 flags 重编 → section/反汇编比对）、`comment_blocks.py`（长注释定位）

## 第二轮关键数据

- **RTT**：业务代码 0 处 API 调用（`Application/ Device/ Kernel/ Platform/` 唯一命中 = `pl_rtt.c`）；
  `nm` 见 `pl_rtt_init` / `__hw_initcall_1_pl_rtt_init` / `SEGGER_RTT_Init|Write|WriteNoLock` /
  `_acUpBuffer` / `_acDownBuffer` / `_SEGGER_RTT`；`pl_rtt.c` md5 `3e432cf868fb6a916b1f56476851feb9`（= HEAD）
- **四口径（全量重编，仅 3 条既有 HAL 告警）**：
  `ALL/1_263/STD` = text **167752** / rodata **195776** / data 1656 / ccmram 39652 / bss **124768** / heap 2560
  （SRAM 余 **2088B**）；`FACTORY=MM` = text 166216 / rodata 195664 / bss 124504（余 2352B）；
  `PROTO=CQ` = text 154492 / rodata 195112 / bss 121292（余 6368B）；
  无显示等价口径 = text 131928 / rodata 191968 / ccmram 16084 / bss 115704（余 11224B，产物落 `.analysis/`）
- **注释**（本轮 4 文件）：−1848 字符 / −41 行；证据 = token 级「仅删除」5/5（4 个纯注释文件删除 token 0）
  + 非注释差异行 0 + 机器码 A/B（section 逐字节 + 反汇编逐行一致：4 文件 / 105 段 / 1801 行）
- **build/**：只剩 `Debug`(12M) + `Release`(340K)；删 `EIDEMyanmar`/`EIDEMyanmar1665`/`EIDEMyanmarNoDisp`/
  `MMAblation`/`Testing` = 43MB
- **脚本**：verify_all --fast **14 绿 / 1 跳过**、mm_frame_sim **157/157**、display_guards **16/16 + 95/95**、
  guard_disasm 原操作丢失 0、yn_ol_frame_sim **184/184**、eide_targets **40/45**（口径差异）、
  token_equiv **5/5**
- **文档**：`doc/17` §5、`doc/CLAUDE.md`（瘦身轮条目/内存布局/调试/堆诊断/CCM/缅甸节）、`doc/06-04`
  （头部 + §8.2 + §8.4.4 + §8.5/§8.6 + 修订）、`doc/构建开关总表.md`（§0/§3/§4/修订）、`doc/16`（横幅/§9/§10/变更表）、
  `doc/01`（§0.6/§5/新增 §5.1）、`.analysis/myanmar/README.md`

## 遗留（详见 round2_report.md §7）

1. **待裁决 1 项**：`SEGGER_RTT_Syscalls_GCC.c` 自带 printf→RTT 重定向（强 `_write`/`_write_r`）——
   现状 = 瘦身前形态（保留三文件）；要严格无重定向则把该文件移出 `SRC_RTT`
2. `.eide/eide.yml` 本轮未改 ⇒ **无需** `EIDE: Reload Project`
3. `check_flash_artifact.sh` 仍跳过（`tree=` 指纹随第一轮删除；恢复该判据属脚本增强，未在裁决范围内）
4. 第一轮遗留未变：只读观察 API 待裁决；`heap_4.c` 的 `xLastFailedAllocSize` 补丁无消费者
5. 未烧录、未提交、未改 git 配置
