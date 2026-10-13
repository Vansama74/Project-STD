# 19 新疆费额显示器协议（ZJ_2024）

> 〔2026-10-13〕本仓库 Makefile 已移除，构建仅 EIDE；本文中以 make 为入口的命令一律作废（历史记录），口径迁移见 doc/构建开关总表.md §7。

> **⚠ 2026-10-13 用户裁决：`.analysis/` 目录已从仓库移除且禁止再建。** 本文中所有 `.analysis/**` 引用
> （宿主推演 / 校验脚本 / 调研与实施报告 / EIDE 备份 / 构建证据）**一律作废、仅作历史线索保留**；
> 对应文件需要时从 git 历史取回：`git show 069be96:<.analysis 下路径>`。下文脚本结果列为历史记录。

- **协议文档（主）**：`费额显示器通讯协议(2).pdf`（**PDF 全文「费额显示器通讯协议 2020 年」，22 页**，
  WPS 生成；修订说明：2020-07 增心跳与「彩色行显示协议」、2021-08 增入口发卡/出口收卡语音）
  —— 全文留档 `.analysis/xinjiang/doc_v2/doc_v2.txt`（`pdftotext -layout`）+ 含 hexdump 的页 20/21/22 渲染图
  （`.analysis/xinjiang/doc_v2/img/`）
- **协议文档（旧摘录版）**：`/home/yystation/Desktop/费额显示器通讯协议.md`（标题「2024年」、144 行、9 命令；
  缺 §2 设计 / §3.1.1~3.1.12 多条命令 / §3.2 应答 / §4.1~4.4 语音）
- **接入日期**：2026-10-12（**同日第二轮按 PDF 全文复核 + 现场「无反应」根因修复**）
- **模块前缀**：`xj_` / `XJ_`；目录 `Application/{Src,Inc}/ProtocolParser_XinJiang/`
- **前置调研**：`.analysis/xinjiang/pre_integration_questions.md`（协议摘要、probe 判据草案、帧头冲突分析、
  文档矛盾清单 M1~M10）；**实施报告**：`.analysis/xinjiang/implementation_report.md`；
  **第二轮报告（PDF 差异表 + 根因 + 修复 + 验证）**：`.analysis/xinjiang/round2_pdf_fulltext_and_rb.md`
- **宿主推演**：`python3 .analysis/xinjiang/xj_frame_sim.py`（**331/331 通过**；§10 = PDF 示例帧逐帧用例；
  2026-10-13 编译期断言清理后原 7 条断言存在性检查改「契约注释 + 宿主复算」两类，净 +2）

---

## §1 协议摘要（逐节提取，实施依据）

### 1.1 帧格式

| 项 | 结论 | 依据 |
|---|---|---|
| 帧头 | `0xFF 0xDC`（所有命令统一，[0]/[1]） | 文档 §2.1~§2.10 各表「Message start」 |
| 长度字段 | **1 字节 = 整帧总长**（含 2 字节帧头与末尾 BCC） | §2.2/2.3/2.4/2.5/2.7/2.10 均 `Length=5` 且各帧恰 5 字节；§2.1 `10+N*16` = 9 头 + N×16 内容 + 1 BCC |
| 命令字 | 1 字节 ASCII（[3]） | 同上 |
| BCC | **除 BCC 字节外全部字节逐个 XOR**，放末字节 | §2.11「BCC 是每个消息字节的 XOR」+ 示例 `FF^DC^05^44=0x62`（实测成立） |
| 帧长范围 | **5 ~ 106**（无载荷 5；'T' 6 行 = 10+6×16 = 106；'D' 全屏 6 行 = 7+6×16 = 103）。**PDF §5.2 现场采样帧 = 103B（96B 数据 = 6 行 ×16B）、BCC 逐字节自洽**，故 2026-10-12 第二轮由 74 放宽到 106 | §2.1/§2.2 + §5.2 采样（页 20/21） |
| 转义 | 无转义、无填充；BCC 可为 0x00（不能以 0x00 定界） | §2.11 |
| 应答 | **入向命令本身不需上位机应答，但设备须对 `'D'/'C'/'Z'/'P'` 回 5 字节应答**（§3.2.1~3.2.4），`'B'` 回 §3.2.5；另有 §3.2.6 启动上报 `'R'`、§3.2.7 Nack `'?'` | PDF §3.2 全节（摘录版缺失）；`'R'`/`'?'` 不实现，见 §8-Q14 |

### 1.2 命令与字段布局（0 基偏移）

| 命令 | 整帧长 | 字段 | 语义 |
|---|---|---|---|
| `'T'` 0x54 文本显示 | 10+16N（N=1~6 → 26…106；文档表 4 行 = 74、§5.2 采样形态 6 行 = 106） | `[4]`亮度 0~255、`[5]`显示行（恒 0）、`[6]`R、`[7]`G、`[8]`B、`[9..len-2]`内容（16N 字节） | 彩色文本；「一行 16 个字节，每 16 字节一行」 |
| `'O'` 0x4F 心跳 | 5 | — | 上位机 ≥5s 发一次；超时 → 红字「系统维护中」（「可由出厂软件设定」） |
| `'E'` 0x45 绿灯 | 5 | — | 绿灯亮、红灯关 |
| `'F'` 0x46 红灯 | 5 | — | 红灯亮、绿灯关 |
| `'J'` 0x4A 报警开 | 5 | — | 报警器开 |
| `'K'` 0x4B 报警关 | 5 | — | 报警器关（正文小节号 2.5 → 2.7，**2.6 缺失**） |
| `'V'` 0x56 TTS 播报 | 71 或 72（见 M5） | `[4]`语言（0x30 男声/0x31 女声）、`[5]`音量 0x30~0x37、`[6..len-2]`64/65 字节文本（GBK） | 「几型车，您应多少元」；注明「维语报与汉语相同含义语音」 |
| `'S'` 0x53 预制语音 | 8 | `[4]`语言（0x30 汉语/0x31 维语/0x32 双语）、`[5]`音量、`[6]`编号 | 编号 0x31 您好 / 0x32 祝您一路平安 / 0x33 ETC车辆，请通行 / 0x34 ETC写卡失败，请领取通行卡 / 0x35 ETC交易失败，请转人工处理 / 0x36（无说明） |
| `'C'` 0x43 重置屏幕 | 5 | — | 清屏 |
| `'D'` 0x44 | **PDF 全文已澄清** | 摘录版不在命令表；PDF §3.1.1 行显示 Len=23（`[5]`行号 1~4）/ §3.1.2 全屏 Len=71（`[5]`=0）/ **§5.2 采样全屏 Len=103（96B 数据 = 6 行，`[5]`=0）**；§3.2.1 另有 5 字节**出向应答** | **已实现**（行显示 23 + 全屏 7+16N（N=2~6，含 71/103）；出向 5 字节形态在 parse 拒绝） |

> **2026-10 依据 PDF 全文（2020 版 22 页）修订**：此前本 README 基于桌面 `费额显示器通讯协议.md`
> 摘录版（缺 §2 设计 / §3.1.1 行显示 / §3.1.2 全屏 / §3.1.6~3.1.12 / §3.2 应答 / §4.1~4.4）。
> PDF 全文解除了 M1/M2/M3 三项缺口：`'D'` 两形态、`'G'`/`'H'`、`'Z'`、`'P'`、`'S'` 四帧长、32 条 ETC 文案。

**通信参数**：标准 RS232，异步全双工，9600~115200 bps / 8N1；本设备波特率由 DIP1 全局选择
（`app_uart_baud`：OFF=9600 / ON=115200），协议无改波特率命令。

**上电默认行为**：文档未规定上电画面与灯态；与「默认态」相关的只有心跳超时（§2.5 按 15s 实现）。

### 1.3 文档矛盾/缺口（M1~M10，逐条待厂商确认）

| # | 位置 | 问题 |
|---|---|---|
| M1 | 修订说明 vs 正文 | 修订说明称 2020-07 增「彩色行显示协议」，**正文无对应章节** → 可能缺命令 |
| M2 | §2.5 → §2.7 | **2.6 缺失**（可能缺一整条命令） |
| M3 | §2.11 示例 | 示例命令字 `'D'`(0x44) 不在命令表（表中 0x43='C'） |
| M4 | §2.1 表 | 字段 10 标长度 74 但描述「64 字节 N 行」；BCC 标在字段 75 与 `Length=10+N*16` 不自洽 |
| M5 | §2.8 表 | 字段标 `7-71`（65 字节）却标「64 Lines data」；`Length=72` 与 64 字节内容推得的 71 差 1 → probe 两种长度都容忍 |
| M6 | §2.9 表 | 「语音编号」标 2 字节，但 `Length=8` 仅 1 字节自洽；取值范围写 `0x30~0x255`（超 1 字节，非法） |
| M7 | §2.9 正文 vs 备注 | `[4]` 语言选择与备注的语音编号是**两套编号**；`0x36` 悬空 |
| M8 | §2.1 字段 6 | 「显示行 = 0」只有 0 一个取值，无说明 → 忽略该字段（§2.3） |
| M9 | §2.1 亮度 | 0~255 未定义 0 的特殊语义 → 按本项目惯例定为「恢复光敏自动」（§2.4） |
| M10 | 文档抬头 | 全文未出现「新疆」字样（仅「维语」暗示地域）→ 已按用户确认的现行版本实施 |

---

## §2 已裁决事项与实现

### 2.1 调度框架掩码扩容 32 → 64（本轮同时落地）

- **动因**：`PROTO=ALL` 口径下 `app_proto_register()` 已被调用 32 次，`registered_mask`（原 `uint32_t`）
  满位 → 新增协议注册**返回 0 并静默退出**（2026-09-29 瘦身轮后无任何日志，现场现象 =「完全没有反应」）。
- **改动**：`PROTO_MAX_COUNT 32 → 64`；`proto_mask_t` 由 `uint32_t` → `uint64_t`；`registered_mask` 与
  `ch_proto_map[]` 随类型；`proto_index()` / `app_proto_register()` 改用新增的 `bit_ctz64()`
  （`Kernel/Inc/bit_utils.h` + `bit_utils.c`；`x==0` 返回 0，与既有 `bit_ctz` 语义一致）；所有位移改
  `((proto_mask_t)1U << i)`；`app_channel_dispatch()` 的 RB 去重表由 `PROTO_MAX_COUNT` 改按**缓冲池槽数**
  `RB_CNT_MAX`（3）开表 —— 64 指针表会把 1KB 通道任务栈帧从 140B 抬到 268B（实测），按槽数开表后为 64B。
- **零行为变化**：只加宽不改逻辑；`mask=0` 仍是「无协议」；槽位仍按「最低空闲位」递增分配，既有 35 处
  `app_proto_register` 的 mask 值逐位不变（宿主推演 §10 + 构建实测见 §6）。
- **代价**（`PROTO=ALL DISP=1_263 FACTORY=STD`，全量重编）：`.text +592` / `.bss +588` /
  `._user_heap_stack +4`（`g_dispatch` +424 = 三个 64 项指针表 +384、`registered_mask` +4、
  `ch_proto_map[]` +36；35 个协议模块的 `s_*_mask` 静态量各 4→8B 合计 +140；余为对齐），
  `.rodata/.data/.ccmram ±0`，零新增告警。

### 2.2 通道绑定 = RS485 + RS232 双槽（两独立 mask，**自备槽体 + acquire**）

- 注册五步 × 2：`app_proto_acquire_buf(RB_SLOT_RS485/RS232)` → 每槽独立 `app_proto_register` +
  `app_proto_bind_channel`（同一 probe、同一静态队列）→ 处理任务；**禁止 bind `CH_ID_RS232_1`**（USART6 语音 TX）。
- **本模块自备两个槽体**（与青海同款，2026-10-12 第二轮起）：
  `RB_PROVIDE_WEAK(rb_provide_rs485, RB_SIZE_RS485)` + `RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232)`
  （`app_xj_proto.c` 文件顶）。同槽多提供者 weak 合并（容量同为 768B，链接器取其一）⇒ 多协议口径**零额外 SRAM**；
  单协议目标（如「IAP + LDI + 新疆」）也能自己拿到槽体、正常注册 + 绑定。
- **为什么必须自备（现场教训）**：原设计「只 acquire 不 provide」，前置条件是「同构建至少有一个协议为 RS485/RS232
  提供槽体」。用户把 EIDE Debug 目标裁成「IAP + LDI + 新疆」（排除全部其它串口协议）后该前提被打破 ——
  `app_proto_acquire_buf()` 双双返回 `nullptr` → 本模块 init **静默 return**（不注册/不绑定/不建队列任务）→
  `app_boot.c` 通道门控（`app_dispatch_channel_has_proto`）连 `app_rs485_start()`/`app_rs232_start()` 都跳过
  （**UART RX 都没挂上**）→ 上位机发帧零反应。详见 `.analysis/xinjiang/round2_pdf_fulltext_and_rb.md` §③。
- 波特率沿用 DIP1 全局选择（协议无改波特率命令）；现场按上位机配置置 DIP1。

### 2.3 文本显示 `'T'`（严格 16 字节/行）

- `N = 内容长度/16`（内容 = `[9..len-2]`，共 16N 字节，N=1~4）；**`[5]` 显示行字段忽略**（文档只定义 0）。
- **先整屏清黑，再逐行渲染**；字号 `FONT_16`、字型 `FONT_ST`；每行左对齐 `x=0`、`y=行号×16`。
- 行内容**去尾部 0x00/0x20**（两者都不是 GBK 尾字节，去除安全）；去尾后为空则该行不画（行号不压缩）。
- **GBK 双字节不被 16B 边界劈开**：某行窗口末字节恰为 GBK 首字节（0x81~0xFE）且其尾字节在窗口外时，
  **该字符整字节留给下一行**（本行退 1 字节）——代价是总行数可能多 1（行宽永不超过 16 字节 = 协议 8 汉字口径），
  收益是字形不被劈开、行宽不超协议假设。行起点越屏（`y ≥ 屏高`）= 执行但不落屏。
- 整屏清黑 + 逐行渲染 + 提交整段持 `app_scroll_render_lock()`（与滚动/其它协议渲染串行）。
- 黑字（三通道全 <128）时**只清屏不画字**（§2.4）。

### 2.4 颜色与亮度

- **颜色**：`bit0 = R≥128`、`bit1 = G≥128`、`bit2 = B≥128`，组合值 0~7 与 `display_color_t` 枚举**恒等**
  （1 红 / 2 绿 / 3 黄 / 4 蓝 / 5 紫 / 6 青 / 7 白）；三通道全 <128 = 黑（不画）。阈值 128 为裁决值。
- **亮度**（每帧 `'T'` 携带）：`0 = 恢复光敏自动调光`（仅当本模块曾挂起时恢复）；`1~255 → 8 档`
  （`level = 1 + (v-1)×7/254`，`v=255 → 8` 最亮，`v=254 → 7`），非 0 时挂起光敏任务（`osThreadSuspend`）。
  与安徽 0x92 / 云南 '8' 的「0 = 自动」口径一致；无显示实例时整段跳过。

### 2.5 心跳与维护屏（`XJ_FAULT_SCREEN`）

- **超时 15s**（文档 5s 的 3 倍，容忍上位机节拍抖动）；**自模块 init 起算**（`osKernelGetTickCount()` 与
  「最近一次有效帧时刻」比较），**不新建任务** —— 帧处理任务的队列等待用 **500ms 超时**做节拍。
- 超时 → 整屏清黑 + **红字居中「系统维护中」**（FONT_16/FONT_ST）；**收到任意有效帧即解除**（若维护屏
  正显示，解除时**清屏一次**避免残字；解除动作先于本帧执行 —— `'T'`/`'C'` 自身首步会清屏，顺序反了会擦掉内容）。
- 编译开关 `XJ_FAULT_SCREEN`（`#ifndef` 守卫，默认 **1**）：`=0` 时**无心跳计数、无维护屏渲染**，
  处理任务改 `osWaitForever`（零周期唤醒）；维护屏两函数随 `--gc-sections` 整体丢弃（§6 实测）。
- **机制只存在于本模块**：模块不编入 = 完全没有该机制，不触碰任何全局/其它模块。

### 2.6 语音（`'V'` / `'S'`）

- `'V'`：文本（GBK，`[6..len-2]`）去尾部 0x00/0x20 后**直送 `dev_rs232_voice`**（USART6 旁路）；
  **忽略 `[4]` 男/女声**（硬件无音色接口）。
- `'S'`：**按整帧长判四种形态**（长度是唯一判别依据，四者字段前缀相同）
  - Len=12/17/18（PDF §4.1/4.2/4.3 报价 1/2/3）：载荷是 **ASCII 数字**，按 PDF 语序拼中文句
    （`X型车，请交费N元` / `X型车，车货总重N.NN吨，请交费N元` / `X型车，车货总重N.NN吨，超重N.NN吨`）后播报；
  - Len=8（§4.4 其他播报）：编号查表 → 固定中文文案（UTF-8 字面量运行转 GBK），未收录编号静默忽略。
    编号表按 **出口收卡机** 一栏实现：`0xA0`、`0xB0`~`0xB6`（8 条）。入口发卡机 `0xA1`~`0xAF`
    与出口同号异义，若现场为入口机需补表（§8）。
  - 旧摘录版的 0x31~0x35 文案**已移除**——PDF 全文的 §4.4 无此编号。
  **忽略 `[4]` 语言字段**。
- 音量（`'V'`/`'S'` 的 `[5]`，0x30~0x37）：**线性映射到板载档位 1~9**（`1 + (v-0x30)×8/7`，0x30→1、0x37→9），
  沿用贵州先例区间 `{1,3,5,7,9}` 的两端；**映射表须现场校准**（语音板音量非线性）。
- **硬件级缺口**：文档的维语播报、汉/维双语、男/女声本硬件均无（语音板只有自由文本 TTS + 音量两接口）→
  记 §8 待现场确认（若现场必须出维语，需升级语音板）。

### 2.7 实现范围 = 14 命令

`'T' 'O' 'E' 'F' 'G' 'H' 'J' 'K' 'V' 'S' 'C' 'D' 'Z' 'P'`。

**不进白名单**：`'B'` 下载位图（PDF §3.1.12，帧头 `FE DB` + **双字节**长度 1093，超出本模块
单字节长度字段模型，且依赖 128×64 bitmap 存储——本硬件 1-263 模组为单色文本屏，见 §8）。

---

## §3 probe 判据与同槽共存

### 3.1 判定链（PURE：只 `rb_peek`，无副作用、无阻塞）

| 步 | 判据 | 结果 |
|---|---|---|
| 1 | `avail == 0` | FAKE（禁止盲 WAIT 阻塞链式探测） |
| 2 | `[0] != 0xFF` | FAKE（首字节快拒） |
| 3 | `avail < 3` | WAIT（长度字段未到齐） |
| 4 | `[1] != 0xDC` | FAKE（与 RLS `FF FE` / MM `FF E0` 分道） |
| 5 | `len = [2]`；`len < 5 || len > 106` | FAKE（不等齐先判：治超帧长 0xDC=220 一类立即排除，不占等待预算；契约注释（`XJ_FRAME_LEN_MAX < 220`）锁此前提） |
| 6 | `avail < 4` | WAIT（命令字未到齐） |
| 7 | `[3]` 不在白名单 `{'T','O','E','F','G','H','J','K','V','S','C','D','Z','P'}` | FAKE（交链式探测/1 字节重同步，不消费） |
| 8 | `avail < len` | WAIT（整帧未到齐） |
| 9 | `XOR([0..len-2]) != [len-1]` | FAKE（BCC 第三重隔离） |
| 10 | READY，写 `*total_len = len` | **仅 READY 写输出参数** |

> 步 3~5 由**一次 `rb_peek` 读 `[0..2]`**（帧头 2 + 长度字段）供判：只读 2 字节会让
> `len = head[XJ_LEN_OFFSET]` 取栈上垃圾值（未初始化读，2026-10-12 修复），故读长恒为
> `XJ_LEN_OFFSET + 1U`；`xj_frame_sim.py` 已加「probe 内 `buf[N]` 访问须被前置 peek 读长覆盖」的回归断言。

命令字白名单复用 `xj_cmd_from_byte()`（probe 与 parse 单一真源）。

### 3.2 同槽共存（RS485 / RS232 链式）

| 同槽协议 | 其帧特征 | 与 `FF DC` 的关系 | 隔离结果 |
|---|---|---|---|
| 四川治超 SC_OL | `FF + 长度(07~FF，排除 FE) + … + BCC + FF` | **唯一重叠**（0xDC 是其合法长度值） | 四重隔离：长度 ≤74（治超 0xDC=220 立即 FAKE）、命令字白名单（治超 0x80~0xB9 无 ASCII 字母）、BCC、链式（治超对本协议帧判 WAIT 继续试下一协议 → 本 probe READY 认领） |
| RLS | `FF FE …` | `[1]=0xFE` | 第 4 步 FAKE |
| 缅甸 MM | `FF E0 …`（定长 20） | `[1]=0xE0` | 第 4 步 FAKE |
| 四川 ETC / 安徽 / `{` 族 | `0A` / `5A` / `7B` | 首字节不同 | 第 2 步 FAKE |
| GZ_OL | `54 43 4C 59` | 首字节不同 | 第 2 步 FAKE |

⇒ **无需 EIDE 目录互斥**；**非 `{` 帧族 → 不定义 `g_brace_proto_guard`**（与安徽/GZ_OL 的「免互斥」同类）。
残余误判概率 = 治超帧恰以 `FF DC` 开头且第 4 字节恰为 9 白名单字母之一且 BCC 吻合
（`9/256 × 1/256 ≈ 1.4e-4`），且要求治超第 2 字节（长度）恰为 0xDC 且 ≤74 —— 与「长度 = 0xDC = 220」互斥，
实际为 0。

---

## §4 内存与队列

| 项 | 值 | 说明 |
|---|---|---|
| 帧队列 | `XJ_QUEUE_DEPTH = 3`，`XJ_MSG_SIZE = sizeof(frame_msg_t) + 106 = 114` | 两 mask 共用一队列；`XJ_PAYLOAD_MAX ≤ RB_SIZE_RS485/RS232` 契约注释 |
| CCMRAM（模块符号级跨度 **1668B**） | 任务帧缓冲 114 + 队列体 `3×114 = 342` + `StaticQueue_t` 80 + 任务栈 1024 + `StaticTask_t` 100 = **1660**（+对齐 8） | 放置理由同 CQ/GZ_OL/YN_OL/MM（SRAM 余量紧；CPU 独占访问无 DMA）。相对 payload=74 时的 1544B 链接增量 **+124**（队列 +96 / 任务帧缓冲 +32 / 对齐差 −4） |
| SRAM | `.bss` 常驻 **32B**（2×mask 16 + 队列句柄 8 + 心跳时刻 4 + 标志 2）+ **自备两槽体 1568B**（`data` 32 + `bss` 1536） | 多协议口径下 weak 合并、不占；**单协议目标必需**（§2.2） |
| Flash | 模块整体（probe/解析/执行/任务/应答/文案）相对 9 命令基线 `text +1664`（含本轮 RB getter +16）/ `rodata +240`（全协议口径，见 §6） | 见 §6 |
| 任务 | `PL_TASK_STATIC_STORAGE(xj_handle, 256)`（256 words = 1KB），`osPriorityNormal` | 只创建一次、永不退出 ⇒ 静态落 CCMRAM |
| 渲染互斥 | 所有「清屏 + 渲染 + 提交」整段持 `app_scroll_render_lock()` | 与滚动渲染/其它协议串行 |
| 无显示构建 | 全部显示入口 `dev_display_get()` 判空；灯控/语音不受影响 | 判据脚本 `.analysis/myanmar/check_display_guards.py` 100/100 覆盖 |

---

## §5 构建收录

### 5.1 〔历史〕Makefile（已完成；Makefile 已于 2026-10-13 从仓库移除，本小节仅作历史）

- `INC_DIRS` 增 `-I Application/Inc/ProtocolParser_XinJiang`
- `SRC_APPLICATION` **末尾**追加 3 个 .c（`app_xj_proto.c` / `app_xj_proto_parse.c` / `app_xj_proto_cmd.c`）
  —— 放在所有既有协议之后 ⇒ **新疆 probe 优先级最低**（同槽重叠时既有协议先认领，最安全）。

### 5.2 EIDE（**2026-10-12 已外部增收，待用户 `Reload Project`**）

> **2026-10-12 更新**：由本轮任务裁决直接外部编辑 `.eide/eide.yml`（最小外科式插行 + YAML 解析校验；
> 原文备份 `.analysis/xinjiang/eide_20261012/eide.yml.before_xj`，本轮只新增 10 行、未触碰会话前既有在飞改动）。
> 落地内容与原「GUI 执行清单」逐条对应：

| # | 操作 | 2026-10-12 落地结果 |
|---|---|---|
| 1 | `files` 节点新增源文件 | 已增 `Application/protocol/ProtocolParser_XinJiang` 虚拟文件夹（3 个 .c，**排在 `rls` 之后**）。注：EIDE 实际链接/probe 顺序由 `objlist` **路径字典序**决定（与 yml 收录序无关），本模块在 Debug 编入集中仍排全部协议之后 ⇒ **probe 优先级最低**（设计目标达成） |
| 2 | `targets.Debug.incList` 增包含路径 | 已增 `Application/Inc/ProtocolParser_XinJiang`（**不带 `-I`**，EIDE 条目格式；I7 不变量硬要求） |
| 3 | 目标选择 | **`targets.Debug` 编入**（不在 `excludeList`）+ `defineList` 追加 **`XJ_FAULT_SCREEN=0`**（共存构建关闭 15s 心跳维护屏）；**`targets.Myanmar` 明确排除**（`excludeList` 增该虚拟文件夹）且 `incList` 兜底同路径；**`targets.Release` 不动**（`incList` 仍是已删除的 `Drivers/BSP/*` 旧路径 = 非可用入口） |
| 4 | 收尾 | **待用户执行 `EIDE: Reload Project`**（否则 EIDE 用内存模型回写 yml 时可能丢失外部条目；Reload 后 EIDE 按自己格式回写 yml 属预期） |

- **等价口径实测**（`.analysis/xinjiang/eide_20261012/`：`Debug.before/after.mk` + `build_before/after.log` +
  `size_before/after.txt`；独立 `BUILD_DIR`，未触碰 `build/Debug/`）——Debug 编入 **206 → 209** 个 .c（+3）、
  `.text 133544 → 135160` / `.rodata 192232 → 192480` / `.data 1632`（±0）/ `.ccmram 17260 → 18804` /
  `.bss 116420 → 116452`（Δ = **+1616 / +248 / +1544 / +32**）、告警仍 **3 条既有 HAL**。
- **`XJ_FAULT_SCREEN=0` 生效判据**（三重）：链接后 `xj_fault_screen_show` 与「系统维护中」**均为 0**；
  同一 `.o` 内为 **1/1**（证明是链接期 `--gc-sections` 丢弃而非源码缺失）；正对照 `build/xj_b2/Project_STD.elf`
  （上一轮默认开关口径）= **1/1**〔该 ELF 属抽样中间产物，2026-10-12 已随 `build/xj_*` 清理；
  重跑 `make -j8 BUILD_DIR=build/xj_b2` 可重建，判据记录见 `.analysis/xinjiang/eide_20261012/assertions.txt`〕。该开关在本口径下的净效果 = **text −288 / rodata −16 / bss −8**（`Debug.after_faulton.mk`
  对照构建实测），与 §6 全协议口径记录**逐字节一致**。
- 结构不变量 `check_eide_targets.py` **改前 14/15 → 改后 14/15**（唯一失败 = 既有 Debug `PROTO_CHONGQING` ↔
  CQ 模块错配 I6，非本轮引入；新疆相关不变量全绿）；Myanmar 等价 Makefile 改前/改后**逐字节相同**（不含新疆 3 文件）。
- 方法学注记：脚本产出的是 **EIDE 目标「源文件集等价」** Makefile（用本仓 Makefile 的 Debug 旗标 `-Og -g`），
  与 EIDE 真实旗标（`optimization: level-3`）略有差异 ⇒ 绝对段尺寸仅供同口径对照，**本表 Δ 为同一旗标下的真 A/B**。

⚠ **2026-10-12 第二轮更正（原「前置检查」已作废）**：本模块**自备 RS485/RS232 槽体**（§2.2），
不再要求「目标内必须有其它协议提供 RB」。教训来源：用户把 `targets.Debug` 裁成「IAP + LDI + 新疆」
（16:25 的 yml 外部编辑，相对 15:41 备份**唯一差异 = 新增 `ProtocolParser_SiChuang_{Overload,MTC,ETC}` 三行排除**），
15:44 那轮「Debug 口径满足」的断言随之失效 —— 该目标实测（`build/Debug` 17:25）
`nm` **无 `rb_provide_rs485/rs232`**、`g_rb_provide` 表 = `791c0408 00000000 00000000`
⇒ 本模块不注册、串口通道不启动 ⇒ 上位机发帧零反应。修复与 A/B 数字见
`.analysis/xinjiang/round2_pdf_fulltext_and_rb.md` §③/§④。

---

## §6 实测段尺寸（`PROTO=ALL DISP=1_263 FACTORY=STD`，Makefile GCC Debug，全量重编）

> 〔2026-10-13〕**本节为 make 时代采样记录**（Makefile 已移除）；下列 `make … BUILD_DIR=build/xj_*`
> 命令一律作废，数字仅作历史基线与归因对照，**不可与 EIDE 构建的字节数对等比较**（编译参数不同）。

| 口径 | `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | `._user_heap_stack` | SRAM 合计 / 余 | 告警 |
|---|---|---|---|---|---|---|---|---|
| **B0** 工作区基线（未改代码） | 167944 | 195776 | 1656 | 39652 | 124768 | 2560 | 128984 / **2088B** | 3 条 HAL |
| **B1** = B0 + 掩码扩容 32→64 | 168536 | 195776 | 1656 | 39652 | 125356 | 2564 | 129576 / **1496B** | 3 条 HAL |
| **B2** = B1 + 新疆模块 + Makefile 收录 | 170392 | 196024 | 1656 | **41196** | 125388 | 2564 | 129608 / **1464B** | 3 条 HAL |
| B1−B0（掩码扩容归因） | +592 | ±0 | ±0 | ±0 | +588 | +4 | +592 | — |
| B2−B1（新疆模块归因） | +1856 | +248 | ±0 | **+1544** | +32 | ±0 | +32 | — |

- **B0/B1/B2 口径**：`make -j8 BUILD_DIR=build/xj_b{0,1,2}`（各自独立目录全量构建，避免 EIDE 残留 elf 干扰；
  `build/Debug/` 未触碰）。
  〔注（2026-10-12 工作区清理轮）：本节及 §6.1~§6.3 的全部 `BUILD_DIR=build/xj_*` 抽样目录属中间产物，
  已随工作区清理删除（`rm -rf build/xj_*`）；归档证据 = `.analysis/xinjiang/`（`size_*.txt` / `build_*.log` /
  `r2_evidence/`），按各行命令重跑即可重建目录。〕
- 掩码扩容归因：`g_dispatch` 488 → 912B（+424）、35 个协议模块 `s_*_mask` 4→8B（+140）、对齐填充（+24）；
  `.text` 增量 = 64 位 ctz/掩码运算 + 各 init 的 8 字节 mask 存取（`frame_dispatch_task +110`、
  `app_channel_dispatch +40`、`app_proto_register +36`、`bit_ctz64 +32` 等）。
  实测 `app_channel_dispatch` 栈帧 140 → **64B**（去重表按 RB 槽数开表，而非 64 指针 = 268B）。
- 新疆模块归因：CCM +1544 = 队列三段 408 + 任务栈 1024 + TCB 100 + 对齐；SRAM +32 = mask×2 + 句柄 + 时刻 + 标志。

### 6.1 `PROTO=CQ` 口径（B2-CQ，全量重编 `make -j8 PROTO=CQ BUILD_DIR=build/xj_b2cq`）

| `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | `._user_heap_stack` | SRAM 合计 / 余 | 告警 |
|---|---|---|---|---|---|---|---|
| 157004 | 195360 | 852 | 38940 | 121900 | 2564 | 125316 / **5756B** | 3 条既有 HAL |

- 链接通过、零新增告警；`.ccmram` 相对该口径未含新疆时的数额 +1544B（= 模块 CCM 增量，与 ALL 口径一致）。
- CQ 口径未单独测 B0/B1（本轮要求 B2 额外做一次 CQ）；CQ 下 LDI 被剔除，新疆模块不受影响。

### 6.2 `XJ_FAULT_SCREEN=0` 对照口径

`make -j8 BUILD_DIR=build/xj_b2_nofault DEFINES="-DUSE_HAL_DRIVER -DSTM32F407xx -DSTD_ALL_PROTO -DXJ_FAULT_SCREEN=0"`：

| `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | 相对 B2 |
|---|---|---|---|---|---|
| 170104 | 196008 | 1656 | 41196 | 125380 | `.text −288` / `.rodata −16` / `.bss −8`（心跳函数体、维护屏渲染与文案、`s_xj_fault_shown`/`s_xj_last_valid_tick` 全部消失） |

- 印证「=0 时完全无心跳计数、无渲染」：处理任务改 `osWaitForever`（无 500ms 节拍），
  `xj_fault_screen_show/clear` 无引用 → 链接期 `--gc-sections` 丢弃；编译零告警。

### 6.3 第二轮（2026-10-12）：PDF 命令面 + 帧长 74→106 + 自备 RB

命令与日志：`make -j8 PROTO=ALL DISP=1_263 FACTORY=STD BUILD_DIR=build/xj_r2_all`（`.analysis/xinjiang/build_r2_all.log`）、
`make -j8 PROTO=CQ DISP=1_263 FACTORY=STD BUILD_DIR=build/xj_r2_cq`（`build_r2_cq.log`），均全量重编、零新增告警。

| 口径 | `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | heap | SRAM 合计 / 余 | 相对 15:36 基线（B2 行） |
|---|---|---|---|---|---|---|---|---|
| ALL / 1_263 / STD | **172056** | **196264** | 1656 | **41324** | 125388 | 2564 | 129608 / **1464B** | text +1664 / rodata +240 / ccmram +128 / **bss ±0** |
| CQ / 1_263 / STD | **158652** | **195592** | 852 | **39068** | 121900 | 2564 | 125316 / **5756B** | text +1648 / rodata +232 / ccmram +128 / **bss ±0** |

- 归因：text/rodata = ①14 命令白名单 + `'S'` 四形态 + `'D'/'C'/'Z'/'P'` 应答 + 8 条文案 + ②帧长口径 74→106、
  'T' N≤6、'D' 全屏 7+16N（本轮 RB getter 只占 **+16B text**）；ccmram +128 = 队列 `3×(82→114)` +96 与
  任务帧缓冲 `82→114` +32；**bss ±0 = 自备槽体被同槽 weak 合并**（多协议口径无额外 SRAM）。
- 模块自身 `.ccmram` 跨度（`nm` 符号级）：任务帧缓冲 114 + 队列体 342 + cb 80 + 栈 1024 + TCB 100 = 1660（+对齐 8）= **1668B**。

#### 6.3.1 用户目标口径 A/B：自备 RB 的代价（等价 EIDE Debug = IAP + LDI + 新疆）

Makefile 原由 `python3 .analysis/myanmar/check_eide_targets.py --target Debug --emit-make /tmp/xjmk/Debug.mk` 生成
（**该脚本已随 `.analysis` 移除**；生成物 = 与 EIDE Debug 等价的 28 个 app 源文件、显示 `dev_display_1_263.c`、
DEFINES 含 `PROTO_CHONGQING` / `XJ_FAULT_SCREEN=0`）；
`make -f /tmp/xjmk/Debug.mk CONFIG=EIDEDebugXJ -B -j8 BUILD_DIR=build/xj_r2_dbg`（**改后**）与
`… BUILD_DIR=build/xj_r2_dbg_nofix`（**临时摘掉两行 `RB_PROVIDE_WEAK` 的改前对照**，日志 `build_r2_dbg{,_nofix}.log`）：

| 段 | 改前（无提供者） | 改后（自备） | Δ |
|---|---|---|---|
| `.text` | 130584 | 130600 | **+16**（两个 getter） |
| `.data` | 1592 | 1624 | **+32**（两个 `ring_buffer_t`） |
| `.bss` | 112372 | 113908 | **+1536**（768+768 槽体） |
| `.ccmram` | 15548 | 15548 | 0 |
| SRAM 合计 | 116528 | 118096 | **+1568** ⇒ 余 **12976B** |
| 告警 | 3（既有 HAL） | 3（既有 HAL） | 0 |

改后实测符号/表：`rb_provide_rs485` = `W 0x08048644`、`rb_provide_rs232` = `W 0x0804864c`；
`g_rb_provide` = `d1670408 45860408 4d860408` ⇒ **{RJ45, RS485, RS232} 三槽全非空**（改前为 `791c0408 00000000 00000000`）。

---

## §7 宿主推演与校验脚本

> **下表脚本均已随 `.analysis/` 目录移除（2026-10-13）**，结果列为当时的判据记录（历史）；
> 现行复核 = 直接读源码 + `make` / EIDE 段尺寸（`arm-none-eabi-size -A`）+ `objdump` 抽查。
> 需要脚本时用 `git show 069be96:<路径>` 取回（在仓库外运行）。

| 脚本 | 结果 |
|---|---|
| `.analysis/xinjiang/xj_frame_sim.py` | **331/331 通过**（2026-10-13 编译期断言清理后，原 7 条断言存在性检查改「契约注释 + 宿主复算」）：14 命令正常帧、半帧/粘包/伪头/超长/BCC 错、'V' 71/72 双长度、'S' 四帧长（报价 1/2/3 按 12/17/18 + 预制 8）、'D' 行显示 23 / 全屏 71 与 103 / 5 字节出向形态被 parse 拒、'T' N=1~6 收 / N=7 拒、'P' 图号透传、'B' 帧头 FE DB 被拒、颜色边界、亮度边界、16B 行切分（含 GBK 跨边界）、维护屏 15s 触发/解除状态机、同槽共存（SC_OL/RLS/MM/ETC/安徽/`{` 族）、probe 白名单（14 命令）、**§10 文档示例帧七条**（PDF §3.1.2 实例 71B 自洽 + §5.2 的 103B 'D' 双采样 + 'L' 8B 采样 FAKE）、源码一致性断言（含自备 RB 两槽、契约块纪律 + 「probe 内 `buf[N]` 访问须被前置 `rb_peek` 读长覆盖」的未初始化读回归断言 + 栈预算两条） |
| `.analysis/audit_20261008/landing_20261008/check_channel_gate.py` | **15/15 通过**（bind 调用点 35 处全在 `sw_app_initcall` 内；七条通道门控与桩调用一一对应） |
| `.analysis/myanmar/check_display_guards.py` | **16/16 硬断言 + 101/101 调用点判空覆盖**（新疆调用点全部判空） |
| `.analysis/comment_pass_20261008/scripts/refine_round2/check_contracts.py` / `check_doc_pointers.py` / `long_comments.py` | 复跑全绿（前两脚本的模块表为硬编码、不含新疆；新疆契约块纪律由 `xj_frame_sim.py` 自证：头 ≤10 行、条数 4~8、每条 ≤30 字、无日期/理由、引用编号一致、长行 ≤100 列） |
| `.analysis/myanmar/check_eide_targets.py` | **14/15**（结构不变量口径；唯一失败 = `[Debug] I6 定义 PROTO_CHONGQING ⇒ CQ 模块编入`——2026-10-08 已记录的**预存口径差异、非本轮回归**，待用户在 EIDE GUI 二选一；新疆是否编入 EIDE 由用户裁决，见 §5.2） |

---

## §8 待确认项与联调前置

**待厂商/用户确认（文档缺口）**：

| # | 项 | 现状 |
|---|---|---|
| Q1 | 「彩色行显示协议」 | **已澄清并实现** = PDF §3.1.1 行显示（`'D'` Len=23）+ §3.1.2 全屏（`'D'` Len=71） |
| Q2 | 缺失的 §2.6 命令 | **已澄清**：摘录版 2.6 缺失处 = PDF 的 §3.1.6 `'G'` 全亮 / §3.1.7 `'H'` 全关，均已实现 |
| Q3 | `'T'` 内容区是「N×16 变长」还是「固定 64 字节」 | 按 N×16 实现（M4）；固定区写法亦能通过 probe（长度 74）但 parse 会拒 |
| Q4 | `'V'` 是 71 还是 72 整帧长、内容 64 还是 65 字节 | probe 两种都收，内容 = `[6..len-2]`（71→64B、72→65B）。**注：PDF 全文无 `'V'` 命令**，此为摘录版扩展 |
| Q5 | `'S'` 编号 1 还是 2 字节、0x36 是什么 | **PDF 全文已澄清**：Len=8 形态编号为 1 字节（`[6]`，0xA0~0xB6）；0x36 无定义。报价 1/2/3 为 12/17/18 整帧长 |
| Q6 | 显示行字段的其它取值 | `'T'` 忽略；`'D'` Len=23 形态按 1~4 行号定位，越界丢弃 |
| Q7 | 心跳超时严格阈值（5s？）与「系统维护中」可配置范围 | 按 15s 实现（`XJ_FAULT_TIMEOUT_MS`，可改）；文案固定 |
| Q8 | 维语/汉维双语/男女生音色 | **硬件无此能力**（语音板仅自由文本 TTS + 音量）；若现场必须出维语需升级语音板 |
| Q9 | 音量映射（0x30~0x37 → 板载档位） | 按 `1~9` 线性映射实现，**须现场校准** |
| Q10 | 「收费系统软件配置为 STD」含义 | 疑为收费系统侧设备类型名，与本项目 `PROGRAM_CODE` 无关 |
| Q11 | §3.1.11/3.1.12 位图（`'P'`/`'B'`，128×64） | **硬件无位图存储**。`'P'` 已收并按协议「图号不匹配则清屏」执行；`'B'` 不进白名单 |
| Q12 | §4.4 入口发卡机编号 0xA1~0xAF | 本表按**出口**一栏实现；现场若是入口机需补入口文案（编号同、语义不同） |
| Q13 | `'G'`/`'H'` 红绿灯全亮/全关 | **单灯硬件**（PD14 一路，红绿靠继电器切换）无法同时点亮双灯，`'G'`→亮 / `'H'`→灭为唯一可表达近似 |
| Q14 | §3.2.6 启动复位 `'R'`、§3.2.7 Nack `'?'` | **不实现**：`'R'` 是上电主动上报（无上报时机与触发源）；`'?'` 在静默丢帧下无法定位出错帧，盲回会让车道机误判费显故障 |
| Q15 | **帧长上限**（原 74 是否够） | **2026-10-12 第二轮按 PDF §5.2 采样放宽到 106**：采样两份全屏 `'D'` 帧都是 103B（96B = 6 行 ×16B，BCC 按文档规则自洽），74 上限会 FAKE；现收 `'T'` N=1~6、`'D'` 全屏 N=2~6（39/55/71/87/103），屏外行由显示层裁剪 |
| Q16 | §5.2 采样 `FF DC 08 4C 30 00 01 01`（命令 `'L'` 0x4C） | **不属本协议**：`'L'` 不在 §3.1 命令表；该 8B 帧 XOR 前 7B = 0x56 ≠ 末字节 0x01，**不满足文档自身 BCC 规则** ⇒ 不加白名单（判为别的设备/别的协议在同一串口上的流量或文档抄录偏差） |

**联调前置**：

1. **DIP1**：按上位机波特率置位（OFF=9600 / ON=115200；9600 更保守）。
2. **接线**：RS232（USART3）或 RS485（USART1）—— 两槽都绑；上位机发帧样本（至少 `'T'`/`'O'`/`'C'` 各一帧
   十六进制含 BCC）用于比对。
3. **RB 提供者**：**本模块自备两槽体（§2.2，2026-10-12 第二轮起）** ⇒ 任何协议集合下都能注册；
   若曾经历「原 acquire-only 版本」的目标（如「IAP+LDI+新疆」），务必重烧新版镜像（旧镜像在该目标下
   模块不注册、串口通道不启动，见 §5.2 更正条）。
3b. **上位机样帧**：请提供任意一条命令的完整十六进制（最好含 `'T'`/`'D'`/`'O'`）；
   - 若首字节/第二字节不是 `FF DC` ⇒ 上位机可能不用这份协议；
   - 若是 `FE DB …` ⇒ 用的是下载位图 `'B'`（未实现，需补）；
   - 若整帧 > 106B ⇒ 还有更长形态（需再放宽）；
   - 响应核对：`'C'`/`'Z'` 会回 `FF DC 05 43/5A BCC`（本轮已实现），`'T'`/`'O'` 单向不回。
4. **EIDE**：按 §5.2 收录后 `EIDE: Reload Project`。
5. 无显示构建下：`'T'`/`'C'`/维护屏走判空跳过，灯控/语音正常（`check_display_guards.py` 已覆盖）。

---

## §9 变更记录

| 日期 | 内容 |
|---|---|
| 2026-10-12 | 首版：新疆费额显示器协议接入（9 命令最小集）+ 调度框架掩码 32→64；Makefile 收录（EIDE 待用户）；宿主推演 246/246；B0/B1/B2/B2-CQ 四口径实测（见 §6）；文档同步（doc/05-01、doc/06-04、doc/构建开关总表、CLAUDE.md） |
| 2026-10-12 | **复核修正（缺陷修复，段尺寸不变）**：`xj_probe_frame` 第二处 `rb_peek` 读长 `XJ_LEN_OFFSET`(2) → `XJ_LEN_OFFSET + 1U`(3)，消除 `head[XJ_LEN_OFFSET]` 的未初始化读（探针对首个/突发帧偶发误判 FAKE/WAIT → 现场偶发丢帧）；全模块 `rb_peek` 调用点复查仅此一处不配套；`xj_frame_sim.py` 加 1 条覆盖回归断言（**247/247**）；PROTO=ALL/CQ 全量重编段尺寸与上条逐字节相同（见 §6） |
| 2026-10-12 | **EIDE 收录轮（外部编辑 `.eide/eide.yml`，见 §5.2）**：`files` 增 `ProtocolParser_XinJiang` 虚拟文件夹（3 个 .c，排 `rls` 之后）；`targets.Debug` 编入 + `defineList` 追加 `XJ_FAULT_SCREEN=0` + `incList` 增本模块 `-I`；`targets.Myanmar` 排除（`excludeList` 增虚拟文件夹）+ `incList` 兜底；`targets.Release` 不动。等价口径实测 Debug 编入 **206 → 209** 文件、`.text +1616 / .rodata +248 / .ccmram +1544 / .bss +32`、告警仍 3 条 HAL；`XJ_FAULT_SCREEN=0` 净效果实测 text −288 / rodata −16 / bss −8；`check_eide_targets.py` 改前/改后均 14/15（I6 遗留，非本轮）。**未运行 EIDE、未提交、未改 git 配置**；待用户 `Reload Project` |
| 2026-10-12 | **按 PDF 全文（2020 版）扩协议面**：白名单 9→14 命令（新增 `'D'` 行/全屏、`'G'`/`'H'`、`'Z'`、`'P'`，`'B'` 不进）；`'S'` 按帧长 12/17/18/8 四形态分派（报价 1/2/3 拼中文句，修复报价语音 100% 静默失效）；§4.4 ETC 文案 5→8 条；§3.2 应答帧经来源通道回送；推演 312/312。**未改架构、未动其他模块** |
| 2026-10-12 | **第二轮：PDF 全文（2020 版 22 页）差异核对 + 现场「无反应」根因修复**。① **根因（已修）**：用户 EIDE Debug 目标被裁成「IAP + LDI + 新疆」（16:25 yml 外部编辑，相对 15:41 备份唯一差异 = 新增 3 行 SC_* 排除）⇒ RS485/RS232 **两槽无提供者** ⇒ `app_proto_acquire_buf` 双双失败 ⇒ 模块 init 静默 return（不注册/不绑定）⇒ `app_boot.c` 门控连串口通道启动都跳过（UART RX 未挂）⇒ 发帧零反应。修复 = 模块**自备两槽 `RB_PROVIDE_WEAK`**（A/B：text +16 / data +32 / bss +1536 / ccmram ±0；`PROTO=ALL`/`CQ` 下 `.bss` ±0）。② **帧长 74→106**：PDF §5.2 两份采样全屏 `'D'` 帧都是 103B（96B = 6 行，BCC 自洽，此前会 FAKE）⇒ 现收 `'T'` N≤6、`'D'` 全屏 7+16N（N=2~6）。③ 文档同步（本文 §1.1/§1.2/§2.2/§3.1/§4/§5.2/§6.3/§7/§8/§10、`doc/CLAUDE.md`、`doc/06-04` §8.4.6、`.analysis/xinjiang/eide_20261012/README.md` 补注）；推演 **329/329**；报告 `.analysis/xinjiang/round2_pdf_fulltext_and_rb.md`、PDF 全文留档 `.analysis/xinjiang/doc_v2/`。**未烧录、未提交、未改 git 配置、未改 `.eide/eide.yml`** |
| 2026-10-13 | **编译期检查清除（用户裁决）**：本模块 12 条 `_Static_assert` 全删 → 改写为「一句人话契约 + doc 指针」（§11 逐条映射）；机器码零变化（段级逐字节 + strip-debug elf md5 一致，`.analysis/comment_pass_20261013/report.md`）。同轮全项目注释精简（本模块内）：`app_xj_proto.c` 自备 RB 注释去现场事故叙事、`app_xj_proto_cmd.c` 音量/缓冲注释去「实测」措辞 |
| 2026-10-13 | **编译期断言清理扩展到全仓（第二轮，本轮）**：业务代码再删 **64 条**（20 文件）→ 逐条一行契约注释（含越限后果 + doc 指针）；保留用户裁决守卫 6 条（`dev_display_22_1665.c` 2 + `app_dispatch.c` RB 表/尺寸 4）。**行为零变化**：改前/改后全量重编段级逐字节 + strip-debug elf md5 相同；`xj_frame_sim.py` **329 → 331/331**（原 7 条断言存在性检查改「契约注释 + 宿主复算」；§7 表与 `.analysis/myanmar/mm_frame_sim.py` 158/158 同批复跑）。报告见 `.analysis/comment_pass_20261013/report.md` §⑧ |
| 2026-10-13 | **裁决执行轮：全项目编译期断言清零 + 只读观察 API / 调试冻结清理**。用户四项裁决：① 4 个零引用 git 跟踪脚本删除（`.analysis/cleanup_20260929/round2_ab_objdump.sh` + `.analysis/yn_ol/ab_*.link_no_ynol.sh`×3，记录见 `.analysis/cleanup_20261013_scripts/README.md` §9）；② **保留的 6 条编译期断言全部删除**（`dev_display_22_1665.c` 2 + `app_dispatch.c` 4）⇒ 业务代码 **0 条**，逐条改「一行契约注释（含越限后果）+ doc 指针」；③ 废弃头 `Application/Inc/protocol_select.h` 删除（全仓零 include）；④ `pl_tim_dbg_freeze` 与只读观察 API（`app_dispatch_*_drops` / `app_render_chip_info_get` / `app_render_glyph_probe` / `app_render_glyph_addr_for` / `app_udp_gzol_bind_status(_str)` / `g_tcp_server_connected` / `s_mm_lamp_state`）删除（零业务消费者）。**验证**：全量重编仅 3 条既有 HAL 告警；`PROTO=ALL` `.text 172056 → 171832`（−224）/ `.bss 125388 → 125364`（−24）/ 其余段 ±0，`PROTO=CQ` `.text −208` / `.bss −24`；逐对象期望清单 7/7、断言隔离 A/B 2/2（断言插回前后对象逐字节 + 反汇编逐行一致）。详见 `.analysis/comment_pass_20261013/report.md` 裁决执行轮与 `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` §8.4.7。 |
| 2026-10-12 | **现场回归修复（栈溢出致协议任务停摆）**：上条改动后板子对新疆帧完全无响应。根因 = 为传 `src_ch` 把任务里的 `const xj_parsed_cmd_t cmd` 改成具名非常量，`xj_parsed_cmd_t`（24B→64B）副本与 `xj_execute_cmd` 帧内副本在 256 words 任务栈上并存，逼近上限 → 栈溢出、任务停摆（未开 `configCHECK_FOR_STACK_OVERFLOW`，症状为静默卡死而非报错）。修复：`xj_execute_cmd` 改为 `(const channel_t *src_ch, const uint8_t *raw, uint16_t raw_len)` 自解析，调用点只传裸帧、零拷贝；`XJ_QUOTE_GBK_MAX` 96→48（实测最坏句 36B）。帧大小 `xj_execute_cmd` 124B→40B，最深调用链 116B/1024B（11%）。推演加 3 条栈预算回归断言（315/315） |

---

## §10 栈预算（易回归，务必保持）

任务栈 `PL_TASK_STATIC_STORAGE(xj_handle, 256)` = **1024B**。实测最深调用链：

| 路径 | 帧 |
|---|---|
| `xj_execute_cmd` | 40B |
| `_xj_exec_voice_quote`（含 48B GBK 缓冲） | 68B |
| `_xj_gbk_append` | 8B |
| **合计** | **116B / 1024B（11%）** |

**三条纪律**：① 解析结果 `xj_parsed_cmd_t` 只允许存在于 `xj_execute_cmd` 一帧内，
调用方（`xj_proto_handle_task`）**不得具名持有**——副本会使栈占用翻倍；② 任何放在任务栈上的
字节缓冲（如 `_xj_exec_voice_quote` 的 GBK 组装区）不得随手放大；③ 任务帧缓冲 `msg_buf` 随
`XJ_PAYLOAD_MAX`（现 106）变化（现 114B），放大它等于同时抬任务栈与 CCM 队列体。
`xj_frame_sim.py` 有负向断言盯着这三点。

> 注：**探针的 106B 帧缓冲位于 `frame_dispatch_task`（1KB）栈上**，不在本任务栈；
> 实测 `xj_probe_frame` 栈帧 88B（74 上限）→ 约 120B（106 上限），与调度任务自身 68B 帧叠加仍远低于 1KB。

---

## §11 源码契约（原 12 条编译期断言 → 文字契约，2026-10-13）

**背景**：2026-10-13 用户裁决——**防御性编译期检查不出现在业务逻辑（.c/.h）中**。本模块原有
12 条 `_Static_assert`（`app_xj_proto.h` 11 条 + `app_xj_proto_cmd.c` 1 条）已全部删除，逐条改写成「一句人话契约 + doc 指针」（源码位置见下表）；
删除**不改变编译与运行语义**（断言不产码，A/B 实测段级逐字节一致，见
`.analysis/comment_pass_20261013/report.md`）。全项目扩展见本文件 §11.1。

| # | 原断言（改错会怎样） | 现契约文字（源码留一行） | 源码位置 |
|---|---|---|---|
| 1 | `XJ_PAYLOAD_MAX ≤ RB_SIZE_RS485`（超限则整帧装不进 RS485 环形缓冲 → 丢帧） | 帧上限须 ≤ 串口 RB 容量 | `app_xj_proto.h` 帧常量区 |
| 2 | `XJ_PAYLOAD_MAX ≤ RB_SIZE_RS232`（同上，RS232 槽） | 同上 | 同上 |
| 3 | `XJ_FRAME_LEN_MAX < 220`（≥0xDC 时治超长度字段 0xDC 落入本帧合法域 → 首字节重叠判据失效、链卡死） | 上限须 < 治超长度字段 0xDC，保快拒不卡链 | 同上 |
| 4 | `XJ_T_TEXT_OFFSET + XJ_T_CONTENT_MAX + 1 == XJ_FRAME_LEN_MAX`（'T' 帧长与内容区不自洽 → 误判 FAKE/WAIT） | 'T' 整帧长 = 9 + 16N + BCC 1 | `app_xj_proto.h` 'T' 段 |
| 5 | `XJ_S_QUOTE1_FRAME_LEN == XJ_S_PAYLOAD_OFFSET + 5 + 1`（报价 1 帧长错 → probe 不认领） | 'S' 报价整帧长 = 6 + 载荷长 + 1 | `app_xj_proto.h` 'S' 段 |
| 6 | `XJ_S_QUOTE2_FRAME_LEN == XJ_S_PAYLOAD_OFFSET + 10 + 1`（报价 2） | 同上（报价 2 载荷 10B） | 同上 |
| 7 | `XJ_S_QUOTE3_FRAME_LEN == XJ_S_PAYLOAD_OFFSET + 11 + 1`（报价 3） | 同上（报价 3 载荷 11B） | 同上 |
| 8 | `XJ_D_LINE_FRAME_LEN == XJ_D_TEXT_OFFSET + 16 + 1`（行显示帧长错） | 'D' 行显示整帧长 = 6 + 16 + 1 | `app_xj_proto.h` 'D' 段 |
| 9 | `XJ_D_FULL_FRAME_LEN_MIN == 39`（全屏下限错 → 误收/误拒） | 全屏形态 6 + 16N + 1，N=2~6 ⇒ 39~103 | 同上 |
| 10 | `XJ_D_FULL_FRAME_LEN_MAX == 103`（全屏上限错） | 同上 | 同上 |
| 11 | `XJ_D_FULL_FRAME_LEN ∈ [MIN, MAX]`（文档表 71B 形态越界 → 该帧被拒） | 文档表形态 71B 须落在该区间内 | 同上 |
| 12 | `XJ_PRESET_COUNT == sizeof(s_xj_preset_texts)/sizeof([0])`（编号表与文案表错位 → 播错语音） | 编号表与文案表逐项同序等长 | `app_xj_proto_cmd.c` 预制语音表 |

**曾保留的例外已清零（2026-10-13 用户裁决）**：原「用户 2026-10-08 明确要求保留」的
`Device/Display/dev_display_22_1665.c` 2 条守卫与 `Application/Src/app_dispatch.c` 4 条（RB 表/尺寸）
**已全部删除** ⇒ 业务代码现存 **0 条**编译期断言，约束 = 契约注释 + 链接期兜底。

### §11.1 全项目扩展（2026-10-13 第二轮，本轮）

同一裁决扩展到全仓业务代码（`Application` / `Device` / `Platform` / `Kernel` / `Core`）：
**64 条** `_Static_assert` / `static_assert` 全部删除（涉及 19 个文件，另 1 个文件仅注释同步），逐条改为「一行契约注释
（含越限后果）+ doc 指针」；连同本模块第一轮的 12 条，全项目共清理 **76 条**：

| 族 | 条数 | 原断言位置 | 现契约注释落点 |
|---|---|---|---|
| 协议帧上限 ≤ RB（含定长帧） | 25 | 9 个串口协议模块 18（QH/SD/GZ/YN/ANHUI/ETC/MTC/SC_OL/RLS）+ GZ_OL 3 + YN_OL 3 + MM 1 | 各 payload 宏处一行，指向 `doc/08_协议模块接入规则/01_通用规则与模块骨架.md` §4 |
| 定长 / 表长自证 | 3 | MM 定长 20B、YN_OL `0x49` 记录 12B、IAP 命令表条目数 | 结构体/表定义处一行 |
| Sector1 记录布局（三固件二进制兼容） | 10 | `Application/Inc/Config/app_board_net_cfg.h`（72B + 偏移） | 头文件「布局契约」块，指向 `doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md` §15 |
| LDI 结构体布局 | 26 | `app_ldi_cmd.c` 22 + `app_ldi_cmd.h` 1 + `app_ldi_cfg.h` 1 + `app_ldi.c` 2 | 结构体处一行，指向 `doc/02` 附件2 |

**例外清零（2026-10-13 用户裁决执行轮）**：原保留的 `dev_display_22_1665.c` 2 条 + `app_dispatch.c` 4 条亦已删除，业务代码 0 条编译期断言。
**行为零变化**：断言不产码——改前/改后同口径全量构建段级逐字节一致 + strip-debug elf md5 相同
（见 `.analysis/comment_pass_20261013/report.md` §⑧）。
