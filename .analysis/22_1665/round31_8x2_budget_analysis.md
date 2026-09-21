# 22-1665 几何 8×2（128×32）编译失败诊断 — 第三十一轮（2026-09-18）

> 本轮**只诊断**：未改仓库任何源码/文档、未烧录、未 commit、未改 `.eide/eide.yml`。
> 复现构建 = `make DISP=22_1665 -j8`（**未** `make clean`），日志 `.analysis/22_1665/round31_build_8x2.log`。
> 整片口径实测在 `/tmp` 临时副本（仅把驱动的 CCM 上限临时抬到 24576）里做，仓库未被触碰。

## 1. 复现结果：一条错误，就是 CCM 预算断言

工作区实态：`Device/Display/dev_display_22_1665.c`（md5 `efc96ba3208d008952d1946806880c47`，432 行）
第 30/31 行 = `(8U)` / `(2U)`；`build/Debug/.build_stamp` = `CONFIG=Debug / TOOLCHAIN=gcc / PROTO=ALL / DISP=22_1665`
（与 `make DISP=22_1665` 同口径，无需全量重编）。

```
Device/Display/dev_display_22_1665.c:134:1: error: static assertion failed: "22_1665: CCMRAM budget exceeded (16KB; reduce MODULE_ROWS/MODULE_COLS)"
  134 | _Static_assert(_22_1665_CCM_BYTES <= 16384U,
      | ^~~~~~~~~~~~~~
make: *** [Makefile:553：build/Debug/Device/Display/dev_display_22_1665.o] 错误 1
make: *** 正在等待未完成的任务....
EXIT=2
```

- **首个错误 = 唯一错误**（全日志仅此 1 条 `error:`）；其余 240 个 TU 正常编译，链接未开始
  ⇒ 旧 `build/Debug/Project_STD.elf`（16:54 的 EIDE 产物）未被改写。
- EIDE 侧会得到**逐字相同**的报错：几何宏是文件内普通 `#define`，断言表达式与
  `PROTO_CHONGQING` / `STD_ALL_PROTO` / 包含路径无关；`.eide/eide.yml` Debug 目标
  `excludeList` 已排除 `1_263` / `22_1703`，`22_1665` 在 files 内 ⇒ EIDE Debug 编的就是本文件。

## 2. 全部 17 条 `_Static_assert` 与 8×2 判定

| # | 行 | 断言（表达式） | 消息 | 8×2 取值 | 判定 |
|---|---|---|---|---|---|
| 1 | 71 | `MODULE_ROWS>=1 && MODULE_COLS>=1` | MODULE_ROWS/MODULE_COLS must be >= 1 | 8 / 2 | 过 |
| 2 | 74 | `MODULE_ROWS<=255 && MODULE_COLS<=255` | module count must fit uint8_t (<= 255) | 8 / 2 | 过 |
| 3 | 77 | `MODULE_PIXEL_ROW % BLK_W == 0` | MODULE_PIXEL_ROW must be a multiple of 4 | 16%4=0 | 过 |
| 4 | 79 | `MODULE_PIXEL_COL % 2 == 0` | MODULE_PIXEL_COL must be even | 16%2=0 | 过 |
| 5 | 81 | `HALF_COLS>=4 && HALF_COLS%4==0` | half screen height >= 4 and multiple of 4 | 8 | 过 |
| 6 | 83 | `(PIX_ROW/4)*(HALF_COLS/4) == CHIPS` | one module per line = 8 blocks = 128 bits | 4×2=8 | 过 |
| 7 | 89 | `LINE_COUNT == 4` | data lines are fixed at 4 per group | 4 | 过 |
| 8 | 90 | `CHAIN_COUNT == 4*MODULE_TOTAL && >=4` | chain count must be 4 x module count | 64=64 | 过 |
| 9 | 94 | `STATES <= 256` | state count exceeds uint8_t | 16 | 过 |
| 10 | 95 | `BUFFER_SIZE <= 65535` | pixel count exceeds uint16_t | 4096 | 过 |
| 11 | 97 | `FRAME_BITS <= 65535` | frame bits exceed uint16_t (scan bound) | 2048 | 过 |
| 12 | 101 | `GROUP_MAX >= 1` | channel table must hold ≥1 R/G pair | 5 | 过 |
| 13 | 103 | `2*MODULE_COLS <= HUB75_CHANNEL_MAX` | per-column wiring needs 2 ch/column (2 x COLS <= 10) | 4 ≤ 10 | 过 |
| 14 | 106 | `MODULE_COLS <= GROUP_MAX` | MODULE_COLS exceeds line table rows | 2 ≤ 5 | 过 |
| 15 | 116 | `GROUP_COUNT*CLOCKS == FRAME_BITS` | state array = group count x frame clocks | 2×1024=2048 | 过 |
| 16 | **134** | **`CCM_BYTES <= 16384`** | **CCMRAM budget exceeded (16KB; …)** | **23040 > 16384** | **不过（唯一）** |
| 17 | 184 | `lines[5][4]` 形状 == `GROUP_MAX×LINE_COUNT` | line table must be GROUP_MAX rows x 4 lines | 5×4 | 过 |

验证方式：临时副本里只把第 134 行上限改成 24576，`make DISP=22_1665 -j8` **exit=0**、
驱动 TU 零告警（全项目仅 3 条既有 HAL `-Wunused-parameter`）⇒ 除本条外无第二处编译期守卫被触发。

## 3. 算账

### 3.1 本模组 CCM（驱动自己的公式 `1408·M + 256·COLS`，M = ROWS×COLS）

| 数组 | 声明 | 字节表达式 | 1×5（M=5, COLS=5） | 8×2（M=16, COLS=2） |
|---|---|---|---|---|
| `_22_1665_chain_dst[4M][128]` uint16_t | `[[gnu::section(".ccmram")]]` | 2·4M·128 = **1024·M** | 5120 | **16384** |
| `_22_1665_pixel_map[256M]` uint8_t | 同上（= 基类 `pixel_map`） | **256·M** | 1280 | 4096 |
| `_22_1665_frame_state[128M]` uint8_t | 同上（= 基类 `hub75_buff`） | **128·M** | 640 | 2048 |
| `_22_1665_bsrr_tab[COLS][2][16]` `pl_hub75_bsrr_t` | 同上（结构 8B：`uint32_t val` + `GPIO_TypeDef*`） | 8·2·16·COLS = **256·COLS** | 1280 | 512 |
| **合计** | | **1408·M + 256·COLS** | **8320** | **23040** |
| vs 预算 16384 | | | 余 8064 | **超 6656** |

> `_22_1665_bsrr_slot_cnt[COLS]`（uint8_t）与 `g_22_1665` 实例在 **SRAM**（`.bss` / `.data`），不计入 CCM。
> 单看 `chain_dst` 一项：M=16 时 16384 B == **整个 16KB 预算**——即使其余数组为 0，8×2 也不可能过。

### 3.2 预算内最大几何（阈值）

约束 `1408·M + 256·COLS ≤ 16384`（另有 `M = ROWS·COLS`、`COLS ≤ 5`）：

| COLS | 不等式解 | M 上限 | ROWS 上限 | 最大几何（16×16 模块） | 该点 CCM |
|---|---|---|---|---|---|
| 1 | M ≤ 11.45 | 11 | 11 | **11×1 = 176×16** | 15744 |
| 2 | M ≤ 11.27 → 偶数取 10 | 10 | 5 | **5×2 = 80×32** | 14592 |
| 3 | M ≤ 11.09 | 9（3 的倍数 ≤11） | 3 | **3×3 = 48×48** | 13440 |
| 4 | M ≤ 10.9 | 8 | 2 | **2×4 = 32×64** | 12288 |
| 5 | M ≤ 10.7 | 10 | 2 | **2×5 = 32×80** | 15360 |

- **全局最大模块数 M = 11（11×1）**；触发点：12×1 = 17152 > 16384（`check_compile_matrix.sh` 既有用例）。
- **128×32（= 8×2，M=16）在当前 16KB 预算下无任何摆法可过**；要 128 宽只能 ROWS=8：
  `8×1 = 128×16`（M=8，11520 ✓，但高度只有 16）；`8×2` 必超。
- 参考：**4×2 = 64×32**（M=8，11776 ✓）——正是 `build/Debug/Project_STD.elf`（EIDE 16:54 产物）的几何。

### 3.3 整片 CCMRAM（64KB）核算 —— 实测（非估算）

口径：`make PROTO=ALL DISP=22_1665`（GCC Debug，工作区当前树），**在 `/tmp` 临时副本构建**：

| 几何 | 本模组 | 非模组部分 | 整片 `.ccmram` | 64KB 余量 |
|---|---|---|---|---|
| 1×5（仓库当前宏值（旧）） | 8320 | **35836** | **44156** | 21380 |
| 8×2（上限临时抬到 24576） | 23040 | **35836** | **58876** | **6660** |

- 两个口径的「非模组部分」实测**逐字节一致 = 35836 B**，与 `doc/06-04 §8.4.1` 一致
  （44156 − 8320 = 35836；58876 − 23040 = 35836）。
- 8×2 实测整片 58876 B **远小于 64KB 硬限** ⇒ 编译期的障碍**只是驱动自设的 16KB 断言**，
  不是芯片/链接器限制。整片 64KB 硬限由链接脚本 `CCMRAM (xrw): ORIGIN=0x10000000, LENGTH=64K`
  （`Compiler/STM32F407XX_FLASH.ld:67`，`.ccmram` 段无 `ASSERT`，溢出即链接报错）兜底。
- 整片 8×2 下的模块**理论上限**：65536 − 35836 = **29700 B** ⇒ 10×2（M=20，28672）刚好
  64508 B（余 1028 B，不建议）；9×2（M=18，25856 → 61692，余 3844 B）较稳。
- 非模组 35836 B 的构成（`nm` 逐符号，2026-09-18 口径）：静态任务栈/TCB 25340 + 空闲/定时器
  内核缓冲 1736 + CQ 6377 + GZ_OL 1136 + YN_OL 1152 ≈ 35741，余为对齐填充。

### 3.4 仓库内 `build/Debug/Project_STD.elf` 的口径说明（不可直接当证据）

`arm-none-eabi-size -A build/Debug/Project_STD.elf`（16:54，**EIDE** 构建，非 make ALL 口径）：

```
.text 136664  .rodata 108832  .data 804  .bss 105988  .ccmram 23180  ._user_heap_stack 2564
```

- 其 `_22_1665_chain_dst` = 0x2000（8192）、`pixel_map` = 0x800（2048）、`bsrr_tab` = 0x200（512）
  ⇒ **几何是 4×2 = 64×32**（M=8；本模组 11776 B），不是 8×2（8×2 根本编不出来）。
- 非模组部分 = 23180 − 11776 = **11404 B**（EIDE Debug 口径：`excludeList` 排掉 LDI 与多数协议、
  无 `STD_ALL_PROTO`）⇒ 该 elf **不能**用于「make ALL 口径整片 CCM」的核算。
- 注意 `build/Debug/.build_stamp` 写着 `DISP=22_1665/ALL` 但产物是 EIDE 子集——即
  `doc/CLAUDE.md`「构建卫生 R1」记录的同名产物覆盖现象。

## 4. 其它约束（8×2 下逐条）

| 约束 | 表达式/来源 | 8×2 | 说明 |
|---|---|---|---|
| 通道数 | `2·COLS ≤ HUB75_CHANNEL_MAX(10)`（L103） | 4 ≤ 10 过 | COLS ≤ 5 仍是硬约束；8×2 只用组 0/1（PG9/PG10/PG15/PB6 + PB8/PB9/PE1/PE2） |
| 数据线表行数 | `COLS ≤ GROUP_MAX = 5`（L106） | 2 ≤ 5 过 | `_22_1665_lines[5][4]` 与模块数无关，不补表 |
| 每帧时钟 | `CLOCKS = 128 × ROWS` | **1024**（1×5 为 128） | 组内每线串 8 块 = 8×8 片 = 1024 位，状态数组仍 `GROUP_COUNT×CLOCKS = 2048` ✓ |
| 状态数组长度 | `GROUP_COUNT·CLOCKS == FRAME_BITS`（L116） | 2×1024 = 2048 过 | — |
| 落点/像素索引宽度 | `BUFFER_SIZE ≤ 65535`（L95） | 4096 过 | `_22_1665_region_offset` 返回 uint16_t，最大偏移 4095；`chain_dst` 元素 uint16_t 存像素偏移 ✓ |
| 扫描位宽 | `FRAME_BITS ≤ 65535`（L97） | 2048 过 | `for (uint16_t p = CLOCKS; …)`，CLOCKS=1024 ✓ |
| 基类字段宽度 | `modules_per_row/col` uint8_t（L74）、`screen_rows/cols`、`buffer_size` uint16_t | 8 / 2 / 128 / 32 / 4096 过 | — |
| BSRR 表槽位 | `[COLS][GROUP_PORT_MAX=2][16]`，`flush_group` 展开 2 槽（`#if >2` 分支） | 2×2×16×8B = 512 B 过 | 每组 4 脚仍落在 G+B / B+E 两个端口 |
| **扫描时序（软约束，非编译期）** | TIM3 周期 **500µs**（`Core/Src/tim.c`：Prescaler 84−1、Period 500−1，84MHz/84/500 = 2kHz） | ⚠ **紧** | `_22_1665_scan` 实测反汇编 ≈53 指令/时钟 × 1024 ≈ 54k 指令 ≈ **0.36~0.48ms**（按 doc 实测 1×2 = 45~60µs/128 时钟 × 8）⇒ 占 500µs 帧周期 **72%~96%**，需上机 DWT 复核 |
| **prepare 整帧重算（软约束）** | 反汇编 ≈76 指令/(模块×时钟位)；总 = 128·M·76 | ⚠ **重** | M=5 ≈ 0.32~0.38ms；**M=16 ≈ 1.0~1.2ms > 一帧 500µs**。仅在「有提交」的那一帧发生（静态画面只跑 scan），但 8×2 每次刷新提交都会跨帧（事件标志合并、掉帧，不崩） |
| 链长/电学 | 每数据线 8 块 × 8 片 = 64 片 = 1024 位 | 无编译期约束 | 时钟/数据扇出、线长需硬件确认 |

## 5. 方案与影响（只列选项，未动手）

**A. 回到预算内的几何（零代码改动，只改文件里两个宏）**：M ≤ 11。
- `11×1 = 176×16`（15744）、`10×1 = 160×16`、`8×1 = 128×16`（11520）、`5×2 = 80×32`（14592）、
  `4×2 = 64×32`（11776）、`3×3 = 48×48`、`2×4 = 32×64`、`2×5 = 32×80`、`1×5 = 16×80`。
- 注意 `ROWS=水平模块数 → 屏宽`、`COLS=垂直模块数 → 屏高`：要 128 宽的预算内组合只有
  `8×1 = 128×16`；**128×32 在 16KB 内不存在**。若现场就是「两行八列」，A 只能先保 80×32/128×16。

**B. 抬高驱动自设的 16KB 上限**：
- 8×2 需 ≥ **23040**；建议新上限 **24576（24KB）**（留 1536 B 余量，不静默贴边）。
  实测（临时副本）整片 58876 B、**余 6660 B**（约占 64KB 的 10%）。
- 连带更新（全部为文档/脚本，不含逻辑）：
  - 驱动：L128~131 注释「预算上限 16KB」+ L134 断言字面值；
  - `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`：§0 L149/L150（预算 ≤16KB）、
    L396（12×1 超限表）、L403（上限 16KB ⇒ COLS ≤ 5）、L667（排错表）、L685（§0.9 对照表）、
    L847（断言清单）、L1092（索引）；
  - `doc/CLAUDE.md`：L98、L279、L734、L1020（通式与上限、工作区宏值/整片 `.ccmram` 基线）；
  - `doc/构建开关总表.md`：L88、L289；
  - `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md`：§7/§8.4（本模组与整片复采）。
- **风险**：16KB 是**驱动自设预算**，芯片硬限是**整片 64KB CCMRAM**；抬高后几何一开就是 23KB，
  余量从 21380 B 掉到 6660 B。他处在飞改动（静态任务栈 1KB+100B/任务、CQ/GZ_OL/YN_OL 队列、
  以及「把 36KB 堆搬 CCM」的备选方案）都会与它抢这 6660 B；若同时把几何停在 8×2，整片再增
  6.6KB 即溢出（链接器报 CCMRAM overflow）。

**C. 不改上限而支持 128×32 的其它路（需要先确认信息，不臆断）**：
- 先确认：屏体是不是 **16 块 2200001665（16×16）按 8 列 × 2 行拼**、每行 8 块是否真的串在同一
  条数据线上、两个 HUB 口各自 4 根数据脚是否就是组 0/组 1 那 8 根（决定「用不用本驱动」）。
- 若换兄弟驱动：`1_263`（1/8 扫、行址线 A/B/C、224×64 现态）/ `22_1703`（1/4 扫）都是
  **不同扫描架构的模组**，不是同一种屏的替代品；只有当现场硬件确实是那两种模组时才是选项，
  届时按各自驱动重算 CCM（当前树另有两处在飞改动，口径未定，不在此估数）。
- 若坚持 22_1665 且 8×2：另一个**不改上限**的做法是**改驱动实现**——`chain_dst` 的 1024·M
  可以从「全量预计算表」降级为「按需计算 + 小块常量表」：M=16 时 `chain_dst` 一项就吃掉
  16384 B（= 整个预算），而 `region_offset` 是纯函数；去掉该表后本模组只剩 6656 B
  （pixel_map 4096 + frame_state 2048 + bsrr 512），16KB 上限绰绰有余。
  代价 = prepare 热路径每 (模块×位) 多十余条指令（本就 ≈76 指令/位，8×2 下已超一帧），
  且属代码改动 + 全套宿主回归（`verify_all.sh` 18 项）——须单独立项评估。
- 「一块 MCU 分两块屏」现架构不支持：`dev_display_register` 只保留最后注册的一个活动屏、
  `dev_display_start` 只建一个 scan_task（`Device/Display/dev_display.c:40/183`），
  且两块屏的 CCM 还会叠加。

## 6. 结论一句话

8×2（128×32）**不是芯片装不下**（整片实测 58876 B / 64KB），而是**驱动自设的 16KB 模块预算**
（`chain_dst` 单项在 M=16 时恰为 16384 B）在编译期拦下；同时即使放开预算，8×2 的
`scan`(≈0.36~0.48ms) 与 `prepare`(≈1.0~1.2ms，仅提交帧) 相对 **500µs TIM3 帧周期**也偏紧，
需上机 DWT 复核。预算内的 128 宽几何只有 `8×1 = 128×16`。
