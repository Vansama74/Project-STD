# 22-1665 第三十四轮：取消编译期防御 + 精简注释（2026-09-20）

> **用户裁决**：再次整理 `Device/Display/dev_display_22_1665.c` —— **取消编译期防御（全部
> `_Static_assert` 及同类编译期校验）、精简注释**。
> **范围纪律（严格）**：本轮**只做两件事** —— ① 删除编译期防御；② 精简注释。**不改**任何表结构 /
> 逻辑 / 几何宏 / 行为开关（`_22_1665_CHAIN_HEAD_IS_MODULE0` 是行为配置，保留）；`_22_1665_line_t`
> 映射化、`name` 字段、`chain_dst` 去表**三项不碰**（列为「未做、待决策」，见 §9）。
> **未 commit、未烧录、未改 `.eide/eide.yml`、未动 TIM3/TIM4 参数。**

---

## 0. 一句话结论

驱动 **441 → 372 行**（注释字符 −21%，历史轮次/长推导清零），**17 条 `_Static_assert` 与 2 个仅断言
使用的派生宏全部删除**，约束降级为**源码注释契约 + `doc/01` §0.3**；**驱动对象逐字节不变**
（20 个 section 的 md5 + 反汇编 366 行整体 md5 全同），`verify_all.sh` 全量 **20 项全绿 / 1 项跳过**。
⚠ **工作区当前宏值 8×4 = 128×64 在 `PROTO=ALL` / `PROTO=CQ` 下链接期 CCMRAM 溢出**（16380B / 14124B）
—— 这是**预存状态**（第三十三轮已按算术预测），改动前后链接报错文本逐字节一致，与本轮无关。

---

## 1. 基线冻结（步骤 1）

| 项 | 值 |
|---|---|
| 归档 | `.analysis/22_1665/archive/round34/dev_display_22_1665_pre_round34.c` |
| md5 | `af7fe2b44be42b96a14e43c96e0c6e10`（与改动前工作区文件逐字节一致） |
| 行数 | **441 行**（总 441 = 空行 42 + 纯注释行 116 + 尾注释代码行 43 + 纯代码行 240） |
| **编辑时刻几何宏值（实测，非旧记录）** | **`_22_1665_MODULE_ROWS = 8` / `_22_1665_MODULE_COLS = 4`** ⇒ **8×4 = 128×64**（M = 32）<br>（第三十三轮报告已记录：文件 mtime 2026-09-18 17:37，相对 `archive/round32/` 的非注释差异只有 `MODULE_COLS 2 → 4`） |
| 单模块像素宏 | `MODULE_PIXEL_ROW = 16` / `MODULE_PIXEL_COL = 16` |
| 行为开关 | `_22_1665_CHAIN_HEAD_IS_MODULE0 = 0`（默认，保留） |
| 本模组 CCM 通式代入 | `1408 × 32 + 256 × 4` = **46080B** |

> 说明：用户提示「源码里现在是 `MODULE_ROWS=8 / MODULE_COLS=4`，以打开文件时看到的为准」——
> 实测确认 8/4（不是旧记录的 8×2），本轮所有结论按 8×4。

## 2. 基线对象指纹（步骤 2）

命令：`make DISP=22_1665 -j8`（增量，未 clean；`PROTO=ALL`、GCC Debug `-Og`、`APP_DIAG_BANNER` 默认开），
日志 `.analysis/22_1665/build_round34_baseline.log`；指纹 `.analysis/22_1665/round34_baseline.txt`
（工具 `obj_fingerprint.py`：逐 section 名/大小/对齐 + **逐 section 内容 md5** + `objdump -d` 整体 md5）。

**基线对象**（`build/Debug/Device/Display/dev_display_22_1665.o`）：

* 文件 md5 `136b1cbceebb51431aed9515e0cd18f0`（65772 字节 —— 含 `.debug*`，跨源码路径不可比，仅存档）
* `.ccmram` = **46080B**，`.bss._22_1665_bsrr_slot_cnt` = 4B，`.rodata._22_1665_lines` = 240B，
  `.rodata.str1.4` = 120B，`.text` 各函数合计 **942B**（`prepare` 200 / `scan` 128 / `build_bsrr_table` 320 /
  `build_chain_dst` 132 / `region_offset` 74 / `init` 32 / `get` 8 / `set_row` 8 + 该文件实例化的
  `pl_hub75_Decoder_set_row` 40），`.rodata` 常量段合计 388B（表 240 + 线名 120 + 角色表 16 + ops 12）
* 反汇编：**366 行**，整体 md5 `60b0f51127790c408626c63d332e5983`

**⚠ 基线构建即已链接失败**（本轮最重要的旁证）：`make DISP=22_1665 -j8`（`PROTO=ALL`）
→ `ld: region \`CCMRAM' overflowed by 16380 bytes` ⇒ 该宏值下 22_1665 在 ALL 口径**无法链接**（见 §8）。

## 3. 删除的编译期防御（步骤 3）

### 3.1 17 条 `_Static_assert`（file:line 为改动前行号，消息为原文）

| 位置 | 消息原文 |
|---|---|
| `Device/Display/dev_display_22_1665.c:78` | `22_1665: MODULE_ROWS/MODULE_COLS must be >= 1` |
| `:81` | `22_1665: module count must fit uint8_t (<= 255)` |
| `:84` | `22_1665: MODULE_PIXEL_ROW must be a multiple of 4 (block width)` |
| `:86` | `22_1665: MODULE_PIXEL_COL must be even (upper/lower half split)` |
| `:88` | `22_1665: half screen height must be >= 4 and a multiple of 4` |
| `:90` | `22_1665: one module per line must cover exactly 8 blocks = 128 bits = 256 pixels (16x16 / 32x8 only)` |
| `:96` | `22_1665: data lines are fixed at 4 per group` |
| `:97` | `22_1665: chain count must be 4 x module count (upper/lower R/G per module)` |
| `:101` | `22_1665: state count exceeds uint8_t` |
| `:102` | `22_1665: pixel count exceeds uint16_t (chain_dst offset type)` |
| `:104` | `22_1665: frame bits exceed uint16_t (scan bound)` |
| `:108` | `22_1665: channel table must hold at least one R/G pair` |
| `:110` | `22_1665: per-column wiring needs 2 channels per module column (2 x MODULE_COLS <= HUB75_CHANNEL_MAX = 10)` |
| `:113` | `22_1665: MODULE_COLS exceeds the pre-declared line table rows` |
| `:123` | `22_1665: state array (FRAME_BITS) must equal group count x frame clocks` |
| `:143` | `22_1665: arrays exceed CCMRAM region (64KB; reduce MODULE_ROWS/MODULE_COLS)` |
| `:193` | `22_1665: line table must be GROUP_MAX rows x 4 lines (R/G x upper/lower)` |

### 3.2 其它编译期校验与「仅断言使用」的派生宏

* **无 `#error`、无 `__attribute__((error))`**（该文件本来就没有）；`#if` 只剩两处**正常条件编译**：
  `#ifndef _22_1665_CHAIN_HEAD_IS_MODULE0`（行为开关，保留）与 `#if _22_1665_GROUP_PORT_MAX > 2`
  （`flush_group` 的第 3 槽分支，按宏值编译，保留）。
* 删除 `_22_1665_BSRR_SLOTS`（= `GROUP_PORT_MAX × GROUP_COUNT`）与 `_22_1665_CCM_BYTES`
  （含 `sizeof(uint16_t) × CHAIN_COUNT × SEGMENT_BITS + …` 的 CCM 算式）—— 两者**只被断言使用**，
  全文件零其它引用（已核）。
* **保留** `_22_1665_GROUP_MAX`（= `HUB75_CHANNEL_MAX / 2`）：它仍是**数据线表的维度**
  （`_22_1665_lines[_22_1665_GROUP_MAX][4]`），不能删；只是从「防御段」移到「物理数据线」段。

### 3.3 保留下来的物理契约（注释，2 行）

```c
 * 物理约束（契约，非编译期守卫）：MODULE_COLS ≤ 5（HUB75 通道对上限，超限会越界读
 * `_22_1665_lines`）、像素数 / 帧位数 ≤ 65535（uint16 落点偏移与 scan 上界）；
 * 整片 CCMRAM 是否装得下由链接期兜底。
```

另在「物理数据线」段头写明「表按通道表上限预声明 5 组（`HUB75_CHANNEL_MAX / 2`），不用满 5 组是常态」，
在「CCMRAM 缓冲与实例」段头写明通式 `1408·M + 256·COLS` 与实例值（1×1 = 1664B、8×4 = 46080B）。

### 3.4 代码行差分（去注释、去空行、空白归一后逐行比对）

```
旧代码行 283 / 新代码行 241
[delete] 17 条 _Static_assert（见 §3.1）
[delete] #define _22_1665_BSRR_SLOTS / #define _22_1665_CCM_BYTES（含多行算式）
[insert] #define _22_1665_GROUP_MAX (HUB75_CHANNEL_MAX / 2U)   ← 同一定义，位置从防御段移到表前
```

⇒ **除「删除的防御」与「GROUP_MAX 移位」外，代码行全等**（`prepare` / `scan` / 建表 / `init` /
实例初始化 / 数据线表 / 宏算式一字未动）。

## 4. 注释精简（步骤 4）

度量脚本 `.analysis/22_1665/comment_density.py`（纯词法扫描，跳过字符串）：

| | 总行数 | 空行 | 纯注释行 | 尾注释代码行 | 纯代码行 | 注释字符 | 注释/代码（字符） |
|---|---|---|---|---|---|---|---|
| 改动前 | 441 | 42 | 116 | 43 | 240 | 7021 | 0.67 |
| **改动后** | **372** | 39 | **92** | 43 | 198 | **5550** | **0.71** |
| 兄弟 `dev_display_1_263.c`（参照） | 243 | 28 | 76 | 14 | 125 | 3380 | 0.72 |
| 兄弟 `dev_display_22_1703.c`（参照） | 345 | 41 | 109 | 29 | 166 | 5457 | 0.82 |

* **净减 69 行（−16%）**；注释字符 **−1471（−21%）**；纯注释行 −24。
* 用另一口径（纯注释行 ÷（纯代码行 + 尾注释代码行））：改动前 0.41 → 改动后 **0.38**，
  兄弟 `22_1703` = 0.56、`1_263` = 0.60 ⇒ **不高于兄弟驱动**（两种度量都指向同一结论）。
* **删除的内容**：与 `doc/01` 重复的历史轮次记录 / 长篇推导 / 「编译期防御」段头 13 行 /
  文件头里重复的「无自设分辨率上限」表述 / 落点规则之外的叙述性解说。
* **保留的语义**（用户点名的清单）：模型 B 接线（组 g = 通道对 2g/2g+1、各组并行）、
  几何通式与每帧时钟数（`128 × MODULE_ROWS`）、四个数组各自的含义（`pixel_map` / `frame_state` /
  `chain_dst` / `bsrr_tab`）、颜色角色与半屏约定（`role_bit` 表 + 行序 `{RED,0}/{GREEN,0}/{RED,1}/{GREEN,1}`）、
  那 1 个行为开关（`_22_1665_CHAIN_HEAD_IS_MODULE0`）、现场定标脚位（PG9/PG10/PG15/PB6、PB8/PB9/PE1/PE2）、
  数据建立裕量的现场调法（NOP 4→8/16）。

## 5. 零行为变更证明（步骤 5）

### 5.1 同一工作区、同一口径（8×4）改前 vs 改后

```
python3 .analysis/22_1665/obj_fingerprint.py build/Debug/Device/Display/dev_display_22_1665.o
  → round34_baseline.txt（改动前）/ round34_current.txt（改动后）
diff（仅结构行，排除 .debug*）⇒ 0 处差异
```

| 段（全部代码/数据段） | 改动前 | 改动后 |
|---|---|---|
| `.text.*`（9 个函数）+ `.text.pl_hub75_Decoder_set_row` | 大小/对齐/内容 md5 **全同** | ← |
| `.rodata.str1.4` (120) / `.rodata._22_1665_ops` (12) / `.rodata._22_1665_role_bit` (16) / `.rodata._22_1665_lines` (240) | **全同** | ← |
| `.data.g_22_1665` (48) / `.bss._22_1665_bsrr_slot_cnt` (4) / `.hw_initcall.2` (8) | **全同** | ← |
| `.ccmram` | **46080** | **46080** |
| `objdump -d` | 366 行 / md5 `60b0f51127790c408626c63d332e5983` | **同** |

**唯一差异 = `.debug_*` 与 `.ARM.attributes`**（行号表随注释/断言行移动；`.o` 文件 md5 因此不同：
`136b1cbc…` → `6c65aef3…`）—— 属工具链记账，非代码差异（与第三十/三十二轮 A/B 同一口径）。

### 5.2 归档 vs 现行（`check_round34_ab.sh`，几何钉死两口径）= **5/5**

* 源码级自证：现行 **0 条** `_Static_assert` / 归档 **17 条** / `_22_1665_CCM_BYTES`·`_22_1665_BSRR_SLOTS` 零残留。
* A/B [1×5]：**20 个段逐字节一致 + 反汇编 360 行 0 差异**（`.ccmram 8320B`）。
* A/B [8×4]：**20 个段逐字节一致 + 反汇编 366 行 0 差异**（`.ccmram 46080B`）。

### 5.3 链接期报错文本 A/B（8×4，`PROTO=ALL`）

```
改动前：ld: build/Debug/Project_STD.elf section `.ccmram' will not fit in region `CCMRAM'
        ld: region `CCMRAM' overflowed by 16380 bytes
改动后：逐字节相同
```

⇒ **没有任何「被删的东西」其实有作用**：断言与注释不产生代码，产物与链接行为完全一致。

## 6. 宿主回归与脚本同步（步骤 6）

### 6.1 `verify_all.sh` 结果

| 模式 | 结果 | 退出码 |
|---|---|---|
| `--fast` | **14 项全绿 / 0 失败 / 1 跳过** | 0 |
| 全量 | **20 项全绿 / 0 失败 / 1 跳过** | 0 |

逐项通过数：`check_equivalence.py` 75/75、`check_host_differential.py` 42/42、
`check_driver.py` **60/60** ×4 口径、`check_eide_command.sh` **20/20**、`check_compile_matrix.sh` **25/25**、
`check_round30_ab.sh` 2/2、`check_round32_ab.sh` 2/2、**`check_round34_ab.sh` 5/5**、
`round25/26/27` = 17/27/32、`link_variant.sh` 6 个换口径变体全部链接通过。

**跳过项**：`check_flash_artifact.sh` —— 「前置不满足」（`build/Debug/Project_STD.hex` 不是「当前树
`tree=` + 22_1665」的 make 产物）。原因见 §8：8×4 宏值下**任何 make 口径都装不下**（ALL 溢出 16380B、
CQ 溢出 14124B），只有 EIDE Debug 子集能装（57484B/65536B）。`verify_all.sh` 现在把它**显式记为
「跳过（前置不满足）」并打印原因**（不静默算通过、也不算失败）。

### 6.2 各脚本改了什么

| 脚本 | 改动 | 计数变化 |
|---|---|---|
| `check_driver.py` | ① `A 节`「每帧时钟数算式」检查去掉 `_Static_assert` 子句、改查注释里的「每帧 128 × MODULE_ROWS 时钟」；② `C 节`「源码内只剩 64KB 物理守卫」→「**无编译期防御**（0 条 `_Static_assert` / 无 `#error` / 无 error 属性）」，新增「仅断言用的宏零残留」「物理契约以注释保留（`MODULE_COLS ≤ 5` + ≤65535 + 链接期兜底）」「CCM 通式与实例值写在注释里」；③ 「uint8/uint16/线数/链数守卫」检查（断言消息）删除、「自设 16KB 残留」检查改为「自设预算与 64KB 物理守卫断言**均已删除**」；④ `D 节`「通道数守卫（编译报错）」→「**契约写在注释里**（含越界读后果）」+ 新增「历史轮次叙述零残留」；⑤ 「全部 N 条断言消息为 ASCII」→「断言已清零 ⇒ 不再有该约束」；⑥ `E 节` 规模上限 **700 → 400 行**（现值 372） | 56 → **60**（×4 口径） |
| `check_compile_matrix.sh` | 结构改为 **A/B/C/D 四节**：A 合法口径（新增 **8×4 = 工作区值**）；**B = 旧非法口径的现行行为**（新增 `warn` 期望：`COLS=0`/`ROWS=0` 编过但有 `-Wtype-limits`；`ok` 静默期望：24×16 / 16×32 / 30 / `COLS=6` 零告警编过）；**C = 链接期兜底（新）**：`link_case()` 用「驱动对象 + 最小桩 + 真链接脚本（`-nostdlib -nostartfiles`）」验证 47×1 ⇒ `overflowed by 896 bytes`、1×1 与 8×4 对照零溢出；D 陷阱回归不变 | 25 → **25**（内容整体改写：A 15 + B 6 + C 3 + D 1） |
| `check_eide_command.sh` | `bad_case`（期望编译失败 + 断言消息）**全部退休**，改为 `warn_case`（`ROWS=0` → `-Wdiv-by-zero`；`COLS=6` → `-Waggressive-loop-optimizations`（UB））与 `ok_case`（`COLS=0` 在 `-O3 -Wall` 下**静默**、24×16 / 16×32 / 30 静默、47×1 对象零告警）；合法口径新增 8×4；round30 零回归 A/B 三口径不变 | 17 → **20** |
| **`check_round34_ab.sh`（新增）** | 归档 `archive/round34/pre_round34.c` vs 现行：源码级 3 条自证 + **1×5 / 8×4 两口径**各「20 段逐字节 + 反汇编 0 差异」 | 新增 **5/5** |
| `link_variant.sh` / `check_round30_ab.sh` / `check_round32_ab.sh` / `check_round34_ab.sh` | 日志选取由「`ls -t | head -1`」改为**「最新一份含 22_1665 驱动编译命令的日志」**（新增共享 `pick_log.sh`；`link_variant.sh` 用 `--need-link` 同时要求含链接命令）——原先收尾用 `make -j8`（1_263 口径）恢复产物后就抓不到驱动命令而失败 | 通过数不变 |
| `verify_all.sh` | ① 新增 `check_round34_ab.sh` 项；② 各项描述同步；③ `check_flash_artifact.sh` 加**前置判定**（hex 必须含 `2200001665` 与当前 `tree=`，否则记「跳过」+ 原因）；④ 换口径变体**两个模块轴全部钉死**（原先只钉一轴：工作区 `COLS=4` 时 `mod2x1` 实际是 2×4、`geom32x8` 会变 M=32=46080B 而链接期溢出）；⑤ 新增 `_skip` 记账与汇总里的「跳过」计数 | `--fast` 14 / 全量 **20 + 1 跳过** |
| 新增工具 | `obj_fingerprint.py`（对象逐 section 指纹）、`comment_density.py`（注释密度）、`pick_log.sh`（日志选取） | — |

## 7. 构建（步骤 8）

四口径实测（`APP_DIAG_BANNER` 默认开、GCC Debug `-Og`、增量 make 未 clean）：

| 口径 | 结果 | `.text` | `.rodata` | `.data` | `.bss` | `.ccmram` | `._user_heap_stack` | SRAM 合计（余） |
|---|---|---|---|---|---|---|---|---|
| `PROTO=ALL DISP=22_1665`（8×4） | **链接期溢出 16380B**（无 elf） | — | — | — | — | 81916（本模组 46080） | — | — |
| `PROTO=ALL DISP=1_263` | exit 0 | **174976** | **203040** | 1656 | 124836 | 38332 | 2564 | **129056（余 2016B）** |
| `PROTO=CQ DISP=1_263` | exit 0 | **161796** | **202384** | 856 | 121360 | 36076 | 2560 | **124776（余 6296B）** |
| `PROTO=CQ DISP=22_1665`（8×4） | **链接期溢出 14124B**（无 elf） | — | — | — | — | 79660（本模组 46080） | — | — |

* **零新增告警**：四口径均只有既有的 3 条 HAL `-Wunused-parameter`（`stm32f4xx_hal_flash_ex.c`）。
* 与第三十二轮记录对照：`ALL/1_263` 与 `CQ/1_263` 的 `.text/.rodata/.ccmram` **逐值相同**
  （这两口径**不编 22_1665 驱动**）⇒ 再次印证「本轮不产生代码」。
* **8×4 溢出的算术**：本模组 46080 + 其他消费者 35836（静态任务栈 25340 + 内核缓冲 1736 + CQ 6377 +
  GZ_OL 1136 + YN_OL 1152 + 对齐）= 81916 > 65536 ⇒ **ALL 溢出 16380B**；CQ 口径其他消费者 33580 ⇒ 溢出 14124B。
  第三十三轮已按算术预测，本轮实测确认（**预存状态**）。
* 日志：`build_round34_baseline.log`（改前）/ `build_round34_current.log`（改后）/ `build_round34_all_1_263.log` /
  `build_round34_cq_1_263.log` / `build_round34_cq_22_1665.log` / `build_round34_cq_final.log`。
* **工作区当前 `build/Debug` 产物 = `PROTO=CQ DISP=1_263`**（最后成功的一次 make；**不含 22_1665 驱动**，
  故 `check_flash_artifact.sh` 跳过）。**没有** 22_1665 的 make elf/hex —— 要产出它须减几何
  （`8×2` 口径 ALL = 58876B，`link_variant.sh mod8x2` 实测链接通过）或让出 CCM，属**待用户决策**。

## 8. 非法几何的现行行为（实测，供现场判读）

`-fsyntax-only -Wall -Wextra`（矩阵）与 EIDE 原命令 `-O3 -Wall`（真实构建）两套口径实测：

| 旧非法口径 | 现行行为 | 备注 |
|---|---|---|
| `MODULE_COLS = 0` | 编过；`-Wextra` 下 `-Wtype-limits`（恒假比较），**`-O3 -Wall` 下静默** | 组数 0 ⇒ 什么也不驱动 |
| `MODULE_ROWS = 0` | 编过；`-O3 -Wall` 下 `-Wdiv-by-zero`（落点表除以 0 模块/行） | 静默错口径 |
| 单模块 `24×16` / `16×32` / `PIXEL_COL=30` | **零告警编过** | 落点只覆盖部分像素（块栅格 ≠ 8）= **静默错口径** |
| `MODULE_COLS = 6` | `-fsyntax-only` 静默；`-O3` 报 `-Waggressive-loop-optimizations`（UB） | **运行期越界读 `_22_1665_lines[5]`**（表只有 5 行） |
| `MODULE_ROWS = 47`（本模组 66432B） | 对象零告警编过；**链接期** `region CCMRAM overflowed by 896 bytes` | 由链接器兜底 |

## 9. 未做、待用户决策（本轮明确不动）

1. **`_22_1665_line_t.name` 字段**（第三十三轮报告列为独立项）：逻辑 0 次读取（纯文档/烧录物身份）；
   删除可省 `.rodata` 160B + 线名字符串 ~120B，但会让 `check_flash_artifact.sh` 的「链表名 blob」身份判据失效
   （须同步改脚本）。
2. **`half` 列只读组 0**（第三十三轮报告列为独立项）：`build_chain_dst` 只读 `_22_1665_lines[0][j].half`，
   组 ≥1 的 `half` 从不被读（「每组可单独配半屏」是幻觉可配置性）；`role` 则每组都读。
   改成「行序约定 + 注释」可消除幻觉（行为等价，宿主逐位可证）。本轮保留原表结构。
3. **`chain_dst` 去表**（第三十三轮报告列为独立项）：`[4M][128]` = `1024·M` 字节 CCM（8×4 = 32768B，
   占本模组 71%）；改为「按需计算 + ~256B 常量模式表」可省绝大部分，代价 = `prepare` 热路径每元素 +1~2 指令
   + 上机复核，且 `check_equivalence.py` 的对照面要从「落点表内容」改为「`prepare` 输出」。
   **这也是 8×4 在 `PROTO=ALL` 下唯一现实的解法**（省下的 CCM 正好抵消溢出量）。

## 10. 风险提示（取消防御后的代价）

* **`MODULE_COLS > 5` 将静默越界访问 `_22_1665_lines`**：表只预声明 `HUB75_CHANNEL_MAX / 2 = 5` 组
  （每组 4 行 × 12B）。`MODULE_COLS ≥ 6` 时 `_22_1665_prepare`（`lines = _22_1665_lines[g]`）与
  `_22_1665_init` 的建表（`_22_1665_lines[g][j].line->port`）会读表外内存：
  * `-fsyntax-only` / `-Og` 下**可能零告警**（实测矩阵零告警）；
  * `-O3`（EIDE 口径）下 gcc 报 `iteration 4 invokes undefined behavior [-Waggressive-loop-optimizations]`
    —— 既是告警，也意味着**优化器有权按 UB 处理该循环**（可能少迭代/乱序）；
  * 运行期后果不可预测（读到的 `const hub75_pin_t *` 是垃圾指针 → 可能 HardFault）。
* **像素 / 帧位 > 65535** 会静默截断 uint16 落点与 scan 上界（`chain_dst` 是 `uint16_t`、`_22_1665_CLOCKS`
  参与 `uint16_t` 循环），表现为**错位/部分不刷新**而非报错。
* **单模块非 256 像素**（如 24×16）静默少画/错画。
* **整片 CCMRAM 装不下**：本模组对象单独 > 64KB 时链接期报错（可读）；**整片（本模组 + 其他消费者）
  超 64KB 时同样在链接期报错**，但**只看驱动编译通过会误判**（本轮实测：8×4 编过、链接溢出）。
* 上述契约现在只写在**源码注释**（文件头 2 行 + 各段头）与 `doc/01` §0.3/`§5`/`§6` 里——
  **改几何宏后请务必跑一次真实链接**（`make` 或 `link_variant.sh`）并跑
  `bash .analysis/22_1665/verify_all.sh --fast`。

## 11. 文档同步清单

| 文档 | 改动 |
|---|---|
| `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | ① 前置新增「第三十四轮（现行形态）」段，旧轮次「现行」标记改「历史」；② §0.3「合法性由编译期断言把关」→「**无编译期防御** + 约束清单 + 越界后果」；③ §0.5 开关表工作区宏值改 8 / 4；④ §0.6 横幅示例改 `128x64`；⑤ §0.7「上限口径」重写（无守卫 + 链接期兜底 + 8×4 实测溢出）+ 新增「第三十四轮口径快照」；⑥ §0.9 单模块像素段改「不再被编译期拦下」；⑦ §5 排障表：原「改宏后 EIDE 报编译错」行重写 + 新增「编过但屏不对」「链接期 overflow」两行；⑧ §6 遗留：8×4 的 CCM 现实 + **三项待决策**；⑨ **新增附录 A.20**（断言逐条清单 / 注释幅度 / 零回归 / 脚本同步 / 构建）；⑩ 附录 B 驱动行（372 行、无编译期防御）+ 报告/基线索引 |
| `doc/CLAUDE.md` | 22_1665 段：行数沿革 +372 行、上限口径改「无编译期守卫 + 链接期兜底 + 8×4 溢出」、断言条数「第三十四轮起 0 条」、CCM 通式段、内存布局条（8×4 = 46080B 与两 make 口径溢出）、initcall 表条目、模组对比表行、选编口径段（工作区 8×4 + `pick_log.sh` 说明）、附录索引 A.20 |
| `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` | §8.2 上限守卫句改「无编译期守卫、只剩链接期兜底」；§8.4.1 尾注补 8×4；§8.4.2 物理守卫注明「已于第三十四轮删除」；**新增 §8.4.3**（第三十四轮复采：四口径表 + 溢出算术 + 验证入口） |
| `doc/构建开关总表.md` | `DISP=22_1665` 行：上限口径改「无编译期守卫」、断言条数注明 0 条、`verify_all.sh` 计数、工作区宏值改 8×4 与溢出；轮次清单补第三十二/三十四轮 |
| `.analysis/22_1665/README.md` | 头部「第三十四轮口径（现行）」段；脚本表 6 处描述与计数（含新增 `check_round34_ab.sh` / `obj_fingerprint.py` / `comment_density.py` / `pick_log.sh`）；报告索引（34/33/32/31）；归档表新增 `archive/round34/`；现场判读 §4-1/§4-3/§4-6 |

## 12. 本轮产物清单

| 类别 | 文件 |
|---|---|
| 改动 | `Device/Display/dev_display_22_1665.c`（441 → **372 行**，无编译期防御） |
| 归档基线 | `archive/round34/dev_display_22_1665_pre_round34.c`（441 行 / md5 `af7fe2b44be42b96a14e43c96e0c6e10`） |
| 指纹 | `round34_baseline.txt` / `round34_current.txt`（工具 `obj_fingerprint.py`） |
| 日志 | `build_round34_baseline.log` / `build_round34_current.log` / `build_round34_all_1_263.log` / `build_round34_cq_1_263.log` / `build_round34_cq_22_1665.log` / `build_round34_cq_final.log` |
| 脚本 | 新增 `check_round34_ab.sh` / `obj_fingerprint.py` / `comment_density.py` / `pick_log.sh`；更新 `check_driver.py` / `check_compile_matrix.sh` / `check_eide_command.sh` / `link_variant.sh` / `check_round30_ab.sh` / `check_round32_ab.sh` / `verify_all.sh` |
| 文档 | `doc/01`（含 A.20）/ `doc/CLAUDE.md` / `doc/06-04`（§8.4.3）/ `doc/构建开关总表.md` / `.analysis/22_1665/README.md` / 本报告 |

**边界声明**：未 commit、未烧录、未改 `.eide/eide.yml`、未动 TIM3/TIM4 参数、未改任何表结构/逻辑/几何宏/行为开关。
