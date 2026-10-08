# 批 1b：贵州治超模块（GZ_OL）注释规范化 — 报告

- **日期**：2026-10-08
- **规范依据**：`doc/18_注释规范.md`（批 0 产物）
- **范围**：`Application/Inc/ProtocolParser_GuiZhou_Overload/`（3 个 `.h`）+ `Application/Src/ProtocolParser_GuiZhou_Overload/`（4 个 `.c`）
  + `doc/14_贵州治超屏协议/README.md`（注释事实迁移落点）
- **性质**：**只动注释**（代码 token 零改动、机器码零变化）；文档同步为事实落点核对/更正；构建全部落
  `/tmp/comment_pass/`，未覆写 `build/`
- **状态**：工作区改动**未提交**（留待用户抽审）；批次产物 = 本目录（报告 + 归档 + 脚本 + 日志）+ `doc/14` 增量
- **流程样板**：`.analysis/comment_pass_20261008/batch01a_yn_ol.md`（批 1a：YN_OL）

---

## 1. 文件清单与规模

| 文件 | 行数（前 → 后） | 注释行（前 → 后） | git diff（+ / −） |
|---|---|---|---|
| `app_gz_ol_proto.h` | 180 → **137** | 84 → **42** | +34 / −77 |
| `app_gz_ol_proto_cmd.h` | 7 → 7（未改） | 4 → 4 | — |
| `app_gz_ol_proto_parse.h` | 7 → 7（未改） | 4 → 4 | — |
| `app_gz_ol_proto.c` | 227 → **192** | 81 → **46** | +29 / −64 |
| `app_gz_ol_proto_cmd.c` | 384 → **337** | 131 → **85** | +43 / −90 |
| `app_gz_ol_proto_default.c` | 79 → **76** | 21 → **18** | +6 / −9 |
| `app_gz_ol_proto_parse.c` | 213 → **183** | 55 → **25** | +19 / −49 |
| **合计** | 1097 → **939** | **380 → 224（−156）** | **+131 / −289** |

- 5 个文件头全部压到 **≤10 行**（含 `/**` 与 `*/`）：`app_gz_ol_proto.h` 50 → **10** 行、
  `app_gz_ol_proto.c` 29 → **10** 行、`app_gz_ol_proto_cmd.c` 16 → **10** 行、
  `app_gz_ol_proto_parse.c` 24 → **10** 行、`app_gz_ol_proto_default.c` 13 → **10** 行。
- 两个薄转发头（`_cmd.h` / `_parse.h`）本就符合规范，**零改动**。

## 2. 逐文件改了什么

### 2.1 `app_gz_ol_proto.h`（协议事实注释主体 → 契约 + 指针）

| 位置 | 改前 | 改后 |
|---|---|---|
| 文件头 | 50 行：协议文档出处、帧格式逐偏移表、8 命令逐条语义（含源固件差异）、四通道、字节序沿革、内存纪律 | 10 行：帧结构一行式（`54 43 4C 59` + 长度 2 LE 整帧长 + 命令低字节 + `0x00`、无校验）+ BE16 契约 + `net_cfg.port` 双写 + `0x54` 唯一性 + doc 指针（§1、§3、§4、§13） |
| 帧常量宏 | 部分无注释（`GUIDE_LEN`/`PAYLOAD_MAX`/`QUEUE_DEPTH`） | 逐个一行说明（单位/取值域/消费者）；`FRAME_LEN_MAX` 保留「实测 158B 的 1.6× 余量 + SKIP 语义 + 与 RB 的编译期关系」 |
| `gz_ol_pcmd_t` / `gz_ol_parse_sta_t` | 成员行尾注释（多为长句） | 一行式语义；`ERR_CMD` = 静默丢弃、`ERR_PARAM` = 整帧丢弃（契约） |
| `gz_ol_display_t` | 成员注释（含「RGB→8 色映射，见 README Q7」无线索指针） | 一行式 + doc 指针（字号仅 16/24/32、字型回退宋体、颜色映射见 §1） |
| `gz_ol_setip_t.port` | 4 行（字节序 + 语义 + 裁决日期） | 2 行：**BE16 契约**（10028 = `27 2C`、`2C 27` = 11303）+ 写 `net_cfg.port`（专用 UDP 口 + TCP 业务口，§13） |
| 5 个公开 API | 注释齐备但 `@note` 偏长（含 Q 编号、守卫细节） | 标准形（`@brief` + `@param` + `@return`/`@note`）；`gz_ol_proto_init` 补「网口槽 acquire 失败静默跳过 + 禁绑 `CH_ID_RS232_1`」 |

### 2.2 `app_gz_ol_proto.c`（注册 / 探测 / 帧处理任务）

- 文件头 29 → **10 行**：四通道 + 禁绑 `RS232_1` + 9600/DIP1 + `0x54` 唯一（无需 EIDE 互斥、不定义 `g_brace_proto_guard`）+ RJ45 RB 只 acquire 不 provide + 指针（§1、§3、§4、§11、§13）；原「源固件 10028/广播差异」「双 mask 复核报告路径」等由 §11/§4/§13 承载。
- `PL_TASK_STATIC_STORAGE` 注释：4 → **2 行**（保留「一次性创建 ⇒ 可静态化、不占 ucHeap、CPU 独占无 DMA」契约 + 账本指针 doc/06-04 §8）。
- RB provide 注释：2 → **2 行**（保留「RJ45 槽不 provide = 避免第二份 1536B」）。
- 静态队列注释：4 → **3 行**（保留 **1136B CCMRAM = 792+80+264** + CPU 独占理由，账归 §6）。
- `gz_ol_proto_init` 文档：8 → **6 行**（修正原文「注册五步 × **三通道**」→ 四通道；保留禁绑契约）。
- `gz_ol_probe_frame` 文档：21 → **8 行**（10 步判定链外移 §3；保留 PURE 契约、`len > RB_SIZE_RS485-1` 必 FAKE 的防死等契约、SKIP 语义、`ch`/`aux` 未使用）。
- `gz_ol_proto_handle_task` 文档：4 → **6 行**（补「`msg->data` 为任务私有拷贝」契约——0x20 原地转换依赖）。

### 2.3 `app_gz_ol_proto_parse.c`（纯解析）

- 文件头 24 → **10 行**：解析纪律压缩为 4 行（长度/尾字节二次防御；`0x10/0x30/0x50/0x60` 载荷不限；`0x20` ≥17B、`0x40` ≥14B；字号仅 16/24/32）+ BE16 仅 0x40 字段 + 指针（§1、§13.5.1）。
- `_gz_ol_guide_match` / `_gz_ol_le16` / `_gz_ol_be16`：一行式保留（LE16 口径未变 / BE16 仅 0x40 的划分是契约）。
- `_gz_ol_font_type`（4 行 Doxygen → 1 行）、`_gz_ol_font_size`（6 → 1）、`_gz_ol_color`（6 → 1）——静态平凡 helper，按规范就地压缩。
- 函数体内联注释：字号 UB 行、屏宽/屏高忽略（`README Q9` → `§2 #4`）、`0x40` 下界、无载荷命令「长度不限 + 文档 0x30 帧例 17B 参数块依据」、`0x50` 入站兼容（`README Q3` → `§8 Q3`）、`0x70` 入站丢弃——**全部保留为一行式**。

### 2.4 `app_gz_ol_proto_cmd.c`（命令执行与应答组帧）

- 文件头 16 → **10 行**：显示/亮度/网络/渲染锁/单播回源五条一行式 + BE16 + 指针（§1、§3、§8、§10、§11、§13）。
- `_gz_ol_send_frame`：7 → **7 行**（标签逐一列出，保留「应答载荷最长 14B」契约）。
- `_gz_ol_net_payload`：12 → **7 行**（保留 port 双消费者 + `udp_port` 不参与 + BE16）。
- `_gz_ol_text_unescape_nl`：28 → **8 行**（保留 **GBK 安全行走**契约：尾字节 0x5C 不参与转义判定、首字节 0x81~0xFE + 合法尾字节整体拷贝、仅 `\n`、输出 ≤ 输入无额外缓冲；裁决与陷阱回归归 §1/§14）。
- `_gz_ol_exec_display`：15 → **7 行**（保留「字号不受屏高限制 + 交集裁剪 + 区域 = 剩余屏幕 + x/y 钳 0」契约，三层改动归 §12.6）。
- `_gz_ol_exec_full_color`：8 → **6 行**；`_gz_ol_exec_set_ip`：10 → **7 行**（保留「`0x50` 回显不是落盘证据 + 100ms 传输余量 + 重启生效」）。
- `_gz_ol_exec_search`：4 → **5 行**（补「记录无效 → 全 0 回显」一行契约）；`_gz_ol_exec_fault_query`：6 → **5 行**（保留「00 00 合规 + 源固件缺陷不复制」）。

### 2.5 `app_gz_ol_proto_default.c`（上电默认画面）

- 文件头 13 → **10 行**：三行文案 + 前导空格勿删 + FONT_16/HT/绿 + **单槽首个注册生效**竞争提醒 + 指针 §7。
- `_gz_ol_default_line` / `_gz_ol_default_show` / init：已符合规范，保留（仅 `_gz_ol_default_line` 之前的 `/* 源固件上电三行文案 */` 一行不变）。

### 2.6 未改文件

- `app_gz_ol_proto_cmd.h` / `app_gz_ol_proto_parse.h`：薄转发头（4 行文件头 + 转发 include），已符合规范 → **0 改动**（与批 1a 同判例）。

---

## 3. 源码注释 → doc/14 落点映射（可核查，30 条）

> 核对方式 = `grep` 关键字 + 抽读章节；「本轮补/更正」= 先改 `doc/14` 再精简源码。

| # | 源码注释（改前携带的事实） | doc/14 落点 | 状态 |
|---|---|---|---|
| 1 | 协议名/帧族/与贵州常规 `gz_`/`GZ_` 前缀区分 | 文首引言 + §1 | 已在册 |
| 2 | 帧格式逐偏移（引导串/序号/长度 2B 小端 = 整帧长/保留/命令 4B 低字节/`0x00`/无校验/帧长 = 17 + 载荷） | §1 帧格式表 | 已在册 |
| 3 | 8 命令语义（`0x10/0x20/0x30/0x40/0x50/0x60/0x70/0x80`） | §1 命令表 | 已在册 |
| 4 | `0x20` 载荷布局（X/Y/屏宽/屏高/字体名/字号/颜色/变长文本） | §1 载荷布局表 | 已在册 |
| 5 | 字体名 4 选 1、未命中回退宋体 | §1 载荷表 `8..11` 行 | 已在册 |
| 6 | 字号仅 16/24/32、其余整帧丢弃（源固件 UB 不复制） | §1 载荷表 `12..13` 行 + §2 #8 | 已在册 |
| 7 | 颜色映射 `FF0000→红 / 00FF00→绿 / 0000FF→黄 / 其余→红` | §1 载荷表 `14..16` 行 + §2 #5 | 已在册 |
| 8 | 屏宽/屏高读取后忽略 | §1 载荷表 + §2 #4 | 已在册 |
| 9 | **字号不受屏高限制**、行框下缘越屏按屏幕交集裁剪、区域 = 剩余屏幕 | §12.3 / §12.6（§1 命令表 `0x20` 行与载荷表 `12..13` 行原文仍是旧「整块丢弃/字号 ≤ 屏高」） | **本轮更正**（§1 两处） |
| 10 | 文本字面 `\n`（0x5C 0x6E）→ 换行 0x0A、GBK 安全行走、仅 `\n` | §1 命令表/载荷表 + §14（2026-09-18 条） | 已在册 |
| 11 | 端口字段 **BE16**（10028 发 `27 2C`、`2C 27` = 11303）、0x40/0x50/0x70 同序 | §1 + §2 #6 + §13.5.1 | 已在册 |
| 12 | `0x40` 只写 `net_cfg.port`、保留 `udp_port`、双消费者（TCP 业务口 + 专用 UDP 口） | §1 命令表 + §13.1 / §13.3 | 已在册 |
| 13 | `0x40` → 应答 `0x50` → `osDelay(100)` → 软复位 | §1 命令表 + §13.4 | 已在册 |
| 14 | `0x50` 应答是回显、**不是落盘证据**；证据 = `0x70` 回读 + 新端口实发 | §13.4 | 已在册 |
| 15 | `0x50`/`0x60` 搜索 → `0x70` 单播回源；`0x70` 仅出站、入站静默丢弃 | §1 命令表 + §2 #1 + §11 | 已在册 |
| 16 | 搜索时记录无效 → 回**全 0** 14B 载荷 | §1 **新增「载荷长度与取值纪律」段** | **本轮补** |
| 17 | 载荷长度纪律（`0x10/0x30/0x50/0x60` 不限、`0x20` ≥17B、`0x40` ≥14B） | §1 **同上新增段**（来源 = 解析层注释 + `0x30` 文档帧例依据） | **本轮补** |
| 18 | `0x80` 三色 + 未识别不动屏幕 + 副作用（挂起光敏 + 亮度 8）+ 任意帧恢复 | §1 命令表 + §2 #3 + §8 Q6 | 已在册 |
| 19 | `0x10` 应答 `00 00` 的理由（文档补 0、源固件静默缺陷不复制） | §1 命令表 + §2 #2 | 已在册 |
| 20 | 四通道绑定（RS485/RS232/10011/专用口）+ 源固件 10028 + 广播差异 | §1 传输段 + §3 绑定条 + §11 | 已在册 |
| 21 | 帧头 `0x54` 全槽唯一 / 无需 EIDE 互斥 / 不定义 `g_brace_proto_guard` / 双向快拒 | §4（含反向证据） | 已在册 |
| 22 | probe 10 步判定链 + `len > RB-1` 必 FAKE（防永久 WAIT 卡死链式探测）+ `>256` SKIP | §3 probe 判定链 + §8 Q1 | 已在册 |
| 23 | RJ45 槽 RB 只 acquire 不 provide | §3 RB 条 + §11 | 已在册 |
| 24 | 队列/控制块/任务帧缓冲 **1136B 置 CCMRAM**（792 + 80 + 264）+ CPU 独占理由 + SRAM 侧 21B | §6 放置策略 + CCM 构成 | 已在册 |
| 25 | 处理任务栈 + TCB **静态落 CCMRAM、不占 ucHeap** | §3 任务行 + §6「任务与堆」段（改前误记「1KB，ucHeap」「本模块占 ucHeap 3×1KB」） | **本轮更正**（§3/§6） |
| 26 | 渲染互斥：`0x20/0x30/0x80` 清屏+渲染+提交整段持 `app_scroll_render_lock` | §3 渲染互斥条 | 已在册 |
| 27 | 波特率 9600 8N1 → 现场 DIP1 置 OFF | §1 | 已在册 |
| 28 | 上电三行绿字（含前导空格）+ FONT_16/HT/绿 + 单槽首个注册生效竞争 | §7 + §8 Q11 | 已在册 |
| 29 | 禁止绑 `CH_ID_RS232_1`（USART6 语音 TX 专用） | §3 绑定条 | 已在册 |
| 30 | 解析状态语义（`ERR_CMD` 静默丢弃 / `ERR_PARAM` 整帧丢弃） | §8 Q14 + §2 #8 | 已在册 |

**映射结论**：30 条中 **26 条已在册**、**2 条本轮补**（#16、#17 → §1 新增段）、**2 组本轮更正**（#9 → §1 两处旧口径；#25 → §3/§6 任务新事实）。

## 4. `doc/14` 变更清单（章节号 + 来源）

| 章节 | 变更 | 来源 / 理由 |
|---|---|---|
| §1 命令表 `0x20` 行 | 「字形框越出屏体几何由显示层整块丢弃（前置条件字号 ≤ 屏高）」→「**字号不受屏高限制**：行框下缘越屏按屏幕交集裁剪绘制（2026-09-14 裁决）」 | 与源码 `_gz_ol_exec_display` 注释、§12.3/§12.6 一致（更正历史口径残留） |
| §1 载荷布局表 `12..13` 行 | 同上去掉「前置条件：字号 ≤ 屏高 …整块丢弃 → 清屏后无内容」 | 同上 |
| §1 新增「**载荷长度与取值纪律**」段 | `0x10/0x30/0x50/0x60` 不校验长度（含文档 `0x30` 帧例 17B 参数块依据）；`0x20` ≥17B、`0x40` ≥14B；记录无效时搜索回显**全 0** | 来源 = `app_gz_ol_proto_parse.c` 头 + `app_gz_ol_proto_cmd.c` `_gz_ol_exec_search`；标注「2026-10-08 注释规范化迁移补充」 |
| §3 任务行 | 「栈 256×4（1KB，ucHeap）」→「栈 + TCB 经 `PL_TASK_STATIC_STORAGE` 静态落 CCMRAM（2026-09-17 起不占 ucHeap，账本 doc/06-04 §8）；帧缓冲 static 同落 CCMRAM」 | 来源 = `app_gz_ol_proto.c`（堆修复轮 as-built；旧文与代码不符） |
| §6 「任务与堆」段 | 「本模块占 ucHeap 3×1KB」→「处理任务已静态化；占 ucHeap 的是 `udp_gzol_task`/`udp_gzol_connect_task`（`app_udp.c`）」+ 指向现行账本 | 同上 |
| §6 口径说明 blockquote | 追加「**现行基线（2026-10-08）**」指针（本表为 2026-09-14 历史采样；现行以 doc/06-04 §8.4.4 + `doc/CLAUDE.md` 为准） | 历史采样易被误读为现状 |
| §8 其它未闭环第 6 条 | 「可用字号上限 = 屏高（整块丢弃 → 清屏黑底）」→「字号不再受屏高限制；屏体几何仍须先确认（决定完整显示 vs 可见上部）」 | 同 §1 更正 |
| §13.4 判定矩阵两行 | 「查 RTT：`net_cfg.port invalid` / `busy … skip bind`」→「读只读 getter `app_udp_gzol_bind_status_str()`（SKIPPED/FALLBACK）」；「抓 RTT + 查 Sector1」→「调试器读 Sector1 + 复测 `0x40`」 | RTT 设施已于 2026-09-29 删除（文首横幅已注明，此处为具体判据同步） |
| §13.4 判定段 | 去掉失效行号指针 `app_gz_ol_proto_cmd.c:432` → 指向 §1 命令表 | 注释规范化后行号已变（避免死指针） |
| §13.5 历史记录 | `app_gz_ol_proto.c:120-142` 标注「= 当时版本行号」 | 同上（历史段保留、防误读） |
| §14 修订记录 | 新增「2026-10-08（注释规范化迁移批 1b）」条 | 模块修订记录纪律 |

> 批次索引 `.analysis/comment_pass_20261008/README.md` 同步增补「批 1b」行与批 1b 复跑命令（产物目录内部，不属 `doc/`）。

## 5. 验证结果（全部通过）

### 5.1 A/B：改前（归档 = 基线 `c624915` 内容）vs 改后，同 flags 编译

脚本 `.analysis/comment_pass_20261008/scripts/batch01b/ab_check_gz_ol.sh`（flags 逐字取自 `make -n` 的 GZ_OL 编译命令；
唯一差异 = 头文件搜索路径指向「改前影子目录」或仓库现行目录）；日志 `logs/batch01b_ab.log`。

| TU | strip-debug 对象 md5（前 = 后） | 段级（排除 `.debug*`） | 反汇编 | 非调试段源码路径串 |
|---|---|---|---|---|
| `app_gz_ol_proto.c` | `71d6e9af45269ddc75aec445ca1012ea` ✔ | 22 段逐字节一致 ✔ | 231 行逐行一致 ✔ | 0 处 ✔ |
| `app_gz_ol_proto_cmd.c` | `19084126acc354ecb5e06a81a806d3b9` ✔ | 17 段逐字节一致 ✔ | 454 行逐行一致 ✔ | 0 处 ✔ |
| `app_gz_ol_proto_parse.c` | `26da1d26e60151c1005b3da4842be3d7` ✔ | 10 段逐字节一致 ✔ | 304 行逐行一致 ✔ | 0 处 ✔ |
| `app_gz_ol_proto_default.c` | `03beed9e942924ff6c257f72713eccfa` ✔ | 11 段逐字节一致 ✔ | 87 行逐行一致 ✔ | 0 处 ✔ |

附：整对象 md5（含 `.debug*`）前/后不同属预期（注释行号移动）；4 个 TU 编译 **0 告警**。

### 5.2 仅注释变化（代码 token 零改动）

`scripts/batch01b/code_only_diff.sh`（`gcc -fpreprocessed -dD -E` 去注释 → 空白折叠 → 逐字节比较）；日志 `logs/batch01b_code_only.log`：

```
✔ app_gz_ol_proto.h / _cmd.h / _parse.h / .c / _cmd.c / _parse.c / _default.c   去注释 token 流逐字节一致
######## 结果：仅注释变化（代码 token 零改动）
```

### 5.3 `git diff` 逐文件复核

`scripts/batch01b/diff_comment_only.py`（逐 hunk 行号 → 改前/改后**有状态**注释剥离；
每条被标记 +/- 行的代码残余必须为空，或与改前版本的代码文本一致；另比对**非空代码残余多重集相等**）；
日志 `logs/batch01b_diff_review.log`：

```
✔ 5 个被改文件：+131 / −289（其中行尾注释改写 30 行 = 15 行 × 前后各一次；代码部分未动）
######## 结果：+131 / −289，仅注释变化 ✔
```

### 5.4 全量重编（`PROTO=ALL DISP=1_263`，独立 BUILD_DIR）

| 项 | 结果 |
|---|---|
| 命令 | `make PROTO=ALL DISP=1_263 -j8 BUILD_DIR=/tmp/comment_pass/all1263_b01b` → **exit 0**，242 个 `.o` |
| 段尺寸 | `.text 167944` / `.rodata 195776` / `.data 1656` / `.ccmram 39652` / `.bss 124768` / `._user_heap_stack 2560` —— 与任务书基线**逐项一致** |
| 告警 | **3 条**（全部为既有 HAL `stm32f4xx_hal_flash_ex.c -Wunused-parameter 'Banks'`），**零新增**；`error` 0 条 |
| elf md5 | `1a3e6f003711da9bf38593f4b760485a`（注释规范化不影响机器码；md5 因 `__DATE__/__TIME__` 横幅随构建时间变化，口径见 `doc/构建开关总表.md`） |
| 日志 | `logs/batch01b_build_all1263.log`；`build/` 未被触碰（全部产物落 `/tmp/comment_pass/`） |

### 5.5 宿主推演与文档指针

| 检查 | 命令 | 结果 |
|---|---|---|
| GZ_OL 宿主推演 | `python3 ~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/gz_ol_frame_sim.py` | **122 / 122 通过、失败 0**（`logs/batch01b_gz_ol_sim.log`） |
| doc 指针可解析 | `python3 .analysis/comment_pass_20261008/scripts/batch01b/check_doc_pointers.py` | **7 / 7 通过**（含 §13.5.1、§14 等四级章节存在性；`logs/batch01b_doc_pointers.log`） |

### 5.6 边界核查

- `__LINE__` / `__FILE__`：本模块 **0 命中**（复核 `grep -rn` 本模块目录）；A/B 脚本另有「非调试段无源码路径串」断言 ✔。
- **注释块长度上限**（规范 §2.4/§3/§4）：文件头 ≤10 行、函数注释 ≤8 行——脚本逐块扫描 7 个文件
  **全部通过**（5 个文件头 = 10 行；最长函数注释 8 行 = `gz_ol_probe_frame` / `_gz_ol_text_unescape_nl`；
  薄头 4 行）；记录在 `logs/batch01b_stats.log`。
- 本批**未执行任何 git 写操作**（仅 `git status/log/show/diff`）；未烧录；未改 `.eide/eide.yml`（也无需 `Reload Project`）；未改范围外模块。
- 归档完整性：`git show HEAD:<path>` 逐文件 `cmp` 通过（见 §8）。

---

## 6. 保留的契约行清单（源码一行式，逐条）

1. `.h` 文件头：帧结构 + **无校验** + **`0x40/0x50/0x70` 端口字段高字节在前 BE16（10028 = `27 2C`）** + `0x40` 只写 `net_cfg.port`（`udp_port` 保留）+ `0x54` 全槽唯一 → 无需 EIDE 互斥。
2. `.h` `GZ_OL_FRAME_LEN_MIN/MAX`：17 下界（16 帧头 + 1 尾）；256 上限（实测 158B 的 1.6× 余量）、超长结构合法帧 **probe SKIP 整帧消费**。
3. `.h` `gz_ol_setip_t.port`：**BE16 有意偏离源固件/协议文档**（10028 = `27 2C`，`2C 27` = 11303）；写 `net_cfg.port`（专用 UDP 口 + TCP 业务口）。
4. `.h` `gz_ol_parse_sta_t`：`ERR_CMD` 静默丢弃 / `ERR_PARAM` 整帧丢弃。
5. `.h` `gz_ol_display_t`：字号仅 16/24/32（其余整帧丢弃）；字型未命中回退宋体；x/y ≥ 屏宽/屏高时执行层钳 0。
6. `.c`（proto）文件头：四通道 + 禁绑 `CH_ID_RS232_1` + 9600 8N1（DIP1 置 OFF）+ `0x54` 唯一 + RJ45 RB 只 acquire 不 provide。
7. `.c`（proto）任务存储注释：一次性创建 ⇒ 静态落 CCMRAM、**不占 ucHeap**、任务栈仅 CPU 访问不经 DMA。
8. `.c`（proto）队列注释：**1136B CCMRAM（792 + 80 + 264）** + osMessageQueue 经 CPU memcpy（无 DMA/ETH）。
9. `.c`（proto）probe 文档：PURE（只 rb_peek）；`len > RB_SIZE_RS485-1` **必 FAKE**（防永久 WAIT 卡死同槽链式探测）；`>256` = SKIP。
10. `.c`（proto）probe 内联：长度上界防卡死；超上限整帧跳过不污染重同步。
11. `.c`（proto）`handle_task`：`msg->data` 为任务私有拷贝（0x20 原地 `\n` 转换依赖该所有权）。
12. `.c`（parse）文件头：长度字段须 = 整帧长、尾字节须 `0x00`；`0x10/0x30/0x50/0x60` 载荷不限；`0x20` ≥17B、`0x40` ≥14B；字号仅 16/24/32。
13. `.c`（parse）`_gz_ol_be16` 注释：**仅 `0x40` 端口字段**；`_gz_ol_le16` 注释：帧长与 X/Y/屏宽/屏高口径**未变**。
14. `.c`（parse）内联：字号非 16/24/32 整帧丢弃（源固件 UB）；屏宽/屏高读取后忽略；无载荷命令长度不限（文档 `0x30` 帧例 17B 参数块）；`0x50` 入站兼容搜索触发；`0x70` 入站静默丢弃。
15. `.c`（cmd）文件头：显示/亮度/网络/渲染锁/单播回源 + 端口 **BE16** + GBK 安全行走指针。
16. `.c`（cmd）`_gz_ol_send_frame`：应答载荷上限 **14B**（0x10 的 2B / 0x50、0x70 的 14B）。
17. `.c`（cmd）`_gz_ol_net_payload`：port 报 `net_cfg.port`（专用 UDP 口 + TCP 业务口，与 LDI 12H / IAP 0x01 同源）；`udp_port` 不参与；**BE16**。
18. `.c`（cmd）`_gz_ol_text_unescape_nl`：**GBK 安全行走**（尾字节 0x5C 不参与转义判定；GBK 双字节整体拷贝）；仅 `\n` 转换；输出 ≤ 输入、无需额外缓冲。
19. `.c`（cmd）`_gz_ol_exec_display`：**字号不受屏高限制**（交集裁剪，2026-09-14 裁决）；区域 = 剩余屏幕；x/y 钳 0。
20. `.c`（cmd）`_gz_ol_exec_full_color`：仅三色，其余不动屏幕；副作用 = 挂起光敏 + 亮度 8，任意帧恢复。
21. `.c`（cmd）`_gz_ol_exec_set_ip`：写 `net_cfg.port` 保留 `udp_port`；`0x50` 回显**不是落盘证据**；100ms 后软复位。
22. `.c`（cmd）`_gz_ol_exec_search`：记录无效 → **全 0** 载荷（诚实回显）。
23. `.c`（cmd）`_gz_ol_exec_fault_query`：`00 00` 合规；源固件空实现缺陷不复制。
24. `.c`（default）文件头：三行文案 + **前导空格勿删** + FONT_16/HT/绿 + **单槽、首个注册生效**（全协议构建竞争）。
25. `.c`（default）`_gz_ol_default_line`：行号超屏早退（保留原有参数注释）。

---

## 7. 遗留与待用户抽审点

1. **契约行的取舍需抽审**：以下为「必须留源码一行」的判断，如需更精简可再议——
   BE16（`.h` 结构体 + 解析内联 + 发送注释三处）、`net_cfg.port` 双消费者、`0x54` 唯一性、
   队列 1136B CCM 构成、任务静态化、probe 防死等上界、`\n` GBK 安全行走、渲染互斥、9600/DIP1。
2. **`gz_ol_parse_sta_t` 枚举未加 `_Static_assert`/注释说明**（按规范「不给静态/平凡对象补新注释」原样保留）。
3. **`_gz_ol_default_line` 等静态 helper 的注释保留未再压**（6 行 ≤ 模板上限 8，且参数语义非显而易见；如要求统一压到一行可在下批处理）。
4. **`doc/14` 的历史段（§12.4/§12.8 等）保留 RTT 字样**：文首横幅已声明「RTT 已删除、含 RTT 段落为历史记录」，
   本批只同步了**具体判据类**文字（§13.4）；如需全量清理历史段，请裁决。
5. **范围外文档漂移（只报告、未改）**：
   - `doc/CLAUDE.md:520`（initcall 表）与 `:986`（协议模块表）：GZ_OL 通道写作「RS485+RS232」，
     未含 `CH_ID_UDP`（10011）与 `CH_ID_UDP_GZOL` 专用口（代码四通道四 mask，doc/14 §1/§3 为准）。
   - `doc/CLAUDE.md:271`（文档地图）与 `:1170`（协议表）：仍称 doc/14 有「RTT 诊断」/`GZ_OL_RTT_DIAG` 打 `nl=`
     计数——该设施已于 2026-09-29 瘦身轮删除。
   - `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md:345`（§7「本轮增量账」行）：
     「任务栈 | ucHeap 支出 | `gz_ol_handle_task` 1×1KB」为 2026-09-14 历史快照；该文件 §8 账本已按
     2026-09-17 静态化后口径登记（本模块不占 ucHeap），两处并存易误读。
6. 本轮**未烧录、未提交、未改 git 配置、未改 `.eide/eide.yml`**；`build/` 未被触碰（构建全落 `/tmp/comment_pass/`）。

---

## 8. 归档与证据清单

```
.analysis/comment_pass_20261008/
├── batch01b_gz_ol.md                     ← 本报告
├── archive/batch01b/                     ← 改前源码（= 基线提交 c624915 内容，cmp 逐文件核对通过）
│   ├── Application/Inc/ProtocolParser_GuiZhou_Overload/*.h   （md5 见下）
│   └── Application/Src/ProtocolParser_GuiZhou_Overload/*.c
├── scripts/batch01b/
│   ├── ab_check_gz_ol.sh                 ← A/B（段级 md5 + 反汇编 + strip-debug + 路径串断言）
│   ├── code_only_diff.sh                 ← 去注释 token 流比较
│   ├── diff_comment_only.py              ← git diff 逐行「仅注释」判定（有状态剥离 + 残余多重集）
│   └── check_doc_pointers.py             ← doc 指针 + 章节存在性检查
├── ab/batch01b/                          ← A/B 中间产物（pre/cur 对象、.dis、token 快照、影子头文件）
└── logs/
    ├── batch01b_ab.log                   ← A/B 完整输出（4 TU 全绿）
    ├── batch01b_code_only.log            ← token 流比较（7 文件全绿）
    ├── batch01b_diff_review.log          ← diff 复核（+131 / −289）
    ├── batch01b_doc_pointers.log         ← 指针检查（7/7）
    ├── batch01b_gz_ol_sim.log            ← 宿主推演（122/122）
    ├── batch01b_build_all1263.log        ← 全量重编（段尺寸与基线逐项一致、3 条既有告警）
    └── batch01b_stats.log                ← 行数/注释行对照 + diff stat
```

**归档 md5（改前源码，= `git show HEAD:<path>`）**：

| 文件 | md5（改前） | md5（改后，供抽审对拍） |
|---|---|---|
| `Inc/.../app_gz_ol_proto.h` | `716721522ba16c7cc25239f1a3ad59f1` | `fcad1199b1c187c8aae5d9d0f216814a` |
| `Inc/.../app_gz_ol_proto_cmd.h` | `f751d984f44b120935dab87e7bd8cdb6` | 同前（未改） |
| `Inc/.../app_gz_ol_proto_parse.h` | `2330ce8036715a38b125c2e43cfc2bf1` | 同前（未改） |
| `Src/.../app_gz_ol_proto.c` | `a35ad3ebab7107926f237a3ed3f25518` | `bfc2745b28b324e0b986a38977e10a8e` |
| `Src/.../app_gz_ol_proto_cmd.c` | `0e54accecf941a0f0716a3170f9a6292` | `e64bc13de4280317d0c616a6c87d7953` |
| `Src/.../app_gz_ol_proto_default.c` | `bf4b8b6698850b4088a4ca82862262b7` | `34046cb462d0bacdbb1767c39b44825d` |
| `Src/.../app_gz_ol_proto_parse.c` | `a3502f235b9ca5a9b67f4add8d308782` | `a1e052c387fc15b0aa064ade32cbf89f` |

**基线**：`c624915bf605f59a113351bbe1184c783f9625f1`（批 1a 开工前唯一一次 git 写操作；本批未做任何 git 写操作）。
