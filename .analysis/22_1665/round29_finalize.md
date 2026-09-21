# 第二十九轮：生产化收口 —— 删除「已验证完毕的化石可配置性」

**日期**：2026-09-17 晚  **对象**：`Device/Display/dev_display_22_1665.c`  **性质**：纯删除 + 文档/脚本同步（**有效路径行为零变化**）

> **前提裁决（用户）**：现场验证完全、驱动正常 ⇒ `_22_1665_HUB_WIRING`（模型 A 路径）、
> `_22_1665_BLUE_AS_LIT`、`_22_1665_DATA_ACTIVE_HIGH` 三个「化石开关」可以删。
> **归档纪律**：该驱动是 git 未跟踪文件（无历史可回退），故**先归档再删**。

---

## 0. 一句话结论

驱动 591 → **468 行**（diff −171 / +48），三个开关与模型 A 的全部代码路径删除，
**只保留模型 B 一条生成路径**；被保留的实体函数（`prepare` / `scan` / `build_chain_dst` /
`build_bsrr_table` / `init` / `_22_1665_role_bit`）在 **1×1 / 1×2 / 1×5** 三个几何口径下
**段级 20 段逐字节一致 + 反汇编逐条一致**（`check_equivalence.py` 75/75）；
两口径固件构建零新增告警（仅 3 条既有 HAL），宿主全量验证 **17 项全绿 / 0 失败**
（`check_flash_artifact.sh` 在重编当前树后回到 **7/7**，见 §5.3）。

---

## 1. 归档（恢复路径）

| 项 | 值 |
|---|---|
| 归档路径 | `.analysis/22_1665/archive/finalize/driver_pre_finalize_20260917.c` |
| md5 | **`2ccd650cc24c4360b24b151cc220e1b3`** |
| 行数 / 字节 | **591 行 / 33500 B** |
| 归档时形态 | 第二十八轮收口整理后、**三开关与模型 A 分支仍在**的最后一版（= 本轮删除的「删除前源码」） |
| 现行路径 / md5 / 行数 | `Device/Display/dev_display_22_1665.c` / `0fc30ad48ce7feb70ce6bd04630ddc3d` / **468 行** |

**恢复路径（怎么拿回旧行为）**：

```bash
# ① 直接用归档驱动替换（最简；默认口径即第二十八轮形态）
cp .analysis/22_1665/archive/finalize/driver_pre_finalize_20260917.c \
   Device/Display/dev_display_22_1665.c
# ② 要模型 A（同线级联）/「蓝不亮」/反向极性 —— 在归档驱动上加 -D
#    -D_22_1665_HUB_WIRING=1        模型 A：单组 4 根脚串全部模块、每帧 128×M 时钟
#    -D_22_1665_BLUE_AS_LIT=0       蓝分量丢弃（紫→只红、青→只绿、白仍红+绿）
#    -D_22_1665_DATA_ACTIVE_HIGH=0  数据极性反相（明暗反相时用）
# ③ 机器级对照（归档 vs 现行）随时可复跑：python3 .analysis/22_1665/check_equivalence.py
```

> 另：第二十五/二十六轮现场「默认画面 = 测试串 `"1\n2"`」的历史前提快照在
> `archive/finalize/app_default_display_test_1n2_20260917.c`（**不参与构建**，仅做历史脚本的断言锚点；
> 现行 `Application/Src/app_default_display.c` 已由用户回改为标准欢迎画面「欢迎行驶\n高速公路」）。

---

## 2. 删除清单（逐项）

### 2.1 宏（全部随开关删除）

| 已删宏 | 归档取值 / 形态 | 固化后现行 |
|---|---|---|
| `_22_1665_HUB_WIRING` | `#ifndef` 默认 `2U`；`1` = 模型 A 同线级联 | 宏与全部 `#if` 分支删除，只留模型 B |
| `_22_1665_BLUE_AS_LIT` | `#ifndef` 默认 `1U`；两版 `_22_1665_role_bit[2][8]` | 固化 `1`（蓝/紫/青/白 让红绿都亮），只留一张表 |
| `_22_1665_DATA_ACTIVE_HIGH` | `#ifndef` 默认 `1U`；建表里 `^ (_22_1665_DATA_ACTIVE_HIGH == 0U)` | 固化 `1`，异或写成常量形 `on = ((st >> j) & 1U) != 0U` |
| `_22_1665_PORT_MAX` | `(3U)`，**仅模型 A** 用（单组 4 线端口槽上限） | 删除（模型 B 用 `_22_1665_GROUP_PORT_MAX = 2U`） |

### 2.2 条件分支（`#if / #else / #endif`，全部为「模型 B / 模型 A」二选一）

| 位置（归档行号） | 删除的分支 | 保留的分支 |
|---|---|---|
| 接线派生宏（171~186） | 模型 A：`GROUP_COUNT=1` / `CLOCKS=128×MODULE_TOTAL` / `GROUP_MODULE_COUNT=MODULE_TOTAL` / `MODULE_OF_GROUP` 忽略 `g` | 模型 B：`GROUP_COUNT=MODULE_COLS` / `CLOCKS=128×MODULE_ROWS` / `GROUP_MODULE_COUNT=MODULE_ROWS` |
| `_22_1665_BSRR_SLOTS`（199~203） | 模型 A：`(_22_1665_PORT_MAX)` | `(_22_1665_GROUP_PORT_MAX * _22_1665_GROUP_COUNT)` |
| 合并写表声明（282~292） | 模型 A：`[PORT_MAX][STATES]` + 变量 `_22_1665_bsrr_port_cnt` | `[GROUP_COUNT][GROUP_PORT_MAX][STATES]` |
| `_22_1665_role_bit`（331~343） | `_22_1665_BLUE_AS_LIT=0` 版（蓝→不亮） | `BLUE_AS_LIT=1` 版（蓝→红+绿） |
| `scan` / flush（395~443） | 模型 A：`_22_1665_flush_state(uint8_t)`（3 槽 `always_inline`）+ 单组 `scan`（每帧 `128×M` 时钟） | 模型 B：`_22_1665_flush_group(g, st)`（按组查表）+ 按组分段 `scan`（每帧 `128×MODULE_ROWS`） |
| `build_bsrr_table`（492~581） | 模型 A 版（`PORT_MAX` 槽、填 `_22_1665_bsrr_port_cnt`） | 模型 B 版（逐组收集端口、填 `_22_1665_bsrr_slot_cnt[g]`） |

### 2.3 函数 / 变量（仅模型 A 路径）

- `_22_1665_flush_state`（模型 A 3 槽 flush，`always_inline`）——删除；
- `_22_1665_scan` 模型 A 版——删除（保留模型 B 版）；
- `_22_1665_build_bsrr_table` 模型 A 版——删除（保留模型 B 版）；
- `_22_1665_bsrr_port_cnt`（模型 A 端口计数变量）——删除。

### 2.4 断言（3 行→1 行；**净 −2 条**）

| 归档断言 | 处理 |
|---|---|
| `_Static_assert(_22_1665_HUB_WIRING == 1U \|\| _22_1665_HUB_WIRING == 2U, "…HUB_WIRING must be 1 … or 2 …")` | **整条删除**（开关不复存在） |
| `_Static_assert(_22_1665_PORT_MAX >= 2U, "…data pins must span >= 2 GPIO ports …")` | **整条删除**（`_22_1665_PORT_MAX` 是模型 A 专用宏） |
| `_Static_assert(_22_1665_HUB_WIRING == 1U \|\| 2U * MODULE_COLS <= HUB75_CHANNEL_MAX, …)` | **简化**为 `_Static_assert(2U * _22_1665_MODULE_COLS <= HUB75_CHANNEL_MAX, …)`（模型 B 口径；通道守卫保留） |

> **实际断言条数（本轮顺手更正 doc/01 §0.3 的笔误）**：现行驱动**真实 `_Static_assert` 共 17 条**
> （`grep -cE '^[[:space:]]*_Static_assert'`）；文档此前写「18 条」是把**文件头注释里的一处
> `_Static_assert` 字样**也算进去了。归档驱动为 19 条（17 + 2 条模型 A 开关断言）。
> （含注释提及的总出现次数：现行 18 / 归档 20。）

### 2.5 注释与文件头

文件头与章节注释中「模型 A / 开关」的描述同步删除或改写为「历史形态已删、恢复路径见归档」；
文件头 `/** … */` 由 48 行缩到 46 行；文件总行数 **591 → 468**。

### 2.6 明确保留：403~406 扩展点

归档 403~406 的 `#if _22_1665_GROUP_PORT_MAX > 2`（`_22_1665_flush_group` 里的**第 3 端口槽**）
**与本次删除无耦合**，现位于现行驱动 **347~350 行**，**保留**——理由：它是「换板后单组数据脚
跨 >2 个 GPIO 端口」的**零成本同步点**（`_22_1665_GROUP_PORT_MAX > 2` 恒假时不生成代码，
默认 2 槽下不落任何指令），删掉反而会让未来换板时 `flush` 分支与建表槽数不对称。

---

## 3. 保留清单与理由

| 保留项 | 理由 |
|---|---|
| **`_22_1665_CHAIN_HEAD_IS_MODULE0`**（默认 0） | **唯一的现场 A/B 旋钮**：决定「组内横向链首装组内第一块还是最后一块」。当前 `MODULE_ROWS = 1`（每组仅一块）时**无任何影响**，其适用场景（**`MODULE_ROWS ≥ 2` 的组内横向级联顺序**）**至今无现场证据**（doc/01 §0.8 遗留项 ①）——删掉就等于把「多模块横向首接」的排查手段一起删了。**建议保留到现场跑过 `2×1`/`2×2` 口径之后**（见 §7-1）。 |
| **守卫断言（17 条）** | 护栏、**零运行时成本**；且 `check_compile_matrix.sh` 的**非法口径拦截用例依赖它们**（模块数 0 / 非 256 像素模块 / 半屏高非 4 倍数 / CCM 超限 / 列数 > 5 共 7 条用例）。本轮**只删除随开关一起失效的 2 条**（§2.4）。 |
| **`MODULE_ROWS / COLS / PIXEL_ROW / PIXEL_COL`** | **改分辨率唯一入口**（`doc/01` §0.9：只改前两个宏，链段/落点/帧长全派生、不补任何表）。 |
| **`_22_1665_lines[5][4]` 表与 `name` 字段** | 硬件事实表（组 × 4 行，前 `MODULE_COLS` 组在用）；`name`（`"R1"…"G10"`）是**烧录物身份判读**依赖——`check_flash_artifact.sh` 用 `R1/G1/R2/G2` 与 `R3/G3/R4/G4` 的相邻 blob 判「这是 22-1665 且是模型 B 形态的镜像」。 |
| **`_22_1665_GROUP_PORT_MAX (2U)` 与 403~406 扩展点** | 见 §2.6。 |

---

## 4. 行为零变化的机器级证据

### 4.1 方法

同一 `-O3` 编译命令行（从 `build_round*.log` 抓），**两侧显式钉死同一宏口径**
（`-D_22_1665_MODULE_ROWS/_COLS/_PIXEL_ROW/_PIXEL_COL`），分别编译
**删除前归档源码**（`archive/finalize/driver_pre_finalize_20260917.c`）与**现行源码**
（`Device/Display/dev_display_22_1665.c`），然后：

1. **段级逐字节**：`arm-none-eabi-objcopy -O binary -j <section>` 逐段 md5；
2. **反汇编逐条**：`objdump -d` 取各 `.text.*` 段的指令序列逐条比对。

> 为什么应该「全等」：归档在默认口径下三个 `#if` 均走模型 B 分支（`HUB_WIRING=2` /
> `BLUE_AS_LIT=1` / `DATA_ACTIVE_HIGH=1`），**模型 A 分支与另一版颜色表/极性表达式根本不参与编译**；
> 模型 A 专属实体（`_22_1665_flush_state` / `_22_1665_bsrr_port_cnt`）在归档编译产物里**也不存在**
> （`#if` 排除 / 未引用）。故两侧产物必须逐字节一致——这是「只剩死分支被删」的直接证明。

### 4.2 结果（`check_equivalence.py`，75/75 全绿）

| 几何口径 | 段级逐字节 | `prepare` 指令 | `scan` 指令 | `build_bsrr_table` | `build_chain_dst` | `init` |
|---|---|---|---|---|---|---|
| **1×1**（16×16，硬件基线） | **20 段全同** | 逐条一致 | 逐条一致 | 逐条一致 | 逐条一致 | 逐条一致 |
| **1×2**（16×32，第二十七轮现场口径） | **20 段全同** | 逐条一致 | 逐条一致 | 逐条一致 | 逐条一致 | 逐条一致 |
| **1×5**（16×80，**工作区当前宏值**） | **20 段全同** | 逐条一致 | 逐条一致 | 逐条一致 | 逐条一致 | 逐条一致 |

- `_22_1665_flush_group` 是 `always_inline`（无独立段），其内联体随 `scan` 一并逐条一致；
- 模型 A 专属符号 `_22_1665_bsrr_port_cnt` / `_22_1665_flush_state` 在**两侧产物中都不存在**
  （断言「死函数/死变量不落镜像」）；
- 三段之外的小节：`_22_1665_role_bit` 固化后**只有一张 16B 表**（与归档 `BLUE_AS_LIT=1` 版逐字节相同），
  已在 `.rodata._22_1665_role_bit` 段内被上述「20 段全同」覆盖。

### 4.3 语义级证据（不受删除影响，仍全绿）

`check_equivalence.py` A~D 段（落点表 512 项 / `prepare` 8 组图案 `frame_state` /
合并写表 16×2 与每帧 256 次 `(端口,BSRR)` 写序 / `set_row` 电平）：
**1×1 归档 round24 版 vs 现行**逐字节一致，8 组图案置位点数（512/1/128/128/384/386）全部吻合。
`check_host_differential.py` 42/42：宿主编译产物「1×1 新 = 旧」+ 模型 B 各口径（1×1/2×1/1×2/2×2）
对**独立规格模型**逐字节一致（`*_A` / `blue0_inv` / `invpol` 等随开关消失的变体已退休）。

### 4.4 驱动对象段尺寸（各口径，`arm-none-eabi-size -A`）

| 口径 | `.ccmram`（本模组） | `.text`（驱动对象） | `.rodata`（含 240B `_22_1665_lines`） | `.bss` |
|---|---|---|---|---|
| 1×1 | **1664 B** | 908（8 段函数 + 40B `pl_hub75_Decoder_set_row`） | 388 | 1 |
| 1×2 | **3328 B** | 904 | 388 | 2 |
| **1×5（工作区当前）** | **8320 B** | 904 | 388 | 5 |

与通式 `1408·M + 256·MODULE_COLS` 完全吻合（1664 / 3328 / 8320）；
`link_variant.sh cols2` / `cols1` 重链实测同为 **3328 B / 1664 B**。

---

## 5. 构建与脚本结果

### 5.1 固件构建（GCC Debug `-Og`，`APP_DIAG_BANNER` 默认开）

| 口径 | EXIT | `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | `._user_heap_stack` | SRAM 合计 | 告警 |
|---|---|---|---|---|---|---|---|---|---|
| `make -j8`（默认 `DISP=1_263`） | 0 | 173492 | 203840 | 1672 | 38332 | 124720 | 2560 | 128952 | **仅 3 条既有 HAL** |
| `make -j8 PROTO=ALL DISP=22_1665`（1×5） | 0 | 173732 | 204096 | 1672 | **44156**（本模组 **8320**） | 124724 | 2564 | 128956 | **仅 3 条既有 HAL** |

> ⚠ 整项目 `.text/.rodata/.bss` 绝对值**含工作区他处在飞改动**（云南治超 YN_OL、网络通道等，
> 本轮会话期间被并行编辑；第二十八轮基线为 text 172164 / rodata 202952 / bss 124716），
> **与本模组无关**。比较本模组只看**驱动对象**段尺寸（§4.4）与线性增量：
> 1×5 − 1×2 = `8320 − 3328 = 4992 = 1408×3 + 256×3` ✔。
> 三条 HAL 告警为 `stm32f4xx_hal_flash_ex.c:948/1027/1063 -Wunused-parameter`（既有，非本轮引入）。

### 5.2 宿主全量验证（`bash .analysis/22_1665/verify_all.sh`）

| 项 | 结果 | 第二十八轮 | 变化 |
|---|---|---|---|
| `check_equivalence.py` | **75 / 0** | 46/46 | 改锚为「归档 vs 现行」×3 几何口径（第二重机器级证据） |
| `check_host_differential.py` | **42 / 0** | 70/70 | 退休模型 A 与 blue0/invpol 变体 |
| `check_driver.py`（默认 + `--geom 1x1/2x1/2x2`） | **54 / 0** ×4 | 54/54 ×4 | 断言改为「三开关已退休」 |
| `check_eide_command.sh` | **12 / 0** | 15/15 | 去掉模型 A / HUB_WIRING 非法值用例 |
| `check_compile_matrix.sh` | **17 / 0** | 25/25 | 合法 10 + 非法 7（去掉模型 A 与开关组合用例，几何/CCM/通道非法用例全保留） |
| `round25_two_modules.py` | 17 / 0 | 17/17 | 历史口径，锚定归档驱动 |
| `round26_two_hubs.py` | 27 / 0 | 27/27 | 同上 |
| `round27_wiring_b_and_etc.py` | 32 / 0 | 32/32 | 模型 B 取证（现行形态） |
| `link_variant.sh`（5 个变体） | `[ok]` ×5 | 9 个 | 退休 `wiringA1x2` / `wiringA11x1` / `blue0` / `invpol` |
| **合计** | **17 项全绿 / 0 失败** | 21 项全绿 | 见 §5.3 |

### 5.3 `check_flash_artifact.sh`：曾因**并发外部改动**瞬时失配，**重编后已回到 7/7**

- **最终结果**：`check_flash_artifact.sh` **7 通过 / 0 失败**（当场重编 22_1665 后实测）；
- **曾失配的原因**：`tree=` 由 Makefile 在编译 `app_boot.o` 时内嵌，而本轮构建期间工作区被
  另一会话并行编辑（`app_yn_ol_proto*.{c,h}`、`app_boot.{c,h}`、`app_tcp_client/server.c`、
  `app_dispatch.c` 等文件 mtime 更新），树哈希随即漂移 → 「是当前源码树的 make 构建」断言失败；
- **与本模组无关**：其余 6/7 项（驱动串 `2200001665`、`R1/G1/R2/G2` 与 `R3/G3/R4/G4` 双 blob、
  无旧 RTT/探针串）**全程通过**；
- **处置（本轮已执行）**：树静止后 `make clean && make -j8 PROTO=ALL DISP=22_1665` 重编，
  随即全量 `verify_all.sh` ⇒ **17 项全绿 / 0 失败**。
  **纪律**：`tree=` 随任何源码改动漂移，**改完源码必须先重编再跑该脚本**（否则红）。

---

## 6. 脚本与文档更新清单

**脚本（`.analysis/22_1665/`）**

| 脚本 | 改动 |
|---|---|
| `check_equivalence.py` | 机器级锚点**改锚**：从「钉模型 A 路径 vs round24」→「**删除前归档 vs 现行**」×3 几何口径（1×1/1×2/1×5）；文件头 E 段说明同步改写 |
| `check_host_differential.py` | 去掉 `*_A`（模型 A）与 `blue0_inv` / `invpol` 变体；规格模型按删除后实现收敛（42/42） |
| `check_driver.py` | 去掉「两版 `role_bit` 表都在」「模型 A / HUB_WIRING 锚点」断言；新增「三开关已退休（无 `#define`、无 `#if` 接线分支、无模型 A 函数）」断言 |
| `check_compile_matrix.sh` | 去掉 `HUB_WIRING` 非法值 / 模型 A 口径用例（25→17）；几何/CCM/通道非法用例**全保留并核对仍被拦** |
| `check_eide_command.sh` | 断言消息清单按剩余断言更新（15→12）；文件头注明三开关已删 |
| `link_variant.sh` | 注释注明退休变体；主体逻辑未变（`wiringA*` / `blue0` / `invpol` 由调用方移除） |
| `verify_all.sh` | 换口径变体 9→5（`geom32x8` / `mod2x1` / `mod2x2` / `flip1x2` / `cols5`）；索引/项数说明更新 |
| `round25/26/27_two_*.py` | **未改逻辑**——历史口径脚本锚定归档驱动与历史现场快照（`archive/finalize/`），保持可解释、不复红 |

**文档**

| 文件 | 改动 |
|---|---|
| `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` | 头部加第二十九轮块；§0.1 驱动链「唯一形态 = 模型 B」；§0.3 断言条数 **18 → 实际 17**（并注归档 19）；§0.4 蓝策略固化；**§0.5 开关总表 4 → 1** + 「已删除三开关」表 + 「自本轮起 `-D` 无效、恢复路径见归档」；§0.7 CCM 通式去模型 A；§0.9/§0.10/§0.11 模型 A 段标注「已删、用归档驱动」；**新增附录 A.17** |
| `doc/CLAUDE.md` | 22-1665 段：开关列表 4 → 1、行数 591 → 468、断言 17 条、CCM 通式保留模型 B、段尺寸标注含他处改动；各引用行同步 |
| `doc/构建开关总表.md` | §1/§2 的 22-1665 条目删三开关；修订记录加第二十九轮块 |
| `.analysis/22_1665/README.md` | 现行状态改第二十九轮；脚本表计数（75/42/54/12/17/7/17/27/32、项数 16+1）；变体索引 5 个；**归档与恢复路径**；现场判读速查去模型 A |
| `.analysis/22_1665/archive/README.md` | 新增 `finalize/` 行（本轮归档驱动 + 默认画面快照） |
| `.analysis/22_1665/round29_finalize.md` | **本报告（新增）** |

---

## 7. 待用户拍板

1. **`_22_1665_CHAIN_HEAD_IS_MODULE0` 是否也删？**
   *建议：暂留。* 它只在 `MODULE_ROWS ≥ 2`（组内横向串多块）时可见效，而该场景**从未上过现场**
   （doc/01 §0.8 遗留项 ①）。删了以后，现场首次跑 `2×1` / `2×2` 若左右两块内容对调，就只能
   回退归档驱动排查。**若用户确认「永远只用每口一块的 1×N 接法」，则可与守卫断言一起再收一轮**。
2. **守卫断言是否进一步精简？**
   *建议：不精简。* 17 条全为编译期、零运行时成本，且 7 条非法口径用例正依赖它们做拦截回归。
   唯一可讨论的是「模块数 ≤ 255（uint8_t）」这类基类字段约束——但收益为 0（不省 Flash/RAM）。
3. **是否把 `app_default_display.c` 的现场测试画面快照（`archive/finalize/app_default_display_test_1n2_…c`）
   也一并从归档清掉？** *建议：保留*（它是 round25/26 历史脚本的断言锚点，删了那两支脚本会复红）。
4. **`doc/01` §0.10 的 T7（`-D_22_1665_HUB_WIRING=1` 现场 A/B 溯源）** 已在现行驱动失效。
   *建议：保留为历史条目但显式标注「用归档驱动 + `-D`」，不再作为现场可执行步骤*（本轮已照此标注）。

---

## 8. 复跑清单（本轮全部命令）

```bash
# 机器级零回归（归档 vs 现行，3 几何口径）
python3 .analysis/22_1665/check_equivalence.py                 # 75/75

# 两口径固件构建（实测：均 EXIT=0、仅 3 条既有 HAL 告警）
make clean && make -j8                                          # 1_263，EXIT=0
make clean && make -j8 PROTO=ALL DISP=22_1665                   # 1×5，EXIT=0

# 换口径重链（驱动对象 .ccmram）
bash .analysis/22_1665/link_variant.sh cols2 -D_22_1665_MODULE_COLS=2   # 3328 B
bash .analysis/22_1665/link_variant.sh cols1 -D_22_1665_MODULE_COLS=1   # 1664 B

# 全量宿主验证
bash .analysis/22_1665/verify_all.sh                            # 17 项全绿 / 0 失败
```

日志：`.analysis/22_1665/build_round29d_1_263.log`、`build_round29d_22_1665.log`；
验证汇总：`.analysis/22_1665/verify_round29d.log`。
