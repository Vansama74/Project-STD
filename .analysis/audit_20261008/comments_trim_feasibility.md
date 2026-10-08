# 注释精简可行性评估（参考 phtty 远端仓库）

- **日期**：2026-10-08
- **范围**：本地 STD 工作区（`/home/yystation/Program/3833024/Project-STD-main`）+ 远端 phtty 仓库（本地 `upstream` remote）
- **性质**：**只读评估**。除本报告与统计脚本（`.analysis/audit_20261008/scripts/`）外未修改工作区任何文件；phtty 源码读取全部经 git 对象（不落盘工作区）；试验性分析产物在 `/tmp`。
- **一句话**：phtty 确实找到了（`https://github.com/phtty/Project-STD.git`），但**它做的不是「注释精简」，而是「陈旧注释订正 + Doxygen 规范化 + 知识外移文档」，注释净增不净减**——评估结论以此为前提展开。

---

## 0. 结论摘要（三个问题的直接回答）

| 问题 | 结论 |
|---|---|
| **1. 能实现吗？** | **有条件能**。但「参考 phtty 做精简」这一表述与 phtty 实际做法不符（§1.2）。按「减少注释量」口径：本地实测可净减 **400~900 行注释**（约占现有注释 3.5%~8%），**不能承诺大幅精简**——2026-09-29 已有一次实测先例，复核结论是「注释基本是契约/why，删了就丢信息」（§3.1）。按「注释更好用」口径：phtty 的 A+B 方法论可直接照搬。 |
| **2. 有什么问题？** | 6 条风险：知识/契约唯一性丢失、与未提交改动混做 diff、代码-文档同步牵连、验证成本高、产品线分叉（phtty 已不含本地全部省份协议）、收益有限（注释不占 Flash/RAM）（§4.2）。 |
| **3. 怎么做？** | 分批：**第一批**陈旧订正 + 历史叙述外移（8 文件量级，§4.3）；**第二批**未复核协议模块逐块判「契约 vs 历史」（先迁移再精简）；**契约注释/显示驱动/Core/HAL 建议不动**。每批沿用 2026-09-29 的 A/B 验证三件套（§4.5）。目标：全项目注释占比 27.8% → 约 26%（§4.4）。 |

---

## 1. phtty 仓库定位与实况

### 1.1 定位（已找到）

- 本仓库 `.git/config` 中 `upstream` = `https://github.com/phtty/Project-STD.git`（phtty 为上游原作者；本地 `origin` = `Vansama74/Project-STD`，属同源 fork 线）。
- `STM32F407-Bootloader-master` / `STM32F407-Recovery-master` 两个仓库**均无 phtty remote**（只有各自 origin，实测 `git remote -v`）。
- 2026-10-08 经本机代理 `git -c http.proxy=http://127.0.0.1:7897 fetch upstream` **成功**，取得最新 14 个分支，例如：
  - `upstream/main` = e86b15f（2026-09-17）
  - `upstream/yy` = 573f8dc（2026-09-01，含 Vansama74 侧 PR 合并）
  - `upstream/refactor/multi-variant` = 86f05fb（2026-09-28，phtty 最新重构线）
- 本地与 phtty 已**分叉为两条产品线**：phtty 保留原始产品线并新增 CASC 级联 / 折叠屏 / 多板适配；本地在其基础上开发了全部省份协议（青海/山东/贵州/云南/安徽/缅甸/治超系列）、22-1665 驱动、诊断瘦身等。**phtty 最新分支不含本地的任何协议模块**（`git ls-tree` 实测 multi-variant 的 Application 下无 `ProtocolParser_*`）。

### 1.2 phtty 的注释工程 = 「订正 + 规范化 + 知识外移」，**非精简**

phtty 在 `refactor/multi-variant` 分支 2026-09-23 前后做过一整套注释改造，关键提交：

| 提交 | 内容 | 注释净效果 |
|---|---|---|
| `d542f65` | 新增 `Doxyfile` + 《注释与 Doxygen 改造方案.md》（157 行方案） | — |
| `30703e3` | **A 类陈旧注释清扫**（22 条：数值错误/过期阶段/"P1、P4 留待"表述/调试残留） | **删过期信息**（订正） |
| `bb38080` | B0：27 个文件补 `@file`/`@brief` 文件头（+135 行） | **增** |
| `19bd71a` | B1：15 个契约头补 Doxygen 标签（`@brief` +157 / `@return` +57 / `@param` +43 / 成员 `/**<` +359） | **增**约 616 处 |
| `3c00233` | B2：40+ 头文件导出符号文档补齐 | **增** |
| `688f65a` | 工程指引 `CLAUDE.md` → `AGENTS.md` 重写 + 新增 `docs/架构说明.md`；旧 CLAUDE.md 删除、失效部分不保留 | 知识改放文档 |

方案文档核心条款（`git show d542f65:'docs/注释与Doxygen改造方案.md'`）：

- **共性约束**：「只补标签、只订正事实；**不重写**已有的设计性论述」；
- **明确「不要动（避免误伤）」**：历史归因说明（"原先的规矩""缺陷历史"）、真前向标记（真 TODO）、`test_*` 回归案例历史；
- **知识布局**：`docs/架构说明.md`（面向人的架构）+ `docs/命名约定.md`（§5 自注释规则、§12 Doxygen 最小必需集、§13 禁改清单）+ `AGENTS.md`（命令/目录/坑）+ Doxyfile 告警计分器；
- **验证**：「每批仍跑 `make test` + 两个固件目标（防手滑改到代码）」，Doxygen 告警数作验收计分器。

### 1.3 注释密度量化对照

| 口径 | 文件数 | 总行 | 注释行 | 注释/非空 |
|---|---|---|---|---|
| 本地工作区 | 254 | 46299 | 11583 | **27.8%** |
| phtty `main` | 162 | 28924 | 5839 | **22.4%** |
| phtty `wyh` | 162 | 29039 | 5898 | **22.5%** |
| phtty `refactor/multi-variant`（Doxygen 化后） | 222 | 41656 | 12394 | **33.3%** |

- 共同文件（相对路径一致的 **148 个**）逐文件对比：本地注释 5945 行 vs phtty 5242 行，**仅多 703 行（+13.4%）**；
- 本地注释的 **~49%（5638/11583 行）位于本地独有的 106 个文件**（协议模块、22-1665 驱动、缅甸模块等）——**这些文件 phtty 根本没有，无"参考对象"**；
- phtty 自身并非极简：其 `app_dispatch.c` 注释占比 47.9%（114/285 行）、`app_ldi_cmd.c` 44%，均与本地同级；其 `app_dispatch.c` 头部同样有设计 rationale（"每个协议持有自己的缓冲区，因此一个缓冲区只有唯一主人……"）。

**结论**：两边注释风格**同源**（Doxygen 头 + 分级标题 + 行尾短注释 + 设计 rationale）。真正的差异是**内容性质**而非数量（§1.4）。

### 1.4 真实差异：本地多出「历史决策档案」型注释

对自研层（Application/Device/Platform/Kernel/Core/Compiler）grep 统计：

| 标志 | 命中行数 |
|---|---|
| 注释中的日期（`20xx-xx-xx`） | **182** |
| 「裁决 / 定案 / 拍板 / 用户决定」 | **121** |
| `doc/` 文档指针 | **110** |
| `§` 章节指针 | **93** |

phtty 的注释**不写**「何时改的、谁裁决的、第几轮、现场实测」；本地大量注释正是这类「决策档案」（实例见 §2.3）。这是「参考 phtty 精简」的**实际可操作空间**所在，也是**风险**所在（这些叙述常常是该文件当前行为的唯一快速解释，删前要确认 doc/ 有等价段落）。

---

## 2. 本地注释存量清点

统计脚本：`.analysis/audit_20261008/scripts/comment_stats.py`（用法见附录 C）。数据：`.analysis/audit_20261008/local_stats.json`。

### 2.1 总量与分目录

| 目录 | 文件数 | 总行 | 注释行 | 注释/非空 |
|---|---|---|---|---|
| Application | 142 | 22920 | 7275 | **36.0%** |
| Device | 30 | 4150 | 1236 | 34.6% |
| Platform | 38 | 2734 | 822 | 35.2% |
| Kernel | 13 | 12001 | 310 | 2.6%（`text_cvt.c` 字库大表占 11414 行） |
| Compiler | 2 | 268 | 115 | 46.0% |
| Core（CubeMX/ST 生成） | 29 | 4226 | 1825 | 51.8% |
| **合计** | **254** | **46299** | **11583** | **27.8%**（总行占比 25.0%） |

注：**自研层（除 Core）注释约 9758 行**，Application/Device/Platform 三目录占比 34.6%~36.0%——这是「可讨论精简」的真实基数。Core/ 的 51.8% 是 ST 官方模板注释，**不属于本工程资产**。

### 2.2 Top 20 「注释量最大」文件

| 文件 | 总行 | 注释 | 注释/非空 | 最长注释块 |
|---|---|---|---|---|
| Application/Src/LDI/app_ldi_cmd.c | 1174 | 427 | 41.5% | 13 |
| Core/Src/system_stm32f4xx.c（ST 生成） | 725 | 356 | 55.3% | 36 |
| Core/Inc/stm32f4xx_hal_conf.h（ST 生成） | 504 | 265 | 63.5% | 39 |
| Application/Src/app_dispatch.c | 604 | 229 | 43.7% | 12 |
| Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h | 285 | 206 | 79.5% | 52 |
| Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c | 683 | 202 | 33.7% | 22 |
| Application/Inc/LDI/app_ldi_cmd.h | 287 | 201 | 77.6% | 13 |
| Platform/Src/pl_eth.c | 675 | 192 | 33.2% | 11 |
| Application/Src/app_render.c | 833 | 182 | 24.5% | 34 |
| Kernel/Src/text_cvt.c | 11414 | 164 | 1.4% | 12 |
| Application/Src/ProtocolParser_YunNan/app_yn_proto_cmd.c | 524 | 161 | 33.8% | 19 |
| Device/Network/dev_dp83848.c | 459 | 156 | 39.2% | 16 |
| Application/Src/LDI/app_ldi.c | 574 | 145 | 29.2% | 9 |
| Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c | 384 | 145 | 42.3% | 28 |
| Application/Src/ProtocolParser_GuiZhou/app_gz_proto_cmd.c | 534 | 141 | 28.4% | 11 |
| Device/Display/dev_display_22_1665.c | 372 | 135 | 40.5% | 20 |
| Application/Src/Channel/app_udp.c | 668 | 130 | 22.9% | 21 |
| Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h | 171 | 129 | 83.8% | 29 |
| Application/Src/ProtocolParser_ChongQing/app_cq_proto.c | 359 | 123 | 38.1% | 23 |
| Application/Src/app_scroll.c | 516 | 122 | 26.3% | 35 |

「仅本地新增文件」注释排行前 5：`app_yn_ol_proto.h`(206) / `app_yn_ol_proto_cmd.c`(202) / `app_yn_proto_cmd.c`(161) / `app_gz_ol_proto_cmd.c`(145) / `app_gz_proto_cmd.c`(141)。

### 2.3 注释形态分类（附实例证据）

1. **契约型（必须保留）**——承载编译期已删/无法表达的约束：
   - `Device/Display/dev_display_22_1665.c:13`：「物理约束（契约，非编译期守卫）：MODULE_COLS ≤ 5…」（17 条 `_Static_assert` 已删，约束仅存于此处 + `doc/01` §0.3）
   - `Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h:154`：成员 `/**< **高字节在前 BE16**（已裁决…）*/`
2. **设计说明 / rationale 型（保留）**：
   - `Platform/Inc/pl_task_static.h:5-13`「## 背景」（为什么下沉 CCMRAM）、`pl_task_static.h:26-44` 用法三步——与 phtty 风格一致
   - `Application/Src/app_dispatch.c`（229 注释行，多为「通知元素三件事一次解决」这类设计说明）
3. **历史 / 裁决档案型（精简主要候选）**：
   - `Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c:12-19`：「引脚（2026-09-24 用户裁决，2026-09-28 修正极性）…⚠ 接管副作用…」
   - `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c:13-43`：四段裁决分节（RJ45 竞争 / 上电效果 / 0x49 持久化 / 帧头纪律），`doc/15` 均已有对应章节
   - `Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h:24-31`：BE16 字节序裁决过程，`doc/14` §13.5.1 已有
   - `Application/Src/app_net_boot.c:8-17`：修订史（"2026-08-24 修订"等），`doc/07` 已有
   - `Application/Src/app_scroll.c:5-42`：机制/停止语义/并发模型叙述，自注「详见 doc/01_显示系统/动态滚动显示实现记录.md」——**注释与文档重复**
4. **流程复述 / 逐行解说（可删，量小）**：未发现大段；个案如 `app_scroll.c:12`（"渲染 = …"）等少数行。

### 2.4 陈旧引用扫描（对应 phtty「A 类」）

| 检查项 | 结果 | 判定 |
|---|---|---|
| 已删除诊断设施（`APP_DIAG` / `[diag]` / `*_RTT_DIAG`）的注释引用 | **0 命中** | 2026-09-29 瘦身轮已清理干净，**无 A 类存量** |
| `TODO` / `[待更新]` / 待裁决标记 | 8 处（多为 `app_ldi_cmd.c` 的 `dev_*_ctrl` 占位 TODO） | 属「真 TODO」，phtty 口径：**不动** |
| `_Static_assert` 现状与注释一致性 | 22-1665 已删断言（约束在注释+doc/01）；其余协议头断言**仍在**（如 `app_yn_ol_proto.h:79-81`） | 一致性正常；22-1665 的注释=约束唯一代码侧载体 |

**结论**：本地「陈旧注释」存量比预想小（上一轮瘦身已体检过），**没有 phtty 那种 22 条 A 类清单可扫**。

---

## 3. 本地先例与规则（评估依据）

### 3.1 2026-09-29 已有一次「注释精简」实测（空间小的直接证据）

`.analysis/cleanup_20260929/round2_report.md`：

- 实际改动 **4 文件 / 6 处**，净 **−41 行 / −1848 字符**（`round2_report.md:108-125`）；
- 复核结论（`round2_report.md:128-138`）：「**已复核但未继续删的文件**（结论：注释基本是契约/why，删了就丢信息）」——列了 `app_ldi_cmd.c`（全契约）、`app_render.c`（公式推导 why）、`app_udp.c`、`app_yn_ol_proto_cmd.c`、`pl_eth.c`、`dev_dp83848.c`、`dev_display_22_1665.c` 逐个结论；
- 剩余可选项仅「与 doc 段落逐字重复」的文本，**收益有限**（原文）。

方法论与工具（**可直接复用**）：token 级「只删不改」校验（`check_token_equivalence.py`）、非注释差异行扫描、机器码 A/B（`round2_ab_reconstruct.py` 重建改前源码 + `round2_ab_compare.sh` 段级 md5 + 反汇编逐行）——见 §4.5。

### 3.2 22-1665「注释承载约束」的既成事实

`_Static_assert` 删除（第三十四轮）后，几何约束以「**源码注释契约 + `doc/01` §0.3**」形式存在（`doc/01_显示系统/22-1665模组驱动分析与迁移记录.md:12`、`:272`；驱动侧 `dev_display_22_1665.c:13-16`）。**教训：注释被当契约用时，删注释 = 删约束，必须先确认文档已完整承载**（现该例已在档，属「已迁移」状态）。

### 3.3 代码-文档同步规则（用户纪律）

修改代码后必须检查并更新相关文档（`doc/`、模块 README、`CLAUDE.md` 等）；注释被文档引用（或反向指针）时需同步。本工程 doc/ 体系庞大（17 个专题目录 + `doc/CLAUDE.md` 1360 行），精简任何「文档指针注释」都可能触发文档侧同步。

### 3.4 工作区现状（混做风险的事实基础）

- `git status`：**61 个已跟踪文件 M** + **8 个未跟踪项**（`ProtocolParser_Myanmar/`、`app_factory_test_mm.c`、`.analysis/cleanup_20260929/`、`.analysis/myanmar/`、`doc/16_*`、`doc/17_*` 等）；最近提交 `df3fdbb`；
- 实测跟踪状态：`Device/Display/dev_display_22_1665.c` = **tracked**；`Application/Src/ProtocolParser_Myanmar/app_mm_proto.c`、`app_mm_proto.h`、`app_factory_test_mm.c` = **UNTRACKED**（无 git 基线）；
- `build/Debug/Project_STD.elf` mtime = 2026-10-08 09:10（今天被构建过——注意既有纪律「EIDE 会覆写 build/Debug 共享产物」，见 doc/CLAUDE.md 构建卫生 R1）。

---

## 4. 分析与结论

### 4.1 可行性判定：有条件能

| 条件 | 说明 |
|---|---|
| ① 先定目标口径 | 「**减量**」还是「**phtty 式规范化**」？两者工作内容、产出、收益完全不同（phtty 实际做的是后者，注释净增）。参照物用错会白干。 |
| ② 先冻结在飞改动 | 61 M + 8 untracked 必须先提交或冻结；未跟踪的缅甸模块无 git 基线，须文件级备份。 |
| ③ 接受 A/B 验证成本 | 每批 = 备份 + token 校验 + 全量重编 + 段/反汇编对照（§4.5），成本高于改动本身。 |
| ④ 不对齐 phtty 内容 | phtty 已无本地协议模块，**只能借其方法（订正/规范化/外移），不能借其内容**。 |

### 4.2 风险清单（6 条）

1. **知识/契约唯一性丢失（最高风险）**：`dev_display_22_1665.c:13`、`app_mm_proto_cmd.c:12-19`、`app_gz_ol_proto.h:24-31` 等注释是约束/现场实测/裁决的（快速）唯一记录。**判据**：精简前逐条核对对应 doc 章节（doc/01 §0.3、doc/16 §4、doc/14 §13.5.1）有等价内容；无 → 先迁移再动。
2. **与未提交改动混做（diff 不可审）**：当前大量 M 文件 + untracked 目录；同批进行会使 `git diff` 混入无关变更、A/B 基线漂移。对策：先提交/冻结；每批只动注释且单独提交。
3. **代码-文档同步牵连**：注释中 110 处 `doc/` 指针 + 93 处 `§` 指针；精简/改写指针后须同步对应文档（可能触发 doc/01、doc/07、doc/14、doc/15、doc/16 等多处）。超出本批范围时要显式告知用户。
4. **验证成本**：每批需归档改前源码 + 一次全量重编（-j8，数分钟级，参考 `.analysis/cleanup_20260929/round2_build.sh` 四口径流程）+ 段级 md5/反汇编对照；若含口径变体（22_1665 等），成本再乘。
5. **phtty 路线与本地知识密度的冲突**：phtty 直接删掉旧 CLAUDE.md、失效信息不保留；本地 `doc/CLAUDE.md`（1360 行）是全项目知识地图与纪律载体——**「照 phtty 删文档/删历史」不可取**；只可借鉴「注释只写当下事实 + 指针指向文档」的写法。
6. **收益有限（无空间收益）**：注释不改变 Flash/RAM（2026-09-29 实测 section 逐字节一致即证明）；精简只赚可读性/维护性。若为「省空间」而做——**方向错误**。

### 4.3 分批实施清单（建议）

#### 第一批：陈旧订正 + 历史叙述外移（可直接做，已抽检支撑）

| 文件 | 现状（证据） | 动作 | 预估 |
|---|---|---|---|
| `Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h` | 头块 52 行，含 4 段裁决叙述（`yn_ol_proto.h:28` 起） | 裁决过程压为 `doc/15` 指针；命令表保留 | −15~25 行 |
| `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c` | 头块 52 行（4 个裁决分节，`:13-43`） | 同上（doc/15 §4/§7/§8） | −15~25 行 |
| `Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h` | 头块 50 行，字节序裁决过程 `:24-31` | 压为 `doc/14` §13.5.1 指针 | −8~15 行 |
| `Application/Src/app_scroll.c` | 头块 42 行，机制叙述与 doc/01 滚动文档重复（`:5-42`） | 压缩为结论 + 指针 | −15~25 行 |
| `Application/Src/app_net_boot.c` | 头块 29 行，修订史 `:8-17` | 压为 `doc/07` 指针 | −8~12 行 |
| `Platform/Inc/pl_task_static.h` | 头块 60 行（背景 9 行 + 用法 + 纪律） | 背景压缩；**用法与纪律保留** | −8~15 行 |
| `Application/Inc/ProtocolParser_Anhui/app_anhui_proto.h` | 头块 27 行 | 抽检后决定 | −5~10 行 |
| 零散订正 | 各协议 cmd 文件内的过时表述 | 逐条核对（成本高，随批触达） | −10~30 行 |

#### 第二批：先迁移知识、再精简（协议模块逐块判定）

- **未复核过的协议实现文件**（§3.1 清单之外的）：`app_gz_ol_proto_cmd.c`(145)、`app_gz_proto_cmd.c`(141)、`app_yn_proto_cmd.c`(161)、`app_anhui_proto_cmd.c`(118)、`app_sc_mtc_proto_cmd.c`(110)、`app_sc_etc_proto.c`(100) 等。方法：逐注释块分类「契约（留）/ 历史（迁 doc 后删）/ 陈旧（订正）」，历史部分迁入对应 `doc/14/15/13/11` 的 README。
- `Application/Src/LDI/app_ldi_cmd.c`（427 注释行）：上轮结论「全契约」——**整文件不动**，仅逐条抽检。
- `Application/Inc/Config/app_board_net_cfg.h/.c`（86+93 注释行）：与 `doc/07` 有对应章节，可评估外移。

#### 建议不动

- **契约注释载体**：`Device/Display/dev_display_22_1665.c`（已历 4 轮精简，注释=约束唯一载体）、`app_yn_ol_proto.h:79-81` 等 `_Static_assert` 邻近注释；
- **契约头**：`app_dispatch.h`、`dev_display.h`、`app_render.h`、`dev_storage.h`、`ring_buffer.h`（phtty 自己的 B1 批也是**给这些补标签**，不删）；
- **第三方/生成物**：`Core/`（CubeMX/ST）、`Drivers/`、`Middlewares/`、`Kernel/Src/text_cvt.c`（数据表）；
- **未跟踪的缅甸模块 + `app_factory_test_mm.c`**：**待提交后再评估**（无 git 基线、现场仍在联调）。

### 4.4 注释占比目标与工作量估算

- 存量基数：全项目 11583 行注释（27.8%）；自研层 ~9758 行（Application 36% / Device 34.6% / Platform 35.2%）。
- 合理净减：**400~900 行**（第一批 ~80~130 行 + 第二批 ~200~500 行 + 零散订正）。
- 目标：全项目 **27.8% → 约 26%**（自研层 36% → 33%~34%）。**不建议承诺 20% 以下**——那意味着删契约。
- 工作量：**3 轮**（第一批 / 第二批 / 复核 + 文档同步），每轮 8~12 文件；按 2026-09-29 经验，单轮「改动 + 全量验证」为**半天到一天**量级（不含用户评审时间）。
- 若追加 phtty 式 **Doxygen 规范化**（本地现无 `Doxyfile`，无 `AGENTS.md`）：属**增量工程**——phtty 仅 B1 单批就动 15 个契约头、+616 处标签；本地 254 个文件全量补齐远超精简工作量，**建议单独立项、不与精简混做**。

### 4.5 每批验证步骤（沿用项目 A/B 纪律的具体命令思路）

```bash
# 0. 归档改前源码（纯注释变更也要可回溯）
tar -czf /tmp/trim_<批号>_pre.tar -C <仓库根> <本批文件列表>

# 1. 只改注释（不改任何代码/宏/字符串）

# 2. token 级「只删不改」校验（复用现成脚本）
python3 .analysis/cleanup_20260929/check_token_equivalence.py   # 断言：改后 token 流 = 改前子序列

# 3. 非注释差异行扫描：diff -u 改前 vs 改后，过滤注释行后必须 0 差异

# 4. 重建「改前源码」，与现行源码用与 make 完全相同的 flags 各编译一次
python3 .analysis/cleanup_20260929/round2_ab_reconstruct.py     # 或按批自定

# 5. 段级 md5（排除 .debug* / .comment / .ARM.attributes）+ 反汇编逐行对照
bash .analysis/cleanup_20260929/round2_ab_compare.sh            # 模板，改文件列表即可

# 6. 全量重编一次（默认口径 ALL/1_263/STD），核对：
#    - arm-none-eabi-size -A 各段与改前一致（注释不影响段尺寸）
#    - 告警仍为 3 条既有 HAL
# 7. 按影响面回归宿主脚本（.analysis/22_1665/verify_all.sh --fast、.analysis/myanmar/mm_frame_sim.py 等）
```

### 4.6 为什么「注释不应改变目标文件」以及如何证实/证伪

- **原理**：C 翻译阶段 3（预处理）会把注释替换为空白——注释不进入 token 序列、不参与条件编译、不产生符号。因此**非调试段（`.text/.rodata/.data/.bss/.ccmram` 等）应逐字节不变**；**`.debug_*` 必然变**（DWARF 行号表），所以对照必须排除它（`round2_ab_compare.sh` 已这么做）。
- **三个边界条件**（需在脚本层核查）：
  1. `__LINE__`：自研层唯一命中 = `Core/Inc/stm32f4xx_hal_conf.h:493` 的 `assert_param` 宏定义——HAL 断言行号由各 HAL 调用点展开，本轮范围不含 HAL 调用文件，可忽略；若有精简触及调用 `assert_param` 的自研文件，须单独比对。
  2. 行尾反斜杠续行的注释（`// ... \`）删除时可能改变后续行的归属——脚本需扫描本批文件是否含此写法。
  3. 注释被当 token 分隔符的极端写法（`a/**/b`）——本地 0 处。
- **证实**：§4.5 的 2/3/5/6 全绿 = 纯注释变更（2026-09-29 已对本项目 4 文件实测通过：105 段 / 1801 行反汇编逐行一致）。
- **证伪**：任何一步出现差异 = 有非注释改动混入（这正是校验机制的价值）；反之，反汇编/段尺寸一致而 token 校验失败 = 存在「改写而非删除」（值相同但文本不同），仍应视为不纯。

### 4.7 「找不到 phtty」预案（本条不适用，供追溯）

phtty 已定位并取到最新数据。若未来克隆失败：本仓库 `upstream` remote 已配置，`git -c http.proxy=http://127.0.0.1:7897 fetch upstream` 即可；再失败则请用户提供仓库地址。**本报告全部本地侧分析（§2/§3/§4.3~4.6）不依赖 phtty**，结论独立成立。

---

## 5. 需要用户提供 / 裁决的信息

1. **目标口径**：要「**减少注释量**」还是「**phtty 式订正 + Doxygen 规范化 + 知识外移**」？（决定工作量差一个数量级）
2. **在飞改动的处理**：61 个 M 文件与缅甸模块等 8 个未跟踪项何时提交/冻结？注释精简必须基于稳定基线。
3. **是否进入实施**：本报告为评估；如需实施，建议先做第一批 8 文件 + A/B 验证样板。
4. **Doxygen 化的取舍**：是否引入 `Doxyfile` + 契约头标签体系（phtty 路线）？本地 doc/ 体系已重，两者需明确分工。

---

## 附录

### A. 本地 vs phtty（main）同名文件注释增量 Top 15

| 文件 | 本地注释 | phtty 注释 | Δ | 本地% | phtty% |
|---|---|---|---|---|---|
| Application/Src/app_dispatch.c | 229 | 114 | +115 | 43.7% | 47.9% |
| Application/Src/app_render.c | 182 | 80 | +102 | 24.5% | 17.3% |
| Application/Src/Channel/app_udp.c | 130 | 29 | +101 | 22.9% | 18.2% |
| Application/Src/LDI/app_ldi_cmd.c | 427 | 364 | +63 | 41.5% | 44.0% |
| Application/Src/app_test.c | 84 | 32 | +52 | 38.5% | 31.4% |
| Device/Display/dev_display.c | 66 | 18 | +48 | 24.4% | 14.3% |
| Device/Inc/dev_display.h | 89 | 41 | +48 | 65.4% | 54.7% |
| Application/Inc/app_render.h | 121 | 84 | +37 | 65.8% | 63.2% |
| Application/Src/RLS/app_rls_cmd.c | 38 | 2 | +36 | 21.0% | 3.4% |
| Application/Src/LDI/app_ldi.c | 145 | 110 | +35 | 29.2% | 27.3% |
| Platform/Src/pl_uart.c | 52 | 20 | +32 | 23.7% | 15.7% |
| Platform/Inc/pl_gpio.h | 36 | 5 | +31 | 66.7% | 23.8% |
| Application/Src/app_boot.c | 40 | 15 | +25 | 21.4% | 13.2% |
| Application/Src/Channel/app_rs232.c | 46 | 22 | +24 | 38.3% | 17.3% |
| Platform/Src/pl_gpio.c | 25 | 5 | +20 | 30.1% | 12.5% |

（注：`pl_gpio.h`/`dev_light_sensor.h` 这类「本地单方面补的接口文档」正是 phtty 后来在 B 类里补的同类内容——方向一致，只是时序不同。）

### B. phtty 关键提交（可用于 `git show` 复现）

```
d542f65  docs: 新增 Doxyfile 与《注释与 Doxygen 改造方案》
30703e3  docs: 订正 A 类陈旧注释（数值 / 过期阶段 / 路径 / 调试残留）
bb38080  docs: B0 为 27 个文件补 @file/@brief 文件头
19bd71a  docs(B1): 15 个契约头补齐 Doxygen 标签（含收尾）
3c00233  docs(B2): 导出符号文档补齐（40+ 头文件 + 参数文档统一）
688f65a  docs: 工程指引 CLAUDE.md → AGENTS.md 重写；新增 docs/架构说明.md
```
（均在 `upstream/refactor/multi-variant`；文档：`upstream/refactor/multi-variant:docs/命名约定.md` §12、`docs/架构说明.md`。）

### C. 复现命令

```bash
# 本地统计（本报告 §2 数据）
python3 .analysis/audit_20261008/scripts/comment_stats.py --root . \
  --dirs Application Device Platform Kernel Core Compiler --top 20 \
  --label "本地工作区" --json .analysis/audit_20261008/local_stats.json

# phtty 对照（§1.3 数据）
git archive upstream/main Application Device Platform Kernel Core Compiler | tar -x -C /tmp/phtty_main
python3 .analysis/audit_20261008/scripts/comment_stats.py --root /tmp/phtty_main \
  --dirs Application Device Platform Kernel Core Compiler --top 15 --json /tmp/phtty_main_stats.json

# 同名文件对比（附录 A）
python3 .analysis/audit_20261008/scripts/compare_stats.py \
  .analysis/audit_20261008/local_stats.json /tmp/phtty_main_stats.json --top 30
```

*（报告完；所有引用行号以 2026-10-08 工作区实况为准。）*
