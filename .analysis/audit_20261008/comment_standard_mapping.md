# 注释标准定位与改写口径取证（Application / Device）

- **日期**：2026-10-08
- **范围**：`/home/yystation/Program/3833024/Project-STD-main`（工作区 + `origin`/`upstream` 远端 git 对象 + 家目录检索）
- **性质**：**只读**。本轮未修改任何源码/文档/配置；未 `git add/commit/config`；未烧录。统计脚本与中间产物全部落 `/tmp/audit_cls/`。
- **本报告位置**：`.analysis/audit_20261008/comment_standard_mapping.md`（本轮唯一新增文件）
- **上一轮基础**：`.analysis/audit_20261008/comments_trim_feasibility.md`（注释精简可行性评估）、`.analysis/audit_20261008/architecture_conflicts.md`（架构冲突审计）

**一句话结论**：仓库里**不存在**名为 `why` 的分支或注释规范文件；唯一近似物是 **`wyh`**（同一个人 `dzzwmqj@qq.com` 的提交身份 + `origin/wyh`/`upstream/wyh` 分支 + 已删除的桌面工作区 `~/Desktop/Project-STD-wyh`）。`wyh` 分支的注释风格是 **Doxygen `@brief` + `@param`/`@return`/`@retval` 的函数注释**，且**该分支连 `doc/` 目录都没有**——因此「复杂说明指向 doc」这一条**无法从 wyh 分支取证**，只存在于**本地现行代码**（106 行 `doc/` 指针注释 / 92 处 `§`）。→ 需用户澄清「why」的确切出处（§A.5 / §D 问题 1）。

---

## 0. 结论摘要（速读表）

| 问题 | 本轮结论 |
|---|---|
| **A. 「why 的注释标准」在哪？** | **未找到明确出处**。仓库无 `why` 分支；`wyh` 分支（`origin/wyh` = Vansama74 fork、`upstream/wyh` = phtty 上游）是唯一近似物，且只有「**既有做法**」没有「**成文标准**」；所有远端分支中无 `注释*`/`规范*`/`comment*` 命名的文件（唯一含「注释」二字的文档是 phtty `upstream/refactor/multi-variant:docs/命名约定.md`，属另一条重构线）。**需要用户给出出处或确认按 §C 模板执行**。 |
| **A. wyh 分支的范本长什么样？** | ① 文件头 `@file`/`@brief` + 职责；② 函数用 `@brief` + 空行 + 可选「why 段」+ `@param`/`@return`（`@retval` 用于错误码）；③ 简单函数用一行横幅 `/* ---- 名: 说明 ---- */`。**无 doc 指针（该分支无 doc/）**，语言中文。 |
| **B. 存量** | Application + Device：**172 文件 / 27070 行 / 注释 8511 行（31.4%）**；其中 `.c` 90 文件 6004 行、`.h` 82 文件 2507 行。Application 7275 行（31.7%）/ Device 1236 行（29.8%）。 |
| **B. 六类分布** | 主分类（互斥）：历史档案 **1749 行**、函数注释 **1321 行**（另有 2170 行紧邻函数签名）、契约 **1132 行**、文件头 **659 行**、宏/结构/表 **166 行**、doc 指针 **174 行**、其它/行内 **3310 行**；标签重叠（一块属多类）：契约 2067 行、日期 1421 行、doc 指针 1196 行。 |
| **C. 模板草案** | 函数注释 = `@brief`（一句话，动词开头）+ `@param`（语义一句话，不重复类型）+ `@return`/`@retval` + 最多一条 `@note`（非显而易见的 why）+ `详见 doc/xx §y`；无参/无返回/平凡函数允许一行式 `/** @brief ... */`。文件头 ≤10 行（现有中位数 8 行，只有 14 个文件 ≥20 行）。 |
| **C. doc 映射** | 15 个模块已有专文档（doc/01~doc/16），**四川 ETC/MTC/治超无专文档**（只在 `doc/CLAUDE.md`）、**Device/Network（DP83848/ETH）与 Device/Storage 无专文档**——这三处「复杂说明指向 doc」当前**无落点**。 |
| **D. 最关键待裁决** | ① why 出处；② 模板是否采用（含 Doxygen 标签取舍）；③ .h/文件头/宏/无参函数的处理口径；④ 是否先提交/冻结 53 个 M + 8 个未跟踪项再开工；⑤ (d)(e) 类「先迁移 doc 再精简 vs 直接精简 vs 原样保留」；⑥ 是否新建注释规范文档（doc/18 或并入 doc/CLAUDE.md）；⑦ 与架构冲突（C1 几何 / C2·C3 EIDE 口径）的先后顺序。 |

---

# A. 「why 的注释标准」定位

## A.1 检索过程（命令 → 结果，全部只读）

| # | 检索动作 | 结果 |
|---|---|---|
| 1 | `git remote -v` | `origin` = `Vansama74/Project-STD.git`，`upstream` = `phtty/Project-STD.git` |
| 2 | `git branch -r`（本地仓库，含两个远端） | origin 侧 9 个、upstream 侧 14 个分支；**无 `why`**；有 **`origin/wyh`** 与 **`upstream/wyh`** |
| 3 | `git branch -r \| grep -iE "why\|wyh\|comment\|doc\|style\|refactor"` | 只命中 `wyh` + 三个 `refactor/*` |
| 4 | 三个仓库（主固件 / Bootloader / Recovery）的 `git branch -a` | 三个仓库**都有 `origin/wyh`**；作者 `wyh <dzzwmqj@qq.com>` |
| 5 | 全部分支 `git ls-tree -r --name-only \| grep -iE "注释\|规范\|标准\|风格\|style\|comment\|convention"` | **0 个命中**（唯一含「注释」的文件是 `upstream/refactor/multi-variant:docs/命名约定.md`，属 2026-09 的另一条注释规范化线，见 §A.4 注） |
| 6 | 仓库内 grep `\bwhy\b`（`.md/.c/.h/.yml/.json`，排除 build/Drivers/Middlewares） | **无「why 标准/why 分支」**；仅三处把 `why` 当英文词用：`.analysis/cleanup_20260929/report.md:97`「非显而易见的原因（`why`）与陷阱」、`round2_report.md:128`「注释基本是契约/why」；其余命中是归档探针代码里一个名为 `why` 的出参 |
| 7 | 仓库内 grep `wyh` | 命中 3 类：`Device/Display/dev_display_22_1703.c:14-15` 与 `doc/01` §2.1/§4 的「**以 wyh 分支实机驱动为准**」；`.analysis/9k23881580/*` 的 wyh 版公式比对；`doc/CLAUDE.md:802`「wyh 分支实机版同口径」 |
| 8 | 家目录检索：`find ~ -maxdepth 4 -iname "*why*" -o -iname "*wyh*"` | 命中 `~/.cursor/projects/home-yystation-Desktop-Project-STD-wyh/`（Cursor 的工程元数据目录，含 `canvases/ mcps/ terminals/`，创建于 2026-09-14）；**其源码目录 `~/Desktop/Project-STD-wyh/` 已不存在**（`ls` 空） |
| 9 | 全局规则检索：`~/.cursor/rules/*.mdc`、`~/.claude/CLAUDE.md` | 只有 `code-doc-sync.mdc`、`git-proxy.mdc`、`multitask-blocking-retrieval.mdc` 三条与流程相关，**无注释规范** |
| 10 | 工作区 `.cursor/rules/` | **空目录** |

## A.2 唯一近似物 = `wyh`（三条独立证据）

1. **提交身份**：`git log --all --format='%an <%ae>' | sort | uniq -c` →
   `289 phtty <dzzwmqj@outlook.com>` / **`52 wyh <dzzwmqj@qq.com>`** / `48 Vansama74` / `15 JieLiu` / `4 AIyu3` / `2 马前进`。
   `wyh` 与 `phtty` 是**同一自然人的两个 git 身份**（同一邮箱主体 `dzzwmqj`）。
2. **分支**：`origin/wyh`（Vansama74 fork，tip `885ed85`，2026-07-28「chore: 暂时改变启动方式以方便测试模组扫描」）与 `upstream/wyh`（phtty 上游，tip `d8c5b06`，2026-09-17）；三个仓库都有 `wyh` 分支。
3. **工作区**：`doc/01_显示系统/22-1703模组驱动分析与迁移记录.md:21` 明确写
   「**参照实现**：`/home/yystation/Desktop/Project-STD-wyh/Device/Display/dev_P10_32x16_2200001703.c`（另一 STD 分支在用…）」；
   `Device/Display/dev_display_22_1703.c:14-15` 同口径留痕。→ **用户口中的「why」大概率是 `wyh` 的口误/拼写变体**，指同一人。

## A.3 wyh 分支的注释实况（范本实例原文）

**（1）文件头：`@file` + `@brief` + 职责一句** —— `origin/wyh:Application/Inc/app_dispatch.h:1-10`

```c
/**
 * @file    app_dispatch.h
 * @brief   多协议多通道调度框架（Application 层核心）
 *
 * channel_t 使用虚表模式（ch_ops_t）实现 OCP：新增通道类型无需修改此文件。
 * 通道-协议映射由协议模块通过 app_proto_bind_channel 声明。
 * 协议模块通过 sw_app_initcall 自注册，框架零改动。
 */
```
（`upstream/wyh` 同一文件把这段扩到 22 行，多了「基类约定 1/2」「为什么首个成员是 name 而不是 ops」等 why 段——**同源风格的两代形态**。）

**（2）函数注释（完整 Doxygen + why 段）** —— `origin/wyh:Application/Src/app_dispatch.c:61-68`

```c
/**
 * @brief 协议掩码 → 数组索引
 *
 * 通过 bit_ctz 计算尾零位数，将 bitmask 映射为 0~31 的索引。
 *
 * @param mask  协议掩码（必须是 2 的幂或 0）
 * @return      对应的数组索引，mask=0 返回 0xFF
 */
uint8_t proto_index(uint32_t mask)
```

**（3）函数注释（极简形：`@brief` + 逐参数一行）** —— `origin/wyh:Application/Src/AH_MQTT/ah_mqtt_cmd.c:23-29`

```c
/**
 * @brief 测试用命令
 *
 * @param ch 通道实例
 * @param buff 命令参数
 */
static void cmd_default(channel_t *ch, char *buff)
```

**（4）Device 层：带 `@retval` 错误码清单** —— `origin/wyh:Device/Network/dev_dp83848.c:5-11`

```c
/**
 * @brief  向 PHY 设备对象注册 IO 操作函数
 * @param  obj    DP83848 设备对象
 * @param  io_ctx IO 操作函数集（读/写寄存器、获取时间戳等）
 * @retval DP83848_STATUS_OK    注册成功
 * @retval DP83848_STATUS_ERROR 缺失必要的 IO 函数
 */
int32_t dev_dp83848_register_bus_io(dev_dp83848_obj_t *obj, dev_dp83848_io_ctx_t *io_ctx)
```

**（5）另一形态：单行横幅（无参数文档）** —— `origin/wyh:Application/Src/app_boot.c:28-29`

```c
/* ---- HalfSecTask: 500ms 喂狗 / 60s RTC 备份 / LED 翻转 ---- */
static void half_sec_task(void *argument)
```

**格式量化（`git grep -c` 汇总，Application+Device）**

| 分支 | `.c` `@param` | `.c` `@brief` | `.c` `@return` | `.h` `@param` |
|---|---|---|---|---|
| `origin/wyh`（2026-07-28，分支树） | 55 | 87 | 7 | **6** |
| `upstream/wyh`（2026-09-17，分支树） | 50 | 92 | 3 | 33 |
| 本地工作区（现行，含在飞改动） | **294** | **415** | **55** | **108** |

（口径：`git grep -c <tag>` 按文件计数后求和 = 出现次数；本地行取**工作区**而非 `HEAD` 提交——工作区有 53 个 M 文件，与提交态（283/415/55/101）略有差异。）

**规则归纳（从实例反推，非成文）**：中文正文 + 英文 Doxygen 标签；`@brief` 一句话；`@param` 只写语义/取值域不重复类型；错误码用 `@retval` 逐条列；why 段放在 `@brief` 与 `@param` 之间的空行段；简单函数可退化为一行横幅。

## A.4 与用户口述标准的对照

| 用户口述标准 | wyh 分支是否有 | 本地现行代码是否有 | 备注 |
|---|---|---|---|
| （.c 函数）参数简介 | ✅ 有（`@param`） | ✅ 有（工作区 `.c` 294 处 / `.h` 108 处） | 一致 |
| （.c 函数）功能极简介绍 | ✅ 有（`@brief` 一句话） | ✅ 有（工作区 `.c` 415 处 / `.h` 298 处） | 一致 |
| 复杂说明指向 doc 且在 doc 详细说明 | ❌ **wyh 分支无 `doc/`**（`git ls-tree -r --name-only origin/wyh \| grep -c '^doc/'` = **0**） | ✅ 已有雏形：106 行含 `doc/` 指针、92 处 `§` | **这一条无法从 wyh 取证**——只能按用户口述 + 本地既有做法执行 |
| 文件头长度规则 | ⚠️ 无明文；实测普遍 6~12 行 | 中位数 8 行、均值 10.5 行、3 个文件 ≥40 行 | 需用户定阈值 |
| 是否 Doxygen 标签 | ✅ 是 | ✅ 是（本地更密） | 一致 |

> 注：`upstream/refactor/multi-variant` 分支另有一条**注释规范化**线（2026-09-23 前后：`d542f65` 新增 `Doxyfile` 与《注释与 Doxygen 改造方案》、`bb38080` 补 `@file/@brief`、`19bd71a` 补 `@brief/@return/@param`；并落成 `docs/命名约定.md` §12「Doxygen 最小必需集」+ §13「禁改清单」）。**它是 phtty 的路线、不是 wyh 的**，但其方案文档的「共性约束（只补标签、只订正事实，不重写设计性论述）」与「明确不要动（历史归因、真 TODO、回归案例）」两条可直接借鉴。

## A.5 三种可能解释（需用户裁决）

- **(i) 通用原则**：用户指的是「注释解释 **why**（为什么这样做）而非 what（代码在做什么）」这一通用原则；括号里的三条就是本次改写的操作化定义。→ 若如此，本报告 §C 的模板即按此设计，可直接进入实施。
- **(ii) 指 `wyh` 分支的既有风格**（拼写变体）：即 §A.3 归纳的 Doxygen 极简风格 + 用户补充「复杂说明指向 doc」。→ 若如此，范本已取到（§A.3 五例），可直接执行。
- **(iii) 指某个 git 之外的出处**（如 wyh 本人在别的仓库/聊天中给出的规范）：**本地与两个远端均无此文件**，需用户提供路径/链接/原文。

---

# B. Application / Device 注释存量分类

## B.0 方法与口径

- **统计工具**：`/tmp/audit_cls/classify2.py`（逐行注释扫描器与 `.analysis/audit_20261008/scripts/comment_stats.py` **同源**，保证与上一轮数字可比）+ `/tmp/audit_cls/func_cov2.py`（函数定义与注释覆盖）。
- **校验**：本轮合计 **8511 行**，与上一轮报告 §2.1 的 `Application 7275 + Device 1236 = 8511` **逐字一致**（互校通过）。
- **分类口径**：先按「注释行 → 连续块」聚合，再给每块打标签（日期/裁决/现场、契约、doc 指针、文件头、函数前、宏/结构/表、行内），最后**按优先级取唯一主分类**：`行内 > 历史 > 契约 > doc 指针 > 文件头 > 函数 > 宏/结构/表 > 其它`。
- **已知偏差（重要）**：
  1. 优先级天然吃掉重叠——例如「52 行的 `app_yn_ol_proto.h` 文件头同时含日期/裁决/契约/doc 指针」，它在主分类里计入**历史**，但在标签统计里同时计入 fileheader/contract/docptr。**两张表都要看**（§B.3 双表）。
  2. `.h` 里的**函数声明注释**（后跟 `);`）不被「函数前」判据捕获（判据要求下一行是 `{`），落在「其它/文件头/契约」类。`.h` 的实际函数声明注释量被低估。
  3. 主分类「其它」含大量**单行小节横幅**（`/* ---- xxx ---- */`、`/* ==== */`）与**行尾注释**，不是废话。

## B.1 总量（`.c` / `.h` × 目录）

| 范围 | 文件 | 总行 | 注释行 | 注释/总行 | 文件头块合计 |
|---|---|---|---|---|---|
| **Application `.c`** | 72 | 18779 | **4975** | 26.5% | 768 |
| **Application `.h`** | 70 | 4141 | **2300** | 55.5% | 553 |
| **Device `.c`** | 18 | 3473 | **1029** | 29.6% | 160 |
| **Device `.h`** | 12 | 677 | **207** | 30.6% | 67 |
| **小计** | **172** | **27070** | **8511** | **31.4%** | 1548 |

> 对照：全项目 254 文件 / 46299 行 / 注释 11583 行（27.8%）——**本次范围占全项目注释的 73%**（8511/11583），是全项目注释的绝对主体。

**函数维度（仅 `.c`）**：`Application + Device` 的 `.c` 共 **662 个函数定义**，**457 个（69.0%）**紧邻上方有注释，**205 个（31.0%）完全无注释**。函数注释块共 **448 个**，其中含 `@brief` 278 个（62%）、含 `@param` 169 个、**无任何 Doxygen 标签（纯文本/横幅）169 个（38%）**。

「未注释函数最多」Top 10（改造工作量热点）：

| 文件 | 函数数 | 有注释 | **无注释** |
|---|---|---|---|
| `Device/Storage/dev_w25qxx.c` | 23 | 8 | **15** |
| `Application/Src/Channel/app_udp.c` | 27 | 13 | **14** |
| `Application/Src/ProtocolParser_ChongQing/app_cq_proto_cmd.c` | 28 | 19 | **9** |
| `Application/Src/app_boot.c` | 11 | 3 | **8** |
| `Application/Src/LDI/app_ldi_device.c` | 9 | 2 | **7** |
| `Application/Src/Channel/app_mqtt.c` | 12 | 5 | **7** |
| `Device/Display/dev_display.c` | 14 | 7 | **7** |
| `Application/Src/app_render.c` | 21 | 15 | 6 |
| `Application/Src/RLS/app_rls_cmd.c` | 10 | 4 | 6 |
| `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c` | 27 | 21 | 6 |

**Doxygen 标签存量（Application+Device 全量，出现次数）**：`@brief` **713**（`.c` 415 / `.h` 298）、`@param` **402**（`.c` 294 / `.h` 108）、`@note` 130、`@return` 90、`@retval` 48、`@file` **148**（**148/172 文件已有 `@file` 头，覆盖率 86%**）、`@warning` 10、`@see` 2、`@todo` 0。

## B.2 Top 20（`.c` / `.h` 分开）

**Top 20 `.c`**

| # | 文件 | 注释 | 总行 | 占比 | 头块 |
|---|---|---|---|---|---|
| 1 | `Application/Src/LDI/app_ldi_cmd.c` | 427 | 1174 | 36.4% | — |
| 2 | `Application/Src/app_dispatch.c` | 229 | 604 | 37.9% | 9 |
| 3 | `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c` | 202 | 683 | 29.6% | 22 |
| 4 | `Application/Src/app_render.c` | 182 | 833 | 21.8% | 17 |
| 5 | `Application/Src/ProtocolParser_YunNan/app_yn_proto_cmd.c` | 161 | 524 | 30.7% | 19 |
| 6 | `Device/Network/dev_dp83848.c` | 156 | 459 | 34.0% | — |
| 7 | `Application/Src/LDI/app_ldi.c` | 145 | 574 | 25.3% | — |
| 8 | `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c` | 145 | 384 | 37.8% | 16 |
| 9 | `Application/Src/ProtocolParser_GuiZhou/app_gz_proto_cmd.c` | 141 | 534 | 26.4% | 11 |
| 10 | `Device/Display/dev_display_22_1665.c` | 135 | 372 | 36.3% | 20 |
| 11 | `Application/Src/Channel/app_udp.c` | 130 | 668 | 19.5% | 14 |
| 12 | `Application/Src/ProtocolParser_ChongQing/app_cq_proto.c` | 123 | 359 | 34.3% | 23 |
| 13 | `Application/Src/app_scroll.c` | 122 | 516 | 23.6% | **35** |
| 14 | `Device/Display/dev_display_22_1703.c` | 119 | 326 | 36.5% | 27 |
| 15 | `Application/Src/ProtocolParser_Anhui/app_anhui_proto_cmd.c` | 118 | 298 | 39.6% | 8 |
| 16 | `Application/Src/ProtocolParser_ChongQing/app_cq_proto_cmd.c` | 111 | 581 | 19.1% | 14 |
| 17 | `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c` | 110 | 253 | 43.5% | **52** |
| 18 | `Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto_cmd.c` | 110 | 450 | 24.4% | 9 |
| 19 | `Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto.c` | 100 | 300 | 33.3% | 10 |
| 20 | `Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c` | 100 | 217 | **46.1%** | 18 |

**Top 20 `.h`**

| # | 文件 | 注释 | 总行 | 占比 | 头块 |
|---|---|---|---|---|---|
| 1 | `Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h` | 206 | 285 | 72.3% | **52** |
| 2 | `Application/Inc/LDI/app_ldi_cmd.h` | 201 | 287 | 70.0% | — |
| 3 | `Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h` | 129 | 171 | 75.4% | 29 |
| 4 | `Application/Inc/app_render.h` | 121 | 218 | 55.5% | 12 |
| 5 | `Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h` | 120 | 180 | 66.7% | **50** |
| 6 | `Application/Inc/LDI/app_ldi.h` | 106 | 176 | 60.2% | — |
| 7 | `Application/Inc/ProtocolParser_Anhui/app_anhui_proto.h` | 97 | 171 | 56.7% | 27 |
| 8 | `Application/Inc/ProtocolParser_YunNan/app_yn_proto.h` | 95 | 150 | 63.3% | 18 |
| 9 | `Application/Inc/ProtocolParser_GuiZhou/app_gz_proto.h` | 90 | 145 | 62.1% | 17 |
| 10 | `Device/Inc/dev_display.h` | 89 | 167 | 53.3% | 7 |
| 11 | `Application/Inc/Config/app_board_net_cfg.h` | 86 | 137 | 62.8% | 16 |
| 12 | `Application/Inc/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto.h` | 84 | 146 | 57.5% | 12 |
| 13 | `Application/Inc/ProtocolParser_ShanDong/app_sd_proto.h` | 77 | 124 | 62.1% | 15 |
| 14 | `Application/Inc/ProtocolParser_SiChuang_ETC/app_sc_etc_proto.h` | 75 | 121 | 62.0% | 11 |
| 15 | `Application/Inc/ProtocolParser_SiChuang_Overload/app_sc_ol_proto.h` | 68 | 124 | 54.8% | 11 |
| 16 | `Application/Inc/app_scroll.h` | 67 | 96 | 69.8% | 19 |
| 17 | `Application/Inc/ProtocolParser_QingHai/app_qh_proto.h` | 57 | 139 | 41.0% | 6 |
| 18 | `Application/Inc/ProtocolParser_ChongQing/app_cq_proto.h` | 56 | 110 | 50.9% | 11 |
| 19 | `Application/Inc/app_dispatch.h` | 46 | 152 | 30.3% | 11 |
| 20 | `Application/Inc/ProtocolParser_ChongQing/app_cq_proto_parse.h` | 37 | 60 | 61.7% | 4 |

## B.3 六类分类

### 表 1：主分类（互斥，行数 / 块数 / 覆盖文件数）

| 类 | 行数 | 块数 | 文件数 | 占 8511 行 |
|---|---|---|---|---|
| (d) 决策历史（日期/裁决/现场） | **1749** | 195 | 52 | 20.5% |
| (b) 函数注释（紧邻函数定义） | **1321** | 337 | 113 | 15.5% |
| (e) 契约/安全约束 | **1132** | 222 | 70 | 13.3% |
| (a) 文件头叙述 | **659** | 99 | 99 | 7.7% |
| (f) 指向 doc 的指针 | **174** | 29 | 21 | 2.0% |
| (c) 宏/结构体/查表 | **166** | 50 | 14 | 2.0% |
| (h) 行内/行尾短注释 | **391** | 391 | 8 | 4.6% |
| (g) 其它（小节横幅、声明注释等） | **2919** | 1233 | 128 | 34.3% |

### 表 2：标签重叠（一块可属多类；更能反映「内容性质」的分布）

| 标签 | 行数 | 块数 | 说明 |
|---|---|---|---|
| 含 Doxygen 标签 | 4376 | 715 | 覆盖一半以上注释 |
| **紧邻函数签名** | 2170 | 448 | 函数注释的真实规模（比主分类 1321 行大，差额被历史/契约优先级吃掉） |
| **含契约词** | **2067** | **310** | 契约/约束/不变量/禁止/必须/守卫/断言/越界/极性/字节序/BE16/掩码/互斥/纪律 |
| 位于首代码行之前（文件头） | 1548 | 148 | 99 个文件有头块（含多个） |
| **含日期** | **1421** | **140** | `20xx-xx-xx` |
| 含 doc 指针 | **1196** | **99** | `doc/xx`、`§y`、`详见 doc` |
| 含裁决词 | 970 | 88 | 裁决/定案/拍板/用户决定 |
| 含现场/实测 | 772 | 72 | 现场/实测/实机/上机/联调 |
| 含查表/映射词 | 497 | 62 | 查表/映射表/行表/矩阵 |
| 含 struct/typedef | 83 | 21 | — |

> 交叉读出：**「文件头」几乎总与「历史/契约/doc 指针」同现**——148 个头块里，含日期的 23 个、含裁决的 15 个、含现场的 17 个、含契约词的 34 个、含 doc 指针的 32 个，另有 6 个同时含「契约+日期+裁决+doc 指针+现场」（合计 225 行，即 §B.4 里首批该外移的对象）。

### (a) 文件头长叙述

- 规模：99 个文件有头块（合计 1548 行）；**中位数 8 行 / 均值 10.5 行**；**≥20 行者 14 个文件、≥40 行者 3 个**。
- 长尾清单（≥20 行）：`app_yn_ol_proto.h`(52)、`app_yn_ol_proto.c`(52)、`app_gz_ol_proto.h`(50)、`app_scroll.c`(35)、`app_mm_proto.h`(29)、`app_net_boot.c`(29)、`app_gz_ol_proto.c`(29)、`app_anhui_proto.h`(27)、`dev_display_22_1703.c`(27)、`app_gz_ol_proto_parse.c`(24)、`app_cq_proto.c`(23)、`app_yn_ol_proto_cmd.c`(22)、`app_yn_ol_proto_parse.c`(21)、`dev_display_22_1665.c`(20)。
- 实例：`Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c:1-52`——含「RJ45 竞争纪律 / 上电效果（不实现）/ 0x49 持久化 / 帧头冲突纪律」四段分节，**doc/15 的 §1/§7/§8 已逐条有等价段落**（见 §C.4）。

### (b) 函数注释（现有格式抽样）

- 三形态并存：
  1. **完整 Doxygen**：`Application/Src/app_dispatch.c:116-123`（`@brief` + 空行解释 + `@param` + `@return`）——728 个 `@brief` 的主体形态；
  2. **单行横幅**：`Application/Src/app_boot.c:38`「`/* ---- HalfSecTask: 500ms 喂狗 / 60s RTC 备份 / LED 翻转 ---- */`」、`app_boot.c:59`「`/* ---- 启动入口 ---- */`」；
  3. **无注释**：`Device/Display/dev_display.c:56` `dev_display_commit_frame()`、`:69` `dev_display_commit_frame_rect()`（无块注释，仅有函数体内行内注释）。
- `@param` 最密的 `.c`：`app_gz_proto_cmd.c`(30)、`app_yn_ol_proto_cmd.c`(21)、`dev_dp83848.c`(20)、`app_yn_proto_cmd.c`(20)、`app_sc_mtc_proto_cmd.c`(14)。
- 需改造面：**169 个函数前注释块无任何 Doxygen 标签**（38%）+ **205 个函数完全无注释**。

### (c) 宏/结构体/查表

- 主分类仅 166 行，但这是**优先级吃掉**的结果：`Application/Inc/app_render.h:33-43`（枚举成员 `/* 宋体 */`、`FONT_ENC_UTF8 /* 输入文本编码 — 内部自动转 GBK */`）、`app_ldi_cmd.h`（结构体字段逐行）、`dev_display_22_1703.c:38-45`（面板常量）等。
- 特征：**数值 + 单位 + 取值域**；22-1665 的几何宏处还额外承担**契约**（见 (e)）。

### (d) 决策历史（含日期、裁决、现场实测结论）

- 规模：主分类 **1749 行 / 195 块 / 52 文件**；含日期 1421 行、含裁决 970 行、含现场实测 772 行（三者重叠）。
- 热点文件：`app_yn_proto_cmd.c`(98)、`app_gz_ol_proto_cmd.c`(90)、`app_anhui_proto_cmd.c`(89)、`app_yn_ol_proto_cmd.c`(81)、`app_mm_proto.h`(80)、`app_yn_ol_proto.h`(78)。
- 实例（逐条**均可在 doc 找到等价内容**）：
  - `app_gz_ol_proto_cmd.c:189-191`「文本字面 `\n`（0x5C 0x6E）→ 换行 0x0A 原地转换（2026-09-18 用户裁决，GBK 安全行走）」↔ `doc/14` §11。
  - `app_anhui_proto_cmd.c:63-78`「0x85 静态显示（x=data[0]、y=data[1] 直接像素）」↔ `doc/13` §1.2。
  - `app_mm_proto.h:47-53`「结束码结构正签名（末字节 [19] 不参与判别）」↔ `doc/16` §2/§3。
  - `app_udp.c:59-67`「广播发送 2026-08-21 修复 LDI 搜索广播丢包」↔ `doc/07` §14/§15。
- 判断：**这 1749 行是「按新标准该压缩/外移」的主体**，但每条都必须先核对 doc 等价段落（见 §B.5 风险）。

### (e) 契约/安全约束

- 规模：主分类 **1132 行 / 222 块**；含契约词标签 2067 行 / 310 块。
- 热点：`app_dispatch.c`(107)、`app_sc_mtc_proto.c`(42)、`app_ldi_cmd.c`(41)、`app_mm_proto.c`(39)、`dev_dp83848.c`(39)、`dev_display_22_1665.c`(36)、`app_gz_ol_proto.c`(33)、`app_anhui_proto.c`(33)。
- **典型「注释 = 唯一约束载体」实例**（删注释 = 删约束）：
  - `Device/Display/dev_display_22_1665.c:13-15`「物理约束（契约，非编译期守卫）：MODULE_COLS ≤ 5…」——**17 条 `_Static_assert` 已删**，约束只存于此注释 + `doc/01` §0.3（该文档 §0.3 已完整承载，属「已迁移」）；
  - `Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto.c:30-32`「`{` 帧族互斥守卫：同一构建只允许编入一个…」——守卫宏 `g_brace_proto_guard` 的**语义**说明；
  - `Application/Src/app_dispatch.c:309-312`「通道回验：通知携带的指针必须仍注册在案，否则丢弃该通知」——悬垂指针防护的**设计契约**；
  - `app_dispatch.c:351-353`「单轮通知最多解析 64 帧…杜绝异常路径死循环」——**反死循环上限**契约；
  - `Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h:154` 成员级「**高字节在前 BE16**（已裁决…）」——字节序契约（doc/14 §13.5.1 / doc/15 §2 有等价内容）；
  - `Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c:28-30`「改硬件（换脚位/改有效电平）**只改本表 + `_mm_drive()`**」——单一改动点契约（doc/16 §3/§4 有等价内容）。

### (f) 指向 doc 的指针

- 规模：主分类 174 行 / 29 块；含指针标签 1196 行 / 99 块（多数指针与历史/契约同块）。
- 全量 grep（行级）：`doc/` **106 行**、`§` **92 行**、注释内日期 **176 行**（与块级统计口径不同，供交叉参考）。
- 现状问题：**指针格式不统一**——混用 `doc/14 §13`、`见 README §待裁决 Q3`、`见 doc/15 §4/§7`、`详见 doc/01_显示系统/动态滚动显示实现记录.md`（全路径）、`（doc/15 §8）` 等形式；`app_gz_ol_proto.h:32` 甚至指向 **README 的相对「§待裁决 Q3」**（跨模块失效风险）。

### (g)/(h) 其它与行内

- 单行小节横幅（`/* ==== … ==== */`、`/* ---- x ---- */`）：量大但零风险，保留（可统一为一种样式）。
- 行尾注释 391 行 / 391 块（8 个文件），如 `app_dispatch.c:361-364`（`bool any_wait = false; /* 有协议：帧头可能匹配但数据不足 */`）——**建议保留**（是最近的语义提示）。

## B.4 逐类处理建议与风险等级

| 类 | 建议动作（按新标准） | 预期收益 | 风险 | 备注 |
|---|---|---|---|---|
| (a) 文件头 | 统一压到 **≤10 行**（`@file` + `@brief` + 1~2 行职责 + 指针）；≥20 行的 14 个文件优先 | −400~600 行 | **低**（内容基本都有 doc 落点，逐条核对即可） | 头块中位数本来就是 8 行，绝大多数文件不用动 |
| (b) 函数注释 | ① 169 个无标签块 → 按模板重写；② 205 个无注释函数 → 至少补 `@brief`；③ 长解释挪 `@note`/指针 | 结构统一（行数基本持平，可能微增） | **低** | 注意「只加不减」：不能以统一为名删掉现有 why |
| (c) 宏/结构体/表 | 保留；统一 `/**< ... */` 成员注释；表头加一行来源指针 | ±0 | **低** | 数值/单位/取值域必须留 |
| (d) 决策历史 | **先迁移 doc → 再精简为结论一句 + 指针**（默认口径）；或「原样保留，仅压缩措辞」 | −600~1000 行 | **中高** | 需逐条核 doc 等价段落；核不到的先迁移 |
| (e) 契约/安全 | **原样保留**；仅当 doc 有完整等价段落时才压缩措辞，**禁止删除** | −100~250 行 | **高** | 22_1665 / BE16 / 缅甸极性 / `{` 帧族守卫 / 通道掩码 等 |
| (f) doc 指针 | 保留；统一格式 `详见 doc/xx §y`（去掉 README 相对节号） | ±0（质量提升） | **低** | 改指针格式会触碰少量文档同步 |
| (g)/(h) 其它/行内 | 保留；可选统一小节横幅样式 | ±0 | **低** | — |

**工作量口径（估算，仅供排期）**

- 触及文件：**99 个有头块的文件 + 205 个无注释函数所在文件**，实际改动面预计 **60~90 个文件**（协议模块 + 显示驱动 + 通道/引擎）。
- 注释行数：8511 → **约 7200~7700**（净减 8%~15%）；`@brief`/`@param` 覆盖率提升到函数数的 ~100%。
- 分三批较稳：**批 1** = 14 个长头文件 + 169 个无标签函数块（纯结构化，低风险）；**批 2** = (d) 类逐模块「先迁移 doc 再精简」；**批 3** = 跨模块指针/横幅统一 + 复核。

---

# C. 改写口径设计（供用户确认的模板草案）

## C.1 `.c` 函数注释模板

**标准形（有参或有返回）**

```c
/**
 * @brief  <一句话功能，动词开头、≤40 字、不复述函数名>
 *
 * @param  <名>  <语义/单位/取值域/所有权，一句话；不重复类型>
 * @param  <名>  <…>
 * @return <返回值语义；仅 void 可省>
 * @note   <仅当必要时：非显而易见的 why / 副作用 / 调用时机/上下文约束>
 *         详见 doc/xx §y（复杂机制、推导、历史取舍一律放文档）
 */
```

**极简形（无参 / 无返回 / 平凡 helper）**

```c
/** @brief <一句话功能> */
```
或沿用横幅（与极简形等价，**二选一需用户定**）：
```c
/* ---- <名>: <一句话功能> ---- */
```

**写作纪律（建议随规范落文）**

1. `@brief` 一句话，**不写**「本函数用于…」这类套话；不复述参数名。
2. `@param` 只写**语义**（含义/单位/范围/谁拥有内存），**不写类型**（签名已有）。
3. 错误码返回用 `@retval` 逐条列（沿用 `dev_dp83848.c` 既有形态）。
4. 正文（why 段）最多 2~3 行；更长的推导/历史/现场记录**全部外移 doc**，只留指针。
5. 指针统一格式：`详见 doc/NN_<目录>/<文件>.md §x.y`；跨模块引用**禁止**用 `README §待裁决 Q3` 这种相对写法。
6. 语言：中文正文 + 英文标签；`@file` 头保持现有 `@file/@brief` 两行结构。

## C.2 改写前后对照（真实摘录 ×3）

### 例 1：带 doc 指针的长块注释（`Application/Src/app_boot.c:168-175`）

**改前（现状，11 行）**
```c
/* ---- FreeRTOS 内核静态任务缓冲：空闲任务 / 定时器服务任务 → CCMRAM ----
 *
 * 强定义覆盖 cmsis_os2.c 的 `__WEAK` 实现（弱版把 Idle/Timer 的 TCB+栈共 1736B
 * 放在自己的 .bss = SRAM）。账本、地址实证与「CCM 不经 DMA 故可放」的安全性论证见
 * doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md §8.2；
 * 仍未下沉的 timers.c 定时器队列（240B）与 `configUSE_TIMERS=0` 评估见同节。
 *
 * 调用时机：`vTaskStartScheduler()` 内（先空闲任务、后定时器任务），早于 init_task。 */
```
**改后（5 行）**
```c
/* FreeRTOS 内核静态任务缓冲 → CCMRAM（强定义覆盖 cmsis_os2.c 的 __WEAK 实现）。
 * 调用时机：vTaskStartScheduler() 内，早于 init_task。
 * 账本/安全性论证/未下沉项（定时器队列 240B、configUSE_TIMERS=0）见
 * doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md §8.2 */
```
（**保留**：指针 + 调用时机 + 为什么在 CCM；**外移**：与 doc 逐字重复的论证细节。）

### 例 2：运行时守卫函数（`Device/Display/dev_display.c:44-50`）

**改前（现状，6 行，无 `@param`）**
```c
/* ---- 「整机无显示驱动」构建的调用侧守卫（运行时判空，无编译期开关）----
 *
 * 显示模组驱动未编入的构建（EIDE Myanmar 目标 = Device/Display 下无
 * dev_display_*.c）里 s_active_display 恒为 nullptr：显存/查表/扫描任务全都不存在。
 * 凡「必须显示才能完成」的入口先经本函数判空 → 安全跳过或降级。
 * （`what` = 调用点标签，保留供调试器观察；原有 RTT 限速告警随本轮移除。） */
bool dev_display_guard(const char *what)
```
**改后（7 行，模板化）**
```c
/**
 * @brief  「整机无显示驱动」构建的调用侧守卫（运行时判空，无编译期开关）。
 * @param  what  调用点标签（仅便于调试器观察，不解引用）。
 * @return true = 有可用显示实例；false = 无显示驱动，调用方须安全跳过或降级。
 * @note   无显示构建（EIDE Myanmar 目标）下 s_active_display 恒为 nullptr。
 *         17 条调用路径清单见 doc/16_缅甸2D费显协议/README.md §10。
 */
```

### 例 3：协议执行文件头（`Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c:1-18`）

**改前（18 行：三段叙述 + 引脚/极性裁决 + 副作用）**
```c
/**
 * @file    app_mm_proto_cmd.c
 * @brief   缅甸 2D 费显协议 — 帧解析 + 车道灯执行 + **干接点输出单一映射点**
 *
 * ① `mm_parse_frame`：…（4 行）
 * ② `s_mm_lamp_map` + `_mm_drive()` / `mm_apply_lamp()` / `mm_apply_init()`：…（5 行）
 * ③ `mm_arrow_map_toggle()`：…（3 行）
 * 引脚（2026-09-24 用户裁决，2026-09-28 修正极性）：红灯 = S1 = PE12 / …（4 行）
 * ⚠ 接管副作用（预期）：SW1~SW3 输入语义失效…（3 行）
 */
```
**改后（8 行：职责 + 契约点 + 指针）**
```c
/**
 * @file    app_mm_proto_cmd.c
 * @brief   缅甸 2D 费显 — 帧解析 + 车道灯执行 + 干接点输出**单一映射点**
 *
 * 硬约束（改硬件只改这里）：`s_mm_lamp_map` + `_mm_drive()` 是灯位→引脚/极性的
 * 唯一真源（红灯 S1/PE12、左下 S2/PE11、右下 S3/PE10，**高有效**推挽）。
 * 接管顺序 / EXTI 摘除 / 极性实测证据 / RLS 副作用见
 * doc/16_缅甸2D费显协议/README.md §3、§4.1~§4.3。
 */
```

### 例 4（补注释：从 0 到 1）（`Device/Display/dev_display.c:56`）

**改前**：`void dev_display_commit_frame(dev_display_t *dev)` 上方**无注释**。
**改后**
```c
/**
 * @brief  提交整帧（全屏语义）：置帧就绪并令脏矩形失效，下一次扫描重排全屏。
 * @param  dev  显示实例（nullptr 直接返回）。
 * @note   与 dev_display_commit_frame_rect 的区别见 dev_display.h；
 *         提交/消费在 osKernelLock 保护下完成。
 */
```

## C.3 边界规则（建议，逐条待裁决）

| # | 对象 | 建议口径 |
|---|---|---|
| 1 | **静态函数** | 与公开函数同模板（`.c` 主体是静态函数）；**平凡一行函数**允许 `/** @brief ... */` 一行式 |
| 2 | **无参函数** | 只写 `@brief`（`(void)` 参数不写 `@param`） |
| 3 | **返回值** | 非 void 必写 `@return`；多错误码用 `@retval`；布尔返回值写清 true/false 语义 |
| 4 | **初始化 / 回调类** | `@brief` + `@note`（**调用时机/上下文**：initcall 顺序、ISR 上下文、是否可在任务前调用）——如 `vApplicationGetIdleTaskMemory`、`_on_tim3_period` |
| 5 | **`.h` 文件** | 只保留**声明级极简**：导出函数 `@brief`（+必要时 `@param`）、结构体成员 `/**< 语义/单位 */`、契约宏保留；**不复制 `.c` 的实现说明**，复杂机制给指针 |
| 6 | **文件头** | `@file` + `@brief` + ≤2 行职责 + 指针；**建议阈值 ≤10 行**（现状中位数 8 行） |
| 7 | **宏** | 保留；写清单位/取值域；有编译期约束的（如 `MODULE_COLS ≤ 5`）**必须留** |
| 8 | **结构体/枚举** | 成员逐行 `/**< ... */`；布局有二进制兼容要求的（`app_board_net_cfg.h` 72B 记录、LDI 65B 网络块）**保留偏移/尺寸注释** |
| 9 | **查表/映射表** | 表头一行写「表义 + 来源」；数值本身不注释 |
| 10 | **函数内部注释** | 只解释**非显而易见的 why**（锁、内存序、越界防护、反死循环上限…）；**删除**逐行复述代码的注释（本轮抽样未发现成规模的此类） |
| 11 | **禁止触碰** | 真 `TODO`（`app_ldi_cmd.c:1028/1032/1072/1077/1082` 5 处占位、`app_anhui_proto_cmd.c:243` 语音占位）、`[待裁决]`/待确认标记、历史回归案例（`doc/08_协议模块接入规则/01_通用规则与模块骨架.md:176` 第 5 条「代码注释纪律」已明文禁止新增外部参考工程引用，存量按批处理时顺带订正） |
| 12 | **语言与标签** | 中文正文 + Doxygen 英文标签（沿现状）；不引入 `@since`/`@version` 之类无消费者的标签 |

## C.4 doc 映射表（模块 → 目标文档 → 是否已含等价内容）

> 判定依据：`grep -rl <模块名> doc/` 命中 + 抽读章节标题（§B 的 doc 指针注释已交叉验证）。

| 源模块（Application / Device） | 目标文档 | 是否已含等价内容 | 需补章节（若 §B 发现缺口） |
|---|---|---|---|
| `Application/Src/app_dispatch.c` + `app_dispatch.h` | `doc/05_协议模块多协议兼容优化/01_architecture.md`（+ `doc/08_.../01_通用规则与模块骨架.md`） | ⚠️ 部分：架构/链式 probe/queue 深度有；**「通道回验」「64 帧上限」「WAIT 预算」三条契约无专节** | 建议在 doc/05-01 增 §「帧分发引擎的防御性契约」 |
| `Application/Src/app_scroll.c` + `app_scroll.h` | `doc/01_显示系统/动态滚动显示实现记录.md` | ✅ 已含（机制/停止语义/互斥/内存） | — |
| `Application/Src/app_render.c` + `app_render.h` | `doc/01`（自适应字号/滚动）+ `doc/02_.../字库芯片更换与移植指导.md` | ⚠️ 分散：渲染引擎总览与 `readaddr` 公式推导**无单一落点** | **需新建**「渲染引擎实现说明」或归入 doc/01 |
| `Application/Src/app_boot.c`、`app_test.c`、`app_key.c`、`app_uart_baud.c`、`app_default_display.c` | `doc/CLAUDE.md`（启动流程/initcall 表/任务清单）+ `doc/06-04 §8`（堆） | ✅ 已含（CLAUDE.md 是现状权威） | — |
| `Application/Src/app_net_boot.c`、`Application/Src/Config/app_board_net_cfg.*` | `doc/07_LDI与IAP配置解耦/`（README + 02_decoupling_solutions.md） | ✅ 已含（72B 布局/自愈/默认落盘） | — |
| `Application/Src/LDI/*`（含 `app_ldi_cmd.c` 427 注释行） | `doc/02_LDI协议与外设接口/`（外设接口实施计划 + 字库 + 光敏） | ⚠️ 部分：LDI 帧格式/命令语义**主要靠 doc/CLAUDE.md 与代码**；doc/02 多为专项归档 | 建议新建「LDI 命令集与 0AH/响应帧组帧」专节（doc/02 内） |
| `Application/Src/IAP/*` | `doc/07_.../02_decoupling_solutions.md`（+ doc/CLAUDE.md IAP 章） | ⚠️ 部分：0x03 版本字节序/0x04 缺口在 CLAUDE.md 有；**`upstream/wyh` 的 IAP 记录镜像同步修复尚未回流本地**（本地代码无 `pl_net_register_ip_listener`） | 与本轮无关，另案 |
| `ProtocolParser_ChongQing/*`、`RLS/*` | `doc/03_重庆高速二代费显协议/`（PartA/PartB/README） | ✅ 已含 | — |
| `ProtocolParser_QingHai/*` | `doc/04_青海高速费显协议/`（PartA/PartB） | ✅ 已含 | — |
| `ProtocolParser_ShanDong/*` | `doc/09_山东费显协议/README.md` | ✅ 已含 | — |
| `ProtocolParser_GuiZhou/*` | `doc/10_贵州费显协议/README.md` | ✅ 已含 | — |
| `ProtocolParser_YunNan/*` | `doc/11_云南费显协议/README.md` | ✅ 已含 | — |
| `ProtocolParser_Anhui/*` | `doc/13_安徽费显协议/README.md` | ✅ 已含（§1.2 坐标终裁、§6 内存） | — |
| `ProtocolParser_GuiZhou_Overload/*` | `doc/14_贵州治超屏协议/README.md` | ✅ 已含（§13.5.1 字节序、§12 字号/几何） | — |
| `ProtocolParser_YunNan_Overload/*` | `doc/15_云南治超屏协议/README.md` | ✅ 已含（§1/§4/§7/§8/§11） | — |
| `ProtocolParser_Myanmar/*` + `app_factory_test_mm.c` | `doc/16_缅甸2D费显协议/README.md` | ✅ 已含（§3/§4.1~§4.3/§10/§13/§14） | — |
| **`ProtocolParser_SiChuang_{ETC,MTC,Overload}/*`** | **无专文档**（仅 `doc/CLAUDE.md` §四川三协议） | ❌ **缺口** | **需新建 `doc/18_四川三协议/`**（或归入 doc/08 的「已接入协议档案」） |
| `Application/Src/Channel/*`（udp/tcp/rs232/rs485/mqtt） | `doc/05-01`（RB/通道绑定）+ `doc/08/02_串口协议接入.md`、`03_网络协议接入_UDP与TCP.md` | ⚠️ 部分：通道注册/生命周期有；**app_udp 的三实例端口语义（10011/20103/9528+GZ_OL）无单节** | 建议 doc/05-01 增「UDP 三实例端口与 netconn 预算」节 |
| `Device/Display/*`（1_263 / 22_1703 / 22_1665 / 1_969 / 1_577 / 1_260 / p20 / 基类） | `doc/01_显示系统/`（每模组一篇 + 滚动 + 自适应字号） | ✅ 已含（22-1665 有 1456 行专文，含 §0.3 契约） | — |
| `Device/Storage/*`（w25qxx / flash_int） | `doc/06-04`（内存）+ `doc/02/字库芯片更换与移植指导.md` + `doc/07` | ⚠️ 部分：**W25Qxx 读写时序/半双工 DMA/事件等待无专文档** | 建议新建「Storage 层实现说明」（或归入 doc/06） |
| `Device/Comm/*`（rs485/rs232/rs232_voice） | `doc/08/02_串口协议接入.md` + `doc/CLAUDE.md` UART 章 | ⚠️ 部分：DMA 乒乓缓冲尺寸在 doc/06-04 有；驱动实现细节无专节 | 可归入 doc/08-02 |
| **`Device/Network/*`（dp83848 / eth）** | **仅 `doc/CLAUDE.md` §网络子系统** | ❌ **缺口**（本地 156 注释行的 `dev_dp83848.c` 无对应文档） | **需新建**或归入 doc/05/CLAUDE.md |
| `Device/IO/*`（key / io_ctrl / light_sensor） | `doc/16 §4.2/§4.3`（EXTI 接管）+ `doc/02/光敏传感器…md` + `doc/CLAUDE.md` 按键表 | ✅ 大体已含 | — |

**映射表结论**：15/19 个模块有现成落点，可直接执行「注释压到结论 + 指针」；**4 处需要先补文档**（四川三协议、Device/Network、Storage 层、渲染引擎/通道 UDP 端口语义），否则会丢掉内容。

## C.5 验证方案（沿用项目 A/B 纪律）

> 原理：注释在翻译阶段 3 就被替换为空白，**不进入 token 流** ⇒ 非调试段应逐字节不变；`.debug_*` 必变故须排除（`--only-section` 对照时剔除 `.debug*`/`.comment`/`.ARM.attributes`）。

**可执行步骤概述**

```bash
# 0) 基线固化：先提交/冻结在飞改动（否则 diff 不可审、A/B 基线漂移）
git status --short            # 现状：53 M + 1 D + 8 未跟踪项

# 1) 归档改前源码（纯注释变更也要可回溯）
tar -czf /tmp/comment_std_pre_<批号>.tar -C <仓库根> <本批文件列表>

# 2) 只改注释（不改任何代码/宏/字符串/行号敏感内容）

# 3) token 级「只删不改」校验（现成脚本，断言 after 是 before 的子序列）
python3 .analysis/cleanup_20260929/check_token_equivalence.py /tmp/comment_std_pre_<批号>.tar

# 4) 非注释差异行扫描：diff -u 后过滤注释行，必须 0 差异（本批若含「改写」而非纯删，
#    该步会报差异——改写口径下用下面 5)6) 的机器码 A/B 作为主证据）

# 5) 重建「改前源码」→ 与现行源码用与 make 完全相同的 flags 各编译一次
python3 .analysis/cleanup_20260929/round2_ab_reconstruct.py       # 模板，替换批次清单
bash    .analysis/cleanup_20260929/round2_ab_compare.sh           # 模板，改文件列表即可

# 6) 段级 md5（排除 .debug_*）+ 反汇编逐行对照（objdump -d，双方一致）

# 7) 全量重编一次（默认口径 ALL/1_263/STD）：
arm-none-eabi-size -A build/Debug/Project_STD.elf   # 段尺寸与改前一致；告警仍 3 条既有 HAL
# 若本批触及 22_1665/协议模块，再跑对应宿主脚本：
bash .analysis/22_1665/verify_all.sh --fast；python3 .analysis/myanmar/mm_frame_sim.py
```

**三个必须脚本层核查的边界**（沿用上一轮报告 §4.6）：`__LINE__`（自研层唯一敏感点是 HAL 的 `assert_param`，本轮范围不涉及）、行尾反斜杠续行的注释、`a/**/b` 这类把注释当分隔符的极端写法（本地 0 处）。

**`__LINE__` 特别提醒**：本轮若为「**补注释**」（新增行）而非纯删，`.debug_line` 与 `__LINE__` 都会变；**必须**用段级对照（排除 `.debug_*`）而非整文件 md5 来证明「目标文件行为不变」。

## C.6 「代码-文档同步」规则要求同步更新哪些文档

按用户纪律（`~/.cursor/rules/code-doc-sync.mdc`）：改写注释后必须核对/更新被注释引用或被文档反向引用的地方：

1. **`doc/CLAUDE.md`**（1360 行，全项目知识地图）：`§代码风格` 需补注释规范（或改为指向新规范文档）；`应用模块全景`/`任务清单` 中大段模块描述若与压缩后的注释口径不一致，需同步。
2. **`doc/构建开关总表.md`**：本轮不动开关，**无需更新**；若新建规范文档需在其中登记（该文件是构建/开关清单）。
3. **(d) 类外移落点**：`doc/01`（渲染/滚动）、`doc/14`（GZ_OL）、`doc/15`（YN_OL）、`doc/16`（缅甸）、`doc/13`（安徽）、`doc/11`（云南）、`doc/07`（网络配置）——**外移时必须先确认等价内容已在册，否则先补文档**。
4. **反向引用**：`doc/01_显示系统/22-1665…md` 与 `doc/16` 明确把「源码注释契约」写进正文（如 doc/01 §0.3 第 272 行「约束只剩源码注释里的契约 + 本文档 §0.3」）——**若压缩 22_1665 驱动注释，doc/01 §0.3 的这句话必须同步改写**（否则文档指向的载体消失）。
5. **新增文档建议**：若决定立规范，建议 **`doc/18_注释规范.md`**（与 doc/17 同级的独立文件），并在 `doc/CLAUDE.md §代码风格` 加一行指针；不建议并入 doc/CLAUDE.md（该文件已 1360 行、职责是地图而非规范）。

---

# D. 待用户裁决问题清单

| # | 问题 | 选项 / 备注 |
|---|---|---|
| **Q1** | **「why 的注释标准」的确切出处？** 本仓库/两个远端/家目录均无 `why`；只有 `wyh`（同人双身份 `dzzwmqj@`、`origin/wyh`+`upstream/wyh` 分支、已删除的 `~/Desktop/Project-STD-wyh`）。 | (i) = 通用「注释解释 why」原则（则按 §C 模板执行）；(ii) = `wyh` 分支既有风格（则范本见 §A.3，且需补「指向 doc」这一条）；(iii) = 另有一处出处（请给路径/链接/原文） |
| **Q2** | **注释格式最终模板**（§C.1）：是否采用 `@brief + @param + @return/@retval + @note + 指针`？平凡函数是否允许一行式或横幅（二者选一）？ | 需定：① 是否强推 Doxygen 标签；② 横幅 `/* ---- x ---- */` 保留还是统一为 Doxygen（现状 169 个块为横幅/纯文本） |
| **Q3** | **`.h` / 文件头 / 宏与结构体 / 无参函数 / 静态函数**的处理口径（§C.3 表 1~9）是否照准？特别是**文件头阈值（建议 ≤10 行）**与**`.h` 是否只留声明级极简**。 | — |
| **Q4** | **在飞改动是否先提交/冻结**？现状 **53 M + 1 D + 8 未跟踪项**（含无 git 基线的 `ProtocolParser_Myanmar/` 两文件与 `app_factory_test_mm.c`）。 | 建议：先提交或打 tar 冻结；未跟踪文件另做文件级备份。否则 diff 混做、A/B 基线漂移 |
| **Q5** | **(d) 决策历史 / (e) 契约**的处理策略。 | (a) **先迁移 doc 再精简**（默认建议，工作量大但信息不丢）；(b) 直接精简（**有丢信息风险**，本报告不建议）；(c) 原样保留、仅压缩措辞（最保守）；(d) 分模块分别裁定 |
| **Q6** | **是否新建「注释规范」文档**（建议 `doc/18_注释规范.md`），并同步改 `doc/CLAUDE.md §代码风格`？ | 该规范是本次改写的验收依据（否则无判据） |
| **Q7** | **与架构冲突裁决（C1 几何宏 8×4、C2/C3 EIDE 口径）的先后顺序**？两者会改同一批文件（`Device/Display/*`、`app_boot.c` 等）。 | 建议：**先裁 C1/C2/C3 → 再动注释**（否则注释里的几何/口径描述将被二次改写、A/B 基线反复重建） |
| **Q8** | 本轮范围是否严格限于 **Application + Device**（Platform/Kernel/Core/Compiler/Middlewares/Drivers 不动）？ | 用户已明确；此处仅确认（本轮报告即按此范围统计） |
| **Q9** | 是否**同时**做「补注释」（205 个无注释函数）还是**只做改写**（不动无注释函数）？ | 「补注释」=新增行，会触碰 `.debug_line`/`__LINE__`（机器码仍应一致）；「只改写」可保持纯替换 |

---

## 附录 A：本轮证据与复现命令

```bash
# ---- A. why/wyh 定位 ----
git remote -v && git branch -a
git log --all --format='%an <%ae>' | sort | uniq -c | sort -rn
git ls-tree -r --name-only origin/wyh | grep -c '^doc/'
git show origin/wyh:Application/Inc/app_dispatch.h | head -12
git show origin/wyh:Application/Src/app_dispatch.c | sed -n '61,68p'
git show origin/wyh:Application/Src/AH_MQTT/ah_mqtt_cmd.c | sed -n '23,29p'
git show origin/wyh:Device/Network/dev_dp83848.c | sed -n '5,11p'
git show origin/wyh:Application/Src/app_boot.c | sed -n '28,29p'
git grep -c "@param" origin/wyh -- "Application/**/*.c" "Device/**/*.c"
find /home/yystation -maxdepth 4 -type d \( -iname "*why*" -o -iname "*wyh*" \)
grep -rn "\bwhy\b" --include=*.md --include=*.c --include=*.h .

# ---- B. 存量与分类 ----
python3 .analysis/audit_20261008/scripts/comment_stats.py --root . \
  --dirs Application Device --top 20 --label "Application+Device" --json /tmp/audit_cls/ab_stats.json
python3 /tmp/audit_cls/classify2.py --root . --dirs Application Device --out /tmp/audit_cls/cls2.json
python3 /tmp/audit_cls/func_cov2.py
grep -rc "@param" Application Device --include=*.c | grep -v ":0$" | sort -t: -k2 -rn
grep -rn "TODO\|待更新\|待裁决" Application Device --include=*.c --include=*.h

# ---- C. doc 映射 ----
for kw in app_render dev_w25qxx dev_rs485 dev_dp83848 app_scroll app_net_boot \
          app_board_net_cfg app_factory_test app_ldi_cmd app_dispatch SiChuang app_rls dev_key; do
  echo -n "$kw: "; grep -rl "$kw" doc/ | tr '\n' ' '; echo
done
```

**数据文件**：`/tmp/audit_cls/cls2.json`（2556 个注释块 + 172 文件元数据 + 六类聚合）、`/tmp/audit_cls/doc_map.txt`（源文件→doc 命中）。脚本：`/tmp/audit_cls/{classify2,func_cov2,doc_map}.py`。

## 附录 B：分类器口径与已知偏差（可复核）

1. **与上一轮数字互校通过**：8511 行 = 7275 (Application) + 1236 (Device)，逐文件级亦一致（差额文件 0 个，见 `/tmp` 校验输出）。
2. **主分类优先级**导致文件头/函数注释被历史/契约「吃掉」——因此 §B.3 提供**两张表**（主分类 + 标签重叠）与**交叉分布**（如 148 个文件头块中 34 个含契约词、32 个含 doc 指针）。
3. **`.h` 声明注释**未被「函数前」判据覆盖（判据要求下一行 `{`），落在「其它」类；`.h` 的函数注释量**被低估**。
4. **`.c` 中 5 处 `in_comment` 状态机**不处理嵌套注释（C 语言无嵌套，安全）；字符串/字符字面量中的 `//`、`/*` 已正确跳过（与上一轮脚本同源逻辑）。
5. 本报告所有文件名/行号**以 2026-10-08 工作区实况为准**；引用远端分支内容时显式标注 `origin/wyh:` / `upstream/wyh:` 前缀。
6. **本轮未做的**：未对 `Platform/Kernel/Core/Compiler/Middlewares/Drivers` 统计（用户已排除）；未对 phtty 分支做原型改写；未生成任何补丁。

---

*（报告完。全程只读：除本文件与 `/tmp/audit_cls/` 下的分析脚本/数据外，未创建、修改或删除任何文件；未执行 git 写操作，未走网络。）*
