# 批 1a：云南治超模块（YN_OL）注释规范化 — 报告

- **日期**：2026-10-08
- **规范依据**：`doc/18_注释规范.md`（批 0 产物）
- **范围**：`Application/Inc/ProtocolParser_YunNan_Overload/` + `Application/Src/ProtocolParser_YunNan_Overload/` 全部 6 个文件
- **性质**：**只动注释**（代码 token 零改动、机器码零变化）；构建产物全部落 `/tmp/comment_pass/`，未覆写 `build/`
- **状态**：工作区改动**未提交**（留待用户审核）；批次产物 = 本目录 + `doc/18` + YN_OL 源码 + `doc/15` / `doc/CLAUDE.md` 增量

---

## 1. 文件清单与规模

| 文件 | 行数（前 → 后） | 注释行（前 → 后） | git diff |
|---|---|---|---|
| `app_yn_ol_proto.h` | 285 → **218** | 139 → **72** | +20 / −87 |
| `app_yn_ol_proto_cmd.h` | 7 → 7（未改） | 3 → 3 | — |
| `app_yn_ol_proto_parse.h` | 7 → 7（未改） | 3 → 3 | — |
| `app_yn_ol_proto.c` | 253 → **193** | 105 → **45** | +29 / −89 |
| `app_yn_ol_proto_cmd.c` | 683 → **638** | 180 → **135** | +55 / −100 |
| `app_yn_ol_proto_parse.c` | 305 → **277** | 54 → **24** | +19 / −47 |
| **合计** | 1540 → **1340** | **478 → 276（−202）** | +123 / −323 |

- 4 个文件头全部压到 **10 行**（上限 ≤10）；`app_yn_ol_proto.h` 文件头 52 → **10 行**。
- 两个薄转发头（`_cmd.h` / `_parse.h`）已符合规范，**零改动**。

---

## 2. 逐文件改了什么

### 2.1 `app_yn_ol_proto.h`（协议事实注释主体 → 契约 + 指针）

| 位置 | 改前 | 改后 |
|---|---|---|
| 文件头 | 52 行：协议文档出处、模块前缀、帧格式、21 条命令表、BE16 裁定过程、四通道与波特率、上电效果、帧族纪律、内存纪律 | 10 行：帧结构 + `≤259B/无校验`、**BE16** 契约、`g_brace_proto_guard` 互斥、CCM 1152B + doc 指针（§1、§2、§4、§6、§7） |
| 帧常量/宏 | 一行式（保留） | 一行式（保留，含"越限/取值域"信息） |
| `yn_ol_exec_ret_t` 枚举头 | 3 行，含已删除的 `[yn_ol] exec ret=` 诊断口径 | 2 行：语义 + **供调试器观察，不产生上行报文** |
| `yn_ol_setip_t.port` | 指向文件头「端口字段字节序」段（该段已外移） | 一行契约 + `25 38` 示例 + `doc/15…README.md §2` |
| `0x49` 持久化块 | 27 行（记录布局、扇区占用实测推导、字库末扇区推算、写语义） | **5 行**：记录语义 + 独立扇区 `capacity-12288`（不与 `-4096`/`-8192` 共享）+ 无整扇区镜像 + `§7` |
| 4 个 API 的 `@note` | 各 3~5 行（默认字号横向对照、装载/保存失败语义重复） | 各 1 行 + `§7` 指针 |

### 2.2 `app_yn_ol_proto.c`（注册 / 探测 / 任务）

- 文件头 48 → **10 行**：四通道 + `RS232_1` 禁绑 + **掩码隔离**契约（CQ 不绑 TCP / 本模块不绑 10011）+ 帧族互斥 + 指针（§1、§3、§4）；原「RJ45 槽 probe 竞争」长叙述（含收录序推演与 CQ 兼容路径）全部由 doc/15 §1/§4 承载（该处原本更详细）。
- `PL_TASK_STATIC_STORAGE` 注释：6 → 3 行（保留"处理任务静态化 / 自检任务保持动态"判据，账本指向 doc/06-04 §8）。
- `g_brace_proto_guard`：4 → 3 行（保留"多编即链接期 multiple definition"契约）。
- RB `weak` 合并/只 acquire 不 provide：2 → 2 行（保留）。
- 静态队列 CCM 注释：4 → 2 行（保留 1152B + CPU 独占访问理由，指向 §6）。
- `yn_ol_proto_init` 文档：7 → 5 行；网口槽内联注释 5 → 3 行。
- `yn_ol_probe_frame` 文档：21 → **7 行**（7 步判定链外移 §3，保留 PURE 契约与单一真源一句）。
- 「命令字快筛」内联注释（`'6'/'7'/'9'/'B` → FAKE）：保留（快拒契约）。
- `yn_ol_proto_handle_task` 文档：保留（公开 API）。

### 2.3 `app_yn_ol_proto_parse.c`（纯解析）

- 文件头 18 → **10 行**：帧结构 + 长度自洽/无参=0/0x47≥14 校验纪律 + 快拒 + 编码容错 + 指针（§1、§2、§11.1）。
- 「字段编码容错」块：8 → 3 行（保留"只放宽、不改语义 + 越界仍丢弃"契约，背景归 §11.1）。
- `_yn_ol_parse_color` / `_yn_ol_parse_row`：各 6 行 Doxygen → **1 行 `/** @brief … */`**（静态平凡函数，按规范不展开）。
- `yn_ol_cmd_from_byte` 文档：7 → 4 行（单一真源 + 三档快拒值）。
- `'3'` 单行 case：5 行内联说明 → 2 行（保留"参数 ≥3B / 超宽截断 / 行 5 不落屏"）。
- `'8'` 亮度 case：3 行 → 2 行（保留超集口径 + 已裁决）。
- `0x47` 端口 BE16 内联：2 → 2 行（保留已裁决 + `25 38` 示例 + 指针）。
- 其余 case 的一行式协议说明（帧例、无参要求）**全部保留**。

### 2.4 `app_yn_ol_proto_cmd.c`（命令执行与应答）

- 文件头 26 → **10 行**：显示映射三条 + 亮度/灯控口径 + Sector1 真源 + BE16 + 渲染互斥 + 指针。
- `_yn_ol_send_frame`：7 → 1 行（静态）。
- `_yn_ol_net_payload`：9 → 6 行（保留 port 语义 + BE16 + 指针）。
- `0x49` 持久化块：17 → **5 行**（与 `.h` 同款的契约 + 指针，去掉与 doc 重复的扇区推导）。
- `_yn_ol_row_black`：7 → 5 行（"不提交"契约 + 越界早退）；
  `_yn_ol_exec_host_query` / `_yn_ol_selftest_task` / `_yn_ol_exec_one_line` / `_yn_ol_exec_full_screen` /
  `_yn_ol_exec_fill_all` / `_yn_ol_exec_brightness` / `_yn_ol_exec_peripheral` / `_yn_ol_exec_set_ip` /
  `_yn_ol_exec_get_ip` / `_yn_ol_exec_screen_param` / `_yn_ol_exec_version` / `yn_ol_execute_cmd`：
  各 6~11 行 → **4~7 行**（保留契约一句话，历史/判读外移）。
- 2 处失效表述订正（原指已删除的 RTT 诊断）：
  - `return YN_OL_EXEC_OK; /* 行起点越界 = 执行但不落屏（诊断行标注） */` → `（不提交可见变化外的动作）`；
  - `_yn_ol_exec_one_line` 尾部块注释去掉"诊断行会打印 y/屏高并标注 OFF-SCREEN 供现场判读"。

---

## 3. 源码注释 → doc 落点映射（可核查）

| # | 源码注释（改前内容） | doc 落点 | 状态 |
|---|---|---|---|
| 1 | 协议文档出处/版本/日期、模块前缀与云南常规区分 | `doc/15` 文首引言 + §1 | 已在册 |
| 2 | 帧格式与"帧总长 = 参数长度 + 4" | `doc/15` §1 帧格式表 | 已在册 |
| 3 | 21 条命令表逐条语义 | `doc/15` §1 命令表 | 已在册 |
| 4 | BE16（0x47/0x51 端口字段）裁定与依据 | `doc/15` §2 第 7 行 + §8 裁决 2 | 已在册 |
| 5 | 四通道绑定 + 禁止 `RS232_1` + 波特率 DIP1 | `doc/15` §1「传输」段 + §3 | 已在册 |
| 6 | RJ45 槽 CQ 竞争推演（收录序/掩码隔离/反向安全/反事实） | `doc/15` §1 引用块 + §4 表 + 宿主脚本 §9（38 用例） | 已在册 |
| 7 | 上电画面不实现（原 default 模块已删） | `doc/15` §7.1 | 已在册 |
| 8 | `{` 帧族互斥守卫 + `ld -r` 实测 + 0x42 同字节歧义 | `doc/15` §4 + §8 裁决 7 | 已在册 |
| 9 | 命令字快拒（'6'/'7'/'9' 不开发） | `doc/15` §1 表 + §2 第 1 行 | 已在册（源码留一行） |
| 10 | 队列/控制块/帧缓冲置 CCMRAM（1152B）及理由 | `doc/15` §6 + `doc/06-04` §7.3/§8 | 已在册 |
| 11 | 处理任务静态化 / 自检任务动态（压垮堆的最后一份） | `doc/06-04` §8（§8.1/§8.2）+ `pl_task_static.h` | 已在册 |
| 12 | probe 7 步判定链 | `doc/15` §3「probe 判定链」 | 已在册 |
| 13 | 解析纪律：长度自洽、无参=0、0x47≥14、颜色越界丢弃、0x01/0x49 值域 | `doc/15` §2 **新增第 12 行**（2026-10-08 迁移补充） | **本轮补** |
| 14 | `'3'`/`'4'` 颜色与 `'3'` 行号 ASCII/二进制超集容错 | `doc/15` §11.1 C + §2 第 12 行 | 已在册 |
| 15 | `'2'` 自检：数字 1~9 / 500ms / 可打断 / **被打断不清屏** / 无语音 / 固定汉字未给 | `doc/15` §1 `'2'` 行 + §8 残余 1 | **本轮补**"不清屏" |
| 16 | `'1'` 恒回状态正常；`'4'` 回车由渲染引擎处理；行清除越界不落屏 | `doc/15` §1 命令表相应行 | 已在册 |
| 17 | `'8'` 亮度超集（'0'~'5' → 0x00/'0' 自动 + '1'~'8' 手动） | `doc/15` §2 第 4 行 + §8 裁决 4 | 已在册 |
| 18 | `'A'` 红灯优先 + P6 附注 `04`=黄闪开 | `doc/15` §1 `'A'` 行 | 已在册 |
| 19 | `0x47` 写 `net_cfg.port`（保留 `udp_port`）→ 0x51 回显 → 100ms → 软复位 | `doc/15` §1 表 + §2 第 8 行 + §8 裁决 3 | 已在册 |
| 20 | `0x48` 无效记录回全 0 | `doc/15` §1 表 `0x48` 行 | 已在册 |
| 21 | `0x49` 记录布局/扇区选址（1997 扇区/独占核对）/写语义/装载保存失败语义 | `doc/15` §7（含候选扇区对照表） | 已在册 |
| 22 | `0x02` 版本回 `PROGRAM_CODE` + 屏显格式 | `doc/15` §1 表 + §2 第 6 行 | 已在册 |
| 23 | `yn_ol_exec_ret_t` 取值语义（0/-1/-2/-3/-4） | `doc/15` §3 **新增「执行结果观察量」条**（2026-10-08 迁移补充） | **本轮补** |
| 24 | 渲染互斥（`app_scroll_render_lock` 包住清屏/渲染/提交） | `doc/15` §3 末条 | 已在册 |
| 25 | `0x49` 默认值口径（FONT_16/FONT_ST，与青海/贵州一致） | `doc/15` §7 末段 + §8 裁决 8 | 已在册 |

> 说明：doc 落点核对方式 = `grep` 关键字 + 抽读章节；缺失项 3 条（#13/#15/#23）已先补进 `doc/15` 再精简源码。

---

## 4. 验证结果（全部通过）

### 4.1 A/B：改前（归档 = 基线提交内容）vs 改后，同 flags 编译

脚本 `.analysis/comment_pass_20261008/scripts/ab_check_yn_ol.sh`（flags 逐字取自 `make -n` 的 YN_OL 编译命令；唯一差异 = 头文件搜索路径指向"改前影子目录"或仓库现行目录）；日志 `logs/batch01a_ab.log`。

| TU | strip-debug 对象 md5（前 = 后） | 段级（排除 `.debug*`） | 反汇编 | 非调试段源码路径串 |
|---|---|---|---|---|
| `app_yn_ol_proto.c` | `0ed06b47ee624d9ff5338d3b8976fbf0` ✔ | 22 段逐字节一致 ✔ | 203 行逐行一致 ✔ | 0 处 ✔ |
| `app_yn_ol_proto_cmd.c` | `68c35618e10332f88faffef7b6ceb6e8` ✔ | 42 段逐字节一致 ✔ | 963 行逐行一致 ✔ | 0 处 ✔ |
| `app_yn_ol_proto_parse.c` | `0470abe17547cf3dcc183018f5299d6b` ✔ | 7 段逐字节一致 ✔ | 395 行逐行一致 ✔ | 0 处 ✔ |

附：整对象 md5（含 `.debug*`）前/后不同属预期（注释行号移动）；**最终两轮构建的 YN_OL 对象连整对象 md5 都相同**（文档指针归一化只改注释文本、未增删行，见 §4.4）。

### 4.2 仅注释变化（代码 token 零改动）

脚本 `scripts/code_only_diff.sh`（`gcc -fpreprocessed -dD -E` 去注释 → 空白折叠 → 逐字节比较）；日志 `logs/batch01a_code_only.log`：

```
✔ app_yn_ol_proto.h / _cmd.h / _parse.h / .c / _cmd.c / _parse.c   去注释 token 流逐字节一致
######## 结果：仅注释变化（代码 token 零改动）
```

### 4.3 `git diff` 逐文件复核

- 4 个文件的 diff 共 **+123 / −323** 行，全部为注释行或"代码行 + 行尾注释"（行尾注释改写的 7 行：代码部分逐字符未动，由 §4.2 token 流一致独立证明）。
- 复核命令：`git diff -- <file>`；机器可读复核 = 上述 token 流比较（比逐行过滤更强）。

### 4.4 全量重编（`PROTO=ALL DISP=1_263`，独立 BUILD_DIR，不覆写 `build/`）

| 项 | 结果 |
|---|---|
| 构建 | `make PROTO=ALL DISP=1_263 -j8 BUILD_DIR=/tmp/comment_pass/all1263_final` → **exit 0**，242 个 `.o` |
| 段尺寸 | `.text 167944` / `.rodata 195776` / `.data 1656` / `.ccmram 39652` / `.bss 124768` / `._user_heap_stack 2560` —— 与基线（`landing_20261008` 记录）**逐项一致** |
| 告警 | **3 条**（全部为既有 HAL `stm32f4xx_hal_flash_ex.c -Wunused-parameter 'Banks'`），**零新增** |
| 错误 | 0 |
| 日志 | `logs/batch01a_build_all1263_final.log`（首轮 `logs/batch01a_build_all1263.log` 同尺寸） |

### 4.5 宿主推演与文档指针

| 检查 | 命令 | 结果 |
|---|---|---|
| YN_OL 宿主推演 | `python3 .analysis/yn_ol/yn_ol_frame_sim.py` | **184 / 184 通过、失败 0**（`logs/batch01a_yn_ol_sim.log`） |
| doc 指针可解析 | `python3 .analysis/comment_pass_20261008/scripts/check_doc_pointers.py` | **24 / 24 通过**（`logs/batch01a_doc_pointers.log`；逐条核对文件存在 + 章节号存在） |
| 堆账本 | `python3 .analysis/heap/heap_ledger.py --markdown` | 与 doc/06-04 §8 一致（启动期余量 23480B），无变化 |
| `__LINE__`/`__FILE__` 扫描 | `grep -rn '__LINE__\|__FILE__' Application Device` | Application/Device 范围内 **0 命中**；全仓唯一命中 = HAL `assert_param`（`Core/Inc/stm32f4xx_hal_conf.h:493`，范围外且 YN_OL TU 不展开）；A/B 脚本另有"非调试段无源码路径串"断言 ✔ |

---

## 5. 文档同步（代码-文档同步纪律）

| 文档 | 变更 |
|---|---|
| `doc/18_注释规范.md` | **新建**（批 0）：目的/范围、总则、文件头模板（≤10 行）、`.c` 函数模板、`.h`、宏/结构体、历史/契约迁移规则、验收标准、**3 组真实改前/改后对照示例**（取自本批） |
| `doc/15_云南治超屏协议/README.md` | ① §1 `'2'` 行补"被打断退出时不清屏"；② §2 **新增第 12 行**（参数长度与取值校验）；③ §3 新增「执行结果观察量（`yn_ol_exec_ret_t`）」条；④ §3 文件职责表 `.h` 行改为"协议事实见文档"；⑤ §13 修订记录新增本条（2026-10-08） |
| `doc/CLAUDE.md` | 「代码风格」节**只加一行**指针：`注释规范详见 doc/18_注释规范.md（why/约束/坑 + doc 指针；文件头 ≤10 行、只给公开 API 补注释）` |

---

## 6. 遗留与待用户抽审点

1. **`doc/18 §9` 示例 1 的"52 行"取自归档实测**；示例为节选（用 `…（共 30+ 行…）…` 标注），完整改前文本见 `archive/batch01a/`（md5 清单在 §7）。
2. **契约行的取舍需抽审**：以下契约我判断为"必须留源码一行"，如需更精简可再议——
   BE16（`.h` 结构体成员 + 解析内联）、`g_brace_proto_guard` 互斥、CCM 1152B、掩码隔离（文件头一行）、
   快拒（probe 内联一行）、`0x49` 独立扇区（`.h`/`.c` 各一块）。
3. **两个"stale 语义"顺手订正**（属注释改写、非行为）：`_yn_ol_exec_one_line` 尾部与
   `_yn_ol_exec_clear_row` 行尾原指"诊断行打印 OFF-SCREEN 判读"的措辞（RTT 设施已于 2026-09-29 删除）。
4. **`app_yn_ol_proto.h` 的 `_Static_assert` 三条未加注释**（按规范"不给静态/平凡对象补新注释"；如希望给守卫补一行说明，可在下一批统一处理）。
5. **后续批次**：批 1a 之外的模块（含 `Application/` 与 `Device/` 其余文件）尚未开始；`doc/18 §1` 已声明范围，可直接按批推进（建议顺序：同族协议模块 → 通道/框架 → Device）。
6. 本轮**未烧录、未提交、未改 git 配置、未改 `.eide/eide.yml`**；`build/` 未被触碰（构建全部落 `/tmp/comment_pass/`）。

---

## 7. 归档与证据清单

```
.analysis/comment_pass_20261008/
├── batch01a_yn_ol.md                     ← 本报告
├── archive/batch01a/                     ← 改前源码（= 基线提交 c624915 内容）
│   ├── Application/Inc/ProtocolParser_YunNan_Overload/*.h   （md5 见下）
│   └── Application/Src/ProtocolParser_YunNan_Overload/*.c
├── scripts/
│   ├── ab_check_yn_ol.sh                 ← A/B（段级 md5 + 反汇编 + strip-debug + 路径串断言）
│   ├── code_only_diff.sh                 ← 去注释 token 流比较
│   └── check_doc_pointers.py             ← doc 指针 + 章节存在性检查
├── ab/batch01a/                          ← A/B 中间产物（pre/cur 对象、.dis、token 快照）
└── logs/
    ├── batch01a_ab.log                   ← A/B 完整输出
    ├── batch01a_code_only.log            ← token 流比较输出
    ├── batch01a_doc_pointers.log         ← 指针检查输出
    ├── batch01a_build_all1263{,_final}.log  ← 两轮全量重编日志
    └── batch01a_yn_ol_sim.log            ← 宿主推演（184/184）
```

**归档 md5（改前源码）**：

| 文件 | md5 |
|---|---|
| `Inc/.../app_yn_ol_proto.h` | `c5805f1309116ae355beb1c85f719ad6` |
| `Inc/.../app_yn_ol_proto_cmd.h` | `871a4a6c13d5c9a9974935cf6b6052b7` |
| `Inc/.../app_yn_ol_proto_parse.h` | `34fd69602a5cd451d6d213c07c7ffbd8` |
| `Src/.../app_yn_ol_proto.c` | `6f07bc05cf8392c36a4cc19f49ad6c32` |
| `Src/.../app_yn_ol_proto_cmd.c` | `1ca4845c9c3125947290d2207c28c383` |
| `Src/.../app_yn_ol_proto_parse.c` | `d0f6758d9ebcdbcb8ec8e0dd36d5dba3` |

**基线提交**（本轮开工前唯一一次 git 写操作）：`c624915bf605f59a113351bbe1184c783f9625f1`
（266 files changed, +107365 / −1909；469 个构建产物按纪律留在未跟踪区、未入库）。
