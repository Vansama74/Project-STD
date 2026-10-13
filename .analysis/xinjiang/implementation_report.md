# 新疆费显协议接入 + 调度框架掩码扩容 — 实施报告

- 日期：2026-10-12
- 范围：`Application/{Src,Inc}/ProtocolParser_XinJiang/`（新建）、调度框架掩码 32→64、Makefile 收录、
  宿主推演脚本、文档同步
- 蓝本：`.analysis/xinjiang/pre_integration_questions.md`（调研报告；协议摘要/判据草案/文档缺口 M1~M10）
- 协议文档：`/home/yystation/Desktop/费额显示器通讯协议.md`（「费额显示器通讯协议 2024年」）
- 文档：`doc/19_新疆费显协议/README.md`
- **状态：未烧录、未提交、未改 git 配置、未改 `.eide/eide.yml`**

---

## ① 改动文件清单

### 新增（源码）

| 文件 | 行数 | 职责 |
|---|---|---|
| `Application/Inc/ProtocolParser_XinJiang/app_xj_proto.h` | 93 | 帧常量 / 命令枚举 / 解析结果类型 / 契约块（8 条）/ 对外声明 |
| `Application/Inc/ProtocolParser_XinJiang/app_xj_proto_parse.h` | 46 | 解析与 BCC 声明 |
| `Application/Inc/ProtocolParser_XinJiang/app_xj_proto_cmd.h` | 31 | 执行层 + 维护屏声明 |
| `Application/Src/ProtocolParser_XinJiang/app_xj_proto.c` | 222 | 注册五步（RS485+RS232 两 mask）+ probe + 处理任务 + 心跳节拍 |
| `Application/Src/ProtocolParser_XinJiang/app_xj_proto_parse.c` | 134 | 纯解析 + BCC（probe/parse 单一真源） |
| `Application/Src/ProtocolParser_XinJiang/app_xj_proto_cmd.c` | 338 | 清屏 / 16B 行切分渲染 / 灯控 / 语音 / 维护屏 |

### 修改（源码 / 构建）

| 文件 | 改动 |
|---|---|
| `Kernel/Inc/bit_utils.h` / `Kernel/Src/bit_utils.c` | 新增 `bit_ctz64()`（`x==0` 回 0，与 `bit_ctz` 同语义；低 32 位非零复用 32 位表，否则高位 +32） |
| `Application/Inc/app_dispatch.h` | `PROTO_MAX_COUNT 32→64`；`proto_mask_t` → `uint64_t`；`registered_mask` 随类型；`proto_index(proto_mask_t)`；新增槽位说明注释 |
| `Application/Src/app_dispatch.c` | `proto_index`/`app_proto_register`/三处遍历循环改 64 位 + `bit_ctz64`；全部位移改 `((proto_mask_t)1U << i)`；`app_channel_dispatch` 的 `seen[]` 去重表由 `PROTO_MAX_COUNT` 改 `RB_CNT_MAX`（**有意偏离字面加宽**，见 ③） |
| `Makefile` | `INC_DIRS` 增 `-I Application/Inc/ProtocolParser_XinJiang`；`SRC_APPLICATION` **末尾**追加 3 个 .c（probe 最低优先级） |

### 新增（文档 / 证据）

| 文件 | 内容 |
|---|---|
| `doc/19_新疆费显协议/README.md` | 协议摘要 §1、裁决与实现 §2（含掩码扩容 §2.1）、probe/共存 §3、内存 §4、构建收录 §5（含 EIDE 待办清单）、实测 §6、脚本 §7、待确认与联调前置 §8、变更记录 §9 |
| `.analysis/xinjiang/xj_frame_sim.py` | 宿主推演（247 用例 + 源码断言；2026-10-12 复核修正 +1 条 probe 覆盖回归断言） |
| `.analysis/xinjiang/implementation_report.md` | 本文件 |
| `.analysis/xinjiang/size_b*.txt` / `build_b*.log` | 五口径段尺寸与构建日志（`build_b0/b1/b2/b2_cq/b2_nofault.log`） |

### 修改（文档同步）

`doc/05_协议模块多协议兼容优化/01_architecture.md`（帧头事实表 / RB 绑定表 / 队列表 / 兼容矩阵 4 行 + 1 条变更记录）、
`doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md`（队列表 XJ 行 + 新增 §8.4.5 本轮复采）、
`doc/构建开关总表.md`（§1 表新增 `XJ_FAULT_SCREEN` 行 + §5 修订记录）、
`doc/CLAUDE.md`（文档地图 / CCMRAM 头与例外 5 / 队列清单两处 / sw_initcall 表 / 全景表 / 调度层掩码说明 / 新增协议小节 / 现行基线数字）。

---

## ② 构建验证与 A/B 量测

命令（各测量点独立目录全量构建，**不触碰 `build/Debug/`**，避免 EIDE 残留 elf 干扰）：

```
make -j8 BUILD_DIR=build/xj_b0                                    # B0 工作区基线
make -j8 BUILD_DIR=build/xj_b1                                    # B1 = B0 + 掩码扩容
make -j8 BUILD_DIR=build/xj_b2                                    # B2 = B1 + 新疆模块 + Makefile
make -j8 BUILD_DIR=build/xj_b2cq PROTO=CQ                         # B2-CQ
make -j8 BUILD_DIR=build/xj_b2_nofault DEFINES="-DUSE_HAL_DRIVER -DSTM32F407xx -DSTD_ALL_PROTO -DXJ_FAULT_SCREEN=0"
```

口径 = `PROTO=ALL DISP=1_263 FACTORY=STD`（除注明外）；GCC Debug（`-Og -g`）+ `arm-none-eabi-size -A`。

| 口径 | `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | `._user_heap_stack` | SRAM 合计 / 余 | CCM 余 | 告警 | elf md5 |
|---|---|---|---|---|---|---|---|---|---|---|
| **B0** 基线 | 167944 | 195776 | 1656 | 39652 | 124768 | 2560 | 128984 / **2088B** | 25884B | 3 | `3f735676e13afdf02a760ec5799b6373` |
| **B1** +掩码扩容 | 168536 | 195776 | 1656 | 39652 | 125356 | 2564 | 129576 / **1496B** | 25884B | 3 | `954899e51e051ec3b02304ac63ac0b7a` |
| **B2** +新疆模块 | 170392 | 196024 | 1656 | **41196** | 125388 | 2564 | 129608 / **1464B** | **24340B** | 3 | `cd20bfd8dc26dceb51955c06e71971b2` |
| **B2-CQ**（`PROTO=CQ`） | 157004 | 195360 | 852 | 38940 | 121900 | 2564 | 125316 / **5756B** | 26596B | 3 | `2fbdc2d40f6e9c1498d1f1fd56de4184` |
| **B2-nofault**（`XJ_FAULT_SCREEN=0`） | 170104 | 196008 | 1656 | 41196 | 125380 | 2564 | 129608 / 1464B* | 24340B | 3 | `082b50a64bfb3b6527cc6344621a414e` |

\* nofault 与 B2 仅差 text/rodata/bss；SRAM 合计按 `.bss` 计为 129600B。

**归因**：

- **掩码扩容（B1−B0）**：`.text +592` / `.bss +588` / `._user_heap_stack +4`，`.rodata/.data/.ccmram ±0`。
  `.bss` = `g_dispatch` 488 → 912B（+424：`proto_rb`/`proto_probe`/`frame_queue` 各 32→64 项 = +384，
  `registered_mask` +4，`ch_proto_map[9]` +36）+ 35 个协议模块 `s_*_mask` 4→8B（+140）+ 对齐。
  `.text` = 64 位 ctz/掩码运算（`frame_dispatch_task +110`、`app_channel_dispatch +40`、`app_proto_register +36`、
  `app_proto_bind_channel +20`、新增 `bit_ctz64 +32`、各协议 init 的 8 字节 mask 存取 +8~+44）。
  **栈影响**：`app_channel_dispatch` 栈帧 140B → **64B**（去重表按 `RB_CNT_MAX` 开表；按 64 指针开表为 268B）。
- **新疆模块（B2−B1）**：`.text +1856` / `.rodata +248` / `.ccmram +1544` / `.bss +32`，`.data ±0`。
  `.ccmram` = 队列体 246（3×82）+ `StaticQueue_t` 80 + 任务帧缓冲 82 + 任务栈 1024 + `StaticTask_t` 100 + 对齐 12；
  `.bss` = mask×2 16 + 队列句柄 8 + 心跳时刻 4 + 标志 2 + 对齐。
- **`XJ_FAULT_SCREEN=0`**：`.text −288` / `.rodata −16` / `.bss −8`（心跳函数体 + 维护屏渲染 + 文案 + 两个静态量
  随 `--gc-sections` 丢弃；处理任务改 `osWaitForever`）。
- **告警**：五口径全部 3 条既有 HAL `-Wunused-parameter`（`stm32f4xx_hal_flash_ex.c`），**零新增**。
- **md5 稳定性**：最终源码（含 `XJ_DATA_OFFSET` 版解析层）增量重编后 `build/xj_b2/Project_STD.elf`
  md5 与上表一致（`cd20bfd8…`），段尺寸逐项不变 ⇒ 上表 md5 即**最终源码**产物。

**行为零回归证据（掩码扩容）**：只加宽不改逻辑 ⇒ `mask=0` 仍为「无协议」、槽位仍按最低空闲位分配；
既有 35 处 `app_proto_register` 的 mask 值逐位不变；`mm_frame_sim.py`（**157/157**）与
`yn_ol_frame_sim.py`（**184/184**）复跑全绿（两脚本均含对 `app_dispatch.c`/Makefile 的源码断言）。

---

## ③ 与调研报告/字面要求的偏离（逐条说明）

| # | 项 | 说明 |
|---|---|---|
| 1 | `seen[]` 去重表长 | 字面「只加宽」= 按 `PROTO_MAX_COUNT`(64) 开表（栈帧 140 → **268B**，这些调用方是 1KB 栈的通道任务）。改按**缓冲池槽数** `RB_CNT_MAX`(3) 开表 + 上界防御（理论不可达）：去重语义逐字节等价（本函数写入的 RB 全部来自 `app_proto_acquire_buf`，池内最多 3 个不同指针），栈帧降到 64B。**行为零变化**，见 ② |
| 2 | 调研报告 §5 的「正签名」 | 本协议**不需要** MM 式正签名：`'D'`/未知命令字一律 FAKE（不消费字节，交链式/1 字节重同步），长度上界 ≤74 已排除治超长帧 |
| 3 | 调研报告 §1 表格的 `'V'` 内容长度 | 实施按任务书口径：内容 = `[6..len-2]`（0 基**含端点**）⇒ 71 → 64B、72 → 65B；两种整帧长都收 |
| 4 | 掩码扩容的对外行为 | `PROTO=ALL` 下新疆注册需要 2 槽（原 32 槽已满）；扩容后 `PROTO=ALL` 实际注册 34 个 mask（原 32 + 新疆 2） |

---

## ④ 脚本通过数

| 脚本 | 结果 |
|---|---|
| `.analysis/xinjiang/xj_frame_sim.py`（新建） | **247/247 通过，失败 0**（2026-10-12 复核修正：`xj_probe_frame` 第二处 `rb_peek` 读长 `XJ_LEN_OFFSET`→`XJ_LEN_OFFSET + 1U`，消除 `head[XJ_LEN_OFFSET]` 未初始化读；新增「probe 内 buf[N] 访问须被前置 peek 读长覆盖」的源码回归断言） |
| `.analysis/audit_20261008/landing_20261008/check_channel_gate.py` | **15/15**（bind 调用点 35 处，全在 `sw_app_initcall` 内） |
| `.analysis/myanmar/check_display_guards.py` | **16/16 硬断言 + 100/100 调用点判空覆盖**（新增 5 处新疆调用点全部判空） |
| `.analysis/myanmar/mm_frame_sim.py` | **157/157**（回归） |
| `.analysis/yn_ol/yn_ol_frame_sim.py` | **184/184**（回归） |
| `.analysis/comment_pass_20261008/scripts/refine_round2/check_contracts.py` | 全绿（模块表硬编码、不含新疆；新疆契约块纪律改由 `xj_frame_sim.py` 自证：头 ≤10 行 / 条数 4~8 / 每条 ≤30 字 / 无日期理由 / 引用一致） |
| `.analysis/comment_pass_20261008/scripts/refine_round2/check_doc_pointers.py` | **101/101** |
| `.analysis/comment_pass_20261008/scripts/refine_round2/long_comments.py` | 全绿（新疆 6 文件无 >100 列长行） |
| `.analysis/myanmar/check_eide_targets.py` | **14/15**（唯一失败 = `[Debug] I6 定义 PROTO_CHONGQING ⇒ CQ 模块编入`，**2026-10-08 已记录的预存口径差异、非本轮回归**，待用户在 EIDE GUI 二选一） |
| `.analysis/**/*.py` 掩码假设核查 | `grep PROTO_MAX_COUNT / registered_mask / proto_mask / bit_ctz` = **0 命中**（无脚本硬编码 32 槽假设，无需同步） |

---

## ⑤ 文档更新清单

| 文档 | 更新内容 |
|---|---|
| `doc/19_新疆费显协议/README.md` | **新建**（§1 协议摘要 / §2 七项裁决与实现（含 §2.1 掩码扩容）/ §3 probe 与共存 / §4 内存 / §5 构建收录（含 EIDE 待办）/ §6 实测 / §7 脚本 / §8 待确认与联调 / §9 变更记录） |
| `doc/05_协议模块多协议兼容优化/01_architecture.md` | §3.3 帧头事实 +1 行；§4 绑定表 RS485/RS232 两行加 XJ；§4.1 队列表 +1 行；§6 兼容矩阵 +4 行；变更记录 +1 条（含掩码扩容） |
| `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` | 协议帧 queue 表 +1 行（XJ）；**新增 §8.4.5**（B0/B1/B2/B2-CQ/B2-nofault 段尺寸 + 归因） |
| `doc/构建开关总表.md` | §1 编译期宏开关表 +`XJ_FAULT_SCREEN` 行（默认 1 / 覆盖方式 / 段尺寸影响）；§5 修订记录 +1 条 |
| `doc/CLAUDE.md` | 文档地图 +`doc/19` 行；协议全景表 +`app_xj_proto` 行；sw_initcall 表 +`xj_proto_init` 行；两处队列深度清单加 XJ；CCMRAM 头改本轮实测（41196B）+ 例外 5；调度层新增「掩码位宽 64」段；绑定调用点数 33 → 35；新增「新疆费额显示器协议」协议小节；现行基线数字（SRAM 余 1464B / CCM 余 24340B / CQ 余 5756B） |

---

## ⑥ EIDE 侧需用户执行的收录清单

> **〔2026-10-12 更新：§⑥ 已由「EIDE 收录轮」外部执行完毕，见 §⑨；此处保留原始清单作对照〕**
> 原记录：**未改 `.eide/eide.yml`**（EIDE 地盘，外部改会被回写冲掉）。

1. **`files` 节点**新增（`Application/Src/ProtocolParser_XinJiang/` 虚拟目录）：
   `app_xj_proto.c`、`app_xj_proto_parse.c`、`app_xj_proto_cmd.c`
2. **各目标 `incList`** 增条目：`Application/Inc/ProtocolParser_XinJiang`（EIDE 条目不带 `-I`）
3. **目标选择**：是否编入新疆模块由用户裁决（`excludeList` 表达）；新疆与既有协议**可同编共存**（免互斥）
4. **收尾**：`EIDE: Reload Project`
5. **前置检查**：目标内至少编入一个为 RS485/RS232 提供 RB 体的协议（本模块**只 acquire 不 provide**）；
   若目标「仅新疆」，需在本模块加一行 `RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232)`

---

## ⑦ 待确认 / 遗留项

| # | 项 | 现状 / 建议 |
|---|---|---|
| 1 | `'D'`(0x44) 与「彩色行显示协议」 | 文档缺章（M1/M2/M3）→ 未实现、不在白名单；**若现场在用，probe 白名单与实现范围都要改**（联调前问清） |
| 2 | `'T'` 内容区「N×16 变长」vs「固定 64 字节」 | 按 N×16 实现；固定区写法（len=74）能通过 probe、parse 会拒（M4） |
| 3 | `'V'` 71/72 双长度、`'S'` 编号 1/2 字节、0x36 | 已按「两种都收 / 1 字节 / 未知忽略」实现（M5/M6/M7） |
| 4 | 维语 / 汉维双语 / 男女生音色 | **硬件级缺口**（语音板仅自由文本 TTS + 音量）；若现场必须出维语需升级语音板 |
| 5 | 音量映射 0x30~0x37 → 板载 1~9 | **须现场校准**（语音板音量非线性；沿用贵州 {1,3,5,7,9} 区间两端） |
| 6 | 心跳阈值 15s（文档 5s）、文案「系统维护中」 | 按 15s 实现（`XJ_FAULT_TIMEOUT_MS` 可改），文案固定；待现场确认 |
| 7 | 联调前置 | DIP1 波特率、接线口（RS232/RS485）、上位机发帧样本（≥T/O/C 各一帧含 BCC） |
| 8 | build 目录 | `build/xj_*` 抽样目录（15 个，各 ~10~12MB，`.gitignore` 覆盖）**已于 2026-10-12 工作区清理轮删除**（`rm -rf build/xj_*`，回收 ~161MB）；结论证据 = 本目录 `build_*.log` / `size_*.txt`，按各行 `make … BUILD_DIR=…` 重跑即可重建；清理记录 `.analysis/cleanup_20261012_workspace/README.md` |
| 9 | 文档地图缺口 | `doc/CLAUDE.md` 的文档地图本就缺 `doc/17` / `doc/18` 两行（本轮只补 `doc/19`，未越界改动） |

---

## ⑧ 声明

> **〔首版轮声明；2026-10-12 随后的「EIDE 收录轮」按要求改动了 `.eide/eide.yml`，见 §⑨〕**

**未烧录、未提交 git、未改 git 配置、未改 `.eide/eide.yml`**；未新增 CMake 等构建入口；未引入第三方依赖；
业务代码零 RTT/printf 调用；未触碰与任务无关的在飞改动文件（`.eide/eide.yml`、`doc/14/15/16/18`、
`ProtocolParser_{GuiZhou_Overload,Myanmar,YunNan_Overload}`、`app_factory_test_mm.c`、`dev_display_1_26*.c`
等仍是会话开始时的既有修改状态）。

---

## ⑨ EIDE 收录轮（2026-10-12，外部编辑 `.eide/eide.yml`）

**背景**：用户裁决「把新疆模块收录进 EIDE 并编入 `targets.Debug`」——故本轮与首版不同，**直接外部编辑 yml**
（最小外科式插行；备份 `.analysis/xinjiang/eide_20261012/eide.yml.before_xj`，md5 `fe52ce12d30b60a4f090a0bc081e4fa2`）。

| # | 动作 | 结果 |
|---|---|---|
| 1 | `files` | 新增 `Application/protocol/ProtocolParser_XinJiang` 虚拟文件夹（`app_xj_proto.c` / `_parse.c` / `_cmd.c`，**插在 `rls` 之后**）；候选池 249 → 252 |
| 2 | `targets.Debug.defineList` | 追加 `XJ_FAULT_SCREEN=0`（共存构建关闭 15s 心跳维护屏，防盖掉其它调试画面） |
| 3 | `targets.Debug` 编入集 | 不排除（`excludeList` 无新疆条目）⇒ 编入 **206 → 209** 文件；`incList` 增 `Application/Inc/ProtocolParser_XinJiang` |
| 4 | `targets.Myanmar` | `excludeList` 增 `<virtual_root>/Application/protocol/ProtocolParser_XinJiang`（编入仍 **207**、排除 42 → 45）；`incList` 兜底增同路径 |
| 5 | `targets.Release` | **不动**（incList 仍含已删除的 `Drivers/BSP/*` / `Drivers/RTT/*` / `LWIP/App` = 历史残留、非可用入口） |
| 6 | 编辑量 | 相对备份 **+10 行 / −0 行**（会话前已有的在飞改动未触碰） |

**验证**（全部只读 / 独立产物；未运行 EIDE）：

- YAML 解析通过（`yaml.safe_load`）；`git diff` 与「备份对照」两个视角都能清楚分离本轮增量与在飞改动。
- `python3 .analysis/myanmar/check_eide_targets.py`：**改前 14/15 → 改后 14/15**（唯一失败仍是 Debug 的
  `PROTO_CHONGQING` ↔ CQ 模块错配 I6，非本轮引入）；新疆相关结构不变量（显示驱动 ≤1 / 厂测恰好 1 / `{` 帧族 ≤1 /
  I7 -I 覆盖）全绿。产出 `check_before.txt` / `check_after.txt` / `check_after_verbose.txt`。
- **等价口径三口径真编译**（各自独立 `BUILD_DIR`，全量 `-B -j12`，逻辑 3 条 HAL 告警不变）：

| 口径（Debug 等价） | `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | `._user_heap_stack` | .o 数 |
|---|---|---|---|---|---|---|---|
| 改前（无新疆） | 133544 | 192232 | 1632 | 17260 | 116420 | 2564 | 206 |
| 改后（新疆 + `XJ_FAULT_SCREEN=0`） | 135160 | 192480 | 1632 | 18804 | 116452 | 2564 | **209** |
| 对照（新疆 + 开关默认 1） | 135448 | 192496 | 1632 | 18804 | 116460 | 2564 | 209 |
| **Δ 改后−改前** | **+1616** | **+248** | ±0 | **+1544** | **+32** | ±0 | +3 |
| **Δ 开关关闭效果** | **−288** | **−16** | ±0 | ±0 | **−8** | ±0 | — |

  - `.ccmram +1544` 与 §6 全协议口径的模块增量**逐字节一致**；`XJ_FAULT_SCREEN=0` 的 −288/−16/−8 亦与
    doc/19 §6 记录一致。`.text +1616` = 模块自有 1856（`.o` 三段合计实测） − 288（开关） **+ 44**
    （链接期新保留的 `app_scroll_render_lock` 24B + `app_scroll_render_unlock` 20B，由本模块的渲染互斥调用牵引）
    + 4B 对齐 —— 归因经 `nm` 符号集差分核实（新符号 44 个，含 XJ 全部符号 + 上述两个 + `s_render_mutex`）。
  - **`XJ_FAULT_SCREEN=0` 生效判据**：链接后 `xj_fault_screen_show` / 「系统维护中」= **0 / 0**；
    同一 `.o` = **1 / 1**（证明是链接期 `--gc-sections` 丢弃）；正对照 `build/xj_b2/Project_STD.elf` = 1 / 1。
  - **新疆符号入镜像**：`xj_proto_init` / `xj_probe_frame` / `xj_parse_frame` / `xj_execute_cmd` / `xj_bcc_*` /
    `s_xj_queue_*` / `s_task_xj_handle_*`（后四者在 `0x1000_xxxx` = CCMRAM ✓）；SRAM mask 两个 8B。
  - **Myanmar 不受影响**：等价 Makefile 改前/改后**逐字节相同**、`SRC_APPLICATION` 37 个（不含新疆）、
    编入 207 文件（`check_after_verbose.txt` 中 Myanmar 编入段 0 处 XinJiang、排除段 3 处）。
- 方法学注记：脚本产出的是「源文件集等价」Makefile（沿用本仓 Makefile 的 Debug 旗标 `-Og -g`），与 EIDE 真实
  旗标（`optimization: level-3`）略有差异 ⇒ 绝对尺寸仅供同口径对照，Δ 为同旗标真 A/B。
- 证据目录：`.analysis/xinjiang/eide_20261012/`（备份、`apply_yaml_edits.py`、三份 `.mk`、
  三份 build log、三份 size 记录、check_before/after）。

**本轮声明**：**未运行 EIDE、未触发任何 EIDE 构建、未 git commit、未改 git 配置、未改源码、未动其它既有文件**
（`doc/19 §5.2/§9` 与本报告除外）；待用户执行 `EIDE: Reload Project`。
**遗留（超出本轮授权，待办）**：`doc/CLAUDE.md` 与 `doc/构建开关总表.md` 中「EIDE 待用户在 GUI 收录（files 三文件 +
各目标 incList + Reload）」的表述现已过时，需下一轮改为「已外部收录（Debug 编入 + `XJ_FAULT_SCREEN=0` + Myanmar 排除）」。
