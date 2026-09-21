# 22-1665 宿主验证与历史归档（`.analysis/22_1665/`）

本目录是 `Device/Display/dev_display_22_1665.c` 的**宿主侧（PC）工具 + 历史归档**，
不参与固件构建，只依赖 `python3`、`gcc`（机器级差分）与 `arm-none-eabi-*` 工具链（编译/反汇编类脚本）。

> **现行状态（2026-09-17 第三十轮后）**：模组**已现场调通**，驱动 **432 行**（文件头 16 行），几何**彻底参数化**
> （**改整屏分辨率只改 `_22_1665_MODULE_ROWS` / `_22_1665_MODULE_COLS` 两个宏**；数据线表 / 落点 /
> 帧长 / 时钟全派生；**第三十四轮起取消全部编译期防御**（17 条 `_Static_assert` 全删，约束 = 注释契约 + `doc/01` §0.3），
> 且**接线形态唯一 = 每列一口独立数据线（模型 B）**：
> 组 g（= 一个 HUB 口 / 一个模块列）取兄弟驱动通道对 `2g / 2g+1` 的 R/G 脚
> —— **组 0 = R1/G1/R2/G2（PG9/PG10/PG15/PB6）、组 1 = R3/G3/R4/G4（PB8/PB9/PE1/PE2）**；
> 各组**并行**、**每帧 `128 × MODULE_ROWS` 时钟**；`MODULE_ROWS=1` 时每组一块 ⇒ 每块收满 128 位
> ⇒ **HUB1 显逻辑上半、HUB2 显逻辑下半**（两块合起来才是完整逻辑屏）。
> **1×1 行为零回归**由语义级 + **机器级**（`check_host_differential.py` **42/42**：
> 宿主编译产物 1×1 新=旧 + 模型 B 各口径对规格模型）双重证明。
> **第二十六轮**：两块各插一口 + 默认画面 `"1\n2"` ⇒ HUB1 显「2」/ HUB2 全黑 ⇒ 硬件 = 模型 B
> （H1 并联 / H3 真级联被否证）；报告 `round26_two_hubs.md`、`doc/01` §0.11。
> **第二十七轮**：模型 B 实现为默认 + **四川 ETC 七行现场现象零剩余解释**
> （单块模组只见逻辑下半 + 单行帧在 16px 宽屏只可能出「12」；唯一自洽 = 工具 0-based 行号 +
> 观测前清屏，7/7）；报告 `round27_wiring_b_and_etc.md`、`doc/01` §0.10/§0.11、附录 A.15。
> **第二十八轮**：收口整理（调试残留审计 + 格式对齐 `22_1703`；595 → 591 行，行为零变化）；
> 报告 `round28_cleanup.md`、`doc/01` 附录 A.16。
>
> **第二十九轮（本轮，生产化收口 —— 删除「已验证完毕的化石可配置性」）**：按用户裁决，
> 现场验证完全、驱动正常 ⇒ **只保留模型 B 一条生成路径**：
> ① 模型 A 全部分支 + 接线开关 `_22_1665_HUB_WIRING`（含模型 A 的 `_22_1665_flush_state` /
> `scan` / `build_bsrr_table` 与 `_22_1665_bsrr_port_cnt`、`_22_1665_PORT_MAX`、只服务模型 A 的断言）；
> ② 蓝分量开关 `_22_1665_BLUE_AS_LIT`（固化 `1`）；③ 数据极性开关 `_22_1665_DATA_ACTIVE_HIGH`
> （固化 `1`，建表那处异或写成常量形）。**保留** `_22_1665_CHAIN_HEAD_IS_MODULE0`（唯一的现场 A/B 旋钮，
> `MODULE_ROWS ≥ 2` 才可见效、当前无现场证据）与全部**守卫断言**（当时 17 条；**第三十四轮已全删**）及 4 个几何宏。
> 驱动 **591 → 468 行**；**行为零变化**：三个几何口径（1×1 / 1×2 / 工作区当前 1×5）下
> 「删除前归档源码 vs 现行源码」驱动对象**20 段逐字节一致 + 反汇编逐条一致**；
> `check_equivalence.py` **75/75**。报告 `round29_finalize.md`、`doc/01` 附录 A.17。
> **`-D_22_1665_HUB_WIRING=…` / `BLUE_AS_LIT=…` / `DATA_ACTIVE_HIGH=…` 自本轮起对现行驱动完全无效**。
>
> **第三十轮（2026-09-17 夜，文件头瘦身 + 几何宏写法对齐兄弟驱动）**：① 驱动文件头 **46 → 16 行**（长篇
> 叙述移入 `doc/01` §0；全文 **468 → 432 行**）；② 四个几何宏由 `#ifndef` 守卫改**兄弟驱动同款普通
> `#define`**（兄弟 `1_263` / `22_1703` 本就是普通 `#define`）⇒ **`-D_22_1665_MODULE_*=…` 不再生效**
> （只剩「重定义告警 + 文件值胜出」）——换几何口径**改文件**，宿主脚本一律走 `macro_override.py`
> **源码替换**（`link_variant.sh` 的几何参数改为 `宏名=值`，给几何宏传 `-D` 会被直接拒绝）。
> **`_22_1665_CHAIN_HEAD_IS_MODULE0` 未动**（仍 `#ifndef` + 可 `-D`）。**行为零变化**：改动前基线
> `archive/round30/dev_display_22_1665_pre_round30.c`（468 行 / md5 `abe51c0ba7dc5c0bac06973a2c53109b`）
> vs 现行 ⇒ **22 个 section 逐字节一致 + 反汇编 360 行 0 差异**（`check_round30_ab.sh`）。
> 报告 `round30_header_and_macros.md`、`doc/01` 附录 A.18。
>
> ⚠ **工作区当前宏值 = `MODULE_ROWS=1 / MODULE_COLS=5`（16×80）**——第二十八轮会话期间的外部改动，未回改；
> 该口径驱动对象 `.ccmram` = **8320B**；宿主复现 1×2（驱动对象 3328B）/ 1×1（1664B）用
> `bash .analysis/22_1665/link_variant.sh mod1x2 _22_1665_MODULE_COLS=2`（**源码替换**；写 `-D` 已不生效）。
> as-built、几何宏算式、改分辨率操作步骤见 `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` §0 / §0.9。

### 归档与恢复路径（**先归档再删**）

| 归档物 | 路径 | md5 / 规模 | 用途 |
|---|---|---|---|
| **删除前驱动**（第二十九轮） | `archive/finalize/driver_pre_finalize_20260917.c` | `2ccd650cc24c4360b24b151cc220e1b3` / **591 行** | **三开关 + 模型 A 路径的最后一版**；恢复旧行为时把它拷回 `Device/Display/dev_display_22_1665.c`（或直接对它加 `-D`） |
| 现场默认画面快照（历史前提） | `archive/finalize/app_default_display_test_1n2_20260917.c` | `bc8854546d49042f36d14eaabf2acd38` / 不参与构建 | `round25/26` 历史脚本的断言锚点（当时默认画面 = 测试串 `"1\n2"`；现行已回改标准欢迎画面） |

```bash
# 恢复旧行为的三种口径（都用归档驱动）
cp .analysis/22_1665/archive/finalize/driver_pre_finalize_20260917.c Device/Display/dev_display_22_1665.c
#   → 第二十八轮形态（模型 B + 三开关可用）
#   → 模型 A：再加 -D_22_1665_HUB_WIRING=1
#   → 蓝不亮：再加 -D_22_1665_BLUE_AS_LIT=0      → 明暗反相：再加 -D_22_1665_DATA_ACTIVE_HIGH=0
```

---

## 1. 一键验证入口

```bash
make clean && make -j8 DISP=22_1665        # 先构建（check_flash_artifact 需要它；改驱动后必须 clean）
bash .analysis/22_1665/verify_all.sh       # 全部：20 项（含 6 个换口径重链）
bash .analysis/22_1665/verify_all.sh --fast # 快速：14 项，跳过 link_variant（秒级）
```

`verify_all.sh` 逐个运行下方「现行脚本」并汇总通过 / 失败，**退出码 = 失败项数**（0 = 全绿）。

**当前构建基线**（2026-09-18 **第三十二轮**，`PROTO=ALL` `DISP=22_1665`（**工作区宏值 8×2 = 128×32**），
GCC Debug `-Og`，`APP_DIAG_BANNER` 默认开）：`.text 175248 / .rodata 203296 / .data 1656 /
.ccmram **58876**（本模组 **23040B** = `1408×16 + 256×2`）/ .bss 124836 / ._user_heap_stack 2564`
⇒ SRAM 合计 129056B（余 **2016B**）/ CCM 余 **6660B**；换口径重链 `mod8x2` 与 make 构建**逐尺寸一致** ✔。
驱动对象本模组 `.ccmram`：1×1 = **1664B** / 1×2 = **3328B** / 2×2 = **6144B** / 1×5 = **8320B** / 8×2 = **23040B**
（`link_variant.sh` 源码替换重链实测）。同轮 `ALL/1_263` 余 2016B、`CQ/1_263` 余 6296B（**两口径不编本驱动**）。
`.ccmram` 绝对值含工作区他处改动（静态任务栈、云南治超/网络通道等）；
镜像身份以 RTT 横幅 `fw=`/`built=`/`tree=` 为准（**注意：tree= 由 `app_boot.o` 内嵌，改驱动后需
`make clean` 才会刷新；他处文件在构建期间被编辑也会让 tree= 立刻漂移**）。

> **第三十四轮口径（现行）**：**全部 17 条 `_Static_assert` 已删除**（含第三十二轮改成的「≤ 整片 CCMRAM 区域 64KB」
> 物理守卫 + 仅断言用的 2 个派生宏）；错误几何 = 静默错口径 / 越界读 `_22_1665_lines`，或**链接期** `region CCMRAM overflowed`。
> 现行驱动 **372 行**（441 → 372，注释字符 −21%）；零回归证明 `check_round34_ab.sh`（5/5）。
> **历史第三十二轮口径**：曾把自设 16KB 预算改为「本模组数组 ≤ 整片 CCMRAM 区域 64KB」
> 物理守卫；**分辨率只受 CCMRAM 实际空间约束、只由两个几何宏定义**。其他消费者实测 **35836B** ⇒
> 本模组实用上限 ≈**29700B**（COLS=2 时 M≈20 / 10×2 = 160×32 为上限量级〔估算，以链接期为准〕）。
> **⚠ 8×2 性能风险**：scan ≈0.36~0.48ms（TIM3 周期固定 500µs ⇒ 占 72%~96%）、prepare 整帧提交 ≈1.0~1.2ms
> 跨帧——**须上机 DWT 复核**；本轮**未改 TIM3/TIM4 参数、未做去表重构**。

> **第二十九轮增量**：驱动对象 `.text` / `.rodata` 在三个几何口径下与删除前**逐字节相同**
> （纯删除死分支），`.ccmram` 仍严格 `1408·M + 256·MODULE_COLS`；`.bss` 仍 = `MODULE_COLS`
> （`_22_1665_bsrr_slot_cnt[]`）。整项目 CCM 绝对值含工作区他处改动（`s_task_*_stack` 等），**与本模组无关**。

**口径解耦**：源码里的模块数宏是**用户在用的旋钮**，因此「零回归 / 基线」类脚本
（`check_equivalence.py`、`check_host_differential.py` 的 1×1 口径、`check_compile_matrix.sh` 的每个用例）
都**显式钉死 1×1 / 16×16**，不随源码当前值漂移；「当前口径」的几何自检用
`check_driver.py --geom RxC`（`verify_all.sh` 已固定跑 1×1 / 2×1 / 2×2 三档）。

**「n/n 通过」的含义**：脚本内部逐条断言（源码事实 / 复算结果 / 编译结果），
全部断言通过即打印 `通过 n / 失败 n`；出现 `失败 ≥ 1` 时脚本会打印失败项细节并以非 0 退出。
**何时必须跑**：改动驱动后（`verify_all.sh --fast` 起步）、**改分辨率宏后（务必带 `--geom` 项）**、
切换构建口径后、烧录前（`check_flash_artifact.sh`）、以及现场排障时按需跑对应单项。

## 2. 现行脚本（顶层）

| 脚本 | 一句话用途 | 运行方式 | 典型结果 | 何时跑 |
|---|---|---|---|---|
| `verify_all.sh` | **汇总入口**：跑完下列全部脚本并汇总 | `bash .analysis/22_1665/verify_all.sh [--fast]` | **14 项（--fast）/ 20 项 + 1 跳过（全量）**（全量含 6 个换口径重链；第三十四轮起 `check_flash_artifact.sh` 在前置不满足时显式记「跳过」——8×4 下没有 22_1665 的 make 产物，见 §4-3） | 每次改驱动 / 烧录前 |
| `macro_override.py` | **几何宏源码替换工具**（第三十轮新增）：把源码里 `#define NAME (V)` 的值替换成目标值、生成临时副本再编译（几何宏已是普通 `#define`，`-D` 只会静默测错口径）；`materialize()` / `split_macros()` 供各脚本 `import` | `python3 .analysis/22_1665/macro_override.py <源.c> <目标.c> NAME=VAL ...` | 写出替换副本 | 任何需要换几何口径的场合 |
| `check_round30_ab.sh` | **第三十轮零回归 A/B**：把「改动前归档」与现行驱动用**同一条编译命令行**（取自最新构建日志）各编一份对象，比 **section 逐字节 + `objdump -d` 逐行**。**第三十二轮起两侧几何钉死 1×5**（归档件带旧 16KB 断言，8×2 下自编不过；且不随工作区宏值漂移）。**第三十四轮起日志选取 = 「最新一份含驱动编译命令的日志」**（`pick_log.sh`，最近一次 make 不是 22_1665 口径也能工作） | `bash .analysis/22_1665/check_round30_ab.sh` | **2/2**（22 段一致 / 反汇编 0 行差异） | 改动驱动注释/宏形态后（证明产物不变） |
| `check_round32_ab.sh` | **第三十二轮零回归 A/B**：同上，对照 `archive/round32/dev_display_22_1665_pre_round32.c`（第三十一轮的 **8×2 现场态**，md5 `efc96ba3208d008952d1946806880c47`）；两侧几何同样钉死 1×5（理由同上）；日志选取同第三十轮（第三十四轮起走 `pick_log.sh`） | `bash .analysis/22_1665/check_round32_ab.sh` | **2/2**（22 段一致 / 反汇编 0 行差异） | 第三十二轮「CCM 自设上限删除」的零回归证据 |
| **`check_round34_ab.sh`** | **第三十四轮零回归 A/B**（新增）：对照 `archive/round34/dev_display_22_1665_pre_round34.c`（**第三十二轮后形态 = 改动前**，441 行 / md5 `af7fe2b44be42b96a14e43c96e0c6e10`、17 条断言），两侧几何各跑 **1×5 与 8×4** 两口径；另加 3 条源码级自证（现行 0 条 `_Static_assert` / 归档 17 条 / `_22_1665_CCM_BYTES`·`_22_1665_BSRR_SLOTS` 零残留） | `bash .analysis/22_1665/check_round34_ab.sh` | **5/5**（两口径各 20 段一致 / 反汇编 0 行差异） | 第三十四轮「取消编译期防御 + 精简注释」的零回归证据 |
| `check_equivalence.py` | **零回归对照（语义级 + 机器级）**：语义级 = 改动前驱动（归档备份）vs 现行驱动，逐字节比对落点表 / `prepare` 产物 / 合并写表与每帧 BSRR 写序 / `set_row` 电平；**机器级（第二十九轮改锚）** = 同一宏口径下**「删除前归档驱动」vs「现行驱动」**的驱动对象逐段逐字节 + `prepare`/`scan`/建表/`init` 反汇编逐条比对（1×1 / 1×2 / 1×5 三口径）。旧锚点「钉模型 A 路径」已随开关删除失效 | `python3 .analysis/22_1665/check_equivalence.py [--no-machine]` | **75/75** | **每次改驱动**（重构 / 优化后必跑） |
| `check_host_differential.py` | **零回归 + 多口径（机器级）**：把新旧驱动各编成 x86-64 二进制（桩 `host/stub/` + `host/host_driver.c`），多组图案逐字节比对 `frame_state` / BSRR 写序 / CLK 数 / `set_row`；并对**模型 B 各口径（1×1 / 2×1 / 1×2 / 2×2）**做「编译产物 vs 独立规格模型」对拍，并核对**引脚表 main.h ⇔ harness 一致**。**每个用例的两个几何轴都显式钉死**（**第三十轮起经 `macro_override.materialize()` 源码替换**，不再是 `-D`——几何宏已是普通 `#define`；不随源码宏值漂移；宿主侧 `pl_hub75_bsrr_t` 在 x86-64 上是 16B、比真板大，只钉一轴会让 CCM 预算失真——第二十八轮修）。模型 A 与 `blue0`/`invpol` 变体随开关删除**已退休** | `python3 .analysis/22_1665/check_host_differential.py [--cases N]` | **42/42** | 改了 `prepare` / 落点 / 时钟映射后 |
| `check_driver.py` | 结构自检 + **几何口径自检**：几何宏与派生式（**含「几何宏无 `#ifndef` 守卫」**）、**数据线表（5 组 × 4 行 = 通道对口径）**、链段区域推导 / 双射 / 红绿覆盖全屏 / **时钟归属**、CCM 占用通式（**第三十四轮起：无编译期防御**——0 条 `_Static_assert` / 无 `#error`；物理契约以注释保留（`MODULE_COLS ≤ 5` + ≤65535 + 链接期兜底）；仅断言用的 2 个派生宏零残留）、基类字段一致性、**RTT/探针/旧口径零残留 + 三开关已退休 + 历史轮次叙述零残留**、文件规模（**头 ≤60 行、全文 ≤400 行**） | `python3 .analysis/22_1665/check_driver.py [--geom 2x1] [--pix 16x16]` | **60/60**（每口径） | 改了驱动结构 / 宏 / 注释；**改分辨率后带 `--geom` 跑** |
| `check_eide_command.sh` | **EIDE 原命令复现**：照抄 EIDE 构建日志里的 `arm-none-eabi-gcc` 命令行（`-std=gnu23 -O3` 全量 -I/-D），验证合法口径（1×5 / 多模块 / 几何变体 / **8×2 与 12×1（旧 16KB 预算的界外值）**，**几何经源码替换**）零报错零告警；**旧非法口径改为实测期望**（第三十四轮）：`warn_case` = `MODULE_ROWS=0` → `-Wdiv-by-zero`、`MODULE_COLS=6` → `-O3` 的 `-Waggressive-loop-optimizations`（UB：越界读 `_22_1665_lines`）；`ok_case`（**静默编过**）= `MODULE_COLS=0` / 单模块 24×16·16×32·30 / 47×1（对象零告警，越区域由链接期兜底）；A/B 用例两侧钉死几何。三开关用例已随开关删除退休 | `bash .analysis/22_1665/check_eide_command.sh` | **20/20** | **改几何宏后必跑**（回答「EIDE 里会不会又报错」） |
| `check_compile_matrix.sh` | 编译矩阵（精简命令、快）：合法口径（1×1 / 2×1 / 1×2 / 2×2 / 3×2 / 1×5 / **8×2（现场几何）** / 12×1 / 6×2 / 11×1 / **46×1（64KB 区域边界）** / 32×8 / 链首开关 ×2，**几何一律源码替换**）须零告警编过；**旧非法口径改为实测期望（第三十四轮起 A/B/C/D 四节）**：B = `warn`（`COLS=0`/`ROWS=0` 编过但有 `-Wtype-limits`）与 `ok`（24×16 / 16×32 / 30 / `COLS=6` **零告警编过 = 静默错口径**）；**C = 链接期兜底**（新增最小链接设施：驱动对象 + 桩 + 真链接脚本 ⇒ 47×1 `region CCMRAM overflowed by 896 bytes`、1×1 与 8×4 对照零溢出）；**陷阱回归**：`-D_22_1665_MODULE_COLS=2` 必须出现重定义告警 | `bash .analysis/22_1665/check_compile_matrix.sh` | **25/25**（合法 15 + 旧非法现行行为 6 + 链接期 3 + 陷阱 1） | 改了任何几何宏 / 约束注释 |
| `check_flash_artifact.sh` | **烧录物自检**：核对 `build/Debug/Project_STD.hex` 的内容身份（`2200001665` / 数据线名 blob R1·G1·R2·G2 **与组 1 的 R3·G3·R4·G4** / **不含旧 RTT 串** / 当前源码树 `tree=`），识别「EIDE 与 make 共用 build/Debug 导致的混装」 | `bash .analysis/22_1665/check_flash_artifact.sh [hex]` | **7/7** | **每次烧录前** |
| `link_variant.sh` | 宿主「换口径重链」：抓**最新一份含驱动命令的构建日志**（`pick_log.sh`，第三十四轮起不要求最近一次 make 是 22_1665 口径）里的编译/链接命令，只替换驱动 `.o`，输出到 `/tmp`——验证开关/几何口径能否**编译 + 链接通过**，并打印整项目段尺寸（含 `.ccmram`）。**几何写 `宏名=值`（内部调 `macro_override.py` 源码替换）；给几何宏传 `-D` 会被直接拒绝（exit 2）**——防「静默测错口径」；`_22_1665_CHAIN_HEAD_IS_MODULE0` 仍用 `-D` | `bash .analysis/22_1665/link_variant.sh <名> [宏名=值 ...] [-D宏名=值 ...]` | `[ok] 链接通过` + 段尺寸 | 改了口径相关代码 |
| **`obj_fingerprint.py`** | **对象「不改动证明」指纹工具**（第三十四轮新增）：对一个 `.o` 打印各 section 的序号/名/大小/对齐 + **逐 section 内容 md5** + `objdump -d` 整体 md5 与行数（`.debug*`/符号表按工具链记账排除）；两份不同源码的输出可直接 `diff` 判定「产物是否逐字节一致」 | `python3 .analysis/22_1665/obj_fingerprint.py <对象.o> [--label 标签]` | 指纹清单（可 diff） | 任何「只改注释/断言，须证明产物不变」的场合 |
| **`comment_density.py`** | **注释密度度量**（第三十四轮新增，报告用）：纯词法扫描 `//` 与 `/* */`（跳过字符串），给出「总行数 / 空行 / 纯注释行 / 尾注释代码行 / 纯代码行」与「注释字符 / 有效代码字符」比值——用于和兄弟驱动 `22_1703`（0.82）/ `1_263`（0.72）对比精简幅度 | `python3 .analysis/22_1665/comment_density.py <file.c> ...` | 三驱动对照数字 | 讨论「注释密度是否与兄弟驱动相当」时 |
| **`pick_log.sh`** | **共用助手**（第三十四轮新增，被 `link_variant.sh` / `check_round3*_ab.sh` source）：按 mtime 从新到旧扫描 `build_round*.log`，取**第一份含 22_1665 驱动编译命令**（`--need-link` 时还要含整项目链接命令）的日志——最近一次 make 若不是 22_1665 口径（例如收尾用 `make -j8` 恢复 1_263 产物）也能工作 | `. "$HERE/pick_log.sh"; LOG="$(pick_log_1665 --need-link)"` | 日志路径（stdout） | 所有「从构建日志抓命令行」的脚本 |
| `analyze_board_flash.py` | **板上 flash 实读分析**：对 J-Link 只读 dump（1MB 全片）逐段解释——线名 blob / 树哈希 / 协议判别串 / 默认画面串，回答「板上跑的是哪份镜像」；并列出已删除的旧 RTT 串命中（应为 0） | `python3 .analysis/22_1665/analyze_board_flash.py [dump.bin]` | 文字报告 | 需判「板上镜像身份」而 RTT 不可用时 |
| `round25_two_modules.py` | **1×2 面板级取证**（第二十五轮；**历史口径，锚定归档驱动 + 现场画面快照**）：把 1×2 帧逐位展开（`frame_state` 分块 / 每时钟写序），用单模块落点模型解出**三种接线模型下每块物理屏各显示什么**；核对现场「默认全黑 / TEST 出 MD」；并证明「默认口径改动前后逐字节一致 + flip 开关 = 块对调」 | `python3 .analysis/22_1665/round25_two_modules.py` | 17/17 | 多模块首接 / 换接线 / 现场判「哪块是哪个模块」时（模型 A 历史口径） |
| `round26_two_hubs.py` | **双 HUB 口现象判定**（第二十六轮；**历史口径，锚定归档驱动 + 现场画面快照**）：默认画面 `"1\n2"` 在 16×32 上的**真实落点**（源码事实 + 独立复写布局模型）、帧位流归属（扫描序逐项核对 + 两块解回 16×16 图像逐像素）、H1/H2/H3 三假设对照、「第二组数据脚从未被驱动」的机器级证据、flip 开关效果、现场实验清单 E1~E7 | `python3 .analysis/22_1665/round26_two_hubs.py` | 27/27 | 双口/多模块接线判型、判「哪块是哪一行」、判「旧固件为什么 HUB2 黑」时 |
| **`round27_wiring_b_and_etc.py`** | **模型 B 取证 + ETC 现象解释**（第二十七轮，**现行形态**）：ETC 单行/全屏渲染参数源码事实、逐行号穷举表、四种「工具口径 × 清屏」假设与现场七条观测对照（H4 = 7/7）、单行帧「只可能出 12」的不可能性证明、模型 B 的帧结构/引脚/归属机器级证据、新固件现场预测表 | `python3 .analysis/22_1665/round27_wiring_b_and_etc.py` | **32/32** | 现场 ETC/双口复测前、接线相关改动后 |

**本轮（34）报告**：`round34_defenses_removed.md`（17 条断言逐条清单 + 消息原文 / 注释精简幅度（441→372 行）/
对象逐字节零回归（`round34_baseline.txt` vs `round34_current.txt`）/ 脚本同步 / 三口径构建与 8×4 链接期溢出 /
**未做待决策三项**）。

**上轮（33）报告**：`round33_structure_alignment_and_detabling_scope.md`（只读辨析：`_22_1665_line_t` 与
`chain_dst` 去表是两件事 / `name` 字段与只读的 `half` 列 / 三方案代价对比 —— **本轮未做，待用户决策**）。

**上轮（32）报告**：`round32_ccm_limit_removed.md`（删除自设 16KB CCM 预算与选择理由 / 8×2 as-built /
三口径构建 / 脚本同步与新的通过数 / 性能风险）。

**上轮（31）报告**：`round31_8x2_budget_analysis.md`（8×2 编译失败诊断 / 17 条断言逐条判定 / 整片 CCM 实测）。

**上轮（30）报告**：`round30_header_and_macros.md`（文件头瘦身对照 / 几何宏改普通 `#define` / 受影响脚本
改源码替换 / 新旧用例计数 / 机器级零回归）。

**机器级差分设施**（`check_host_differential.py` 用，非独立脚本）：
`host/stub/{dev_display.h,pl_hub75.h,initcall.h}`（把 HAL 换掉的最小桩，含 BSRR/CLK/行址的**记录函数**）
+ `host/host_driver.c`（harness：从 stdin 读每用例 pixel_map，打印 `GEOM`/`S`/`T`/`K`/`R` 行）。
**注意**：桩只用于对拍，**不是真机头文件**；新旧驱动、各口径都用同一份桩编译，故对拍有效。

生成物（由上面脚本写出，勿手改）：无（`check_driver.py` / `check_equivalence.py` / `check_host_differential.py`
只做断言与对拍，不生成预测图；历史预测图见 `archive/screens/` 与 `archive/scripts/*.md`）。

## 3. 归档（`archive/`）

| 目录 | 内容 | 说明 |
|---|---|---|
| `archive/scripts/` | 历史脚本（第十二~十八轮）+ **第二十~二十二轮脚本**：`check_round20.py` / `check_round21.py` / `check_multichain.py` / `check_model_matrix.sh` | 前九轮脚本已被后续取代；**新归档的四个脚本断言的是「重构前驱动形态」，其 `SRC` 已固定指向 `archive/prev_round/dev_display_22_1665_round23_pre_refactor.c`，2026-09-17 复跑分别 58/58、23/23、31/31、96/96 全通过**（保留旧形态自身的证据链）。**注（第二十七轮）**：顶层的 `round25_two_modules.py` / `round26_two_hubs.py` 取证的是**模型 A 历史形态**，脚本锚定 `archive/finalize/driver_pre_finalize_20260917.c` + `app_default_display_test_1n2_20260917.c` 快照（第二十九轮起）；模型 B（现行唯一形态）的对应取证在 `round27_wiring_b_and_etc.py` |
| **`archive/finalize/`（第二十九轮新增）** | **删除前驱动** `driver_pre_finalize_20260917.c`（**591 行 / md5 `2ccd650cc24c4360b24b151cc220e1b3`** = 三开关 + 模型 A 路径的最后一版；**恢复路径与用法见上一级 README 的「归档与恢复路径」**）+ 现场默认画面快照 `app_default_display_test_1n2_20260917.c`（不参与构建，仅供历史脚本断言） | 生产化收口前状态；`check_equivalence.py` 的机器级锚点与 `round25/26` 脚本的取证基线 |
| **`archive/round30/`（第三十轮新增）** | **第三十轮改动前基线** `dev_display_22_1665_pre_round30.c`（**468 行 / md5 `abe51c0ba7dc5c0bac06973a2c53109b`** = 第二十九轮收口形态；由 `reconstruct_pre_round30.py` 逆向生成，**冻结件、勿手改**） | `check_round30_ab.sh`（本目录顶层）的零回归对照基准：证明「文件头瘦身 + 几何宏改普通 `#define`」不改变产物 |
| **`archive/round34/`（第三十四轮新增）** | **第三十四轮改动前基线** `dev_display_22_1665_pre_round34.c`（**第三十二轮后形态 = 改动前**，**441 行 / md5 `af7fe2b44be42b96a14e43c96e0c6e10` / 17 条 `_Static_assert`**，编辑时刻宏值 = `MODULE_ROWS 8 / MODULE_COLS 4` = 128×64；**冻结件、勿手改**） | `check_round34_ab.sh` 的零回归对照基准：证明「删除全部编译期防御 + 精简注释」不改变产物（1×5 与 8×4 两口径各 20 段逐字节 + 反汇编 0 差异） |
| **`archive/round32/`（第三十二轮新增）** | **第三十二轮改动前基线** `dev_display_22_1665_pre_round32.c`（**第三十一轮的 8×2 = 128×32 现场态**，432 行 / md5 `efc96ba3208d008952d1946806880c47` —— 与 `round31_8x2_budget_analysis.md` 记录逐字节一致，**冻结件、勿手改**） | `check_round32_ab.sh` 的零回归对照基准：证明「删除自设 16KB CCM 上限 + 注释更新」不改变产物 |
| `archive/reports/` | 各轮报告：`round17_report.md`、`round18_explain_and_field.md`、`round19_probe_request.md`、`round20_calibration.md`、`round21_diagnosis.md`、`round22_cleanup.md`、`round23_refactor.md`、`candidates.md`、`signatures.md`、`verify_report.txt` | 现场记录与推理过程（含 `prepare` 逐链掩码缺陷的完整取证）。**第二十四轮报告在顶层 `round24_resolution.md`** |
| `archive/screens/` + `archive/scripts/*.md` | 历史口径预测图：`expected_screens.md`、`probe7_screens.md`、`probe8_screens.md`、`round17_screens.md`、`round18_color_matrix.md`；现行口径预测图 `multichain_screens.md`、`round20_screens.md`（由归档脚本对旧形态生成，**落点模型与现行一致**） | 预测 ASCII / 症状签名表 |
| `archive/logs/` | 历史构建日志：`build_round15/17/20.log` | 历史报告引用的那几次构建 |
| `archive/prev_round/` | 驱动历史版本：`*.20260917.bak`（第十一轮）、`*_round16_multichain.c`、`*_round17_probe9.c`、`*_round21_pre_round22.c`、**`*_round23_pre_refactor.c`（探针/RTT 全量版——换屏标定从这里取回）**、**`*_round24_pre_resolution.c`（第二十四轮几何参数化前的 1×1 现场调通版——零回归对照基线）**、**`*_round25_pre_order.c`（第二十五轮链首口径开关前的形态——改动前对照）** | 回退 / 对照用 |

> **标定工具已归档**：现行驱动无探针（7/8/9）、无上电引脚回读自检、无任何 RTT 打印。
> 换屏 / 换接线需要重新标定时：从 `archive/prev_round/dev_display_22_1665_round23_pre_refactor.c`
> 取回文件（用 `-D_22_1665_CHAIN_PROBE=7/8/9`、`-D_22_1665_PIN_SELFTEST=1` 编译），
> 标定步骤见 `doc/01` §0.3 与附录 A（第二十轮）。

## 4. 现场判读速查（现行口径：无驱动内 RTT）

1. **判镜像身份**（唯一手段 = `[diag]` 开机横幅，`app_boot.c`）：
   ```
   [diag] fw=... built=... tree=<8 位树哈希>
   [diag] build proto=ALL disp=22_1665 config=Debug toolchain=gcc
   [diag] display screen=16x16 code=2200001665 scan_lines=1 chans=1
   ```
   `display screen=WxH code=2200001665 scan_lines=1` 即本模组（**改了模块数宏 W/H 会跟着变**）；
   **没有 `[diag]` 行 = 板上是旧镜像**。（改驱动后需 `make clean` 才刷新 `tree=`——它由 `app_boot.o` 内嵌。）
   ⚠ 上面的示例行按 **1×2 口径**写（`screen=16x32`）；**工作区当前宏值 8×4 ⇒ 应打 `screen=128x64`**
   （第三十一轮曾改 8×2，之后被外部改成 8×4 —— 注意该宏值下 make 的 ALL/CQ 两口径都链接期溢出，见 §4-3）。
2. **验收判据**：红「口」= 整幅 52 点（R1 上半 23 + R2 下半 29）、绿「口」= 整幅 52 点（G1 + G2）；
   只亮一半或全黑 = 驱动/接线异常（先跑 `check_equivalence.py` + `check_host_differential.py`）。
3. **改整屏分辨率**：只改 `Device/Display/dev_display_22_1665.c`「模组参数」一节的
   `_22_1665_MODULE_ROWS` / `_22_1665_MODULE_COLS` **两个宏**（**不补任何表**；旧称「一」节，第二十八轮整理后该节标题为
   「22-1665 模组参数 — 16x16 红绿双色 / 静态单扫」），
   改完跑 `check_eide_command.sh`（EIDE 原命令）+ `check_driver.py --geom RxC`（几何自检），
   并按公式核占用：**`CCM = 1408 × M + 256 × MODULE_COLS`；第三十四轮起驱动内无编译期守卫**
   （约束 = 注释契约 + `doc/01` §0.3），**整片 64KB 由链接期兜底**（其他消费者实测 35836B ⇒
   实用上限 ≈29700B，估算、以链接期为准），物理硬约束 **`2 × MODULE_COLS ≤ 10`（⇒ COLS ≤ 5）**；
   **⚠ 实测：工作区 8×4（M=32，本模组 46080B）在 `PROTO=ALL` / `PROTO=CQ` 下均链接期溢出**
   （16380B / 14124B，预存状态）⇒ 只有 EIDE Debug 子集能装；**改宏后务必跑一次真实链接**（`make` 或
   `link_variant.sh`），不要只看编译通过。口径对照表与 8×2 as-built 见 `doc/01` §0.9 / §0.7。
4. **多模块屏首接（现行唯一形态 = 每列一口独立数据线）**：把两块各插一口，上电应见 **HUB1「1」/ HUB2「2」**
   （默认画面两行内容不同）；每次只改一件事的判读：
   - **HUB1 正常、HUB2 全黑** ⇒ 组 1 脚位假设不成立（HUB2 不走通道 2/3）或 HUB2 硬件异常
     —— 用通断档量 **PB8/PB9/PE1/PE2 → HUB2 数据 pin**，实测后只改 `_22_1665_lines[1]` 行（代码零结构改动）；
   - **两块都出「1」** ⇒ 两组脚被并联（同一组 MCU 脚）；
   - **HUB1 出「2」、HUB2 黑** ⇒ 板上仍是**旧固件**（第二十七轮前的模型 A 形态只写第一组；先看 `[diag]` 横幅判镜像）；
   - `MODULE_ROWS ≥ 2` 时再核**组内横向顺序**：`-D_22_1665_CHAIN_HEAD_IS_MODULE0=1` 对调组内链首
     （默认 0 = 组内最后一块收末 128 位；当前无现场证据，是**唯一保留的现场 A/B 旋钮**）。
   - 回归网：`round27_wiring_b_and_etc.py`（模型 B 取证 / ETC 现象 / 预测表）+
     `round25_two_modules.py` / `round26_two_hubs.py`（历史口径，锚定归档驱动）
     + `check_host_differential.py` 的多口径对拍。
5. **回退 / 对照**：**接线形态与三开关已固化**（第二十九轮，模型 B / `BLUE_AS_LIT=1` /
   `DATA_ACTIVE_HIGH=1`）——要模型 A（同线级联）、蓝不亮或明暗反相时，用**归档驱动**
   `.analysis/22_1665/archive/finalize/driver_pre_finalize_20260917.c`
   （拷回驱动路径，或直接对它加 `-D_22_1665_HUB_WIRING=1` / `-D_22_1665_BLUE_AS_LIT=0` /
   `-D_22_1665_DATA_ACTIVE_HIGH=0`；**这些 `-D` 对现行驱动无效**）。
   需要更旧行为（16×8 单链兼容口径、探针/RTT 判读）时用
   `archive/prev_round/…round23_pre_refactor.c`（带 `-D_22_1665_MULTI_CHAIN=0`）；
   链首口径开关前的形态 = `archive/prev_round/…round25_pre_order.c`。
6. **改宏后（历史上会报断言错，第三十四轮起不再报）**：自己按注释契约核——模块数须 ≥1、列数须 ≤5（`2 × MODULE_COLS ≤ 10`）、
   单模块须 256 像素（单链段 8 块 = 128 位）、半屏高须 ≥4 且为 4 的倍数、**本模组数组不得超整片
   CCMRAM 区域 64KB**（通式 `1408 × M + 256 × MODULE_COLS`；**自设 16KB 预算第三十二轮已删**；
   整片装不下时由链接期 CCMRAM overflow 兜底——记得同时看链接结果，别只看驱动编过）；
   用 `check_eide_command.sh` 可在本机复现同一命令行。
