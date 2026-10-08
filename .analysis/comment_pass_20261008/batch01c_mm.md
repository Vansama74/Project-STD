# 批 1c：缅甸 2D 费显模块（MM）注释规范化 + doc/16 迁移 — 报告

- **日期**：2026-10-08
- **规范依据**：`doc/18_注释规范.md`（批 0 产物）
- **范围**：`Application/Inc/ProtocolParser_Myanmar/`（1 个 `.h`）+ `Application/Src/ProtocolParser_Myanmar/`（2 个 `.c`）
  + `Application/Src/app_factory_test_mm.c`（缅甸版出厂测试，doc/16 §13）
  + `doc/16_缅甸2D费显协议/README.md`（注释事实迁移落点）
- **性质**：**只动注释**（代码 token 零改动、机器码零变化）；文档为事实落点核对/更正；构建全部落
  `/tmp/comment_pass/`，未覆写 `build/`
- **状态**：工作区改动**未提交**（留待用户抽审）；批次产物 = 本目录（报告 + 归档 + 脚本 + 日志）+ `doc/16` 增量
- **流程样板**：`.analysis/comment_pass_20261008/batch01b_gz_ol.md`（批 1b：GZ_OL）

---

## 1. 文件清单与规模

| 文件 | 行数（前 → 后） | 注释行（前 → 后） | 文件头（前 → 后） | git diff（+ / −） |
|---|---|---|---|---|
| `app_mm_proto.h` | 171 → **122** | 109 → **60** | 29 → **10** | +26 / −75 |
| `app_mm_proto.c` | 257 → **220** | 89 → **52** | 19 → **9** | +27 / −64 |
| `app_mm_proto_cmd.c` | 217 → **189** | 93 → **65** | 18 → **10** | +22 / −50 |
| `app_factory_test_mm.c` | 76 → **76** | 30 → **30** | 10 → **10** | +3 / −3 |
| **合计** | 721 → **607** | **321 → 207（−114）** | — | **+78 / −192** |

- 三个文件头超限（29 / 19 / 18 行）全部压到 **≤10 行**（`app_factory_test_mm.c` 原已合规，仅修正 1 处失效措辞）。
- 统计口径 = `.analysis/comment_pass_20261008/scripts/batch01c/stats_comments.py`（有状态块注释判定；
  文件头 = 文件起始首个 `/** ... */` 块，`pre` 读归档 / `cur` 读工作区），日志
  `logs/batch01c_stats_pre.log`、`logs/batch01c_stats_cur.log`。

## 2. 逐文件改了什么

### 2.1 `app_mm_proto.h`（协议事实注释主体 → 契约 + 指针）

| 位置 | 改前 | 改后 |
|---|---|---|
| 文件头 | 29 行：协议原文出处、帧逐偏移字节序、文档 5 命令表与自相矛盾、灯位映射与 `0x0B` 存疑、输出层引脚/极性/接管/副作用、通道与命名纪律 | 10 行：帧结构一行式（`FF E0` + 命令 + 12B + 5B ⇒ 定长 20B、无长度/无校验/无应答）+ 只解析三命令 + 干接点引脚/极性/红叉默认 + 只绑 RS232/9600/箭头交换不持久化 + doc 指针（§2、§3、§6、§7、§8、§13、§14） |
| `MM_TAIL_SIG_*` 前注释 | 4 行（正签名定义 + 同源 8051 XOR 推导 + 误拒风险） | 2 行：正签名 = `[15..18] == 0x00`，**末字节 `[19]` 不参与**（8051 XOR 算出但从不比对）+ 指针 §2.4/§6 |
| `MM_QUEUE_DEPTH` / `MM_MSG_SIZE` | 无注释 | 行尾一行式（深度 3 = 文档「先到先处理」语义；单消息 = `frame_msg_t` + 定长载荷） |
| `MM_LAMP_COUNT` | 无注释 | 行尾一行式（灯位总数/遍历上界） |
| `mm_parse_frame` | 9 行（纯函数语义 + probe 层分工 + 参数/返回域） | 6 行标准形（`@brief`/`@param`/`@return`/单条 `@note`） |
| `mm_execute_cmd` | 5 行；**指针名笔误** `_mm_apply_lamp()`（实际为公开 API `mm_apply_lamp()`） | 4 行：修正名称 + 交换单点 `_mm_effective_lamp()` + 指针 §13 |
| `mm_arrow_map_toggle` | 19 行（默认/交换态逐条语义、三条不变量、消费者、矩阵） | 4 行：状态/返回/不持久化 + 消费者 + 指针 §13（细节归 doc） |
| `mm_apply_lamp` | 12 行（映射表内容复述 + 现场证据） | 5 行：单一映射点 + 改硬件只改 `s_mm_lamp_map` + `_mm_drive()` + 指针 §3 |
| `mm_apply_init` | 15 行（源 8051 全灭语义对比 + 五步顺序 + 影子状态 + 用户需求 3） | 5 行：接管顺序 ①~⑤ 一行式 + 不硬编码/不碰 NVIC + 不持久化 + 指针 §14.2/§4.2 |
| `mm_arrow_map_is_swapped` / `mm_proto_handle_task` / `mm_proto_init` | 已合规 | 保留（未改） |

### 2.2 `app_mm_proto.c`（注册 / 探测 / 帧处理任务）

- 文件头 19 → **9 行**：只绑 `CH_ID_RS232`（含禁绑 `RS232_1`）+ 9600/DIP1 置 OFF + `0xFF` 重叠的
  正签名判别（双向不误吞、不卡链、无需 EIDE 互斥）+ 指针（§5、§6、§7、§10、§14）；
  原「帧事实见 app_mm_proto.h 文件头」的转述改为指针。
- 队列体/控制块 CCMRAM 注释：保留 2 行契约（`PROTO=ALL` SRAM 余量放不下 + CPU 独占、不经 DMA/ETH），补指针 §6。
- 「开机默认画面」章节盒说明 7 → 2 行（仅带显示驱动注册 + 几何不硬编码 + 单槽首个注册生效）。
- `_mm_cross_px` 文档 7 → 1 行（静态平凡 helper，按规范就地压缩）。
- `_mm_default_display_init` 文档 8 → 4 行（无显示不注册 → 回落守卫跳过路径，指针 §10.2）。
- `mm_proto_init` 文档 9 → 4 行：**修正漂移** —— 原「六步」漏列 `mm_apply_init()` 接管干接点一步，
  改为 as-built 次序（acquire → register → bind → 注册默认画面 → 接管干接点（建任务前）→ 建队列 → 建任务），
  `@warning` 折入 `@note`（规范标签集不含 `@warning`）。
- `mm_probe_frame` 文档 14 → 6 行（PURE 契约 + 三返回语义 + `*total_len` 写出域 + 指针 §5④/§6，
  另给 doc/05-01 §3.2 规则 3 指针）。
- probe 内联正签名注释 12 → 3 行：保留「为什么必须正签名」的 why（治超 `byte[1]`=帧长 0xE0 半帧时
  SC_OL WAIT → 无条件 SKIP 会截断治超帧），细节归 §5④。
- `mm_proto_init` 内联注释：2 处共 5 行 → 3 行（默认画面注册 + 干接点接管时序「建任务前」保留为 why）。

### 2.3 `app_mm_proto_cmd.c`（帧解析 + 干接点输出唯一映射点）

- 文件头 18 → **10 行**：干接点引脚/极性/红叉默认 + 接管顺序无毛刺 + **禁止 `HAL_NVIC_DisableIRQ`** +
  单一映射点 + RLS 副作用（指针 §4.3）+ 指针（§3、§4、§6、§13、§14）；删去旧「①②③ 文件内容目录」式描述。
- 极性注释：保留 2 行契约（高有效 + 现场实测证据），指针由 `doc/16 §4.1` 改为全路径。
- `_mm_drive` 文档：修正 `@param m 灯位映射项；@param on …` 挤在一行的写法 → 2 个 `@param` 分行。
- `_mm_drive` 内联：2 → 1 行（电平唯一真源 `on != m->active_low`）。
- `mm_apply_lamp` 文档：4 行（已合规，仅参数排版对齐）。
- `mm_apply_init` 文档 8 → 4 行；体内 ① 注释 3 → 1 行、⑤ 注释 3 → 2 行（①③④ 一行式保留，无毛刺顺序契约不丢）。
- `_mm_effective_lamp` 文档 14 → 4 行（唯一生效点 + 红灯原样返回 + 指针 §13.2）。
- `mm_parse_frame` 文档 8 → 1 行；`mm_execute_cmd` 文档 6 → 4 行。
- 未改（已合规/契约）：`s_mm_lamp_map` 映射表本体与 3 条 `active_low`、`s_mm_lamp_state` 影子状态注释、
  `s_mm_arrow_swapped`（volatile 理由 + 不持久化，3 行）、`mm_lamp_map_t` 文档、①/①b/② 章节分隔注释。

### 2.4 `app_factory_test_mm.c`（缅甸版出厂测试）

- 文件头 10 行保持合规，修正 1 处失效措辞：`语义/日志与现场测试矩阵` → `语义与现场测试矩阵`
  （`[mm]` RTT 日志已随 2026-09-29 瘦身轮删除），并补「**不引用任何显示 API**（无显示构建安全）」
  （doc/16 §13.3 已有此断言，源码此前缺一行契约）。
- 其余（`g_factory_test` 契约注释、`mm_factory_monitor_task` 文档、空实现/hook 文档）本就符合规范 → 0 改动。

## 3. 源码注释 → doc 落点映射（可核查，30 条）

> 核对方式 = `grep` 关键字 + 抽读章节；「本轮补/更正」= 先改 `doc/16` 再精简源码。

| # | 源码注释（改前携带的事实） | doc/16 落点 | 状态 |
|---|---|---|---|
| 1 | 帧 = `FF E0` + 命令 1B + 内容 12B(DATA0~11) + 结束码 5B ⇒ 定长 20B、字节序 | §2.2 帧格式表 | 已在册 |
| 2 | 无校验 / 无长度字段 / 无应答 | §2.2 + §2.3「应答」段 + §2.3 帧切分依据 | 已在册 |
| 3 | 文档 5 命令（0x10/0x40/0x80/0x90/0x0A）、`0x0B` 文档不存在、0x0A 标题注文矛盾 | §2.3 命令表 + §8 Q3 | 已在册 |
| 4 | 只解析 `0x90`/`0x0A`/`0x0B`（DATA0：1 开 / 0 关） | §1 + §2.3 + §3 | 已在册 |
| 5 | 非本模块命令（0x10/0x40/0x80）整帧 SKIP、不动作 | §2.3 + §6 ⑥ | 已在册 |
| 6 | probe 九条判据（含 `avail==0` FAKE、首字节/地址快拒、等齐 20B、DATA0 越界 SKIP） | §6 判定表 ①~⑨ | 已在册 |
| 7 | 正签名 = `[15..18] == 00 00 00 00`；末字节 `[19]` 不判别（8051 XOR 算出但从不比对） | §2.4 + §6 末两条 | 已在册 |
| 8 | SC_OL 同槽 `0xFF` 重叠：WAIT/链式/正签名 → 双向不误吞；残余风险 (1/256)^4 | §5 四层判别链 + §7.2 | 已在册 |
| 9 | 无需 EIDE 互斥、不定义 `g_brace_proto_guard`（非 `{` 帧族） | §5 结论 + §7.2 | 已在册 |
| 10 | 只绑 `CH_ID_RS232`；**禁绑** `CH_ID_RS232_1`（USART6 语音 TX） | §7.2 勾选项 + doc/05-01 §6 兼容矩阵行 | 已在册 |
| 11 | 9600 8N1 靠 DIP1 置 OFF（不改波特率逻辑） | §7.3 | 已在册 |
| 12 | 干接点映射 红灯=S1/PE12、左下=S2/PE11、右下=S3/PE10 | §3 映射表 + §4.1 | 已在册 |
| 13 | 推挽、**高电平有效（点亮=输出高）**、现场证据（复位灯亮 + PE12 3.3V） | §3 + §4.1 + §8 Q1 | 已在册 |
| 14 | 上电默认红叉 = 红灯点亮（取代「三路全灭」；`0x90 00` 仍可关） | §3 + §4.1 + §14.2 | 已在册 |
| 15 | 接管顺序（先写灭电平 → 按线摘 EXTI → 切推挽 → 点亮），无毛刺 | §3「上电初始化顺序」+ §4.2 ③ | 已在册 |
| 16 | 禁 `HAL_NVIC_DisableIRQ`；逐线摘除 `pl_gpio_release_exti`（不碰其它线/NVIC） | §4.2 步骤 2/3 | 已在册 |
| 17 | 映射单一改动点 = `s_mm_lamp_map` + `_mm_drive()` | §3「映射表 + 动作函数是同一个改动点」 | 已在册 |
| 18 | 箭头交换语义（默认/交换态、可来回）、红灯豁免、不持久化、开机复位 | §13.1 / §13.2 | 已在册 |
| 19 | 交换生效单点 = `mm_execute_cmd()` → `_mm_effective_lamp()`；probe/解析/队列零改动 | §13.2 影响层表 | 已在册 |
| 20 | 消费者 = 缅甸版厂测（TEST 键，与标准版二选一编译） | §13.3 | 已在册 |
| 21 | 默认画面红叉：整屏清黑 + 两条对角线、`COLOR_RED`、线宽 `max(2, h/24)`、整数插值 | §14.3 | 已在册 |
| 22 | 红叉几何取自 `dev_display_get()`（无硬编码分辨率、不依赖字库） | §14.3 | 已在册 |
| 23 | 红叉仅带显示驱动注册 + 回调判空（无显示纵深防御、零告警） | §10.2 #15 + §10.3 硬断言 + §14.3 | 已在册 |
| 24 | 单槽首个注册生效 ⇒ `PROTO=ALL` 画面不变 | §14.3「作用域」 | 已在册 |
| 25 | 队列深 3、`MM_MSG_SIZE = sizeof(frame_msg_t) + 20`、队列体+cb+任务帧缓冲 CCMRAM、SRAM 侧 16B | §6 末尾 + doc/05-01 §4.1 行 | 已在册 |
| 26 | 任务栈 + TCB 静态落 CCMRAM（256 words、`osPriorityNormal`、只创建一次） | §7.2 勾选项 + §3 任务行 | 已在册 |
| 27 | RS232 槽体 weak 合并；本模块不 provide RS485/RJ45 | doc/05-01 §4.1 变更记录 + §6 行 | 已在册 |
| 28 | **注册次序**（含 `mm_apply_init()` 一行：建任务前完成，避免上电沿未定义灯态窗口） | §7.1 `app_mm_proto.c` 行 | **本轮更正**（旧文「六步」漏列该步） |
| 29 | 命名前缀 `mm_`/`MM_` 与 doc/12 旧方案 `app_md_*` **不复用、不混用** | 顶部「模块」行 | **本轮补** |
| 30 | 厂测文件契约：同名对外符号 ⇒ 同编 `multiple definition`；`app_factory_mode_interrupt` 空实现 / `app_factory_test_active` 恒 false / `g_factory_test`；不引用显示 API | §13.3 符号对照表 + 勾选项 | 已在册 |

**映射结论**：30 条中 **28 条已在册**、**1 条本轮补**（#29）、**1 条本轮更正**（#28）；
另有 1 处源码失效措辞修正（厂测文件头「日志」字样，RTT 已删除 → 归 §13.3/顶部横幅事实）。

## 4. `doc/16` 变更清单（章节号 + 来源）

| 章节 | 变更 | 来源 / 理由 |
|---|---|---|
| 顶部「模块」行 | 追加「与 doc/12 未实施的旧缅甸方案 `app_md_*` **不复用、不混用**——2026-10-08 注释规范化批 1c 迁移补充」 | 来源 = `app_mm_proto.h` 文件头；doc/12 为未实施方案（命名纪律落点） |
| §7.1 文件清单 `app_mm_proto.c` 行 | 「六步注册（**只绑 `CH_ID_RS232`**）+ probe + …」→ **「注册次序（as-built）：acquire RS232 槽 → `app_proto_register`（probe）→ `app_proto_bind_channel(CH_ID_RS232)` → 注册开机默认画面 → `mm_apply_init()` 接管干接点（建任务前完成，避免上电沿出现未定义灯态窗口）→ 建静态队列 → `app_proto_set_frame_queue` + 建处理任务」** | 来源 = `app_mm_proto.c` `mm_proto_init()` 实读；旧文「六步」漏列 `mm_apply_init()` 一步（注释规范化迁移补充） |
| §12 修订记录 | 追加「2026-10-08（注释规范化批 1c：缅甸 MM 模块，本轮）」行：4 文件注释规范化（只动注释、A/B/全量重编证据指针）+ 上述两处文档更正 + 源码保留的契约行清单 | 模块修订记录纪律（批 1b 同款） |

- `doc/16` 行数 788 → **790**、`git diff` = **+4 / −2**、md5 `56889429…` → `7226be1b…`；
  **其它章节一字未改**（含 §2~§6 协议事实、§13/§14 需求记录、历史修订行）。

## 5. 验证结果（全部通过）

### 5.1 A/B：改前（归档 = 基线 `c624915` 内容）vs 改后，同 flags 编译

脚本 `.analysis/comment_pass_20261008/scripts/batch01c/ab_check_mm.sh`（flags 逐字取自 `make -n` 的 MM 编译命令；
唯一差异 = 模块头文件搜索路径指向「改前头文件影子目录」或「仓库现行目录」，影子头 = 归档 `app_mm_proto.h`）；
日志 `logs/batch01c_ab.log`。

| TU | strip-debug 对象 md5（前 = 后） | 段级（排除 `.debug*` 等） | 反汇编 | 非调试段源码路径串 | 编译告警 |
|---|---|---|---|---|---|
| `app_mm_proto.c` | `3005b165acce0fe84679349f06ab0db3` ✔ | 19 段逐字节一致 ✔ | 265 行逐行一致 ✔ | 0 处 ✔ | 0 |
| `app_mm_proto_cmd.c` | `182090b56f9e261f225d71907614f5c8` ✔ | 15 段逐字节一致 ✔ | 185 行逐行一致 ✔ | 0 处 ✔ | 0 |
| `app_factory_test_mm.c` | `0ca905f64f7cf2b7e0b62a2144c4da3e` ✔ | 13 段逐字节一致 ✔ | 75 行逐行一致 ✔ | 0 处 ✔ | 0 |

附：整对象 md5（含 `.debug*`）前/后不同属预期（注释行号移动 ⇒ 调试段变化）：
`app_mm_proto.c` `af2e1c0f…` → `2f3efa36…`、`app_mm_proto_cmd.c` `8d4689e5…` → `5facda04…`、
`app_factory_test_mm.c` `a8cddee3…` → `5bec8cce…`。

### 5.2 仅注释变化（代码 token 零改动）

`scripts/batch01c/code_only_diff.sh`（`arm-none-eabi-gcc -fpreprocessed -dD -E -P` 去注释 → 空白折叠 → 逐字节比较）；
日志 `logs/batch01c_code_only.log`：

```
✔ app_mm_proto.h / app_mm_proto.c / app_mm_proto_cmd.c / app_factory_test_mm.c   去注释 token 流逐字节一致
######## 结果：仅注释变化（代码 token 零改动）
```

### 5.3 `git diff` 逐文件复核

`scripts/batch01c/diff_comment_only.py`（逐 hunk 行号 → 改前/改后**有状态**注释剥离；
每条被标记 +/- 行的代码残余必须为空，或与改前版本的代码文本一致；另比对**非空代码残余多重集相等**）；
日志 `logs/batch01c_diff_review.log`：

```
✔ app_mm_proto.h        +26 / −75（其中行尾注释改写 6 行 = 3 行 × 前后各一次）
✔ app_mm_proto.c        +27 / −64
✔ app_mm_proto_cmd.c    +22 / −50
✔ app_factory_test_mm.c +3 / −3
######## 结果：+78 / −192，仅注释变化 ✔
```

### 5.4 全量重编（`PROTO=ALL DISP=1_263`，独立 BUILD_DIR）

| 项 | 结果 |
|---|---|
| 命令 | `make PROTO=ALL DISP=1_263 -j8 BUILD_DIR=/tmp/comment_pass/all1263_b01c` → **exit 0**，`Build complete` |
| 段尺寸 | `.text 167944` / `.rodata 195776` / `.data 1656` / `.ccmram 39652` / `.bss 124768` / `._user_heap_stack 2560` —— 与任务书基线**逐项一致** |
| 告警 | **3 条**（全部为既有 HAL `stm32f4xx_hal_flash_ex.c -Wunused-parameter 'Banks'`），**零新增**；`error` 0 条 |
| elf md5 | `7e13a5a120e873c08e121dc34b42bb4b`（md5 因 `__DATE__/__TIME__` 横幅随构建时间变化；此处同口径可对拍） |
| 隔离 | 日志 `logs/batch01c_build_all1263.log`；`build/` **未被触碰**（`find build -newermt '2026-10-08 12:20'` 为空） |

### 5.5 宿主推演、守卫审计与文档指针

| 检查 | 命令 | 结果 |
|---|---|---|
| MM 宿主推演 | `python3 .analysis/myanmar/mm_frame_sim.py` | **157 / 157 通过、失败 0**（含 §9 引脚/极性/接管顺序、§10 交换语义、§11 红叉几何 + 源码不变量断言；日志 `logs/batch01c_mm_sim.log`） |
| 无显示守卫审计 | `python3 .analysis/myanmar/check_display_guards.py` | **16/16 硬断言 + 95/95 调用点分类（0 处待确认）**（红叉注册/回调两处判空断言通过；日志 `logs/batch01c_display_guards.log`） |
| doc 指针可解析 | `python3 .analysis/comment_pass_20261008/scripts/batch01c/check_doc_pointers.py` | **7 / 7 通过**（`doc/16` §2~§14 逐章节 + `doc/05-01` §3.2；日志 `logs/batch01c_doc_pointers.log`） |

### 5.6 边界核查

- `__LINE__` / `__FILE__`：本模块 3 文件 + 厂测文件 **0 命中**（`grep -rn` exit=1，归档与现行两组均核）；
  A/B 脚本另有「非调试段无源码路径串」断言 ✔。
- 注释块长度上限（规范 §2.4/§3/§4）：文件头 10/9/10/10 行（≤10）；函数注释最长 6 行（`mm_parse_frame` / probe，≤8）——
  统计见 `logs/batch01c_stats_cur.log`。
- 本批**未执行任何 git 写操作**（仅 `git show/diff/status`）；未烧录；未改 `.eide/eide.yml`（也无需 `Reload Project`）；
  未改批 1a/1b 的 9 个在飞文件与 `doc/14`/`doc/15`（mtime 12:05~12:18，均早于本批开工 12:21）；未改 `build/`。
- 归档完整性：`git show HEAD:<path>` 逐文件 `cmp` 通过（见 §8）。

## 6. 保留的契约行清单（源码一行式，逐条）

> 「契约」= 不看就会写错 / 改错会坏正确性的约束；下图每条均在源码保留为一行（或 2 行内的紧密一行式）。

**`app_mm_proto.h`**

1. 文件头：帧 = `FF E0` + 命令 1B + 12B(DATA0~11) + 结束码 5B ⇒ **定长 20B、无长度字段、无校验、无应答**；
   只解析 `0x90`/`0x0A`/`0x0B`（DATA0：1 开 / 0 关），其它命令整帧 SKIP。
2. 文件头：干接点 红灯=S1/PE12、左下=S2/PE11、右下=S3/PE10，推挽**高电平有效**，**上电默认红叉**（红灯亮）。
3. 文件头：**只绑 `CH_ID_RS232`**（禁绑 `CH_ID_RS232_1`）；9600 靠 **DIP1 置 OFF**；箭头交换不持久化。
4. `MM_TAIL_SIG_*`：正签名取 `[15..18]` 四字节 == `0x00`；**末字节 `[19]` 不参与判别**（8051 XOR 算出但从不比对）。
5. `MM_QUEUE_DEPTH`/`MM_MSG_SIZE`：深度 3（文档「先到先处理」缓存语义）；单消息 = `frame_msg_t` + 定长载荷。
6. `mm_parse_frame`：纯函数；`raw_len ≠ MM_FRAME_LEN` → `parsed=false`；不校验结束码（判别在 probe）。
7. `mm_execute_cmd`：无应答 / 无显示 / 无语音；落点经 `_mm_effective_lamp()` 交换单点。
8. `mm_arrow_map_toggle`：`0x0A` ⇄ `0x0B`；**红灯 `0x90` 不受影响**；**不持久化**（`mm_apply_init()` 开机复位）。
9. `mm_apply_lamp`：引脚/极性**只在 `s_mm_lamp_map` + `_mm_drive()` 里改**。
10. `mm_apply_init`：接管顺序 ①灭电平（映射表推导、不硬编码）→ ②**按线**摘 EXTI（不碰其它线/NVIC）→
    ③切推挽输出 → ④影子状态 → ⑤点亮红灯（唯一动作点）；同时复位箭头交换开关（不持久化）。

**`app_mm_proto.c`**

11. 文件头：只绑 RS232；9600/DIP1 OFF；`0xFF` 与四川治超重叠 → 非本模块命令须**正签名**才 SKIP，
    双向不误吞、不卡链、**无需 EIDE 互斥**。
12. 任务存储：栈 + TCB 落 CCMRAM（创建一次、永不退出、**不占 ucHeap**）；仅 CPU 访问、不经 DMA。
13. `RB_PROVIDE_WEAK`：RS232 槽体与同槽协议 weak 合并；**本模块不 provide RS485/RJ45**。
14. 队列体 + 控制块置 CCMRAM（`PROTO=ALL` SRAM 余量放不下；CPU 独占、不经 DMA/ETH）。
15. `mm_probe_frame`：PURE（只 `rb_peek`）；`*total_len` 只在 SKIP / READY 写出；三返回语义（READY/SKIP/FAKE）。
16. probe 内联：非本模块命令须正签名才 SKIP，**否则 FAKE 不消费**（防截断同槽治超帧）。
17. probe 内联：定长帧等齐 20B 再判（避免半帧误判命令）；本模块命令但 DATA0 > 1 → 整帧消费。
18. 红叉回调：`if (d == nullptr) return;`（无显示纵深防御）；注册点 `dev_display_get() != nullptr` 守卫。
19. 接管干接点**在建任务前完成**（避免上电沿出现未定义灯态窗口）。

**`app_mm_proto_cmd.c`**

20. 文件头：高有效、上电默认红叉、接管顺序无毛刺、**禁止 `HAL_NVIC_DisableIRQ`**、单一映射点、
    RLS 副作用（量产排除 RLS 即无影响）。
21. 映射表注释：现行极性 = **高有效**（`active_low=false`；复位灯亮 + PE12 3.3V ⇒ 3.3V = 点亮）。
22. `mm_lamp_map_t`：改硬件（脚位/电平/驱动方式）**只改本表 + `_mm_drive()`**。
23. `_mm_drive`：**电平唯一真源**（`high = on != m->active_low`）。
24. `s_mm_arrow_swapped`：`volatile`（厂测任务写 / 帧任务读）；**不持久化**、开机复位。
25. `_mm_effective_lamp`：交换唯一生效点；**红灯原样返回**（只有 LEFT ⇄ RIGHT 两支）。
26. `mm_apply_init`：①~⑤ 顺序 + ①② 不硬编码电平；⑤ 只经唯一动作点（不新增硬编码、不破坏无毛刺顺序）。

**`app_factory_test_mm.c`**

27. 文件头：与标准版**二选一编译**（同名对外符号 ⇒ 同编 `multiple definition` 天然守卫）；
    选择入口 `FACTORY=STD|MM` / EIDE `excludeList`；**不引用任何显示 API**（无显示构建安全）。
28. `mm_factory_monitor_task`：TEST 键一次性消费（`dev_key_wait_press`）→ 切换映射。
29. 对外符号契约：`app_factory_mode_interrupt()` 空实现；`app_factory_test_active()` 恒 false
    （不占用显示资源、CQ 心跳照常）；`g_factory_test` 同名同义。

## 7. 遗留与待用户抽审点

1. **契约行取舍需抽审**：以下为「必须留源码一行」的判断，如需更精简可再议 —— 正签名判据（含末字节不判别的理由）、
   接管顺序 ①~⑤、`HAL_NVIC_DisableIRQ` 禁令、队列/任务栈 CCMRAM、高有效 + 现场证据、箭头交换不持久化、
   RLS 副作用、9600/DIP1、二选一编译契约。
2. **未动的代码格式残留（非注释，按纪律不碰）**：`app_mm_proto_cmd.c` 的 `mm_apply_lamp()` 函数体末尾
   1 个空行、`mm_arrow_map_toggle()` 内 2 个连续空行（RTT 打印删除后的遗留）；`diff` 检查会把这些
   显示为「空行 +/-」（本批未触碰，故不在 diff 内）。若允许「顺带清理空行」，可在下批统一处理。
3. **`MM_CMD_OTHER = 0xFF` 哨兵值**：非协议取值（仅枚举内部哨兵），注释已写「非本模块命令字」；
   未加 `_Static_assert`，与项目既有枚举口径一致。
4. **范围外文档漂移（只报告、未改）**：
   - `doc/05_协议模块多协议兼容优化/01_architecture.md:277`（MM × RLS 兼容矩阵行）：
     仍写「⇒ **上电默认全灭态 = 三路 SW 全读「闭合」**」——与同一行开头的「极性 2026-09-28 翻转为高有效」
     及 doc/16 §4.3 的「上电默认（红叉亮，2026-09-29 起）= SW1 读『未闭合』、SW2/SW3 读『闭合』」矛盾；
     RLS 最终结论（渲染「车道关闭」）不变，但括号内的状态描述已过时。**该文件不在本批范围，未改。**
   - `doc/16` §11「现场联调建议帧与预期现象」与 §13.4 表格中的 `[mm]` RTT 列、§3「诊断」段为历史记录
     （文首横幅已声明 RTT 设施已删除）——本批未清理，按「历史段落保留」处理；如需全量清理历史 RTT 措辞，请裁决。
5. 本批**未烧录、未提交、未改 git 配置、未改 `.eide/eide.yml`**；`build/` 未被触碰
   （构建全落 `/tmp/comment_pass/all1263_b01c`）。

## 8. 归档与证据清单

```
.analysis/comment_pass_20261008/
├── batch01c_mm.md                        ← 本报告
├── archive/batch01c/                     ← 改前源码（= 基线提交 c624915 内容，cmp 逐文件核对通过）
│   ├── Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h
│   ├── Application/Src/ProtocolParser_Myanmar/app_mm_proto.c · app_mm_proto_cmd.c
│   ├── Application/Src/app_factory_test_mm.c
│   └── doc/16_缅甸2D费显协议/README.md     （doc 改前基线，供 diff 复核）
├── scripts/batch01c/
│   ├── ab_check_mm.sh                    ← A/B（段级 md5 + 反汇编 + strip-debug + 路径串断言）
│   ├── code_only_diff.sh                 ← 去注释 token 流比较
│   ├── diff_comment_only.py              ← git diff 逐行「仅注释」判定
│   ├── check_doc_pointers.py             ← doc 指针 + 章节存在性检查
│   └── stats_comments.py                 ← 行数 / 注释行 / 文件头统计
├── ab/batch01c/                          ← A/B 中间产物（pre/cur 对象、.dis、token 快照、影子头文件）
└── logs/
    ├── batch01c_archive_md5.txt          ← 改前源码 md5 清单
    ├── batch01c_ab.log                   ← A/B 完整输出（3 TU 全绿）
    ├── batch01c_code_only.log            ← token 流比较（4 文件全绿）
    ├── batch01c_diff_review.log          ← diff 复核（+78 / −192）
    ├── batch01c_doc_pointers.log         ← 指针检查（7/7）
    ├── batch01c_mm_sim.log               ← 宿主推演（157/157）
    ├── batch01c_display_guards.log       ← 守卫审计（16/16 + 95/95）
    ├── batch01c_build_all1263.log        ← 全量重编（段尺寸逐项一致、3 条既有 HAL 告警）+ elf md5
    ├── batch01c_stats_pre.log / _cur.log ← 行数/注释行对照
```

**归档 md5（改前 = `git show HEAD:<path>`；改后供抽审对拍）**：

| 文件 | md5（改前） | md5（改后） |
|---|---|---|
| `Inc/ProtocolParser_Myanmar/app_mm_proto.h` | `c09f9946ab6662137e12b61ade34126e` | `c6ef5f72b6594c00eadfaaac439b406f` |
| `Src/ProtocolParser_Myanmar/app_mm_proto.c` | `f9ffbc1d5d3e3015239a343d00920da5` | `b7c2e32e3af14c50cc2e138325ed90be` |
| `Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c` | `20747e7e571be7cf909abe68a56b4b42` | `e556be31bb2aa44554f1838621015e34` |
| `Src/app_factory_test_mm.c` | `8e828ee14abec69e043ae656b0063542` | `b6def3c397201fb28689565616ecfb75` |
| `doc/16_缅甸2D费显协议/README.md` | `568894297cac2aaec0a98dc07d862d52` | `7226be1b53f0b285472bbedd4337ac08` |

**基线**：`c624915bf605f59a113351bbe1184c783f9625f1`（批 1a 开工前唯一一次 git 写操作；本批未做任何 git 写操作）。
