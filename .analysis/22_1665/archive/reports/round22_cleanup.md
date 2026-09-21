# 第二十二轮（2026-09-17）：22-1665 资料整理报告

**背景**：22-1665 显示模组经第十二~二十一轮已**现场调通**（用户确认「显示正常」）。
本轮按用户诉求「整理文件：相关 .py、RTT、.md，清理多余的缓存文件和代码」做一次收口整理，
**核心约束 = 默认显示行为逐位不变**（见 §4 证明）。

| 项 | 前 | 后 |
|---|---|---|
| 驱动 `dev_display_22_1665.c` | 1704 行（文件头注释 208 行） | **1566 行**（文件头注释 61 行；功能代码零改动） |
| `doc/01_显示系统/22-1665....md` | 2181 行 | **492 行**（§0 as-built + §1~§6 专题 + 附录 A 历史 + 附录 B 索引） |
| `doc/CLAUDE.md` | 1086 行 / 102026 字符 | 1099 行 / **89338 字符**（净减 **12688 字符**，−12.4%；行数 +13 因 DISP 条目改为多行列表） |
| `.analysis/22_1665/` 顶层 | 42 文件（含 4 份构建日志、15 个历史脚本、7 份历史报告、5 份历史预测图） | **14 文件**（13 现存 + 本报告），其余 37 份进 `archive/` |
| 仓库根残留 | `a-dev_display_22_1665.d`、`a-initcall.d` | **已删除** |

---

## 1. 清点（整理前的全部 22-1665 相关产物）

### 1.1 `.analysis/22_1665/`（45 文件 / 2.2MB）

| 类别 | 文件 | 判定 |
|---|---|---|
| 现行回归脚本 | `check_round20.py`（58/58）、`round21_check.py`（23/23）、`check_multichain.py`（31/31） | **保留**（`round21_check.py` 改名 `check_round21.py`） |
| 现行编译/产物工具 | `check_model_matrix.sh`（96/96）、`check_flash_artifact.sh`（4/4）、`link_variant.sh` | **保留** |
| 现场诊断工具 | `round21_board_flash.py`（J-Link dump 解析） | **保留**（改名 `analyze_board_flash.py`） |
| 历史回归脚本 | `check_round14/15/17/18.py`、`check_switch_matrix.sh`、`verify.py` | **归档**（被后续轮次取代） |
| 一次性反解/生成工具 | `dump_glyph.py`、`solve_wiring.py`、`solve_wiring2.py`、`level_cell_order.py`、`reverse_engineer.py`、`gen_probe7_screens.py`、`gen_expected_screens.py`、`sync_docs.py`、`fix_doc_header.py` | **归档**（第十二~十四轮的模型来源，留档可复跑） |
| 现行产物 | `multichain_screens.md`、`round20_screens.md` | **保留**（由保留脚本生成） |
| 历史产物 | `expected_screens.md`、`probe7_screens.md`、`probe8_screens.md`、`round17_screens.md`、`round18_color_matrix.md`、`verify_report.txt`、`signatures.md`、`candidates.md` | **归档** |
| 历史报告 | `round17_report.md`、`round18_explain_and_field.md`、`round19_probe_request.md`、`round20_calibration.md` | **归档** |
| 最新报告 | `round21_diagnosis.md` | **保留** |
| 构建日志 | `build_round15/17/20/21.log`（各 ≈380KB） | **归档**（`archive/logs/`；顶层只留最新 `build_round22.log`，`link_variant.sh` 依赖它） |
| 驱动历史版本 | `prev_round/`（第十一轮 `.bak`、第十六轮、第十七轮多链/探针 9 版） | **归档**（`archive/prev_round/`），并新增本轮改动前的版本 `*_round21_pre_round22.c` |
| 索引 | `README.md`（17KB，含大量逐轮叙述与旧判读速查） | **重写**（1.6KB→8KB 结构化索引：每个脚本一行用途 + 运行方式 + n/n 含义 + 何时跑 + 归档说明 + 现场判读速查） |
| 新建 | — | `verify_all.sh`（一键跑完全部断言并汇总）、`round22_cleanup.md`（本报告） |

**未发现**：`__pycache__` / `*.pyc`（python3 直跑不生成）；`.analysis/22_1665/` 内**无** `*.bin` / flash dump / 临时 out（J-Link dump 在 `/tmp`，未入库）。

### 1.2 仓库根残留（疑似手工 gcc 的 `-MMD` 产物）

| 文件 | 判定 |
|---|---|
| `a-dev_display_22_1665.d`（4206B，2026-09-16 18:20） | 手工在仓库根跑 `gcc -MMD` 的依赖文件；**全仓库检索无任何脚本/Makefile 引用** → 删除 |
| `a-initcall.d`（79B，同上） | 同上 → 删除 |

### 1.3 驱动内探针 / RTT / 死代码清单（`-Wunused-macros` + 逐符号审计）

| 对象 | 结论 |
|---|---|
| 探针 0..9（`_22_1665_CHAIN_PROBE`）、`PROBE8_*` / `PROBE9_*`、`POS/SPAN/HOLD` | **保留**——现场标定设施，且 `check_model_matrix.sh`（现行）逐组合编译验证，`check_round20.py`/`check_round15.py` 断言其语义 |
| `_22_1665_PIN_SELFTEST` / `_22_1665_RTT_DIAG` | **保留**（开机自检是现场判「脚是否被驱动」的唯一手段） |
| `_22_1665_DATA_LINES`（10 脚候选掩码） | **保留**（探针扫描范围；兼容模式 = 同流脚） |
| 镜像旗标 `_22_1665_XF_*` / `_22_1665_ROW_XF` / `_22_1665_ROW_DST` | **保留**（下半屏朝向标定与异形排布的逃生口；`check_multichain.py`/`check_round17.py` 断言） |
| 兼容口径 `_22_1665_MULTI_CHAIN` / `BLK_COLS` / `ROLE_MONO` | **保留**（现场对照与回退口径；与第十五轮 CCM 逐字节一致） |
| 死代码 / 重复实现 | **未发现可安全删除者**：逐符号审计（含 `-Wunused-macros`）报出的 9 个「未用宏」全部是**结构上必需或文档化的 API**（如 `LINE_B1/B2` 供掩码定义、`ROLE_MONO` 供兼容口径、`XF_ROT180` 供现场、`SCAN_LINE_PX` 供兼容 scan） |
| 唯一实际改动 | 文件头注释（208 → 61 行）与 3 处文档引用注释行——**纯注释**，机器码零变化（§4） |

> 结论：本驱动的「冗余」几乎全在**注释与跨文件叙述**（多轮 changelog 堆在文件头、`doc/01` 2000+ 行、
> `.analysis` README 17KB），不在功能代码。**保守原则优先，未做任何功能性删改**。

---

## 2. 删除 / 归档 / 保留 三类清单

### 2.1 删除（仅 2 个文件，均为确认无引用的手工残留）

```
a-dev_display_22_1665.d      # 仓库根，-MMD 残留，无任何引用
a-initcall.d                 # 同上
```

### 2.2 归档（37 文件 → `archive/`，全部保留未删）

```
archive/scripts/   check_round14.py check_round15.py check_round17.py check_round18.py
                   check_switch_matrix.sh verify.py dump_glyph.py solve_wiring.py solve_wiring2.py
                   level_cell_order.py reverse_engineer.py gen_probe7_screens.py gen_expected_screens.py
                   sync_docs.py fix_doc_header.py _round22_compress_claude_md.py（本轮文档压缩助手）
archive/reports/   round17_report.md round18_explain_and_field.md round19_probe_request.md
                   round20_calibration.md candidates.md signatures.md verify_report.txt
archive/screens/   expected_screens.md probe7_screens.md probe8_screens.md round17_screens.md
                   round18_color_matrix.md
archive/logs/      build_round15.log build_round17.log build_round20.log build_round21.log
archive/prev_round/ dev_display_22_1665.c.20260917.bak（第十一轮）
                    dev_display_22_1665_round16_multichain.c、dev_display_22_1665_round17_probe9.c
                    dev_display_22_1665_round21_pre_round22.c（本轮改动前，行为不变证明的基线）
                    + archive/README.md（归档说明 + 「哪些脚本现在跑会失败、为什么」）
```

### 2.3 保留（顶层 14 文件）

```
README.md(重写)  verify_all.sh(新)  round22_cleanup.md(本报告)
check_round20.py  check_round21.py(改名自 round21_check.py)  check_multichain.py
check_model_matrix.sh  check_flash_artifact.sh  link_variant.sh
analyze_board_flash.py(改名自 round21_board_flash.py)
multichain_screens.md  round20_screens.md  round21_diagnosis.md  build_round22.log
```

**归档脚本可跑性实测**（路径已改为 `parents[4]` / `../../../../`）：
`check_round14.py` 14/14、`check_round15.py` 98/98、`check_round17.py` 35/35、`check_round18.py` 35/35 全通过；
`check_switch_matrix.sh` 26/9、`verify.py` 输出差异清单——**均为预期**（前者枚举第十一轮已删除的开关、
后者对照第十轮已作废的 512 位模型），已在 `archive/README.md` 写明。

---

## 3. 文档重构对照（`doc/01` 2181 行 → 492 行）

| 原位置 | 去向 | 说明 |
|---|---|---|
| 顶部「现行模型」块 + 状态行 | **§0 引言 + §0.1** | 改为「已调通」+ 导航说明 |
| §1 模组参数 | §0.1 | 修正为现场实态（4 链 × 8 片、静态扫、引脚表） |
| §2 静态扫描原理 | §1.2（精简） | 保留与扫描类模组的对比表 |
| §3.1~§3.2 缓冲区 / 六个校准开关（第九~十一轮） | **§2.2 + 附录 A.1** | 旧开关**已删除**，改为「位流不变量」表 + 作废标注 |
| §3.3 位流不变量 | §2.2 | 保留（历轮重写的安全性依据） |
| §3.4 ④a 脏矩形 | §0.2 + §4 | 保留结论 |
| §4 MBI5034B 接口与时序 | §1.1 + §1.3 | 剔除「假设 A7」等已确认/已作废表述；保留时序裕量与电平风险 |
| §5 颜色映射（蓝＝不点亮） | **§0.4** | **更新为现行蓝策略**（`BLUE_AS_LIT=1`，蓝→红绿都亮），旧裁决标注为单链期口径 |
| §6 内存占用（第九轮 768B） | **§0.7** | 换为现行口径：本模组 1792B + 四口径变体表 + 本轮基线 |
| §7 构建选编口径 | §3 | 保留三选一/EIDE/口径指纹，补「烧录物自检纪律」 |
| §8 scan 热路径（512 位/24 指令） | §4 | 换为现行 **33 指令/时钟位 × 128 位** |
| §9 待现场核对清单（21 条推定） | **删除** | 全部已闭环（结论进 §0.1/§0.8）；过程见附录 A.2/§A.3 |
| §10 驱动内假设清单 | **删除** | 同上 |
| §11 本轮更新/未更新 + 走位自检删除理由 | 附录 A.1 | 摘要保留 |
| §12 节结构对照表 | **删除** | 结构已随多轮重写变化，价值低 |
| §13「显示乱」排查 | **§5** | 重写为现行「症状 → 首查 → 动作」表（旧表指向已删除开关） |
| §14 第十一轮 | 附录 A.1 | 摘要（开关归一 + 链序探针 + 宿主验证） |
| §15 第十二轮 | **附录 A.2** | 保留四条反解规则（现行落点模型） |
| §16/§17/§18 第十三~十五轮 | 附录 A.3 | 合并（多脚 / 10 候选脚 / 探针 8 + 自检） |
| §19 第十六轮 | 附录 A.4 | 多链框架 |
| §20 第十七~十九轮 | 附录 A.5/A.6/A.7 | **A/B/C 推定与 {G2,B2,D} 候选集明确标注「已作废」** |
| §21 第二十轮 | **附录 A.8** | **保留探针 8 逐脚读数表**（关键证据） |
| §22 第二十一轮 | **附录 A.9** | **保留根因表达式、J-Link 证据、修复一行、教训与烧录物自检纪律**（关键证据） |
| 新增 | **§0.5 开关总表 / §0.6 RTT 判读表 / §0.8 验收与遗留 / 附录 B 索引 / A.10 本轮** | RTT 判读表 = 每行含义 + 正常值，现场一眼核对 |

**跨文档同步**：

- `doc/CLAUDE.md`：22_1665 的 6 处长条目（DISP 条目、initcall 行、Display 模组表行、选编口径
  Makefile 条目、第二十轮段尺寸长条目、EIDE 实态条目、CCMRAM 明细行）压缩为「现行状态 + 指针」；
  历史轮次细节全部交给 `doc/01`；CCMRAM 明细修正为第二十二轮实测（本模组 1792B / 兼容 560B / 探针 8-9 1888B，
  全片 10460 / 9228 / 10556 / 9532）。
- `doc/构建开关总表.md`：DISP 开关说明与 `DISP` 表行改写为现行口径；新增第二十二轮条目；
  删/改指已归档文件的路径（`build_round20.log`、`round20_calibration.md`、`check_round14.py`、
  `probe7_screens.md`、`expected_screens.md`）；烧录前核对补 `DISP=22_1665` 基线与
  `check_flash_artifact.sh` 纪律。
- `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md`：22_1665 段尺寸本就与实测一致（172460/202896/10460/384B），
  补一条「第二十二轮复核：段尺寸逐字节未变，仅 md5/tree 随注释与时间变化」的注。
- `doc/15_云南治超屏协议/README.md`：核对无改动需求（其 22_1665 行数字 172460/202896/10460/130688/384B 与实测一致）。

> **说明**：`doc/01` 重构前的版本**未单独留档**（文件被就地重写）。其内容去向逐条列于上表：
> 现行结论全部进 §0；历史细节进附录 A 或被归档报告（`archive/reports/` 100KB+）与驱动备份覆盖。
> 未保留的是当时已自认失效的推定叙述（§9/§10/§12）与已删除开关的描写。

---

## 4. 代码 diff 摘要与**行为不变证明**

### 4.1 改动摘要（`Device/Display/dev_display_22_1665.c`）

- 文件头注释 208 → 61 行：删除「第十六/十七/二十/二十一/十二轮」五段 changelog 与旧「开关总览」，
  改为 as-built（硬件口径 / 数据流 / 链表指针 / 开关表 / 探针表 / 设计约束 / 历史指针）；
- `#include "SEGGER_RTT.h"` 行注释改为「RTT 诊断：…全部由 `_22_1665_RTT_DIAG` 单一开关控制」；
- 链表标定块内的两处外部文档引用由旧小节号（§19.4 / §21）改为新锚点（§0.3 / §0.8）；
- **功能代码、宏、探针、表结构、`_Static_assert` 全部零改动**（`diff` opcode 全部落在旧文件第 3~206 行区间）。

### 4.2 证明（三重，全部实测）

```bash
# ① 同一编译命令分别编译「改动前 / 改动后」驱动，反汇编逐行比对
arm-none-eabi-gcc <CC 行> -o /tmp/rd22_old.o  .analysis/22_1665/archive/prev_round/dev_display_22_1665_round21_pre_round22.c
arm-none-eabi-gcc <CC 行> -o /tmp/rd22_new.o  Device/Display/dev_display_22_1665.c
diff <(objdump -d /tmp/rd22_old.o) <(objdump -d /tmp/rd22_new.o)     # → 仅 objdump 文件名行不同
# ② 符号表（名字 / 尺寸 / 所属段）
diff <(nm --print-size --size-sort /tmp/rd22_old.o) <(... _new.o)    # → 完全一致
# ③ 整机重链：用改动前的驱动 .o 替换进同一套对象集，链接后取裸二进制
arm-none-eabi-gcc <LD 行，-o /tmp/rd22_relink_olddrv.elf，-o 替换驱动 .o>
objcopy -O binary build/Debug/Project_STD.elf /tmp/rd22_new.bin
objcopy -O binary /tmp/rd22_relink_olddrv.elf /tmp/rd22_old.bin
md5sum /tmp/rd22_new.bin /tmp/rd22_old.bin
# 5e6f58e7885c1995426ea22ec3425c46  /tmp/rd22_new.bin
# 5e6f58e7885c1995426ea22ec3425c46  /tmp/rd22_old.bin
```

**结论：本轮改动对固件镜像零影响**——「改动前的驱动」与「改动后的驱动」经同一对象集链接后，
裸二进制**逐字节相同**（md5 `5e6f58e7885c1995426ea22ec3425c46`）。段尺寸亦逐项一致
（text 172460 / rodata 202896 / data 1672 / ccmram 10460 / bss 126452）。
唯一变化是**源码树哈希**（tree `c0b61e2f` → **`4bed24a7`**，因树哈希含全部源码文本；属注释变更的预期副作用）。

---

## 5. 验证数据（2026-09-17 最终一轮）

### 5.1 构建

```
make clean && make -j8 DISP=22_1665        # 退出码 0
告警：3 条，全部为既有 HAL 预存（stm32f4xx_hal_flash_ex.c -Wunused-parameter）→ 零新增
.text 172460 / .rodata 202896 / .data 1672 / .ccmram 10460 / .bss 126452 / ._user_heap_stack 2564
SRAM 合计 130688B（余 384B）  ccmram 余 55076B
内嵌 tree 4bed24a7
elf md5 c143afe27572526b1c82eed0d5db172a / hex 26b68a895e1f1c9dabf649070f077232 / bin 5e6f58e7885c1995426ea22ec3425c46
（横幅内嵌 __DATE__/__TIME__ → 重编必变；镜像身份以 RTT 横幅 fw=/built=/tree= 为准）
```

### 5.2 口径变体（`link_variant.sh`：换口径编译 + 整机链接，不动 `.build_stamp`、不覆盖 `build/Debug`）

| 口径 | 结果 | `.ccmram` |
|---|---|---|
| `-D_22_1665_MULTI_CHAIN=0`（兼容） | 编译 + 链接通过 | 9228 |
| `-D_22_1665_CHAIN_PROBE=8` | 通过 | 10556 |
| `-D_22_1665_CHAIN_PROBE=9` | 通过 | 10556 |
| 兼容 + 探针 9 | 通过 | 9532 |

### 5.3 宿主脚本（`verify_all.sh` 一键：**8 项全绿 / 0 失败**）

| 脚本 | 结果 |
|---|---|
| `check_round20.py` | 58/58 |
| `check_round21.py` | 23/23 |
| `check_multichain.py` | 31/31 |
| `check_model_matrix.sh` | 96/96 |
| `check_flash_artifact.sh` | 4/4（tree `4bed24a7`、链表行签名齐） |
| `link_variant.sh` × 兼容 / 探针 8 / 探针 9 | 各 1/1（链接通过） |

### 5.4 烧录物自检（复测，确认 elf/hex 同源）

```
hex md5 26b68a895e1f1c9dabf649070f077232
✔ 含 22-1665 驱动（字符串 2200001665）
✔ 含 22-1665 多链 RTT 行（[22_1665] multichain=）
✔ 是**当前源码树**的 make 构建（内嵌 tree=4bed24a7）
✔ 链表 = 第二十轮新表（C1=G1 / C2=R2 / C3=G2 行签名齐全；旧表 R1/A/B/C 行签名无）
⇒ 4 通过 / 0 失败
```

> **测得混装现象（本轮真实发生，恰是本纪律的存在理由）**：整理期间，EIDE 构建（IDE 终端）在
> 16:24:20 覆写了 `build/Debug/Project_STD.{elf,hex,bin}`，与我方 `make` 的输出互相覆盖，
> 父会话随后的 J-Link 烧录取到的正是当时目录里的 `hex`。**结论与纪律不变**：
> ① EIDE 构建后必须 `make clean && make`；② 烧录前用 `check_flash_artifact.sh` 核对 **hex 内容身份**
> （不能只看时间戳）；③ 板上镜像身份看 RTT 横幅 `fw=/built=/tree=`。

---

## 6. 遗留与建议

1. **下半屏朝向**（驱动侧唯一开放项）：探针 8 是均匀填充、读不出朝向。现场如见「下半屏内容上下颠倒 /
   左右镜像」，给链 2/3 换 `_22_1665_ROW_XF(..., _22_1665_XF_ROT180)` 即可（步骤见 doc/01 §0.3）；
   也可用探针 9 一次读全「脚 ↔ 区域 / 颜色 / 朝向」。
2. **探针命名**：`_22_1665_probe8_*` 同时服务探针 8/9（历史命名）。重命名会破坏宿主脚本的字面断言，
   收益低——**建议维持现状**；若将来重命名，须同步改 `check_round15.py`（已归档）与 `check_round20.py`。
3. **`check_switch_matrix.sh` / `verify.py`**：归档后跑会报差异（针对已删除的旧开关/旧模型）——
   属预期，已在 `archive/README.md` 写明；如需「全绿」体验请用 `verify_all.sh`。
4. **文档检索习惯**：现行事实看 `doc/01` **§0**；脚本用法看 `.analysis/22_1665/README.md`；
   历史轮次看 `doc/01` 附录 A 与 `.analysis/22_1665/archive/`。
5. **`doc/01` 重构前版本未留档**（就地重写）——如需对比旧稿，可参考本报告 §3 的逐节去向表 +
   归档报告；后续轮次若再做大改，建议先 `cp` 一份到 `archive/`。
