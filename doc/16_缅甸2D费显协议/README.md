# 16 缅甸 2D 费显协议（车道灯开关量控制，RS232 定长 20 字节帧）

> 〔2026-10-13〕本仓库 Makefile 已移除，构建仅 EIDE；本文中以 make 为入口的命令一律作废（历史记录），口径迁移见 doc/构建开关总表.md §7。

> **⚠ 2026-10-13 用户裁决：`.analysis/` 目录已从仓库移除且禁止再建。** 本文中所有 `.analysis/**` 引用
> （宿主脚本 / 推演与校验脚本 / 报告 / 抽取存档 / EIDE 备份）**一律作废、仅作历史线索保留**；对应文件
> 需要时从 git 历史取回：`git show 069be96:<.analysis 下路径>`。下文脚本结果列为历史记录。

> 协议文档：`/home/yystation/文档/创迪科技生产订单与技术资料/01 产品软件/产品协议文档/2D缅甸费显协议.doc`
> （标题《缅甸费显》，3 页，末次保存 2026-04-02；**全文实读**，抽取存档
> `.analysis/myanmar/doc_extract.txt`）。
> 模块：`Application/Src/ProtocolParser_Myanmar/` + `Application/Inc/ProtocolParser_Myanmar/`
> （前缀 `mm_` / 宏 `MM_`；全仓无既有 `mm_`/`MM_` 占用；与 doc/12_缅甸费显项目适配/README.md 未实施的旧缅甸方案
> `app_md_*` **不复用、不混用**）。
> 宿主推演：`.analysis/myanmar/mm_frame_sim.py`（**158 用例通过 / 失败 0**——2026-10-13 全仓编译期断言清理后，原「定长 20 字节编译期断言」检查改为「契约注释存在 + 宿主侧复算」，净 +1；2026-09-29 代码瘦身轮复跑计数为 157；原 159 条中的 4 条 RTT 日志断言已改写为「无 SEGGER/RTT 残留」+「行为修复保留」两类，检查强度不降。含源码常量/引脚/极性/接管顺序断言 + §10 箭头落点交换语义 + §11 开机默认画面几何）。
> EIDE 目标校验：`.analysis/myanmar/check_eide_targets.py`（**该脚本已随 `.analysis/` 于 2026-10-13 移除、不再可用**；
> 其历史结论：**2026-10-08 改写为「结构不变量」口径**——不再断言「Debug 必须 = 某集合」（协议/显示模组编不编属可按需调整的选择，
> `.eide/eide.yml` 是 GUI 地盘、外部不改），改为 I1~I8 八条结构不变量（显示驱动 ≤1 / 基类必编 / 出厂测试恰好 1 / `{` 帧族 ≤1（无
> `STD_ALL_PROTO`）/ `PROTO_CHONGQING` ⇒ CQ 模块 / 私有 `-I` 齐备 / 工程宏）+ 当前集合信息输出；
> 当时复跑 14/15（唯一失败 = Debug 的 I6：带 `PROTO_CHONGQING` 却排掉 CQ 模块，待用户在 GUI 二选一）——
> **2026-10-13 target 重组后该 I6 遗留已清零**（Base/Debug 基线不定义 `PROTO_CHONGQING`、CQ 模块在排除表）；
> 现行核对方式 = 按 I1~I8 清单人工核对 + 构建验证，见 `doc/构建开关总表.md` §7）。
> 出厂测试：TEST 键切换 `0x0A` ⇄ `0x0B` 箭头落点（`mm_arrow_map_toggle()`）+ 缅甸版出厂测试文件
> `Application/Src/app_factory_test_mm.c`（与标准版**二选一编译**，`FACTORY=STD|MM`）——见 **§13**。

> ⚠ **2026-09-29 代码瘦身轮：RTT 调试输出已整体移除**——本文件所有 `[mm]` / `[disp]` / `[diag]`
> 日志样例、`MM_RTT_DIAG` 门控与「按 RTT 判读」的现场步骤**不再可用**（`Application/Inc/app_diag.h`、
> 各 `*_RTT_DIAG` 与全部 SEGGER RTT 打印已删除）。**RTT 底层已恢复**（2026-09-29 第二轮：
> `Middlewares/Third_Party/SEGGER_RTT/` 三源文件 + `Platform/Src/pl_rtt.c` 在编译清单内、`hw_pl_initcall`
> 注册恢复；**业务代码零 RTT API 调用**，自研诊断设施保持删除）——要临时打点直接加 `SEGGER_RTT_printf(0, …)`。
> 现场判读仍靠：万用表/灯板实况（干接点）、调试器断点（`s_mm_lamp_state` / `g_tcp_server_connected`
> 等原 `volatile` 观察量已于 2026-10-13 裁决清理）、IAP 0x03 版本号与 `arm-none-eabi-size -A`。以下含 RTT 字样的段落**保留为历史记录**。
> 构建基线：`.analysis/cleanup_20260929/report.md`（第一轮）/ `round2_report.md`（第二轮）；
> **现行**（第二轮，本模块相关段尺寸）`FACTORY=MM` = text **166216** / rodata **195664** / bss 124504。

**状态**：`[解析层已落地]` `[干接点输出层已落地 = PE12/PE11/PE10 高有效推挽（2026-09-28 现场实测翻转极性），2026-09-24 用户裁决]`
`[Makefile PROTO=ALL/CQ DISP=1_263 链接通过 · 零新增告警]` `[宿主推演 158/158]`
**`[TEST 键箭头落点切换 + 缅甸版出厂测试已落地（FACTORY=MM，2026-09-28）]`**
**`[Myanmar target 已于 2026-10-13 按用户裁决整块删除——缅甸固件改按 doc/构建开关总表.md §7.2 第 5 条在 EIDE GUI 点选组出（源码与厂测文件均未变）；原目标结构与内存账见 §9（历史），基线口径见「现状」条]`**

> **〔现状 2026-10-13〕EIDE target 重组**：`Myanmar` target 已删除；现行 target = `Base`（**基线 / 重置模板**）、
> `Debug`（**与 `Base` 同口径、日常主用**；2026-10-13 全量 Rebuild 构建成功）；**`Release` target 亦已于
> 2026-10-13 按用户裁决整块删除**（原「已损坏、勿用」态）。
> 基线 = 核心代码 + IAP + 标准厂测 `app_factory_test.c` + RTT 底层 4 源 + 显示基类 `dev_display.c`；
> 7 个显示驱动 + 13 个协议目录 + `ah`/`rls`/`ldi` + `app_factory_test_mm.c` 全在排除表、`defineList` 仅
> `USE_HAL_DRIVER` / `STM32F407xx`。**缅甸固件配方 = 取消排除 `ProtocolParser_Myanmar` +
> 取消排除 `app_factory_test_mm.c`（同时排除标准版）+ 不编任何显示驱动 + 不定义 `PROTO_CHONGQING`**；
> 本模块的「只绑 RS232 / 干接点 / probe 判据 / 厂测切换」等行为说明**全部照旧有效**（源码零改动）。

---

## 1. 需求（用户原话要点）

1. 新建缅甸费显协议解析模块，**只使用 RS232 通信**（`CH_ID_RS232`），**默认 9600 波特率**。
2. **只解析车道灯控制指令**：
   - `90 01` 红灯开 / `90 00` 红灯关
   - `0a 01` 左下箭头灯开 / `0a 00` 左下箭头灯关
   - `0b 01` 右下箭头灯开 / `0b 00` 右下箭头灯关
3. 开关灯**不走常规 GPIO 通道**（不是 `dev_io_ctrl` 的 PD14 车道灯 / PD15 闪光灯），
   而是走**干接点 S1/S2/S3**：**红灯 = S1、左下箭头 = S2、右下箭头 = S3**。
4. 「有问题随时提出来」——查不清的不猜，写进 §8 交用户拍板。

---

## 2. 协议事实（文档全文实读）

### 2.1 通信参数（文档第 2 节原文）

| 项 | 文档值 |
|---|---|
| 接口 | **RS232 标准接口** |
| 波特率 | **9600** |
| 数据位 / 校验位 / 停止位 | 8 BIT / 无 / 1 BIT |
| 起始码 | **0xFFH** |
| 机器地址 | **0xE0H** |
| 命令长度 | **20 字节** |
| 结束码 | **5 字节** |

文档第 3 节另规定：费显处理器若不能及时处理，需对收到的命令**按队列「先到先处理」缓存**
（本实现用 FreeRTOS 静态消息队列，深度 3，语义一致）。

### 2.2 帧格式（文档第 4 节原文 = 定长 20 字节）

> 原文：**起始码，机器地址，命令方式字，[命令内容]，结束码** / 1字节 1字节 1字节 12字节 5字节
> 结束码：`00 00 00 00 00`

| 偏移 | 长度 | 字段 | 取值 |
|---|---|---|---|
| 0 | 1B | 起始码 START | `0xFF` |
| 1 | 1B | 机器地址 ADDRESS | `0xE0` |
| 2 | 1B | 命令方式字 COMMAND | 见 §2.3 |
| 3 | 1B | DATA0（金额第 6 位／**车道灯开关量取值位**） | 见 §2.3 |
| 4..14 | 11B | DATA1~DATA11（金额其余位 + 重量位） | 数码管命令用；**车道灯命令位不参与** |
| 15..19 | 5B | 结束码（文档表内 = DATA12~DATA16 = 结束字符5..1） | `00 00 00 00 00` |

**无校验字段**（文档未定义 CRC/BCC/XOR）。文档示例逐字节核对均为 20 字节：

```
0x10 例1：FF E0 10 3A 3A 3A 3A 31 30 3A 3A 3A 3A 32 35 00 00 00 00 00   （金额10 / 总重25）
0x90 例1：FF E0 90 01 3A 3A 3A 3A 3A 3A 3A 3A 3A 3A 3A 00 00 00 00 00   （红灯开）
0x0a 例2：FF E0 0a 00 3A 3A 3A 3A 3A 3A 3A 3A 3A 3A 3A 00 00 00 00 00   （0x0a 关）
```

> **⚠ 用户口径与文档的差异**：用户写「`90 01`」，指的是**命令字 + DATA0 两字节**；
> 完整线上帧是**上行 20 字节**（`FF E0 90 01 3A×11 00×5`）。本模块按**完整 20 字节帧**
> 实现（文档第 4 节 + 例 1/例 2 均为 20 字节）。若现场上位机实际只发 2 字节裸命令，
> 则本 probe 判据需改 —— **列为 §8 Q2（最高优先级待确认项）**。

### 2.3 命令表（文档第 5 节：文档**只列出 5 条**）

| 命令字 | 文档标题 | 参数语义 | 本模块处理 |
|---|---|---|---|
| `0x10` | 数码管正常显示 | DATA0~DATA11 十二位，**每位低 4 位 BCD**，`0x3A` = 该位不显示 | **不处理**（整帧 SKIP 消费，不渲染） |
| `0x40` | LED 显示亮度设置 | DATA0 = 亮度 `0x00`~`0x0A`（`0x0A` 最亮） | **不处理**（SKIP） |
| `0x80` | 报警控制 | DATA0 的 **bit2 = 黄闪报警**：1 开 / 0 关 | **不处理**（SKIP；用户需求只列三个车道灯） |
| `0x90` | 红色车道灯控制 | DATA0：**1 开 / 0 关** | **红灯（S1）** |
| `0x0A` | 「红色车道灯控制命令」（标题）／注文写「1 为**绿色**车道灯开」（**文档自相矛盾**） | DATA0：1 开 / 0 关 | **左下箭头灯（S2）** |
| `0x0B` | **文档中不存在**（全文无 `0x0b`，文档在 `0x0a` 例 2 之后即结束） | — | **右下箭头灯（S3）——按用户口径实现** |

**应答**：文档**未定义任何应答**（无查询命令、无回执约定）→ 本模块**不回任何应答**。
**帧切分依据**：定长 20 字节 + 起始码/地址双重判别（无长度字段、无校验）。

### 2.4 同源 8051 固件交叉核对（`/home/yystation/Program/3218008/MianDian_Fee`）

该目录是缅甸费显上一代 8051 主板的**原始源码**（`C51_UART.C` / `main.c` / `PORT.H`），
与本文档帧结构一致，可作旁证：

- 接收状态机：`FRAME_HEAD(0xFF) → FRAME_ADDR(0xE0) → 收 17 字节 → 第 20 字节`，
  `g_u16ResvLen = 18`、`memcpy(..., 19)` ⇒ **同样是 20 字节定长帧**。
- `C51_UART.C` 里算了 XOR（`tempxor = buf[2] ^ buf[3..18]`）但**比对语句被注释**
  （`// if(tempxor == tmpsbuf)`）⇒ **XOR 从未生效**；文档也未定义校验。
  → 本模块**不校验**结束码/无 XOR（末字节不参与判别，见 §6）。
- 命令映射：`0x80`→C1、`0x90`→C2、`0x0A`→C3；上电 `C1=C2=C3=0`（**灯全灭**）。
- **注意差异**：8051 的三路开关量叫 **C1/C2/C3**（`PORT.H`：C1=P2^2、C2=P2^1、C3=P2^3，
  排线输出），个数与命令映射都**不是**用户口径的 S1/S2/S3（且**没有 0x0B**）——
  本次以**用户口径**为准，该差异列入 §8 Q1/Q3。

---

## 3. 命令 → 灯位 → 干接点映射（本实现，2026-09-24 用户裁决落地；2026-09-28 现场实测修正极性）

| 命令字 | DATA0 | 灯位（用户口径） | 干接点 | MCU 引脚 | 本模块动作 |
|---|---|---|---|---|---|
| `0x90` | `0x01` / `0x00` | 红灯 | **S1** | **PE12** | 点亮（输出**高**）/ 熄灭（输出**低**） |
| `0x0A` | `0x01` / `0x00` | 左下箭头灯 | **S2** | **PE11** | 同上 |
| `0x0B` | `0x01` / `0x00` | 右下箭头灯 | **S3** | **PE10** | 同上 |

- 电气口径（2026-09-28 现场实测修正）：**推挽输出**（无上下拉、低速档）、**高电平有效**
  （点亮 = 输出**高** / 熄灭 = 输出**低**；3.3V = 点亮，证据见 §4.1）、
  **上电/复位默认显示红叉 = 红灯 S1/PE12 点亮（输出高）、两个绿箭头熄灭**（**2026-09-29 用户需求**，
  取代 2026-09-24 的「三路全灭」；协议帧到达后照常按帧驱动：红灯可被 `0x90 00` 关掉）、
  关灯 = **断开**（非脉冲）。
- 映射表 + 动作函数是**同一个改动点**：`Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c`
  的 `s_mm_lamp_map[MM_LAMP_COUNT]`（`contact/lamp_name/port/pin/active_low`）+ `_mm_drive()`。
  改脚位/电平/驱动方式**只改这一处**（Platform 侧接口不变）。
- **上电初始化顺序（无毛刺）**：`mm_apply_init()` 在 `sw_app_initcall` 内、建任务前调用 ——
  ① 先写 ODR 为「灭」电平（**高有效 ⇒ 低**；由映射表 `active_low` 推导，复用 `_mm_drive(m, false)`，
  不硬编码；此时引脚仍是输入 + 上拉，写 ODR 不驱动引脚）
  → ② `pl_gpio_release_exti()` 按线摘除 EXTI → ③ `pl_gpio_config_output()` 切推挽输出（此刻起真正驱动）
  → ④ **`mm_apply_lamp(MM_LAMP_RED, true)` 点亮红灯 = 上电默认红叉**
  （唯一动作点；原影子状态与 `[mm]` 诊断日志已分别于 2026-10-13 / 2026-09-29 清理；2026-09-29 用户需求，见 §14）；
  同时把**箭头落点交换开关复位为默认态**（见 §13，不做掉电保存）。
- **箭头落点可运行时交换（2026-09-28 新增，出厂测试用）**：`mm_arrow_map_toggle()` 把
  `0x0A` / `0x0B` 两条命令的**干接点落点**互换（`0x0A`→右下 / `0x0B`→左下），再调一次还原；
  **红灯 `0x90` 不受影响**、**开机回默认态**。上表是**默认态**的映射；交换态的完整语义、
  RTT 日志样例、消费者（TEST 键出厂测试）与 Makefile/EIDE 选择方式见 **§13**。
- **分层纪律**：Application 层**不含** HAL 头、不碰寄存器，只经 `pl_gpio_write()` /
  `pl_gpio_config_output()` / `pl_gpio_release_exti()`（宿主脚本断言覆盖）。
- 诊断（`MM_RTT_DIAG`，默认跟随 `APP_DIAG_BANNER`）：**每帧**打一行
  `[mm] S1 (红灯) <- ON(开灯) => PE12 HIGH (高有效:高=点亮)`（重复同值命令也打印 ——
  现场判「帧到了没有 / 输出动了没有」的第一现场证据；文案随映射表极性和电平自动分支）；
  上电接管完成另打一行摘要。

---

## 4. 干接点 S1/S2/S3：2026-09-24 用户裁决、2026-09-28 现场实测修正

### 4.1 裁决内容（引脚 / 电气口径）

> **用户裁决（2026-09-24）**：三路干接点**复用原 SW1/SW2/SW3 端子**，方向**由输入改为输出**。
> **现场实测修正（2026-09-28，取代当日的「低有效」初判）**：极性为 **高有效** ——
> 复位板子、不发任何命令时**红灯（红叉）即亮**，此时固件按原「低有效」配置把三路输出**高电平
> 3.3V**（初始化即全「灭」档 = 输出高）；发 `FF E0 90 00 …`（红灯关）后 **65 脚（PE12）对地
> 仍是 3.3V、灯仍亮** ⇒ **3.3V = 点亮 = 该灯高电平点亮**，原 `active_low = true` 反了
> （解析/执行链本身正常，RTT `[mm]` 执行日志存在）。

| 干接点 | 灯位 | MCU 引脚 | 有效电平 | 驱动方式 | 上电初始态 | 关灯语义 |
|---|---|---|---|---|---|---|
| **S1** | 红灯（`0x90`） | **PE12** | **高有效**（点亮 = 输出**高**） | **推挽**（无上下拉、低速档） | **点亮（红叉，2026-09-29 起重为默认态）** | 断开（非脉冲） |
| **S2** | 左下箭头灯（`0x0A`） | **PE11** | 同上 | 同上 | 灭（输出低） | 断开 |
| **S3** | 右下箭头灯（`0x0B`） | **PE10** | 同上 | 同上 | 灭（输出低） | 断开 |

- **上电默认显示红叉（2026-09-29 用户需求）**：三路接管完成后 **红灯 S1/PE12 点亮**（= 车道关闭态，
  安全默认）、两个绿箭头熄灭；**取代** 2026-09-24 沿用的同源 8051（`MianDian_Fee`，
  `C1=C2=C3=0`）「三路全灭」语义。协议帧到达后照常按帧驱动（红灯可被 `0x90 00` 关掉）。
  实现 = `mm_apply_init()` 末尾一句 `mm_apply_lamp(MM_LAMP_RED, true)`（唯一动作点，不新增电平硬编码），
  详细语义与备选见 §14。
- **单一改动点**：`app_mm_proto_cmd.c` 的 `s_mm_lamp_map`（`port/pin/active_low`）+ `_mm_drive()`；
  本轮就是「只改这一处」的实例（三条 `active_low` 由 `true` 改 `false`，驱动/接管代码零改动）。
- Platform 增量（Application 层不碰 HAL）：`pl_gpio_config_output()`（推挽输出）、
  `pl_gpio_release_exti()`（按线摘 EXTI）；驱动电平仍用既有 `pl_gpio_write()`。

### 4.2 EXTI 接管与安全论证（阶段二核心，逐条可复核）

PE10/PE11/PE12 原来是 **SW1/SW2/SW3 输入**（`Core/Src/gpio.c`：`GPIO_MODE_IT_FALLING` + `GPIO_PULLUP`，
EXTI 线 10/11/12 汇入 `EXTI15_10_IRQn`；`Device/IO/dev_key.c` 注册回调）。接管为输出时必须**只摘这三条线**：

1. **先读 HAL 源码（不凭记忆）**：
   - `HAL_GPIO_DeInit()`（`Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c`）—— 会清
     `EXTI->IMR/EMR/RTSR/FTSR` 与 `SYSCFG->EXTICR[n]`，但**只有在该线当前映射到本端口时才清**，
     且**全文件从不写 `EXTI->PR`**（grep 实证：该文件内无 `EXTI->PR`）；
   - `HAL_GPIO_Init()` —— 其 EXTI 配置块由 `if((GPIO_Init->Mode & EXTI_MODE) != 0)` 守卫，
     `GPIO_MODE_OUTPUT_PP` **不进入该分支** ⇒ 输出配置对 EXTI **零副作用**（同样不写 `PR`）。
   ⇒ 结论：**挂起标志必须自己清**，其余按线显式清最稳（不依赖 DeInit 的条件判断）。
2. **实现**（`Platform/Src/pl_gpio.c` 的 `pl_gpio_release_exti()`，逐线）：
   `EXTI->IMR &= ~line`（中断屏蔽）/ `EXTI->EMR &= ~line`（事件）/ `EXTI->RTSR &= ~line` 与
   `EXTI->FTSR &= ~line`（双边沿）/ `EXTI->PR = line`（**写 1 清挂起**；`rc_w1`）/
   `SYSCFG->EXTICR[pin >> 2]` 仅在「该线当前映射到本端口」时清 4 位（避免误清同号其它端口线）。
3. **为什么不影响 TEST 键与其它线**：
   - **不调用 `HAL_NVIC_DisableIRQ()`**（一刀切会连带杀掉整条 `EXTI15_10_IRQn` 上的其它线）；
     源码断言：全工程 `.c` 中无 `HAL_NVIC_DisableIRQ` 调用（宿主脚本 `mm_frame_sim.py` 校验）。
   - TEST 键 **PD8 走 `EXTI9_5`**，与本次操作无关；PE7/PE8（DIP1/DIP2）是纯轮询输入、本来不占 EXTI；
     操作对象只有 EXTI 线 10/11/12 三条，**其它 EXTI 线与 NVIC 使能位逐位不变**。
   - 操作顺序（无毛刺）：① 先写 ODR = 灭电平（**高有效 ⇒ 低**，由映射表推导；引脚仍输入 +
     上拉，写 ODR 不驱动引脚）→ ② 摘 EXTI → ③ 切推挽输出（此刻起真正驱动，电平 = 第 ① 步的
     「灭」= 低）→ ④ 三路全灭落定后**点亮红灯**（上电默认红叉，2026-09-29 新增；
     `mm_apply_lamp(MM_LAMP_RED, true)`）—— 接管阶段仍无毛刺：点亮发生在引脚已切成输出之后，
     是「稳定的低 → 稳定的高」，不存在竞争窗口。
   - **步骤 ① 之前的窗口**（复位 → `mm_proto_init`）：三脚仍是 CubeMX 的输入 + 内部上拉
     （`Core/Src/gpio.c`），高有效口径下外部呈**弱高**（是否足以点亮取决于灯板输入阻抗）——
     属**现场观察项**（若现场发现复位瞬间有可见闪亮，可把这三脚的 CubeMX 上拉改下拉或把接管
     提前到 hw_initcall；本轮不做，见 §4.3 与 §8）。
4. **上机抽查**（本轮已做的静态核对）：`objdump -d` 显示 `mm_apply_init` 的循环体依次调用
   `pl_gpio_write`（`_mm_drive` 内联）→ `pl_gpio_release_exti` → `pl_gpio_config_output`；
   `pl_gpio_release_exti` 反汇编中 `0x40013c00`（EXTI）偏移 0/4/8/0xC 为 BIC 清位、偏移 0x14（PR）
   为写 1，`0x40023800+0x44`（RCC_APB2ENR）置 `0x4000`（SYSCFGEN），`0x40013808`（SYSCFG EXTICR）
   按端口比较后清 4 位。

### 4.3 副作用与冲突后果（**预期行为**，非缺陷）

| 项 | 后果 | 处理 |
|---|---|---|
| **SW1/SW2/SW3 输入语义失效** | EXTI 不再触发（IMR 已按线清）；`dev_key_get_state()` 是**直读 GPIO**（`_exti_get_state = !pl_gpio_read`，低=「闭合」），仍能返回电平，但读的是**本板自己驱动的电平** | 已文档化（**高有效口径，2026-09-28 起**）：**灯灭（输出低）→ 对应 SW 位读为「闭合」**；**某灯点亮（输出高）→ 该 SW 位读为「未闭合」**。**上电默认（红叉亮，2026-09-29 起）= SW1（PE12 高）读「未闭合」、SW2/SW3（PE11/PE10 低）读「闭合」** |
| **RLS 干接点轮询受影响** | `app_rls_cmd.c` 的 `rls_dry_contact_poll` 在 `PROTO=ALL` 同编构建下据 SW1/SW2/SW3 渲染「ETC 专用 / ETC 人工 / 车道关闭」（优先级 车道关闭 > ETC专用 > ETC/人工）。**上电默认态（红叉亮）= SW3「车道关闭」（PE10 低 = 闭合）命中 ⇒ 约 2 个轮询拍（~200ms）后 RLS 持续渲染红字「车道关闭」**（与 2026-09-28 全灭态结论一致——全灭态三位全闭合，同样命中「车道关闭」优先级） | **缅甸量产构建排除 RLS 即无影响**（见 §9 的 EIDE Myanmar 目标）；`PROTO=ALL` 开发构建下该现象为**已知预期**；渲染互斥保证不撕裂画面（RLS 后续 bitmap 帧可正常覆盖） |
| `dev_key` 的 EXTI 回调注册残留 | `pl_exti_register_cb` 条目仍在，但该线不再产生中断 | 无害（不动 Platform 代码，避免影响其它构建） |
| `MX_GPIO_Init` 的 CubeMX 生成配置 | 上电仍先配成输入 + 上拉，运行到 `mm_proto_init` 时被本模块改配置 | 与 CubeMX 再生成**无冲突**（不在生成区改代码） |
| 上电窗口（复位 → `mm_proto_init`） | 三脚是输入 + 内部上拉 ⇒ **高有效口径下外部呈弱高**（旧极性下该窗口恰为「灭」，现可能被灯板判为「亮」——是否可见取决于灯板输入阻抗）；同时 RLS 在该窗口读「未闭合」 | 已记录为**现场观察项**；若现场发现复位瞬间可见闪亮，可将这三脚 CubeMX 上拉改下拉或把接管提前到 `hw_initcall`（本轮不做）。量产排除 RLS 后无 RLS 误判问题 |

### 4.4 历史调查（阶段一存档：为什么当时判「未查清」）

> 阶段一（本轮之前）曾按「查不清就不要猜」把输出层实现为**未接线占位**。下列调查路径保留，
> 供后续核对（引脚结论已由 §4.1 用户裁决取代）：

| 来源 | 结果 |
|---|---|
| `Core/Inc/main.h`、`Core/Src/gpio.c`（全部已配置引脚） | 无 `S1`/`S2`/`S3` 定义；DO 仅 `LED(PD9)` / `Lane_Light(PD14)` / `Flash_Light(PD15)` / `RS485_RE(PA8)` |
| `Device/IO/dev_io_ctrl.c` | `dev_io_lane_light`(PD14) / `dev_io_flash_light`(PD15) —— **用户已明确排除这一套** |
| `Device/IO/dev_key.c` + `Core/Inc/main.h` | `SW1/SW2/SW3` = **PE12/PE11/PE10**，`GPIO_MODE_IT_FALLING` + **内部上拉 = 输入**（本轮据此裁决复用为输出） |
| 协议文档 `2D缅甸费显协议.doc`（3 页全文） | **只定义 RS232 收发**，无任何输出端子/引脚/驱动方式描述 |
| 缅甸费显说明书（2024 版）接线座丝印图 | 只有 `RS232(RX/GND)` + `220V IN(L/N/⏚)`，**无 S1/S2/S3 端子** |
| 继电器控制板 `3015024` 丝印 DXF | `S1 / S2` 两个标号（无 S3），属 **AC220V 继电器板** |
| `2026-WS002 变更通知（2026-03-19）` | 「不带车道灯费显」「需要增加报警灯的外围控制命令」「RS232，9600」 |
| `2026-WS005 缅甸费显灯板` | 工作日志记载 2026-09-08 完成录入，**本机资料树中尚无该文件** |
| 桌面 `Netlist_主控板原理图_2026-09-24.tel` 的 `S1/S2/S3` | 器件为 `IRF7342`，属另一块「24V 网口版车道指示器控制板」，**不是 STD 费显主板 3833024**，不作依据 |
| STD 主板 `3833024` 丝印 DXF | 丝印存在 `S1/S2/S3`（器件注释 `Dz117-M33T6`），但不在 BOM 内、固件未配置 → 当时仅作线索 |

## 5. 帧头冲突纪律（RS232 槽 `0xFF` 家族）

RS232 槽（USART3）既有协议首字节：`0x0A`（四川 ETC）/ `0x5A`（安徽）/ `0x54`（贵州治超）/
`0x7B`（`{` 族：青海/山东/贵州/云南/四川 MTC/云南治超）/ **`0xFF`（四川治超 SC_OL）**。
本协议首字节 = **`0xFF`**，与**四川治超首字节重叠**（RLS 亦为 `0xFF`，但只绑 RS485，不同槽）。

四层判别链（**实测推演见 `mm_frame_sim.py` §6**）：

| # | 事实 | 后果 |
|---|---|---|
| ① | SC_OL probe 见本协议帧：`byte[1] = 0xE0 = 224` 落在其长度合法域 `[7,255]`（仅 `0xFE` 排除） | SC_OL 返回 **WAIT**（不 FAKE、不写 `*total_len`） |
| ② | 链式契约（doc/05_协议模块多协议兼容优化/01_architecture.md §3.1）：**WAIT 继续试下一协议** | 本 probe 同轮被调用 → **READY(20) 认领** → `any_parsed` 终止本轮、WAIT 预算失效 |
| ③ | 本 probe 对 `byte[1] ≠ 0xE0` **立即 FAKE**；且 SC_OL 命令字集合 `{80~88,94,96,98,99,A0~A8,B6,B8,B9}` 与本模块 `{0x90,0x0A,0x0B}` **无交集** | **不会误吞 SC_OL 帧** |
| ④ | 反向：SC_OL 帧长恰为 `0xE0` 且**半帧到达**时（SC_OL WAIT，链式到本 probe） | 本 probe 对**非本模块命令**要求「**结束码正签名**」`byte[15..18] == 00 00 00 00`（文档「结束字符5..2」）才 SKIP；正签名不成立 → **FAKE（不消费任何字节）** → 治超帧不被截断。残余风险 = 治超帧 `[15..18]` 恰为 4 个 0x00（实测推演 §6.5 明确标为已知残余风险，概率 `(1/256)^4` 量级） |

**结论：无需 EIDE 目录互斥、不定义 `g_brace_proto_guard`**（非 `{` 帧族），
可与现有全部协议同编共存。`0x90`/`0x0B` 在全部槽位均无既有占用（`0x90` 仅本模块）。

---

## 6. probe 判据（实现契约）

| # | 条件 | 返回 |
|---|---|---|
| ① | `avail == 0` | FAKE |
| ② | `byte[0] != 0xFF` | FAKE（首字节快拒） |
| ③ | `avail < 2` | WAIT |
| ④ | `byte[1] != 0xE0` | **FAKE**（机器地址不符；交外层按 1 字节重同步，**不消费整帧**） |
| ⑤ | `avail < 20` | WAIT（定长帧：帧长与命令无关，等齐再判） |
| ⑥ | `byte[2] ∉ {0x90,0x0A,0x0B}` **且** `byte[15..18] == 00 00 00 00` | **SKIP**（文档 0x10/0x40/0x80 等：结构合法、非本模块 → 整帧消费，避免 20 轮逐字节重同步噪声） |
| ⑦ | `byte[2] ∉ {…}` **且** 正签名不成立 | **FAKE**（见 §5 ④；防误吞同槽治超长帧） |
| ⑧ | `byte[3] > 0x01`（本模块命令但 DATA0 越界） | **SKIP**（SC_OL 命令集与 `0x90/0x0A/0x0B` 无交集 → 该分支无误吞风险） |
| ⑨ | 其余 | **READY**，`*total_len = 20` |

- **结束码不校验取值**：文档定义为 `00 00 00 00 00`，但同源 8051 把末字节当 XOR（算出但从不比对）；
  为免「上位机真填 XOR → 整帧全拒」，判别只用到 `byte[15..18]`（4 字节），**末字节 [19] 不参与**
  （`MM_TAIL_SIG_LEN = MM_TAIL_LEN - 1`）。
- `*total_len` 只在 SKIP / READY 分支写出（doc/05_协议模块多协议兼容优化/01_architecture.md §3.2 规则 3）。
- 队列：深度 **3**，`MM_MSG_SIZE = sizeof(frame_msg_t) + 20`；队列体 + cb + 任务帧缓冲**置 CCMRAM**。

---

## 7. 模块设计与接入点

### 7.1 文件清单

| 文件 | 内容 |
|---|---|
| `Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h` | 帧常量（定长 20B 契约注释）、命令/灯位枚举、解析结果、API、**干接点契约（PE12/PE11/PE10 高有效，2026-09-28 现场实测）** 说明 |
| `Application/Src/ProtocolParser_Myanmar/app_mm_proto.c` | **注册次序（as-built）**：acquire RS232 槽 → `app_proto_register`（probe）→ `app_proto_bind_channel(CH_ID_RS232)` → 注册开机默认画面 → `mm_apply_init()` 接管干接点（**建任务前完成**，避免上电沿出现未定义灯态窗口）→ 建静态队列 → `app_proto_set_frame_queue` + 建处理任务（栈/TCB 静态落 CCMRAM）+ **开机默认画面「红色叉（×）」注册**（`_mm_default_red_cross()` / `_mm_default_display_init()`，**仅带显示驱动时注册**，见 §14.3） |
| `Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c` | 帧解析（纯函数）+ **干接点输出单一映射点**（`s_mm_lamp_map` + `_mm_drive`）+ 上电接管与**默认红叉**（2026-09-29，见 §14.2）+ **0x0A⇄0x0B 箭头落点运行时交换**（`mm_arrow_map_toggle()` / `_mm_effective_lamp()`，见 §13） |
| `Application/Src/app_factory_test_mm.c` | **2026-09-28 起**：缅甸版出厂测试（TEST 键切换箭头落点；与标准版 `app_factory_test.c` **二选一编译**，见 §13）。不引用任何显示 API（无显示构建安全）；栈/TCB 静态落 CCMRAM（256 words，同标准版） |
| `Platform/Inc/pl_gpio.h` · `Platform/Src/pl_gpio.c` | **本轮增量**：`pl_gpio_config_output()`（推挽输出）+ `pl_gpio_release_exti()`（按线摘 EXTI） |
| `Makefile`（**已于 2026-10-13 移除，本行仅作历史**） | `-I Application/Inc/ProtocolParser_Myanmar` + 两个 `.c`（`SRC_APPLICATION` 末尾，Anhui 之后）；**本轮增量**：`FACTORY ?= STD`（`STD`/`MM` 二选一，非法值 `$(error)`；开关值进 `.build_stamp`；`SRC_FACTORY_TEST` 决定收录标准版还是缅甸版） |
| `.eide/eide.yml` | **2026-09-28 本轮增量（历史；2026-10-13 target 重组后口径见 §9 顶部「现状」条）**：`files` 收录 `app_factory_test_mm.c`；Debug / Myanmar 目标 `excludeList` 增 `<virtual_root>/Application/app_factory_test.c`（= 编缅甸版、排标准版，恰好一个；见 §13.3）；缅甸模块两文件收录与 `targets.Myanmar`〔已删〕同前 |
| `.analysis/myanmar/mm_frame_sim.py`（**已随 `.analysis` 移除**，结果列为历史记录） | 宿主 probe/parse 推演 + 源码断言（**当时 158/158**——2026-10-13 编译期断言清理后原断言存在性检查改「契约注释 + 宿主复算」，净 +1；此前瘦身轮删除 RTT 断言后由 159 收敛为 157，见 `.analysis/cleanup_20260929/report.md`；含引脚/极性/上电默认态/接管顺序/EXTI 纪律 + **§10 箭头落点交换语义与不变量** + **§11 开机默认画面「红色叉（×）」几何与源码断言** + Makefile `FACTORY` / 缅甸版出厂测试文件断言；**2026-09-28 起期望值 = 高有效**，**2026-09-29 起上电默认 = 红叉亮**） |
| `.analysis/myanmar/check_eide_targets.py`（**已随 `.analysis` 移除**） | **EIDE 目标校验（结构不变量口径）+ 编译集/CCM 预算**（**2026-10-08 改写**：只校验 I1~I8 结构不变量 + 输出当前集合；不再锁「Debug 必须 = 某集合」；`.eide/eide.yml` 只读不改）。**现行 14/15：唯一失败 = Debug 的 I6（`PROTO_CHONGQING` 定义但 CQ 模块未编入 = C3 混编，待用户在 GUI 二选一）**；另有与 `build/<target>/builder.params` 的对拍（差异只报信息）与 `--emit-make [--target T]`（生成等价口径，无显示时 `SRC_DISPLAY` 置空） |

### 7.2 接入清单（对照 doc/08_协议模块接入规则/04_接入检查清单.md）

- [x] probe 纯函数（只 `rb_peek`，无 `rb_read`/`rb_skip`/全局写）
- [x] `avail == 0` → FAKE；首字节快拒；`*total_len` 仅 READY/SKIP 写
- [x] 长度/地址上下界 + 正签名双重判别
- [x] 静态队列 + payload ≤ RB_SIZE_RS232（契约注释）
- [x] 任务栈 256 words、优先级 `osPriorityNormal`、栈/TCB 走 `pl_task_static.h`（CCMRAM）
- [x] **未绑** `CH_ID_RS232_1`（USART6 语音 TX 专用）、未绑 RS485/网口
- [x] **干接点真实驱动**（PE12/PE11/PE10 **高有效**推挽、**上电默认显示红叉 = 红灯 S1/PE12 点亮（2026-09-29 起，取代「上电全灭」）**、按线摘 EXTI；见 §4/§14）
- [x] 〔历史（make 时代）〕`make -j8`（`PROTO=ALL` / `PROTO=CQ`）零新增告警（仍 3 条既有 HAL）——**2026-10-13 起等价判据 = EIDE 全量重建零新增告警**
- [x] doc/05_协议模块多协议兼容优化/01_architecture.md（§3.3 / §4.1 / §6 变更记录）、doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md、doc/CLAUDE.md、本文件 已更新
- [x] **EIDE 已收录**：〔2026-10-13 更新〕原独立 `targets.Myanmar`（阶段三口径 =「无显示驱动 + 无 `PROTO_CHONGQING`」，配套无显示构建的运行时守卫见 §10）**已于 2026-10-13 删除**；**缅甸固件改按 `doc/构建开关总表.md` §7.2 第 5 条在 GUI 点选组出**（取消排除本模块 + 缅甸版厂测 + 不编显示驱动 + 不定义 `PROTO_CHONGQING`）。〔历史〕2026-09-28~2026-10-13 期间 Debug 曾「编入本模块、排除 LDI + 四川 ETC/MTC/治超」（见 §9.1，已作废）

### 7.3 波特率（默认 9600 的落地方式）

协议要求 **9600 8N1**。工程用 **DIP1 拨码**全局选波特率（`Application/Src/app_uart_baud.c`：
`OFF = 9600 / ON = 115200`，同时作用于 RS485 + RS232）。因此：

- **本模块不改任何波特率逻辑**（最低侵入，避免与既有 DIP1 语义冲突）；
- **现场必须把 DIP1 置 OFF（9600）**，否则 RS232 全部协议（含本模块）在 115200 下收不到数据；
- 若后续要求「无论 DIP1 如何本模块都 9600」，需要新增端口级波特率覆写（当前 `pl_uart_set_baud`
  是整端口级，会把 RS485 一起改）——列入 §8 Q5。

---

## 8. 待确认问题清单（★ 需用户拍板）

| # | 问题 | 影响 / 现状 |
|---|---|---|
| **Q1** | ~~S1/S2/S3 对应哪个 MCU 引脚？驱动方式？有效电平？上电初始态？关灯是「断开」还是「脉冲」？~~ | ✅ **2026-09-24 用户裁决并落地，2026-09-28 现场实测修正极性、2026-09-29 用户需求改上电默认态**：**S1=PE12 / S2=PE11 / S3=PE10**，复用原 SW1/SW2/SW3 端子改为**输出**；**高电平有效（点亮 = 输出高 / 熄灭 = 输出低；实测 3.3V = 点亮，取代 2026-09-24 的「低有效」初判）、推挽、上电/复位默认显示红叉 = 红灯 S1/PE12 点亮（2026-09-29 取代「三路全灭」）、关灯=断开**。实现与安全论证见 §4；副作用（RLS 干接点误判）见 §4.3；上电默认红叉与开机默认画面见 §14 |
| **Q2** | 现场上位机是否发送**完整 20 字节帧**（`FF E0 90 01 3A×11 00×5`）？还是只发 2 字节裸命令（`90 01`）？ | 文档与例 1/例 2 均为 20 字节定长帧，本实现按此。若上位机只发 2 字节，probe 判据需整体改写（并会与 `0x0A` 的四川 ETC 首字节冲突重新出现） |
| **Q3** | `0x0B`（右下箭头）在协议文档中**不存在**（文档只到 `0x0A`）；`0x0A` 文档标题写「红色车道灯」、注文写「绿色车道灯」。**请提供 0x0B 的出处** | 本实现按用户口径实现 `0x0B`（`doc_extract.txt` 已确认原文无 `0x0B`）。若 `0x0B` 是笔误，删一条命令即可 |
| **Q4** | 结束码 5 字节现场实际取值？（文档 `00 00 00 00 00`，同源 8051 末字节为 XOR 但从不比对） | 本实现**不校验**结束码取值（只把 `[15..18]` 当正签名用于 SKIP 分支）。若现场确认恒为 `00×5`，可收紧为全 5 字节校验 |
| **Q5** | 「默认 9600」是否需要**脱离 DIP1** 强约束（即 DIP1 置 ON 时本模块仍 9600）？ | 当前 = 不改波特率逻辑，靠 **DIP1 置 OFF** 落地（§7.3）。若需强约束，需新增端口级波特率覆写机制（影响 RS485 或需拆分端口配置） |
| **Q6** | 是否需要在**缅甸量产构建**里同时保留四川治超（SC_OL）/ 四川 ETC / 四川 MTC / LDI / IAP？ | ✅ **2026-09-24 用户裁决（第三阶段）：保留 —— 协议编译集保持「继承 Debug 全集」**，一行不裁。理由：四者首字节 `0xFF` 的共存已做到双向不误吞（§5，宿主推演 §6 覆盖），同编即可；如将来要更小的量产集，在 GUI 里把对应协议目录加进排除表即可（一行一处；原 `targets.Myanmar.excludeList` 已随该 target 于 2026-10-13 删除，同义操作见 `doc/构建开关总表.md` §7.2） |
| **Q7** | 是否需要**报警灯控制**（`0x80` bit2 黄闪）与**亮度**（`0x40`）？ | 本次按用户需求**只做三个车道灯**；文档的 `0x80`/`0x40`/`0x10` 目前整帧 SKIP（不动作）。2026-WS002 变更通知明确要求「增加报警灯的外围控制命令」→ 很可能是后续需求 |
| **Q8** | **EIDE 显示模组口径**：`targets.Myanmar` 是否要编显示驱动？ | ✅ **2026-09-24 用户裁决（第三阶段）：不带显示驱动** —— 〔当时〕`targets.Myanmar.excludeList` 排除 `Device/Display/` 下**全部** `dev_display_*.c`（含当时编入的 `dev_display_1_969.c` 与全部备用模组 p20/1_263/1_260/22_1703/22_1665/1_577）；基类 `dev_display.c` **保留**（注册表 / 守卫 / 通用像素 API 宿主）。**代价与防护**：无显示时全部渲染路径必须能安全跳过 —— 本阶段补齐运行时 NULL 守卫，见 **§10**。实测（等价口径 `CONFIG=EIDEMyanmarNoDisp`）：`.ccmram **16084B**`（余 49452B）、链接通过、`Device/Display/` 下只编 `dev_display.c`（`nm` 无任何模组驱动符号）。**〔2026-10-13：该 target 已删除，同义口径 = 点选时不取消排除任何显示驱动，见 `doc/构建开关总表.md` §7.2 第 5 条〕** |
| **Q9** | **EIDE `defineList` 仍沿用 Debug 的 `PROTO_CHONGQING`**（⇒ 该目标默认网络配置走 CQ 口径 IP 192.168.1.5 / TCP 9528 / udp_port 20103），但**目标内不编 CQ 协议**（CQ 目录在排除表内）。缅甸是否需要自己的网络默认口径？ | ✅ **2026-09-24 用户裁决（第三阶段）：去掉 `PROTO_CHONGQING`** —— 〔当时〕`targets.Myanmar.cppPreprocessAttrs.defineList` 为 `USE_HAL_DRIVER` / `STM32F407xx`（= Debug 去掉该宏），网络默认回落 **dev 口径**（192.168.114.200 / 网关 .1 / TCP 9528 / CQ 业务口固定 20103）；缅甸不用网络，此改动只影响默认 IP 文本与端口绑定对象。**〔2026-10-13：该 target 已删除；Base/Debug 基线本身就不定义 `PROTO_CHONGQING`，点选缅甸固件时保持基线 defineList 即同义〕** |

---

## 9. EIDE 独立缅甸目标〔**已于 2026-10-13 按用户裁决整块删除**〕（阶段二落地、阶段三改口径）与内存账

> **⚠ 现状（2026-10-13）**：`Myanmar` target 已删除（175 行），**需要缅甸固件时改按 GUI 点选现场组出**：
> 取消排除 `ProtocolParser_Myanmar`（两文件）+ 取消排除 `app_factory_test_mm.c` 并排除
> `app_factory_test.c`（两文件同名符号，必须恰好一个）+ **不编任何显示驱动** +
> **不定义 `PROTO_CHONGQING`**（Base/Debug 基线本来就没有）；配方见 `doc/构建开关总表.md` §7.2 第 5 条。
> 同轮 `Debug` 也已清成基线（排缅甸、排 LDI/四川三、编标准厂测）。**本节下方内容为 `targets.Myanmar`
> 存在期间的目标结构与内存账（历史）**——除标注「现状」者外一律按历史理解；**内存数字仅对
> 各自标注的口径/日期成立**。

### 9.1 目标结构（`.eide/eide.yml`）（历史）

| 项 | 内容 |
|---|---|
| `files` | 虚拟文件夹 `Application > protocol > ProtocolParser_Myanmar`，收录 `app_mm_proto.c` / `app_mm_proto_cmd.c`（**EIDE 的文件树只收 `.c`**；`.h` 走 incList）。**本阶段一字未动**。另：`Application` 虚拟文件夹收录 `app_factory_test.c` **与** `app_factory_test_mm.c`（2026-09-28 新增，二选一由排除表决定，见下一行） |
| **出厂测试二选一（2026-09-28 新增）** | 〔当时〕`targets.Debug` 与 `targets.Myanmar` 的 `excludeList` 均含 `<virtual_root>/Application/app_factory_test.c` ⇒ 两目标**恰好编入缅甸版** `app_factory_test_mm.c`（标准版被排除）。**现状（2026-10-13）**：Base/Debug 基线反过来 = 编标准版、排缅甸版；要缅甸版在 GUI 把两处勾选互换（`doc/构建开关总表.md` §7.2 第 5 条）；互斥机制不变（见 §13.3） |
| `targets.Debug` | 〔**历史口径（2026-09-28 用户裁决当时）；已于 2026-10-13 作废**〕**编入缅甸模块**（不在其 `excludeList` 内）、**排除 `ldi` + 四川 ETC/MTC/治超**；显示模组为 `dev_display_1_969.c`。**编入 = IAP(2) + 缅甸(2) + `dev_display_1_969.c`（193 源文件**，实测 = EIDE 自己写的 `build/Debug/builder.params` `sourceList` 193 条，见 §9.2）。**更早历史口径（阶段三基线，2026-09-24 修复轮一度恢复）= 排缅甸、编 IAP + LDI + 四川三（206 源文件）**。**现状 = Base/Debug 基线（190 个 .c，排缅甸/排 LDI/排四川三、编标准厂测）**，见顶部「现状」条 |
| `targets.Myanmar` | 〔**该 target 已于 2026-10-13 整块删除**；下表为删除前的阶段三口径，仅作历史〕以 Debug 为模板：① 不排除缅甸模块；② `incList` 增 `Application/Inc/ProtocolParser_Myanmar`；③ **不带显示驱动**——`excludeList` 排除 `Device/Display/` 下全部 `dev_display_*.c`（新增一行 `dev_display_1_969.c`，其余 6 个阶段二已在表内）；④ **`defineList` 去掉 `PROTO_CHONGQING`**（同义口径现由点选表达：`doc/构建开关总表.md` §7.2 第 5 条） |
| 排除表 | 〔历史〕`targets.Myanmar.excludeList` = **阶段三基线 Debug 排除集**（历史口径）− {缅甸} + {`dev_display_1_969.c`}（**`targets.Myanmar` 自身未随 2026-09-28 的 Debug 口径变化**）；**协议排除项一条未加**（Q6 裁决：保留 LDI / 四川 ETC/MTC/治超） |
| defineList | `USE_HAL_DRIVER` / `STM32F407xx`（**无 `STD_ALL_PROTO`、无 `PROTO_CHONGQING`**）：编入的 `{` 帧族只剩四川 MTC 一个 ⇒ `g_brace_proto_guard` 不冲突 |
| toolchain / uploader | GCC / **JLink**（`tool/flash_all.sh`）。注：Debug 的 uploader 是 `Custom`，缅甸目标是 JLink —— 属阶段二既有差异，本阶段按「不回退既有内容」保持现状（`check_eide_targets.py` 已把它钉成基线快照） |

⚠ **改完 `.eide/eide.yml` 必须在 EIDE 里执行 `EIDE: Reload Project`**（EIDE 会用内存模型回写 yml，
外部编辑会被冲掉；见 doc/CLAUDE.md「EIDE 持有 eide.yml 纪律」），重载前不要做任何会触发保存的 GUI 操作。
**2026-09-24 修复轮即为该纪律的又一次实例**（§9.1.1）。

> **2026-09-28 复现观测（极性轮，未修改 yml）**：缅甸极性现场翻转轮的收尾复跑发现
> `check_eide_targets.py` 报 **30/35**（当时脚本 35 条断言；5 条失败全指向 `targets.Debug.excludeList`；
> **2026-09-28 出厂测试轮把脚本扩到 45 条断言后，同口径为 40/45**）：
> 其内容 = **EIDE 回写后的口径**（与 2026-09-24 修复轮记录的改动前状态相同）——
> **缺「缅甸」排除**、且**含** `ldi` + 四川 ETC/MTC/治超的排除（即 Debug 编入缅甸、
> 不编 LDI/四川三协议，与阶段三基线「Debug 编 LDI+四川三、排缅甸」相反）。同期证据：`build/Debug/builder.params`（EIDE 自己写）
> 的 `sourceList` **含缅甸两文件、`/LDI/` 计数 0、无四川三协议**、`defines` 含 `PROTO_CHONGQING`
> —— 与「Debug 编缅甸」一致；且 **EIDE 在本轮会话期间（10:02~10:03）仍在活动并回写**
> `build/Debug/Project_STD.objlist` / `builder.params` / **`.eide/eide.yml`（mtime 10:03:05）**，
> 说明该回写是**持续行为**、非一次性事故。现场快照与脚本输出归档
> `.analysis/myanmar/polarity_flip_20260928/eide_yml_state_20260928.txt`。**本轮按用户指令
> 未动 `.eide/eide.yml`**。
>
> **2026-09-28 用户裁决：暂保持现行口径（Debug 编入缅甸、排除 LDI + 四川 ETC/MTC/治超），
> 不恢复阶段三排除表**——Debug 正好编入缅甸，便于当前现场调试（LDI/四川以后再说）。
> **该口径下 `check_eide_targets.py` 预期 40/45（5 条失败 = 口径差异，非回归；2026-09-28 出厂测试轮扩断言前为 30/35）**；
> （本节上表 `targets.Debug` 行已按本裁决改写为**当时**实态；「阶段三基线」一律作历史口径标注。）
> **如需恢复阶段三基线**（非现行口径，可选路径）：在 **EIDE GUI 的目标设置里**把「缅甸」加回 Debug 排除表、
> 并去掉 `ldi` 与四川三协议的排除（纯文件编辑会被再次回写），随后 `EIDE: Reload Project`，
> 再以 `check_eide_targets.py`（期望 **35/35**）复核。
>
> **〔2026-10-13 更新：以上「暂保持现行口径」整段作废（历史）〕** —— 用户已按新用法对 EIDE 目标重组：
> 新增 `Base` 基线、`Debug` 清成同一基线、**`Myanmar` target 删除**；协议/显示驱动不再靠「维持某个
> 排除表口径」，而是随时按需点选。上文的「恢复阶段三基线 / 35/35 复核」等操作（含已移除的
> `check_eide_targets.py`）不再执行。

### 9.1.1 2026-09-24 修复轮：EIDE 回写改坏 `targets.Debug` 排除表（当轮已修复；后于 2026-09-28 再次被回写，用户裁决**暂保持**，见 §9.1）

**症状**：EIDE 构建 Debug 目标失败 —— `Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c:40:10:
fatal error: app_mm_proto.h: No such file or directory`（两个缅甸文件都失败），且 EIDE 的编译命令
**不带 `-IApplication/Inc/ProtocolParser_Myanmar`**、却带 `-DPROTO_CHONGQING`（= Debug 口径在编缅甸）。

**根因（取证链，全部落盘可复核）**：

1. EIDE 实际编译清单 `build/Debug/builder.params`（17:37 版，现备份为
   `.analysis/myanmar/eide_writeback_20260924/builder.params.debug_before_fix`）：
   `sourceList` **193 条（含缅甸）**、`incDirs` **33 条（无缅甸）**、`defines` 含 `PROTO_CHONGQING`。
2. 阶段三基线 `build/Debug/Project_STD.objlist`（17:31，206 条）**含 LDI + 四川三协议对象、不含缅甸**
   ⇒ 17:31 时 Debug 的排除表是阶段三口径（编 LDI/四川三、排缅甸）。
3. `check_eide_targets.py` 当轮复跑 5 条断言失败，恰好覆盖：Debug 丢「缅甸」排除、Debug 误加
   `ProtocolParser_ldi` + 四川 ETC/MTC/治超 排除（阶段三基线里这 4 条**不在** Debug 排除表内）。
4. `.eide/eide.yml` 的 mtime（17:35/17:40）落在上述两次证据之间，且 EIDE 日志
   （`~/.config/Cursor/logs/<session>/…/8-eide-log.log`）显示同期多次 `Updating Compiler Database`
   与 `--only-dump-compilerdb` 调用以 `code=134`（SIGABRT；.NET 10 跑 .NET 6 工具的兼容问题）失败。
   ⇒ **EIDE 用内存模型回写 yml**，覆盖了外部编辑（丢失范围：① Debug 的「缅甸」排除条目；
   ② Debug 对 `ldi` / 四川 ETC/MTC/治超 的「不排除」状态）。
5. 修复过程中另观测到 EIDE 在 17:40:27/17:40:48 两次回写（编辑被部分覆盖）——最终改用
   「一次性原子写入 + 回读校验 + 4s 稳定性复验」落盘，此后 EIDE 17:42 重新生成清单即为修复版。

**修复（`.eide/eide.yml` 共 3 处，纯增量/恢复）**：

- `targets.Debug.excludeList`：**+** `<virtual_root>/Application/protocol/ProtocolParser_Myanmar`；
  **−** `<virtual_root>/Application/protocol/ldi` 与三条 `ProtocolParser_SiChuang_{ETC,MTC,Overload}`
  （后 4 条为恢复阶段三基线，即「Debug 编入 LDI + 四川三」）。
- `targets.Debug.incList` + `targets.Release.incList`：**+** `Application/Inc/ProtocolParser_Myanmar`
  （**兜底**：即使将来有人误把缅甸编进某目标，也不会因缺 `-I` 而炸；仅头文件搜索路径、零副作用；**`targets.Release` 已于 2026-10-13 按用户裁决删除，本行保留为历史记录**）。
- 未动 `files` 树、未动 `targets.Myanmar`、未动其它 target。

**验证（2026-09-24）**：

- 修复后 EIDE 重新生成 `builder.params`：`sourceList` **206 条（无缅甸、含 LDI+四川三）**、
  `incDirs` **34 条（含缅甸）**；`check_eide_targets.py`（已按新口径更新两条 incList 断言）
  **35/35 全绿**（含与 params 逐条对拍）；`check_eide_command.sh` 20/20。
- **EIDE 实机构建通过**（终端日志 16：`rebuild mode 206 文件` → 链接 RAM 91.72% / CCMRAM 80.83% /
  FLASH 33.66% → `build successfully`，退出码 0）；用 `TMPDIR=/home/yystation/tmp-mm
  DOTNET_ROLL_FORWARD=LatestMajor dotnet <eide>/unify_builder.dll -p build/Debug/builder.params --rebuild`
  亲手复跑，结果同尺寸同退出码；产物 `nm` 缅甸符号 0、LDI 符号在。
- 手工编译一正一反：Myanmar 口径（+缅甸 incDir、无 `PROTO_CHONGQING`）两文件**编过**；
  原报错用**修复前 incDirs（33 条）**可逐字复现 ⇒ 根因确认为「缺 `-I`」。
- `TMPDIR=/home/yystation/tmp-mm make -B -j8`（默认 `PROTO=ALL`/`DISP=1_263`）：
  `Build complete`、rc=0、`0 error / 3 warning`（全部为 HAL `stm32f4xx_hal_flash_ex.c` 既有
  `-Wunused-parameter`）⇒ make 路径未受影响。**注**：make 与 EIDE 共用 `build/Debug/` 产物，
  本轮跑完 make 后已把 EIDE 产物整目录恢复，避免口径混淆（纪律见 doc/CLAUDE.md「构建卫生」）。

### 9.2 编译集（实证，非推测；**以下均为 `targets.Myanmar` / 旧 Debug 存在期间的口径，2026-10-13 起为历史**）

`.analysis/myanmar/check_eide_targets.py`（**该脚本已随 `.analysis/` 于 2026-10-13 移除**；历史结果：**阶段三基线口径 35/35**；阶段二 19/19、阶段三 34/34、
2026-09-24 修复轮改两条 incList 断言后 35/35；**2026-09-28 出厂测试轮新增 11 条断言（两文件入 `files` /
每目标恰好编入一个 / 是缅甸版 / 排除标准版 / 地面真相差异只允许「空 / 整对」）后共 45 条——当时 Debug
口径下预期 40/45（5 条失败 = 口径差异，非回归）**；用例只增不减）按 EIDE 的
排除语义推导编入/排除清单，并与 EIDE 上次构建留下的 `build/Debug/builder.params`（**IDE 自己写的
源文件清单**，**当时 193 条**）**逐条对拍一致**：

- **Debug 编入（〔当时〕口径，推导 = 实测，193 源文件）**〔**2026-09-28~2026-10-13 期间口径，现已作废**：
  现行 Base/Debug = 基线（190 个 .c，编 IAP(2) + 标准厂测 + `dev_display.c`，排缅甸/LDI/四川三及各协议）〕：
  平台 + Kernel + Core + HAL + LwIP/FreeRTOS/RTT +
  cJSON + **IAP(2) + 缅甸(2)** + `dev_display_1_969.c`；
  **排除**：山东 / 青海 / 贵州常规 / 贵州治超 / 云南常规 / 云南治超 / 重庆 / 安徽 / AH / RLS
  **+ LDI(5) + 四川 ETC(3)/MTC(4)/治超(3)** + 全部非 1_969 显示驱动。
  （**历史口径 = 阶段三基线**：编入 IAP + LDI + 四川三、排除缅甸 = 206 源文件。）
- **Myanmar 编入（目标自身口径未变）**〔**该 target 已于 2026-10-13 删除**；下集即点选缅甸固件时的编入集〕 = 阶段三基线编入集 + 缅甸两文件 **− 显示驱动
  `dev_display_1_969.c`**（**207** 源文件）⇒ 四川治超 / 四川 ETC / 四川 MTC / LDI / IAP **全部保留**
  （Q6 裁决；**现状点选配方可另行裁剪**，见 `doc/构建开关总表.md` §7.2 第 5 条）；**`Device/Display/` 下只编基类 `dev_display.c`**（守卫与注册表宿主，必须保留）。
- **编译期口径**：无 `PROTO_CHONGQING`（网络默认回落 dev 口径）、无 `STD_ALL_PROTO`。

### 9.3 CCMRAM 预算与等价口径实测（**链接通过**）

EIDE 子集的固定 CCM 消费者 = **13644B**（静态任务栈 + TCB + 内核 idle/timer 缓冲 + `dev_display.c`
自身的 scan 静态存储 + 对齐；由两次实测互证）+ 缅甸模块自身 **2440B**：

| 口径 | CCM 用量 | 结论 |
|---|---|---|
| 〔当时〕**缅甸目标 = 无显示驱动，Q8 裁决** | 13644 + 2440 = **16084B** | 链接通过，**余 49452B**（实测 `.ccmram 16084`） |
| 历史（阶段二继承 1_969，192×96） | 13644 + 2440 + 38208 = 54292B | 链接通过，余 11244B |
| 参考（22_1665 8×4，128×64） | 13644 + 2440 + 46080 = 62164B | 链接通过，余 3372B |

- **等价口径复现**〔2026-10-13 更新〕：**Makefile 已移除，等价口径构建一律走 EIDE** ——
  **在 EIDE GUI 里对当前 target 按需点选**（取消排除 `ProtocolParser_Myanmar` 两文件 + 缅甸版厂测
  两处勾选互换 + 保持 `Device/Display/` 下全部 `dev_display_*.c` 排除、基类 `dev_display.c` 保留 +
  不定义 `PROTO_CHONGQING`）后直接 Build；配方与不变量清单见 `doc/构建开关总表.md` §7.2 第 5 条。
  〔历史〕原做法 = 新建/复制 target 或 `check_eide_targets.py --emit-make` 生成等价口径 Makefile
  （产物落 `.analysis/myanmar/build_nodisp/`，`CONFIG=EIDEMyanmarNoDisp`；脚本已随 `.analysis` 移除、
  `make BUILD_DIR=<dir>` 亦随 Makefile 移除作废）。
  （**阶段三首次实测**：`.text 138720 / .rodata 197056 / .data 1584 / .ccmram 16084 / .bss 116060 /
  ._user_heap_stack 2564` ⇒ **SRAM 合计 120208B（余 10864B）**、**CCM 余 49452B**；207 个源文件、
  零新增告警（仍 3 条既有 HAL）。`Device/Display/` 下只有 `dev_display.o`，`nm` 无任何模组驱动符号）。
  **2026-09-28 厂测轮复测（出厂测试改缅甸版）**：
  `.text **137328** / .rodata **196968** / .data 1584 / .ccmram **16084** / .bss **115796** /
  ._user_heap_stack 2564` ⇒ **SRAM 合计 119944B（余 11128B）**、**CCM 余 49452B**；207 个源文件、
  链接通过、零新增告警（相对标准版出厂测试口径 text −1392 / rodata −88 / bss −264，`ccmram`/`data` 不变）。
  **2026-09-29 复测（上电默认红叉 + 开机默认画面红叉轮）**：
  `.text **137632** / .rodata **197000** / .data 1584 / .ccmram **16084** / .bss **115796** /
  ._user_heap_stack 2564` ⇒ **SRAM 合计 119944B（余 11128B）**、**CCM 余 49452B**；相对厂测轮
  text **+304** / rodata **+32**（红叉回调编译进对象；无显示 ⇒ 运行时不注册、`[disp]` 告警无新增）。
  **2026-09-29 第二轮（RTT 底层恢复 + build 清理，本条为现行）**：
  `.text **131928** / .rodata **191968** / .data 1584 / .ccmram **16084** / .bss **115704** /
  ._user_heap_stack 2560` ⇒ **SRAM 合计 119848B（余 11224B）**、**CCM 余 49452B**；相对上条
  text **−5704** / rodata **−5032**（= 同日瘦身轮删除全部自研诊断打印 + 注释精简的净效果，其中
  **RTT 底层恢复已回加 text +448 / rodata +40**）；同口径「RTT 移除态」（瘦身轮第一轮）为
  `.text 131480 / .rodata 191928`。产物落 `.analysis/myanmar/build_nodisp/EIDEMyanmarNoDisp/`
  （**该产物目录已于 2026-10-12 清理，重跑 `Makefile.mm_nodisp` 即重建**）、
  elf md5 `ce7fd155adac088b782f5b18cafe23d0`、207 个源文件；生成物按现行 yml 重生成。
- ⚠ **注意区分**：Makefile 的 `PROTO=ALL` / `PROTO=CQ` 口径编 22_1665(8×4) 仍是**预存**的链接期
  CCMRAM 溢出（**17700B / 15444B**，2026-10-08 实测；`doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` §8.4.2）——那是「全协议 + 全部任务栈」口径；
  **无显示口径（原 `targets.Myanmar`；现按不编任何显示驱动点选）不存在该问题**（余量 49KB）。

---

## 10. 无显示驱动构建的边界防护（阶段三核心）

无显示口径（〔历史〕原 `targets.Myanmar`；**该 target 已于 2026-10-13 删除，现 = 点选时不编任何显示驱动**）
不编任何显示模组驱动 ⇒ **`dev_display_get()` 恒返回 `nullptr`**。此时「解引用显示
实例」的每一处运行路径都必须能**安全跳过 / 降级**，否则上电即 HardFault（`dev_display_start` 是
`sw_initcall`，启动链上必然执行）或定时任务空指针（光敏 1s 周期）。

### 10.1 设计原则

- **不新增编译期开关**：一律运行时判空（避免又一套 `#ifdef` 口径与 `-D` 地狱，也避免错误开关组合）。
- **守卫助手**：`Device/Inc/dev_display.h` 的 `bool dev_display_guard(const char *what)`（实现见
  `Device/Display/dev_display.c`）——有显示时**只多一次判空**（返回 true，调用方路径逐条不变）；
  无显示时返回 false（调用方安全跳过/降级）。〔2026-09-29 瘦身轮：原「无显示时限速打印
  `[disp] no display driver linked -> skip <what>`」的告警已删除；`what` 形参保留供调试器观察。〕
- 分层：**Application 不碰 HAL**（原「告警直用 SEGGER RTT」的通道已随瘦身轮移除）。

### 10.2 覆盖路径清单（`dev_display_get()` / 显示 API 使用点）

| # | 路径（改动后文件:行） | 无显示时的行为 | 守卫形态 |
|---|---|---|---|
| 1 | `Device/Display/dev_display.c` `dev_display_start` | **不创建 `scan_task`、不启动 TIM3/TIM4** | `dev_display_guard("scan_task")` |
| 2 | `Device/Display/dev_display.c` `_on_tim4_period`（PWM ISR） | 判空直接返回（TIM4 未启动，本不会触发） | `if (dev == nullptr) return;` |
| 3 | `Device/Display/dev_display.c` `set_pixel` / `set_brightness` / `fill` / `draw_bitmap` | 写操作安全丢弃（纵深防御，覆盖未逐点审计的调用方） | `if (!dev) return;` |
| 4 | `Application/Src/app_boot.c` `app_splash_display` | 跳过开机画面（含 5s 延时） | `dev_display_guard("splash")` |
| 5 | `Application/Src/app_default_display.c` `_render_welcome` | 跳过默认欢迎画面 | `dev_display_guard("default-display")` |
| 6 | `Device/IO/dev_light_sensor.c` `dev_light_sensor_auto_adjust` | 跳过调光（采样继续但不写 `light_level`） | `if (dev == nullptr \|\| dev->display == nullptr) return;` |
| 7 | `Application/Src/app_factory_test.c` `factory_monitor_task` | **整体降级为待机**（出厂检测全部步骤均为屏上判读，无对象可测；TEST 键 EXTI 保留但不产生动作） | `if (dsp == nullptr) { warn; for(;;) osDelay(1000); }` |
| 8 | `Application/Src/app_factory_test.c` `_aging_fill_screen` | 直接返回（纵深防御，task 已整体降级） | `if (!dsp) return;` |
| 9 | `Application/Src/app_test.c` `app_test_pixel_scan` / `app_test_render_text` | 直接返回（`app_test_run()` 本就注释未启用） | `if (!dsp) return;` |
| 10 | `Application/Src/LDI/app_ldi_device.c` `display_show`（E6 显示） | **按协议回 `LDI_DEV_FAIL`** ⇒ 应答 `status=0x01`（不静默假装成功） | `dev_display_guard("ldi.e6")` |
| 11 | `Application/Src/LDI/app_vms_ctrl.c` `vms_display_ctrl`（E9 VMS） | 安全跳过（`vms_ctrl` 返回 void，无应答字段） | `dev_display_guard("ldi.vms")` |
| 12 | `Application/Src/RLS/app_rls_cmd.c` `cmd_display` / `cmd_display_save` | 安全跳过（旧代码在复合字面量里直接 `dev_display_get()->screen_rows`，**是真实空指针缺陷**） | `if (!d) return;` |
| 13 | `Application/Src/AH_MQTT/ah_mqtt.c` `ReportTask` | `run_sta` 报 `'0'`（与「屏未开」同义） | 三元判空 |
| 14 | `Application/Src/AH_MQTT/ah_mqtt_cmd.c` `cmd_display` | 只跳过渲染，NotifyID 更新与应答照常 | `if (dsp != nullptr) { … }` |
| 15 | `Application/Src/ProtocolParser_Myanmar/app_mm_proto.c` `mm_proto_init` / `_mm_default_display_init` / `_mm_default_red_cross` | **不注册**开机默认画面（红叉）⇒ 回落到系统默认欢迎画面路径（该路径本身已有守卫，见 #5）；红叉回调内再判空一次 | `if (dev_display_get() != nullptr) app_default_display_register(…)` + 回调内 `if (d == nullptr) return;` |

**已具备防护、本阶段无需改动**（审计确认）：`app_render()` 入口 / `app_render_save` / `app_render_restore` /
`app_render_draw_glyph_clipped` 全部判空 `s_render_display`；`app_scroll.c` 四处（`app_scroll_start`
返回 false ⇒ 滚动引擎（含 `scroll_task`）根本不会被创建；`_render_slot` / `_scroll_tick` /
`_scroll_stop_clear_nolock` 判空）；`dev_display_commit_frame` / `commit_frame_rect` 判空；
`app_boot.c` 横幅 `if (d != nullptr) … else`（无显示打 NULL 行）；四川 ETC/MTC/治超的 12 处 `if (!d) return;`；
`app_ldi_device.c` 的 `display_clear` 路径（经 `app_render`，已判空）。

### 10.3 判据与复跑

> **下表的「工具」列脚本（`check_display_guards.py` / `check_guard_disasm.py` 等）已随 `.analysis/` 移除**，
> 结果列 = 当时的判据记录（历史）；现行等价复核 = §10.1 的判空清单逐条对照源码 + `objdump` / `nm` 抽查。

| 判据 | 工具 | 结果 |
|---|---|---|
| 守卫硬断言 **16 条**（2026-09-29 起，+2 = 缅甸红叉注册/回调判空）+ 全仓 `dev_display_get()` 调用点分类（**2026-09-29 瘦身后 = 95 处**） | `.analysis/myanmar/check_display_guards.py` | **16/16 通过；95/95 调用点已判空/三元（0 处待确认）** |
| 「有显示时零行为变化」机器级对照（寄存器名归一化的操作序列，原操作零丢失） | `.analysis/myanmar/check_guard_disasm.py` | **当前函数 92 个（改动 27 个）原操作丢失 0**；若干「仅新增守卫指令」+ 已复核的代码生成差异（三元折叠 / `cbz`→`cmp+beq` / 寄存器重排 / 编址方式），逐条列在脚本输出里（函数总数随 A/B 基线集合与瘦身后代码变化；判据恒为**原操作零丢失**） |
| 无显示目标的可执行证据 | `objdump` / `nm` / `strings`（见 §9.3 等价口径 ELF） | ① `dev_display_start` 首条即 `bl <dev_display_guard>` + `cbnz`，为假时 `add sp/pop` 直接返回（**未走到 `osThreadNew`**）；② `pl_tim_start_it` 的调用者**只有 `scan_task`**（×2）且 `MX_TIM3/4_Init` 只 `HAL_TIM_Base_Init`（不 Start）⇒ 无显示时 TIM3/TIM4 永不启动；③ `app_splash_display` / `_render_welcome` 首条即守卫 + `cbnz`；④ `factory_monitor_task` 为 `cmp r0,#0` + `beq.w` 跳降级分支；⑤ ELF 内含 `[disp] no display driver linked -> skip %s` 与 `[diag] display dev_display_get()=NULL` 两条字符串 |

### 10.4 无法安全降级的路径（明确列出，不静默）

| 路径 | 状态 | 说明 / 方案 |
|---|---|---|
| 出厂检测的**全部步骤** | **降级为待机**（非崩溃） | 固件信息 / 七色满屏 / 通信灯文案 / 老化循环 / 斜扫都必须在屏上判读；无显示时 TEST 键**不产生任何动作**。若将来要求「无显示构建下 TEST 键也有用」，需为这些步骤单独定义**无屏判据**（如仅跑 IO 灯 + RTT 打点）——当前不做，避免在量产件里塞无用代码 |
| LDI E6 显示命令 | **回失败**（`status=0x01`） | 上位机会看到「执行失败」——这是**诚实**的语义（设备确实无屏），优于静默返回成功 |
| LDI E9 VMS | **静默跳过** | `vms_ctrl` 返回 void、协议无该路应答字段；只能跳过。已在 §10.2 #11 记录 |
| 光敏自动调光 | 任务保留、动作跳过 | 保留任务可让 `g_light_sensor_task_handle` 始终有效（出厂检测里的 suspend/resume 无需再判空）；无显示时它每秒采样一次 ADC、不产生副作用（可接受） |

### 10.5 有显示时的零行为变化（论证 + 证据）

- **论证**：所有守卫形态都只在「无显示」时生效——`dev_display_guard()` 在 `s_active_display != nullptr`
  时**立即返回 true**，`if (!dev) return;` / `if (dsp == nullptr)` 在有显示时不成立；守卫之后的所有语句
  与改动前**逐字相同**（源码 diff 为纯增量）。
- **证据**：① 标准口径 `PROTO=ALL` `DISP=1_263` 与 `PROTO=CQ` 全量重编**链接通过、零新增告警**，
  段尺寸增量仅来自守卫代码本身（text +224 / rodata +96 / bss +8，见
  `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` 变更记录）；
  ② `check_guard_disasm.py` 逐函数机器码对照（100 个函数、19 个改动、原操作零丢失）；
  ③ 守卫分支只在 `dev_display_get()==nullptr` 时进入（有显示时仅一次判空；原 `[disp]` 输出已随
  2026-09-29 瘦身轮删除）。

---

## 11. 现场联调建议帧与预期现象

| 目标 | 帧（hex，20B） | 预期（诊断开启时） |
|---|---|---|
| 红灯开 | `FF E0 90 01 3A 3A 3A 3A 3A 3A 3A 3A 3A 3A 3A 00 00 00 00 00` | RTT：`[mm] S1 (红灯) <- ON(开灯) => PE12 HIGH (高有效:高=点亮)`；**万用表实测 PE12 = 3.3V**（灯亮） |
| 红灯关 | `FF E0 90 00 3A 3A 3A 3A 3A 3A 3A 3A 3A 3A 3A 00 00 00 00 00` | `[mm] S1 (红灯) <- OFF(关灯) => PE12 LOW …`；PE12 ≈ 0V（灯灭） |
| 左下箭头开 | `FF E0 0A 01 3A 3A 3A 3A 3A 3A 3A 3A 3A 3A 3A 00 00 00 00 00` | `[mm] S2 (左下箭头灯) <- ON … => PE11 HIGH`；PE11 = 3.3V（亮） |
| 右下箭头开 | `FF E0 0B 01 3A 3A 3A 3A 3A 3A 3A 3A 3A 3A 3A 00 00 00 00 00` | `[mm] S3 (右下箭头灯) <- ON … => PE10 HIGH`；PE10 = 3.3V（亮） |
| 数码管显示（应被消费不动作） | `FF E0 10 3A 3A 3A 3A 31 30 3A 3A 3A 3A 32 35 00 00 00 00 00` | 无 `[mm]` 行（probe SKIP） |

**前置条件**：DIP1 置 **OFF（9600）**；RTT 通道 0 已接（J-Link/CMSIS-DAP SWD）；
`MM_RTT_DIAG`（默认跟随 `APP_DIAG_BANNER`）为 1，否则 RTT 无输出（`make APP_DIAG=0` 时整段消除）。
**上电默认态（2026-09-29 起）**：红灯（红叉）**点亮** —— 上表「红灯关」一行可用于把它关掉；
「左下箭头开 / 右下箭头开」一行用于点亮绿箭头。**上电自检**见本节末段。

**上电自检**：复位后先看一行 `[mm] dry contacts taken over: S1=PE12 / S2=PE11 / S3=PE10 (push-pull, active-high; boot default = S1 red X ON, arrows OFF; EXTI released per-line)`，
紧跟一行 `[mm] S1 (红灯) <- ON(开灯) => PE12 HIGH (高有效:高=点亮)`（**上电默认红叉**）；
随后三脚应实测为 **PE12 = 3.3V（红叉亮）、PE11 / PE10 ≈ 0V（绿箭头灭）**。

---

## 12. 修订记录

| 日期 | 内容 |
|---|---|
| 2026-09-24（阶段一） | 新建：协议文档全文实读（定长 20 字节帧 / 5 命令 / 无校验 / 无应答 / 9600 RS232）+ 同源 8051 源码交叉核对；模块落地（只绑 `CH_ID_RS232`、队列与任务栈落 CCMRAM、probe 九条判据含抗误吞正签名）；干接点 S1/S2/S3 引脚调查（**未查清 → 未接线占位**）；宿主推演 `mm_frame_sim.py` 77/77；构建 `PROTO=ALL DISP=1_263` 零新增告警（text +672 / rodata +416 / ccmram +1320 / bss +16）；EIDE 未收录 |
| 2026-09-24（阶段二） | **① 干接点输出层落地**：用户裁决 **S1/S2/S3 = PE12/PE11/PE10**（复用原 SW1/SW2/SW3 端子改为输出）、**低有效推挽、上电全灭、关灯=断开**（⚠ 其中「低有效」极性已于 **2026-09-28 现场实测翻转为高有效**，见本表末行；其余结论不变）；Platform 新增 `pl_gpio_config_output()` + `pl_gpio_release_exti()`（**按线摘 EXTI，不碰 NVIC/其它线**，HAL 源码依据见 §4.2）；副作用（RLS 干接点误判、量产排除即无影响）见 §4.3。**② 宿主推演** 77 → **103/103**（新增 §9 引脚/极性用例 + 接管顺序 / 单一映射点 / EXTI 逐线清理 / 分层纪律断言）。**③ 构建**：`PROTO=ALL DISP=1_263` = text 176064 / rodata 203544 / data 1656 / ccmram 39652 / bss 124852 / heap_stack 2564（SRAM 余 2000B、CCM 余 25884B）；`PROTO=CQ` = text 162868 / rodata 202888 / data 856 / ccmram 37396 / bss 121376 / heap 2560（SRAM 余 6280B、CCM 余 28140B）；**零新增告警**；A/B 增量 text +1088 / rodata +504 / data ±0 / ccmram +1320 / bss +16（相对阶段一占位实现 = text +416 / rodata +88，其余 ±0）。**④ EIDE**：新增 `targets.Myanmar`（Debug 模板 + 缅甸模块 + 排除云南治超），Debug 目标行为不变（**当时口径；2026-09-28 用户裁决起 Debug 现行 = 编入缅甸、排除 LDI + 四川三，见本表末行**）；新增校验脚本 `check_eide_targets.py`（19/19，含与 EIDE 构建清单对拍）；等价口径**实测链接通过**（1_969 ⇒ CCM 54292 余 11244B；22_1665(8×4) ⇒ CCM 62164 余 3372B）。**未烧录、未提交、未跑实机** |
| 2026-09-24（阶段三，本轮） | **用户三裁决**：① 协议构建集**保持继承 Debug 全集**（Q6，不裁 LDI/四川三协议）；② 缅甸目标**不带显示驱动**（Q8，`excludeList` 排除 `Device/Display/` 下全部 `dev_display_*.c`，含原编入的 `dev_display_1_969.c`；基类 `dev_display.c` 保留）；③ **去掉 `PROTO_CHONGQING`**（Q9）。`.eide/eide.yml` 改动 = 删 1 行 + 增 1 行（**`targets.Debug` 与 `files` 一字未动**，YAML 合法性已校验）。**① 无显示构建的边界防护（本阶段核心）**：审计全部 `dev_display_get()` 使用点（97 处）并补齐运行时 NULL 守卫——新增守卫助手 `dev_display_guard()`（限速告警前缀 `[disp]`，**无编译期开关**）覆盖 14 条路径（`dev_display_start` 不建 `scan_task`/不启 TIM3/TIM4、splash、默认画面、光敏调光、出厂检测降级待机、LDI E6 回 `LDI_DEV_FAIL`、LDI E9 跳过、RLS 两处、AH 两处等），像素 API 加纵深防御判空；详见 §10。**② 验证**：标准口径 `PROTO=ALL DISP=1_263` = text 176288 / rodata 203640 / data 1656 / ccmram 39652 / bss 124860 / heap_stack 2564（**SRAM 余 1992B**、CCM 余 25884B）；`PROTO=CQ` = text 163060 / rodata 202976 / data 856 / ccmram 37396 / bss 121384 / heap 2560（SRAM 余 6272B）；**两口径零新增告警**（仍 3 条既有 HAL）；守卫代码 A/B 增量 = text **+224** / rodata **+96** / bss **+8**（守卫助手静态变量）/ data·ccmram **±0**。**无显示等价口径实测链接通过**（`CONFIG=EIDEMyanmarNoDisp`，207 源文件）：`.text 138720 / .rodata 197056 / .data 1584 / .ccmram **16084** / .bss 116060 / heap_stack 2564` ⇒ SRAM 余 **10864B**、CCM 余 **49452B**；`Device/Display/` 下只编 `dev_display.c`，`nm` 无模组驱动符号。**③ 脚本**：`check_eide_targets.py` 19 → **34/34**（新增「无显示 / 无 `PROTO_CHONGQING` / Debug 保持现状 / 显示驱动全排除 / 基类保留」等断言，`--emit-make` 支持无显示口径、`SRC_DISPLAY` 置空）；新增 `check_display_guards.py`（**14/14 硬断言 + 97/97 调用点分类**；**2026-09-29 起随缅甸红叉注册点扩为 16/16 + 99/99，见本表末行**）与 `check_guard_disasm.py`（**100 函数 / 19 改动 / 原操作零丢失**，证明「有显示时零行为变化」）；`mm_frame_sim.py` **103/103**、`check_eide_command.sh` **20/20** 复跑全绿。**④ 文档**：doc/16 §8（Q6/Q8/Q9 裁决）+ §9（新目标口径与实测）+ **新增 §10 无显示构建的边界防护**、doc/CLAUDE.md、doc/06-04、`.analysis/myanmar/README.md` 已同步。**未烧录、未提交、未改 git 配置**；**改完 `.eide/eide.yml` 必须在 EIDE 里 `EIDE: Reload Project`** |
| 2026-09-24（修复轮，本轮） | **EIDE Debug 构建报错修复**：症状 = `app_mm_proto.h: No such file or directory`（EIDE 的 Debug 清单里混入缅甸两文件、incDirs 却无其路径）。根因 = **EIDE 用内存模型回写 `.eide/eide.yml`**（同期其 `--only-dump-compilerdb` 调用多次 `code=134` SIGABRT），把 `targets.Debug.excludeList` 改坏 —— 丢失范围：①「缅甸」排除条目；② `ldi` + 四川 ETC/MTC/治超 的「不排除」状态（硬证据 = 阶段三 `Project_STD.objlist` 206 条含 LDI+四川三、不含缅甸）。修复 = ① `targets.Debug.excludeList`：**+缅甸、−`ldi` 与四川 ETC·MTC·治超**（恢复阶段三基线）；② **兜底**：`targets.Debug` 与 `targets.Release` 的 `incList` 均补 `Application/Inc/ProtocolParser_Myanmar`（误编入也不缺 `-I`）；`files` / `targets.Myanmar` / 其它内容未动。验证 = `check_eide_targets.py` 由 5 条失败 → **35/35 全绿**（其中两条 incList 断言按「兜底」新口径更新，其余三条由 yml 修复转绿）、`check_eide_command.sh` 20/20；**EIDE 实机构建 206 文件通过**（链接 RAM 91.72% / CCMRAM 80.83% / FLASH 33.66%，`build successfully`；`unify_builder --rebuild` 亲手复跑同尺寸同退出码；产物 `nm` 缅甸符号 0 / LDI 符号在）；手工编译一正一反（Myanmar 口径编过；用修复前 incDirs 逐字复现原报错）；`make -B -j8`（`PROTO=ALL`/`DISP=1_263`）`0 error / 3 warning`（全部 HAL 既有）、`Build complete`。证据归档 `.analysis/myanmar/eide_writeback_20260924/`；详录 §9.1.1。**未烧录、未提交、未改 git 配置** |
| **2026-09-28（极性现场实测翻转，本轮）** | **现场实测推翻 2026-09-24 的「低有效」初判 ⇒ 改为高有效**。证据（用户 2026-09-28 提供）：① 复位板子、不发任何命令时**红灯（红叉）即亮**，此时固件按「低有效」把三路输出高电平 3.3V；② 发 `FF E0 90 00 …`（红灯关）后 **65 脚（PE12）对地仍 3.3V、灯仍亮** ⇒ **3.3V = 点亮 ⇒ 高电平点亮**（RTT `[mm]` 日志正常，解析/执行链无问题）。**改动（最小改动 = 单一映射点）**：`app_mm_proto_cmd.c` 的 `s_mm_lamp_map` 三条 `active_low` 由 `true` 改 `false` + 同文件/`app_mm_proto.h` 的极性注释与接管日志文案随动（`_mm_drive` 电平表达式 `on != active_low` **一字未改**；`mm_apply_init()` 的「先写灭电平」复用 `_mm_drive(m, false)` = 同一真源，新极性下即「灭 = 输出低」）；诊断行文案由 `m->active_low` 自动分支 ⇒ 现打「高有效:高=点亮」，无需改文案。**影响面（已同步文档）**：① 全灭 = 输出低；② SW1/SW2/SW3 读数为**反相**——全灭态三路 SW 全读「闭合」，`PROTO=ALL` 同编构建下 RLS 干接点轮询约 200ms 后持续显示「车道关闭」（量产排除 RLS 即无影响，§4.3）；③ 复位→`mm_proto_init` 窗口内三脚为输入 + 上拉（弱高），列为现场观察项（§4.3）。**脚本**：`mm_frame_sim.py` §9 期望值全部翻转 + 新增 5 条源码断言（三项 `active_low = false`、无 `true` 残留、诊断文案「高有效:高=点亮」、接管日志 `active-high`、无 `active-low` 残留）⇒ **103 → 108/108**；`check_display_guards.py` / `check_eide_targets.py` / `check_guard_disasm.py` 复跑全绿（14/14、原操作零丢失；`check_eide_targets.py` 当时 **35/35**，收尾复跑因 EIDE 回写转 **30/35**，见本行末）。**未烧录、未提交、未改 git 配置**；**未动 `.eide/eide.yml`、显示驱动几何宏、`dev_display_1_577.c`**。**另**：收尾复跑 `check_eide_targets.py` 发现 Debug 排除表又被 EIDE 回写成「缺缅甸排除 / 含 `ldi` + 四川 ETC·MTC·治超排除」形态（脚本 **30/35**，与 2026-09-24 修复轮记录的改动前状态一致；EIDE 在本轮会话期间 10:02~10:03 仍在回写 `eide.yml` / `builder.params`）——**本轮按指令未动 yml**，证据与现状裁决见 §9.1「2026-09-28 复现观测」（**该注记在 §9.1，非 §9.1.1**）。**口径裁决（2026-09-28）：暂保持现行 EIDE 口径（Debug 编入缅甸、排除 LDI + 四川 ETC/MTC/治超），不恢复阶段三排除表；该口径下 `check_eide_targets.py` 预期 30/35（口径差异，非回归）** |


| **2026-09-28（TEST 键箭头落点切换 + 缅甸版出厂测试，本轮）** | **新增：TEST 键切换 0x0A / 0x0B 干接点落点**（默认态 `0x0A→左下 S2/PE11` / `0x0B→右下 S3/PE10`；交换态互换；**红灯 0x90 不受影响**；**不做掉电保存**，`mm_apply_init()` 开机复位）。**单一生效点** = `mm_execute_cmd()` → `_mm_effective_lamp()`（LEFT⇄RIGHT 两支）：probe 判据 / 帧解析（纯函数）/ 队列 / `s_mm_lamp_map` / `_mm_drive` 极性表达式**零改动**；`[mm]` 日志按最终灯位取表 ⇒ 如实反映实际驱动的灯与引脚。**新增公开 API** `mm_arrow_map_toggle()` / `mm_arrow_map_is_swapped()`（`MM_RTT_DIAG` 门控打一行 `[mm] TEST: arrow map swapped|restored -> 0x0A=… / 0x0B=…`，引脚与标签由映射表推导）。**新增 `Application/Src/app_factory_test_mm.c`**（缅甸版出厂测试：TEST 键只做映射切换，不进入标准版 11 步序列；对外符号与标准版同名同义 —— `app_factory_mode_interrupt()` 空实现 / `app_factory_test_active()` 恒 false / `g_factory_test`；任务栈 256 words 静态落 CCMRAM；**不引用任何显示 API**）；与 `app_factory_test.c` **二选一编译**（同编 **实测** `arm-none-eabi-ld -r` 报 3 个 multiple definition = 天然互斥守卫）。**Makefile 新增 `FACTORY ?= STD|MM`**（默认 `STD` 保持现状、非法值 `$(error)`、开关值进 `.build_stamp`；`SRC_FACTORY_TEST` 决定收录）；**EIDE**：`files` 收录新文件 + Debug / `targets.Myanmar` 的 `excludeList` 均增标准版条目（恰好编入缅甸版）；`check_eide_targets.py` 新增 11 条断言（**30/35 → 40/45**，5 条失败仍为 Debug 现行口径差异）；`mm_frame_sim.py` **108 → 142/142**（新增 §10 交换语义 10 条 + 源码不变量 12 条）。**构建实测**（`TMPDIR=… make -B -j8`，`PROTO=ALL DISP=1_263`）：`FACTORY=STD` = text **176336** / rodata **203648** / data 1656 / ccmram 39652 / bss 124860 / heap_stack 2564（SRAM 余 **1992B**）；`FACTORY=MM` = text **174944** / rodata **203616** / data 1656 / ccmram 39652 / bss 124596 / heap_stack 2564（SRAM 余 **2256B**）；两口径**零新增告警**（仍 3 条既有 HAL）。驱动级 A/B（同树摘除交换代码复编）：`mm_parse_frame` / `mm_apply_lamp` / `_mm_drive` **反汇编逐条一致**，新增仅 `_mm_effective_lamp` 28B / `mm_execute_cmd` +6B / `mm_apply_init` +8B / `mm_arrow_map_toggle` 168B + 日志串 78B / `s_mm_arrow_swapped` 1B（STD 构建下 toggle/日志被 `--gc-sections` 丢弃，链接后仅 text +48 / rodata +8）。**文档**：本文件 §3 / §7.1 / §9.1 / **新增 §13** + `doc/构建开关总表.md` §3 + `doc/CLAUDE.md` 已同步。**未烧录、未提交、未改 git 配置**；**改完 `.eide/eide.yml` 必须在 EIDE 里 `EIDE: Reload Project`** |
| **2026-09-29（上电默认红叉 + 开机默认画面，本轮）** | **用户需求**：「在使用缅甸协议解析时……**顺便修改成开机默认显示红叉**」（与 2026-09-28 同文重申；同轮还要求 TEST 键切换 0x0A/0x0B —— 见下条「已具备」）。**① TEST 键切换 = 已具备（核实，零代码改动）**：逐条比对用户描述与 2026-09-28 实现（默认 `0x0A→S2/PE11`、`0x0B→S3/PE10`；按一次互换、再按还原；红灯 `0x90` 不受影响；不持久化；`_mm_effective_lamp()` 单一生效点；极性 `active_low=false` 未动；EIDE Debug / Myanmar 两目标均编 `app_factory_test_mm.c`）—— 完全一致，未改一行。**② 上电默认红叉（车道灯板，主解释）**：`mm_apply_init()` 接管完成后 `mm_apply_lamp(MM_LAMP_RED, true)` ⇒ **红灯 S1/PE12 点亮（输出高）= 车道关闭态**、两个绿箭头熄灭；**取代** 2026-09-24 沿用的「三路全灭」；唯一动作点、影子状态与诊断日志同步、接管顺序仍无毛刺。**③ 开机默认画面红叉（屏，备解释；仅带显示驱动构建）**：`app_mm_proto.c` 新增 `_mm_default_red_cross()` + `_mm_default_display_init()`（`dev_display_get() != nullptr` 才 `app_default_display_register()`）；整屏清黑 + 两条对角线（`COLOR_RED`、几何取自 `dev_display_get()`、线宽 `max(2, h/24)`、整数插值）；注册单槽首个生效 ⇒ `PROTO=ALL` 开发构建画面不变（山东先占槽），EIDE Debug / Myanmar 目标生效（后者无显示驱动 ⇒ 不注册）。**构建实测**（`PROTO=ALL DISP=1_263`，全量 `-B`）：`FACTORY=STD` = text **176576** / rodata **203672** / data 1656 / ccmram 39652 / bss 124860 / heap_stack 2564（**SRAM 余 1992B**）；`FACTORY=MM` = text **175184** / rodata **203640** / data 1656 / ccmram 39652 / bss 124596 / heap_stack 2564（**SRAM 余 2256B**）；两口径**零新增告警**（仍 3 条既有 HAL）；增量 = **text +240 / rodata +24**（相对 2026-09-28 厂测轮基线，两口径一致）；同轮 `PROTO=CQ DISP=1_263` = text **163332** / rodata **203008** / ccmram 37396 / SRAM 余 **6272B**，零新增告警。**无显示等价口径**（`CONFIG=EIDEMyanmarNoDisp`，`--emit-make` 重生成）**链接通过**：text **137632** / rodata **197000** / data 1584 / ccmram **16084** / bss 115796 / heap 2564 ⇒ **SRAM 余 11128B、CCM 余 49452B**；`nm` 模组驱动符号 0、`_mm_default_red_cross`/`_mm_cross_px` 在（编译进对象；无显示时不注册）。**脚本**：`mm_frame_sim.py` 142 → **159/159**（§9.6 上电默认态翻转 + §9.6b + 新增 §11 红叉几何 6 条 + 源码断言 9 条：仅显示驱动时注册 / 回调判空 / 几何取自 `dev_display_get()` / 颜色 COLOR_RED / 先清黑 / 提交帧 / 无编译期开关 / 接管顺序含红灯点亮）；`check_display_guards.py` **16/16 + 99/99**（新增 2 条硬断言）；`check_guard_disasm.py` **原操作零丢失**；`check_eide_targets.py` **40/45**（5 条 = EIDE Debug 现行口径差异，非回归）。**文档**：本文件 §3 / §4.1 / §4.2 / §4.3 / §8(Q1) / §10.2 / §10.3 / §11 / §13.4 / **新增 §14** / 本表 + `.analysis/myanmar/README.md` + `doc/CLAUDE.md` + `doc/05_协议模块多协议兼容优化/01_architecture.md` + `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` + `doc/构建开关总表.md` 已同步。**未烧录、未提交、未改 git 配置、未改 `.eide/eide.yml`** |
| **2026-10-08（冲突修复落地轮，本轮）** | **① 22_1665 驱动恢复 2 条编译期守卫**（通道上限 / 本模组 ≤ 64KB；机器码零变化；全文 395 行）—— 与缅甸无关，但本模块 `check_eide_targets.py` 的 CCM 预算表随之核对（8×4 = 46080B；缅甸目标 + 8×4 = 62164B 装得下）。**② `check_eide_targets.py` 改写为「结构不变量」口径**：不再断言「Debug 必须 = 某集合」（协议/显示模组编不编 = 可按需调整的选择，yml 是 GUI 地盘、外部不改），改为 I1~I8 + 当前集合信息输出；**复跑 14/15（唯一失败 = Debug 的 I6：`PROTO_CHONGQING` ↔ CQ 模块错配，交用户在 GUI 二选一）**，`Debug` 206 / `Myanmar` 207 源文件与 `builder.params` 逐条一致。**③ 全仓加固（非本模块专属）**：LDI 上报 IP 单一真源（doc/07 §16）+ 七条通道按协议绑定启动（doc/05 变更记录）——**本模块只绑 RS232，通道照常启动、行为零变化**。**④ 文档**：doc/CLAUDE.md、doc/01、doc/05、doc/06-04、doc/07、doc/构建开关总表、本文件与 `.analysis/myanmar/README.md` 同步。**⑤ 等价口径实构（2026-10-08）**：Myanmar（207 源文件、无显示）= `.text 132136 / .rodata 191968 / .data 1584 / .ccmram 16084 / .bss 115704 / heap 2560` ⇒ SRAM 余 **11224B**、CCM 余 49452B，链接通过、零新增告警（仍 3 条既有 HAL）。**未烧录、未提交、未改 git 配置、未改 `.eide/eide.yml`**；完整记录 `.analysis/audit_20261008/landing_20261008.md` |
| **2026-09-29（第二轮：RTT 底层恢复 + build 清理，本轮）** | **① RTT 底层恢复**（用户裁决「只清理 API 调用、不清理底层」）：`Platform/Src/pl_rtt.c`（逐字节 = `git HEAD`，`hw_pl_initcall` 注册）+ Makefile `SRC_RTT` 三源文件 （`SEGGER_RTT.c` / `SEGGER_RTT_printf.c` / `SEGGER_RTT_Syscalls_GCC.c`）+ `-I Middlewares/Third_Party/SEGGER_RTT` 加回；**自研诊断设施（`app_diag.h` / `[diag]` / `[mm]` / `[disp]`）保持删除**，**业务代码零 RTT API 调用**（grep 0 命中）；本模块 `MM_RTT_DIAG` 代码块仍为删除态。**② build/ 清理 + 防再污染**：删 `build/` 下 5 个非 Debug/Release 目录（合计 43MB，含 `EIDEMyanmarNoDisp`）；Makefile 新增 `BUILD_DIR ?=`，`check_eide_targets.py --emit-make` 生成物默认把产物写到 `.analysis/myanmar/build_nodisp/$(CONFIG)`（不再污染 `build/`）。**③ 等价口径复测（现行）**：`.text 131928 / .rodata 191968 / .data 1584 / .ccmram 16084 / .bss 115704 / heap_stack 2560` ⇒ SRAM 余 **11224B**、CCM 余 49452B，207 源文件、零新增告警。**④ 脚本**：`mm_frame_sim.py` **157/157**、`check_display_guards.py` **16/16 + 95/95**、`check_guard_disasm.py` 原操作零丢失、`check_eide_targets.py` **40/45**（口径差异，非回归）。**⑤ 本模块段尺寸（现行）**：`FACTORY=MM` = text **166216** / rodata **195664** / bss 124504（SRAM 余 2352B）。完整记录：`.analysis/cleanup_20260929/round2_report.md`、`doc/17` §5。**未烧录、未提交、未改 git 配置、未改 `.eide/eide.yml`** |

| **2026-10-13（全仓编译期断言清理，第二轮）** | 本模块 2 条 `_Static_assert`（`MM_PAYLOAD_MAX ≤ RS232 RB` / `MM_FRAME_LEN == 20U`）删除 → 契约注释（§2 定长帧事实）；`mm_frame_sim.py` **157 → 158/158**（原断言存在性检查改「契约注释存在 + 宿主复算」）。**行为零变化**：改前/改后全量重编段级逐字节一致 + strip-debug elf md5 相同（`.analysis/comment_pass_20261013/report.md` §⑧）。文档同步：本节 + §7.1 表 + 顶部横幅 + `.analysis/myanmar/README.md`。**未烧录、未提交、未改 git 配置、未改 `.eide/eide.yml`** |
| **2026-10-13（EIDE target 重组：`Myanmar` target 删除）** | 用户裁决 EIDE 侧重组成 3 个 target：**新增 `Base`**（基线 / 重置模板）、**`Debug` 清成同一基线**（日常主用；当日全量 Rebuild 构建成功、190 个 .c）、**`Myanmar` target 整块删除**（175 行）；`Release` 仍损坏勿用（**同日稍晚亦按用户裁决整块删除**）。**本模块源码零改动**（协议 / 干接点 / 厂测文件均在仓库），**缅甸固件改为 GUI 点选组出**：取消排除 `ProtocolParser_Myanmar` + 取消排除 `app_factory_test_mm.c`（同时排除标准版）+ 不编任何显示驱动 + 不定义 `PROTO_CHONGQING`（配方见 `doc/构建开关总表.md` §7.2 第 5 条）。文档同步：本文件顶部「现状」条 + §9 标题/§9.1/§9.2/§9.3 + §7.1/§7.2 + §8 Q6/Q8/Q9 注记 + §10/§13.3/§14.3/§14.5 + `doc/CLAUDE.md` 构建小节与缅甸小节 + `doc/构建开关总表.md` §1/§7 + `doc/06-04` + `doc/19` + `doc/05-01`。**未烧录、未提交、未改 `.eide/eide.yml`**（该 yml 改动由用户在 EIDE GUI 完成） |
| **2026-10-13（裁决执行轮：全项目编译期断言清零 + 观察 API 清理）** | ① 本模块**无编译期断言**（第二轮已清）；② **`s_mm_lamp_state` 影子状态删除**（零业务消费者；`mm_apply_lamp`/`mm_apply_init` 各减一处写入）。**行为零变化**：灯驱动路径 `_mm_drive()` 与映射表一字未改；宿主推演 `mm_frame_sim.py` **158/158**、`check_display_guards.py` **16/16 + 101/101** 全绿；全量重编 `.text −224 / .bss −24`（`PROTO=ALL`，逐对象期望清单 7/7 归因）。文档同步：本表 + §3/§4.1/§4.2/§4.3/§14.2 的「影子状态」表述 + `doc/CLAUDE.md` + `doc/06-04` §8.4.7 + `doc/05-01`。详见 `.analysis/comment_pass_20261013/report.md` 裁决执行轮。 |

---

## 13. TEST 键切换 0x0A / 0x0B 箭头落点（出厂测试，2026-09-28 新增）

### 13.1 需求与默认态

> **用户需求要点（2026-09-28）**：在使用缅甸协议解析的构建里，**TEST 按键（PD8）每按一次**，
> 切换 `0x0A` / `0x0B` 两条命令的**目标灯（干接点落点）**，切换可来回：
> · **默认态**：`0x0A` → 左下绿箭 = **PE11**（`MM_LAMP_LEFT`）；`0x0B` → 右下绿箭 = **PE10**（`MM_LAMP_RIGHT`）；
> · **按下一次**：`0x0A` → 右下 = PE10；`0x0B` → 左下 = PE11；再按一次切回默认；
> · **红灯 `0x90` / PE12 不受影响**（只交换 0x0A 与 0x0B 的落点）；
> · **切换状态不做掉电保存**（每次开机回默认态）。

| 状态 | `0x0A`（DATA0=1 开） | `0x0B`（DATA0=1 开） | `0x90` |
|---|---|---|---|
| **默认态**（上电初值） | 左下箭头 **S2/PE11** | 右下箭头 **S3/PE10** | 红灯 S1/PE12（不受影响） |
| **交换态**（TEST 按下一次） | 右下箭头 **S3/PE10** | 左下箭头 **S2/PE11** | 红灯 S1/PE12（不受影响） |

### 13.2 实现：单一生效点 + 不变量

**公开 API**（`Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h`，实现在 `app_mm_proto_cmd.c`）：

| API | 语义 |
|---|---|
| `bool mm_arrow_map_toggle(void)` | 翻转开关并返回新状态（true = 交换态）；`MM_RTT_DIAG` 开启时打一行 RTT |
| `bool mm_arrow_map_is_swapped(void)` | 查询当前是否交换态 |

**生效单点** = `mm_execute_cmd()` 里的 `_mm_effective_lamp()`（命令 → 灯位的**最后一步**，
只有 `MM_LAMP_LEFT ⇄ MM_LAMP_RIGHT` 两支参与，红灯原样返回）。各层受影响情况：

| 层 | 位置 | 是否受开关影响 |
|---|---|---|
| probe 判据 / 抗误吞正签名 | `app_mm_proto.c` `mm_probe_frame()` | **否**（只判字节，与灯位无关） |
| 帧解析（纯函数） | `mm_parse_frame()` | **否**（只依赖 raw 字节；宿主脚本断言源码内无开关引用） |
| 队列 / 帧处理任务 | `mm_proto_handle_task()` | **否** |
| **命令 → 灯位** | `mm_execute_cmd()` → `_mm_effective_lamp()` | **唯一生效点** |
| 灯位 → 引脚 / 极性 | `s_mm_lamp_map` / `_mm_drive()` | **否**（映射表与极性表达式一字未改） |
| `[mm]` 执行日志 | `mm_apply_lamp()` | 按**最终灯位**取映射表 ⇒ **如实反映实际驱动的灯与引脚**（交换态下 0x0A 的日志就是 `S3 (右下箭头灯) … PE10`） |

**开机复位（不持久化）**：`mm_apply_init()` 第一句 `s_mm_arrow_swapped = false;`
（该函数在 `mm_proto_init()` 内、建任务前调用）——**无任何 W25Qxx / Sector1 读写**。

**RTT 日志样例**（`MM_RTT_DIAG` 门控，默认跟随 `APP_DIAG_BANNER`；引脚与标签**由映射表推导**，
改脚位无需改文案）：

```
[mm] TEST: arrow map swapped -> 0x0A=PE10(右下箭头灯) / 0x0B=PE11(左下箭头灯)
[mm] TEST: arrow map restored -> 0x0A=PE11(左下箭头灯) / 0x0B=PE10(右下箭头灯)
```

### 13.3 消费者 = 缅甸版出厂测试文件（与标准版**二选一编译**）

新增 `Application/Src/app_factory_test_mm.c`，与标准版 `Application/Src/app_factory_test.c`
**提供同一套对外符号** ⇒ 两文件同时编入必然链接报 `multiple definition`（**天然互斥守卫**，
无需额外宏）：

| 对外符号 | 标准版 | 缅甸版（本文件） |
|---|---|---|
| `app_factory_mode_interrupt()` | 置中止标志，monitor 回 IDLE | **空实现**（无检测序列可中止；业务数据与切换开关互不干扰） |
| `app_factory_test_active()` | 进入检测序列后 true（CQ 心跳暂停计数） | **恒 false**（不占用显示资源，心跳照常） |
| `g_factory_test` | 监控任务句柄 | 同（同一套符号） |
| `sw_app_initcall` 的自注册函数 | `_factory_test_init`（256 words，静态 CCMRAM） | 同名静态函数（`mm_factory_monitor`，256 words，静态 CCMRAM） |
| **TEST 键行为** | 11 步序列（固件信息 / 七色满屏 / 通信灯 / 老化循环 / 斜扫 / 重启） | **每按一次 → `mm_arrow_map_toggle()`**，不进入任何序列、不渲染 |

- 监视任务名 `mm_factory_test`（≤ `configMAX_TASK_NAME_LEN`=16），优先级 `osPriorityHigh`（同标准版）；
- 本文件**不引用任何显示 API** ⇒ 在「无显示驱动」构建（〔历史〕`targets.Myanmar` 口径；现 = 不编任何显示驱动）下同样安全；
- TEST 键消费方式不变：`dev_key_wait_press(DEV_KEY_TST, …)`（EXTI 下降沿释放二值信号量，
  获取即**一次性消费**）。

**互斥验证（实测，2026-09-28）**：

```
arm-none-eabi-ld -r build/Debug/Application/Src/app_factory_test.o \
                    build/Debug/Application/Src/app_factory_test_mm.o -o /tmp/both.o
→ exit=1，三个 multiple definition：
  g_factory_test / app_factory_mode_interrupt / app_factory_test_active
```

**选择方式（EIDE 为唯一入口；下表 Makefile 行为历史）**：

| 入口 | 标准版 | 缅甸版 |
|---|---|---|
| 〔历史〕Makefile（**已移除**） | `make`（默认 `FACTORY=STD`，保持改动前行为） | `make FACTORY=MM`（非法值 `$(error)`；开关值进 `.build_stamp` ⇒ 切口径全量重编 + 重链接） |
| EIDE | 把 `<virtual_root>/Application/app_factory_test.c` 移出 `excludeList`（并排除 `app_factory_test_mm.c`） | **〔2026-10-13 现行〕Base/Debug 基线 = 编标准版、排缅甸版；要缅甸版把两处勾选互换**（`doc/构建开关总表.md` §7.2 第 5 条）。〔历史（2026-09-28~2026-10-13）〕Debug / `targets.Myanmar` 的 `excludeList` 均含标准版条目 ⇒ 恰好编入缅甸版 |

### 13.4 现场测试矩阵（TEST 键）

| # | 操作 | 预期（**表中 RTT 行为列已失效**——RTT 设施 2026-09-29 删除；灯板/万用表列为准） |
|---|---|---|
| T0 | **复位上电**（不发任何命令） | `[mm] … boot default = S1 red X ON, arrows OFF …` + `[mm] S1 (红灯) <- ON(开灯) => PE12 HIGH`；**PE12 = 3.3V（红叉亮）**、PE11/PE10 ≈ 0V —— **上电默认红叉（2026-09-29）** |
| T1 | 复位上电（默认态），发 `FF E0 0A 01 …` | `[mm] S2 (左下箭头灯) <- ON … => PE11 HIGH`；PE11 = 3.3V（亮） |
| T2 | 发 `FF E0 0B 01 …` | `[mm] S3 (右下箭头灯) <- ON … => PE10 HIGH`；PE10 = 3.3V（亮） |
| T3 | **按 TEST 一次** | `[mm] TEST: arrow map swapped -> 0x0A=PE10(右下箭头灯) / 0x0B=PE11(左下箭头灯)` |
| T4 | 再发 `FF E0 0A 01 …` | `[mm] S3 (右下箭头灯) <- ON … => PE10 HIGH`（**0x0A 落到 PE10**，PE11 不再被本帧驱动） |
| T5 | 再发 `FF E0 0B 01 …` | `[mm] S2 (左下箭头灯) <- ON … => PE11 HIGH`（**0x0B 落到 PE11**） |
| T6 | 交换态下发 `FF E0 90 01 …` | `[mm] S1 (红灯) <- ON … => PE12 HIGH`（**红灯不受影响**） |
| T7 | **再按 TEST 一次** | `[mm] TEST: arrow map restored -> …`，T1/T2 复现（可来回切） |
| T8 | 断电重启后发 `FF E0 0A 01 …` | 回默认态：落到 PE11（**不持久化**）；且上电仍是红叉亮（T0） |

---

## 14. 上电默认「红叉」与开机默认画面（2026-09-29 用户需求）

### 14.1 需求原话与两种解释

> **用户需求（2026-09-29 复述，与 2026-09-28 同）**：「……按下时切换 0x0A、0x0B 的解析逻辑……**顺便修改成
> 开机默认显示红叉**。有问题随时提出来。」

「红叉」在本项目有两种可能解释，**本条需求按下面两条同时落地**（互不冲突、互不干扰）：

| # | 解释 | 依据 | 落点 |
|---|---|---|---|
| **A（主）** | **车道灯板的红叉**（= 红灯 S1/PE12）**上电点亮**（车道关闭态） | 用户历史用词即指灯：2026-09-28「**车道灯红叉**还是被点亮了，测量 65 脚控制的端子…」；且用户明确「**缅甸费显不需要 HUB75 输出**，只需要控制车道灯灯板」 | `mm_apply_init()` 末尾点亮红灯（§14.2） |
| **B（备）** | **屏上画一个红色叉（×）**（若该构建带显示驱动） | 「显示红叉」字面 + 本工程「开机默认画面」= `app_default_display_register()` 注册制默认界面 | `app_mm_proto.c` 注册 `_mm_default_red_cross()`（§14.3） |

- 两条都实现的原因：A 是灯具侧（用户实际设备），B 是显示侧（若调试构建上挂了屏，用户看到的就是
  「上电一个红叉」）；B 只在**带显示驱动**的构建生效，对无屏量产件零影响（§14.3 有作用域说明）。
- **若用户只要其中一条**：A = 删 `mm_apply_init()` 里的一行 `mm_apply_lamp(MM_LAMP_RED, true);`；
  B = 删 `mm_proto_init()` 里的一行 `_mm_default_display_init();`（其余可留，`__gc-sections` 会丢弃回调）。

### 14.2 车道灯板侧：上电默认红叉（改 `mm_apply_init()`）

- **改动**：三路接管循环（先写灭电平 → 按线摘 EXTI → 切推挽输出）之后，
  追加一句 `mm_apply_lamp(MM_LAMP_RED, true);` —— 红灯 S1/PE12 输出**高**（高有效 = 点亮），
  两个绿箭头保持熄灭。
- **不变量**：① 走**唯一动作点** `mm_apply_lamp()`（不新增电平硬编码；原影子状态与 `[mm]`
  诊断日志已分别于 2026-10-13 / 2026-09-29 清理）；② **极性表达 `on != active_low` 与映射表一字未改**；
  ③ 接管顺序仍无毛刺（点亮发生在引脚已切成输出之后 = 「稳定的低 → 稳定的高」）；
  ④ 协议帧到达后照常按帧驱动（`0x90 00` 仍能关掉红灯）。
- **诊断**：接管摘要行改为
  `[mm] dry contacts taken over: S1=PE12 / S2=PE11 / S3=PE10 (push-pull, active-high; boot default = S1 red X ON, arrows OFF; EXTI released per-line)`，
  随后由 `mm_apply_lamp()` 打一行 `[mm] S1 (红灯) <- ON(开灯) => PE12 HIGH (高有效:高=点亮)`。
- **副作用变化**（见 §4.3）：上电默认态由「三路 SW 全读闭合」变为「SW1 读未闭合、SW2/SW3 读闭合」；
  RLS 干接点轮询的结论**不变**（SW3 = 车道关闭闭合，仍渲染「车道关闭」）。

### 14.3 屏侧：开机默认画面 = 红色叉（×）（改 `app_mm_proto.c`）

- **注册**：`mm_proto_init()` 内调 `_mm_default_display_init()` ——
  `if (dev_display_get() != nullptr) app_default_display_register(_mm_default_red_cross);`
  **仅当本构建带显示驱动时注册**；无显示驱动构建（〔历史〕`targets.Myanmar`；现 = 不编任何显示驱动）不注册、不打印、零行为
  （调用侧仍走 `dev_display_guard("default-display")` 的既有安全跳过路径，见 §10）。
- **画法**（不依赖字库，避免字库无 '×' 字形时画空）：整屏清黑 → 两条对角线（整数线性插值逐行落笔，
  线宽 `max(2, h/24)`：192×96 → 4px、32×32 → 2px）→ `dev_display_commit_frame()`；
  颜色固定 `COLOR_RED`；几何全部取自 `dev_display_get()` 的 `screen_rows`（宽）/`screen_cols`（高）
  —— **无硬编码分辨率**（宿主脚本断言函数体内不出现 16/32/64/96/128/160/192/224 等数值）。
- **作用域（单槽首个注册生效）**：本模块在 Makefile 收录序上位于山东（`app_sd_proto_default.c`）与
  贵州治超（`app_gz_ol_proto_default.c`）之后 ⇒ **`PROTO=ALL` 开发构建仍由山东先占槽、画面不变**；
  只有「无其它默认画面注册者」的构建（**〔2026-10-13 现态〕点选补上任一显示驱动的构建**
  〔历史：2026-09-29 当时 = `EIDE Debug`（山东/贵州治超均在排除表内）、`EIDE targets.Myanmar`
  （同上，且无显示驱动 ⇒ 不注册）〕）由本回调生效。
- **无显示纪律**：回调内 `if (d == nullptr) return;` 纵深防御；不新增任何编译期开关；
  新调用点已进 `.analysis/myanmar/check_display_guards.py` 的硬断言表（16/16）与调用点扫描（当时 99/99；**2026-09-29 瘦身轮后 = 95/95**，差值 = 随 RTT 诊断删除的 `dev_display_get()` 调用点）。
- **开机时序提醒**：`app_splash_display()`（FW/MD 版本，5s）仍在**默认画面之前**；
  「开机默认画面」= splash 之后的常驻画面（`app_default_display()`），与工程既有语义一致。

### 14.4 构建与脚本实测（本轮，2026-09-29）

| 口径 | `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | `_user_heap_stack` | SRAM 合计（余量） |
|---|---|---|---|---|---|---|---|
| `PROTO=ALL DISP=1_263 FACTORY=STD`（默认） | **176576** | **203672** | 1656 | 39652 | 124860 | 2564 | 129080B（余 **1992B**） |
| `PROTO=ALL DISP=1_263 FACTORY=MM` | **175184** | **203640** | 1656 | 39652 | 124596 | 2564 | 128816B（余 **2256B**） |
| `PROTO=CQ DISP=1_263` | **163332** | **203008** | 856 | 37396 | 121384 | 2560 | 124800B（余 **6272B**） |
| 无显示等价口径（`CONFIG=EIDEMyanmarNoDisp`） | **137632** | **197000** | 1584 | **16084** | 115796 | 2564 | 119944B（余 **11128B**）、CCM 余 49452B |

- 增量（相对 2026-09-28 基线）：`FACTORY=STD` / `MM` 两口径一致 = **text +240 / rodata +24**，
  `data / ccmram / bss / heap_stack ±0`（**SRAM 余量不变**）；无显示口径 = text +304 / rodata +32。
- **三口径链接通过、零新增告警**（仍 3 条既有 HAL `stm32f4xx_hal_flash_ex.c` `-Wunused-parameter`）。
- 脚本：`mm_frame_sim.py` **158/158**（**2026-10-13 编译期断言清理后净 +1；此前瘦身轮由 159 收敛为 157**，见本文件顶部横幅）、`check_display_guards.py` **16/16 + 95/95**（瘦身后）、
  `check_guard_disasm.py` **原操作零丢失**、`check_eide_targets.py` **40/45**（〔当时〕5 条 = Debug 现行口径差异，非回归；**该脚本已随 `.analysis` 移除、I6 遗留已随 2026-10-13 target 重组清零**）。
  证据留档：无显示口径 ELF `nm` 无模组驱动符号、红叉符号在对象内；`objdump -dr` 显示
  `mm_apply_init` 的重定位序 = `_mm_drive` → `pl_gpio_release_exti` → `pl_gpio_config_output` → `mm_apply_lamp`
  （接管完成后才点亮红灯，顺序与设计一致）。

### 14.5 现场验证与待拍板

| # | 操作 | 预期 |
|---|---|---|
| 1 | 复位上电（不发任何命令） | **车道灯板红灯（红叉）亮**（PE12 = 3.3V）；RTT 两行（接管摘要 + 红灯 ON） |
| 2 | 发 `FF E0 90 00 …` | 红叉灭（PE12 ≈ 0V）—— 证明协议仍可按帧关灯 |
| 3 | 屏侧（若调试构建挂了屏，如 EIDE Debug 的 1_969 = 192×96） | 上电默认画面 = **整屏黑底 + 红色叉（×）**（替换原「欢迎行驶 / 高速公路」欢迎语） |
| 4 | 无显示构建（〔历史〕`targets.Myanmar`；现 = 不编任何显示驱动） | 无任何显示相关行为、无 `[disp]` 告警新增（红叉回调不注册） |

**待用户拍板**：① 「红叉」是否**只需要车道灯侧**（若要，B 一行删除）；② 屏侧红叉的**大小/线宽/是否留边**
（现行 = 满屏对角线、线宽 `max(2, h/24)`）；③ 若希望「上电只显示红叉、连 splash 也不要」，
需要另说（splash 属开机自检信息，本轮未动）。

