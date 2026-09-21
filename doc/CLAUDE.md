# CLAUDE.md

此文件为 Claude Code (claude.ai/code) 在此仓库中工作时提供指导。

## 构建系统

STM32F407ZGTx 嵌入式项目，工具链 `arm-none-eabi-gcc`，C23 标准。

- **构建**：`make -j8`（根目录 Makefile，`TOOLCHAIN=gcc|clang`，`CONFIG=Debug|Release`，`PROTO=ALL|CQ`，**`DISP=1_263|22_1703|22_1665`**）
  - **`DISP ?= 1_263`**（2026-09-14 新增，2026-09-16 扩第三值，2026-09-17 第二十二轮复核）：显示模组三选一——
    `1_263`（默认，P6 32×32）、`22_1703`（P10 32×16 / 料号 2200001703，1/4 扫描）、
    **`22_1665`（16×16 红绿双色 / 4 条数据线 × 8 片 MBI5034B / 静态单扫）**：第二十四轮起**几何彻底参数化**——
    **改整屏分辨率只改 `_22_1665_MODULE_ROWS` / `_22_1665_MODULE_COLS` 两个宏**（屏宽/高 = 模块数 × 单模块像素，
    语义同 1_263/22_1703；数据线表 `_22_1665_lines[5][4]` 恒 5 组 × 4 行、链段 `4×M` / 落点 / 帧长全派生、**不补任何表**；
    **改的是文件里的值——第三十轮起这四个几何宏是兄弟驱动同款普通 `#define`，命令行 `-D_22_1665_MODULE_*=…` 不生效**
    〔只剩重定义告警 + 文件值胜出〕，宿主换口径用 `macro_override.py` 源码替换）、
    驱动内 RTT / 探针 / 兼容口径全部删除（第二十三轮；**第二十九轮生产化收口 591 → 468 行、第三十轮文件头瘦身后 432 行、第三十四轮取消编译期防御 + 精简注释后 372 行**——
    三个化石开关已删、只留模型 B 一条生成路径；复核 0 残留、无死宏）。
    **`22_1665` 现场调通（2026-09-17 用户确认显示正常）**：第一组四条链 = **R1(上半红) / G1(上半绿) / R2(下半红) / G2(下半绿)**
    （第二十轮探针 8 逐脚定标；B1/B2/A/B/C/D 六根实测无链）；「红口只上半屏 / 绿口全黑」的真根因 =
    `prepare` 逐链掩码表达式写错（第二十一轮一行修复）。本模组 CCM（模型 B 默认）**`1408·M + 256·COLS`**
    （`chain_dst` 1024M + pixel_map 256M + frame_state 128M + 合并写表 256·COLS；1×1 = **1664B**、1×2 = **3328B**、**8×2 = 23040B**；
    **无自设上限**（第三十二轮）**且第三十四轮起无任何编译期守卫** —— 17 条 `_Static_assert` 全删（含 64KB 物理守卫），约束降级为**源码注释契约 + doc/01 §0.3**：错误几何的后果 = 静默错口径 / 越界读 `_22_1665_lines`，或**链接期** `region CCMRAM overflowed`（工作区 8×4 = 本模组 46080B ⇒ `PROTO=ALL` 溢出 16380B、`PROTO=CQ` 溢出 14124B，**任何 make 口径都装不下**，只有 EIDE Debug 子集能装 57484B）；
    其他消费者实测 35836B ⇒ 本模组实用上限 ≈29700B、COLS=2 时 M≈20 为上限量级〔估算〕）、
    scan 热路径 ≈**55 指令/时钟位**（1×2 两组），
    **每帧时钟数 = 128 × MODULE_ROWS**（模型 B = 现行唯一形态；历史模型 A 的 `128 × 模块总数` 已随第二十九轮删除）（1×1 ≈ 25~35µs，1×2 ≈ 45~60µs；
    **⚠ 8×2 = 1024 时钟 ≈ 0.36~0.48ms，占 TIM3 固定 500µs 周期 72%~96%、prepare 整帧提交 ≈1.0~1.2ms 跨帧 —— 须上机 DWT 复核，本轮未改定时参数**）。
    **多模块接线（第二十七轮落地 = 模型 B「每列一口独立数据线」；第二十九轮起为唯一形态）**：
    接线模型开关 `_22_1665_HUB_WIRING` 与模型 A（同线级联）的全部代码路径已于第二十九轮删除。
    **模型 B** = 每列模块（= 一个 HUB 口 / 一个模块列）各占一组
    4 根数据脚，组 g 取兄弟驱动通道序**通道对 `2g / 2g+1` 的 R/G 脚**：**组 0 = R1/G1/R2/G2
    （PG9/PG10/PG15/PB6，现场逐脚定标）**、**组 1 = R3/G3/R4/G4（PB8/PB9/PE1/PE2；四脚已由
    `Core/Src/gpio.c` 配成推挽输出、与 DIP/SW/其余 HUB75 脚无冲突）**；各组**并行**（共享 CLK/LAT/OE）、
    **每帧 `128 × MODULE_ROWS` 时钟**，组内横向模块同线级联（`MODULE_ROWS=1` 时每组一块 ⇒ 每块收满 128 位
    ⇒ **HUB1 显逻辑上半、HUB2 显逻辑下半**；两块合起来才是完整逻辑屏）。
    **模型 A（第二十九轮已删除）** = 单组 4 根脚串全部模块，模块 m 占 `[(M-1-m)·128, (M-m)·128)`、
    链首装模块 M-1 的位流，每帧 `128 × M` 时钟（要旧口径用归档驱动 + `-D_22_1665_HUB_WIRING=1`）。
    现场 A/B 开关 `-D_22_1665_CHAIN_HEAD_IS_MODULE0=1`（默认 0；= 组内横向链首对调，
    `MODULE_ROWS=1` 时无影响）**保留**（`MODULE_ROWS ≥ 2` 场景无现场证据）。**现场测试矩阵见 doc/01 §0.10（T1~T8）**。
    **1×2（用户当前宏值）现场三现象已用宿主脚本逐位复现并解释**（默认全黑 / TEST 只出「MD」/ 口 2 全黑 =
    单块模组只收末尾 128 位 = 模块 1 = 屏体下半；口 2 的脚从未被驱动）——报告
    `.analysis/22_1665/round25_two_modules.md`；**1×2 派生路径本身无 bug**（宿主差分 40/40、几何自检 50/50×4 口径、链接通过）。
    **第二十六轮（2026-09-17 傍晚）双 HUB 口判定**：两块模组各插一个 HUB 口 + 默认画面 `"1\n2"`（FONT_16）
    ⇒ **HUB1 显「2」/ HUB2 全黑**，与**模型 B（每列一口、各占一组独立数据脚）**完全一致（HUB1 那块是链首、
    只留末 128 位 = 模块 1 = 第 2 行「2」；第二组候选脚 R3/G3/R4/G4 = PB8/PB9/PE1/PE2 **全帧一次未被写**——
    候选脚位按兄弟驱动通道序推、**未现场核对**）；H1（两口并联）/ H3（真级联）预测被现场否证
    （唯一保留的替代解释 = HUB2 硬件故障，现场 E1/E4 实验可分辨）；`-D_22_1665_CHAIN_HEAD_IS_MODULE0=1`
    只能让 HUB1 改显「1」、**HUB2 仍黑**（现场开关，非修复）；达成「HUB1 出 1 / HUB2 出 2」= 改接成
    真级联（**零代码** + flip 开关）**或**模型 B（**需先知 HUB2 四根脚真实脚位，禁止猜**）。
    本轮**源码零改动**，`round26_two_hubs.py` 27/27。**（旧固件只写第一组脚、HUB2 黑的故事到此为止：
    第二十七轮起模型 B 逐帧驱动两组的 8 根脚。）**
    **第二十七轮（2026-09-17 晚，现行）**：按用户裁决「第二组脚按兄弟驱动口径取」**实现模型 B 为默认**
    （见上一条）⇒ 1×2 每帧 **128** 时钟、机器级写序含 **PB8/PB9/PE1/PE2**；**ETC 七行现场现象零剩余解释**
    （单块模组只见逻辑下半 + 单行帧在 16px 宽屏只可能出「12」；唯一自洽 = 工具 **0-based 行号**（第一行 → 行号 0 = 全屏）
    + 观测前清屏，7/7）——报告 `.analysis/22_1665/round27_wiring_b_and_etc.md`、doc/01 §0.10/§0.11、附录 A.15。
    本轮**本模块增量（同树 A/B 编译实测）**：驱动对象 `.text +572B`（建表 init 变大）/ `.rodata ±0` /
    `.ccmram +128B`（= 256·COLS）/ `.bss +1B`；`_22_1665_scan` 反而 **−8B**（132 → 124，每帧只 128 时钟）。
    本轮**构建（1×2，模型 B）**：text **172164** / rodata **202952** / data 1672 / ccmram **39164**
    （本模组 **3328B** = `1408×2 + 256×2`）/ bss 124712 / `_user_heap_stack` 2560（SRAM 合计 128944，余 2128B），
    elf md5 `见构建日志/横幅（本轮最终构建 elf md5 见 .analysis/22_1665/round27_wiring_b_and_etc.md §3）`；重链差值：1×1 = −1664B、模型 A 1×2 = −128B、2×2 = 6144B 全部自洽。
    `round27_wiring_b_and_etc.py` **32/32**、`verify_all.sh --fast` **12 项全绿** / 全量 **21 项全绿**
    （`check_host_differential.py` **70/70**、`check_driver.py` **54/54**×4、`check_eide_command.sh` 15/15、
    `check_compile_matrix.sh` 25/25、`check_flash_artifact.sh` 7/7、round25 17/17、round26 27/27）。
    **注**：工作区整项目 `.ccmram` 绝对值含他处在飞改动（静态任务栈 `s_task_*_stack` 等），与本模组无关。
    **第二十八轮（2026-09-17 晚，现行 = 收口整理）**：① **调试残留审计**——驱动内 0 处 RTT / printf /
    SEGGER / diag / probe / debug / selftest 关键词、0 处注释掉的代码块与 `#if 0`，每个 `#define` 与静态符号
    都有消费者（4 个行为开关 + 4 个几何宏**全部保留**——第二十九轮起行为开关删至 **1 个**、几何宏仍 4 个，各有宿主矩阵 / `link_variant` / 文档消费者；
    标定工具仍在 `.analysis/22_1665/archive/prev_round/`）；② **格式对齐兄弟驱动 `dev_display_22_1703.c`**
    （文件头 54 → **49 行**按 22_1703 结构重写；章节分隔注释去中文序号；宏/表格/结构体排布对齐；
    模块码宏 `_22_1665_MODULE_CODE` → **`MODULE_CODE`**、合并写表 `g_bsrr_tab`/`g_bsrr_slot_cnt`/`g_bsrr_port_cnt`
    → **`_22_1665_bsrr_tab`/`_22_1665_bsrr_slot_cnt`/`_22_1665_bsrr_port_cnt`**；595 → **591 行**，注释净减、代码零增删）；
    ③ **行为零变化**（机器级 A/B 双编译：整理前后驱动对象 **15 个 section md5 全同、反汇编 0 行差异**，
    模型 A 口径同法一致；`link_variant.sh cols2` 重链 = 第二十七轮 1×2 基线），fast **12 项** / 全量 **21 项**全绿；
    ④ 顺带修正 `dev_display.h` / `app_test.c` / `app_boot.c` 里第二十三轮旧形态的过时描述
    （app_test 的 22_1665 口径前提、doc/01 §13 → §5 指针）、删除 doc/01 §0.3 结尾重复段落；
    ⑤ **脚本修复**：`check_host_differential.py` 的用例改为**两个几何轴都显式钉死**（原先只钉一轴，
    源码 `MODULE_COLS` 改成 5 后宿主侧 CCM 预算失真触发断言；覆盖不变仍 70/70）。
    报告 `.analysis/22_1665/round28_cleanup.md`、doc/01 附录 A.16。
    **第二十九轮（2026-09-17 晚，现行 = 生产化收口）：删除「已验证完毕的化石可配置性」**——按用户裁决
    （现场验证完全、驱动正常）**只保留模型 B 一条生成路径**：① 模型 A 全部分支 + 接线开关 `_22_1665_HUB_WIRING`
    （含模型 A 专用 `_22_1665_flush_state` / `scan` / `build_bsrr_table` 与 `_22_1665_bsrr_port_cnt`、只服务模型 A 的断言）；
    ② 蓝分量开关 `_22_1665_BLUE_AS_LIT`（固化现行取值 1）；③ 数据极性开关 `_22_1665_DATA_ACTIVE_HIGH`
    （固化现行取值 1，建表异或写成常量形）。**行数 591 → 468**；**行为开关由 4 个减为 1 个**
    （仅 `_22_1665_CHAIN_HEAD_IS_MODULE0`）+ 4 个几何宏；`_Static_assert` **第三十四轮起 0 条**（曾 17 条：第二十九轮删 2 条随开关失效的，
    几何/CCM/通道/端口数/uint 宽度等守卫全保留）。**行为零变化**：1×1 / 1×2 / 工作区 1×5 三个口径下
    「删除前归档源码 vs 删除后现行源码」的驱动对象**段级逐字节一致 + prepare/scan/建表/init 反汇编逐条一致**
    （`check_equivalence.py` **75/75**）。**删除前源码完整归档**（该驱动是 git 未跟踪文件，唯一恢复路径）：
    `.analysis/22_1665/archive/finalize/driver_pre_finalize_20260917.c`（md5 `2ccd650cc24c4360b24b151cc220e1b3`、591 行）
    ——`cp` 回去即第二十八轮形态，加 `-D_22_1665_HUB_WIRING=1` / `-D_22_1665_BLUE_AS_LIT=0` /
    `-D_22_1665_DATA_ACTIVE_HIGH=0` 可复现旧口径；**自本轮起这三个 `-D` 对现行驱动完全无效**（宏已不存在）。
    `verify_all.sh --fast` / 全量均 **17 项全绿**（换口径变体 9 → 5，退休 `wiringA*` / `blue0` / `invpol`；
    `check_host_differential.py` 70 → **42/42**、`check_eide_command.sh` 15 → **12/12**、
    `check_compile_matrix.sh` 25 → **17/17**、`check_equivalence.py` 46 → **75/75** 改锚为归档 vs 现行）。
    报告 `.analysis/22_1665/round29_finalize.md`、doc/01 附录 A.17。
    **⚠ 工作区当前宏值 = `MODULE_ROWS=1 / MODULE_COLS=5`（16×80）**——该值是本轮会话期间的外部改动
    （第二十七轮现场口径为 1×2），**未回改**：该口径整项目 `.text 172164 / .rodata 202952 / .ccmram 44156`
    （本模组 **8320B** = `1408×5 + 256×5`）/ `.bss 124716` / SRAM 128952B（余 2120B）；
    宿主复现 1×2 口径（= 39164B）用 `bash .analysis/22_1665/link_variant.sh mod1x2 _22_1665_MODULE_COLS=2`
    （**源码替换**；第三十轮起 `-D_22_1665_MODULE_COLS=2` 写法已不生效）。**`Application/Src/app_default_display.c` 当前仍是
    第二十六轮现场的测试画面 `"1\n2"`（FONT_16），未擅自回改**（见报告「待用户拍板」）。
    **本模组 CCM 通式（模型 B = 现行唯一形态）= `1408 × M + 256 × MODULE_COLS`；上限口径（第三十四轮起）=
    无自设预算、**无编译期守卫**（约束降级为注释契约 + doc/01 §0.3；整片占用由链接期兜底，超区域报
    `region CCMRAM overflowed by N bytes`）；物理硬约束 `2 × MODULE_COLS ≤ HUB75_CHANNEL_MAX = 10`（⇒ **COLS ≤ 5**，
    越限 = 越界读 `_22_1665_lines`）；历史模型 A 的
    `1408 × M + 384`（M ≤ 11）已随第二十九轮删除**。
    **1×1 行为零回归**由 `.analysis/22_1665/check_equivalence.py`（语义级 + **机器码对照：删除前归档驱动 vs 现行**，1×1/1×2/1×5 三口径 75/75）+
    `check_host_differential.py`（**机器级 42/42**：新旧编译产物 1×1 逐字节 + 模型 B 各口径对规格模型）双重证明。
    **第三十轮（2026-09-17 夜，现行 = 文件头瘦身 + 几何宏写法对齐兄弟驱动）**：① 驱动文件头 **46 → 16 行**
    （长篇叙述移入 `doc/01` §0；全文 **468 → 432 行**）；② 四个几何宏由 `#ifndef` 守卫改**兄弟驱动同款普通
    `#define`**（`1_263` / `22_1703` 本就是普通 `#define`）⇒ **`-D_22_1665_MODULE_*=…` 不再生效**——
    换几何口径一律**改文件**，宿主脚本走 `macro_override.py` **源码替换**（`link_variant.sh` 几何参数写
    `宏名=值`，给几何宏传 `-D` 被直接拒绝）；**`_22_1665_CHAIN_HEAD_IS_MODULE0` 未动**（仍 `#ifndef` + 可 `-D`）。
    **行为零变化**：改动前基线 `.analysis/22_1665/archive/round30/dev_display_22_1665_pre_round30.c`
    （468 行 / md5 `abe51c0ba7dc5c0bac06973a2c53109b`）vs 现行 ⇒ **22 个 section 逐字节一致 + 反汇编 360 行
    0 行差异**（新脚本 `check_round30_ab.sh`，已并入 `verify_all.sh`）。**实测段尺寸**：`ALL/1_263`
    `.text 174996 / .rodata 204848 / .data 1680 / .ccmram 38332 / .bss 124772 / ._user_heap_stack 2564`
    （SRAM 余 **2056B**）；`ALL/22_1665`（1×5）`.text 175252 / .rodata 205104 / .ccmram 44156`（本模组 **8320**）；
    驱动对象 `.ccmram` 1×1 = **1664** / 1×2 = **3328** / 1×5 = **8320 B**；两口径各仅 3 条既有 HAL 告警。
    脚本计数：`verify_all.sh --fast` **13 项** / 全量 **18 项** 全绿（`check_compile_matrix.sh` 17 → **18/18**、
    `check_eide_command.sh` 12 → **15/15**、`check_driver.py` 54 → **56/56** ×4；`check_equivalence.py` 75/75、
    `check_host_differential.py` 42/42 不变）。报告 `.analysis/22_1665/round30_header_and_macros.md`、doc/01 附录 A.18。
    **as-built（几何宏算式 / 数据线表 / 级联时钟 / 开关 / 现场核对 / 构建基线）与「**改分辨率只改两个宏**」操作步骤（含口径对照表、CCM 通式）见 `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` §0（§0.3 模型 / §0.9 操作 / §0.10 现场测试矩阵）**；
    第一~二十七轮的演进、探针 7/8/9 用法（**已归档，换屏标定从 `.analysis/22_1665/archive/prev_round/` 取回**）与作废结论见同文档附录 A（A.17 = 第二十九轮，**A.18 = 本轮**）；宿主脚本（`verify_all.sh` 一键入口 + 索引）
    见 `.analysis/22_1665/README.md`。
    三模组 CCMRAM 显存 + CQ 6377B + 贵州治超 1136B + 云南治超 1152B **不可同编**（同编 = .ccmram 溢出或双实例注册、
    活动屏由链接序决定）；非法值 make 直接 `$(error)` 报错。EIDE 侧等价语义由 `targets.Debug.excludeList` 表达
    （见「选编口径」；**22_1665 已收录进 EIDE 并选为 Debug 编译口径**）。
  - GCC Debug：`make -j8`（默认，`-Og -g`）
  - GCC Release：`make -j8 CONFIG=Release`
  - Clang Debug：`make -j8 TOOLCHAIN=clang`
  - Clang Release：`make -j8 TOOLCHAIN=clang CONFIG=Release`
  - **口径切换必然重链接（2026-09-14 修复）**：elf 依赖 `$(OBJ_ALL)` + 口径指纹
    `build/$(CONFIG)/.build_stamp`（内容 = CONFIG / TOOLCHAIN / PROTO / DISP /
    DEFINES / CFLAGS / LDFLAGS / Makefile cksum / 源列表 `SRC_FILES`），每个 `.o` 同样
    依赖该 stamp——**切口径（PROTO / DISP / CONFIG / TOOLCHAIN 及后续新增开关）必然
    全量重编 + 重链接，不再需要手工删产物**；口径不变时 stamp 内容相同（`cmp` 后才改
    写），增量构建照常（重复 `make` 零编译零链接）。机制/纪律见 `doc/构建开关总表.md` §4。
  - **构建卫生（2026-09-14 补，R1~R3；机制见 `doc/构建开关总表.md` §4.2）**：
    ① **EIDE 构建覆写 `build/Debug/` 共享产物**（`Project_STD.elf/.hex/.bin`，新 mtime）后，
    同口径 `make -j8` 会 **0 编译 0 链接、静默复用 EIDE 的 elf**（口径指纹只跟踪口径内容，
    感知不到 mtime-only 覆写）→ **EIDE 构建后必须 `make clean && make`**（或给 EIDE 独立输出
    目录）；烧录前用 `arm-none-eabi-size -A` 核对默认口径 `.text=171860 .rodata=202592
    .data=1672 .ccmram=38332 .bss=124712`（2026-09-17 堆修复轮口径；权威值 doc/06-04 §8.4）；重复 `make` 的
    「0 编译 0 链接 + md5 不变」仅当最后一次构建确为 make 时有效。
    ② `make -n` 干跑显示"全量过期"属预期（stamp 规则带 `FORCE`，dry-run 不执行配方即假设
    stamp 已变；真实构建 `cmp` 相同不触碰 mtime → 仍 0 编译 0 链接），**不要用 `make -n`
    判断是否需要重建**。③ `PROTO` **无非法值守卫**（`PROTO=bogus` 静默按 `ALL` 走，与 `DISP`
    的 `$(error)` 不对称）——按用户裁决**保持现状**，仅作为已知行为记录，不加守卫。
  - **头文件依赖**：`CFLAGS` 含 `-MMD -MP`，`.d` 与 `.o` 同目录、末尾 `-include` 引入——
    头文件改动触发依赖它的 TU 重编译（不再依赖手工全量重建）
  - **`PROTO ?= ALL`**：默认全协议 dev 构建（CQ 恒定编入，位于 LDI 源之后）。
    **`PROTO=CQ`**：重庆量产口径——从源码列表剔除 LDI 目录文件、追加
    `-DPROTO_CHONGQING`（`{` 帧族仍编入并保留 `-DSTD_ALL_PROTO` 豁免守卫。
    **注（2026-08-24 修订）**：TCP Server/Client 通道两口径**均启动**——
    `app_boot.c` 实际无 `#ifndef PROTO_CHONGQING` 口径裁剪，此前「CQ 构建不
    启动 TCP 通道」表述与代码不符，已按代码实态更正）。
    **网络配置应用（两口径统一，2026-08-21 解耦，2026-08-24 端口应用两口径化）**：
    中立模块 `app_net_boot`（`app_net_boot_apply`，`app_boot.c` init_task 在
    `app_board_net_cfg_fw_version_update` 之后、splash 之前调用——fw_version_update
    空扇区初始化 net_cfg=0，app_net_boot 随后判无效写默认）读 Sector1 net_cfg →
    `pl_net_set_ip` + `app_tcp_server_set_port`（口取 `net_cfg.port` = TCP 业务口，
    两口径统一应用）；Sector1 空/损坏/非法 → accept_write 写本构建默认记录落盘并
    应用（方案 B 2026-08-21：两口径默认均 **port=9528（TCP 口）+ udp_port=20103
    （CQ UDP 口）**，IP 各自口径）：CQ 192.168.1.5/255.255.255.0/192.168.1.1，
    dev 192.168.114.200/255.255.255.0/192.168.114.1。
    LDI `ldi_ctx_init` 与 CQ `cq_proto_init` 不再直接改 netif（ldi 保留 Sector1/W25
    自愈与配置装载）。
    **CQ setip 双构建语义**（doc/03 PartB B.7.1，2026-08-21 方案 B）：setip 命令
    端口写入 Sector1 `net_cfg.udp_port`（CQ UDP 业务口专有字段），TCP 口
    `net_cfg.port` **保留现值不再被 setip 污染**；改 IP 两构建重启后均生效（均走
    `app_net_boot_apply`）；改端口仅 PROTO_CHONGQING 构建对 CQ 业务口生效
    （`_udp_cq_read_port` 读 Sector1 net_cfg.udp_port，无效/0 回退 20103），dev
    构建 CQ 业务口固定 20103（`#else` 分支）、setip 的 port 对 dev 构建任何口都
    不生效——端口修改验证须用 `make PROTO=CQ` 或 EIDE `PROTO_CHONGQING` 目标。
    **CQ 心跳超时故障屏开关**（`app_cq_proto.h` `CQ_FAULT_SCREEN`，1 开 / 0 关，
    doc/03 PartB B.7.2，2026-08-21 新增、2026-08-24 改名单层宏）：默认跟随构建
    口径——PROTO_CHONGQING 开启、共存/其他构建（通用版多协议固件）关闭（他省
    上位机不发重庆 syn1，120s 故障屏会误触发）；可显式覆盖 `-DCQ_FAULT_SCREEN=1`
    强制开 / `=0` 强制关（旧名 CQ_FAULT_SCREEN_ENABLED / CQ_FAULT_SCREEN_FORCE_ON
    已废弃，不保留兼容别名）。
    关闭时 `cq_proto_timer_task` 不计数、不渲染（`s_cq_sync_counter` 恒 0、
    `s_cq_fault_shown` 恒 false），warn1 倒计时不受开关影响。
    **不变量：排除 LDI 与定义 `PROTO_CHONGQING` 必须成对**（app_net_boot 兜底后
    中间态不再致命，但 PROTO_CHONGQING 仍决定默认口径与端口应用对象，不成对 =
    口径错配）。EIDE Debug 目标当前为 CQ 量产口径（excludeList 排除 ldi +
    defineList 含 PROTO_CHONGQING），且**显示模组编 22_1665**（excludeList 同时排除
    `dev_display_1_263.c` 与 `dev_display_22_1703.c`，用户 2026-09-16 裁决切换；
    改选前为 22-1703）——详见「选编口径」小节。
- **诊断开关（`APP_DIAG_BANNER` / `make APP_DIAG`）**：自证版诊断（2026-09-14 GZ_OL 复测轮）——
  `Application/Inc/app_diag.h` 内 `APP_DIAG_BANNER` **默认 1**；`make APP_DIAG=0`（或改宏为 0）
  一键关。开启时 RTT 通道 0 打印**开机横幅 + 延迟体检**（`app_boot.c` `app_diag_boot_banner`/`app_diag_boot_late`：
  `fw`/`built`/`tree` 树哈希 + 口径四元组 + `display screen=WxH` + `driver linked?` + 字库芯片 + `netcfg` +
  四个 UDP 端口与**实测绑定结果 `bind=OK/SKIPPED/FALLBACK/FAIL`**），GZ_OL `0x20`/`0x40` 另打逐帧证据
  （`GZ_OL_RTT_DIAG` 默认跟随总开关，复测轮 ② 加写 0x40 原始载荷 dump + 启动 `netcfg INVALID` 告警）。**增量 A/B 实测：Flash +5616B（text +2640 / rodata +2976）、RAM +0B**（`make APP_DIAG=0` 与加写前逐字节同尺寸）；
  唯一行为差异 = 开机默认画面延后 2.5s（横幅后的 `osDelay(2500)`）。**现场判「板子跑的是哪份镜像」看有无 `[diag]` 行**——
  旧镜像没有该设施（`doc/14` §12.7/§12.8 有判读表与现场清单）。
- **`APP_DIAG_TREE_HASH` 等构建指纹**（Makefile `DIAG_DEFS`，仅对 `app_boot.o` 追加，不进 `DEFINES`/stamp）：
  树哈希 = 全部 `*.c/*.h/*.ld` + Makefile 内容的 md5 前 8 位，随任何源码改动变化——用于自证「板上固件 = 哪棵树」。
- **`{` 帧族编译期互斥守卫**：Makefile 默认定义 `-DSTD_ALL_PROTO`（全协议共存开发构建，`g_brace_proto_guard` 守卫失效）；EIDE 量产构建不定义该宏——多个 `{` 帧族协议（青海/山东/贵州/四川MTC）同时编入即链接报 `multiple definition of 'g_brace_proto_guard'`，强制互斥。
- **EIDE 持有 eide.yml（外部编辑纪律）**：EIDE 运行时用自己的内存模型回写 `.eide/eide.yml`，会冲掉外部直接编辑（曾两次发生：CQ 文件夹收录、app_net_boot.c 收录被回写丢失，导致 EIDE 构建 undefined reference）。**外部改完 yml 后必须立刻在 EIDE 中「重新加载项目」（EIDE: Reload Project）让它读入新条目，期间不要先做任何会触发保存的 GUI 操作**；构建前先确认 EIDE 编译列表含新增源文件。
- **编译数据库**：`bear --output build/Debug/compile_commands.json -- make -B -j8`（`-B` 强制全量重编译）
- **编译开关总表**：`doc/构建开关总表.md`（2026-08-24 新增，收录 PROTO_CHONGQING / STD_ALL_PROTO / CQ_FAULT_SCREEN / g_brace_proto_guard 等全部编译期开关及各构建入口默认值）
- **烧录**：`openocd -f ./Compiler/stm32f407zg.cfg -c "init; halt; program ./build/Debug/Project_STD.hex verify reset exit"`
- **整片擦除**：`openocd -f ./Compiler/stm32f407zg.cfg -c "init; halt; stm32f4x unlock 0; stm32f4x mass_erase 0; shutdown"`
- **J-Link 一键烧录**：`bash tool/flash_all.sh`（Bootloader + Recovery + 主固件一次 J-Link 会话烧完，**默认烧后擦除 Sector1（0x08004000~0x08007FFF）恢复出厂配置态**；`--keep-config` 保留板级配置；`--erase` 整片擦除；`--verify` 校验；`--dry-run` 预览命令不烧录）
- **调试期烧录陷阱（已规避）**：Bootloader 条件 D（非 debug 模式）校验 Sector1 记录 `app_info.size/crc32` 与 `0x08040000` 实机固件 CRC 是否一致，`app_info.size/crc32` 真实值只由 Recovery 升级流程写入（`app_info.version` 由主固件启动时从 PROGRAM_CODE 落库，供 IAP 0x03 上报）。一旦 Recovery 成功升级过一次，再用 J-Link/EIDE 直接重烧主固件而不更新 Sector1 → 条件 D 校验失配 → Bootloader 判「App 损坏」→ 设备永远进 Recovery。**纪律：任何烧录器（J-Link/OpenOCD/EIDE）烧完主固件后必须擦除 Sector1 恢复出厂态**——Sector1 空 → Bootloader 走条件 C（出厂初始化）→ 正常跳主固件；net_cfg 丢失副作用可接受，主固件上电 `ldi_ctx_init` 从 W25Qxx 外部 Flash 同步回写（空扇区自动完整初始化）。`tool/flash_all.sh` 已默认执行该纪律；手动烧录/单固件烧录同样遵守「烧完擦 Sector1」。
- **三固件同步烧录纪律（方案 B 布局变更，2026-08-21）**：方案 B 把 Sector1 记录从 68B 扩到 72B（`NetConfig_t` 新增 `udp_port`，`config_crc` 偏移 64→68，Bootloader/Recovery 的 `config_info.h` 已同步修改）。**设备上 Bootloader/Recovery 若仍是旧 68B 布局（未随主固件重烧）**：旧 Bootloader 按旧偏移读 `config_crc`（读到新记录的 `udp_port` 字段）→ 判 CRC 失配 → 重建**旧布局**出厂记录；主固件按 72B 读又失配 → 反复自愈改写，两布局交替重建、Sector1 永不稳定（网络配置回出厂态；残留升级中间态还可能被旧 Bootloader 条件 A/B/D 误判 → 设备陷 Recovery、主固件不运行 → UDP 10011/20103 全部无响应，LDI 搜索/CQ 业务均失效）。**纪律：方案 B 后三固件必须同步烧录（`tool/flash_all.sh` 一次 J-Link 会话）**，单固件重烧后按惯例擦 Sector1。
- **0x03 版本为空判断路径**：现场 IAP 0x03 上报 `app_info.version` 全 0 时，按序判断：
  ① 看 RTT 是否有 `[fwver]` 日志——**无日志 = 主固件从未运行，设备落在 Recovery 态**（旧
  Sector1 记录 size/crc32 与新烧固件失配 → 条件 D 判 App 损坏），补做「擦 Sector1」后重启
  即可；有 `[fwver] write ok` 仍为空 = 应答来自 Recovery（0x03 应答 ReData[10] `update_sta`
  ==2 且 ReData[0] `size`≠0 为 Recovery 态特征，主固件态 update_sta==0）；有 `erase/write
  failed` = 擦写路径问题；② 确认烧录物为最新构建（`arm-none-eabi-strings
  build/Debug/Project_STD.elf | grep 9K10212482`，EIDE 与 make 共用 `build/Debug` 同名产物，
  注意陈旧覆盖）。
- **清理**：`make clean`（仅删除 `build/$(CONFIG)` 目录）

### 构建产物

| 产物 | 格式 | 用途 |
|---|---|---|
| `Project_STD.elf` | ELF（带调试符号） | 链接器输出，含完整符号表，调试器（probe-rs / cortex-debug）直接使用。`arm-none-eabi-size` 输出各段大小 |
| `Project_STD.hex` | Intel HEX（ASCII 文本） | 烧录文件。OpenOCD 通过 `program ... verify` 写入并回读比对。文本格式，每行含地址+数据+校验和 |
| `Project_STD.bin` | 纯二进制 | 裸二进制映像，无地址信息。适用于 IAP bootloader 直接写入 Flash |

**链接优化**：`--gc-sections` + `-ffunction-sections -fdata-sections`，未引用的函数/数据自动丢弃。`--specs=nano.specs` 链接精简版 newlib。`-u _printf_float` 强制链接 printf 浮点格式化支持（newlib-nano 默认关闭）。`-fshort-enums` 按最小宽度打包枚举类型。

## 文档地图

| 目录 | 角色 |
|---|---|
| `doc/01_显示系统` | 显示层：1-260/1-577/1-969/1-263/22-1703/**22-1665** 模组驱动、自适应字号 |
| `doc/02_LDI协议与外设接口` | LDI 协议与光敏/字库等外设接口 |
| `doc/03_重庆高速二代费显协议` | RLS 重庆协议接入记录 |
| `doc/04_青海高速费显协议` | 青海协议接入记录 |
| `doc/05_协议模块多协议兼容优化` | 多协议 RB/绑定/queue 深度权威（`01_architecture.md`） |
| `doc/06_SRAM内部分数据迁移` | 内存分区唯一权威；`04_current_memory_occupancy.md` 为占用账 |
| `doc/07_LDI与IAP配置解耦` | LDI↔IAP 配置解耦（方案 A 已落地，`app_board_net_cfg`） |
| `doc/08_协议模块接入规则` | 新协议接入权威指引（规则/串口/网络/检查清单） |
| `doc/09_山东费显协议` | 山东车道费额显示器协议接入记录 |
| `doc/10_贵州费显协议` | 贵州常规费显协议接入记录（13 命令/裁决差异表/帧头冲突纪律） |
| `doc/11_云南费显协议` | 云南常规费显协议接入记录（13 命令/24 点阵渲染/已确认决定） |
| `doc/12_缅甸费显项目适配` | 缅甸费显 8051→STD 替代方案设计（方案 B 接口板：TB62726×6 12 位 7 段灯板、3.3V→5V 电平转换、位带 WT588D、FF/E0 20 字节帧 10 命令接入；**方案设计，未实施**） |
| `doc/13_安徽费显协议` | 安徽费显协议（2026-S304）接入记录（5A/A5 帧族 12 命令；动态显示已接入 app_scroll；语音模板句 14 条；静态 SRAM 队列） |
| `doc/14_贵州治超屏协议` | 贵州治超屏协议（"TCLY" 帧族，源项目 9K23881580）接入记录（8 命令；RS485+RS232+**UDP 10011**；队列 1136B 置 CCMRAM；上电三行绿字；帧头 0x54 全槽唯一→无需互斥；**§12 = 0x20 字号与屏体几何（2026-09-14 实机 24/32 复盘 + RTT 诊断；同日晚用户裁决改为「字形可见部分裁剪绘制」，§12.6）**；**§13 = `0x40` 端口字段语义（2026-09-14 实机判定＝语义不匹配 → 用户裁决落地专用 UDP 业务口；§13.5 = 复测轮 ② 四态判别 A/B/C/D + 诊断增强；§13.5.1 = 字节序：2026-09-14 曾定案「E 类上位机大端编码」，**2026-09-15 用户裁决取代：字段改为高字节在前 BE16，不参考协议文档——写 10028 发 `27 2C`，`2C 27` 解析为 11303，收发同序**）**） |
| `doc/15_云南治超屏协议` | 云南治超屏协议（YN_1.3.0 `{` 帧族）接入记录（19 命令字；'6'/'7'/'9' 文档明示治超屏不开发→probe 快拒；**绑 RS485+RS232+TCP Server+TCP Client 四通道**——TCP 与 CQ 的 `{` 同首字节竞争以**通道掩码隔离**解决（CQ 只绑 UDP/UDP_CQ，TCP 链上不被调用），本模块**不绑 10011** → CQ 量产行为零削弱；队列 1152B 置 CCMRAM；**属 `{` 帧族 → 定义 `g_brace_proto_guard` 守卫，量产与其它 `{` 族目录二选一**（与云南常规这一对已 `ld -r` 实测报 multiple definition）；**上电默认画面已删除**——走 STD 默认显示链路；`0x47`/`0x51` 端口字段**已裁决 BE16**；**`0x49` 屏体参数持久化**（W25Qxx 独立 4KB 扇区 `capacity-12288` 12B 记录 + 上电装载）；与云南常规 `0x42` 同字节歧义；§8 = 8 条裁决结果表） |

## 硬件架构

**MCU**：STM32F407ZGTx（Cortex-M4F，168MHz，FPU，1024KB Flash，128KB SRAM + 64KB CCMRAM）

**时钟**：HSE 8MHz → PLL (M=4, N=168, P=2, Q=7) → SYSCLK 168MHz / APB1 42MHz / APB2 84MHz

**外设**：ADC1、SPI1（W25Qxx）、TIM2/3/4/7、USART1/USART3/USART6、DMA1/2、CRC、IWDG、RTC、Ethernet MAC + DP83848 PHY

## 内存布局

链接脚本：`Compiler/STM32F407XX_FLASH.ld`

```
Flash (1024KB, 起始 0x08000000)
├─ Sector 0:    0x08000000  16KB   Bootloader（独立工程，`ORIGIN 0x08000000 LENGTH 16K`）
├─ Sector 1:    0x08004000  16KB   板级系统配置 (app_board_net_cfg.c)
├─ Sector 2~5:  0x08008000  224KB  Recovery 固件（独立工程，`ORIGIN 0x08008000 LENGTH 224K`）
├─ Sector 6~11: 0x08040000  768KB  主固件 .text/.rodata/.initcall（本工程，FLASH ORIGIN 0x08040000）
└─ (Sector 11 已释放 — LDI 配置已迁移至 W25Qxx，不占内部 Flash)

SRAM (128KB, 0x20000000) — Debug 全协议构建 **128944B / 131072B（98.4%，余 2128B）**（2026-09-17 FreeRTOS 堆修复轮重采；以 06/04 §8.4 为准）
├─ .data / .bss     LwIP ram_heap + RX_POOL + PBUF_POOL 等（ETH 大户，永留 SRAM）
├─ ucHeap           **36KB** FreeRTOS heap_4（**2026-09-17 起**：23 个常驻任务栈 + TCB 与空闲/定时器内核缓冲已下沉 CCMRAM，堆只需承载「没静态化的任务」——`init_task`/`half_sec_task`/LwIP 自带任务/通道监听任务/每连接任务/惰性自检任务；启动期峰值余量 **23480B**，账见 06/04 §8）
├─ 协议 RB          RJ45 **1536** / RS485 **768** / RS232 **768**（三槽 provide）
├─ UART DMA         RS485/RS232 各 **1280**（乒乓 640×2；无 RS232_1 DMA）
├─ 帧 queue 静态    IAP2 / LDI4 / QH3 / RLS2 / AH3 / SC_ETC3 / SC_MTC3 / SC_OL3 / SD3 / GZ3 / YN3 / ANHUI3 / GZ_OL3 / **YN_OL3**（不占 ucHeap；**CQ 与 GZ_OL、YN_OL 队列例外置 CCMRAM**）
├─ app_scroll 槽位  s_slots[4] **360B** + 事件/双互斥/任务句柄 16B（2026-09-08 ④a/⑤ 起 376B；scroll_task 栈 1KB **静态落 CCMRAM**——惰性创建但全生命周期只建一次）
├─ RTT Up           1KB（2026-09-04 2KB→1KB）；W25 sec、s_dma_bounce 等
└─ _user_heap_stack newlib + MSP 预留（链接期下限 512B + 2KB，2026-09-04 由 1KB+2.5KB 收紧）

实测（Makefile GCC Debug）：**2026-09-17 FreeRTOS 堆修复轮三口径**（doc/06-04 §8.4）：`PROTO=ALL` `DISP=1_263` text **171860** / rodata **202592** / data 1672 / bss 124712 / `_user_heap_stack` 2560 / **SRAM 合计 128944B（余 2128B）** / **CCM 38332B（余 27204B）**；`PROTO=CQ` `DISP=1_263` text 158684 / rodata 201936 / data 872 / bss 121228 / SRAM 124664B（余 6408B）/ CCM 36076B（余 29460B）；`PROTO=ALL` `DISP=22_1665` text **172036** / rodata 202592 / data 1672 / bss 124712 / SRAM 128944B（余 2128B）/ CCM 39036B（该值 = 堆修复轮、模型 A 驱动 3200B）；**第二十七轮（模型 B 现行）**：同口径 text **172164** / rodata **202952** / CCM **39164**（本模组 **3328B** = `1408×2 + 256×2`）/ bss 124712 / SRAM 128944B（余 2128B）——三口径链接通过、零新增告警（3 条既有 HAL）。**相对修复前（ALL/1_263）**：text +1264 / rodata +856（判空守卫 + heap 诊断 + 内核静态任务缓冲强定义 + `heap_4` 失败请求量补丁）/ bss **−1732** / ccmram **+27168** / **SRAM 余量 400B → 2128B**。**2026-09-18 YN_OL TCP 现场修复轮（现行，doc/06-04 §8.4.1）**：`PROTO=ALL` `DISP=22_1665`（1×5）text **174640** / rodata **203152** / data 1680 / bss 124816 / ccmram 44156 / `_user_heap_stack` 2560 ⇒ **SRAM 合计 129056B（余 2016B）**；同轮 `PROTO=ALL` `DISP=1_263` 余 **2020B**、`PROTO=CQ` `DISP=1_263` 余 **6304B**——三口径链接通过、零新增告警（3 条既有 HAL）。**相对修复前（ALL/22_1665：text 175252 / rodata 205104 / bss 124776 / 余 2056B，tree `3146c4f0`）**：text **−612** / rodata **−1952** / bss **+40**（归因：LwIP 调试档位 `LEVEL_ALL` → `APP_LWIP_DBG_LEVEL` 默认 WARNING 单独贡献 text −1044 / rodata −2216；新增复位原因行 / `[disp]` 无协议承载告警 / TCP keepalive 等净增 text +432 / rodata +264 / bss +40）。烧录镜像 `tree=7df8ac20`、elf md5 `71e94c8f…`（elf 见 `.analysis/yn_ol/fix_verify/final_shipping.elf`）。**2026-09-18 第三十二轮（22_1665 CCM 自设上限删除；工作区宏值 = 8×2 = 128×32，用户第三十一轮改宏）**：`PROTO=ALL` `DISP=22_1665` `.text **175248** / .rodata **203296** / data 1656 / bss 124836 / .ccmram **58876**`（本模组 **23040B** = `1408×16 + 256×2`，**余 6660B**）/ `._user_heap_stack 2564` ⇒ **SRAM 合计 129056B（余 2016B）**；同轮 `ALL/1_263` `.text 174976 / .rodata 203040 / .ccmram 38332`（余 2016B）、`CQ/1_263` `.text 161796 / .rodata 202384 / .ccmram 36076`（余 6296B）——三口径链接通过、零新增告警（仍 3 条既有 HAL；**该两口径不编 22_1665 驱动**，其相对上条基线的 ±600B 级差异来自工作区他处在飞改动）。**（历史口径，2026-09-17 云南治超裁决轮三口径，doc/15 §6）**：`PROTO=ALL` `DISP=1_263` text **170596** / rodata **201736** / SRAM **130672B（余 400B）** / CCM **11164B**（余 54372B）；`PROTO=CQ` `DISP=1_263` SRAM 126392B（余 4680B）；`PROTO=ALL` `DISP=22_1665` text **172460** / rodata **202896** / data 1672 / SRAM 130688B（余 384B）/ CCM **10460B**（本模组 1792B；**该轮口径**——**第二十四轮现行基线**为 text **170772** / rodata **201768** / data 1672 / bss 126436 / SRAM 合计 130672B（余 **400B**）/ CCM **10460B**，tree **`35b296d8`**；多模块口径整项目 CCM 实测 2×1 = 11868B / 2×2 = 14684B / 3×2 = 17500B）——三口径链接通过、零新增告警（3 条既有 HAL）。**本模块增量（A/B）**：text +3520 / rodata +680~688 / data +0 / ccmram +1152 / bss +36（SRAM 合计 +40B / +32B / +40B）；相对上一轮 4 文件模块（text +3232 / rodata +584~592 / ccmram +1152 / bss +20）净增 text +288 / rodata +96 / SRAM +16B（删除上电画面代码 vs 新增 TCP 双通道注册 + `0x49` 持久化含 RTT 诊断）。**（历史口径，2026-09-14 复测轮四口径）**：`PROTO=ALL` `DISP=1_263` SRAM 130632B（余 440B）/ CCM **37084B**（该值为 1-263 文件 224×64 态；**工作区 1-263 当前为 1×1 实验态 → CCM 10012B**）；`PROTO=CQ` 126360B（余 4712B）；`DISP=22_1703` 两口径余 360B / 4632B（ccmram 39164B = 该文件当前 7×4 = 224×64 态；其 1×1 实验态为 11420B）。**诊断设施（`APP_DIAG_BANNER`）增量：Flash +5616B / RAM +0B**。

CCMRAM (64KB, 0x10000000, NOLOAD) — **38332B / 65536B（58.5%）**（Makefile `PROTO=ALL` `DISP=1_263`，2026-09-17 FreeRTOS 堆修复轮；余 27204B）
├─ pixel_map / hub75_buff / row_dst / 模组查表   **显存与查表**
│   1-263（224×64）：pixel_map 14336 + hub75_buff 14336 + row_dst 128 + g_bsrr 768 = **29568B**
│   22-1703（224×64，1/4 扫）：14336 + 14336 + 128 + 合并表 2848 = **31648B**（`DISP=22_1703` 口径；两模组不可同编）
│   22-1665（16×16 双色 / 每组 4 条数据线 × 8 片静态）：链段落点表 `_22_1665_chain_dst[4M][128]`（M = 模块总数；1×1 = 1024）+ pixel_map 256M + 帧状态数组（= `hub75_buff`，1B/时钟位/组，长 128M）128M + 合并写表（模型 B：8×2 槽×16 状态×组数 = 256·MODULE_COLS）= **模型 B 通式 `1408M + 256·COLS`**（1×1 = **1664B**、1×2 = **3328B**、**8×2 = 23040B**）；历史模型 A（`HUB_WIRING=1`，**已随第二十九轮删除**）= `1408M + 384`（1×1 = 1792B）；**上限口径（第三十四轮起）= 无自设预算、无编译期守卫**（17 条 `_Static_assert` 全删，约束降级为注释契约 + doc/01 §0.3；物理硬约束 `2·COLS ≤ 10` ⇒ COLS ≤ 5，越限 = 越界读 `_22_1665_lines`；历史口径「兼容 560B / 探针 8/9 态 1888B」已随设施删除，见 doc/01 附录 A）。**全片实测**（`PROTO=ALL` `DISP=22_1665`）：1×2 口径（第二十七轮）= `.ccmram 39164B`（本模组 3328）；8×2 口径（第三十二轮）= `.ccmram 58876B`（本模组 23040，余 6660B）；**工作区当前宏值 8×4 = 128×64（第三十一轮改 8×2、之后被外部改成 8×4；第三十四轮实测）= 本模组 46080B ⇒ `PROTO=ALL` 链接期溢出 16380B / `PROTO=CQ` 溢出 14124B（预存状态，与本轮断言删除无关），任何 make 口径都装不下** = 本模组 + CQ 6377 + GZ_OL 1136 + YN_OL 1152 + 工作区静态任务栈 + 对齐——**as-built 与历史轮次见 doc/01 §0 / 附录 A（A.20 = 第三十四轮）**）
├─ RTOS 任务栈 / TCB **25340B**（2026-09-17 堆修复轮新增）：23 个「只创建一次」任务经
│   `Platform/Inc/pl_task_static.h` 静态落 `.ccmram`（22×1KB + 1×512B 栈 + 23×100B StaticTask_t）；
│   另有内核空闲/定时器任务缓冲 1736B（`app_boot.c` 强定义覆盖 cmsis_os2.c 的 `__WEAK`，
│   `vApplicationGetIdleTaskMemory`/`vApplicationGetTimerTaskMemory`）——合计 **27076B**；
│   `nm` 实证 25 个静态栈全在 `0x1000_xxxx`；CPU 独占访问、不经 DMA，放置安全（doc/06-04 §8）
├─ 例外 1（2026-08-20）：CQ 协议帧队列/缓冲 **6377B**（s_cq_queue_buf 3156 + queue_cb 80 +
│   任务帧缓冲 1052 + JSON/文本缓冲 2089；SRAM 全协议构建余量不足，CPU 独占访问无 DMA）
├─ 例外 2（2026-09-14）：贵州治超（GZ_OL）队列/控制块/任务帧缓冲 **1136B**（792 + 80 + 264；
│   `PROTO=ALL` 口径 SRAM 余量仅数百 B 放不下；CPU 独占访问无 DMA）
├─ 例外 3（2026-09-17）：云南治超（YN_OL）队列/控制块/任务帧缓冲 **1152B**（801 + 80 + 267 +
│   对齐；同上理由，CPU 独占访问无 DMA）
└─ （无 ucHeap、无协议 RB、无 UART DMA）
```

**CCMRAM 特性**：零等待、D-bus 单端口；`startup.c` 清零。仅数据、不可执行；**DMA/ETH 不可达**。占用以 `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` 为准；分区宪法见同目录 `02_memory_policy.md`。多协议 RB / queue 深度见 `doc/05_协议模块多协议兼容优化/`。

## 闪存扇区职能地图

> **当前状态**：Bootloader 与 Recovery 已作为独立工程部署（workspace 内 `STM32F407-Bootloader-master` / `STM32F407-Recovery-master`）。主固件已从 `0x08040000` 链接（Sector 6~11，768K，`Compiler/STM32F407XX_FLASH.ld`），并在 `Core/Src/main.c` 中 `SCB->VTOR = FLASH_BASE | 0x40000` 重定位向量表。

| 扇区 | 地址 | 大小 | 内容 | 读写方式 |
|---|---|---|---|---|
| 0 | `0x08000000` | 16KB | Bootloader（独立工程，`ORIGIN 0x08000000 LENGTH 16K`） | 烧录时写入 |
| 1 | `0x08004000` | 16KB | 板级系统配置 (magic + update_sta + FWInfo + NetConfig + CRC32；记录 72B，NetConfig 20B 含 port/udp_port 双端口，方案 B 2026-08-21) | `app_board_net_cfg.c` 内存映射读 + 擦写 |
| 2~5 | `0x08008000`~`0x08040000` | 224KB | Recovery 固件（独立工程，`ORIGIN 0x08008000 LENGTH 224K`） | IAP 升级时写入 |
| 6~11 | `0x08040000`~`0x080FFFFF` | 768KB | 主固件 (.text/.rodata/.initcall) | 烧录时写入 |
| 11 注 | `0x080E0000` | 128KB | 已释放 — LDI 配置已迁移至 W25Qxx 最后一个 4KB 扇区（不再占内部 Flash） | — |

**外部 Flash**（W25Qxx，SPI1，~8MB）：字库数据（5 字号×4 字型×2 编码的 40 个三元组） + LDI 设备配置（最后一个 4KB 扇区），通过 `dev_storage_read` 接口访问。LDI 配置绑定在 `Application/Src/LDI/app_ldi_cfg.c`（`sw_dev_initcall` 中 `s_ldi_base = dev_storage_capacity(w25) - 4096`）。

**出厂默认网络配置（三固件统一）**：192.168.114.200/24（网关 192.168.114.1、TCP 口 9528；CQ UDP 口 20103，方案 B 2026-08-21）。Recovery 上电 `MX_LWIP_Init` 直读 Sector1.net_cfg 配 netif（空/无效回退统一默认）；主固件 `ldi_ctx_init` 上电同步三分支：**Sector1 为网络配置真源、W25 为恢复镜像**（Sector1 有效且 net_cfg 合法即优先采纳，含升级中间态，`update_sta` 不参与分支判定（2026-08-21 修订）；Sector1 空/损坏/记录无效或 net_cfg 非法时以 W25 自愈回写；皆空用统一出厂默认，详见 `doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md` §11/§12）。**netif 应用由中立模块 `app_net_boot`（`app_net_boot_apply`）统一执行**——`ldi_ctx_init` 只做自愈与配置装载（2026-08-21 解耦）；Sector1 空/损坏/非法时 `app_net_boot` accept_write 写本构建默认落盘并应用（两口径默认均 port=9528/udp_port=20103，IP 各自口径：CQ 192.168.1.5、dev 114.200，详见 §14/§15）。**所有改 IP 接口（LDI 0AH / IAP 4B02 / Recovery IAP）统一重启生效**（运行时不即时改 netif）；Bootloader 对 Sector1 损坏态（magic 不匹配或 CRC 错）自动重建出厂记录自愈（详见 §12）。UDP 发现口 10011 固定（宏 `LDI_DISCOVERY_PORT`）。**搜索/上报端口字段语义**：LDI 12H 应答 port 与 IAP 0x01 上报 port 均为配置功能端口（TCP 业务口，出厂默认 9528，宏 `LDI_DEFAULT_CONFIG_PORT`），搜索/上报的传输渠道才是 UDP 10011——勿混淆（2026-08-18 修复 12H 误报 10011 的污染循环）。

## 软件分层架构

依赖方向严格单向（上层可见下层，反之不可）：

```
┌──────────────────────────────────────────────────────────────────┐
│  Application/  (app_*)                                           │
│  业务逻辑：boot编排、协议处理(IAP/LDI/AH_MQTT)、渲染引擎、       │
│  传感器管理、网络通道(UDP/TCP/MQTT/RS232/RS485任务)              │
│                                                                  │
│  依赖：Device/ + Kernel/ + Platform/ (不直接碰 HAL 地址)         │
├──────────────────────────────────────────────────────────────────┤
│  Device/  (dev_*)                                                │
│  设备抽象：OCP 虚表封装硬件差异                                  │
│  Display/  Storage/  Comm/  Network/  IO/  Config/               │
│                                                                  │
│  依赖：Platform/ + Kernel/ (通过 pl_* 接口操作硬件)              │
├──────────────────────────────────────────────────────────────────┤
│  Kernel/                                                         │
│  纯软件工具：ring_buffer、initcall、container_of、text_cvt、     │
│  bit_utils、crc_utils、bcc_utils                                 │
│                                                                  │
│  依赖：无（零硬件依赖，仅 stdint/stddef/stdbool）                │
├──────────────────────────────────────────────────────────────────┤
│  Platform/  (pl_*)                                               │
│  HAL 薄封装：不透明句柄 (void*)，HAL 类型对外不可见              │
│  pl_spi  pl_tim  pl_uart  pl_gpio  pl_hub75  pl_eth  pl_net      │
│  pl_flash  pl_crc  pl_rtc  pl_iwdg  pl_dma  pl_dwt  pl_sys       │
│                                                                  │
│  依赖：Core/ + HAL 库 (仅 .c 文件 include Core 头文件)           │
├──────────────────────────────────────────────────────────────────┤
│  Core/                                                           │
│  STM32CubeMX 生成：MX_xxx_Init()、ISR 声明、HAL 配置             │
│  main.c  system_stm32f4xx.c  stm32f4xx_it.c  FreeRTOSConfig.h    │
│                                                                  │
│  依赖：HAL 库 + CMSIS                                            │
├──────────────────────────────────────────────────────────────────┤
│  Compiler/                                                       │
│  startup.c (C 语言 Reset_Handler + 自定义向量表)                 │
│  STM32F407XX_FLASH.ld (链接脚本，含 initcall 段定义)             │
└──────────────────────────────────────────────────────────────────┘
```

### Platform 层内部依赖

```
pl_sys (SystemClock_Config, delay, reset)
 ├─ pl_dma     (DMA 流初始化，需在 SPI/UART 之前)
 ├─ pl_gpio    (GPIO 时钟，几乎所有模块依赖)
 ├─ pl_tim     (TIM2/3/4/7)
 ├─ pl_spi     (SPI1，依赖 DMA)
 ├─ pl_uart    (USART1/3/6，依赖 DMA)
 ├─ pl_crc     (硬件 CRC)
 ├─ pl_iwdg    (独立看门狗)
 ├─ pl_rtc     (RTC 备份寄存器)
 ├─ pl_adc     (ADC1)
 ├─ pl_exti    (外部中断)
 ├─ pl_hub75   (HUB75 GPIO 位拆裂)
 ├─ pl_flash   (内部 Flash 编程)
 ├─ pl_dwt     (DWT 周期计数器)
 ├─ pl_rtt     (SEGGER RTT)
 ├─ pl_eth     (ETH MAC + MDIO)
 └─ pl_net     (LwIP 初始化，依赖 pl_eth)
```

各模块用不透明句柄（`typedef void *pl_xxx_handle_t`），Device 层只传句柄、不感知 HAL 类型。

## 启动流程与 initcall

### 完整启动序列

```
硬件上电 → Reset_Handler (Compiler/startup.c)
  ├─ .data 拷贝 (Flash→SRAM), .bss 清零, CCMRAM 清零
  ├─ SystemInit() — FPU 使能（VTOR 在 main() 中由 `SCB->VTOR` 重定位到 0x08040000）
  └─ main() (Core/Src/main.c)
       ├─ SCB->VTOR = FLASH_BASE | 0x40000  ← 向量表重定位（主固件入口 0x08040000）
       ├─ HAL_Init()                     ← HAL 库基础
       ├─ SystemClock_Config()           ← 168MHz
       ├─ initcall_run(__hw_initcall)    ← RTOS 前硬件初始化
       └─ app_boot()                     ← RTOS 编排
            ├─ osKernelInitialize()
            ├─ osThreadNew(init_task, prio=High, stack=512×4)
            └─ osKernelStart()
                 ├─ [FreeRTOS 接管: SysTick→PendSV 任务切换]
                 └─ init_task:
                      ├─ dev_eth_start()          ← LwIP + netif
                      ├─ sw_board_init()          ← initcall_run(__sw_initcall)
                      ├─ app_board_net_cfg_fw_version_update()  ← PROGRAM_CODE 落库 Sector1（空扇区 net_cfg 置 0）
                      ├─ app_net_boot_apply()     ← Sector1 net_cfg → netif + TCP Server 口（两口径统一应用；空/损坏写默认落盘）
                      ├─ app_splash_display()     ← 开机画面 (FW/MD 版本, 5s)
                      ├─ osThreadNew(half_sec_task)
                      ├─ app_tcp_server_start() / app_tcp_client_start()  （两口径无条件启动；Client 默认 0.0.0.0:0 未配置不连）
                      │   / app_udp_start() / app_udp_cq_start() / app_udp_gzol_start()
                      │     （后两者：CQ 业务口 udp_port / 贵州治超专用口 net_cfg.port，两口径无条件启动）
                      ├─ app_rs485_start() / app_rs232_start()  (app_rs232_1_start 注释)
                      ├─ app_default_display()    ← 自注册默认显示界面（协议注册回调则用之，否则默认欢迎画面）
                      └─ osThreadExit()           ← 自我销毁
```

### initcall 初始化顺序表

**hw_initcall (RTOS 前，main() 中执行)**：

| 顺序 | 层级 | 函数 | 文件 | 职责 |
|---|---|---|---|---|
| 0-pre | — | （空） | — | 预留 |
| 1-pl | pl | `pl_dma_init` | `pl_dma.c` | DMA 流初始化 |
| 1-pl | pl | `pl_gpio_init` | `pl_gpio.c` | GPIO 时钟使能 |
| 1-pl | pl | `pl_spi_init` | `pl_spi.c` | SPI1 初始化 |
| 1-pl | pl | `pl_tim_init` | `pl_tim.c` | TIM2/3/4/7 初始化 |
| 1-pl | pl | `pl_uart_init` | `pl_uart.c` | USART1/3/6 初始化 |
| 1-pl | pl | `pl_crc_init` | `pl_crc.c` | 硬件 CRC |
| 1-pl | pl | `pl_iwdg_init` | `pl_iwdg.c` | 看门狗 |
| 1-pl | pl | `pl_rtc_init` | `pl_rtc.c` | RTC |
| 1-pl | pl | `pl_adc_init` | `pl_adc.c` | ADC1 |
| 1-pl | pl | `pl_exti_init` | `pl_exti.c` | 外部中断 |
| 1-pl | pl | `pl_hub75_init` | `pl_hub75.c` | HUB75 GPIO |
| 1-pl | pl | `pl_rtt_init` | `pl_rtt.c` | SEGGER RTT |
| 2-dev | dev | `dev_display_init` | `dev_display.c` | HUB75 引脚 + DBG 冻结 |
| 2-dev | dev | `dev_display_1_577_init` | `dev_display_1_577.c` | 1-577 模组实例注册（备用） |
| 2-dev | dev | `dev_display_1_260_init` | `dev_display_1_260.c` | 1-260 模组实例注册（备用，Makefile/EIDE 均已排除） |
| 2-dev | dev | `dev_display_1_969_init` | `dev_display_1_969.c` | 1-969 模组实例注册（备用） |
| 2-dev | dev | `dev_display_1_263_init` | `dev_display_1_263.c` | 1-263 模组实例注册（P6 32x32；**224×64** = 7 行 × 32 / 2 列 × 32；Makefile 默认口径 `DISP=1_263`；**EIDE Debug 当前排除本文件、编 22-1665**） |
| 2-dev | dev | `dev_display_22_1703_init` | `dev_display_22_1703.c` | 22-1703 模组实例注册（P10 32x16 / 料号 2200001703；1/4 扫描，224×64，合并写 BSRR 档位 D+；`make DISP=22_1703` 选编，与 1-263 二选一） |
| 2-dev | dev | `dev_display_22_1665_init` | `dev_display_22_1665.c` | **22-1665 模组实例注册** —— **16×16 红绿双色 / 静态单扫**（每组 4 条数据线 × 每模块每线 8 片 MBI5034B = 每段 128 位，共 512 LED）；数据线表 `_22_1665_lines[5][4]` = `{线名, 数据脚, 颜色角色, 半屏}`（**组 × 4 行**，组 g = HUB75 通道对 `2g/2g+1` 的 R/G 脚；**组 0 = R1/G1/R2/G2（PG9/PG10/PG15/PB6）**、**组 1 = R3/G3/R4/G4（PB8/PB9/PE1/PE2）**，B1/B2/A/B/C/D 六根实测无链）；**接线模型 = 模型 B（每列一口独立线，各组并行、每帧 128×MODULE_ROWS 时钟；第二十九轮起为唯一形态，模型 A 同线级联路径已删）**；**链段 = 模块 × 线（4M 段，区域自动推导）**，落点/级联规则见 doc/01 §0.3；几何宏 `_22_1665_MODULE_ROWS/COLS`（**改分辨率只改这两个**）+ 派生 `SCREEN_ROWS/COLS`/`FRAME_BITS`（= 128 × 模块数）；本模组 CCM（模型 B）**`1408M + 256·COLS`**（1×1 = 1664B、1×2 = 3328B、1×5 = 8320B、**8×2 = 23040B**；**第三十二轮删自设 16KB 上限，只剩「≤ 整片 CCMRAM 区域 64KB」物理守卫，整片占用链接期兜底**）；**驱动内无 RTT / 无探针**（标定工具归档 `archive/prev_round/`）；**第二十八轮：格式对齐 22_1703（591 行、`MODULE_CODE`、`_22_1665_bsrr_tab`），对象 15 段 md5 全同 = 行为零变化**；**第二十九轮：生产化收口（468 行 / 三化石开关已删 / 只留模型 B；归档 vs 现行段级 + 反汇编零差异）**；**第三十轮：文件头瘦身（46 → 16 行、全文 468 → 432 行）+ 几何宏改普通 `#define`（不可 `-D`，换口径改文件 / 宿主走源码替换）；改动前归档 vs 现行 22 段逐字节 + 反汇编 0 差异**；**第三十二轮：删自设 CCM 上限（441 行；改动前归档 `archive/round32/` vs 现行 22 段逐字节 + 反汇编 0 差异）；工作区宏值 8×2 实测编过**；**第三十四轮：按用户裁决取消编译期防御（17 条 `_Static_assert` 全删 + 仅断言用的 2 个派生宏）+ 精简注释（441 → 372 行、注释字符 −21%；归档 `archive/round34/` vs 现行 1×5/8×4 两口径 20 段逐字节 + 反汇编 0 差异 = 5/5）；约束降级为注释契约，工作区 8×4 在 `PROTO=ALL`/`CQ` 下链接期溢出（16380B/14124B，预存）** |
| 2-dev | dev | `dev_display_p20_init` | `dev_display_p20.c` | P20 模组实例注册（未编入任何构建） |
| 2-dev | dev | `dev_w25qxx_init` | `dev_w25qxx.c` | SPI Flash JEDEC 识别 |
| 2-dev | dev | `dev_eth_init` | `dev_eth.c` | ETH MAC + DP83848 PHY |
| 2-dev | dev | `dev_rs485_init` | `dev_rs485.c` | RS485 RE 方向回调注入 |
| 2-dev | dev | `_app_board_net_cfg_storage_init` | `Application/Src/Config/app_board_net_cfg.c` | 内部 Flash ops 绑定 (Sector 1) |
| 2-dev | dev | `dev_io_ctrl_init` | `dev_io_ctrl.c` | 车道灯/闪光灯 GPIO 初始化 |
| 2-dev | dev | `dev_key_init` | `dev_key.c` | EXTI 中断回调注册 (SW1~SW3, TEST) |
| 3-post | — | （空） | — | 预留 |

**sw_initcall (RTOS 后，init_task → sw_board_init 中执行)**：

| 顺序 | 层级 | 函数 | 文件 | 职责 |
|---|---|---|---|---|
| 0-pre | — | （空） | — | 预留 |
| 2-dev | dev | `dev_display_start` | `dev_display.c` | 创建 scan_task + 启动 TIM3/4 |
| 2-dev | dev | `dev_key_sw_init` | `dev_key.c` | 创建 TEST 按键信号量 |
| 2-dev | dev | `_app_flash_ldi_storage_init` | `Application/Src/LDI/app_ldi_cfg.c` | LDI 配置 W25Qxx 地址计算 |
| 3-app | app | `app_dispatch_init` | `app_dispatch.c` | 创建 ch_queue + frame_dispatch_task |
| 3-app | app | `_render_init` | `app_render.c` | 绑定 display + storage 句柄 |
| 3-app | app | `_app_key_init` | `app_key.c` | 创建 key_poll_task (20ms) |
| 3-app | app | `app_light_sensor_init` | `app_light_sensor.c` | 创建光传感器自动调节任务 |
| 3-app | app | `iap_module_init` | `Application/Src/IAP/app_iap.c` | IAP 协议自注册（UDP） |
| 3-app | app | `ldi_module_init` | `Application/Src/LDI/app_ldi.c` | LDI 协议自注册（TCP/UDP 三通道） |
| 3-app | app | `qh_proto_init` | `Application/Src/ProtocolParser_QingHai/app_qh_proto.c` | 青海协议自注册（RS485+RS232） |
| 3-app | app | `sc_etc_proto_init` | `Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto.c` | 四川 ETC 费显协议自注册（RS485+RS232） |
| 3-app | app | `sc_mtc_proto_init` | `Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto.c` | 四川 MTC 费显协议自注册（RS485+RS232） |
| 3-app | app | `sc_ol_proto_init` | `Application/Src/ProtocolParser_SiChuang_Overload/app_sc_ol_proto.c` | 四川治超屏协议自注册（RS485+RS232） |
| 3-app | app | `sd_proto_init` | `Application/Src/ProtocolParser_ShanDong/app_sd_proto.c` | 山东费显协议自注册（RS485+RS232） |
| 3-app | app | `gz_proto_init` | `Application/Src/ProtocolParser_GuiZhou/app_gz_proto.c` | 贵州费显协议自注册（RS485+RS232） |
| 3-app | app | `yn_proto_init` | `Application/Src/ProtocolParser_YunNan/app_yn_proto.c` | 云南费显协议自注册（RS485+RS232；不注册默认显示——使用固件默认显示，用户决定 8） |
| 3-app | app | `yn_ol_proto_init` | `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c` | **云南治超屏协议**自注册（**RS485 + RS232 + TCP Server + TCP Client 四通道四 mask 共用一队列**；`{` 帧族 → 定义 `g_brace_proto_guard` 守卫，量产与其它 `{` 族目录二选一；队列 1152B 置 CCMRAM；**上电默认画面不实现**（原 `app_yn_ol_proto_default.c` 已删除，走 STD 默认显示链路）；`0x49` 屏体参数上电从 W25Qxx 独立扇区装载） |
| 3-app | app | `anhui_proto_init` | `Application/Src/ProtocolParser_Anhui/app_anhui_proto.c` | 安徽费显协议自注册（RS485+RS232；5A/A5 帧族，动态显示 0x86~0x89 经 app_scroll 循环滚动） |
| 3-app | app | `cq_proto_init` | `Application/Src/ProtocolParser_ChongQing/app_cq_proto.c` | 重庆CQ协议自注册（UDP_CQ 业务口 + UDP 搜索口双 mask；cJSON 钩子换绑 FreeRTOS 堆；网络配置应用移交 app_net_boot） |
| 3-app | app | `app_uart_baud_init` | `Application/Src/app_uart_baud.c` | DIP1 波特率选择（RS232+RS485 同步切换） |
| 3-app | app | `gz_ol_proto_init` | `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c` | 贵州治超屏协议自注册（RS485+RS232；"TCLY" 帧族 8 命令；队列 1136B 置 CCMRAM；上电三行绿字由 `app_gz_ol_proto_default.c` 注册） |
| 3-app | app | `rls_module_init` | `Application/Src/RLS/app_rls.c` | RLS 协议自注册（RS485） |
| 3-app | app | `_factory_test_init` | `Application/Src/app_factory_test.c` | 出厂检测 monitor |
| 3-app | app | `ah_mqtt_module_init` | `Application/Src/AH_MQTT/ah_mqtt.c` | 已注释，未启用 |
| 4-post | — | （空） | — | 预留 |

### initcall 实现原理

链接脚本中定义两个特殊段：
```
.hw_initcall : { KEEP(*(SORT(.hw_initcall.0))) ... KEEP(*(SORT(.hw_initcall.3))) }
.sw_initcall : { KEEP(*(SORT(.sw_initcall.0))) ... KEEP(*(SORT(.sw_initcall.4))) }
```

每个 `*_initcall(fn)` 宏生成一个 `initcall_entry_t` 常量放入对应 section。`initcall_run(start, end)` 顺序遍历调用。同层内按**链接顺序（源码收录序）**执行——`SORT()` 对同名 section 退化为 .o 链接顺序，Makefile/EIDE 源码收录序即调用序（`app_dispatch_init` 靠收录序在协议之前）。

## 中断体系

### 中断源与分派路径

```
外设硬件中断
├─ TIM3_IRQHandler           (pl_tim.c)
│   └─ HAL_TIM_IRQHandler → HAL_TIM_PeriodElapsedCallback
│       └─ osEventFlagsSet(s_scan_evt) → 唤醒 scan_task
│
├─ TIM4_IRQHandler           (pl_tim.c)
│   └─ HAL_TIM_IRQHandler → HAL_TIM_PeriodElapsedCallback
│       └─ PWM 亮度：pl_hub75_oe_set(pwm_cnt >= light_level)
│
├─ TIM7_IRQHandler           (pl_tim.c)
│   └─ HAL_TIM_IRQHandler → HAL_TIM_PeriodElapsedCallback
│       └─ HAL_IncTick() (FreeRTOS 时基，1ms)
│
├─ USART1_IRQHandler         (pl_uart.c)
│   └─ HAL_UART_IRQHandler + uart_idle_handle
│       └─ DMA 空闲中断 → 计算接收长度 → rx_cb→
│           osMessageQueuePut(rx_queue, &len) → 唤醒 rs485_task / rs232_task
│
├─ USART3_IRQHandler / USART6_IRQHandler  (同上)
│
├─ SPI1_IRQHandler           (pl_spi.c)
│   └─ HAL_SPI_IRQHandler → HAL_SPI_TxRxCpltCallback / HAL_SPI_RxCpltCallback
│       └─ rx_cplt_cb(s_ok=true) → dev_w25qxx DMA 完成通知
│
├─ DMA1_Stream1_IRQHandler   (pl_uart.c, USART3 RX DMA)
├─ DMA2_Stream1_IRQHandler   (pl_uart.c, USART6 RX DMA — 句柄已初始化但无通道启动接收)
├─ DMA2_Stream2_IRQHandler   (pl_uart.c, USART1 RX DMA)
│
├─ ETH_IRQHandler            (pl_eth.c, 见下)
│
├─ HardFault_Handler         (stm32f4xx_it.c)
│   └─ naked asm → hard_fault_handler_c → 现场保存 (R0-R3,R12,LR,PC,xPSR,CFSR,BFAR)
│
├─ SysTick_Handler           (FreeRTOS 内部, CMSIS-RTOS V2 封装)
├─ SVC_Handler  → vPortSVCHandler    (FreeRTOS 内核, 启动第一个任务)
└─ PendSV_Handler → xPortPendSVHandler (FreeRTOS 内核, 任务上下文切换)
```

**设计原则**：
- 所有外设 ISR 内聚在 Platform 层 .c 文件中，不分散在 `stm32f4xx_it.c`
- ISR 中只做最小操作（标记、通知），耗时逻辑全部在 RTOS 任务中完成
- 中断优先级：`configMAX_SYSCALL_INTERRUPT_PRIORITY = 5`，FreeRTOS API 仅可在优先级 ≥5 的 ISR 中调用

### 中断→任务解耦的三级流水线

```
[裸机 ISR]                [RTOS 信号量/队列]         [RTOS 任务]
─────────────────────────────────────────────────────────────────
TIM3 行同步    ──→  osEventFlagsSet        ──→  scan_task (Realtime)
TIM4 PWM       (ISR 内直接完成，不唤醒任务)
UART 空闲中断   ──→  osMessageQueuePut       ──→  rs485_task / rs232_task
                                                     └─→ app_channel_dispatch
                                                           └─→ osMessageQueuePut(ch_queue)
                                                                 ──→ frame_dispatch_task
                                                                       └─→ osMessageQueuePut(frame_queue)
                                                                             ──→ 协议处理任务
SPI DMA 完成   ──→  volatile s_ok=true       ──→  dev_w25qxx _read 轮询 (osDelay 轮询)
ETH 收包       ──→  osSemaphoreRelease       ──→  ethernetif_input
ETH 链路状态   ──→  pl_net_link_listener     ──→  UDP/TCP/MQTT 通道任务重建连接
```

**关键模式**：
- **Event Flags**：`scan_task` 用 `osEventFlagsWait` 等待 TIM3 行同步触发。ISR 中使用 `osEventFlagsSet`（`configUSE_OS2_EVENTFLAGS_FROM_ISR=1` 使能 ISR 安全调用）
- **Message Queue**：`rs485_task` / `rs232_task`、`frame_dispatch_task`、协议处理任务逐级通过队列传递数据指针
- **Semaphore**：UDP/TCP 通道用信号量协调连接/断开生命周期（链路断开→信号量释放→任务重建连接）
- **事件标志等待**：W25Qxx SPI 半双工 DMA 读用 `osEventFlagsWait(s_evt)` 等 DMA 完成回调置位（2026-09-08 起；RTOS 未就绪的单线程早期路径仍 `while(!s_ok) osDelay(1)` 兜底）

## RTOS 配置

`Core/Inc/FreeRTOSConfig.h`，FreeRTOS v10.3.1 + CMSIS-RTOS V2 封装：

| 参数 | 值 | 说明 |
|---|---|---|
| `configTICK_RATE_HZ` | 1000 | 1ms 时基（TIM7） |
| `configMAX_PRIORITIES` | 56 | 优先级范围 0-55 |
| `configTOTAL_HEAP_SIZE` | **36KB** | heap_4 static `ucHeap` → SRAM `.bss`；水位见 06/04 |
| `configAPPLICATION_ALLOCATED_HEAP` | **0** | 不用 CCM 堆文件；禁止堆指针作 DMA（**堆不进 CCM 的政策未变**——2026-09-17 下沉的是任务栈/TCB，不是堆） |
| `configSUPPORT_STATIC_ALLOCATION` | **1** | **关键**：空闲任务与定时器服务任务走 `xTaskCreateStatic`（不再占 ucHeap）；应用侧 23 个任务经 `Platform/Inc/pl_task_static.h` 把「栈 + TCB」静态落 `.ccmram`（`nm` 实证 25 个静态栈全在 `0x1000_xxxx`，`ucHeap` 仍 `0x20015c3c`） |
| `configSUPPORT_DYNAMIC_ALLOCATION` | **1** | 动态创建仍可用（每连接任务、惰性任务）；**创建点必须 `pl_task_create_checked` 判空**（`Platform/Inc/pl_task_guard.h`） |
| `configUSE_TIMERS` | **1** | 定时器服务任务存在（`configTIMER_TASK_STACK_DEPTH=256` words ⇒ 1KB，缓冲在 `app_boot.c` 强定义的 `vApplicationGetTimerTaskMemory` → `.ccmram`）；**全项目无 `osTimerNew`/`xTimerCreate` 调用点**（服务任务空转，可评估 `configUSE_TIMERS=0` 再省 3KB CCM + 240B SRAM） |
| `configUSE_TRACE_FACILITY` | **1** | 连同 `configRECORD_STACK_HIGH_ADDRESS` 提供任务枚举/栈高水位；`INCLUDE_uxTaskGetStackHighWaterMark=1` |
| `configMINIMAL_STACK_SIZE` | 128 words (512B) | 最小任务栈 |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 | 栈溢出检测（检查栈顶标记；对静态栈同样生效） |
| `configUSE_MUTEXES` | 1 | 互斥锁（含优先级继承） |
| `configUSE_COUNTING_SEMAPHORES` | 1 | 计数信号量 |
| `configUSE_OS2_EVENTFLAGS_FROM_ISR` | 1 | ISR 中可操作 EventFlags |
| `configGENERATE_RUN_TIME_STATS` | 1 | 运行时统计（时钟=DWT 周期计数器） |
| `configMAX_SYSCALL_INTERRUPT_PRIORITY` | 5 | 可调用 FreeRTOS API 的最高中断优先级 |
| `configKERNEL_INTERRUPT_PRIORITY` | 15 | 内核中断最低优先级 |

### FreeRTOS 堆（ucHeap）预算与任务栈 CCMRAM 下沉（2026-09-17）

**背景**：全协议 dev 构建启动期「任务栈 + TCB + 动态对象」需求 38032B > 可用 36856B
（赤字 1176B）→ 现场在 `app_rs485_start` 的 `xTaskCreate` 处命中
`vApplicationMallocFailedHook`（RS485/RS232 随之无声失效）。

**修复**：23 个「只创建一次」的任务栈 + `StaticTask_t` 静态落 `.ccmram`
（`Platform/Inc/pl_task_static.h` 的 `PL_TASK_STATIC_STORAGE/ATTR` → `xTaskCreateStatic`），
外加空闲/定时器任务缓冲强定义覆盖到 CCMRAM ⇒ **ucHeap 释放 25800B，CCM 用 27076B**，
**启动期峰值余量 23480B**（目标 ≥2.5KB）。**堆仍是 SRAM 36KB**（政策未变）。

* **永久账本（各任务/对象/尺寸/创建者/静态或动态/合计）在
  `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` §8**；复算脚本
  `.analysis/heap/heap_ledger.py`（`--markdown` / `--ab` / `--eide`），三口径复编
  `.analysis/heap/verify_builds.sh`。
* **诊断**：开机横幅 `[diag] heap at banner free=… min=…` + 每个通道启动后
  `[diag] heap after <chan>_start …`（`xPortGetFreeHeapSize` /
  `xPortGetMinimumEverFreeHeapSize`）；`vApplicationMallocFailedHook` 打
  `request=/free=/min=` 后关中断保留现场（`heap_4.c` 的诊断补丁导出 `xLastFailedAllocSize`）。
* **纪律**：新增任务/动态对象前先跑脚本 + 看 RTT `min`；能静态化的（只创建一次）优先静态化，
  每连接任务保持动态但必须判空。

## 任务清单与同步机制

### 任务栈大小设计考量

| 任务 | 栈 | 优先级 | 创建者 | 职责 | 栈设计依据 |
|---|---|---|---|---|---|
| `scan_task` | 256×4=1KB | Realtime(最高) | `dev_display_start` | 行扫描输出 | 扫屏路径无大栈帧（原 2KB 偏大），仅 event wait + ops 调用 |
| `init_task` | 512×4=2KB | High | `app_boot` | 初始化后退出 | `dev_eth_start`→`sw_board_init`→渲染测试，含 printf 和栈数组（`font_buf[512]`、`text_buf[256]`），曾因溢触发 HardFault，从 256×4 扩容 |
| `frame_dispatch_task` | 256×4=1KB | Normal | `app_dispatch_init` | 帧分发引擎 | 核心调度循环，含 `frame_msg_t`（~1052B），无深层嵌套 |
| `iap_handle_task` | 256×4=1KB | Normal | `app_iap` | IAP 帧处理 | CRC32 校验 + cmd 分派（帧缓冲 static；原 2KB 偏大） |
| `ldi_handle_task` | 256×4=1KB | Normal | `app_ldi` | LDI 帧处理 | 显示内容解析与更新（帧缓冲 static；原 2KB 偏大） |
| `ldi_timer_task` | 256×4=1KB | Normal | `app_ldi` | LDI 定时任务 | 周期性状态上报（原 2KB 偏大） |
| `rs485_task` | 256×4=1KB | Normal | `app_rs485` | RS485 接收循环 | `osMessageQueueGet` → `app_channel_dispatch` |
| `rs232_task` | 256×4=1KB | Normal | `app_rs232` | RS232 接收循环 | 同构 rs485_task |
| `half_sec_task` | 128×4=512B | Low | `init_task` | 500ms 喂狗+LED | 最小合法栈，仅 GPIO+RTC API |
| `scroll_task` | 256×4=1KB | Normal | `app_scroll`（惰性创建） | 动态滚动渲染 | 2ms 节拍循环；仅字形位图（栈内 ≤128B）+ 槽位小局部；空闲事件标志休眠零 CPU |
| 各网络通道任务 | 256×4=1KB | Normal | 各模块 | UDP/TCP 收发包 | LwIP netconn API 调用，不含大局部变量 |
| `ethernetif_input` | 256×4=1KB | — | `pl_net.c` | LwIP 收包线程 | LwIP 内部调用 |
| `ethernet_link_thread` | 256×4=1KB | — | `pl_net.c` | 链路状态监控 | 轮询 PHY 状态 |

**栈/TCB 归属（2026-09-17 起，见 `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` §8）**：
**静态分配、落 `.ccmram`、不占 ucHeap** 的共 **23 个**任务 —— `scan_task`、`factory_monitor_task`、
`frame_dispatch_task`、`scroll_task`、`light_sensor_task`、`iap_handle_task`、`ldi_handle_task`、
`ldi_timer_task`、`cq_proto_handle_task`、`cq_proto_timer_task`、`rls_handle_task`、
`qh/sc_etc/sc_mtc/sc_ol/sd/gz/gz_ol/yn/yn_ol/anhui` 处理任务、`rs485_task`、`rs232_task`
（机制 `Platform/Inc/pl_task_static.h`；内核的空闲/定时器任务同批下沉）。
**仍从 ucHeap 动态分配** 的：`init_task`（启动末自我退出并归还）、`half_sec_task`、`tcpip_thread`、
`EthIf`、`EthLink`、通道监听任务（`tcp_server_task` / `tcp_client_task` / `udp_task` /
`udp_cq_task` / `udp_gzol_task`）、每连接任务（`udp_connect_task` / `udp_cq_connect_task` /
`udp_gzol_connect_task` / `tcp_client_conn_task`，退出即归还）、`yn_selftest_task` /
`yn_ol_selftest_task`。**判据**：全生命周期只创建一次 ⇒ 可静态化；会被反复创建/退出 ⇒ 保持动态
（复用静态栈会栈踩踏），但创建点必须 `pl_task_create_checked()` 判空报警。

**栈大小设计原则**：CMSIS-RTOS V2 栈单位为 word（4 字节）。数字偏大：
- 512B（128 words）= CMSIS 最小栈，仅用 API 无局部数组
- 1KB（256 words）= 标准栈，含少量局部变量
- 2KB（512 words）= 大栈，含 `frame_msg_t`（~1052B）、CRC 计算、printf 等

### 任务间同步机制全景

```
init_task ──────────────────────────────────────────────────────→ (osThreadExit 自我销毁)
  ├─ dev_eth_start → pl_net_init → osThreadNew(ethernetif_input)
  │                               → osThreadNew(ethernet_link_thread)
  └─ sw_board_init → app_dispatch_init → osThreadNew(frame_dispatch_task)
                    → dev_display_start → osThreadNew(scan_task)

half_sec_task:  osDelay(500) 循环 ──→ pl_iwdg_refresh / pl_gpio_write

scan_task:
  osEventFlagsWait(s_scan_evt) ←── TIM3 ISR: osEventFlagsSet
  osKernelLock + NVIC_DisableIRQ(TIM4)  (OE/LAT 原子窗口)

frame_dispatch_task:
  osMessageQueueGet(g_ch_queue) ←── rs485_task / rs232_task / udp_connect_task / ...
  rb_lock(rb) → probe → rb_read → osMessageQueuePut(frame_queue[i])
  （取出的 channel_t * 先经 app_channel_get 回验，脏通知丢弃）
  
协议处理任务 (iap/ldi/ah_mqtt):
  osMessageQueueGet(frame_queue[i]) → 处理 → channel_send

UART 通道任务 (rs485_task / rs232_task):
  osMessageQueueGet(rx_queue, {block,offset,len}) ←── UART ISR 空闲中断
  app_channel_dispatch → rb_write → osMessageQueuePut(ch_queue)

网络通道任务 (UDP/TCP):
  osSemaphoreAcquire(sem) ←── pl_net_link_listener (链路断开)
  netconn_recv → app_channel_dispatch → rb_write → osMessageQueuePut(ch_queue)
```

## OCP 虚表模式

项目中所有设备驱动和通道使用统一模式：

```c
// 1. 基类第一个成员必须是 ops 指针
struct dev_display { const dev_display_ops_t *ops; /* 通用参数 */ };
struct dev_storage  { const dev_storage_ops_t  *ops; uint32_t capacity; };
struct channel      { uint8_t ch_id, state; const ch_ops_t *ops; };

// 2. 派生类将基类作为第一个成员，统一命名为 me
typedef struct { dev_display_t  me; } dev_display_1_577_t;
typedef struct { dev_storage_t  me; uint32_t base_addr, sector; } dev_flash_int_t;
typedef struct { channel_t      me; pl_uart_handle_t uart; ... } rs485_ch_t;  // app_rs485.c（rs232_ch_t 同构）
typedef struct { dev_w25qxx_t   me; pl_spi_handle_t spi; ... } dev_w25qxx_inner; // (注：w25qxx直接在结构内)

// 3. container_of 向上转型（Kernel/Inc/container_of.h）
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
```

四个核心虚表：

| 虚表 | 定义位置 | 方法签名 | 实现者 |
|---|---|---|---|
| `dev_display_ops` | `dev_display.h` | `prepare(dev)` / `scan(dev,line)` / `set_row(row)` | `dev_display_1_577.c` / `dev_display_1_260.c` / `dev_display_1_969.c` |
| `dev_storage_ops` | `dev_storage.h` | `init/read/write/erase/capacity(dev,...)` | `dev_w25qxx.c` / `dev_flash_int.c` |
| `ch_ops` | `app_dispatch.h` | `send(ch, data, len)` | `app_rs485.c` / `app_rs232.c` / `app_udp.c` / TCP/MQTT 通道 |
| `proto_probe_fn_t` | `app_dispatch.h` | `probe(ch, rb, &total_len, &aux)` | `app_iap.c` / `app_ldi.c` / `app_qh_proto.c` / `app_rls.c` / `ah_mqtt.c` |

## Display 子系统 (`Device/Display/`)

### 基类 (`dev_display.h/.c`)

```c
struct dev_display {
    const dev_display_ops_t *ops;
    // 模组参数
    uint8_t module_rows, module_cols, channels_per_module;
    uint8_t modules_per_row, modules_per_col, scan_lines;
    // 派生参数（由模组参数计算）
    uint16_t screen_rows, screen_cols;
    uint8_t  total_channels;
    uint16_t channel_pixels, scan_line_pixels, buffer_size;
    // CCMRAM 缓冲区（派生实例静态分配）
    uint8_t *pixel_map, *hub75_buff;
    // 运行时
    volatile uint8_t light_level;  // 0-7
    volatile bool dirty;
};
```

通用 API：`set_pixel`、`fill`（起点越界丢弃 / 部分越界截断）、`draw_bitmap`（边界检查+MSB-first 位图写入；**2026-09-14 起越界语义 = 与屏幕取交集逐像素裁剪绘制**（起点越界丢弃、部分越界画可见部分、完全在屏内与旧实现逐像素等价），此前为「越界整块丢弃」；`dev_display.c:225-262`）、`set_brightness`、`commit_frame`、`commit_frame_rect`（④a 2026-09-08：脏矩形提交——只重排矩形覆盖行，scan_task 经基类 `dirty_rect_*` 字段传给 prepare（虚表签名不变），未消费多次提交自动合并包围盒；全屏路径 `commit_frame` 行为零变化）。

### 模组清单与选编（2026-09-17 实态：22-1665 已升级为多链独立位流；第二十八轮收口整理，行为零变化）

| 驱动文件 | 几何（宽×高） | 扫描/通道 | CCMRAM | 编入状态 |
|---|---|---|---|---|
| **`dev_display_1_263.c`**（`1000000263`） | **224×64**（7×32 / 2×32） | 1/8，2 通道/模块（总 4） | 29568B | **Makefile 默认口径（`DISP=1_263`）**；EIDE Debug **排除**本文件 |
| **`dev_display_22_1703.c`**（`2200001703`） | 宏当前为 **224×64**（7×32 / 4×16）；**用户 WIP——宏可被改成 1×1 = 32×16**（该态 `.ccmram` 39164→11420，差值 27744B 可反推板上镜像几何） | **1/4**，2 通道/模块（总 **8**） | 31648B（1×1 态 3904B） | `make DISP=22_1703` 选编；**EIDE Debug 现已排除本文件**（2026-09-16 用户改选 22_1665；此前为 EIDE 现行编入项） |
| **`dev_display_22_1665.c`**（`2200001665`） | **16×16 双色**（4 条数据线 × 每模块每线 128 位 = 512 LED = 256 像素 × 2 die）；**几何宏控**：`_22_1665_MODULE_ROWS` / `_22_1665_MODULE_COLS`（**改屏体尺寸只改这两个**）+ 单模块像素宏（16×16）→ 屏面、**每帧时钟 = 128 × MODULE_ROWS**，**语义同兄弟驱动**；数据线表 `_22_1665_lines[5][4]` 恒 5 组 × 4 行（**与模块数无关**），**链段 = 模块 × 线、区域自动推导**，组 0 = R1/G1/R2/G2（现场逐脚定标）、**组 1 = R3/G3/R4/G4 = PB8/PB9/PE1/PE2**；**接线模型 = 模型 B（每列一口独立线，2026-09-17 第二十七轮落地、第二十九轮删去模型 A 后为唯一形态）**；现场 A/B 开关 `_22_1665_CHAIN_HEAD_IS_MODULE0`（默认 0；组内横向链首），测试矩阵见 doc/01 §0.10/§0.11 | **静态 1 扫**，**每段 128 位 = 8 片 MBI5034B**；换屏标定用**归档的旧驱动**（探针 7/8/9，见 doc/01 §0.3 / 附录 A.8） | **模型 B `1408M + 256·COLS`**（1×1 = **1664B**、1×2 = **3328B**、**8×2 = 23040B**：`chain_dst` 1024M + pixel_map 256M + 状态 128M + 合并写表 256·COLS；**上限（第三十四轮）= 无自设预算、无编译期守卫**（17 条 `_Static_assert` 已全删，约束 = 注释契约 + doc/01 §0.3）—— 物理硬约束 `2·COLS ≤ 10` ⇒ **COLS ≤ 5**（越限 = 越界读 `_22_1665_lines`），整片占用链接期兜底）；历史模型 A `1408M + 384`（已删）。驱动对象实测：1×1 1664 / 1×2 3328 / 2×2 6144 / 8×2 23040 / **8×4 46080 B** | **EIDE Debug 现行编入本文件**（1_263 / 22_1703 均在其 excludeList 内）；`make DISP=22_1665` 选编；**2026-09-17 现场确认显示正常**（as-built 与「改分辨率」步骤见 doc/01 §0 / §0.9；**工作区当前宏值 = 8×4 = 128×64（本模组 46080B）⇒ make 的 ALL/CQ 两口径都链接期溢出，只有 EIDE Debug 子集能装**） |
| `dev_display_1_969.c`（`1000000969`） | 192×96（3×3） | 1/8，2 通道/模块（总 6） | 38208B | 备用，EIDE Debug 排除 |
| `dev_display_1_577.c`（`1000000577`） | 192×96（3×3） | 1/8，2 通道/模块（总 6） | 38208B | 备用，EIDE Debug 排除 |
| `dev_display_1_260.c` | 32×32（单模块） | 1/8 | 2560B | 备用，任何构建均排除 |
| `dev_display_p20.c` | 128×32（8×16 / 4×8） | 静态 1 扫 | 9792B | 未编入任何构建 |

**硬约束：显示模组互斥（`1_263` / `22_1703` / `22_1665` 三选一）**——三模组显存
（29568 / 31648 / 1792B）+ CQ 6377 + GZ_OL 1136 + YN_OL 1152 合计超 64KB CCMRAM（且双实例先后
`dev_display_register`，活动屏由链接序决定）→ Makefile 用 `DISP` 开关三选一
（默认 `1_263` 保持现状；非法值 `$(error)`）；EIDE 侧用 `targets.Debug.excludeList`
表达同一语义（`dev_display_1_263.c` 与 `dev_display_22_1703.c` 当前**均在排除表内**、
`dev_display_22_1665.c` 已收录且不在排除表内 → **EIDE Debug 编 22_1665**；
2026-09-16 用户裁决，切模组须同步改排除表并 **EIDE: Reload Project**）。

> `[待核对 2026-09-16]` 本表 1-263 行记「224×64（7×2）/ CCM 29568B」，但**当前工作区
> `dev_display_1_263.c` 的宏是 `MODULE_ROWS=MODULE_COLS=1`（32×32 / CCM 2496B）**，
> 本轮默认口径实测 `.ccmram=10012`（非显示部分 7516 + 2496）即由此而来——
> 该差异与本轮 22-1665 改动无关，待单独一轮复核 1-263 口径。

### `dev_display_1_263.c`（现行默认，P6 32×32）

单模块 32×32 像素，HUB75 2 通道/模块，ABC 1/8 扫描，行驱动 16206S（译码器型）、列驱动 2013EP。工作区当前为 **7 模块/行 × 2 模块/列 = 224×64**（`doc/01_显示系统/1-263模组驱动分析与修复记录.md` §7 记载"MODULE_ROWS 改 2"的扩展路径；本工作区为 7×2 实测口径，见 doc/06-04 §7）。`row_dst[y] = ch*512 + line*64 + row_type*32`，`g_bsrr[TOTAL_CHANNELS][8]` 预计算查表（768B CCM）。

### `dev_display_22_1703.c`（P10 32×16 / 料号 2200001703）

单模块 32×16 像素（水平 32 × 垂直 16），1/4 扫描，2 通道/模块，7 模块/行 × 4 模块/列 → **224×64**，共 8 通道；列驱动 MBI5124GM（16 通道恒流）。位流映射（= 源固件 `displayDataUpdata` 口径，wyh 分支实机版同口径）：

```
ch = y/8 ; line = y%4 ; half = (y%8)/4
row_dst[y] = ch*1792 + line*448 + half*8          // init 预计算（CCMRAM）
prepare    : 每逻辑行 28 个 8 字节块拷贝（块间步长 16）
scan       : 448 时钟；每时钟 5 次 BSRR（按 GPIO 端口合并写，档位 D+）
```

- **scan 档位 D+**：8 通道 24 个数据脚按 GPIO 端口分 5 组（G/B/E/C/F，由 `g_hub75_pin_*` 推导并在 init 校验同组同端口），每时钟合并写 5 次 BSRR（表 2848B CCM）替代档位 A 的 24 次写引脚；校验失败自动回退通用逐通道（`pl_hub75_set_rgb`）。实测 `-Og` 反汇编 **45 指令/时钟 × 448 = 20.2k 指令/行**（现行 1-263 为 58 × 448 = 26.0k 指令/行，即本模组每行位输出是 1-263 的 4 倍而指令少 22%）；按 1.0~1.3 周期/指令估算 **120~156µs/行** = TIM3（500µs）的 **24~31%**（上机须 DWT 实测复核；TIM3 周期为全局参数，本次未改）。
- 详细推导/差异清单/上机清单见 `doc/01_显示系统/22-1703模组驱动分析与迁移记录.md`。

### 扫描架构

```
TIM3 (行同步, 频率=刷新率×scan_lines)
  └─ ISR: osEventFlagsSet(s_scan_evt)
       └─ scan_task (osPriorityRealtime, 最高优先级, 不可抢占):
            1. osEventFlagsWait(s_scan_evt)  ← 阻塞，零 CPU 占用
            2. if(dirty) ops->prepare(dev)   ← pixel_map → hub75_buff
            3. ops->scan(dev, scan_line)     ← BSRR 查表 + CLK 脉冲
            4. OE/LAT 原子窗口               ← osKernelLock + 关TIM4中断
            5. scan_line = (scan_line+1) % scan_lines

TIM4 (PWM, 频率=TIM3×8, 8级亮度):
  └─ ISR: OE=(pwm_cnt >= light_level), pwm_cnt=(pwm_cnt+1)&7
```

**实时性保证**：
- `scan_task` 设为 `osPriorityRealtime`，不会被任何用户任务抢占
- OE/LAT 窗口内 `osKernelLock()` + `NVIC_DisableIRQ(TIM4_IRQn)` 防止 TIM4 抢占破坏 OE 时序
- `prepare` 在 OE/LAT 窗口外完成（off critical path），扫描本身只查表

### 颜色系统

```c
typedef enum { COLOR_BLACK=0, COLOR_RED=1, COLOR_GREEN=2, COLOR_YELLOW=3,
               COLOR_BLUE=4, COLOR_PURPLE=5, COLOR_CYAN=6, COLOR_WHITE=7 } display_color_t;
```
颜色值直接写入 `pixel_map[]`，在 prepare 阶段重新映射——像素重排后写入 `hub75_buff[]`，扫描时查 `g_bsrr` 得到三通道 BSRR 输出值。

## IO 子系统 (`Device/IO/`)

### 按键设备 (`dev_key.c`)

两阶段初始化：`hw_dev_initcall` 注册 EXTI 下降沿回调，`sw_dev_initcall` 创建 TEST 信号量。

| 按键 | GPIO | 类型 | 检测方式 | 用途 |
|---|---|---|---|---|
| SW1 | PE12 | 干接点 | EXTI ↓ + `app_key` 轮询释放 | RLS 干接点：**ETC专用**（绿，2026-09-09 接入） |
| SW2 | PE11 | 干接点 | EXTI ↓ + `app_key` 轮询释放 | RLS 干接点：**ETC/人工**（绿，2026-09-09 接入） |
| SW3 | PE10 | 干接点 | EXTI ↓ + `app_key` 轮询释放 | RLS 干接点：**车道关闭**（红，2026-09-09 接入） |
| KEY_TST | PD8 | 测试键 | EXTI ↓ → 信号量（一次性消耗） | `app_key_test_pressed()` |
| DIP1 | PE7 | 拨码开关 | `app_key` 纯轮询 (3 次一致去抖) | 波特率选择：ON=115200 / OFF=9600（`app_uart_baud`，RS232+RS485 同步） |
| DIP2 | PE8 | 拨码开关 | `app_key` 纯轮询 (3 次一致去抖) | 配置选择 |

所有按键 GPIO 内部上拉，低有效（按下=0）。

**数据流**：
```
硬件按下 → EXTI ISR → dev_key_get_state=标记为 true
                              ↓
app_key key_poll_task (20ms) → 轮询 GPIO 实际电平确认释放
                             → dev_key_clear_state() 清除标记
                             → 更新本地去抖缓存
```

外部通过 `app_key_get_state(DEV_KEY_SW1)` 获取去抖后的稳定状态。`_app_key_init()` 由 `sw_app_initcall` 自动创建轮询任务。

### IO 控制 (`dev_io_ctrl.c`)

`hw_dev_initcall` 初始化 GPIO 输出（PD14 车道灯、PD15 闪光灯），确保启动时均为低电平。

| 函数 | GPIO | 功能 |
|---|---|---|
| `dev_io_lane_light(bool)` | PD14 | 车道灯开关 |
| `dev_io_flash_light(bool)` | PD15 | 黄闪灯开关 |

### 光传感器 (`dev_light_sensor.c`)

ADC1 采样光敏电阻分压（LDR：光越暗→电阻越大→ADC 值越高），8 次均值滤波后映射为亮度等级。`dev_light_sensor_auto_adjust()` 自动更新 `display->light_level`。

**亮度范围限制**：`light_sensor_dev_t` 含 `min_level`/`max_level` 字段（默认 1~7），`dev_light_sensor_read()` 输出前按范围钳位。`app_light_sensor_init()`（`sw_app_initcall`）上电时调用 `dev_light_sensor_set_range(4, 7)`，将光敏自动调光限制在 4~7 级（满足出厂最低/最高亮度要求）。外部可通过 `app_light_sensor_set_range(min, max)` 运行时动态调整。

`app_light_sensor_init`（`sw_app_initcall`）创建 1 秒周期的自动调节任务。

## Storage 子系统 (`Device/Storage/`)

OCP 虚表，上层通过便捷内联调用（如 `dev_storage_read(d, addr, buf, len)` 自动解引用 `d->ops->read`）：

### W25Qxx (`dev_w25qxx.c`)

SPI NOR Flash，JEDEC 自动识别容量（W25Q16~256+），>128Mb 自动 4 字节地址。

**半双工 SPI 读流程**（`_read`）：
```
1. pl_spi_transmit(cmd, addr_len+1)   阻塞发 Read 命令 + 地址
2. pl_spi_receive_dma(buf, len)       DMA 数据→buf，CPU 不参与搬运
3. osEventFlagsWait(s_evt)            事件等待 DMA 完成（2026-09-08 起；
                                       DMA 回调 ISR 置位即刻唤醒；10ms 超时
                                       兜底返回错误；RTOS 未就绪兜底仍
                                       while(!s_ok) osDelay(1)）
4. _cs_high()                         SPI 释放
```

- DMA 回调 `_dma_cb`：`s_ok=true` + `osEventFlagsSet`（DMA 完成唤醒等待方）
- 首次 `_read` 时懒初始化 `s_evt` + 注册 DMA 回调
- 写操作使用读-改-写模式（`_read` 整扇区→修改→`_write_no_check` 整扇区），需先擦除非空扇区
- **读时延口径（2026-09-08 优化）**：原 `osDelay(1)` 轮询粒度 1ms 且锁在睡眠期
  间持有——每字形一次 DMA 读（≤128B，SPI 传输 ~20µs）被摊成 ~1ms；滚动整行
  14 字形 ≈14ms、4 行同拍 ≈56ms，step_ms=2 时实际推进被拖慢至 ~14ms/px（速度
  失真 ~7×）并连带阻塞其他协议渲染。事件等待后每字形 ≈ 传输时间 + 上下文切换
  （~0.1ms 量级），4 行同拍 ≈ 数 ms，滚动速度贴近协议设定

### 内部 Flash (`dev_flash_int.c`)

OCP 虚表实现，提供 `flash_int_ops`（`init/read/write/erase`），由各配置模块绑定到具体实例。

| 实例 | 绑定模块 | 基地址 | 大小 | initcall |
|---|---|---|---|---|
| `g_board_cfg_flash` | `Application/Src/Config/app_board_net_cfg.c` | `0x08004000` (Sector 1) | 16KB | `hw_dev_initcall` |
| (LDI 已迁移) | `Application/Src/LDI/app_ldi_cfg.c` | W25Qxx 最后 4KB 扇区 | 4KB | `sw_dev_initcall` (需要 W25Qxx 先就绪) |

**内部 Flash ops 实现**：
- `_read`：直接内存映射 `memcpy(buf, (void*)(base+addr), len)`
- `_write`：`pl_flash_unlock → pl_flash_program_word × N → pl_flash_lock`（按 word 编程）
- `_erase`：`pl_flash_erase_sector(sector, voltage)`

**板级系统配置** (`Application/Src/Config/app_board_net_cfg.c`)：`app_board_sys_info_t` 记录（72B，方案 B 2026-08-21）= magic(4) + update_sta(4) + FWInfo(40) + NetConfig(20) + CRC32(4)，布局由头文件 `static_assert` 锁定（与 Bootloader/Recovery 二进制兼容）。NetConfig 字段语义：`ip/mask/gw` 两套口共享；`port` = **TCP 业务口**（LDI 0AH / IAP 0x01 报 0x02 写 / TCP Server 监听，出厂默认 9528）+ **贵州治超专用 UDP 业务口**（GZ_OL `0x40` 写 / `CH_ID_UDP_GZOL` 读；同号不同协议栈，2026-09-14 用户裁决，doc/14 §13）；`udp_port` = **CQ UDP 业务口**（CQ setip 写 / CQ UDP 通道读，出厂默认 20103）。网络配置经 `app_board_net_cfg_get` / `app_board_net_cfg_update(ip,mask,gw,port,udp_port)` 读写（LDI 0AH / IAP 4B02 写 port 保留 udp_port；CQ setip 写 udp_port 保留 port），FWInfo/update_sta 经 `app_board_net_cfg_read` 整记录读取（IAP 命令用）。**升级兼容**：旧 68B 记录按新 72B 布局重算 CRC 必失配 → 走空/损坏自愈路径重写（网段配置丢失一次，由 ldi_ctx_init W25 镜像回写或 app_net_boot 默认落盘恢复）；**反向同理**——设备上旧 68B 布局 Bootloader/Recovery 读新 72B 记录同样失配 → 反复重建旧布局出厂记录，**方案 B 后三固件必须同步烧录（`tool/flash_all.sh` 一次会话），否则 Sector1 两布局交替重建、设备可能陷 Recovery（网络全部无响应）**（2026-08-21 纪律强化）。

**LDI 配置** (`Application/Src/LDI/app_ldi_cfg.c`)：已迁移至 W25Qxx 最后一个 4KB 扇区。`app_flash_ldi_record_t`（116B）= magic(4) + cfg(106) + padding(2) + CRC32(4)。`_app_flash_ldi_storage_init`（`sw_dev_initcall`）中 `s_ldi_base = dev_storage_capacity(w25) - 4096`，通过 `dev_w25qxx_get()` 获取存储句柄。

> **解耦已落地（2026-08-14）**：LDI 与 IAP 协议均经中立模块 `app_board_net_cfg`（`Application/Config`）读写 Sector 1 网络配置，互不依赖对方协议目录；`app_iap_cfg.{c,h}` 与死代码 `Device/Inc/config_info.h` 已删除。守卫语义（空/损坏自愈、升级中间态仅放行 net_cfg 字段更新、同值跳过）见 `doc/07_LDI与IAP配置解耦`。

## Render 引擎 (`Application/Src/app_render.c`)

数据驱动字库引擎，字库存储在 W25Qxx Flash 中，模块 `sw_app_initcall` 自注册。

### 字库组织
- 字库 = **(字号, 编码, 字型)** 三元组，由 `font_chip_config_t` 芯片配置描述（`s_chip_configs[]`，当前激活 W25Q64；区块表按芯片配置动态取用，非固定 40 项全局表）
- `_packed_glyph_bytes(size, charset)` = 每字符打包字节数：ASCII `size×((size/2+7)/8)`，GBK `size×((size+7)/8)`
- `_glyph_width_px(size, charset)` = 字符像素宽：ASCII `size/2`，GBK `size`
- `_find_region(size, charset, type)` = 在当前激活芯片配置的区块表中按三元组查找（未命中回退首项）
- `_flash_addr(size, charset, type, ch)` = 单字符 Flash 绝对地址：`char_idx×bpc` 后按 sec/page/byte 分步计算，含芯片级 sec/page/byte 修正（ASCII 原始码/减 0x20、GBK 190 列/94 列按芯片配置区分）

### Tagged Union API

```c
app_render(&(render_cfg_t){
    .type = RENDER_TEXT, .x=0, .y=0, .w=128, .h=16, .color = COLOR_GREEN,
    .text = "你好", .len = strlen("你好"),
    .font_size = FONT_16, .font_type = FONT_ST, .text_enc = FONT_ENC_UTF8,
});
```

- `RENDER_TEXT`：UTF8→GBK→逐字 Flash 地址→`dev_storage_read`→`dev_display_draw_bitmap`。支持 `word_wrap` 换行，不换行时截断
- `RENDER_BITMAP`：直接调用 `dev_display_draw_bitmap`
- `RENDER_FILL`：`w=h=0` 全屏填充，否则矩形填充
- 渲染风格 `render_style_t`：`h_align` / `v_align` / `word_wrap`（文字专属）。测量趟+渲染趟两阶段，支持逐行独立水平对齐（左/居中/右）。

## Application 模块全景

| 模块 | 类型 | 通道绑定 | initcall | 职责 |
|---|---|---|---|---|
| `app_dispatch` | 框架 | — | `sw_app_initcall` | 协议调度引擎 |
| `app_render` | 引擎 | — | `sw_app_initcall` | 字库渲染（含导出 `app_render_draw_glyph_clipped` 单字模逐像素裁剪绘制 / `app_render_glyph_width_px`，供 app_scroll 滚动渲染） |
| `app_scroll` | 引擎 | — | —（惰性） | 通用动态滚动显示（4 槽静态 SRAM 376B；scroll_task 惰性创建 2ms 节拍/空闲事件标志休眠；方向 LEFT/RIGHT/UP/DOWN 循环滚动；④a 整拍一次 `dev_display_commit_frame_rect` 并集提交（仅活跃槽）；⑤ `app_scroll_render_lock/unlock` 渲染互斥与静态渲染串行；**stop=冻结、stop_all=清行（2026-09-09 语义分离）**；安徽 0x86~0x89 第一个消费者，API 预留 LDI/VMS） |
| `app_iap` | 协议 | UDP（RJ45 共享 RB） | `sw_app_initcall` | IAP 固件升级帧处理 |
| `app_ldi` | 协议 | TCP_SERVER / TCP_CLIENT / UDP（RJ45 共享 RB） | `sw_app_initcall` | LDI 显示控制协议 |
| `app_qh_proto` | 协议 | RS485 + RS232 | `sw_app_initcall` | 青海高速费显协议 |
| `app_sc_etc` | 协议 | RS485 + RS232 | `sw_app_initcall` | 四川 ETC 费显协议（0A 帧族；心跳超时显示已停用） |
| `app_sc_mtc` | 协议 | RS485 + RS232 | `sw_app_initcall` | 四川 MTC 费显协议（'{' 方案二 + 0A 46 查询 + 7B 40~45） |
| `app_sc_ol` | 协议 | RS485 + RS232 | `sw_app_initcall` | 四川治超屏协议（FF+len 帧族，BCC 异或） |
| `app_yn_proto` | 协议 | RS485 + RS232 | `sw_app_initcall` | 云南费显协议 |
| `app_yn_ol_proto` | 协议 | **RS485 + RS232 + TCP Server + TCP Client** | `sw_app_initcall` | **云南治超屏协议**（YN_1.3.0 `{` 帧族 19 命令字；'6'/'7'/'9' 不开发→probe 快拒；**四通道四 mask 共用一队列**，**不绑 UDP 10011**（与 CQ 的 `{` 同首字节竞争靠通道掩码隔离，见 doc/15 §1）；属 `{` 帧族→守卫互斥；队列置 CCMRAM；**上电默认画面已删除**；`0x49` 屏体参数持久化于 W25Qxx 独立扇区） |
| `app_anhui_proto` | 协议 | RS485 + RS232 | `sw_app_initcall` | 安徽费显协议（5A/A5 帧族 12 命令；动态显示 0x86~0x89 已接入 app_scroll 循环滚动（**FONT_24，2026-09-09**）、0x85 静态 FONT_24（**坐标直用像素：x=data[0]、y=data[1]，固件不做行号换算，2026-09-15 终裁**）、停止帧=冻结；0x96/0x97 录制语音占位；坐标以模组驱动实际屏幕尺寸为准；0x92 8 档映射、0x95 音量字节执行层忽略；队列体静态 SRAM 与青海/贵州/云南同） |
| `app_gz_ol_proto` | 协议 | RS485 + RS232 | `sw_app_initcall` | **贵州治超屏协议**（"TCLY" 帧族 `54 43 4C 59` + 长度 2B 小端 = 整帧长 + 命令 4B + 载荷 + `0x00`，无校验；8 命令 0x10/0x20/0x30/0x40/0x50/0x60/0x70 出站/0x80；**`0x40`/`0x50`/`0x70` 端口字段＝高字节在前 BE16（2026-09-15 用户裁决，`27 2C`=10028，仅此字段）**；队列 1136B **置 CCMRAM**；上电三行绿字由 `app_gz_ol_proto_default.c` 注册；渲染持 `app_scroll_render_lock`；帧头 0x54 全槽唯一→无需 EIDE 互斥；详见 doc/14 §13.5.1） |
| `app_cq_proto` | 协议 | UDP_CQ（业务 20103）+ UDP（搜索 10011，RJ45 共享 RB） | `sw_app_initcall` | 重庆高速二代费显协议（JSON `{` + 12B 二进制；队列/缓冲置 CCMRAM；**心跳故障屏开关 `CQ_FAULT_SCREEN`（1 开 / 0 关）：PROTO_CHONGQING 默认开、共存构建默认关（他省上位机不发 syn1），可 `-DCQ_FAULT_SCREEN=1/0` 覆盖，见 doc/03 PartB B.7.2；旧名 CQ_FAULT_SCREEN_ENABLED / CQ_FAULT_SCREEN_FORCE_ON 已废弃**） |
| `app_uart_baud` | 应用 | — | `sw_app_initcall` | DIP1 波特率选择与运行态切换（RS232+RS485） |
| `app_rls` | 协议 | RS485 | `sw_app_initcall` | RLS 重庆高速二代费显协议 |
| `app_vms_ctrl` | 协议 | (LDI 子模块) | — | VMS 情报板控制（LDI→Render 桥接） |
| `ah_mqtt` | 协议 | MQTT | (已注释) | AH 平台 MQTT（签到/状态上报/指令） |
| `app_mqtt` | 通道 | CH_ID_MQTT | — | MQTT 网络传输通道 |
| `app_udp` | 通道 | CH_ID_UDP | — | UDP 广播通道（端口 10011） |
| `app_udp`（CQ 实例） | 通道 | CH_ID_UDP_CQ | — | CQ 业务口 UDP 通道（`PROTO_CHONGQING` 读 Sector1 net_cfg.udp_port 默认 20103（方案 B 2026-08-21），dev 共存构建固定 20103；`app_udp_cq_start`/`app_udp_cq_broadcast`） |
| `app_udp`（GZ_OL 实例） | 通道 | CH_ID_UDP_GZOL | — | 贵州治超专用 UDP 业务口（端口 = Sector1 **net_cfg.port**，与 TCP 业务口同号不同协议栈；空/0 回退 9528，与 10011/CQ 同号跳过绑定并 RTT 告警；`app_udp_gzol_start`/`app_udp_gzol_get_port`；`0x40` 改端口的生效对象） |
| `app_tcp_server` | 通道 | CH_ID_TCP_SERVER | — | TCP 服务器通道 |
| `app_tcp_client` | 通道 | CH_ID_TCP_CLIENT | — | TCP 客户端通道（远端默认 0.0.0.0:0 **未配置不连**；`set_remote` 配好后才 connect） |
| `app_rs232` | 通道 | RS232（USART3） | — | RS232-0 通道启动（USART6 语音仅 TX，不启通道） |
| `app_rs485` | 通道 | RS485 | — | RS485 UART 通道启动 |
| `app_key` | 应用 | — | `sw_app_initcall` | 按键轮询去抖 (20ms) |
| `app_light_sensor` | 应用 | — | `sw_app_initcall` | 光传感器自动亮度 (1s 周期) |
| `app_boot` | 编排 | — | — | RTOS 启动编排 |
| `app_net_boot` | 应用 | — | — | 网络配置横切应用（Sector1 net_cfg → netif + TCP Server 口；空/损坏/非法写本构建默认落盘并应用，两口径共用） |
| `app_default_display` | 应用 | — | — | 注册制默认显示界面（协议 sw_initcall 注册回调则用之，否则默认欢迎画面） |
| `app_test` | 测试 | — | — | 硬件测试用例 |

**类型说明**：
- **协议**：注册 probe 到 dispatch，创建处理任务，从 frame_queue 收帧
- **通道**：实现 `ch_ops->send`，收包→`app_channel_dispatch`，发包→网络/UART
- **框架/引擎**：供协议模块调用，不直接通信
- **应用**：硬件相关业务逻辑，不涉及通信协议

**当前运行状态**：`app_boot` 的 `init_task` 显式启动五个通道 — TCP Server、TCP Client、UDP、RS485、RS232。TCP Client 远端默认 `0.0.0.0:0`，未配置则任务 idle、不 `netconn_new`/不 connect（`set_remote` 后才连）。`app_rs232_1_start()`（USART6 语音 TX 桩）已注释；`app_mqtt` 的 start 函数已定义（`static inline`）但未被调用；`ah_mqtt` 的 initcall 已注释（未激活）。

## 协议分发层 (`Application/Src/app_dispatch.c`)

### 核心数据结构 `dispatch_ctx_t`

| 字段 | 类型 | 用途 |
|---|---|---|
| `registered_mask` | `uint32_t` | 已注册协议位掩码（`app_proto_register` 自动分配 2 的幂） |
| `proto_rb[i]` | `ring_buffer_t *[PROTO_MAX_COUNT]` | 协议→缓冲区 |
| `proto_probe[i]` | `proto_probe_fn_t [PROTO_MAX_COUNT]` | 协议→探测函数 |
| `frame_queue[i]` | `osMessageQueueId_t [PROTO_MAX_COUNT]` | 协议→帧队列 |
| `buf_pool[id]` | `ring_buffer_t *[RB_CNT_MAX]` | 缓冲区池（=3 槽：RJ45/RS485/RS232，懒初始化） |
| `ch_queue` | `osMessageQueueId_t` | 通道通知队列 |
| `ch_proto_map[ch_id]` | `proto_mask_t [CH_ID_MAX]` | 通道→协议掩码 |
| `channels[ch_id]` | `channel_t *[CH_ID_MAX]` | 通道注册表 |

### 数据流

**接收**：通道任务 → `app_channel_dispatch(ch,data,len)` → `rb_write` → `osMessageQueuePut(ch_queue,&ch)` → `frame_dispatch_task` → probe → `rb_read` → `osMessageQueuePut(frame_queue[i],&msg)` → 协议任务

**发送**：协议任务 → `channel_send(ch,data,len)`（入口回验 `app_channel_get(ch->ch_id)==ch`，防悬垂通道指针）→ `ch->ops->send(ch,data,len)` → UART DMA / LWIP netconn

### 多协议共享 RB（doc/05 核心成果）

- **一物理通道一 RB**：RJ45 1536 / RS485 768 / RS232 768（`Application/Inc/app_dispatch.h` `_Static_assert` 在册）。网口逻辑通道（TCP Server/Client/UDP/MQTT）共享 RJ45 槽。
- **RB_PROVIDE_WEAK 编译期按需提供**：协议 TU 以 `RB_PROVIDE_WEAK(rb_provide_xxx, size)` 提供 RB 体；未编入任何协议的槽为 NULL（`app_proto_acquire_buf` 返回 nullptr）。
- **同 RB 写去重**：`app_channel_dispatch` 用 `seen[]` 指针去重，多协议共享同一 RB 时只写一次。
- **链式 probe 契约**：READY/SKIP 消费后取下一帧；WAIT/FAKE 继续试下一协议；一轮无消费时 any_wait 停等更多字节 / any_fake 跳 1 字节重同步。
- **probe 首字节快拒**：探测函数首字节不匹配即返回 FAKE，禁止盲 WAIT 阻塞链式探测。

**帧 queue 深度（静态 SRAM 队列，不占 ucHeap）**：IAP 2 / LDI 4 / QH 3 / RLS 2 / AH_MQTT 3 / SC_ETC 3 / SC_MTC 3 / SC_OL 3 / SD 3 / GZ 3 / YN 3 / ANHUI 3 / CQ 3 / GZ_OL 3 / **YN_OL 3**（**CQ、GZ_OL、YN_OL 队列体置 CCMRAM**，见上）。

### 新增协议步骤

1. 定义 `proto_probe_fn_t` 探测函数，返回 `PROTO_PROBE_READY/WAIT/SKIP/FAKE`
2. 协议模块 init 函数中：`app_proto_acquire_buf`→`app_proto_register`→`app_proto_bind_channel`（可多次）→`osThreadNew`
3. 协议任务中：`osMessageQueueNew`→`app_proto_set_frame_queue`→循环 `osMessageQueueGet`→处理
4. `sw_app_initcall(module_init)` 自注册

**新协议接入的完整规则、模块骨架、串口/网络专项与 step-by-step 检查清单见 `doc/08_协议模块接入规则/`**（本列表仅为摘要；RB/绑定/兼容矩阵权威仍为 doc/05，内存占用账仍为 doc/06）。

### 通道标识映射

| ch_id | 枚举 | 传输层 | 实现类 | 绑定文件 |
|---|---|---|---|---|
| 0 | `CH_ID_RS485` | UART (USART1) | `rs485_ch_t` | `app_rs485.c`（buf 在 `dev_rs485.c`） |
| 1 | `CH_ID_RS232` | UART (USART3) | `rs232_ch_t` | `app_rs232.c`（buf 在 `dev_rs232.c`） |
| 2 | `CH_ID_TCP_SERVER` | LwIP TCP | `tcp_server_channel_t` | `app_tcp_server.c` |
| 3 | `CH_ID_TCP_CLIENT` | LwIP TCP | `tcp_client_channel_t` | `app_tcp_client.c` |
| 4 | `CH_ID_UDP` | LwIP UDP | `udp_channel_t` | `app_udp.c` (端口 10011) |
| 5 | `CH_ID_MQTT` | LwIP MQTT | `mqtt_channel_t` | `app_mqtt.c` |
| 6 | `CH_ID_RS232_1` | UART (USART6，仅语音 TTS 旁路 TX) | `dev_rs232_voice`（直接 `pl_uart_send`） | 无通道任务、无 DMA RX，禁止协议 bind |
| 7 | `CH_ID_UDP_CQ` | LwIP UDP（业务口，`PROTO_CHONGQING` 读 Sector1 net_cfg.udp_port 默认 20103（方案 B）；dev 共存构建固定 20103） | `udp_channel_t`（CQ 实例） | `app_udp.c`（`app_udp_cq_start`；CQ 协议 bind，JSON 业务 + 12B 二进制） |
| 8 | `CH_ID_UDP_GZOL` | LwIP UDP（贵州治超专用业务口；端口 = Sector1 `net_cfg.port`，出厂默认 9528，与 TCP 业务口同号；空/0 回退 9528，与 10011/CQ 同号跳过绑定） | `udp_channel_t`（GZ_OL 实例） | `app_udp.c`（`app_udp_gzol_start`；GZ_OL 协议 bind；`0x40` 改端口生效对象，见 doc/14 §13） |

**选编口径（2026-09-16 复核）**：

- **Makefile**：`DISP ?= 1_263`（默认，保持改动前行为）/ `DISP=22_1703`（P10 订单）/ **`DISP=22_1665`（16×16 红绿双色多链，2026-09-17 现场调通）**——三模组 CCM 硬约束不可同编、非法值 `$(error)`；`DISP` 值进口径指纹 `.build_stamp`，切口径必然全量重编 + 重链接。**22_1665 基线**（`PROTO=ALL` `DISP=22_1665`；**第二十七轮现场口径 = 1×2（16×32）、接线模型 B**：text **172164** / rodata **202952** / data 1672 / ccmram **39164**（本模组 **3328B** = `1408×2 + 256×2`）/ bss 124712 / `_user_heap_stack` 2560 / SRAM 合计 **128944**（余 **2128B**）/ elf md5 `见构建日志/横幅（本轮最终构建 elf md5 见 .analysis/22_1665/round27_wiring_b_and_etc.md §3）`；**1×1 口径**：驱动对象 **1664B**；**（历史模型 A 口径 1×2 = 3200B / 1×1 = 1792B 已随第二十九轮删除）**）；**驱动开关 1 个**（`_22_1665_CHAIN_HEAD_IS_MODULE0`，默认 0；= 组内横向链首，`MODULE_ROWS=1` 时无影响）+ 4 个几何宏（`MODULE_ROWS/COLS/PIXEL_ROW/COL`；**改分辨率只改前两个**，链段/落点/帧长全派生、**不补表**）；**第二十九轮已删除三化石开关**（`_22_1665_HUB_WIRING` / `_22_1665_BLUE_AS_LIT` / `_22_1665_DATA_ACTIVE_HIGH`，取值分别固化 2 / 1 / 1；恢复路径见 `.analysis/22_1665/archive/finalize/driver_pre_finalize_20260917.c`）；兼容口径 / 探针口径已删除；本文件零告警（全项目仅 HAL 3 条预存）；scan 热路径 **≈55 指令/时钟位（1×2 两组）**，**每帧时钟数 = 128 × MODULE_ROWS**（1×1 ≈ 25~35µs，1×2 ≈ 45~60µs）。**本模组 CCM 通式（模型 B = 唯一形态）`1408M + 256·COLS`；上限口径（第三十四轮起）= 无自设预算、无编译期守卫** —— 17 条 `_Static_assert` 全删，约束降级为**注释契约 + doc/01 §0.3**：错误几何的后果 = 静默错口径 / 越界读 `_22_1665_lines`（`COLS ≥ 6`），或**链接期** `region CCMRAM overflowed`（其他消费者实测 35836B ⇒ 实用上限 ≈29700B、COLS=2 时 M≈20 为上限量级〔估算〕，整片以链接期为准）；物理硬约束 `2·COLS ≤ 10`（⇒ COLS ≤ 5），历史口径的「非法宏值编译期报 ASCII 断言」已随本轮取消。**工作区当前宏值 = 8×4（128×64；第三十一轮曾改 8×2、之后被外部改成 8×4）：本模组 46080B ⇒ `PROTO=ALL` 链接期溢出 16380B / `PROTO=CQ` 溢出 14124B（预存状态，与断言删除无关），任何 make 口径都装不下，只有 EIDE Debug 子集能装（57484B/65536B）**；8×2 口径（第三十二轮）整项目 `.text 175248 / .rodata 203296 / .data 1656 / .ccmram 58876`（本模组 **23040** = `1408×16 + 256×2`，余 6660B）/ `.bss 124836` / `._user_heap_stack 2564` / SRAM 合计 129056B（余 **2016B**）；⚠ 8×2 / 8×4 的 scan ≈0.36~0.48ms 占 TIM3 500µs 周期 72%~96%、prepare 整帧提交 ≈1.0~1.2ms 跨帧 —— 须上机 DWT 复核（本轮未改定时参数）**；宿主复现 1×2 口径用 `bash .analysis/22_1665/link_variant.sh mod1x2 _22_1665_MODULE_COLS=2`、复现 8×2 用 `... mod8x2 _22_1665_MODULE_ROWS=8 _22_1665_MODULE_COLS=2`（**源码替换**；**第三十轮起几何宏 = 普通 `#define`，`-D_22_1665_MODULE_COLS=2` 写法不生效**，`link_variant.sh` 对几何 `-D` 直接拒绝）。**as-built 见 `doc/01` §0（§0.3 模型 / §0.7 CCM 与上限口径 / §0.9 操作 / §0.10 现场矩阵 / §0.11 双 HUB 判定），历史轮次见同文档附录 A（A.17 = 第二十九轮、A.18 = 第三十轮、A.19 = 第三十二轮、**A.20 = 第三十四轮**）**。
- **22_1665 与 EIDE（2026-09-16 用户裁决后已落地）**：`.eide/eide.yml` 已把 `Device/Display/dev_display_22_1665.c` 加入 `Device/Display` 虚拟文件夹 files，并**保持** Debug `excludeList` 排除 `dev_display_1_263.c` 与 `dev_display_22_1703.c`（连同 p20/1_969/1_260/1_577）→ **EIDE Debug 只编 22_1665**（1665 不在排除表内，已核对）；其它 excludeList/defineList 未动，YAML 合法性已校验。**外部编辑纪律：请立刻在 EIDE 中执行 `EIDE: Reload Project`** 让它读入新条目，重载前不要做任何会触发保存的 GUI 操作（EIDE 会用内存模型回写 yml 冲掉外部编辑）。
- **22_1665 历史段尺寸（追溯用）**：第一~二十一轮逐轮的 text/rodata/ccmram/md5/tree 与各宿主脚本通过数，已随轮次细节移交 `doc/01` 附录 A 与 `.analysis/22_1665/archive/`（各轮报告 + `build_round*.log`）；**第二十三轮重构清单与零回归证明见 `archive/reports/round23_refactor.md`；第二十四轮报告见 `.analysis/22_1665/round24_resolution.md`；第二十五轮（1×2 取证 + 链首口径开关 + 现场测试矩阵）报告见 `.analysis/22_1665/round25_two_modules.md`；第二十六轮（双 HUB 口判定）见 `round26_two_hubs.md`；第二十七轮（模型 B 落地 + ETC 现象解释）见 `round27_wiring_b_and_etc.md`；**第二十八轮（收口整理：调试残留审计 + 格式对齐 + 过时引用修正，行为零变化）见 `round28_cleanup.md`**；现行基线见上一条**。
- **EIDE Debug（当前 `.eide/eide.yml` 实态，**2026-09-17 晚 联调第二轮复核**）**：**显示模组编 22_1665**（`dev_display_1_263.c` / `dev_display_22_1703.c` / `p20` / `1_969` / `1_260` / `1_577` 均在 `targets.Debug.excludeList` 内；`dev_display_22_1665.c` 在 files 内且未排除 —— 几何由驱动宏控，默认 16×16）；`defineList` = `USE_HAL_DRIVER` / `STM32F407xx` / **`PROTO_CHONGQING`**（⇒ netcfg 默认 IP 走 CQ 口径 **192.168.1.5** / 端口 9528 / udp_port 20103）；**excludeList 实态（本轮复核，与旧文不同）**：`Drivers/BSP/*` 三条旧路径 + `ProtocolParser_{ShanDong,Anhui,QingHai,GuiZhou,GuiZhou_Overload,SiChuang_ETC,SiChuang_MTC,SiChuang_Overload,YunNan,ChongQing,aH/rls}` + **`ldi` 目录** + 全部非 22_1665 显示驱动 ⇒ **EIDE Debug 编译集 ≈ 平台 + IAP + 云南治超（YN_OL）+ 22_1665 + `APP_DIAG_BANNER` 横幅**（YN_OL **已编入**：excludeList 排掉的是 `ProtocolParser_YunNan`（云南常规）而非本目录，`{` 帧族守卫满足；EIDE 无 Makefile `DIAG_DEFS` → 横幅的 `tree=`/口径字段打占位值，几何与端口行照常输出；横幅 `driver linked?` 行只探 1_263/22_1703 弱符号，22_1665 下打 0——**用户裁决 2026-09-16 不补 22_1665 弱符号**）。**改选任一目录后须 `EIDE: Reload Project`**；`{` 帧族互斥由 `g_brace_proto_guard` 链接期 `multiple definition` 兜底（「云南常规 × 云南治超」已 `ld -r` 实测）。
- **EIDE Release**：incList/excludeList 均为已删除的 `Drivers/BSP/*` 旧路径，**非可用构建入口**（2026-09-14 复核，未纳入本轮改动）。
- **框架纪律**：`{` 帧族（青海/山东/贵州常规/云南/四川MTC）互斥由 `g_brace_proto_guard` 链接期兜底（EIDE 无 `STD_ALL_PROTO` 时多编即 `multiple definition`）；贵州治超非 `{` 族，不受此约束（**2026-09-04 曾误取消贵州排除→贵州+四川MTC 双 `{` 帧族同编无 STD_ALL_PROTO→链接报 `multiple definition of 'g_brace_proto_guard'`，已恢复贵州排除**）。
- **实测段尺寸（Makefile GCC Debug，2026-09-15 字节序裁决轮复测，四口径全部链接通过、零新增告警）**：`PROTO=ALL` `DISP=1_263` text 167236 / rodata 201032 / data 1672 / ccmram 37084 / bss 126400（SRAM 130632B，余 **440B**）；`PROTO=CQ` `DISP=1_263` text 154060 / rodata 200384 / data 872 / ccmram 37084 / bss 122924（余 4712B）；`DISP=22_1703` 两口径 text 167492 / 154316、rodata 201120 / 200464、data 1752 / 952、ccmram **39164**（该文件当前 7×4 = 224×64 态）、bss 126400 / 122924、SRAM 余 360B / 4632B。**GZ_OL 端口字段改 BE16 属纯字节序互换 → 四口径段尺寸与 2026-09-14 复测轮逐字节相同**（唯一告警仍为 HAL `stm32f4xx_hal_flash_ex.c` 3 条预存 `-Wunused-parameter`）。**诊断设施增量（A/B 实测，`make APP_DIAG=0` 对照）**：text **+2640** / rodata **+2976**（Flash 合计 +5616B）、data/bss/ccmram **+0**（复测轮 ② 加写 0x40 载荷 dump + 启动 `netcfg INVALID` 告警；diag-off 与加写前逐字节同尺寸）。此前增量（对照本轮诊断前诊断态）：GZ_OL 专用 UDP 业务口 text +752 / rodata +296 / data +8 / bss +40 / ccmram +0（SRAM 余量 496→448B，ucHeap 另 +2 任务各 1KB）；贵州治超本体 text +2128（CQ 口径 +2144）/ rodata +216 / bss +16 / ccmram +1136；22-1703 相对 1-263 text +272 / rodata +80 / data +80 / ccmram +2080。**默认口径 elf md5（2026-09-15 字节序裁决轮最终构建）`65286c8febe1a3d39aef24f0638247aa`；安徽 0x85 行首口径修复后重构建（同日晚）`35f77ff1a130167bf271c65b6339e2b3`；注意横幅内嵌 `__DATE__/__TIME__` → 重新编译必然改 md5**（镜像身份以横幅 `fw=`+`built=`+`tree=` 为准）。详见 doc/06-04 §7.2、doc/14 §12/§13.5.1、`.analysis/9k23881580/field_triage_2432_and_port.md` §⑤。

**口径更正（2026-09-14）**：本文与 doc/06-04 早期记载的「1-263 = 224×128 / 显存 59136B / CCM 65516B」为**历史口径**；工作区实态为 `MODULE_ROWS=7 × 32 / MODULE_COLS=2 × 32 = 224×64`（CCM 29568B），已按实态更正（此前标注的 `[待更新: 需用户裁决]` 项由本轮落地解决）。

## UART 通道子系统 (`Device/Comm/` + `Application/Src/Channel/`)

RS485/RS232 按板级资源（Device 层）与通道生命周期（Application 层）分层，全库无统一 UART 通道抽象类型。

**Device 层（板级静态资源）**：
- `dev_rs485.c`（USART1，RE=PA8）：静态 DMA 乒乓双缓冲 `s_rs485_buf[2×RS485_BUF_SIZE=1280B]`（`Device/Inc/dev_rs485.h`，块 640×2）+ RE 方向回调 `rs485_dir_cb`（`pl_uart_set_dir_cb` 注入，`hw_dev_initcall`）。
- `dev_rs232.c`（USART3）：仅 `dev_rs232_get_buf(index)` — index0 返回 1280B 乒乓缓冲（`RS232_BUF_SIZE`=640 块 ×2），index1（USART6）返回 `nullptr`（无协议 RX）。
- `dev_rs232_voice.c`（USART6）：语音板 TTS 专用 TX。`dev_rs232_voice_play` / `dev_rs232_voice_volume` 组帧后经 `pl_uart_send(PL_UART6)` 发送，不经过 dispatch 框架。

**Application 层（通道生命周期 + 任务循环）**：
- `app_rs485.c`：`rs485_ch_t`（`channel_t me` + `uart/rx_queue/rx_buf`），`rs485_task` 循环 `osMessageQueueGet(rx_queue, {块号,偏移,长度})` → 按块拷贝 → `app_channel_dispatch`。`app_rs485_start()` 注册通道 + 注入 RX 回调 + `pl_uart_start_rx` + 创建任务（栈 256×4）。
- `app_rs232.c`：同构（`rs232_ch_t` / `rs232_task`，栈 256×4）。`app_rs232_1_start()` 为桩函数（返回 `nullptr` — USART6 语音 TX 不经通道任务）。
- `app_boot` 中 `app_rs485_start()` / `app_rs232_start()` 创建任务，`app_rs232_1_start()` 已注释。

**波特率选择（DIP1）**：`app_uart_baud.c`（`sw_app_initcall`）读 `dev_key_get_state(DEV_KEY_DIP1)`（PE7，active_low）：ON=115200、OFF=9600，经 `pl_uart_set_baud` 同步重配 USART1（RS485）+ USART3（RS232）。sw initcall 早于 `app_rs485_start`/`app_rs232_start`，切换时 DMA RX 未挂，无竞态；运行态切换（MTC 7B 40 命令）由 `pl_uart_set_baud` 内部停/重挂 DMA RX。DIP2（PE8）为 app_render 字库芯片选择，不得改动。

## IAP 协议 (`Application/Src/IAP/app_iap.c`)

固件升级协议，**仅 UDP**（`CH_ID_UDP`，RJ45 共享 RB）。帧格式 `0x5A5A5A5A (4B) | seq (4B) | cmd (4B) | len (4B) | data | CRC32 (4B)`（`app_iap.h`），probe 首字节 `0x5A` 快拒。

- `iap_handle_task`：协议处理任务（栈 256×4=1KB，帧缓冲 static；原 2KB 偏大），循环 `osMessageQueueGet` → 帧解析 → 命令分派
- `IAP_QUEUE_DEPTH=2`，静态 SRAM 队列（不占 ucHeap）
- 命令表 `g_iap_cmd_table[]`（`app_iap_cmd.c`）：0x00 Test / 0x01 上报 IP 配置 / 0x02 强制修改 IP（写 Flash）/ 0x03 上报固件版本·大小·CRC32·升级状态（version 由主固件启动时经 `app_board_net_cfg_fw_version_update` 从 PROGRAM_CODE 落库 Sector1 `app_info.version`，见 doc/07 §13）/ 0x04 准备升级 / 0x05 发送升级包 / 0x06 进 Recovery（写 RTC 备份寄存器标志）/ 0x07 软复位
- **0x03 应答 version 字节序**：载荷 11 word（ReData[0]=size、[1]=crc32、[2..9]=version[32] ASCII、[10]=update_sta）；version 按**大端 word 构造**（对齐 0x01 IP 约定：`v[4i]<<24|v[4i+1]<<16|v[4i+2]<<8|v[4i+3]`），存储侧保持纯 ASCII，禁 memcpy 裸拷（否则上位机按 4 字节一组反转显示）。主固件与 Recovery `cmd.c` 同构。
- `iap_probe_frame`：验证帧头 + 长度（≤256）+ CRC32，返回 READY/WAIT/FAKE

**0E 远程升级协议文档待完善项**（2026-08-14，协议文档与固件实现的一致性缺口，需在协议文档侧补齐）：

| # | 缺口 | 现状 |
|---|------|------|
| 1 | **4B02 应答帧结果码** | 协议文档当前版本无此定义；固件已按扩展实现：len 0→1，1 word 结果码（0=成功含同值跳过，1=擦写错误），借鉴 4B04 应答 0/1 先例。老上位机按「B402 无载荷」解析时会多读一个 word，混合部署需联调验证（详见 doc/07 §10） |
| 2 | **4B06「进 Recovery」应答后重启** | 协议要求应答后重启；主固件实现只写 RTC 备份寄存器标志（`FLAG_FORCE_UPDATE`）未复位，实际复位由后续流程触发——协议与实现出入 |
| 3 | **4B04「准备升级」应答结果码** | 协议文档有 0/1 结果码先例；主固件实现应答无载荷（len=0），未按文档回结果码 |
| 4 | **LDI 0AH 失败码 01H** | 实现已使用（00H=成功、01H=失败：W25 保存或 Sector1 同步任一步失败即 01H），协议文档需明确该语义（与 0BH / `ldi_status_rsp_t` 既有约定一致） |

## LDI 协议 (`app_ldi.c` + `app_ldi_cmd.c`)

车道设备指示器协议，**仅网口**：`CH_ID_TCP_SERVER / CH_ID_TCP_CLIENT / CH_ID_UDP` 三通道各独立 mask，共用 RJ45 RB 与 `g_ldi_msg_queue`（`LDI_QUEUE_DEPTH=4`）。帧 STX `0xFF 0xFF`（`app_ldi.h` 固定值）+ CRC-16/XMODEM 校验；`LDI_PAYLOAD_MAX=512`。

- `ldi_handle_task`：协议处理任务（栈 256×4=1KB，帧缓冲 static；原 2KB 偏大）
- `ldi_timer_task`：周期性状态上报（栈 256×4=1KB；原 2KB 偏大）
- 13 种设备类型（`Application/Inc/LDI/app_ldi.h`，`LDI_DEV_TYPE_COUNT=13`）：RSU 0xE1 / LPR 0xE2 / VTR 0xE3 / 栏杆机 0xE4 / 车检 0xE5 / 显示屏 0xE6 / 信号灯 0xE7 / 报警器 0xE8 / VMS 0xE9 / 雨棚灯 0xEA / 雾灯 0xEB / 语音 0xF4
- `ldi_ctrl_xxx_t`：每种设备类型的控制载荷结构体（`app_ldi_cmd.h`）
- `app_vms_ctrl`：VMS 情报板子模块，将 LDI 命令翻译为 `app_render()` 调用
- 复合指令模块：支持多设备类型组合控制（1BH 命令）

## 青海协议 (`Application/Src/ProtocolParser_QingHai/app_qh_proto.c`)

青海高速费显协议。帧定界 `'{'...'}'`（len 字段 1B → 帧 ≤259），`QH_PAYLOAD_MAX=259`、`QH_QUEUE_DEPTH=3`（静态 SRAM）。`RB_PROVIDE_WEAK` 提供 RS485+RS232 双 RB，绑定 `CH_ID_RS485` + `CH_ID_RS232`（`sw_app_initcall` 自注册 `qh_proto_init`，与 IAP/LDI 同经链式 probe）。语音命令经 `dev_rs232_voice` 旁路 USART6（`app_qh_proto_voice.c`）。

## 山东协议 (`Application/Src/ProtocolParser_ShanDong/app_sd_proto.c`)

山东车道费额显示器通信协议（协议文档编号 39）。帧 `'{' + 命令字('1'~'5','7','8') + 二进制 len + 参数 + '}'`（无 BCC；**与青海完全同构**），`SD_PAYLOAD_MAX=259`、`SD_QUEUE_DEPTH=3`（静态 SRAM，与青海同 801B）。绑定 `CH_ID_RS485` + `CH_ID_RS232`（`sw_app_initcall` 自注册 `sd_proto_init`）。目标屏幕 192×96（FONT_16 6 行，协议行号 1~5 全覆盖）。

- `'1'` 全屏单色（01红/02绿/03黄）→ 整屏 `dev_display_fill`；`'2'` 取版本号 → 裸 ASCII 应答 PROGRAM_CODE（协议未定义应答格式）；`'3'` 单行（颜色'0'~'2' + 行号'1'~'5' + GBK 文本，先清行再渲染）；`'4'` 全屏可编辑（颜色 + X/Y 坐标 + 文本，先清屏后整屏 word_wrap，0x0A 回车由渲染引擎换行、0x0D 被跳过）；`'5'` 清屏；`'7'` 亮度（'0'~'5'：0=恢复光敏任务自动调光，1~5=挂起光敏任务 + 硬件档 {3,4,5,7,8}）；`'8'` 外设（bit0 绿灯/bit1 红灯/bit2 黄闪报警，红灯优先，PD14/PD15）。无 `'6'` 命令。除 `'2'` 外协议未定义应答 → 不回（对齐青海单向模式）。
- **帧头冲突纪律**：命令字 '1'~'5','7','8' 全部落入青海 probe 命令集（'1'~'9','A','B'），全协议 Makefile 构建下青海 probe 先注册（源码收录序 qh 在前）先认领山东帧——'3'/'4'/'5' 语义与青海巧合一致，'1'/'2'/'7'/'8' 语义分歧（山东 '1'=全屏单色 vs 青海 '1'=主机查询；'2'=版本 vs 自检；'7'=亮度 vs 文明语音；'8'=外设 vs 亮度）。**量产必须 EIDE 目录排除与青海/四川MTC 互斥**（doc/05-01 §6）；当前 `.eide/eide.yml` Debug 目标已排除山东目录（与青海/四川MTC 同列，Debug 保留贵州），山东量产目标须启用山东并排除青海+MTC。编译期互斥守卫 `g_brace_proto_guard` 链接期兜底强制（EIDE 多 `{` 族编入即 `multiple definition` 报错；Makefile 全协议构建经 `-DSTD_ALL_PROTO` 豁免）。
- 上电画面「山东省 高速公路 欢迎您」已实现：`app_sd_proto_default.c` 经 `app_default_display_register` 注册（`sw_app_initcall`），显示「山东省 高速公路 欢迎您」（FONT_16 居中黄字；UTF-8 字面量，`FONT_ENC_UTF8` 渲染）。

## 贵州协议 (`Application/Src/ProtocolParser_GuiZhou/app_gz_proto.c`)

贵州常规费显协议（协议文档 06 贵州常规费显协议，2020-06-17 修订）。帧 `'{' + 命令字('1'~'9','A','B',0x01,0x02) + 二进制 len + 参数 + '}'`（无 BCC；**与青海完全同构**），`GZ_PAYLOAD_MAX=259`、`GZ_QUEUE_DEPTH=3`（静态 SRAM，与青海同 801B）。绑定 `CH_ID_RS485` + `CH_ID_RS232`（`sw_app_initcall` 自注册 `gz_proto_init`）。13 命令：'1' 主机查询（仅回「正常」帧 `7B 31 01 00 7D` 至源通道）、'2' 自检（黄色全屏 + 语音「系统正在加电自检」）、'3' 单行（len 3~18，先清行再渲染）、'4' 全屏可编辑（len 4~86，**按 X/Y 坐标渲染** word_wrap=true，len>71 文本截断 64）、'5' 清屏、'6' 固定格式（客/货行文裸机对齐：客车 车型/金额/余额/信息1，信息2 不显示；货车 有超重→金额/余额/总重/超重、无超重→车型/金额/余额/总重；金额 ≥0.5 元同步费额语音；金额/重量整数运算分/吨分无浮点）、'7' 礼貌用语语音（'0'~'3'，'0'=「您好！欢迎行驶贵州高速公路」）、'8' 亮度（0=恢复光敏任务，1~5=挂起光敏 + 硬件 {3,4,5,7,8}）、'9' 音量（1~5 → 语音板 {1,3,5,7,9}）、'A' 外设（bit0 绿/bit1 红/bit2 黄闪，**红优先**）、'B' 费额语音（金额 ASCII 串→分，≥0.5 元播报，不显示）、0x01 全屏点亮（01红/02绿/**03黄**）、0x02 版本号（串口回裸 ASCII PROGRAM_CODE + 屏幕红字「版本:PROGRAM_CODE」FONT_SELF_ADAPT 居中）。无上电画面（不建 default 文件）。
- **帧头冲突纪律**：命令字 '1'~'9','A','B' 全部落入青海 probe 命令集（**完全重叠**），全协议 Makefile 构建下青海 probe 先注册（源码收录序 qh 在前）先认领贵州帧——'1'/'3'/'4'/'5'/'7'/'8'/'9'/'B' 语义基本一致，'2'/'6'/'A' 细节差异（贵州 '2'=自检黄屏+语音、'6'=固定格式行文不同、'A'=红优先）。**量产必须 EIDE 目录排除与青海互斥**（doc/05-01 §6）；当前 `.eide/eide.yml` Debug 目标已启用贵州（排除青海/山东/四川MTC）。编译期互斥守卫 `g_brace_proto_guard` 链接期兜底强制（EIDE 多 `{` 族编入即 `multiple definition` 报错；Makefile 全协议构建经 `-DSTD_ALL_PROTO` 豁免）。0x01/0x02 二进制命令字青海/山东/四川MTC probe 首命令字快拒，无冲突。金额串转分 `gz_amount_to_fen`（整数部分×100 + 小数前两位，`%.2f` 语义）。宿主推演 `~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/gz_frame_sim.py`。

## 云南协议 (`Application/Src/ProtocolParser_YunNan/app_yn_proto.c`)

云南常规费显协议（协议文档 01 云南常规费显协议-云南LED费显P5，2022.7.5，云南弥玉项目 2022-S134）。帧 `'{' + 命令字('1'~'9','A','B',0x01,0x02) + 二进制 len + 参数 + '}'`（无 BCC；**与青海/贵州完全同构**），`YN_PAYLOAD_MAX=259`、`YN_QUEUE_DEPTH=3`（静态 SRAM，与青海同 801B）。绑定 `CH_ID_RS485` + `CH_ID_RS232`（`sw_app_initcall` 自注册 `yn_proto_init`）。13 命令：'1' 主机查询（回「正常」帧 `7B 31 01 00 7D` 至源通道，恒回正常）、'2' 自检（**复用 app_factory_test.c 老化循环显示序列**——整屏单字居中，字号 {FONT_16/24/32}×字体 {ST/FS/KT/HT} 循环「重庆创迪科技发展有限公司设备老化测试」，每 5s 播报「系统正在自检」，一次性任务 `yn_selftest_task` 防重入，**可被下一帧命令打断**，打断退出不清屏）、'3' 单行（len 3~18，**FONT_24 渲染**行高 24px——协议 24 点阵，先清行再渲染；行号 '1'~'5' **全按协议接受**，行 5 在 96px 屏高下「执行但不落屏」——渲染调用照常发出、渲染层越界早退）、'4' 全屏可编辑（len 4~86，**按 X/Y 坐标渲染** word_wrap=true，FONT_24，0x0A 换行 0x0D 跳过）、'5' 清屏、'6' **单行清除**（'1'~'5'；与贵州 '6' 固定格式语义不同）、'7' 礼貌用语语音（'0'~'3'，协议原文云南文案）、'8' 亮度（**0x00=NUL=恢复光敏自动调光、'1'~'8' 恒等映射硬件档并挂起光敏任务**，8 最亮）、'9' 音量（1~5 → 语音板 {1,3,5,7,9}）、'A' 外设（bit0 绿/bit1 红/bit2 黄闪，**红优先**）、'B' 费额语音（金额 ASCII 串→分，**0 元不播**、小数播小数末位 0 剔除，不显示）、0x01 全屏点亮（**01红/02绿/03黄/04蓝/05紫/06青/07白**——DATA0 与 display_color_t 枚举恒等，P5 全彩屏 8 色除黑）、0x02 版本号（串口回裸 ASCII **PROGRAM_CODE**）。**无上电效果**：不建 default 文件、不注册默认显示（使用固件默认显示，用户决定 8）。
- **帧头冲突纪律**：命令字 '1'~'9','A','B' 全部落入青海 probe 命令集（**完全重叠**），全协议 Makefile 构建下青海 probe 先注册（源码收录序 qh 在前）先认领云南帧（与贵州处境一致）。**量产必须 EIDE 目录排除与青海/山东/贵州/四川MTC 互斥**（doc/05-01 §6）；当前 `.eide/eide.yml` Debug 目标已启用四川MTC、**排除云南**（`Application/protocol/ProtocolParser_YunNan`，与青海/山东/贵州同列）。编译期互斥守卫 `g_brace_proto_guard` 链接期兜底强制（EIDE 多 `{` 族编入即 `multiple definition` 报错；Makefile 全协议构建经 `-DSTD_ALL_PROTO` 豁免）。0x01/0x02 二进制命令字青海/山东/四川MTC probe 首命令字快拒，无冲突。金额串转分 `yn_amount_to_fen`。已确认决定记录见 `doc/11_云南费显协议/README.md` §8。

## 安徽协议 (`Application/Src/ProtocolParser_Anhui/app_anhui_proto.c`)

安徽费显协议（2026-S304 费显通信协议，2026-09 接入）。帧 `5A + 屏号(01) + 命令(1B) + 数据长(1B) + 数据 + CRC(1B) + A5`（长度字段定界，帧 ≤261；**CRC 文档注明「无检验，默认为 0」→ 不校验**），`ANHUI_PAYLOAD_MAX=261`、`ANHUI_QUEUE_DEPTH=3`（**队列体 807 + cb 80 + 任务帧缓冲 269 为静态 SRAM，与青海/贵州/云南同——用户裁决 2026-09-04，CQ 才是 CCMRAM 例外**）。绑定 `CH_ID_RS485` + `CH_ID_RS232`（`sw_app_initcall` 自注册 `anhui_proto_init`）。12 命令：0x81 清屏、0x82/0x83 显示点/关闭点（默认红色；**坐标上限以模组驱动实际屏幕尺寸为准**——`dev_display_get()` 的 `screen_rows` 宽/`screen_cols` 高，协议文档 128×64 不作为标准，parse 不校验坐标、越界由执行层判界丢弃，用户裁决 2026-09-04）、0x85 静态显示（**数据区 = X(1B) + Y(1B) + GBK 文本——坐标直用像素：x=data[0]、y=data[1]，X 在前（与文档表头「（X、Y）」及 0x82/0x83 点命令同构），2026-09-15 终裁，doc/13 §1.2**；固件**不做任何行号换算**——显示位置 (0,16) 就从 (0,16) 开始显示，此前「行号换算 row=Y/16→pixel_y=row×24」与「data[0]/data[1] 对调」两轮口径均已撤销（RTT 实测「第一~四行」四帧逐字节相同 b0=00 b1=00、行号不在坐标字节，证伪行号假说）；**FONT_24 单行不换行（2026-09-09 裁决，原 FONT_16）**；字形框越屏由 `dev_display_draw_bitmap` 按屏幕交集裁剪；**帧级诊断 `ANHUI_RTT_DIAG`（默认跟随 `APP_DIAG_BANNER`，GZ_OL 同款）在 0x85 执行入口每帧打 b0/b1 数据区前两字节原始序 + x/y + 文本前 4 字节**（后续联调判据：上位机真发 (0,16) 时屏幕必须从 y=16 起、诊断行 b1=10 即实证）；0x82/0x83 本就直用像素未随动）、0x86~0x89 动态显示 1~4 行（**已接入通用滚动模块 app_scroll，2026-09-07 此前留空**——运动模式 1=从右往左/2=从左往右/3=从下往上/4=从上往下、速度 ×2ms、停留时间 2B ×100ms 解析保存 v1 忽略、文本 GBK 64B 截断；mode=0/全 0 帧=**冻结该行**（**2026-09-09 裁决：像素停在当前位置静态显示、不清除**；原「停止即清行」）；**循环滚动**（收到停止才停，越过边界从另一侧重新进入）；行区域 Y=row×24、x=0、w=屏宽、h=24（**FONT_24，2026-09-09 裁决，原 FONT_16**；行首 00/18/30/48 为按字号推导的假设，doc/13 §8 #13 待上位机联调；0x85 静态侧已改坐标直用像素、不再与此口径绑定，2026-09-15 终裁）；0x81 清屏/0x85 静态显示先停全部动态并清行（**⑤ 2026-09-08 渲染串行**：0x81/0x85/0x82/0x83 的「stop_all+渲染+commit」整段持 `app_scroll_render_lock`（锁内 stop 用 `app_scroll_stop_all_nolock` 防重入死锁），与 scroll_task 滚动渲染互斥，消除混合帧黑条；stop_all 清行语义 2026-09-09 起与 stop 冻结语义分离，两者独立实现）；机制见 doc/01_显示系统/动态滚动显示实现记录.md；parse 防御：数据长恰为 4 且非停止帧时 text 置空；**2026-09-08 权威 .doc 核对**：速度 ×2ms、停留 2B ×100ms 大端（帧例「00 10」=1.6s）、停止帧（数据长 4 全 0）、86~89 行号均与 2026-S304 文档第 8/9 节一致（行首 Y=00/10/20/30 为 16 点阵口径，24 点阵后自然为 00/18/30/48）；**运动模式取值表与停留行为文档未定义 → mode 1~4 映射为用户拍板约定、stay_ms v1 忽略，待联调校准**，doc/13 §1.1/§8）、0x92 亮度（2B 大端 ≤999；0=恢复光敏自动、**1~999 均匀映射 8 档 1~8**：`level = 1 + value*7/999`，底层 8 档已具备 `DEV_DISPLAY_BRIGHTNESS_MAX=8`，用户裁决 2026-09-04）、0x94 通行灯/报警器（01 通行灯/02 报警器 → 黄闪灯）、0x95 播放声音（编号 01~14 模板句 + 音量 0~9 + 变长参数——**音量字节解析保留、执行层忽略**，语音板不支持协议调音量，不再调用 `dev_rs232_voice_volume`，用户裁决 2026-09-04；金额按文档示例中文读数「一千零三十点四二元」，读数规则暂不修；编号 15 自由文本：参数区即全部播报内容 GBK 原样透传语音板（232_2=USART6 语音口直送，路径不变）、超 200B 截断）、0x96/0x97 播放/录制语音段号（**占位**——语音板无段号接口）。协议未定义应答 → 单向不回；波特率沿用 DIP1。
- **帧头冲突纪律**：帧头 `0x5A` 在 RS485/RS232 槽唯一（既有串口协议首字节 0x7B/0x0A/0xFF），首字节互斥快拒成立，**无需 EIDE 目录排除**；IAP `0x5A5A5A5A` 仅绑 UDP（RJ45 槽）不相交。**非 `{` 帧族 → 不定义 `g_brace_proto_guard`**，与青海/山东/贵州/云南/四川MTC 可同编共存。**内存注意（2026-09-14 更正）**：工作区 1-263 实态为 **224×64**（`MODULE_ROWS=7 × 32` / `MODULE_COLS=2 × 32`，CCM 29568B），早期按「224×128 / 59136B / CCM 占满仅余 20B」记账的表述已失效——CCM 实况（`PROTO=ALL` `DISP=1_263`：38332B，余 27204B，2026-09-17 堆修复轮）见「内存布局」小节与 doc/06-04 §7。安徽队列入 SRAM 后经链接期下限收紧（`_Min_Heap_Size` 1KB→512B、`_Min_Stack_Size` 2.5KB→2KB）+ RTT Up 缓冲 2KB→1KB 让位 2048B，PROTO=ALL/CQ 均链接通过（doc/13 §6）。待确认清单见 `doc/13_安徽费显协议/README.md` §8。

## 贵州治超屏协议 (`Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c`)

贵州治超屏协议（"TCLY" 帧族，协议文档 38 贵州 LED_情报板_协议 V1.0；源项目 `/home/yystation/Desktop/9K23881580`，2026-09-14 接入）。帧 `54 43 4C 59`（ASCII "TCLY"）+ 包序号(4) + **长度(2B 小端 = 整帧总长)** + 保留(2) + 命令字(4，低字节有效) + 载荷(变长) + `0x00` 结束符；**无校验**（文档未定义），`GZ_OL_FRAME_LEN_MIN=17` / `GZ_OL_FRAME_LEN_MAX=256`（结构合法超长帧 → `PROTO_PROBE_SKIP` 整帧消费）、`GZ_OL_QUEUE_DEPTH=3`（**队列体 792 + cb 80 + 任务帧缓冲 264 = 1136B 置 CCMRAM**——`PROTO=ALL` SRAM 余量仅数百 B，CPU 独占访问无 DMA，CQ 先例；SRAM 侧仅 mask/句柄/状态 21B）。绑定 `CH_ID_RS485` + `CH_ID_RS232` + **`CH_ID_UDP`（10011，2026-09-14 增绑，第三 mask 挂 RJ45 槽 RB）** + **`CH_ID_UDP_GZOL`（专用 UDP 业务口，2026-09-14 用户裁决落地；端口 = Sector1 `net_cfg.port`，出厂默认 9528，与 TCP 业务口同号不同协议栈）**（`sw_app_initcall` 自注册 `gz_ol_proto_init`；四 mask 共用一静态队列；`RB_PROVIDE_WEAK` **仅串口双槽**——RJ45 槽体由 IAP/LDI/CQ 提供，本模块只 acquire 不 provide，避免第二份 1536B 网口缓冲）。**端口语义**：专用口 = 设备自身 UDP 服务口（`0x40` 改端口即改此口，重启生效；源固件出厂 10028，本实现出厂 9528）；**端口字段字节序＝高字节在前 BE16**（2026-09-15 用户裁决，有意不参考协议文档：写 10028 发 `27 2C`）；**10011 绑定保留**（兼容旧上位机，应答走收包通道单播回源，两通道各自独立 src 快照）→ 联调时上位机发往 **9528（或 `0x40` 新值）**，10011 亦可用（详见 doc/14 §11/§13）。8 命令：

- `0x10` 故障查询 → **应答 2B `00 00`**（文档「无此故障硬件的位补 0」；源固件空实现不应答属缺陷，不复制）；
- `0x20` 立即显示 → 载荷 `X(2 LE) Y(2 LE) 屏宽(2，忽略) 屏高(2，忽略) 字体名(4 GBK) 字号(2，两字节均为 16/24/32) 颜色(3 RGB) 文本(GBK 变长)`；**先整屏清黑** → 按 (x,y) 渲染、`word_wrap=true` 按屏宽自动换行；**字号不再受屏高限制（2026-09-14 用户裁决）**——字形框（字号×字号）越屏由 `dev_display_draw_bitmap` **按屏幕交集裁剪绘制可见部分**（`dev_display.c:225-262`），渲染层亦不再对「行框下缘越屏但行首在区域内」的行整行跳过（`app_render.c` `cur_y >= cfg->h` 才终止）；**变更前**为整块丢弃 → 清屏后屏幕无任何内容（2026-09-14 实机「16 正常、24/32 无内容」复盘的机理 = 当时板上镜像屏体几何 32×16，非协议/字库缺陷，doc/14 §12）；字号非 16/24/32 → 整帧丢弃（源固件此处为 UB）；颜色映射 `FF0000→红 / 00FF00→绿 / 0000FF→黄（源固件口径）/ 其余→红`；**文本字面 `\n`（0x5C 0x6E）在执行层解析为换行 0x0A**（2026-09-18 用户裁决；GBK 安全行走——GBK 双字节尾字节 0x5C 不参与转义判定；`\r` 及其它转义不处理；`GZ_OL_RTT_DIAG` 打 `nl=` 计数）；
- `0x30` 清屏 → 整屏清黑 + 提交（**载荷长度不限**：源固件忽略载荷，协议文档清屏帧例本身带 17B 参数块）；
- `0x40` 修改 IP → 写 STD 中立配置 `app_board_net_cfg_update`（**写 `port`、保留 `udp_port`**）→ 应答 `0x50` 帧（14B 载荷回显）→ `osDelay(100)` → `NVIC_SystemReset()`（源固件「应答后重启」语义；STD 改 IP 统一重启生效）。**端口字段语义（2026-09-14 实机判定后由用户裁决并落地）**：源固件 port＝设备自身 UDP 口（`UDP_SERVER.h:11` `#define UDP_LOCAL_PORT F407_PORT` → `UDP_SERVER.C:418` `udp_bind`）；本实现写入 `net_cfg.port`，重启后由**两个消费者**应用：① `app_net_boot.c` → `app_tcp_server_set_port`（TCP 业务口）；② **`app_udp.c` `_udp_gzol_read_port` → `CH_ID_UDP_GZOL` 专用 UDP 业务口**（`app_boot.c` 在 `app_net_boot_apply()` 后 `app_udp_gzol_start()` 启动）→ **改端口对 GZ_OL 的 UDP 服务口生效**（同号不同协议栈）。回退/冲突策略：`net_cfg.port` 空/0 → 回退 9528 + RTT 告警（不静默绑 0）；与 10011 或 CQ 业务口同号 → 跳过本实例绑定 + RTT 告警（10011 场景 GZ_OL 仍由 `CH_ID_UDP` 承载）。**0x50 修改应答是回显请求、不能作为落盘证据；落盘/服务口核验用 0x70 搜索应答 + 新端口实发**。**端口字段字节序＝高字节在前 BE16（2026-09-15 用户裁决：有意不参考协议文档/源固件——写 10028 发 `27 2C`，发 `2C 27` 被解析为 11303；`0x40`/`0x50`/`0x70` 同序；仅此字段，帧长与 0x20 的 X/Y/屏宽/屏高仍 LE16，doc/14 §13.5.1；改前为 LE16，曾是 2026-09-14「E 类」判定的依据，现已由本次裁决取代）**。**唯一代码侧的「端口静默回滚」机制** = 启动时 `app_net_boot.c` `accept_write`（记录 CRC 坏 / **IP 全 0** / port=0 → 用本构建默认**同时覆盖 IP 与 port**；`0x40` 帧若把 IP 字段写成 `0.0.0.0` 即触发，现场由 `[diag] netcfg INVALID at boot … -> accept_write defaults` 自证）。记录布局/字段归属均未变（不占用 CQ 专有 `udp_port`），详见 doc/14 §13/§13.5 与 `.analysis/9k23881580/port_10028_diagnosis.md`；
- `0x50` / `0x60` 搜索 → 读 Sector1 net_cfg 组 14B 载荷（ip4+mask4+gw4+port2 小端）→ 应答 `0x70` 帧，**单播回源**（`0x50` 作入站触发是源固件行为：源把搜索实现在 `0x50` 上，本实现两者都接受以覆盖文档版与固件版上位机）；
- `0x70` 搜索应答（仅出站，入站静默丢弃）、`0x80` 创迪扩展全屏纯色（三字节 RGB → 整屏填充；**副作用：挂起光敏自动调光 + 亮度 8**；未识别颜色不动屏幕——源固件行为；收到任意帧即恢复自动调光）；
- 非法命令静默丢弃（协议未定义错误应答）。

上电画面 `app_gz_ol_proto_default.c`（`app_default_display_register`）：三行绿字 FONT_16 黑体「贵州省 / `  高速公路` / `     欢迎您!`」（前导空格与源固件一致）。渲染互斥：`0x20`/`0x30`/`0x80` 的「清屏+渲染+提交」整段持 `app_scroll_render_lock`（与安徽滚动渲染串行，`doc/13` ⑤ 纪律）。**帧头冲突纪律**：首字节 `0x54` 在串口槽（`0x0A`/`0x5A`/`0x7B`/`0xFF`）与网口槽（`0x5A5A5A5A`/`FFFF`/`'{'`/CQ 12B `FF FF`）均唯一 → **双向首字节快拒成立、无需 EIDE 目录互斥、不定义 `g_brace_proto_guard`**（非 `'{'` 帧族；网口槽已实测绑定后复核：IAP/LDI/CQ 三 probe 对 `0x54` 快拒、本 probe 对 `0x5A`/`0xFF`/`0x7B` 快拒，`.analysis/9k23881580/gz_ol_udp_binding_report.md` §2）。波特率 9600 8N1 → **现场 DIP1 必须置 OFF**。宿主推演 `~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/gz_ol_frame_sim.py`（**122 用例通过 / 失败 0**，2026-09-18 新增 §10「0x20 文本字面 `\n` → 0x0A（GBK 安全行走 + 尾字节 0x5C 陷阱）」17 条；此前 2026-09-15 按「高字节在前」更新 §7/§9 并新增「`27 2C`→10028 生效 / `2C 27`→11303 交换值 / `0x40`→`0x70` 端口回读一致性」回归；此前含 2026-09-14 新增「0x20 字号 16/24/32」回归、「`0x40` 端口解析与落库契约」、「`0x40` → `0x70` 端口生效契约（专用 UDP 业务口 + 10011 并存）」、「§9 现场『改端口不生效』状态判别契约（A 旧镜像 / B 启动回滚 / C 设计分支 / D 网络侧 / **E 端口字段字节序——原判「上位机大端编码」，2026-09-15 用户裁决转为设计口径：固件改高字节在前**）」）。详见 `doc/14_贵州治超屏协议/README.md`。

## 云南治超屏协议 (`Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c`)

云南治超屏协议（协议文档《车道费额显示器通信协议》YN_1.3.0，2021-11-24 治超屏裁剪版；模块前缀 `yn_ol_`，与云南常规 `yn_` 严格区分）。**2026-09-17 接入落地**，记录见 `doc/15_云南治超屏协议/README.md`。

> **2026-09-18 现场问题端到端修复（实机，doc/15 §12）**：「TCP 连上后无响应 / 需重启板子」三类根因 ——
> ① **发作镜像漏编 `{` 帧族协议**（EIDE Debug `excludeList` 排除 `ProtocolParser_YunNan_Overload`；TCP 照常
> accept/recv 但无人消费且**全链路零日志**）⇒ 新增 `[disp] ch=N has NO protocol bound …` 告警；
> ② accepted conn **无 keepalive** ⇒ 半开/孤儿连接永久占死串行服务循环 ⇒ 加 `tcp_server_keepaliveinit()`
> （参数对齐 TCP Client），半开**自愈 17.0s**（修复前 30s 零自愈）；③「板子自己死机 ~30s 后重启」
> **实测为外部 EIDE 重编+重烧**（构建产物被外部改写 → 板子失联重启且换镜像；无 `[err]`/无 HardFault/
> `tick` 连续/`bkp1=0` 非 IWDG）。另新增横幅 `reset`/`bkp1(iwdg_cnt)` 复位取证行（`pl_sys_reset_cause()`）。

- **帧**：`'{'` + 命令字(1B) + 参数长度(1B **二进制**) + 参数(变长) + `'}'`（与云南常规完全同构、无校验；帧 ≤259）；probe = 首字节快拒 + 命令字白名单（`yn_ol_cmd_from_byte` 与 parse 共用单一真源）+ 长度/尾字节双重校验。
- **命令集（19 个命令字）**：`'1'` 主机查询（回 `7B 31 01 00 7D`）、`'2'` 自检（数字 1~9 交替，惰性任务可被下一帧打断、无语音）、`'3'` 单行（颜色+行号'1'~'5'+GBK）、`'4'` 全屏（颜色+X+Y+GBK，word_wrap）、`'5'` 清屏、`'8'` 亮度（`0x00`/`'0'` 自动、`'1'`~`'8'` 手动）、`'A'` 外设（bit0 绿/bit1 红/bit2 黄闪，红优先）、`0x42`~`0x46`+`0x50` 六行清除、`0x47` 改 IP、`0x48` 查 IP → `0x51` 应答、`0x49` 屏体参数（字体+字宽，**持久化于 W25Qxx 独立扇区、上电装载**）、`0x01` 全屏点亮（1~7 色）、`0x02` 版本号（裸 ASCII PROGRAM_CODE + 屏显「版本:…」）。**'6'/'7'/'9' 文档明示治超屏不开发（语音 + 固定格式）→ probe 快拒**。
- **网络配置**：`0x47`/`0x51` 的 14B 载荷 = ip4+mask4+gw4+port2，写/读 Sector1 `net_cfg.port`（`udp_port` 保留）；**端口字段字节序 = 高字节在前 BE16**（写 9528 发 `25 38`）——**已裁决**（2026-09-17 裁决 2，转正；依据同厂 GZ_OL doc/14 §13.5.1 + 本固件 LDI 12H/IAP 0x01 既有口径）；`0x47` 应答 `0x51`（14B 回显）后 `osDelay(100)` + `NVIC_SystemReset()`（STD 改 IP 重启生效，对齐 GZ_OL `0x40` 先例）——**已裁决维持**（裁决 3）。
- **`0x49` 屏体参数持久化**（**裁决 5**）：字体+字宽写入 **W25Qxx 独立 4KB 扇区 `capacity - 12288`**（倒数第三个；LDI 占 `capacity-4096`、app_render 显示持久化占 `capacity-8192` → 三者各占一扇区，避免 `dev_w25qxx._write` 整扇区读-改-写互擦）；记录 12B = magic `0x594E4F4C` + version + font_type + font_size + CRC32（仿 `app_ldi_cfg.c` 范式）；`sw_app_initcall` 初始化末尾装载、`0x49` 执行时落盘；记录空/损坏 → 静默/告警回默认，写失败 → 运行态仍生效仅告警，**不阻塞、不重启**；SRAM 侧仅 12B 记录缓冲，无整扇区镜像。
- **通道**：**RS485 + RS232 + TCP Server + TCP Client 四通道四 mask 共用一队列**（队列体+cb+任务帧缓冲 **1152B 置 CCMRAM**，**裁决 1**：现场可能用网口控制）。**与 CQ 的 `{` 同首字节竞争靠「通道掩码隔离」解决、不靠源码收录序**：`app_dispatch.c` `frame_dispatch_task` 按 `ch_proto_map[ch->ch_id]` 取该通道绑定协议链，而 **CQ 只绑 `CH_ID_UDP` + `CH_ID_UDP_CQ`、不绑 TCP** → TCP 双通道上 CQ probe 根本不被调用；本模块**不绑 `CH_ID_UDP`（10011）** → CQ 量产行为逐字节不变。反向安全：CQ JSON 第二字节恒为 `'"'` 不在本 probe 白名单 → FAKE 放行；半帧（avail<3）→ WAIT 且调度器 WAIT 时继续探测下一协议 → 不卡链。**未触碰 CQ 源码**。波特率 DIP1 选择（文档 9600~115200 默认 9600 → **DIP1 置 OFF**）。
- **帧族纪律**：属 `{` 帧族 → **定义 `g_brace_proto_guard` 互斥守卫**；**与云南常规 `app_yn_proto` 这一对已 `arm-none-eabi-ld -r` 双编实测报 `multiple definition of 'g_brace_proto_guard'`**（裁决 7；加 `-DSTD_ALL_PROTO` 复编后退出码 0）→ **量产（EIDE）必二选一**（**实态复核 2026-09-17 晚：`.eide/eide.yml` Debug 的 excludeList 已排除 `ProtocolParser_YunNan`、未排除本目录 ⇒ 本模块已编入 EIDE Debug 镜像**；改回则须把本目录列入 excludeList 并放行任一其它 `{` 族目录，再 `EIDE: Reload Project`）。命令字重叠：`'1'`~`'5'`/`'8'`/`'A'` 与云南常规/青海等、`0x42`~`0x45` 与 MTC；**`0x42` 同字节歧义**（本协议=第一行清除 / 云南常规=`'B'` 费额语音）；本协议独有 `0x46`~`0x51`（全协议构建下独占认领）。
- **上电默认画面：不实现**（**裁决 6**）——文档「上电显示『祝您一路平安』稍候熄灭」不由本模块实现；原 `app_yn_ol_proto_default.c`、其 `app_default_display_register` 注册与 5s 熄灭惰性任务**已删除**，Makefile / `.eide/eide.yml` / 文档表述同步清理；上电画面走 STD 现有默认显示链路（`app_default_display.c`）。
- **默认字号/字型 = 沿用 STD 工程既有默认 `FONT_16` / `FONT_ST`**（**裁决 8**，不单独修改；与同为 `{` 帧族、协议未限定字号的青海/贵州一致，也与 `app_default_display.c` 一致）；0x49 可运行时改并持久化。
- **逐帧 RTT 诊断（2026-09-17 晚 联调轮）**：`[yn_ol]` 前缀，门控 `YN_OL_RTT_DIAG`（默认跟随 `APP_DIAG_BANNER`，置 0 零输出零开销）——每帧最多两次打印：`[yn_ol] rx ch=… len=… hex=… -> sta=… cmd=… 关键字段`（≤32B 全打，超长打前 32B + `cut`）+ `[yn_ol] exec cmd=… ret=… …`（`yn_ol_execute_cmd` 改为返回状态码）或 `[yn_ol] drop …`（拒绝原因）；`'3'` 诊断另打印行号换算的 y、屏体几何与 `ON-SCREEN/PARTIAL/OFF-SCREEN(executed but invisible)` 落屏判读（16×32 屏 + FONT_16 仅 2 行可视）。同轮修复三处：**① TCP 单字节分片被丢**（`app_tcp_server.c`/`app_tcp_client.c` 的 `len > 1` → `len > 0`）；**② 调度层 WAIT 预算被 avail 增长永久重置**（`app_dispatch.c` 改「头部前缀指纹」进度判据（按 RB 分槽计时，多 RB 交替不互相重置）、预算 500ms→1000ms〔RLS 530B@9600 ≈552ms〕、到期打 `[disp] WAIT budget … expired` 告警后 skip 1 字节）；**③ `'3'`/`'4'` 颜色、`'3'` 行号 ASCII/二进制超集容错**（`app_yn_ol_proto_parse.c`）。判读表与现场步骤见 doc/15 §11。
- **联调第二轮：三处观测盲区补齐 + 通知投递缺陷修复（2026-09-17 晚，第二轮）**：症状（TCP 连上后首条命令 RTT 无输出、**再点一次「连接设备」才解析**；0x47 完全无输出）在第一轮修复镜像上仍现 ⇒ 不猜根因，补齐三层可观测性并修一处真实缺陷：
  **① 通道层**（`app_tcp_server.c`/`app_tcp_client.c`）新增门控日志 `[tcp_srv]`/`[tcp_cli]`（`listen`/`bind FAIL`/`accept ip:port`/`recv len+≤16B hex`/`close reason=`；`TCP_SRV_RTT_DIAG`/`TCP_CLI_RTT_DIAG` 默认跟随 `APP_DIAG_BANNER`；客户端连接失败同错误码只打一行）；
  **② 调度层**（`app_dispatch.c` `frame_dispatch_task`）新增 `{`（0x7B）帧**无任何协议消费**时的 probe 决策日志 `[disp] probe idx=… sta=WAIT/FAKE(…) avail=… head8=…`（限速 = 三元组变化 + ≥ `PROBE_DBG_MIN_MS` 200ms，消费即失效；probe 仍纯函数）；
  **③ 通知投递修复**（`app_channel_dispatch`）：原 `timeout=0` 且忽略返回值 → 队列满**静默丢通知**（数据已进 RB 但无人唤醒分发任务，等下次通知才顺带处理）⇒ 改为「立即 + `DISPATCH_NOTIFY_RETRY_MS` 20ms 重试 → 仍失败 `notify_drop++` + 门控告警」，新增 getter `app_dispatch_notify_drops()` 并进 post-boot 体检行（`qfull=… resync=… notify_drop=…`）；**该修复非诊断、常开**（text +48B / bss +4B）。
  **0x47 链路已复核**（白名单 → `declared ≥ 14` → BE16 → 两条 `[yn_ol] 0x47 …` 日志 → 写 Sector1 → 回 0x51 → `NVIC_SystemReset()`），**全路径有日志、代码无需改**。三段判据（有 accept 无 recv / 有 recv 无 `[yn_ol] rx` 看 `[disp] probe` / `notify_drop` 非 0 / 有 rx 无 exec 看 drop）与现场 8 步抓取见 doc/15 §11.3~§11.4。
- **实测（2026-09-17 裁决轮）**：A/B 增量 text **+3520** / rodata **+680~688** / data +0 / ccmram **+1152** / bss **+36**（SRAM 合计 +40B）；三口径 SRAM 余量 **400B**（ALL/1_263）/ **4680B**（CQ）/ **384B**（ALL/22_1665，余量均 >0）；三口径零新增告警（3 条既有 HAL）；宿主推演 `.analysis/yn_ol/yn_ol_frame_sim.py` **184 用例通过 / 失败 0**（§9 TCP 绑定与 CQ 链式竞争 38、§10 `0x49` 持久化契约 24、§11 四通道应答回源 7、§12 源码断言 6、**§13 联调轮容错/诊断断言 6 + §3.24~3.29 编码容错镜像 6**）；8 条裁决逐条结论见 doc/15 §8。**联调轮（晚）三口径重编实测**：`ALL/1_263` text **173556** / rodata **203840** / bss 124732 / ccmram 38332 / SRAM 余 **2104B**；`CQ` text **160380** / rodata **203184** / SRAM 余 **6384B**；`ALL/22_1665`（工作区 1×5）text **173812** / rodata **204096** / ccmram 44156 / SRAM 余 **2104B**；相对 heap 修复轮基线 text **+1696** / rodata **+1248** / bss **+20**（WAIT 预算按 RB 分槽状态）/ ccmram ±0；诊断设施 A/B **Flash +9648B（text +4480 / rodata +5168）/ RAM +0**（同口径 `make APP_DIAG=0`）。**联调第二轮（晚）实测**（`ALL/1_263`，相对上条联调轮基线）：text 173556 → **174996**（+1440）/ rodata 203840 → **204848**（+1008）/ data 1672 → 1680 / bss 124732 → **124772**（+40）/ ccmram ±0，**SRAM 余量 2104 → 2056B**；其中诊断 +1392/+1008/+8/+36、**通知重试修复常开部分 +48 text / +4 bss**；`DISP=22_1665`（工作区 1×5）text **175252** / rodata **205104** / ccmram 44156 / SRAM 余 2056B；`APP_DIAG=0` 对照 text **169124** / rodata **198672**（相对上轮 diag-off 仅 +48B text / +4B bss = 该修复本身），elf 内 `[tcp_srv]`/`[tcp_cli]`/`[disp] probe`/`notify queue FULL` 字符串 0 条。三跑零新增告警（仍 3 条既有 HAL）；**未烧录、未提交、未改 git 配置**。

## 动态滚动模块 (`Application/Src/app_scroll.c`，`Application/Inc/app_scroll.h`)

通用动态滚动显示（Application 层，尺寸无关；安徽 0x86~0x89 第一个消费者，API 预留 LDI/VMS 复用）。API：`app_scroll_start(slot, x,y,w,h, text, len, dir, color, font_size, font_type, step_ms, stay_ms)` / `app_scroll_stop(slot)` / `app_scroll_stop_all()` / `app_scroll_render_lock()` / `app_scroll_render_unlock()` / `app_scroll_stop_all_nolock()`（⑤ 2026-09-08）；方向枚举 `SCROLL_DIR_LEFT=1`（从右往左）/`RIGHT=2`/`UP=3`（从下往上）/`DOWN=4`（值对齐安徽 mode 字节）。语义：**循环滚动**（越过边界从另一侧重新进入，永不自动停）；**stop 语义（2026-09-09 裁决）**：`app_scroll_stop` = **冻结**（置非活跃、不清行、不提交，像素停在当前位置；render 锁保证在途渲染完成后返回、无半帧），`app_scroll_stop_all/_nolock` = **保持清行**（0x81/0x85 依赖——内部 `_scroll_freeze_nolock`/`_scroll_stop_clear_nolock` 两路径独立，stop_all 不继承 freeze）；行区域矩形按 `dev_display_get()` 的 `screen_rows`（宽）/`screen_cols`（高）钳位；`step_ms` 最小 2ms；`stay_ms` v1 保存忽略；文本 GBK ≤64B（`SCROLL_TEXT_MAX`）拷入槽位静态缓冲。实现：专用 `scroll_task`（首次 start 惰性创建，栈 256×4=1KB，ucHeap）基础节拍 2ms（`SCROLL_TICK_MS`），空闲事件标志休眠零 CPU；每槽 `acc_ms` 相位累积，每 step_ms 推进 1px；渲染 = `dev_display_fill` 清行 → 逐字形 `app_render_draw_glyph_clipped`（app_render 导出：复用 `_flash_addr`/`_packed_glyph_bytes` 读字模、逐像素屏幕+裁剪矩形双判界平滑裁剪，字模缓冲为调用栈 128B）→ ④a 整拍一次 `dev_display_commit_frame_rect`（**活跃槽**矩形并集包围盒——冻结槽不参与，pixel_map 冻结内容天然保留；无活跃推进不提交，并集无效回退全量；stop_all 清行同用矩形提交）。⑤ 渲染互斥：scroll_task「渲染+commit」整段持 `s_render_mutex`（锁序 render → slot 单向无死锁），安徽 0x81/0x85/0x82/0x83 的「stop_all+渲染+commit」整段持同一锁（锁内 stop 用 `app_scroll_stop_all_nolock` 防重入），滚动×静态渲染串行消除混合帧黑条。槽位单写者×单读者经 `osMutexNew` 串行（渲染在持锁内，stop 冻结不清行、stop_all 清行提交）；W25 读互斥由 dev_w25qxx 锁保护。内存：`s_slots[4]` 360B + 事件/双互斥/任务句柄 16B = 376B 静态 SRAM（无 CCM 对象）；scroll_task 栈惰性支出。实测增量（2026-09-07 落地）text +1912 / bss +376；（2026-09-08 ④a/⑤）text +672 / data +8 / bss +8；（2026-09-09 FONT_24+停止冻结）text +64 / data 0 / bss 0（stop 拆双路径）。详见 `doc/01_显示系统/动态滚动显示实现记录.md`。

## 四川三协议（`ProtocolParser_SiChuang_{ETC,MTC,Overload}`）

三个四川地区协议模块均绑定 `CH_ID_RS485` + `CH_ID_RS232` 双通道、queue 深度 3（静态 SRAM），与青海同构（acquire → register → bind → set_frame_queue）。协议文档提取自 1D/1E/1F，要点：

- **ETC（1D）**：帧 `0A + 显示方式(00/01) + 行号(00~06) + 数据 + 0D`（数据**变长 0x0D 定界**：单行 ≤24B、全屏 ≤145B，无固定 56B——上限对齐 9K1F212701 etc.c `cmd_etc_disPlay_ctrl` 0x0D 扫描索引 ≤148，GBK）；`0A 36/37/38/39 0D` 灯控 → `dev_io_lane_light`/`dev_io_flash_light` 且同步显示颜色（fontColor 语义）；`0A 40 XX YY 0D` 亮度（XX=00 自动调光）；`0A 50 0D` 心跳（解析保留，识别后丢弃）。应答 `0A 00/01/02 0D`（收到即回，心跳不回）。**0x20 清屏（行号 0 全屏）、0x30 初始化 → 软件复位**。全屏渲染按 9K1F212701 `MakeSixteenLattAll` 语义：清屏后自第 1 行第 1 列按屏宽自动换行。**心跳超时显示已停用（2026-08-17）**：原独立计时任务（栈 256×4，5 分钟无有效帧 → 「ETC车道关闭」）已 #if 0、任务不再创建；黄闪 0A 38 开启后的 10 秒自动关闭依赖同一任务 tick，一并失效——开启后须 0A 39 显式关闭。恢复方法见 `app_sc_etc_proto.c` 注释。
- **MTC（1E 方案二）**：帧 `'{' + 命令('1'~'9','A') + 参数 + '}'`——**'}' 定界变长，无长度字段、无 BCC**（对齐 9K1F212701 mtc.c：各命令 handler 逐字节扫 '}' 定帧，参考扫描上限 228；本设备 probe 上限 `SC_MTC_PAYLOAD_MAX=74B` 队列约束）。字段偏移按变长重算：'3' 单行 = 行号[2]+变长文本[3..]，'4' 全屏 = 变长文本[2..]（先清屏后整屏 word_wrap 渲染：w=screen_rows 屏宽、h=screen_cols 整屏高、word_wrap=true，渲染引擎按当前字号自动折行——2026-08-17 修复，原 16B/行切 ≤4 行且单行不换行，第一行超宽溢出被裁、超 64B 被丢弃；'3' 单行保持 word_wrap=false + h=当前字号截断语义），'6' 固定格式 = 类型[2]+数字串（客车 ≥11B / 货车 ≥20B），'1','2','5'→3B、'7'固定→4B（'78' 自定义文本变长）、'8','9'→4B。带 BCC 变体不加判别：BCC 字节视为内容尾部（与参考语义一致），BCC 不校验。主机查询 `0A 46 0A → 0A 64 0A`、清屏 `0A 46 0D`；附加 7B 40~45 原始帧族（同样 '}' 定界）：40 改波特率（`app_uart_baud_apply`）、41 点阵大小、42 字体、43 协议类型（仅记录）、44 全屏点亮、45 版本号 → `SC_FX_P7.62_1.0`。'6' 字段布局按 9K1F212701 mtc.c。'7' 语音经 `dev_rs232_voice`（固定用语 GBK 直送，动态变量不拼读）。7B 46（1280B 载荷）不实现（超 RB 768）。宿主推演 `~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/sc_mtc_frame_sim.py`（`--old` 可复现旧定长乱码链）。
- **治超（1F 3.5.1）**：帧 `FF + 长度(07~FF，1 字节；0xFE 显式排除给 RLS) + 命令 + 亮度 + 数据(可变长) + BCC(五字段异或) + FF`；**80 全屏显示、81~88 八行显示（9K1F212701 语义：行数据变长 = 总长-6，≤24B 截断、不足不补空格、先清行再渲染）**、94 清屏、96 亮度（00=自动调光，非 0 按 lightLev=(val+1)/32 映射 1~8 档）、99 通行灯（同步显示颜色）、98 黄闪；查询 A0 → A1~A8 每行独立帧（固定 16B 应答兼容工具，参考项目无应答实现）、B6/B9/B8 → 回显当前状态。**与 RLS 区分**：RLS 第二字节 0xFE(254) 被治超 probe 显式排除（长度上限放宽至 FF 后 0xFE 落入合法区间，不排除会把 RLS 帧吞掉并 WAIT 卡死链式），RLS probe 亦要求第二字节 0xFE，双向快拒成立。
- **帧头冲突纪律**：MTC '{' 与青海 '{' 同帧头——MTC probe 以「'}' 定界变长扫描 + 上限 74B」认领，完整青海帧由青海 probe 先认领（qh_proto_init 先注册，源码收录序 QH 在前）；残余风险两类：①青海 '3' 帧总长 ≤74 且数据段含 '}' 字节（GBK 尾字节可为 0x7D）且半帧到达时被 MTC 截断认领，②青海 'A'/'B' 空数据帧与 MTC 7B 41/42 同长（QH probe 先认领）；量产由 EIDE 目录排除纪律兜底（doc/05 §6）；编译期互斥守卫 `g_brace_proto_guard` 链接期兜底强制（EIDE 多 `{` 族编入即 `multiple definition` 报错；Makefile 全协议构建经 `-DSTD_ALL_PROTO` 豁免）。

## RLS 协议 (`Application/Src/RLS/app_rls.c`)

重庆高速二代费显协议。帧头 `0xFF 0xFE` + 尾 `0x0D 0x0C`，`RLS_PAYLOAD_MAX=530`（帧头 6B + bitmap 512B + BCC 1B + 尾 2B）、`RLS_QUEUE_DEPTH=2`。仅绑定 RS485（`RB_PROVIDE_WEAK` 提供 RS485 RB），BCC 校验用 `Kernel/Inc/bcc_utils.h`（`bcc_calcu`）。`sw_app_initcall` 自注册 `rls_module_init`，`rls_handle_task` 栈 256×4。

**干接点车道状态显示（2026-09-09 接入）**：SW1=ETC专用（绿）/ SW2=ETC/人工（绿）/ SW3=车道关闭（红），低有效=闭合。`rls_handle_task` 帧队列等待由 `osWaitForever` 改为 100ms 超时，超时节拍调用 `rls_dry_contact_poll`（`app_rls_cmd.c`，无新任务/新文件）。两级采样消抖（连续两拍 100ms 一致才提交，`dev_key_get_state` 为直读 GPIO 无软件滤波）；仅在状态变化瞬间全屏渲染一次（FONT_24 居中，UTF-8 经 FONT_ENC_UTF8 路径），此后上位机 bitmap 帧可正常覆盖；三路全开→全屏清黑（清除干接点残留）；多路闭合优先级 车道关闭 > ETC专用 > ETC/人工（安全语义）。渲染与提交整段持 `app_scroll_render_lock` 与滚动渲染串行（⑤ 渲染互斥）。详见 `doc/03_重庆高速二代费显协议/README.md`。

## AH MQTT 协议 (`ah_mqtt.c` + `ah_mqtt_cmd.c`)

AH 平台 MQTT 应用层协议。固定帧格式（21B~533B），通过 MQTT topic 路由命令。

**数据结构**（`ah_mqtt.h`，均为 packed 结构）：
- `topic_info_t`：`station_hex(8) + lane_hex(2) + device_type(2) + device_id(2)`
- `notify_id_t`：日期+设备信息+发送计数
- `sign_up_t`：设备签到（含软硬件版本号、协议版本、厂商信息）
- `state_report_t`：定期状态上报

**命令处理器**（`ah_mqtt_cmd.h`，通过 `g_ah_mqtt_cmd_table[]` 跳转）：
- `/ASK/board/NULL` → `cmd_display`：UTF8 文本渲染
- `/ASK/display/clean` → `cmd_fill`：全屏填充颜色
- `/ASK/op/restart` → `cmd_restart`：应答后 `NVIC_SystemReset()`
- `/ASK/op/checktime` → `cmd_checktime`：更新时间戳

**注意**：模块 initcall 当前已注释（`// sw_app_initcall(ah_mqtt_module_init)`），`app_mqtt_start()` 也未调用，AH MQTT 链路未激活。

## 网络子系统

### PHY→MAC→LwIP 依赖链

```
Device 层:  dev_dp83848  (PHY 寄存器操作, 自动协商, 链路状态检测)
              └─ 注入 IO 上下文 (pl_eth_phy_io_*)
Platform 层: pl_eth  (ETH MAC/DMA, MDIO 读写)
              └─ 注册 PHY 链路查询回调 (pl_eth_set_phy_link_fn)
             pl_net  (LwIP netif 初始化, IP 配置, 链路监听器注册)
              └─ ethernet_link_thread 轮询链路状态 → 通知 pl_net_link_listener[]
Application: app_udp / app_tcp_server / app_tcp_client / app_mqtt
              └─ pl_net_register_link_listener → 链路断开回调重建连接
```

### 通道生命周期（以 UDP 为例）

```
udp_task: 绑定端口→循环{创建 udp_connect_task→等待信号量→销毁→延迟重连}
  └─ udp_connect_task (每个客户端一个):
       1. udp_channel_init: ch.ops=udp_ch_ops, state=UP, app_channel_register
       2. 循环 netconn_recv → 提取源IP/端口 → app_channel_dispatch
       3. udp_channel_deinit: ops=nullptr, state=DOWN, conn=NULL, app_channel_register(NULL)
       4. osSemaphoreRelease 通知 udp_task 重连
```

**IP 隔离**：`udp_channel_t` 存储源 IP 为 `uint8_t src_ip[4]` 字节数组，而非 LwIP `ip_addr_t`，避免 Application 层暴露 middleware 类型。

### dev_dp83848 PHY 驱动 (`Device/Network/dev_dp83848.c`)

PHY 寄存器操作、自动协商、链路状态检测。通过 IO 上下文注入（`pl_eth_phy_io_*` 函数指针）与 MAC 层解耦。`dev_dp83848.h`（~249 行）定义完整 PHY 寄存器映射和 15+ 操作 API。

### LwIP 配置要点 (`Platform/Inc/lwipopts.h`)

关键参数（139 行，覆盖 `opt.h` 默认值的部分与 CubeMX 生成配置）：
- **内存**：`MEM_SIZE=12KB`；`MEMP_NUM_PBUF=16`、`PBUF_POOL_SIZE=16`（未覆盖，取 `opt.h` 默认值；SRAM 水位 87-94%，每 +16 池约 +4KB，另行核算后才可调）
- **TCP Client 远端未配置不连（2026-09-18 解耦）**：`g_tcp_client` 编译期默认 `{0,0,0,0}:0`（不再硬编码 `192.168.2.17:9529`）。`tcp_client_task` 仍由 `app_boot` 两口径无条件 `app_tcp_client_start()` 拉起，但 IP 全 0 或 `port==0` 时**不** `netconn_new` / **不** connect，去重打一行 `[tcp_cli] idle (remote unset)` 后 1s 再看；`app_tcp_client_set_remote()`（现仅 LDI 配置装载调用）配好后下一轮自动连。已连接时 `set_remote` 仍释放 `client_disconnect_sem` 踢重连；改回哨兵则拆连接后回 idle。YN_OL 绑 `CH_ID_TCP_CLIENT` 只消费入站，**不**配置 Client 远端。
- **TCP Server 半开连接自愈（2026-09-18 YN_OL TCP 现场修复轮）**：accepted conn 现调用 `tcp_server_keepaliveinit()`（`SOF_KEEPALIVE` + `keep_idle 10000` / `keep_intvl 2000` / `keep_cnt 3`，**与 `app_tcp_client.c` 既有口径一致**）⇒ 对端「不发 FIN 就消失」（拔网线/交换机瞬断/上位机进程被杀）时服务循环 **~16s 内自愈**回 accept（实机 **17.0s**；修复前 30s 零自愈、表现为「连上没反应、必须重启板子」）。同轮 `netconn_new` 失败补 RTT 告警（原为静默 500ms 重试 ⇒ 无法区分「池耗尽=连接超时」与「循环占死=连上零应答」）。
- **LwIP 调试档位（2026-09-18）**：`LWIP_DEBUG`/`SOCKETS_DEBUG`/`TCP_DEBUG` 仍在 `Core/Inc/main.h`（CubeMX 生成区）为 `LWIP_DBG_ON`，但 `LWIP_DBG_MIN_LEVEL` 被 `Platform/Inc/lwipopts.h` 的 USER CODE 段覆盖为 `APP_LWIP_DBG_LEVEL`（默认 `0x01` = `LWIP_DBG_LEVEL_WARNING`）⇒ 滤掉 `tcp_slowtmr`/`tcp_recved` 等 LEVEL_ALL 信息行（实测 **5~17 行/秒**）；警告/错误与工程自有 `[diag]`/`[tcp_*]`/`[yn_ol]`/`[disp]` 诊断（直接 `SEGGER_RTT_printf`）全部保留。**动机**：RTT 上行仅 1KB 且 `NO_BLOCK_SKIP`（满则整条丢），噪声几十秒即灌满缓冲 ⇒ **此后一切现场诊断静默丢失**（上一轮「死机前后 RTT 写不进任何证据」的真因）。`make APP_LWIP_DBG_LEVEL=0x00` 一键恢复全量 LwIP 日志；该值进 `DEFINES` ⇒ 进 `.build_stamp` 口径指纹（见 `doc/构建开关总表.md` §5）。
- **`[disp]` 无协议承载告警（2026-09-18）**：`app_dispatch.c` `frame_dispatch_task` 在 `ch_proto_map[ch]=0`（该通道**没有任何协议绑定**，如构建期把协议目录排除掉）时按通道限速打印 `[disp] ch=N has NO protocol bound -> rx data swallowed (build/excludeList check!)`（间隔宏 `DISPATCH_NOPROTO_DBG_MIN_MS`，默认 1000ms）。此前该情形**全链路零日志**（一个协议都没被探测，连 `[disp] probe` 也不触发）——2026-09-18 现场「TCP 连上发数据零反应」的一种成因即此（EIDE Debug 排除 `ProtocolParser_YunNan_Overload` 的镜像实测复现）。
- **netconn/UDP PCB 池（2026-08-21 覆盖，修复 LDI 搜索广播丢包；2026-09-14 已重核）**：`MEMP_NUM_NETCONN=8`、`MEMP_NUM_UDP_PCB=8`（opt.h 默认 4）。dev 共存构建常驻 netconn = UDP 10011 + UDP 20103(CQ) + **UDP 9528(GZ_OL 业务口)** + TCP Server listener + TCP Client = **5**，叠加 TCP Server 已连接客户端 6；广播回退临时 conn 最多 +2（LDI/CQ 各一，常驻 conn 就绪时不占用）→ **峰值 8 恰为池容量**（不再扩池；若再新增 UDP 端口协议须先扩池或复用常驻 conn）。UDP PCB 常驻 3/8，余量充足。**新增 UDP 端口协议时仍须随通道数核算（doc/06 预算）**
- **硬件校验和**：`CHECKSUM_GEN_IP/UDP/TCP=0`、`CHECKSUM_CHECK_*_HW` 卸载到 MAC
- **UDP 广播**：`LWIP_BROADCAST` 未定义；实际 `IP_SOF_BROADCAST=1` + `IP_SOF_BROADCAST_RECV=1`（IAP 升级依赖）。`app_udp_broadcast`/`app_udp_cq_broadcast` 复用常驻通道 conn（`udp_task`/`udp_cq_task` 创建时已 `ip_set_option(SOF_BROADCAST)`）直接 `netconn_sendto` 广播，常驻 conn 未就绪（断链重建窗口，deinit 置 conn=NULL）才回退临时 conn（2026-08-21）
- **MQTT**：`LWIP_MQTT` / `MQTT_MAX_IN_FLIGHT` 未定义；USER CODE 1 定义 `MQTT_REQ_MAX_IN_FLIGHT=16`、`LWIP_SO_RCVTIMEO=1`
- **TCP**：`LWIP_TCP_KEEPALIVE=1`；`TCP_MSS=536`（未覆盖，取 `opt.h` 默认值）
- **线程**：`TCPIP_THREAD_PRIO=24`（=osPriorityHigh）、`TCPIP_THREAD_STACKSIZE=1024`

### pl_net_adapt.h 架构约束

`Platform/Inc/pl_net_adapt.h` 聚合所有 LwIP API 头文件。**严格禁止在任何 .h 文件中包含此头文件**——仅 .c 实现文件可引用，防止 LwIP 类型泄漏到 Application 层头文件中。

## 调试

**SEGGER RTT**（`pl_rtt.c`）：高速调试输出通道（无需占用 UART），通过 J-Link/CMSIS-DAP 的 SWD 接口传输。`pl_rtt_init()` 在 `hw_initcall` 中初始化，`SEGGER_RTT_printf()` 可在任何上下文中使用。不依赖 RTOS，可在 HardFault Handler 等异常上下文中输出。

**现场自证诊断（`APP_DIAG_BANNER`，2026-09-14）**：`pl_rtt_init()` 就绪后，`app_boot.c` `init_task` 在 `app_net_boot_apply()` 之后打**开机横幅**（`fw`/`built`/`tree` 树哈希 + 构建口径 + `display screen=WxH` + `driver linked?` + 字库芯片 + `netcfg` + 四个 UDP 端口）、在全部通道启动后再打**延迟体检**（各通道实测端口与 `bind=OK/SKIPPED/FALLBACK/FAIL`）。**用途**：现场一眼判「板上跑的是不是这份镜像 / 屏体几何多少 / 端口到底绑到哪」——**没有 `[diag]` 行 = 旧镜像**。总开关 `Application/Inc/app_diag.h` `APP_DIAG_BANNER`（默认 1），`make APP_DIAG=0` 一键关；Flash 增量 +5616B、RAM +0B。GZ_OL `0x20`/`0x40` 另有逐帧证据（`GZ_OL_RTT_DIAG`）。判读表与现场清单见 `doc/14` §12.7/§12.8 与 `.analysis/9k23881580/field_triage_2432_and_port.md`。

**通道层 + 调度层诊断（2026-09-17 晚 联调第二轮）**：三层前缀一起看，`[tcp_srv]`/`[tcp_cli]`（accept/recv/close，`TCP_SRV_RTT_DIAG`/`TCP_CLI_RTT_DIAG`，默认跟随 `APP_DIAG_BANNER`，单行 ≤96B）、`[disp] probe`（`{` 帧无协议消费时的 WAIT/FAKE 第一现场，限速 `PROBE_DBG_MIN_MS` 200ms）、`[disp] notify queue FULL`（通知队列满兜底告警）+ post-boot 体检行 `qfull=… resync=… notify_drop=…`；三段判据与现场 8 步抓取见 `doc/15` §11.3/§11.4。`make APP_DIAG=0` 时新增诊断零输出零开销（elf 内 0 条前缀字符串），但**通知重试修复常开**（text +48B / bss +4B）。

**硬件测试**（`app_test.c`）：9 个测试函数 — `app_test_pixel_scan` / `app_test_render_text` / `app_test_io_output` / `app_test_led_mapping` / `app_test_scan_line_order` / `app_test_diagonal` / `app_test_oblique_scan` / `app_test_prepare_mapping` 等，由 `app_test_run()` 汇总。`app_test_run()` 当前在 `app_boot` 的 `init_task` 中已注释。另有 `app_factory_test.c`（`sw_app_initcall(_factory_test_init)`）：出厂检测 monitor + 老化循环（TEST 键触发，斜扫测试）。**业务数据到达经 `app_factory_mode_interrupt()` 置中止标志（不销毁任务），monitor 各按键等待点分片（100ms）检查标志回 IDLE——TEST 键全程可用**（对齐 9K1F212701 裸机：收包仅清 testMode）。

## Kernel 工具库 (`Kernel/`)

| 文件 | 关键 API | 说明 |
|---|---|---|
| `ring_buffer.h` | `RB_DEFINE(name,sz)` 编译期静态分配 | 零堆开销环形缓冲区。每个 API 末尾的 `void *mutex` 参数控制锁行为：传 `rb->mutex` 自动加锁/解锁（单步操作），传 `nullptr` 跳过（调用者通过 `rb_lock/rb_unlock` 自行持锁，用于多步原子序列）。`rb_init` 在运行时绑定优先级继承互斥锁。相邻数据的 `rb_peek`/`rb_contig`/`rb_skip` 支持帧探测。 |
| `initcall.h` | `hw_pl/dev_initcall`、`sw_pl/dev/app_initcall` | 6 个层级宏，生成 linker section 条目 |
| `container_of.h` | `container_of` | 向上转型宏。`channel_t`/`ch_ops_t`/`channel_id_t`/`proto_probe_sta_t` 等 dispatch 共用类型定义在 `Application/Inc/app_dispatch.h` |
| `bcc_utils.h` | `bcc_calcu` | BCC 校验（RLS 协议） |
| `text_cvt.h` | `UTF8ToGBK` | UTF-8→GBK 编码转换 |
| `bit_utils.h` | `bit_ctz` | 协议掩码→数组索引（`proto_mask_t` → 数组下标） |
| `crc_utils.h` | CRC 校验 | CRC32（IAP）+ CRC-16/XMODEM（LDI） |

## 代码风格

- `.clang-format`：Microsoft 基础，4 空格缩进，无 Tab，Linux 大括号，不限制列宽
- 语言：C23，中文注释
- 命名：`pl_`=Platform、`dev_`=Device、`app_`=Application
- 派生类基类成员统一 `me`（如 `dev_display_t me`、`channel_t me`）
- 函数命名：`_` 前缀 = 内部静态函数、模块前缀 `pl_`/`dev_`/`app_` = 公开 API
- GPIO 定义遵循 CubeMX 命名
