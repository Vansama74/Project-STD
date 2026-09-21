# 交付实施报告：22-1703 显示模组驱动 + 贵州治超屏协议接入

**日期**：2026-09-14
**范围**：`/home/yystation/Program/3833024/Project-STD-main`
**依据**：`.analysis/9k23881580/dev_display_22_1703_analysis.md`（499 行）、`.analysis/9k23881580/gz_overload_protocol_analysis.md`（695 行）、wyh 分支参照驱动 `Desktop/Project-STD-wyh/Device/Display/dev_P10_32x16_2200001703.c`
**纪律**：无实机操作、无烧录、无任何 git 网络操作、无 commit；除 `.eide/eide.yml` 外未改动与任务无关的文件。

---

## 1. 实际改动文件清单

### 1.1 新增（9 个仓库文件 + 1 个工具脚本）

| 文件 | 一句话说明 |
|---|---|
| `Device/Display/dev_display_22_1703.c` | P10 32x16（料号 2200001703）模组驱动：1/4 扫描、224×64、合并写 BSRR 扫描、CCMRAM 显存/查表 |
| `Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h` | 协议公共定义：帧常量、命令枚举、解析结构体、API |
| `Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.h` | 薄转发头（解析入口） |
| `Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.h` | 薄转发头（命令执行入口） |
| `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c` | RB 提供 + 协议注册 + probe 探测 + 帧处理任务 + `sw_app_initcall` |
| `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.c` | `gz_ol_parse_frame()` 纯函数解析 |
| `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c` | 8 命令执行 + 应答组帧（0x10/0x50/0x70） |
| `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_default.c` | 上电默认画面（三行绿字，`app_default_display_register`） |
| `doc/01_显示系统/22-1703模组驱动分析与迁移记录.md` | 模组驱动分析/迁移记录（参数、映射推导与裁决证据、scan 档位与 CPU 估算、差异清单、上机清单） |
| `doc/14_贵州治超屏协议/README.md` | 协议接入记录（帧格式、命令表、不一致清单、帧头冲突、内存账、Q1~Q14 采纳表、as-built、联调帧） |
| `~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/gz_ol_frame_sim.py` | 宿主推演脚本（probe/parse 镜像 + 同槽链式互斥，38 用例；**仓库外**，与既有 `gz_frame_sim.py` 同目录惯例） |

### 1.2 修改（8 个，均为共享文件，串行编辑）

| 文件 | 改动内容 |
|---|---|
| `Makefile` | ① 新增 `DISP` 显示模组选择开关（`1_263` 默认 / `22_1703`，非法值 `$(error)`）；② `SRC_DEVICE` 的模组条目改 `$(SRC_DISPLAY)`；③ `INC_DIRS` 增贵州治超 include 目录；④ `SRC_APPLICATION` 增 4 个协议源文件 |
| `.eide/eide.yml` | ① `virtualFolder → Device/Display` 收录 `dev_display_22_1703.c`；② `targets.Debug.excludeList` 加该文件（→ EIDE Debug 仍编 1-263，默认行为不变）；③ `virtualFolder → Application → protocol` 新增 `ProtocolParser_GuiZhou_Overload` 节点（4 文件）；④ `targets.Debug.incList` 增该 include 目录 |
| `Device/Inc/dev_display.h` | 新增 `dev_display_22_1703_get()` 声明 |
| `doc/CLAUDE.md` | 构建小节（DISP）、内存布局（SRAM/CCM 实测、GZ_OL 队列例外）、Display 子系统（模组清单表 + 22-1703 小节 + 1-263 几何更正）、initcall 表（hw 增 22-1703、sw 增 GZ_OL）、Application 模块全景（GZ_OL 行）、协议新增「贵州治超屏协议」小节、选编口径重写、文档地图加 doc/14 |
| `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` | 头部状态行加「最新采样」与口径警告、§1 加例外注记、新增 §7（2026-09-14 四口径全量重采 + CCM 明细 + 增量账）、修订记录 |
| `doc/05_协议模块多协议兼容优化/01_architecture.md` | §3.3 帧头事实表增 TCLY 行、§4 RB 绑定表 RS485/RS232 增 GZ_OL、§4.1 queue 表增 GZ_OL 行 + 变更记录、§6 兼容矩阵增 3 行（贵州治超 vs 串口各协议 / RS232_1 禁止 / vs 贵州常规费显） |
| `doc/01_显示系统/1-263模组驱动分析与修复记录.md` | 新增 §10：DISP 收录变更、EIDE 侧说明、几何实态更正（224×64）、§8 待确认项 1/2 的实态答复 |
| `doc/08_协议模块接入规则/{01_通用规则与模块骨架.md, 02_串口协议接入.md}` | 现有实例列表与 RB 槽位协议列表补 `ProtocolParser_GuiZhou_Overload` / 安徽 |
| `doc/构建开关总表.md` | §1 构建入口说明补 DISP/DISP 语义与 EIDE Release 陈旧性、§3 新增 `DISP` 行、§4 修订记录 |

---

## 2. `dev_display_22_1703` 驱动实现要点

### 2.1 模组参数与映射

| 项 | 值 |
|---|---|
| 模块码 | `2200001703` |
| 单模块 | 32（水平）×16（垂直），2 通道/模块（R1G1B1 + R2G2B2），**1/4 扫描** |
| 拼装 | **7 模块/行 × 4 模块/列 = 224×64**，总通道 **8** |
| 派生 | 显存 14336B×2、单通道 1792、每扫描行 **448** 时钟、每行 28 组 |

**映射（等价于源固件 `displayDataUpdata`）**：
`ch = y/8`、`line = y%4`、`half = (y%8)/4`、`row_dst[y] = ch*1792 + line*448 + half*8`；
`prepare` = 每逻辑行 28 个 8 字节块拷贝（块间步长 16，下半行整体 +8），支持 ④a 脏矩形行子集。

### 2.2 Q3 裁决（通道序与下半行位序）——**采纳 wyh 版，且 wyh 版 ≡ 源固件**

**证据（宿主穷举比对，`/tmp/map_check.c`）**：同一 `pixel_map`（伪随机 + 全 0 两组输入）分别跑
①源固件公式（`timer.c:359-383`）、②wyh 版 `_prepare` 公式、③本驱动 `row_dst` 公式：

```
几何：224x64, 通道 8, 单通道 1792, 每扫描行 448 时钟, 每行 28 组
位流比对：源固件 vs wyh版 = 一致；源固件 vs 本驱动 = 一致
扫描读取序比对（line/px/ch 读取顺序）：源 vs wyh = 全等；源 vs 本驱动 = 全等
扫描覆盖唯一性：14336/14336 字节 唯一覆盖（异常项 0）
结果：位流全等；扫描序全等；覆盖通过；零值场景全等（EXIT=0）
```

**结论**：wyh 版 = 源固件口径 = **自然通道序 `{R1..R8}`（ch = y/8）+ 下半行位序顺序（偏移 +8）**；
旧 STD `convert_pixelmap_p10`（通道相邻互换 + 下半行逆序 `15-x%8`）**是唯一的异类**且其所在工程/DOM 已废弃，
本屏体不采用。分析报告 §1.6 提出的"必须实机裁决"项**已由代码级穷举证据闭环**（剩余风险仅为硬件接线与
本推导不符，由上机打点图案兜底）。

### 2.3 与 wyh 版 / 源固件的差异清单

| # | 项 | wyh 分支 | 本实现 | 说明 |
|---|---|---|---|---|
| 1 | 文件/实例/init 命名 | `dev_P10_32x16_2200001703.c` / `dev_display_module_t` / `dev_p10_32x16_22200001703_init`（笔误多一个 2） | `dev_display_22_1703.c` / `dev_display_22_1703_t` / `g_22_1703` / `dev_display_22_1703_init` | 对齐 STD 命名规范 |
| 2 | **拼装数** | `MODULE_ROWS = 10`（→ **320×64**） | `MODULE_ROWS = 7`（→ **224×64**） | **差异点**：wyh 值无出处记录；本订单（贵州治超，源固件 `MODULE_PER_ROW 7`）取 7。切换成本 = 改 1 个宏（CCM +12288B）——**列为待拍板项 §5.2** |
| 3 | 基类 API | 旧基类（无 `module_code` / `commit_frame_rect` / `dirty_rect_*` / `DEV_DISPLAY_BRIGHTNESS_MAX`） | 当前 STD 基类全支持 | 以当前基类为准（用户提示已确认 wyh 版较旧） |
| 4 | 行选 | `pl_hub75_set_row()` | `pl_hub75_Decoder_set_row()` | 当前 API；1/4 扫下 A/B 二进制、C/D 恒 0（与源固件逐位一致） |
| 5 | prepare | 逐像素 `%`/`/` 循环（20480 次迭代含除法）、无脏矩形 | `row_dst[]` + 28 次 8 字节 `memcpy` + ④a 脏矩形行子集 | 位流等价（§2.2 穷举）；CPU/可维护性更优 |
| 6 | scan | 逐通道 3 次 `pl_hub75_bsrr_flush`（g_bsrr 表 1536B） | **5 端口合并写**（表 2848B），含端口一致性校验与通用回退 | 位流等价（§2.4 穷举）；档位 D+ |
| 7 | 亮度初值 | 未设（基类默认 0 → 上电全暗） | `DEV_DISPLAY_BRIGHTNESS_MAX`（8，与 1-263 一致） | 与现行模组一致 |
| 8 | 引脚映射 | 相同（`g_hub75_pin_r/g/b`） | 相同，**零 Platform 扩展** | 分析报告 §1.3 引脚对照结论成立 |

### 2.4 scan 实现：档位 D+（按 GPIO 端口合并写 BSRR）

- 8 通道 24 个数据脚按 GPIO 端口分 **5 组**（G/B/E/C/F，由 `g_hub75_pin_*` 在 init 期推导）：
  `G ← ch0(R,G,B)+ch1(R)`、`B ← ch1(G,B)+ch2(R,G)`、`E ← ch2(B)+ch3(R,G,B)+ch4(R,G,B)`、
  `C ← ch5(R,G,B)`、`F ← ch6(R,G,B)+ch7(R,G,B)`。
- 每时钟 **5 次 BSRR 写**（替代档位 A 的 24 次写引脚），key 表达式为 2~4 条移位/或指令；表大小
  64+64+512+8+64 = 712 项 × 4B = **2848B CCMRAM**。
- **安全网**：init 期校验同组信号是否落在同一 GPIO 端口，失败则 `s_22_1703_merged_ok=false` →
  scan 走通用逐通道 `pl_hub75_set_rgb` 回退（不静默出错）。
- **等价性穷举证据**（宿主，8^8 = **16,777,216** 组合，`/tmp/merge_check.c`）：

```
信号覆盖：24 个数据脚 唯一覆盖
穷举 16777216 个颜色组合：失配 0 → 全部等价（合并写 == 逐通道写）
```

> 该穷举在实现过程中**实际抓到一次真实缺陷**（grp1/grp2 的 key 位号与热路径表达式不一致，
> 表现为通道错位），修正后复验通过——这正是"零实机"条件下必须做的机械验证。

- **指令数与 CPU 估算**（`-Og` 反汇编逐条计数）：

| 驱动 | 每时钟指令 | 每行（448 时钟） | 通道-时钟/行 | 每通道-时钟 |
|---|---|---|---|---|
| `_1_263_scan`（现行 2 通道） | 58（22×2 + 14） | 25,984 | 896 | 29 |
| `_22_1703_scan`（8 通道，合并写） | **45** | **20,160** | 3,584 | 5.6 |

  按分析报告 §4.4 的 1.0~1.3 周期/指令折算（168MHz）：**每行 ≈ 120~156 µs**，占 TIM3 现行
  500µs 行周期的 **24~31%**（若 TIM3 取 1ms → 12~16%）。对比：档位 A 339~440µs（68~88%）、
  报告档位 D 估算 133~173µs。**结论：优于档位 D 估算上限，且在 8 通道 4× 位输出的前提下比现行
  1-263（2 通道）每行指令数还少 22%。**
  ⚠ 上机后须用 DWT 实测复核（本数字为指令计数折算，非实机计时）。

### 2.5 CCMRAM 占用（实测）

| 对象 | 大小 |
|---|---|
| `_22_1703_pixel_map` | 14336 |
| `_22_1703_hub75_buff` | 14336 |
| `_22_1703_row_dst`（64×uint16） | 128 |
| 合并表 `_22_1703_tab_{g,b,e,c,f}` | 2848 |
| **合计** | **31648** |

SRAM：`g_22_1703` 48 + `_22_1703_grps[]` 80（`.data`）+ `s_22_1703_merged_ok` 1（`.bss`）≈ **129B**。
链接实测（`PROTO=ALL` 对照 1-263）：text **+256** / rodata **+80** / data **+80** / ccmram **+2080** / bss 0。

---

## 3. 构建结果（全部实跑，非估算）

**命令与结果**（`arm-none-eabi-size -A`；每口径先删产物强制重链接——见 §5.1 发现 A）：

| # | 命令 | 结果 | text | rodata | data | **ccmram（余量）** | bss | heap_stack | SRAM 合计（余量） | 告警 |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 改动前基线 `make -B -j8` | ✅ 链接 | 161524 | 197544 | 1664 | 35948（29588） | 126332 | 2564 | 130560（512） | — |
| 0' | 改动前基线 `make -B -j8 PROTO=CQ` | ✅ 链接 | 148332 | 196888 | 860 | 35948（29588） | 122856 | 2564 | 126280（4792） | — |
| 1 | **`make -j8`**（PROTO=ALL，DISP=1_263，默认口径） | ✅ 链接 | 164068 | 197800 | 1664 | **37084（28452）** | 126348 | 2564 | **130576（496）** | **0** |
| 2 | **`make -j8 PROTO=CQ`** | ✅ 链接 | 150476 | 197104 | 860 | **37084（28452）** | 122872 | 2564 | 126296（4776） | **0** |
| 3 | **`make -j8 DISP=22_1703`** | ✅ 链接 | 164324 | 197880 | 1744 | **39164（26372）** | 126348 | 2564 | **130656（416）** | **0** |
| 4 | **`make -j8 DISP=22_1703 PROTO=CQ`** | ✅ 链接 | 150732 | 197192 | 940 | **39164（26372）** | 122872 | 2564 | 126376（4696） | **0** |

> **更正（2026-09-14，口径指纹 stamp 修复后重采）**：上表第 1 / 3 行（`PROTO=ALL`）为
> **污染态**采样——`make -j8` 只用人工删产物规避、**不重编对象**，两行沿用了上一 CQ 口径的
> `app_cq_proto.o`，其 `CQ_FAULT_SCREEN=1` 引用链被 `--gc-sections` 保留（`cq_render_fault_screen`
> 316B + 故障屏文案 rodata 30B + `app_factory_test_active` 12B，另 `cq_proto_timer_task` +56、
> `_udp_cq_read_port` +28）。**更正后（干净全量重编）**：第 1 行 text **163652** / rodata **197760**、
> 第 3 行 text **163924** / rodata **197840**；data / ccmram / bss / SRAM 合计与余量**不受影响**，
> 第 0 / 0' / 2 / 4 行经干净重采逐项复现（第 0 / 0' 行原本即用 `make -B` 全量重编，干净）。
> 修复与取证详见 `make_stamp_fix_report.md` 与 `doc/构建开关总表.md` §4.1。

- 四条命令 **`make EXIT=0`，`-Wall -Wextra` 零新增告警**（脚本统计 `warning:` / `error` 计数均为 0）。
- **`.ccmram` 段内容复核（`nm`）**：`DISP=1_263` → `_1_263_pixel_map/hub75_buff/row_dst` + `g_bsrr` + `s_cq_queue_buf` + `s_gz_ol_queue_buf/cb`；`DISP=22_1703` → 同名替换为 `_22_1703_*`（含 `tab_g/b/e/c/f`），**两模组不同时出现** ✅。
- **CCMRAM 构成**：模组 29568（1-263）/ 31648（22-1703）+ CQ 6377 + 贵州治超 1136 + 对齐 3。
- **增量账**：贵州治超 text +2544 / rodata +256 / data 0 / bss +16 / **ccmram +1136**；
  22-1703 相对 1-263 text +256 / rodata +80 / data +80 / **ccmram +2080**。
- 1-263 与 22-1703 合计 + CQ + GZ_OL = **68729B > 64KB** → 同编必失败，验证了 `DISP` 二选一与 EIDE exclude 的必要性。

### 3.1 静态检查

- 新驱动/协议单独编译（`-Wall -Wextra -std=gnu23 -Og`）**零告警**；四口径全量构建零新增告警。
- IDE linter（`ReadLints`）对 `Device/Display/dev_display_22_1703.c` 与 `ProtocolParser_GuiZhou_Overload/`
  目录**无任何诊断**。
- **最终复验**：全部代码冻结后**重跑两轮四口径构建**（含注释级改动的一轮），四条命令均
  `EXIT=0`、零告警，且两轮段尺寸**逐字节一致**（§3 表格数字即为最终产物实测）。

### 3.2 宿主推演（协议）

`python3 ~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/gz_ol_frame_sim.py` →
**38 用例全部通过**，覆盖：空缓冲 / 首字节错 / 引导串半帧 / 引导串错 / 长度未到齐 / 长度下界 /
长度超 RB / 整帧未到齐 / 尾字节错 / 超长 SKIP / 各命令正常帧 / 粘包 / 垃圾前缀重同步 / 载荷内嵌
"TCLY" / parse 的 12 类语义 / 与四川治超·RLS·青海`{`族·安徽的**双向快拒**。

---

## 4. 协议模块实现要点 + 采纳裁决表

### 4.1 实现要点

- 帧：`54 43 4C 59` + 序号(4) + **长度(2 小端 = 整帧长)** + 保留(2) + 命令(4，低字节有效) + 载荷 + `00`，**无校验**；
  `FRAME_LEN_MIN=17`、`FRAME_LEN_MAX=256`、`QUEUE_DEPTH=3`；
- probe：首字节快拒 → 引导串 4B 全匹配 → 长度下界 → **长度上界 ≤ RB 容量（767）** → 整帧到齐 → 尾字节 → 超 256 返回 **SKIP**（整帧消费不入队）；
- 绑定 `CH_ID_RS485` + `CH_ID_RS232`（双 mask 一队列）；`RB_PROVIDE_WEAK` 双槽 provide；
- 任务 `gz_ol_handle_task` 1KB 栈（ucHeap）、`osPriorityNormal`；
- **队列体 792 + cb 80 + 任务帧缓冲 264 = 1136B 置 `.ccmram`**（理由：`PROTO=ALL` SRAM 余量 496B 不足；CPU 独占访问无 DMA，CQ 先例）；
- 渲染互斥：`0x20`/`0x30`/`0x80` 的「清屏+渲染+提交」整段持 `app_scroll_render_lock`；
- 上电画面：`app_gz_ol_proto_default.c` 注册三行绿字 FONT_16 黑体（前导空格与源固件一致）。

### 4.2 源分析报告 Q1~Q14 采纳表

| # | 采纳 | 理由 / 风险 |
|---|---|---|
| Q1 | payload **256B** + 超长结构合法帧 **SKIP** | 实测合法最大 158B，1.6× 余量；SKIP 避免"超长帧污染重同步"（报告 §4.3 方案 B） |
| Q2 | `0x10` **应答 `00 00`** | 文档要求应答（无故障硬件补 0）；源固件静默属缺陷。风险：若上位机把 19B 应答误当数据 → 概率极低（长度/尾字节自校验） |
| Q3 | `0x50` **与 `0x60` 均触发搜索应答** | 覆盖文档版（0x60）与固件版（0x50）上位机；无副作用 |
| Q4 | 搜索应答 **单播回源** | STD 通道语义；**风险**：源固件为广播 + 固定对端口 10028，若上位机依赖广播收不到应答 → 需联调确认（§5.4） |
| Q5 | 落库 **`app_board_net_cfg_update` 写 `port`、保留 `udp_port`**；应答后 `osDelay(100)` 复位 | STD 唯一配置真源；改 IP 重启生效。**风险**：本协议语义的 port 是"设备自身 UDP 口"，而 STD `port` = TCP 业务口 → 语义借用（已在 doc/14 §8 Q5 标注，待用户确认） |
| Q6 | `0x80` 完整副作用（挂起光敏 + 亮度 8）+ 收到任意帧恢复自动调光 | 复现现场可观察行为 |
| Q7 | 颜色按源固件（`0000FF`→**黄**、其余→红；`0x80` 未识别色不动屏幕） | 功能等效优先 |
| Q8 | 换行交渲染引擎原生（`0x0A` 换行 / 跳过 `0x0D`）+ `word_wrap=true` | 不把 `_` 当换行（避免破坏含下划线文本）；字面量 `\n` 待抓帧决定 |
| Q9 | 屏宽/屏高字段 **忽略** | 与源固件及 STD 既有裁决一致（坐标以实际几何为准） |
| Q10 | 绑定 **RS485 + RS232**（暂不绑 UDP） | 覆盖源固件串口三路；UDP 绑定与 Q4/Q5 强耦合，待联调 |
| Q11 | **实现上电画面** | 属功能等效；副作用（单槽先注册生效，全协议 dev 构建与山东竞争）已在 doc/14 §7 提示 |
| Q12 | 队列置 **CCMRAM** | SRAM 余量实测仅 496B（比报告推算的 536B 更紧），CCM 余量充足（26372B） |
| Q13 | 静态渲染持 **`app_scroll_render_lock`** | 安徽与贵州治超可同编同屏；无滚动时零成本 |
| Q14 | 非法命令 **静默丢弃** | 协议未定义错误应答；联调期可按 doc/08 临时加 RTT dump |

**额外采纳（报告未覆盖，本轮新增裁决）**：

- **无载荷命令（0x10/0x30/0x50/0x60）不校验载荷长度** —— 依据：协议文档的 0x30 清屏**帧例本身带
  17B 显示参数块**（`22 00` → 34B 帧），严格校验会拒掉上位机真实帧；源固件各 handler 也忽略载荷。
  该点在宿主推演中已作为回归用例固化。
- **`0x40` 载荷要求 ≥14B**（源固件不检查直接读 → 越界风险；加下界、多余字节忽略）。
- **`0x20` 的 x/y 越界钳到 0**（源固件交给渲染层裁剪；本实现避免语义歧义，见 doc/14 §8 遗留 5）。
- **字号非 16/24/32 → 整帧丢弃**（源固件为 UB，比源固件更严格）。

---

## 5. 仍需用户拍板的问题清单（8 条）

### 5.1 构建/工程纪律（2 条，影响理解但不阻塞）

1. **发现 A：`make PROTO=CQ` 不会自动重链接** —— `make` 的依赖不含"源文件列表"，切换口径后若
   产物比所有目标文件新，make 静默复用上一口径的 elf（本次实测：`PROTO=CQ` 首轮得到与 `PROTO=ALL`
   完全相同的段尺寸）。**本轮通过"先删 `build/Debug/Project_STD.*`"规避**。是否在 Makefile 里固化
   （例如让 elf 依赖一个记录源列表的 stamp 文件）请裁决；在此之前，**切换口径必须强制重链接**
   （删产物或 `make -B`）。
   > **已解决（2026-09-14）**：按本条建议实装「口径指纹 stamp」——`build/$(CONFIG)/.build_stamp`
   > 记录 CONFIG/TOOLCHAIN/PROTO/DISP/DEFINES/CFLAGS/LDFLAGS/Makefile cksum/源列表，同时作为每个
   > `.o` 与 elf 的先决条件：切口径必然全量重编 + 重链接（**且不会再有旧口径对象混入**，这正是
   > §3 表第 1/3 行数值偏差的根因），口径不变仍保持增量。另加 `-MMD -MP` 头文件依赖与
   > `.DEFAULT_GOAL := all`。**此后不再需要删产物或 `make -B`**。详见
   > `make_stamp_fix_report.md`、`doc/构建开关总表.md` §4/§4.1。
2. **发现 B：eide.yml 的 `excludeList` 是"虚拟路径"，且当前有效** —— 分析报告附录 B / §4.5 称
   `<virtual_root>/Application/protocol/...` 为"陈旧路径、已不存在"，本轮复核认为**该判断不成立**：
   它是 `virtualFolder` 树里的虚拟路径（`Application → protocol → ProtocolParser_XXX`），因此
   CQ/安徽/青海/贵州/云南/山东/RLS 等条目**确实生效**（旁证：历史上 EIDE Debug 产物的
   `.ccmram` 不含 `s_cq_queue_buf`）。据此，`doc/CLAUDE.md` 旧文"安徽在 EIDE Debug 编入不排除"
   也应更正为"按 excludeList 排除"（本轮已按此更正）。**如与实际 EIDE 行为不符，请以 EIDE 内
   实际编译列表为准并告知**（我无法运行 EIDE 验证）。

### 5.2 需要用户决策（3 条，影响交付口径）

3. **22-1703 的屏体拼装数**：wyh 参照驱动写 `MODULE_ROWS = 10`（→ **320×64**），本任务给定与源固件
   （9K23881580 `MODULE_PER_ROW 7`）均为 **7×4 = 224×64**。**本轮取 7**（与订单、协议、现行 1-263
   一致），并在驱动里做成单点宏——若实物为 10 模块/行，改 1 个宏即可（CCM 31648 → **43936B**，
   余量仍 15181B）。**请以整机模组数量确认**。
4. **TIM3 行同步周期（全局参数）**：现为 **500µs**（`Core/Src/tim.c:82-84`），本模组扫描估算占
   24~31%（若切到 1ms → 12~16%，刷新率 250Hz 仍为源固件 71.4Hz 的 3.5 倍）。**本轮未改**（按纪律，
   全局参数需先裁决）。建议：上机用 DWT 实测 `_22_1703_scan` 单行耗时，再决定是否放宽到 1ms。
5. **EIDE Debug 的默认显示模组**：本轮把 22-1703 放入 `targets.Debug.excludeList`（= EIDE 继续编
   1-263，默认行为不变）。**若 P10 订单才是当前要出货/调试的目标，应把两条排除互换**
   （1-263 入排除、22-1703 出排除）——请指示，或由用户在 EIDE 内自行切换（改完必须 Reload Project）。

### 5.3 联调后才能闭环（3 条）

6. **协议通道与真实帧**：上位机（.NET 测试软件）实际走串口还是网口、JSON 类字段的实际字节，
   需抓包确认（源分析报告 U1~U4）。
7. **搜索应答是否需要广播**（Q4 遗留）+ **`0x40` 端口语义**（Q5：本协议 port = 设备自身 UDP 口
   vs STD `port` = TCP 业务口 —— 本轮按报告推荐写 `port`，若现场需要"改设备 UDP 口"另需字段裁决）。
8. **屏体填充颜色**（RG 双色 vs RGB 全彩）与**行驱动芯片型号**（译码器型 ABCD 假设）需模组铭牌/BOM
   或示波器确认；另 `0x20` 换行符是否含字面量 `\n`（Q8 遗留）、`0x10` 故障位逐位含义（协议 doc
   文本错位）一并待现场确认。

---

## 6. 文档更新清单

| 文档 | 更新章节 |
|---|---|
| **`doc/14_贵州治超屏协议/README.md`**（新建） | 全部（协议事实/命令表/不一致清单/probe 链/帧头冲突/构建口径/内存实测/上电画面/Q1~Q14 采纳表/as-built/联调帧/修订） |
| **`doc/01_显示系统/22-1703模组驱动分析与迁移记录.md`**（新建） | 全部（参数/映射与 Q3 裁决证据/scan 档位 D+ 与指令数/差异清单/内存实测/选编口径/上机清单） |
| `doc/01_显示系统/1-263模组驱动分析与修复记录.md` | 新增 §10（DISP 收录变更、EIDE 说明、几何实态 224×64、§8 待确认项答复） |
| `doc/CLAUDE.md` | ① 构建小节（DISP 开关）② 内存布局（SRAM 130576B / CCM 37084B 实测、GZ_OL 队列例外 2）③ Display 子系统（模组清单表、1-263 224×64 更正、22-1703 小节含 scan 档位与 CPU）④ initcall 表 hw 加 22-1703、sw 加 GZ_OL ⑤ Application 模块全景加 `app_gz_ol_proto` 行 ⑥ 新增「贵州治超屏协议」小节 ⑦ 选编口径重写 + 口径更正声明 ⑧ 文档地图加 doc/14 |
| `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` | 头部「最新采样」行 + 口径警告、§1 例外注记、**新增 §7**（四口径全量重采、CCM 明细、增量账、禁止混用声明）、修订记录 |
| `doc/05_协议模块多协议兼容优化/01_architecture.md` | §3.3 帧头事实表（TCLY 行）、§4 RB 绑定表（RS485/RS232 加 GZ_OL）、§4.1 queue 表（GZ_OL 行）+ 变更记录、§6 兼容矩阵（3 行） |
| `doc/08_协议模块接入规则/01_通用规则与模块骨架.md` | 现有实例列表补 Anhui / GuiZhou_Overload |
| `doc/08_协议模块接入规则/02_串口协议接入.md` | RB 槽位协议列表补 安徽 / 贵州治超 |
| `doc/构建开关总表.md` | §1 构建入口说明（DISP、EIDE Release 陈旧性）、§3 新增 `DISP` 行、§4 修订记录 |

**超出本次任务范围、未改动（明确列出）**：

- `doc/06-04` 的 §0~§3/§5/§6 历史段落（按 1-577 3×3 EIDE 口径与 1-263 224×128 记账）**保留为历史记录**，
  仅在头部加"历史口径"警告并指向新增 §7（逐节改写会覆盖多轮历史账，超出本任务范围）。
- `.eide/eide.yml` 的 `targets.Release` 为陈旧配置（incList 全为已删除的 `Drivers/BSP/*` 路径），
  非可用构建入口 → 未改动，仅在 `doc/构建开关总表.md` 与 `doc/CLAUDE.md` 标注。
- `Application/Inc/ProtocolParser_Anhui/*`、`Application/Src/RLS/*`、`app_render.*`、`app_scroll.*`、
  `dev_display_1_263.c` 等**存在会话开始前的外部未提交改动**，本轮**未触碰**（除 `dev_display.h`
  加 1 条声明）。
- 分析报告（`.analysis/9k23881580/*.md`）作为分析归档**未修改**；本报告 §5.1 更正了其中两条判断
  （eide 路径有效性、SRAM 余量数值），并建议由用户决定是否转正为 `doc/` 正式文档后删除 `.analysis/`。

---

## 7. EIDE 后续操作提醒（必须执行）

1. **本轮已外部修改 `.eide/eide.yml`**（3 处：Display files、Debug excludeList、protocol 节点 + Debug incList）。
   请在 EIDE 中执行 **`EIDE: Reload Project`**，让 EIDE 读入新条目；
   **Reload 之前不要做任何会触发保存的 GUI 操作**（EIDE 会用自己的内存模型回写 yml，历史两次冲掉
   外部编辑导致 `undefined reference`）。
2. Reload 后请确认 EIDE 的编译文件列表包含 4 个 `ProtocolParser_GuiZhou_Overload/*.c`，且
   **不包含** `Device/Display/dev_display_22_1703.c`（当前 Debug 默认编 1-263）；
   若 EIDE 遗漏 GZ_OL 源文件 → 表现为"协议静默不工作"（initcall 为空，不报错）。
3. 若要切 P10 订单：在 EIDE 里把 `Device/Display/dev_display_1_263.c` 加入 Debug 排除、
   把 `dev_display_22_1703.c` 移出排除（两条互斥），保存后同样 Reload；
   或用 `make DISP=22_1703` 走 Makefile 口径验证。
4. **烧录前纪律**：任何烧录器烧完主固件后必须擦除 Sector1（见 `doc/CLAUDE.md` 调试期烧录陷阱）；
   改 IP 类命令（0x40）会写 Sector1 并复位，联调时注意。

---

## 8. 附：本轮使用的验证脚本

| 脚本 | 位置 | 用途 |
|---|---|---|
| 映射穷举 | `/tmp/map_check.c` | 源固件 vs wyh 版 vs 本驱动位流/扫描序/覆盖率全等（224×64） |
| 合并写穷举 | `/tmp/merge_check.c` | 8^8 组合下「合并写 5 次 BSRR」== 「逐通道 24 次写引脚」 |
| 协议推演 | `~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/gz_ol_frame_sim.py` | probe/parse 38 用例 + 同槽链式互斥（已入库外惯例目录） |
| 构建采样 | `/tmp/run_builds.sh` | 四口径串行构建 + 段尺寸/符号采样（含强制重链接） |
