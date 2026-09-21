# 9K23881580（贵州 P10 治超屏）LED 模组驱动 → STD `dev_display_22_1703` 迁移设计分析

**状态**：`[只读分析]` `[未改任何既有文件]` `[设计报告，待用户裁决]`
**日期**：2026-09-14
**范围**：把源裸机固件 9K23881580 的 LED 模组扫描/显示驱动功能等效地迁移进 STD 分层架构，新增 `Device/Display/dev_display_22_1703.c`。
**本轮不做任何代码修改**，仅产出本报告。

## 证据引用约定

| 简写 | 含义 |
|---|---|
| `[SRC]` | `/home/yystation/Desktop/9K23881580/01Embedded_software`（源裸机工程根，Keil/EIDE 工程） |
| `[STD]` | `/home/yystation/Program/3833024/Project-STD-main`（目标 STD 工程根） |
| `[SRC-HIST]` | 行号均为该文件在源磁盘上的原始行号（文件为 GBK 编码；分析时用 `iconv` 转 UTF-8 到 `/tmp` 副本阅读，未改动原文件） |
| 外部资料 | 非仓库证据，单独标注 URL，与代码证据严格区分 |

---

## 0. 结论速览

1. **模组与屏体**：单模块 **32×16 像素、1/4 扫描、2 数据通道/模块**；整屏 **7 模块/行 × 4 模块/列 → 224×64 像素**，共 **8 个数据通道**（4 条 HUB75 排线，每条 2 通道），显存 **14336B**。（`[SRC]/USER/TIMER/timer.h:5-19`）
2. **列驱动芯片 5124GM = 台湾聚积 MBI5124GM**：16 通道恒流 + 内建串行移位寄存器/栓锁，输出 1~25mA，最高 25MHz 时钟，可级联，内建消隐；`GM` 为封装/订购后缀（MSSOP-24）。（外部资料，见 §1.1；仓库内**无任何 5124 字样**，芯片型号由用户提供）
3. **引脚与 STD 100% 一致**：源 `common.h`/`lamp.h` 的 OE/CLK/LAT/A~D + R1..R8/G1..G8/B1..B8 与 `[STD]/Core/Inc/main.h:74-193` 逐条相同 → **`pl_hub75` 可直接复用，零 Platform 扩展**；GPIO 已在 `[STD]/Core/Src/gpio.c:78-107,148-165` 配为推挽输出。
4. **映射可精确改写为 STD 形式**（§1.5）：
   `ch = y/8; line = y%4; half = (y%8 >= 4); idx = ch*1792 + line*448 + (x/8)*16 + (x%8) + half*8`
   与 STD `row_dst[y]` + 8 像素块拷贝的 prepare 模式天然吻合（`[STD]/Device/Display/dev_display_1_263.c:124-158`）。
5. **CCM 占用 30336B**（pixel_map 14336 + hub75_buff 14336 + row_dst 128 + g_bsrr 1536），比现编 1-263（7×2 口径 29568B）多 **768B**；与 CQ（6377B）合计 36716B → **仍余 28820B**。但 **1-263 与新模组不能同时编入**（两者 + CQ = 66281B > 65536B，链接必炸）。
6. **最大工程风险是扫描 CPU 预算**：源固件每扫描线移位 448 时钟 × 8 通道 = **3584 次位输出**（1-263 7×2 只有 1792，1-969 3×3 为 2304）；按 1-263 反汇编实测"15 指令/(像素·通道)"折算，朴素实现约 **339~440µs/行**，而 STD TIM3 行周期是 **500µs**（`[STD]/Core/Src/tim.c:82-84`）→ 占空比 **68%~88%**（源固件 3.5ms/行下仅 13%）。**必须裁决 TIM3 周期 + 采用合并写 BSRR 的扫描优化**（§4.4、Q1、Q8）。
7. **一处必须实机裁决的映射分歧**：旧 STD `Drivers/BSP/Display/scan.c`（git 历史 `85d74b1^`）里的 `convert_pixelmap_p10` 与本源固件**不等价**——下半行 8 位是**逆序**（`15 - x%8`）且 10 通道表按 `{R2,R1,R4,R3,...}` **相邻互换**；本报告按用户要求"与源固件等效"采用**自然通道序 + 半行顺序**（§1.6、Q3）。

---

## 1. 硬件 / 芯片事实清单

### 1.1 列驱动芯片 5124GM

**（a）用户给定**：源固件该屏的模组列驱动芯片为 **5124GM**。仓库内检索：`[SRC]` 全树无 `5124` 文本命中（仅 hex/`.o` 二进制偶发字节巧合）；`[STD]` 亦无（`text_cvt.c`/`stm32f4xx_hal_rcc.h`/`build/*.hex` 为巧合命中）。→ **芯片型号本身来源为"用户提供"，本报告按 MBI5124GM 对待，建议以模组铭牌/BOM 复核（Q4）。**

**（b）外部资料（非仓库证据）**：立创商城 MBI5124GM 商品页（`https://item.szlcsc.com/253063.html`，2026-09-14 抓取）载：

| 项 | 值 |
|---|---|
| 型号/品牌 | MBI5124GM / MBI（台湾聚积 Macroblock） |
| 封装 | MSSOP-24-2.6mm（`GM` 为订购后缀） |
| 概述 | 专为 LED 显示面板设计；内建 CMOS 位移缓存器与栓锁，串行输入转并行输出；**16 个恒流输出**，1~25mA/通道；3.3/5V；**最高 25MHz 时钟**；内建消隐（减轻鬼影）；可级联；输出使能控制；通道间交错延迟抑制 EMI |

**（c）从源固件代码可反推的接口/位宽/级联事实（强证据，与芯片手册方向一致）**：

| 事实 | 证据 |
|---|---|
| **每时钟移入 1 bit/通道/色**：内层循环"每像素每通道"置一次 R/G/B 电平，循环尾发 1 个 CLK 脉冲 | `[SRC]/USER/TIMER/timer.c:295-311` |
| **16 位为一组（GROUP_SIZE=16）**，组内 **前 8 位给上半行、后 8 位给下半行** → 一颗 16 输出芯片的 16 个输出被 8+8 分给同一扫描线点亮的物理两行 | `[SRC]/USER/TIMER/timer.h:11`、`timer.c:374-381` |
| **级联**：单通道单色链长 = `SCAN_LINE_PIXEL_NUM` = **448 bit** = 28×16 → 每色每通道 28 颗 16 位输出串联；全屏 8 通道 × 3 色 × 448 = **10752 bit**（= 14336 px × 3 ÷ 4 扫描），与"每 LED 每色每帧 1 bit"的 1/4 扫描总量吻合 | `[SRC]/USER/TIMER/timer.h:17-19`、`timer.c:292-313` |
| **数据采样沿**：先置数据 → `CLK=1` → 2×`__NOP` → `CLK=0`（上升沿采样） | `[SRC]/USER/TIMER/timer.c:306-309`；STD 同构 `[STD]/Platform/Inc/pl_hub75.h:80-85` |
| **行切换顺序**：整行数据移完 → `OE=1`（消隐）→ 改 A/B/C/D 行址 → `LAT` 脉冲 → 恢复 TIM4 | `[SRC]/USER/TIMER/timer.c:313-322`；STD 同序 `[STD]/Device/Display/dev_display.c:159-170` |
| **OE 低有效**（低=使能） | `[SRC]/USER/LAMP/lamp.h:14`（注释"OE是屏的使能端 低有效"）、`lamp.c:70`（初始化 `HUB75_OE = 1` 失能） |
| **行驱动按 ABCD 二进制行址**（4 扫描行 → 仅 A/B 有效，C/D 恒 0） | `[SRC]/USER/TIMER/timer.c:336-357` |

**待确认**：行驱动芯片型号（源固件未提及；代码按译码器型 ABCD 驱动，若为移位寄存器型行驱则 C/D 语义不同——见 Q4）。

### 1.2 模组几何与屏体拼法

| 项 | 值 | 证据（`[SRC]`） |
|---|---|---|
| 每行模块数 | 7 | `USER/TIMER/timer.h:5` |
| 每列模块数 | 4 | `USER/TIMER/timer.h:6` |
| 单模块像素（行×列） | 32（水平）× 16（垂直） | `USER/TIMER/timer.h:7-8` |
| 每模块数据通道数 | 2 | `USER/TIMER/timer.h:9` |
| 模组扫描行数 | **4（1/4 扫描）** | `USER/TIMER/timer.h:10` |
| 组内像素数 | 16 | `USER/TIMER/timer.h:11` |
| 屏幕像素行（**宽度**） | 7×32 = **224** | `USER/TIMER/timer.h:13` |
| 屏幕像素列（**高度**） | 4×16 = **64** | `USER/TIMER/timer.h:14` |
| 显存 | 224×64 = **14336 B** | `USER/TIMER/timer.h:16` |
| 总通道数 | 4×2 = **8** | `USER/TIMER/timer.h:17`（`MODULE_PER_COL * MOUDLE_CHANNEL_NUM`） |
| 单通道像素 | 32×16×7/2 = **1792** | `USER/TIMER/timer.h:18` |
| 每扫描线像素（= 每通道每行移位时钟数） | 1792/4 = **448** | `USER/TIMER/timer.h:19` |

**通道—行 对应关系（关键结论）**：由映射公式反推 **`channel = y / 8`**、**`scan_line = y % 4`**（推导见 §1.5）。即：
- 每个"模块行"（16 行）= 2 个通道（上半 8 行 + 下半 8 行）→ 4 个模块行 = 8 通道；
- **推断（待硬件复核）**：物理上对应 **4 条 HUB75 排线**（每条覆盖 1 个模块行、含 R1G1B1+R2G2B2 两通道），共用 CLK/LAT/OE/A~D —— 依据是 8 个通道两两成对（R1/R2、R3/R4、R5/R6、R7/R8）恰好与 HUB75 的双数据通道定义吻合，且 `lamp.h:5-17` 的"75 接口"引脚说明只列了 R1/G1/B1、R2/G2/B2 两组数据线；
- 引脚命名 `R1..R8/G1..G8/B1..B8` 恰好 8 组，`R9/R10/G9/G10/B9/B10` 本应用**未使用**（源通道表长度虽为 10，但循环上界 `CHANNEL_NUM = 8`，见 `timer.c:19-52` 与 `timer.c:296`）。

**"75 接口"含义**：源 `lamp.h:5-17` 注释为"标准 75 接口点阵屏管脚定义"，列出 R1/G1/B1、R2/G2/B2、A/B/C/D、SCK(CLK)、STR(LAT)、OE、GND —— 即业内通称的 **HUB75 接口**；"贵州P10治超屏75接口鑫恩拓"中的"75 接口"即此。

**"双色"线索**：`[SRC]/02字库/贵州P10双色治超屏字库`（8MB，文件名即"双色"）；协议侧只产生 red/green/yellow（`USER/LWIP_APP/UDP_SERVER.C:188-196`），但驱动代码 R/G/B 三色引脚齐备且蓝色可驱动（`timer.h:29-36` 颜色枚举含 blue/purple/cyan/white）。→ **屏体是 RG 双色还是 RGB 全彩待确认（Q9）**。

### 1.3 引脚对照（源固件 vs STD）

源定义：`[SRC]/Common/common.h:7-82`（端口/引脚）+ `[SRC]/USER/LAMP/lamp.h:19-73`（HUB75 位带别名）。
STD 定义：`[STD]/Core/Inc/main.h:74-193` + 引脚表 `[STD]/Platform/Src/pl_hub75.c:11-49`。

| 信号 | 源（端口/引脚） | STD（端口/引脚） | 一致 |
|---|---|---|---|
| OE | PD0 | HUB75_OE PD0 (`main.h:154-155`) | ✅ |
| CLK | PD1 | HUB75_CLK PD1 (`main.h:156-157`) | ✅ |
| LAT | PD3 | HUB75_LAT PD3 (`main.h:158-159`) | ✅ |
| A/B/C/D | PD4/5/6/7 | HUB75_A..D PD4..7 (`main.h:160-167`) | ✅ |
| R1/G1/B1 | PG9/PG10/PG12 | 同（`main.h:168-173`） | ✅ |
| R2 | PG15 | 同（`main.h:174-175`） | ✅ |
| G2/B2/R3/G3 | PB6/PB7/PB8/PB9 | 同（`main.h:182-189`） | ✅ |
| B3/R4/G4/B4 | PE0/PE1/PE2/PE3 | 同（`main.h:190-193`、`74-77`） | ✅ |
| R5/G5/B5 | PE4/PE5/PE6 | 同（`main.h:78-83`） | ✅ |
| R6/G6/B6 | PC13/PC14/PC15 | 同（`main.h:84-89`） | ✅ |
| R7/G7/B7/R8/G8/B8 | PF0..PF5 | 同（`main.h:90-101`） | ✅ |
| R9/G9/B9/R10/G10/B10 | PF6/PF7/PF8/PF9/PF10/PC0 | 同（`main.h:102-113`） | ✅（本应用不用） |

GPIO 初始化：`[SRC]/USER/LAMP/lamp.c:14-71`（全部数据脚推挽输出 + 复位为低 + `OE=1`）；
`[STD]/Core/Src/gpio.c:58-107,148-165`（同样把 R1..R10/G/B、OE/CLK/LAT/ABCD 配为输出并复位），`[STD]/Platform/Src/pl_hub75.c:51-59`（`pl_hub75_init`：OE=1、LAT/CLK=0、A~D=0）。

> **结论**：引脚层面 **零冲突、零缺口**，`pl_hub75.c/.h` 与 `dev_display.c` 的通用扫描骨架可原样复用。另注意源与 STD **同一块 PCB** 的旁证：源 `LED1 = PD9`（`USER/LED/led.h:6`）与 STD `LED_Pin PD9`（`main.h:138-139`）也一致。

### 1.4 时序参数（源 vs STD）

| 项 | 源固件 | STD 现行 | 证据 |
|---|---|---|---|
| TIM3（行同步） | `TIM3_Int_Init(3500-1, 83)` → 1MHz 计数 → **3.5ms/扫描行** | `Prescaler 84-1`、`Period 500-1` → **500µs/扫描行** | `[SRC]/Main/main.c:41`；`[STD]/Core/Src/tim.c:82-84` |
| 帧周期 / 刷新率 | 4×3.5ms = 14ms → **71.4 Hz** | 4×0.5ms = 2ms → **500 Hz** | 推导（`timer.h:10` 扫描 4 行） |
| TIM4（亮度 PWM） | `TIM4_Int_Init(10-1, 83)` → 10µs × 8 档 = **80µs 周期**，12.5kHz | `Period 20-1` → 20µs × 8 档 = **160µs 周期**，6.25kHz | `[SRC]/Main/main.c:42`、`timer.c:271-288`；`[STD]/Core/Src/tim.c:118-120`、`dev_display.c:256-263` |
| 亮度档位 | `lightLev` 0~8（初值 7）；光敏映射 `8 - adc/500`，下限 3 → **3~8** | `light_level` 0~8（`DEV_DISPLAY_BRIGHTNESS_MAX=8`）；光敏任务 `set_range(4,7)` → **4~7** | `[SRC]/USER/TIMER/timer.c:272`、`USER/ADC/adc.c:62-65`；`[STD]/Device/Inc/dev_display.h:137`、`Application/Src/app_light_sensor.c` |
| 扫描执行位置 | **TIM3 ISR 内阻塞执行**（`lamp_scan()` 直接调用） | `scan_task`（osPriorityRealtime）由 TIM3 事件标志唤醒 | `[SRC]/USER/TIMER/timer.c:261-267`；`[STD]/Device/Display/dev_display.c:110-172` |
| 帧缓存提交 | 主循环 `displayDataUpdata()`（`newData` 置位才重排） | `dev_display_commit_frame[_rect]()` → `ops->prepare` | `[SRC]/Main/main.c:57`、`timer.c:359-383`；`[STD]/Device/Display/dev_display.c:37-93,122-158` |

### 1.5 显存 → 物理移位顺序（映射推导，本报告核心）

**源实现**（`[SRC]/USER/TIMER/timer.c:359-383`）：

```c
row_cnt = map_cnt / SCREEN_PIXEL_ROW;        // y（0..63）
col_cnt = map_cnt % SCREEN_PIXEL_ROW;        // x（0..223）
ModuelGroup = (row_cnt % 4 + row_cnt / 8 * 4) * (SCAN_LINE_PIXEL_NUM / GROUP_SIZE) + col_cnt / 8;
if ((row_cnt % 8) >= 4)                      // 下半行
    fixBuf[col_cnt % 8 + 8 + ModuelGroup * GROUP_SIZE] = sourceBuf[map_cnt];
else                                         // 上半行
    fixBuf[col_cnt % 8 +     ModuelGroup * GROUP_SIZE] = sourceBuf[map_cnt];
```

**扫描读取**（`timer.c:292-313`）：`fixBuf[line_cnt + scan_line*448 + channel_cnt*1792]`，`line_cnt ∈ [0,448)`、`channel_cnt ∈ [0,8)`。

**等价改写**（把源公式并入 STD 的 `[channel][line][pixel]` 线性布局）：

```
ch   = y / 8                       // 0..7
line = y % 4                       // 0..3
half = (y % 8 >= 4) ? 1 : 0        // 0=上半行(组内偏移 0)、1=下半行(组内偏移 8)
idx  = ch*1792 + line*448 + (x/8)*16 + (x%8) + half*8
```

推导依据：`ModuelGroup*16` 展开为 `(y%4 + y/8*4)*448 + (x/8)*16`，而 `channel*1792 + line*448 = (channel*4 + line)*448`，故 `y%4 + y/8*4 ≡ channel*4 + line` ⟹ `channel = y/8`、`line = y%4`。

**校验样例**：

| (x, y) | 源公式 | 改写式 | 一致 |
|---|---|---|---|
| (0, 0) | group=0, half=0 → idx=0 | ch0,line0,half0 → 0 | ✅ |
| (0, 4) | group=0, 下半 → idx=8 | ch0,line0,half1 → 8 | ✅ |
| (8, 1) | group=(1+0)*28+1=29 → idx=29*16+0=464 | ch0,line1 → 448+16=464 | ✅ |
| (223, 63) | group=(3+28)*28+27=895 → 895*16+7+8=14335 | 7*1792+3*448+27*16+7+8=14335 | ✅ |
| 值域 | — | 最大 14335 = `DISRAM_SIZE-1` | ✅ |

**物理含义**：一个 16 字节组 = **同一扫描线的 8 个 x（上半行）+ 同 8 个 x（下半行）**，两组行号相差 4（`y` 与 `y+4` 同扫描线点亮，符合 1/4 扫描的 4 行交错）。

**逻辑坐标约定一致**：源 `sourceBuf[y*224 + x]`（`[SRC]/USER/FUNC/func.c:770,838,899,972,...`）与 STD `pixel_map[y*screen_rows + x]`（`[STD]/Device/Inc/dev_display.h:80-85` 坐标约定、`dev_display.c:194-197` 写入实现）**完全一致**，故渲染/协议层的坐标语义在迁移后不变。

### 1.6 与旧 STD `convert_pixelmap_p10` 的差异（必须实机裁决）

旧 STD 显示代码（git 历史 `85d74b1^:Drivers/BSP/Display/scan.c`，该文件已在 `2cbcafb` 的全局清理中删除，目录 `Drivers/BSP/` 现不存在）中有 `convert_pixelmap_p10()`：

| 项 | 源固件 9K23881580（本任务基准） | 旧 STD `convert_pixelmap_p10` |
|---|---|---|
| 组号公式 | `(y%4 + y/8*4)*(448/16) + x/8` | **完全相同** |
| 上半行组内偏移 | `x%8`（偏移 0） | `x%8`（偏移 0） |
| 下半行组内偏移 | `x%8 + 8`（**顺序**） | `15 - x%8`（**逆序**） |
| 通道表顺序（红/绿/蓝） | `{R1,R2,...,R10}` 自然序 | `{R2,R1,R4,R3,R6,R5,R8,R7,R10,R9}` **相邻互换** |

两者**不是等价改写**：若同一块屏上两种映射都"看起来正常"，则只可能是字形镜像/通道错位被忽略。旧文件的注释作"P10模组的扫描"，说明历史上确有一版 P10 驱动，但屏体批次/走线可能与 9K23881580 不同。**本报告按用户要求以源固件为准**，并把这两处列为上机第一优先验证项（Q3、M2 里程碑）。

---

## 2. 与现有模组的对比表

| 模组 | 屏体几何（宽×高） | 单模块像素 | 扫描 | 通道/模块 | 总通道 | 列驱动 | 行驱动 | 每扫描线位输出（时钟×通道） | CCMRAM 占用 | 复用度 |
|---|---|---|---|---|---|---|---|---|---|---|
| `1-260`（未编入） | 32×32 | 32×32 | 1/8 | 2 | 2 | 2013EP | — | 64×2 = 128 | 2496 B | prepare/scan 结构可抄，几何不可用 |
| **`1-263`（现行 Makefile / EIDE Debug 在编）** | 224×64（工作区 7×2 口径） | 32×32 | 1/8 | 2 | 4 | 2013EP | 16206S 译码器 | 448×4 = **1792** | **29568 B** | 结构模板同构，映射规则不同 |
| `1-263`（历史 7×4 口径） | 224×128 | 32×32 | 1/8 | 2 | 8 | 2013EP | 16206S | 448×8 = 3584 | 59136 B | 同上 |
| `1-969` | 192×96 | 64×32 | 1/8 | 2 | 6 | 2013EP | RUC7258E 译码器 | 384×6 = 2304 | 38208 B | 同上 |
| `1-577` | 192×96 | 64×32 | 1/8 | 2 | 6 | 2013EP | 译码器型 | 384×6 = 2304 | 38208 B | 同上（左右半区映射） |
| `p20`（未编入） | 128×32 | 16×8 | 静态 1 | 2 | 8 | — | 移位寄存器型（`pl_hub75_ShiftRegister_set_row`） | 64×8 = 512 | 9792 B | 静态扫描语义不同 |
| **`22_1703`（本任务）** | **224×64** | **32×16** | **1/4** | **2** | **8** | **MBI5124GM（16 通道恒流）** | 译码器型（ABCD 二进制，待确认） | **448×8 = 3584** | **30336 B** | 骨架/BSRR/行选全复用；映射与块序需新写 |

> CCMRAM 占用按各类结构体定义精确计算（`pixel_map + hub75_buff + row_dst + g_bsrr`）：其中 1-263 7×4 口径 59136B、1-969/1-577 38208B 与 `[STD]/doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md`（2026-09-04 起采样）记载完全吻合，证明计算方法可信；`1-260`/`p20` 为该公式推算值（p20 的 `g_bsrr` 按 8 通道计）。

---

## 3. `dev_display_22_1703` 设计方案

### 3.1 文件与派生结构体

```
Device/Display/dev_display_22_1703.c   （新增，约 250~300 行，风格对齐 dev_display_1_263.c）
Device/Inc/dev_display.h               （+1 行：dev_display_22_1703_get() 声明，可选）
```

```c
typedef struct { dev_display_t me; } dev_display_22_1703_t;

[[gnu::section(".ccmram")]] static uint8_t  _22_1703_pixel_map[_22_1703_BUFFER_SIZE];
[[gnu::section(".ccmram")]] static uint8_t  _22_1703_hub75_buff[_22_1703_BUFFER_SIZE];
[[gnu::section(".ccmram")]] static uint16_t _22_1703_row_dst[_22_1703_SCREEN_COLS];      /* 64 × 2B */
[[gnu::section(".ccmram")]] static _22_1703_bsrr_t g_bsrr[_22_1703_TOTAL_CHANNELS][8];   /* 8×8×24B */
```

**基类无需任何修改**：`screen_rows=224`、`screen_cols=64`、`total_channels=8`、`channel_pixels=1792`、`scan_line_pixels=448`、`buffer_size=14336` 全部落在现有字段类型内（`uint16_t`/`uint8_t`，`[STD]/Device/Inc/dev_display.h:41-59`），`ops` 三函数签名（`prepare/scan/set_row`，`dev_display.h:30-34`）与 `scan_task` 骨架（`dev_display.c:110-172`）原样适用。

### 3.2 模组参数（与源宏一一对应）

| 新增宏 | 值 | 对应源宏（`[SRC]/USER/TIMER/timer.h`） |
|---|---|---|
| `MODULE_CODE` | `"2200001703"`（**占位，待裁决 Q6**） | — |
| `_22_1703_MODULE_ROWS` | 7 | `MODULE_PER_ROW:5` |
| `_22_1703_MODULE_COLS` | 4 | `MODULE_PER_COL:6` |
| `_22_1703_MODULE_PIXEL_ROW` | 32 | `MODULE_PIXEL_ROW:7` |
| `_22_1703_MODULE_PIXEL_COL` | 16 | `MODULE_PIXEL_COL:8` |
| `_22_1703_CHANNELS_PER_MODULE` | 2 | `MOUDLE_CHANNEL_NUM:9` |
| `_22_1703_SCAN_LINES` | 4 | `MOUDLE_SCAN_LINE_NUM:10` |
| `_22_1703_GROUP_SIZE` | 16 | `GROUP_SIZE:11` |
| 派生：`SCREEN_ROWS` 224 / `SCREEN_COLS` 64 / `BUFFER_SIZE` 14336 / `TOTAL_CHANNELS` 8 / `CHANNEL_PIXELS` 1792 / `SCAN_LINE_PX` 448 | | `timer.h:13-19` |

> **移植陷阱（务必注意）**：`channel_pixels` 必须按 **`PIXEL_ROW * PIXEL_COL * MODULE_ROWS / CHANNELS_PER_MODULE`**（除 **channels_per_module = 2**）计算，得 1792；`dev_display.h:52` 的注释写作"/ total_channels"（若误按 8 除得 448，则整屏映射全错）。1-263 的实现同为除 `CHANNELS_PER_MODULE`（`dev_display_1_263.c:47`）。

### 3.3 `prepare`：像素重排（pixel_map → hub75_buff）

`hub75_buff` 布局采用与源 `fixBuf` **完全相同**的语义（[channel][line][448]，行内 28 组 × 16 字节 = 上半行 8 + 下半行 8），因此 `prepare` 可退化为"每行 28 次 8 字节块拷贝"：

```c
/* init 期预计算（等价于源公式的展开，见 §1.5）：
 * ch = y/8; line = y%4; half = (y%8 >= 4)
 * row_dst[y] = ch*channel_pixels + line*scan_line_pixels + half*8;   */
for (uint16_t row = row_begin; row < row_end; row++) {          /* ④a 脏矩形行子集 */
    const uint8_t *src = pixel_map + (uint32_t)row * screen_rows;
    uint8_t *dst       = hub75_buff + _22_1703_row_dst[row];
    for (uint16_t blk = 0; blk < SCREEN_ROWS / 8U; blk++)       /* 28 块 */
        memcpy(dst + blk * 16U, src + blk * 8U, 8U);
}
```

- 支持 `dirty_rect` 行子集：每逻辑行独立落到自己的 8 字节半区，行子集重排安全（与 1-263 的 ④a 语义一致，`dev_display_1_263.c:124-158`）。
- 单帧全量 prepare 成本量级：64 行 × 28 块 ≈ **1.8K 次 8 字节拷贝**（远低于 scan 的开销，可忽略）。
- 行区间保护（`dirty_rect_h==0` 早退、越界钳位）直接照抄 1-263 范式。

### 3.4 `scan`：一个扫描行的位流输出（两种档位）

**档位 A（朴素/直译版，等价性优先）**——结构与 1-263 完全同构（`dev_display_1_263.c:167-186`），只是常量不同：

```c
static inline void _22_1703_scan(dev_display_t *dev, uint8_t line) {
    const uint16_t scan_line_pixels = dev->scan_line_pixels;   /* 448 */
    const uint16_t channel_pixels   = dev->channel_pixels;     /* 1792 */
    const uint8_t  total_channels   = dev->total_channels;     /* 8 */
    const uint8_t *base = dev->hub75_buff + (uint16_t)line * scan_line_pixels;
    for (uint16_t px = 0; px < scan_line_pixels; px++, base++) {
        const uint8_t *p = base;
        for (uint8_t ch = 0; ch < total_channels; ch++) {
            const _22_1703_bsrr_t *e = &g_bsrr[ch][p[(uint16_t)channel_pixels * ch]];
            pl_hub75_bsrr_flush(&e->r); pl_hub75_bsrr_flush(&e->g); pl_hub75_bsrr_flush(&e->b);
        }
        pl_hub75_clock_pulse();
    }
}
```

**档位 D（推荐产品化版本）**——**每时钟 5 次写端口替代 24 次写引脚**（利用 BSRR 高 16 位复位的特性，把同一端口上多通道的置位/复位合并成一次 32 位写）：
- 端口—通道归属（由 `main.h` 引脚表可得，init 期比较 `g_hub75_pin_*[ch].port` 自动分组）：
  GPIOG ← ch0(R1,G1,B1) + ch1(R2)；GPIOB ← ch1(G2,B2) + ch2(R3,G3)；GPIOE ← ch2(B3) + ch3(R4,G4,B4) + ch4(R5,G5,B5)；GPIOC ← ch5(R6,G6,B6)；GPIOF ← ch6(R7,G7,B7) + ch7(R8,G8,B8)。
- 预计算"端口合并表"：按端口实际涉及的通道颜色位拼 key（G 4 位 / B 4 位 / E 7 位 / C 3 位 / F 6 位，共 **232 项 × 4B = 928B**），key → 该端口 32 位 BSRR 字。
- 每时钟：读 8 个颜色字节（跨 1792 步长）→ 为 5 个端口各组 key（移位/与/或）→ 5×(查表+写 BSRR) → CLK 脉冲。

### 3.5 行选与锁存

`set_row = pl_hub75_Decoder_set_row`（`[STD]/Platform/Inc/pl_hub75.h:109-116`）：A/B 取 `scan_line` 的 bit0/bit1，C/D 恒 0 —— 与源 `scan_channel()`（`[SRC]/USER/TIMER/timer.c:336-357`，`line & 0x01/0x02/0x04/0x08`）逐位一致。锁存与 OE 消隐由通用骨架完成（`dev_display.c:159-170`：`scan → 关 TIM4 → OE=1 → set_row → LAT 脉冲 → 开 TIM4`），与源顺序（`timer.c:313-322`）一致，**无需模组特化代码**。

### 3.6 引脚映射 / Platform 扩展

- **不需要新增 Platform 能力**：`g_hub75_pin_r/g/b[10]`（`pl_hub75.c:11-49`）、`pl_hub75_bsrr_t` / `pl_hub75_bsrr_flush`（`pl_hub75.h:172-181`）、`pl_hub75_clock_pulse` / `latch_pulse` / `oe_set` / `Decoder_set_row` 已完全覆盖 8 通道 + ABCD 译码 + CLK/LAT/OE 需求；`HUB75_CHANNEL_MAX = 10 ≥ 8`。
- **可选（仅档位 D 建议）**：在 `pl_hub75` 增加一个 1 行 helper，避免 Device 层自行比较端口指针，例如
  `__STATIC_INLINE bool pl_hub75_bsrr_same_port(const pl_hub75_bsrr_t *a, const pl_hub75_bsrr_t *b);`
  或 `void pl_hub75_bsrr_merge(pl_hub75_bsrr_t *acc, const pl_hub75_bsrr_t *add);`。若走档位 A 则**一行都不用加**。

### 3.7 initcall 注册

```c
void dev_display_22_1703_init(void) {
    g_22_1703.me.ops = &_22_1703_ops;              /* prepare / scan / set_row */
    dev_display_register(&g_22_1703.me);           /* 与 1-263 同机制，dev_display.c:35 */
    /* 预计算 row_dst[]（§3.3） + g_bsrr[][]（照抄 1_263_init 的引脚→BSRR 装配） */
}
hw_dev_initcall(dev_display_22_1703_init);         /* 与 dev_display_init 同层，先于 sw 层的 dev_display_start */
```

时序保证：`dev_display_init`（hw 层）先做 `pl_hub75_init()` 与 TIM3/TIM4 回调注册，`dev_display_start`（sw 层）再创建 `scan_task`，模组 `init()` 与 `pl_hub75_init` 同层——1-263 已验证该顺序可用（`dev_display.c:99-107,176-191`）。

### 3.8 与源固件行为等价性对照表

| 行为 | 源 | 新驱动 | 等价性 |
|---|---|---|---|
| 逻辑显存语义 | `sourceBuf[y*224+x]`，颜色 0/1/2/4/3/5/6/7 | `pixel_map[y*224+x]`，`display_color_t` 同值域 | ✅ 恒等 |
| 物理重排 | `displayDataUpdata()` 全帧重排 | `prepare`（含脏矩形行子集） | ✅ 位流相同 |
| 每扫描线时钟数 | 448（每通道） | `scan_line_pixels = 448` | ✅ |
| 数据/时钟/锁存时序 | 置数据→CLK 上升沿；OE 消隐→换行→LAT | 同（通用骨架 + pl_hub75） | ✅ |
| 行址编码 | A/B/C/D 二进制（4 行） | `pl_hub75_Decoder_set_row` | ✅ |
| 帧提交时机 | `newData` 置位才重排 | `commit_frame[_rect]` | 🔸 机制不同、语义等价 |
| 刷新率 | 71.4 Hz（3.5ms/行） | 由 TIM3 决定（500µs→500Hz / 1ms→250Hz） | 🔸 更快，非负向 |
| 亮度 | 8 档（0~8），80µs PWM | 8 档（0~8），160µs PWM | ✅ 档位一致；光敏范围 3~8 vs 4~7（Q7） |
| 双色/全彩 | R/G/B 三引脚齐备，协议只用 R/G/Y | 同 | 🔸 待 Q9 确认 |

---

## 4. 内存影响量化

### 4.1 新模组 CCMRAM 明细（精确）

| 对象 | 计算 | 字节 |
|---|---|---|
| `_22_1703_pixel_map` | 224 × 64 | 14336 |
| `_22_1703_hub75_buff` | 224 × 64 | 14336 |
| `_22_1703_row_dst` | 64 × 2 | 128 |
| `g_bsrr`（档位 A/D 用） | 8 通道 × 8 色 × 24B | 1536 |
| **合计（档位 A）** | | **30336** |
| 档位 D 变体（g_bsrr → 端口合并表 232×4B） | 14336+14336+128+928 | **29728**（表放 Flash 则 28800） |
| 档位 C 变体（放弃 hub75_buff 存 BSRR 字流，4 行×448×5×4B） | 14336+35840+128+928 | **51232** |

SRAM 影响：派生实例 `g_22_1703`（`.data`，与 `g_1_263` 同尺寸 **48B**，实测 `[STD]/build/Debug` 产物 `g_1_263 = 0x30`）+ 代码 text（Flash，约 1~2KB）。**无 SRAM 大块开销**（两个 14KB 缓冲在 CCMRAM）。

### 4.2 余量对比（64KB CCMRAM = 65536B）

| 场景 | 组成 | 合计 | 余量 |
|---|---|---|---|
| 当前 EIDE Debug 产物（1-263 7×2，无 CQ） | 29568 | 29568 | **35968**（实测 `.ccmram`，`build/Debug/Project_STD.elf`） |
| 新模组单独编入 | 30336 | 30336 | **35200** ✅ |
| Makefile `PROTO=ALL`（新模组 + CQ 6377 + 对齐 3） | 30336+6377+3 | 36716 | **28820** ✅ |
| 若 1-263 与新模组**同时**编入（+CQ） | 29568+30336+6377 | 66281 | **超限 745B** ❌ 链接失败 |
| 仅 1-263 与 22_1703 同编（无 CQ） | 59904 | 59904 | 5632（可链接，但**双实例注册语义冲突**） |

> ⚠ **口径提醒**：`doc/06.../04_current_memory_occupancy.md` 中 2026-09-04/09-08 的采样（`.ccmram = 65516B`，含"1-263 显存 59136B"）对应 **1-263 7×4 = 224×128** 配置；当前工作区已改为 **7×2 = 224×64**（`git diff Device/Display/dev_display_1_263.c`：`MODULE_ROWS 1→7`、`MODULE_COLS 1→2`），CCM 实际降到 29568B。**两套数字严禁混用**，本轮落地后需按当前工作区重采（见 §7）。

### 4.3 SRAM 余量

`doc/06-04` 最新采样（2026-09-08，Makefile 1-263 口径 `PROTO=ALL`）：SRAM ≈ **130544B / 131072B（99.6%）**，余量 **≈528B**。新模组 SRAM 增量仅 **≈48B（.data 实例）** + 少量链接对齐 → **可行**，但余量进入 500B 级，任何后续 SRAM 静态增量都必须同步核算（`doc/06` 纪律）。**禁止**把 `pixel_map/hub75_buff` 放 SRAM（+28.7KB 必炸）。

### 4.4 扫描 CPU 预算（本任务最关键的工程约束）

**测算基准（实测反汇编）**：`_1_263_scan` 内层循环 = **15 条指令/(像素·通道)**（含 3 次 BSRR 写），外层每像素 7 条（CLK 脉冲 + 循环控制）——`[STD]/build/Debug/Project_STD.elf`，`_1_263_scan` @ `080487f6–0804881a`（15 条）、`0804881c–0804882c`（7 条）。按 1.0~1.3 周期/指令、168MHz 折算：

| 实现 | 每行位输出 | 估算指令 | 估算耗时/行 | TIM3=500µs 占空 | =1ms | =1.5ms |
|---|---|---|---|---|---|---|
| 源固件（参考） | 3584 | ~57K | 339~440µs | （其 TIM3=3.5ms → 10~13%） | — | — |
| **22_1703 档位 A（朴素）** | 3584 | ~57K | **339~440µs** | **68~88%** ⚠ | 34~44% | 23~29% |
| 1-263 7×2（现行） | 1792 | ~30K | 179~232µs | 36~46% | 18~23% | 12~15% |
| 1-969 3×3（历史） | 2304 | ~37K | 222~288µs | 44~58% | 22~29% | 15~19% |
| **22_1703 档位 D（合并写）** | 3584 | ~22K | **133~173µs** | 27~35% | **13~17%** | 9~12% |
| **22_1703 档位 C（BSRR 字流）** | 3584 | ~6K | **35~45µs** | 7~9% | 3~5% | 2~3% |

含义：
- `scan_task` 是 `osPriorityRealtime`（`dev_display.c:182-189`），一次扫描是**不可抢占的连续突发**；档位 A + 500µs 意味着每 500µs 独占 CPU 339~440µs，其他任务（LwIP TCP/IP 线程 prio=24、协议任务 prio=Normal）每周期只剩 60~160µs → **ping/IAP 升级/UDP 业务会明显变慢甚至丢包**。
- 档位 A + TIM3=1.5ms → 23~29%（与现行 1-969 水平相当，可接受）；档位 D + TIM3=1ms → 13~17%（推荐）；档位 C + 500µs → 7~9%（最优但 CCM +20.9KB、prepare 变重）。
- **每个档位都必须上机用 DWT/`g_dev_display_scan_count` 实测复核**（M5 里程碑），本表仅为选型依据。

---

## 5. 分步实施计划（可验证里程碑）

> 纪律：每步先编译链接 + 读实测内存，再上机；每步保留回滚点（1-263 的 Makefile/eide.yml 条目）。

| 里程碑 | 内容 | 验证方法 | 回滚点 |
|---|---|---|---|
| **M0 前置确认** | 落实 Q1~Q3、Q6、Q9 的裁决（尤其 TIM3 周期、屏体几何、通道/半行口径） | 用户签字确认 | — |
| **M1 骨架落地** | 新建 `dev_display_22_1703.c`（参数宏 + `.ccmram` 缓冲 + row_dst/g_bsrr 预计算 + ops 绑定 + hw_initcall）；`dev_display.h` 加 getter 声明；Makefile `SRC_DEVICE` 用新文件替换 1-263；eide.yml 加文件 + 把 1-263 加入 Debug excludeList（改完立刻 EIDE: Reload Project） | `make -j8` 与 EIDE Debug 双口径链接通过；`arm-none-eabi-size -A ... \| grep ccmram` = **30336**（±对齐）；`nm -S \| grep 22_1703` 符号齐全 | 还原 3 处构建文件 |
| **M2 映射正确性（不上协议）** | 用临时测试序列（或 `app_factory_test` 路径）打点：① y=0 全行红 → 应亮屏顶第 1 行；② y=4 全行绿 → 应亮第 5 行（判 half 语义）；③ y=8 全行红 → 应亮第 9 行（判 channel 序）；④ x=0..7 亮 → 屏左起 1..8（判块内位序）；⑤ x=8..15 亮 → 左起 9..16（判块序） | 目视 + 照片比对源固件同图案 | 仅改 row_dst 公式（half/通道互换），不动 scan |
| **M3 时序与刷新** | 逻辑分析仪/示波器测：每行 CLK 脉冲数 = **448**；每行 LAT 脉冲 = 1；A/B 电平序列 0,1,2,3；OE 在换行窗口为高 | 波形截图归档；改 TIM3 前后刷新率对比（源 71.4Hz 基准） | 恢复 TIM3 原值 |
| **M4 亮度/消隐** | `dev_display_set_brightness` 0~8 全档目视；对照源固件同档亮度观感；确认换行无鬼影/无残影 | 目视 + 照度计（可选） | 恢复 TIM4 原值 |
| **M5 CPU 预算实测** | `g_dev_display_scan_count` + DWT 计时统计单行扫描耗时；对 TIM3=500µs/1ms 两档各测；必要时切档位 D 或 C | 记录实测 µs/行 → 对照 §4.4 估算；确认 LwIP ping/IAP 升级无异常 | 档位 A ↔ D 切换为局部改动 |
| **M6 协议/渲染联调** | 224×64 几何下验证 app_render 字号 16/24/32 与换行、滚动（app_scroll）、等宽；与源固件同报文显示效果比对 | 上位机发源固件同款报文（TCLY 0x20）逐条比对 | — |

---

## 6. 待用户裁决的问题清单

### 阻塞项（不裁决无法开工）

**Q1｜TIM3 扫描行周期取多少？**
- 可选：① 维持 500µs（刷新 500Hz，但档位 A 下扫描占空 68~88%，系统饥饿）；② **1ms（刷新 250Hz，档位 A 占 34~44%、档位 D 占 13~17%）**；③ 1.5ms（刷新 167Hz，最省 CPU，仍 2.3× 源固件刷新率）。
- **推荐 ②1ms**：刷新率是源固件（71.4Hz）的 3.5 倍，显示稳定性无风险；同时给 LwIP/IAP 留出足够带宽。改点仅 `[STD]/Core/Src/tim.c:84`（全局唯一 TIM3，影响所有模组）。

**Q2｜CCMRAM 放不下（或语义冲突）时必须替换哪个模组？**
- 事实：1-263 与新模组同编 + CQ = 66281B > 65536B **必链接失败**；即便去掉 CQ 同编也会出现"两个实例先后 `dev_display_register`、活动屏由链接顺序决定"的隐性行为。
- 可选：① **用 22_1703 替换 1-263**（Makefile + eide.yml 各一处）；② 保留 1-263、新模组走 SRAM 缓冲（+28.7KB SRAM，当前余量 528B，**不可行**）；③ 引入编译期开关（`DISPLAY_MODULE_22_1703`）二选一。
- **推荐 ①**：与仓库既有先例一致（"1-260 已随 1-263 收录切换移出 SRC_DEVICE"，`Makefile:299-313`）；若后续需要两个订单共用一套源码，再补 ③ 的编译开关。**同时请确认工作区 1-263 的 7×2（224×64）配置是给哪个订单用的**——它与本任务屏体同尺寸（224×64），容易混淆。

**Q3｜通道顺序与下半行位序按哪一版？**
- 事实：源固件 = 自然通道序 `{R1..R8}` + 下半行 `x%8+8`（顺序）；旧 STD `scan.c` 的 `convert_pixelmap_p10` = `{R2,R1,R4,R3,...}` 互换 + 下半行 `15-x%8`（逆序）。
- 可选：① 严格按源固件（本次要求"功能等效"）；② 按旧 STD 版；③ 做一个 `_22_1703_MAP_VARIANT` 编译期开关（0=源口径，1=旧 STD 口径），上机各试一次。
- **推荐 ① + 预留 ③ 的单点开关**（改动集中在 `row_dst` 公式与通道表，两处各 1~2 行）：因为源固件是该屏量产固件的实机验证版本，而旧 STD 版所在工程/DOM 已废弃，且其屏体批次不可考。M2 里程碑用 §5 的 5 个打点图案当场判定。

### 非阻塞项（可并行推进，但需答复）

**Q4｜行驱动芯片型号与 C/D 用途？**
- 源固件按 ABCD 二进制行址驱动（4 行只用 A/B），若实际是移位寄存器型行驱（如 SM5266PH，需 C=锁存/A=时钟/B=数据）则行选必须换 `pl_hub75_ShiftRegister_set_row`（`pl_hub75.h:128-170`）。
- **推荐**：请提供模组铭牌/背面丝印或 BOM（含行驱型号）；在拿到证据前按译码器型实现（与源一致）。

**Q5｜2026-S244 订单的实际屏体几何确认（224×64？）**
- 源固件为 7×4 模组 = **224×64**；而 `[SRC]/04协议/38贵州LED_情报板_协议V1.0.doc` 的报文示例里"屏宽/屏高"写的是 **192×80**（该文档同时把"字体大小"示例写作 32×32）。协议字段是**上位机上报值**，源固件并不解析它（`USER/LWIP_APP/UDP_SERVER.C:148-201` 只取 X/Y/字体/字号/颜色）。
- **推荐**：以整机实测屏体模组数量为准（7×4=224×64）；若实际是 6×5=192×80，只需改 `_22_1703_MODULE_ROWS/COLS` 两个宏（派生参数与 CCM 尺寸自动重算，**但 CCM 会涨到 192×80×2+128+1536 = 32384B**，仍需 Q2 替换口径）。

**Q6｜`MODULE_CODE` 字符串取什么？**
- 其它模组用 10 位料号（`"1000000263"`/`"1000000577"`），用于开机画面 `MD:` 与老化测试信息页（`[STD]/Application/Src/app_boot.c:72-79`、`app_factory_test.c:147-149`）。历史上 STD 曾出现 `2200001630`（P16 模组，见已删除的 `Drivers/BSP/Display/scan.c` 注释），与"P10 32×16 双色"模组同属 22 系列。
- **推荐**：`"2200001703"`（与 `dev_display_22_1703` 命名同源）；若料号不同请直接给出准确字符串。

**Q7｜亮度范围对齐？**
- 源光敏映射 3~8（`USER/ADC/adc.c:62-65`），STD 光敏固定在 4~7（`app_light_sensor` → `dev_light_sensor_set_range(4,7)`）。
- **推荐**：本订单沿用 STD 现状（4~7），如需与源观感一致再调 `set_range(3,8)`；这是应用层 1 行改动，不影响驱动。

**Q8｜扫描实现档位与显示 CPU 预算上限？**
- 可选：① 档位 A（最简、最易核对，CPU 高）；② **档位 D（推荐，CPU 降至约 1/2.5）**；③ 档位 C（CPU 最低，CCM +20.9KB、prepare 变重 ~0.9ms/帧）。
- **推荐**：M1~M4 先用①打通与源固件的等效性（便于逐位对比），M5 实测后切②。请确认"显示允许占用 CPU 上限"（建议 ≤20%@1ms）。

**Q9｜屏体是双色（RG）还是全彩（RGB）？**
- 证据：字库文件名"贵州P10**双色**治超屏字库"；但驱动三色引脚齐备、协议颜色判定把 `00 00 FF` 映射为 **yellow**（`[SRC]/USER/LWIP_APP/UDP_SERVER.C:188-196`，注意这是源固件既有的怪点）。
- **推荐**：按全彩驱动（硬件齐备、驱动与颜色枚举同值域），并在协议层按双色屏限制颜色（后续协议接入时再定）；请确认屏体实际填充的灯珠颜色。

**Q10｜本轮是否一并接入贵州情报板协议（TCLY 0x10/0x20/0x30 + 0x40/0x50/0x60）？**
- 本任务只要求模组驱动；但 STD 现有协议里（RLS/SC_OL 等）没有 TCLY 帧族，源固件的 UDP/RS485 协议实现（`USER/LWIP_APP/UDP_SERVER.C`、`USER/RS485/rs485.c`）尚未迁移。
- **推荐**：本轮只做驱动（M1~M5），协议接入另立任务（涉及帧头/队列/RB 纪律 doc/05/08，且需确认与现编协议的帧头互斥——TCLY 首字节 0x54，与现有串口协议 0x7B/0x0A/0xFF/0x5A 不冲突，网口侧需避让 IAP 0x5A5A5A5A）。

**Q11｜EIDE 目标切换确认**
- 需要把 `Device/Display/dev_display_22_1703.c` 加入 `.eide/eide.yml` 的 `virtualFolder → Device/Display` 文件表，并把 `<virtual_root>/Device/Display/dev_display_1_263.c` 加进 Debug 目标的 `excludeList`（`eide.yml:424-440`）。**改完必须立刻在 EIDE 执行 "Reload Project"，期间不得先做任何触发保存的 GUI 操作**（CLAUDE.md 明载该纪律，历史上两次因回写丢条目）。
- 另注：excludeList 中 `<virtual_root>/Application/protocol/...`、`<virtual_root>/Drivers/BSP/...` 是**陈旧路径**（这两个目录在 STD 已不存在，协议已迁到 `Application/Src/`），但当前 EIDE Debug 产物实测只含 IAP/LDI/四川三协议，说明排除仍按名生效——新增条目建议两条都写真实路径，并以链接后 `.ccmram`/`nm` 复核。

**Q12｜是否需要保留 1-263 档位共存能力？**
- 若同一套源码要同时支持"P6 224×64"与"P10 224×64"两个订单，需要编译期开关（`-DDISPLAY_22_1703`）或构建目标分离。
- **推荐**：短期直接替换；若确认双订单共存，再引入开关并同步 `doc/构建开关总表.md`。

---

## 7. 文档同步影响（后续落地时必须更新）

| 文档 | 需要更新的部分 | 触发条件 |
|---|---|---|
| `[STD]/doc/01_显示系统/` | **新增** `22-1703模组驱动分析与迁移记录.md`（模组参数/移位链推导/row_dst 映射/待验证项），格式对齐 `1-263模组驱动分析与修复记录.md` | M1 落地后 |
| `[STD]/doc/01_显示系统/1-263模组驱动分析与修复记录.md` | 若 1-263 被移出编入清单，补"收录变更"说明 | Q2 裁决后 |
| `[STD]/doc/CLAUDE.md` | ①「Display 子系统」模组清单与参数；②「内存布局」CCMRAM 占用（现写"1-263 显存 59136B…余 20B"，与工作区 7×2 实态已不符）；③「选编口径」（EIDE/Makefile 编哪个模组）；④ 若改 TIM3，则 `TIM3 行同步` 描述与「任务清单」中 scan_task 说明；⑤ 模块全景表新增模组行 | M1 / Q1 / Q2 落地后 |
| `[STD]/doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` | 追加本轮 CCM/SRAM 实测采样；**并修正 1-263 口径**（59136→29568，工作区 7×2）与 `1-969/1-577` 相关引用；`§1` 表新增 22_1703 行 | M1 后重采 |
| `[STD]/doc/构建开关总表.md` | 若引入 `_22_1703_MAP_VARIANT`（Q3）或 `DISPLAY_MODULE_*`（Q12）编译开关，必须登记 | 开关落地时 |
| `[STD]/doc/05_协议模块多协议兼容优化/`、`doc/08_协议模块接入规则/` | 仅在 Q10 协议接入时涉及（帧头互斥/RB/队列核算） | Q10 立项后 |
| 本报告 | `.analysis/` 为分析产物目录；是否需要纳入版本管理或转为 `doc/01` 正式文档，请裁决（建议转正后删除 `.analysis/`） | 报告评审后 |

---

## 附录 A：源固件关键代码证据索引

| 主题 | 位置 |
|---|---|
| 几何宏与派生式 | `[SRC]/USER/TIMER/timer.h:5-19` |
| 颜色枚举（0/1/2/4/3/5/6/7） | `[SRC]/USER/TIMER/timer.h:28-37` |
| 显存与通道表定义 | `[SRC]/USER/TIMER/timer.c:10-52` |
| 扫描主循环（448 时钟 × 8 通道） | `[SRC]/USER/TIMER/timer.c:292-330` |
| 行切换（ABCD 二进制） | `[SRC]/USER/TIMER/timer.c:336-357` |
| 显存→物理重排（映射公式） | `[SRC]/USER/TIMER/timer.c:359-383` |
| 亮度 PWM（8 档） | `[SRC]/USER/TIMER/timer.c:271-288` |
| 定时器配置（3.5ms / 10µs） | `[SRC]/Main/main.c:40-42` |
| 引脚定义 | `[SRC]/Common/common.h:7-82` |
| HUB75 位带别名与"75 接口"注释 | `[SRC]/USER/LAMP/lamp.h:5-73` |
| GPIO 初始化 | `[SRC]/USER/LAMP/lamp.c:14-71` |
| 光敏→亮度映射（3~8） | `[SRC]/USER/ADC/adc.c:53-65`、`adc.c:73-98` |
| 协议显示命令（字号/颜色/X/Y） | `[SRC]/USER/LWIP_APP/UDP_SERVER.C:149-205` |
| 逻辑显存写入约定 | `[SRC]/USER/FUNC/func.c:5-45, 767-772, 835-840, 895-901` |

## 附录 B：STD 侧关键代码证据索引

| 主题 | 位置 |
|---|---|
| 基类字段/虚表 | `[STD]/Device/Inc/dev_display.h:30-70` |
| 通用 API（fill/draw_bitmap/set_pixel/brightness） | `dev_display.h:84-140`、`dev_display.c:193-250` |
| commit_frame / commit_frame_rect（④a 脏矩形） | `dev_display.c:37-93`、`dev_display.h:118-135` |
| scan_task 骨架（TIM3 事件 + OE/LAT 原子窗口） | `dev_display.c:110-172` |
| TIM3/TIM4 周期（500µs / 20µs） | `[STD]/Core/Src/tim.c:82-84, 118-120` |
| HUB75 引脚表与 BSRR 抽象 | `[STD]/Platform/Inc/pl_hub75.h:15-181`、`Platform/Src/pl_hub75.c:11-59` |
| 模组实例范式（参数宏/prepare/scan/init） | `[STD]/Device/Display/dev_display_1_263.c:33-243` |
| 引脚定义（与源逐条一致） | `[STD]/Core/Inc/main.h:74-193` |
| GPIO 输出配置 | `[STD]/Core/Src/gpio.c:58-107, 148-165` |
| 构建收录（Makefile 一处） | `[STD]/Makefile:302-304` |
| EIDE 收录与排除 | `[STD]/.eide/eide.yml:325-330（files）、424-440（Debug excludeList）` |
| 内存账本 | `[STD]/doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` |

## 附录 C：本次分析使用的外部资料（非仓库证据）

1. 立创商城 MBI5124GM 商品页（品牌/封装/概述/特性）—— `https://item.szlcsc.com/253063.html`（2026-09-14 抓取）。
2. MBI5124 数据手册（16 通道恒流 LED 驱动器，含 16 位移位寄存器 + 栓锁，25MHz）—— 检索结果指向 MacroBlock 官方 datasheet 转载页，未获取原件，仅用于交叉印证 1。

> 以上两条仅用于确认芯片类别与接口方向；**驱动实现不依赖外部资料**——位流顺序、时钟数、锁存/消隐时序全部以 9K23881580 源码为准（§1.1c、§1.5）。
