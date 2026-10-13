# refine 二轮：契约行再精简 + doc 写法统一（YN_OL / GZ_OL / MM）

- **日期**：2026-10-08
- **规范依据**：`doc/18_注释规范.md`（本轮已同步 refine 二轮规则）
- **上游**：refine 一轮（`refine_round1`，报告与证据见 `logs/refine_round1_*.log`、`archive/refine_round1/`）
- **改前归档**：`archive/refine_round2/`（= 提交 `c61c895`，逐文件 md5 见该目录 `_md5.txt`）
- **性质**：**只动注释与文档**（代码 token 零改动、机器码逐字节零变化；构建全落 `/tmp/comment_pass/`）
- **产物**：**未提交**（按要求留工作区供审核）；`git` 本轮只在阶段 B 做过一次 add + 一次 commit（`c61c895`）

---

## 1. refine 一轮覆盖与遗留（本轮起点）

**一轮已覆盖**（`archive/refine_round1` = 提交 `cd9ce318` ↔ 工作区）：

| 面 | 内容 | 证据 |
|---|---|---|
| 范围 | YN_OL 6 文件 + GZ_OL 7 文件 + MM 4 文件（含 `app_factory_test_mm.c`）+ doc/14/15/16/18 | `refine_round1_ab.log`（10 TU 全绿）、`refine_round1_code_only.log`（17 文件 token 一致） |
| 契约形态 | 契约块集中到主头文件、条数 4~8、每条 ≤30 字；其余文件头写「契约一览见 … 文件头」 | `refine_round1_contracts.log`（YN_OL 5 / GZ_OL 6 / MM 8 全部合规） |
| doc | 去「迁移补充」样板句、去修订记录里的迁移条目、指针改全路径 | `git show cd9ce31^..cd9ce31 -- doc/` |

**一轮遗留**（本轮处理）：

1. 契约行仍含**日期/理由/流程复述**（如 MM ② 的「上电默认红叉 = 红灯 S1/PE12 点亮」与 ① 极性重复；MM ② 复述 5 步接管顺序）。
2. 契约行仍含**自证型/「无需做某事」型**条目（YN_OL ③ `{` 帧族互斥 = 链接期强制报错；GZ_OL ② 「帧头全槽唯一：无需 EIDE 互斥」）。
3. 源码内仍有**裸 `§N`**、`doc/NN-NN` 简写与「见」写法（如 `doc/15 §3`、`doc/14 §1 载荷表颜色行`、`doc/08_协议模块接入规则` 无文件名）。
4. doc/14 的 Q8 行与「其它仍未闭环」第 3 条**仍写「字面 `\n` 待联调决定」**——与 §1（2026-09-18 已裁决、已实现）矛盾（文档漂移，本轮订正）。
5. doc/14/15/16 正文仍有 `doc/NN-NN` 简写指针（doc/16 最多）。

---

## 2. 本轮逐文件改动概览（17 文件）

### 2.1 源码（13 文件，`git diff` = `+127 / −98`；仅注释行）

| 文件 | diff | 主要内容 |
|---|---|---|
| `Inc/…YunNan_Overload/app_yn_ol_proto.h` | +10 / −10 | 契约块 5 → 4 条并重排编号；`0x49` 记录注释改指契约 ④；`port` 成员、执行结果枚举、两个默认值 `@note` 的指针统一 |
| `Src/…YunNan_Overload/app_yn_ol_proto.c` | +9 / −7 | 文件头「实现 ②⑧…」改 ②③；任务存储注释 2 行合并；守卫注释去「契约 ③」改挂 doc/15 §4；队列注释 ④→③；注册五步指针落到 `01_通用规则与模块骨架.md §2` |
| `Src/…YunNan_Overload/app_yn_ol_proto_cmd.c` | +13 / −9 | 文件头、自检注释、`0x49` 块、`_yn_ol_net_payload`/`_yn_ol_row_black`/亮度/`0x47`/`_yn_ol_exec_screen_param` 的裸 § 改全路径；契约 ⑤→④ |
| `Src/…YunNan_Overload/app_yn_ol_proto_parse.c` | +2 / −1 | 字段容错块指针补「详见」+ 折行 |
| `Inc/…GuiZhou_Overload/app_gz_ol_proto.h` | +10 / −8 | 契约块 6 → 5 条（删②）并重排；颜色成员、`gz_ol_setip_t.port`、注册入口 `@brief` 指针统一 |
| `Src/…GuiZhou_Overload/app_gz_ol_proto.c` | +8 / −7 | 文件头「实现 ②④」改 ③；任务存储注释；队列注释 ④→③；专用口注释 ③→②；probe `@note` 指针全路径 |
| `Src/…GuiZhou_Overload/app_gz_ol_proto_cmd.c` | +20 / −15 | 文件头「②④」重排；`_gz_ol_net_payload`/BE16 行尾/`\n` 转换/`0x20` 越屏/`0x80` 副作用/`0x40` 落库/`0x50` 回显/`0x10` 的 § 指针全部全路径；去「2026-09-15 用户裁决」日期 |
| `Src/…GuiZhou_Overload/app_gz_ol_proto_default.c` | +1 / −1 | 「见」→「详见」 |
| `Src/…GuiZhou_Overload/app_gz_ol_proto_parse.c` | +5 / −5 | 文件头「详见」；屏宽/屏高行指针 `§2 #4` → 全路径；无载荷命令注释压 2 行 + 指针 |
| `Inc/…Myanmar/app_mm_proto.h` | +14 / −10 | 契约块 8 → 7 条（①② 合并）并重排；结束码正签名块压 2 行 + 指针；5 处 API `@note` 指针全路径 |
| `Src/…Myanmar/app_mm_proto.c` | +18 / −11 | 文件头「④⑥」改 ③⑤；任务存储/队列 ⑥→⑤；开机画面注释、注册 `@note`、默认画面注册、接管注释、probe 指针（4 处）全路径 |
| `Src/…Myanmar/app_mm_proto_cmd.c` | +13 / −8 | 文件头「①②③」重排；映射表极性注释去日期；`mm_apply_init`/`_mm_effective_lamp`/`mm_parse_frame`/`mm_execute_cmd` 指针全路径 |
| `Src/app_factory_test_mm.c` | +4 / −6 | 文件头指针；`app_factory_mode_interrupt` 注释压成 `@brief` + 单 `@note`（去与 doc 重复的标准版语义复述） |

### 2.2 doc（4 文件，`+36 / −21`）

| 文件 | diff | 调整点 |
|---|---|---|
| `doc/18_注释规范.md` | +21 / −8 | §2 第 5（指针统一：源码与 doc 同标准 / 简写禁用 / **历史条目例外**）、§3（**契约行准则** + 编号引用契约）、§7 第 2（指针 + 历史条目）、§9 例 1（更新为 4 条形态 + 记 GZ_OL / MM 条数变化）、§10 变更记录（新行） |
| `doc/14_贵州治超屏协议/README.md` | +4 / −3 | Q8 行与「仍未闭环 3」更新为 **2026-09-18 已裁决**（字面 `\n` → 0x0A，GBK 安全行走）；Q14 的「doc/08」→ `doc/08_协议模块接入规则/04_接入检查清单.md` |
| `doc/15_云南治超屏协议/README.md` | +2 / −2 | §2 第 7 行、§8 裁决 2 的 `doc/14 §13.5.1` → 全路径 |
| `doc/16_缅甸2D费显协议/README.md` | +9 / −8 | 文首、§5（链式契约行）、§6、§7.2 标题、§7.3、§9 注意区分、§10.5 共 7 处简写指针 → 全路径；§7.1 表格「**本轮新增**」→「**2026-09-28 起**」 |

---

## 3. 契约行「保留 / 删除 / 改写」逐条判定

**准则**（本轮新立、已写入 doc/18）：只留「**防未来合理误改**」的绑定型约束（字段编码 / 极性 / 通道归属 /
跨模块字段语义 / Flash 扇区归属 / 内存放置）；每条一行、**不带日期与理由**；删「与代码或断言重复（自证型）」与
「无需做某事」型；同义可合并后重新编号；**编号改动必须同步全模块 `契约 N` 引用**。

### 3.1 YN_OL（5 → 4 条）

| 一轮 | 条目 | 判定 | 理由 |
|---|---|---|---|
| ① | 端口字段（0x47/0x51）高字节在前 BE16 | **改写** | →「0x47/0x51 端口字段高字节在前」（19 字）；BE16 术语退到实现点标签（保留字段编码约束） |
| ② | 不绑 10011：`{` 与 CQ 同首字节靠掩码隔离 | **改写** | 去机制复述 →「不绑 10011（`{` 与 CQ 靠掩码隔离）」（24 字）；通道归属类，保留 |
| ③ | `{` 帧族互斥：量产与其它 `{` 族二选一 | **删除** | **自证型**：守卫缺省即在链接期 `multiple definition` 强制报错（改错必报错、非静默），不满足「静默故障」判据；落点 `doc/15` §4 已在册；守卫处保留一行注释 + 指针（`app_yn_ol_proto.c`） |
| ④ | 队列/控制块/任务帧缓冲置 CCMRAM（1152B） | **改写** | 「静态队列三段（体/cb/帧缓冲）置 CCMRAM」（24 字）；去 1152B（实现点与 doc/15 §6 有） |
| ⑤ | 0x49 记录落 W25Qxx 独立扇区 | **改写** | →「0x49 记录独占 W25Qxx 独立扇区」（21 字）；去偏移算式（宏注释已带「按容量倒推，勿改绝对地址」）；编号 ⑤→④ |

### 3.2 GZ_OL（6 → 5 条）

| 一轮 | 条目 | 判定 | 理由 |
|---|---|---|---|
| ① | 0x40/0x50/0x70 端口字段高字节在前 BE16 | **改写** | 同 YN_OL ①（24 字） |
| ② | 帧头 0x54 全槽唯一：无需 EIDE 互斥 | **删除** | **「无需做某事」型**（不是绑定约束）；落点 = `doc/14` §4 章节标题「结论：**无冲突，无需互斥**」已在册 |
| ③ | 写 `port`：TCP 与 GZ_OL 专用口共读 | **改写**→② | 「`port` 字段被 TCP 与专用 UDP 口共读」（26 字）；跨模块字段语义（`net_cfg.port` 双消费者），最高风险类，保留 |
| ④ | 队列/控制块/帧缓冲置 CCMRAM（1136B） | **改写**→③ | 同 YN_OL ④（24 字） |
| ⑤ | 字面 `\n` 转 0x0A 须按 GBK 结构行走 | **改写**→④（实质保留） | GBK 行走是硬约束（朴素替换毁字），保留（26 字） |
| ⑥ | 9600 8N1：现场 DIP1 **必须**置 OFF | **改写**→⑤ | 去「必须」（22 字）；现场操作契约，保留 |

### 3.3 MM（8 → 7 条）

| 一轮 | 条目 | 判定 | 理由 |
|---|---|---|---|
| ① | 干接点推挽高电平有效：点亮 = 输出高 | **改写**（与②合并） | →「干接点高有效（点亮 = 输出高）；上电默认红灯亮」（24 字）；「推挽」由 `pl_gpio_config_output` 唯一实现点表达 |
| ② | 上电默认红叉 = 红灯 S1/PE12 点亮 | **改写**（并入①） | 与①同属「电平/默认态」语义；**引脚退到映射表**（单一改动点，表内行有 contact/pin）+ `doc/16` §4.1 |
| ③ | 接管顺序固定；摘 EXTI 只碰本线（禁整条 IRQ 禁用） | **改写**→② | →「接管顺序固定；按线摘 EXTI（禁整条 IRQ 禁用）」（27 字）；去 5 步流程复述（代码内已有 ①②③④⑤ 编号注释） |
| ④ | 非本模块命令须结束码正签名才 SKIP | **保留**→③ | 防误吞同槽四川治超帧，最高风险（防误吞纪律） |
| ⑤ | 只绑 CH_ID_RS232；禁绑 RS232_1 语音口 | **改写**→④ | 去「语音口」（USART6 语义在 doc/16 §7.1）（25 字） |
| ⑥ | 队列/帧缓冲/任务栈置 CCMRAM | **保留**→⑤ | 内存放置；实现点 ⑥→⑤ 同步 |
| ⑦ | 箭头交换仅运行时，不做掉电保存 | **改写**→⑥ | →「箭头交换仅运行态」（8 字） |
| ⑧ | 9600 8N1：现场 DIP1 置 OFF | **保留**→⑦ | 现场操作契约 |

**统计**：改写 12 条 / 删除 2 条 / 原样保留（仅编号变化）4 条；契约总数 19 → 16 条；实现点 `契约 N`
引用 17 处全部同步（脚本 `check_contracts.py` 断言引用不越界、无已删编号残留）。

**其它同批收口（非契约行）**：源码内裸 `§N` 与简写指针 12 处 → 全路径（含 `doc/08_协议模块接入规则` →
`…/01_通用规则与模块骨架.md §2`）；日期 1 处（MM 极性注释的 2026-09-28，日期仍在 doc/16 §4.1）；
与 doc 重复的复述 3 处（YN_OL 默认字号/字型两处、`app_factory_mode_interrupt` 标准版语义一处）。

---

## 4. doc 写法统一（本轮口径）

1. **指针**：源码与文档一律 `详见 doc/NN_<目录名>/<文件>.md §x.y`；**同文档自引**可写 `§x.y`；
   禁用 `见上文` / `README §…` / `doc/NN-NN` / 无文件名的目录指针。
2. **历史条目例外**：修订记录 / 归档段落保留当时写法，**不改写历史记录**（写进 doc/18 §2 第 5 条）。
   本轮实测：doc/14 正文 0 残留；doc/15 残留 2 处、doc/16 残留 2 处，**全部位于「修订记录」段**（另有
   doc/15 一处是 RTT 日志样例行，属原文引用）。
3. **迁移样板句**：`迁移补充` / `注释规范化迁移` / `规范化迁移` 三个关键词在 doc/14/15/16 **0 命中**
   （一轮已清，本轮复核）；本轮另把 doc/14 的**过时结论**（Q8 字面 `\n` 待联调）订正为已裁决事实。
4. **不写映射表**：被删契约的落点只记在本报告（doc 正文不出现「映射」字样）。

---

## 5. 验证结果（全绿）

| # | 项 | 判据 / 命令 | 结果 |
|---|---|---|---|
| 5.1 | **归档** | 改前 21 文件 → `archive/refine_round2/`（含 `_md5.txt`） | 22 文件落盘 ✔ |
| 5.2 | **A/B 一致性** | `bash …/refine_round2/ab_check_refine.sh`（10 TU，段级 md5 + strip-debug + `objdump -d` 逐行 + 路径串断言） | **10/10 全绿**，`refine_round2_ab.log`；`pre_warn=cur_warn=0`（每 TU） |
| 5.3 | **token 流** | `bash …/refine_round2/code_only_diff.sh`（`-fpreprocessed -dD -E -P`） | **17/17 一致**，`refine_round2_code_only.log` |
| 5.4 | **git diff 仅注释** | `python3 …/refine_round2/diff_comment_only.py` | **13 源码文件全部仅注释**（4 处行尾注释改写 = 代码残余多重集一致），`refine_round2_diff_comment.log` |
| 5.5 | **契约核查** | `python3 …/refine_round2/check_contracts.py` | **YN_OL 4 / GZ_OL 5 / MM 7 条，每条 ≤30 字；无日期/理由；引用一致；文件头 9 / 10 / 10 行** ✔ |
| 5.6 | **doc 指针** | `python3 …/refine_round2/check_doc_pointers.py` | **101/101**（源码 → doc 76 + doc 正文 → doc 25），`refine_round2_doc_pointers.log` |
| 5.7 | **宿主推演** | `.analysis/yn_ol/yn_ol_frame_sim.py` / `gz_ol_frame_sim.py` / `.analysis/myanmar/mm_frame_sim.py` | **184/184、122/122、157/157**（数量不降），`refine_round2_sims.log` |
| 5.8 | **全量重编** | `make PROTO=ALL DISP=1_263 -j8 BUILD_DIR=/tmp/comment_pass/all1263_refine2` | 段尺寸**逐项一致**：`.text 167944 / .rodata 195776 / .data 1656 / .ccmram 39652 / .bss 124768 / ._user_heap_stack 2560`；**告警 3 条**（全为既有 HAL `-Wunused-parameter`）；elf md5 `21ab9de142ea6ffed1ce3b8231da6ab7`（含 `__DATE__/__TIME__`，跨构建不可比） |
| 5.9 | **长行记录** | `python3 …/refine_round2/long_comments.py` | 长行 10 条（6 条代码行 + 4 条注释/表行，均 ≤116 字符，与一轮同量级），`refine_round2_long_comments.log` |

**未烧录、未提交（refine 二轮产物）、未改 git 配置、未改 `.eide/eide.yml`、未动 `build/`。**

---

## 6. 遗留与建议

1. **修订记录内简写指针保留**（doc/15 2 处、doc/16 4 处 + 触发日志样例 1 处）：按 doc/18 §2 第 5 条
   「历史条目例外」不改写；若用户希望连历史条目一并规范化，可在下一轮批量处理（纯文本替换）。
2. **契约行还剩可议项**：GZ_OL ⑤ / MM ⑦ 的「9600 8N1（DIP1 置 OFF）」属**现场操作契约**（代码不可
   表达、改错=现场无响应）；MM ③（正签名）与 GZ_OL ④（GBK 行走）为纯算法约束。若用户认为可再压，
   建议下一轮只对「9600/DIP1」两条做合并（两模块同一条目 → 可考虑抽到 doc/08 通用规则）。
3. **未触碰的代码格式残留**（非注释，按纪律不碰）：`app_mm_proto_cmd.c` 的 `mm_apply_lamp()`/
   `mm_arrow_map_toggle()` 函数体内 RTT 删除遗留空行；GZ_OL/YN_OL `cmd.c` 内 `(void)wr_ret;` 前后的
   双空行。若允许「顺带清理空行」，可另开一轮（会改变 `git diff` 的「仅注释」判据，需同步改脚本）。
4. **下一批（安徽）建议**：可按 refine 后标准开工——注意三点：① 安徽模块**尚未做过批 1 级改写**，
   需先按 doc/18 全量改（文件头/函数模板/宏一行说明），再套契约块准则；② 契约块上限 4~8 条，
   安徽的「帧头 `0x5A` 串口槽唯一（无需目录互斥）」「坐标直用像素（不做行号换算）」「0x86~0x89 经
   `app_scroll` + 渲染互斥」是候选绑定项，而「无需互斥」类按本轮准则应**删除**；③ 指针一律按
   `详见 doc/13_安徽费显协议/README.md §x.y` 全路径写（该 doc 已是单文件 README）。
5. **本目录索引**：`README.md` 已补「refine 轮」小节（一轮/二轮的报告、脚本、归档、复跑命令）。

---

## 7. 复跑命令（本轮）

```bash
# 契约核查（条数/字数/无日期理由/引用一致）
python3 .analysis/comment_pass_20261008/scripts/refine_round2/check_contracts.py

# doc 指针（源码 → doc + doc 正文 → doc，排除修订记录）
python3 .analysis/comment_pass_20261008/scripts/refine_round2/check_doc_pointers.py

# A/B（10 TU：段级 md5 + strip-debug + 反汇编逐行）
bash .analysis/comment_pass_20261008/scripts/refine_round2/ab_check_refine.sh

# token 流 / git diff 仅注释
bash .analysis/comment_pass_20261008/scripts/refine_round2/code_only_diff.sh
python3 .analysis/comment_pass_20261008/scripts/refine_round2/diff_comment_only.py

# 宿主推演 + 全量重编
python3 .analysis/yn_ol/yn_ol_frame_sim.py
python3 ~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/gz_ol_frame_sim.py
python3 .analysis/myanmar/mm_frame_sim.py
make PROTO=ALL DISP=1_263 -j8 BUILD_DIR=/tmp/comment_pass/all1263_refine2
```

---

## 8. 终态 md5（供抽审对拍）

| 文件 | md5（refine 二轮后） |
|---|---|
| `Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h` | `601d6ae379c60f1a744125fcc20f96f3` |
| `Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.h` | `871a4a6c13d5c9a9974935cf6b6052b7`（未改） |
| `Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto_parse.h` | `34fd69602a5cd451d6d213c07c7ffbd8`（未改） |
| `Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c` | `7fb4f1b47274a7ae23ff0222f4e9df35` |
| `Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c` | `eed852d60a1049db7112e02c9508eabe` |
| `Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_parse.c` | `842ce997c38596b2c8d62ec10576e328` |
| `Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h` | `104c50adb1989481d7fcd8c546fc8e41` |
| `Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.h` | `f751d984f44b120935dab87e7bd8cdb6`（未改） |
| `Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.h` | `2330ce8036715a38b125c2e43cfc2bf1`（未改） |
| `Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c` | `43dd9452c0c583d1a8ab0db38a2613e7` |
| `Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c` | `978ce2cb4603bf128e910dffe999eafd` |
| `Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_default.c` | `5ed2afe46b915dea056242fc415e0af5` |
| `Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.c` | `c107789c2b0fb2220be119f264e6250a` |
| `Inc/ProtocolParser_Myanmar/app_mm_proto.h` | `0e9d6f1fef84381f7645f35f67617cb9` |
| `Src/ProtocolParser_Myanmar/app_mm_proto.c` | `a218f5857aac583482f11f3140237ea0` |
| `Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c` | `7b0aa661bd795d812def7e92e82fd4cf` |
| `Src/app_factory_test_mm.c` | `1eca55f27cfd52053d11c3dfc433677f` |
| `doc/14_贵州治超屏协议/README.md` | `83cf873b86a81beb1b4cfeac803fb17f` |
| `doc/15_云南治超屏协议/README.md` | `51548456b536d9228f1fc8eb96f03ebe` |
| `doc/16_缅甸2D费显协议/README.md` | `6de5580e3c93d4df93558fedc6e1ab0e` |
| `doc/18_注释规范.md` | `d414c05c5bdf9b343ac32847e7ee502c` |

（改前 md5 见 `archive/refine_round2/_md5.txt`）
