# 新疆费显协议 第二轮：PDF 全文（2020 版）差异核对 + 现场「无反应」根因与修复

- 任务：定位并修复「新疆协议解析器使能后，上位机发协议帧，设备无任何反应」。
- 本轮结论一句话：**用户当前 EIDE Debug 目标（IAP + LDI + 新疆，无其它串口协议）里
  RS485/RS232 两个环形缓冲槽体都没有提供者 ⇒ 新疆模块 `app_proto_acquire_buf()` 双双失败、
  在 init 里静默 return（不注册 / 不绑定）⇒ 串口通道被 `app_boot.c` 门控跳过（UART RX 都没挂）
  ⇒ 上位机发什么都不会有任何反应**。已按「模块自备 `RB_PROVIDE_WEAK` 槽体」修复并实测。
- 同时按新版 PDF 全文核对了协议面：命令表/长度口径等差异**已在上一条 2026-10-12 轮补齐**；
  本轮新增发现并修复一处：**PDF §5.2 采样的全屏 `'D'` 帧是 103 字节（96B = 6 行 × 16B），
  此前 74 字节上限会把它 FAKE 掉**，现放宽到 106 并收 7+16N（N=2~6）。
- **未烧录、未提交、未改 git 配置；`.eide/eide.yml` 未改。**

---

## ① 新版文档（PDF）vs 旧摘录版（`~/Desktop/费额显示器通讯协议.md`）差异表

素材：`/run/user/1000/gvfs/smb-share:server=192.168.1.127,share=桌面/费额显示器通讯协议(2).pdf`
→ `/tmp/xj_doc_v2.pdf`；`pdftotext -layout` 全文留档 `.analysis/xinjiang/doc_v2/doc_v2.txt`（930 行），
含 hexdump 的页（20/21/22）另渲染 PNG 逐字校对（`.analysis/xinjiang/doc_v2/img/`，页面级文本
`page_20/21/22.txt`）。PDF 元信息：WPS 生成，22 页，标题「费额显示器通讯协议 2020 年」，
修订说明含 2020-07（增心跳 + 彩色行显示）与 2021-08（增入口发卡/出口收卡机语音）。

| # | 项 | 旧摘录版 `.md`（2024-04-16 重构） | PDF 全文（2020 版） | 现行实现会拒帧？ |
|---|---|---|---|---|
| 1 | 帧头 | `FF DC` | 同为 `FF DC`；**§3.1.12 `'B'` 下载位图另用 `FE DB` + 双字节长度** | `'B'` 帧：会（帧头/长度模型都不符，未实现，见 §8-Q11） |
| 2 | 长度字段语义 | 整帧总长 | **同为整帧总长**（表格 `Length` 与示例 `0x47`=71 字节自洽；§5.2 采样 `0x67`=103 亦自洽） | **否**（语义一致，无需改） |
| 3 | BCC | 除自身外全字节 XOR | 同（§5.2「BCC 是每个消息字节的 XOR」+ `FF^DC^05^44=0x62` 示例） | 否（算法与范围一致） |
| 4 | 帧长范围 | 表内 5~74（'T' 4 行） | 表内 5/7/8/12/17/18/23/71；**§5.2 采样全屏 `'D'` = 103（96B 数据 = 6 行 × 16B）** | **是 → 本轮已修**（74 → 106，见 ②/④） |
| 5 | `'D'` 行显示 / 全屏 | 缺 | §3.1.1 行显示 Len=23（`[5]`行号 1~4）/ §3.1.2 全屏 Len=71（`[5]`=0）/ **§5.2 采样全屏 103** / §3.2.1 出向应答 5B | **曾是**（旧 9 命令白名单无 `'D'`）→ 上一条 2026-10-12 轮已补；**103 形态本轮补齐** |
| 6 | `'G'` / `'H'` | 缺（`.md` 2.6 节整节缺失） | §3.1.6 红绿灯全亮 / §3.1.7 全关，Len 均 5 | 曾会 → 已补（单灯硬件降级为亮/灭） |
| 7 | `'Z'` 查询 | 缺 | §3.1.10 Len=5，**不做动作、只为换回 §3.2.3 应答** | 曾会（且上位机可能等应答）→ 已补 |
| 8 | `'P'` 位图显示 | 缺 | §3.1.11 Len=7（`[4]`亮度、`[5]`图号；图号不匹配则清屏）+ §3.2.4 应答 | 曾会 → 已补（无位图存储：清屏 + 应答） |
| 9 | `'B'` 下载位图 | 缺 | §3.1.12 `FE DB` + 长度 `04 45`(1093) + 6~1092 BMP 数据 + BCC；§3.2.5 应答（FE DB 00 07 'B' 图号 BCC） | **会**（未实现；若上位机用下载位图则无反应——已列入待确认） |
| 10 | `'V'` TTS | 有（Len 72，备注 71 形态） | **PDF 全文没有 `'V'`** | 否（保留兼容，两种长度都收） |
| 11 | `'S'` 语音 | Len=8，编号 0x31~0x36（备注 5 条文案） | §4.1/4.2/4.3 语音报价 1/2/3（Len 12/17/18，载荷 ASCII 数字）+ §4.4 其他播报（Len=8，编号 **0xA0~0xAF / 0xB0~0xB6**，32 条入口/出口文案） | **曾会**：旧实现只认 8 字节 `'S'` → 12/17/18 报价帧被 parse 静默丢；已按帧长四形态分派 |
| 12 | 应答帧 | 全文无 | §3.2.1 `'D'`、§3.2.2 `'C'`、§3.2.3 `'Z'`、§3.2.4 `'P'` → 均 `FF DC 05 <cmd> BCC`；§3.2.5 `'B'`；§3.2.6 启动/复位主动上报 `'R'`；§3.2.7 Nack `'?'`（BCC 错/长度错/命令未定义） | **曾会**（若上位机发完等应答）→ 已回 `'D'/'C'/'Z'/'P'`；`'R'/'?'` 不实现（见 §8-Q14） |
| 13 | 通信参数 | RS232 9600~115200 8N1 | 同 | 否（DIP1 选择） |
| 14 | §2 设计 | 缺 | 4 行显示、每行 8 汉字/16 数字；bitmap 128×64 | 屏体几何差异 → 6 行帧按 6 行收、屏外行由显示层裁剪 |
| 15 | 上电行为 | 无 | 无默认画面要求（只有心跳超时提示「系统维护中」） | 否 |
| 16 | 语音编号 0x36 | 备注列到 0x36（无文案） | §4.4 无 0x36 | 否（0x36 忽略） |
| 17 | 「彩色行显示协议」出处 | 修订说明提及、正文缺（M1） | 正文 §3.1.1「彩色文本显示」(`'T'`，含 RGB 字段) + §3.1.1/3.1.2 `'D'` | 否（两者现行都已实现；`'T'` 才是带 RGB 的「彩色」命令） |

> 表中 5/6/7/8/11/12 项的补齐发生在同一天更早的一轮（见 `doc/19` §9 变更记录第 4 条，推演 312/312）；
> 本表把它与旧 `.md` 的差异一次列全，便于对照。**本轮新增的只有第 4 项（帧长上限）**。

### ② 文档示例帧逐帧喂现行 probe（1:1 复刻判据，见 `xj_frame_sim.py` §10 七条用例）

| 示例出处 | 字节（要点） | 现行 probe 结果 | 卡在哪一步 |
|---|---|---|---|
| §3.1.2 实例（页 6，**完整 71B**） | `FF DC 47 44 FF 00 <64B>` + BCC | **READY(71)**，`parse` 认作全屏 `'D'`、内容 64B | 无（且 BCC 按文档规则成立，证明长度=整帧总长口径正确） |
| §5.2 采样一（页 20/22） | `FF DC 08 4C 30 00 01 01` | **FAKE** | 第 7 步命令字白名单（`0x4C 'L'` 不在文档命令表；且该 8B 帧 XOR 前 7B=0x56 ≠ 末字节 0x01，本身不满足文档 BCC 规则） |
| §5.2 采样二（页 20 dumpB 第二个帧） | `FF DC 67 44 FF 00` + 36 空格 + 「欢迎行驶」+ 6 空格 +「新疆高速公路」+ 34 空格 + `EE`（共 103B） | **修复前**：FAKE（第 5 步长度 103 > 74）；**修复后**：READY(103) + parse 全屏、内容 96B | 修复前卡第 5 步（长度上限） |
| §5.2 采样三（页 21 原样 103B） | 逐字节抄录 103B，XOR(前 102B) == 末字节 `0xB3` | 同上：**修复后 READY(103)**、BCC 自洽、内容 96B | 同上 |
| §5.2 采样一的两处 BCC | 8B 帧与 103B 帧 | 8B 帧 BCC 不自洽（文档自身缺陷/非本协议帧）；103B 帧 BCC 自洽 | — |

**关键判断**：文档表格写 4 行（64B 数据 / 71B 整帧），但文档自己的两份现场采样都是 **6 行（96B 数据 / 103B 整帧）**
且 BCC 按文档规则成立（页 21 逐字节校验通过；页 20 采样把 XOR 结果 `0xEE` 打在了多一个 0x20 的位置，
属该 hexdump 的排版/抄录偏差，数据内容与长度 96B 完全一致）。⇒ 现场上位机**很可能就是发 103B 的
6 行全屏帧**，74 上限会 100% 丢帧。已按「长度 = 7 + 16N，N=2~6」放行，屏外行由显示层裁剪。

---

## ③ 固件侧死点检查：用户构建里新疆模块根本没有跑起来

### 3.1 用户此刻板上最可能是什么构建（证据链）

| 证据 | 内容 |
|---|---|
| `.eide/eide.yml`（mtime 16:25） | `targets.Debug.excludeList` **排除了全部其它串口协议**：`ProtocolParser_{SiChuang_ETC,MTC,Overload,QingHai,GuiZhou,GuiZhou_Overload,YunNan,YunNan_Overload,ChongQing,Anhui,Myanmar,ShanDong}` + `ah` + `rls`；显示只留 `dev_display_1_263.c`；`defineList` 含 `PROTO_CHONGQING` + `XJ_FAULT_SCREEN=0`；`app_factory_test_mm.c` 排除（= 标准厂测） |
| 与 15:41 备份的 diff（`.analysis/xinjiang/eide_20261012/eide.yml.before_xj`） | 除新疆 6 处新增外，**唯一改动 = 新增 `ProtocolParser_SiChuang_{Overload,MTC,ETC}` 三行排除**（469a478,480）⇒ 15:44 那轮等价构建里 Debug 还含四川三（`Debug.after.mk` 实测含 3 个 SC_* 文件），**之后被排除掉** |
| `build/Debug/Project_STD.objlist`（17:25 EIDE 构建） | `Application/Src` 仅 **28** 个 .o：基类 + IAP + LDI + **新疆 3 文件**（无任何其它协议） |
| `build/Debug/unify_builder.log` | 16:27 / 16:28 / 16:34 / 16:42 / 16:46 / **17:25** 共 6 次 `build successfully`（用户在 GUI 连续构建） |
| `build/Debug/compiler.log` | 17:25 那次 `Builder Mode: Incremental`、`The amount of C files: 0` ⇒ **链接复用 17:04 之后的源对象**（含上一轮的栈修复） |

⇒ **用户烧的（或正准备烧的）就是这份「新疆 + IAP + LDI」的 EIDE Debug 镜像。**

### 3.2 死点：两个串口 RB 槽体没有提供者（最高优先怀疑项，已命中）

- 提供者清单（`grep -rn RB_PROVIDE_WEAK Application/Src`）：RS485 由 RLS / YN_OL / SC_ETC / QH / GZ / YN / SC_MTC / SD / SC_OL / GZ_OL / ANHUI 提供；RS232 由 YN_OL / SC_ETC / MM / QH / GZ / YN / SC_MTC / SD / SC_OL / GZ_OL / ANHUI 提供 —— **上述协议在用户目标里全部被排除**。
- 用户镜像实测（`arm-none-eabi-nm build/Debug/Project_STD.elf`，17:25）：
  - `rb_provide_rj45` = `W 0x08041c78`（IAP/LDI 提供）✔
  - **`rb_provide_rs485` / `rb_provide_rs232` 符号不存在**（无 provider）
- `g_rb_provide[]` 表（`app_dispatch.c:60-72`，`.rodata` @0x08066f6c）实测：
  `791c0408 00000000 00000000` ⇒ {RJ45 有值, **RS485 = NULL, RS232 = NULL**}
- 代码路径：`app_proto_acquire_buf()`（`app_dispatch.c:219-241`）见 `provide == nullptr` 即回 `nullptr`
  ⇒ `xj_proto_init()`（`app_xj_proto.c`）两槽皆取不到 ⇒ `if (s_xj_mask_rs485 == 0 && s_xj_mask_rs232 == 0) return;`
  ⇒ **不注册、不绑定、不建队列/任务**。
- 连带效应（比「不注册」更狠）：`ch_proto_map[CH_ID_RS485/RS232]` 恒 0 ⇒ `app_boot.c:166-169`
  的通道门控跳过 `app_rs485_start()` / `app_rs232_start()` ⇒ **UART RX DMA 从未挂上**，
  上位机连一个字节都进不来（与「无任何反应」现象完全吻合）。

### 3.3 顺带核对

- 绑定时机：新疆两处 `app_proto_bind_channel` 都在 `sw_app_initcall(xj_proto_init)` 内
  ⇒ `check_channel_gate.py` **15/15 通过**（纪律满足；问题是「根本没执行到 bind」）。
- EIDE Reload：`eide.yml` 已于 16:25 被回写（`targets.Debug` 结构完整、`files` 含新疆）且 17:25 构建
  的 objlist 与 incList 一致 ⇒ **Reload 已发生，XJ 已编入**（不是「没编译进去」）。
- 上一轮（更早）同一天记录的「栈溢出致协议任务停摆」是本模块的**第二个独立缺陷**（已修，
  `doc/19` §9 第 5 条）：在 RB 缺失的目标里模块从未注册、任务根本没建，所以本次用户的「无反应」
  由 RB 缺失即可完整解释；两处修复互补，均已在本工作区。

---

## ④ 已实施的修复

### 4.1 模块自备 RS485/RS232 槽体（根因修复）

`Application/Src/ProtocolParser_XinJiang/app_xj_proto.c`（文件顶，与青海同款 weak 合并）：

```c
RB_PROVIDE_WEAK(rb_provide_rs485, RB_SIZE_RS485);
RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232);
```

- 语义：`RB_PROVIDE_WEAK`（`Kernel/Inc/ring_buffer.h:40-46`）生成 `__attribute__((weak))` 的
  getter + 内部 static 缓冲；**多协议同槽各自 weak 提供不冲突**（链接器取其一，容量同为 768B）。
- 效果：任何协议集合下本模块都能自己拿到串口槽体 ⇒ 单协议目标也能注册 + 绑定 ⇒ 串口通道被门控放行。
- 多协议共存口径**零 SRAM 代价**（本模块的 weak 定义被同槽他处定义合并，其 static 缓冲随
  `--gc-sections` 丢弃）：`PROTO=ALL` / `PROTO=CQ` 实测 `.bss` 与改前逐字节相同（见 ⑤）。
- 同步把契约 ⑥「acquire-only RB」改为「自备槽体 + acquire」：`app_xj_proto.h` 文件头、
  `app_xj_proto.c` init 注释、`doc/19` §2.2/§4/§5.2/§8。

### 4.2 帧长上限 74 → 106、`'T'` 行数 4 → 6、`'D'` 全屏收 7+16N（N=2~6）

依据 = ② 的采样证据（两份 103B 'D' 帧，BCC 自洽）：

| 位置 | 改前 | 改后 |
|---|---|---|
| `XJ_FRAME_LEN_MAX` | 74 | **106**（'T' 6 行 = 10+96）＋新增 `_Static_assert(< 220)`（保治超 0xDC 长帧仍快拒） |
| `XJ_T_LINE_MAX` | 4 | **6**（文档表 4 行 / 采样 6 行都收；屏外行由显示层裁剪） |
| `'D'` 全屏 | 仅 `raw_len == 71` | `raw_len == 7+16N`，`N ∈ 2..6`（39/55/71/87/**103**）；`N=1`（23）仍归行显示、`[5]` 须 1~4 |
| `xj_frame_sim.py` | 312/312 | **329/329**（新增 §10 七条：§3.1.2 实例、'L' 采样、103B 双采样、行数边界 N=1/4/6/7） |

不改的部分：长度字段语义（= 整帧总长）、BCC 算法、白名单 14 命令、`'B'` 不实现、应答集。

### 4.3 未实施（原因）

- `'B'` 下载位图（`FE DB` + 双字节长度 1093 + BMP 存储/应答）：本硬件无位图存储；且它是**上行 1093B
  大帧**，与单字节长度模型不同，需先确认上位机是否使用（§8-Q11）。
- `'R'` 启动上报 / `'?'` Nack：无上报时机与触发源；Nack 在静默丢帧语义下无法定位出错帧（维持原裁决）。
- 采样一的 `'L'`（0x4C）8B 帧：不属本协议（BCC 亦不自洽），**不**加白名单。

---

## ⑤ 验证数字

### 5.1 宿主推演与结构脚本

| 脚本 | 结果 |
|---|---|
| `python3 .analysis/xinjiang/xj_frame_sim.py` | **329/329 通过**（含 §10 文档示例帧 7 条、RB 自备源码断言 2 条） |
| `.analysis/audit_20261008/landing_20261008/check_channel_gate.py` | 15/15 通过 |
| `.analysis/myanmar/check_display_guards.py` | 16/16 硬断言 + 101/101 调用点判空 |
| `.analysis/myanmar/check_eide_targets.py` | 14/15（唯一失败 = **预存** `PROTO_CHONGQING` ↔ CQ 模块错配 I6，非本轮） |

### 5.2 用户目标口径 A/B（等价 EIDE Debug，`-Og -g`，全量 `-B`）

命令：`make -f .analysis/xinjiang/r2_evidence/Debug.r2.mk CONFIG=EIDEDebugXJ -B -j8 BUILD_DIR=build/xj_r2_dbg`
（Makefile 由 `check_eide_targets.py --target Debug --emit-make /tmp/xjmk/Debug.mk` 生成、已归档为
`.analysis/xinjiang/r2_evidence/Debug.r2.mk`：28 个 app 源文件、
显示 `dev_display_1_263.c`、DEFINES 含 `PROTO_CHONGQING` / `XJ_FAULT_SCREEN=0`）
A/B 只差 4.1 的两行（同一 BUILD_DIR 增量重编，日志 `build_r2_dbg{,_nofix}.log`）：

| 段 | 改前（无提供者） | 改后（自备提供者） | Δ |
|---|---|---|---|
| `.text` | 130584 | 130600 | **+16**（两个 getter） |
| `.rodata` | 192016 | 192016 | 0 |
| `.data` | 1592 | 1624 | **+32**（两个 `ring_buffer_t`） |
| `.bss` | 112372 | 113908 | **+1536**（768+768 槽体） |
| `.ccmram` | 15548 | 15548 | 0 |
| SRAM 合计 | 116528 | 118096 | **+1568** ⇒ 余 **12976B**（131072） |
| 告警 | 3 | 3 | 0（仍 3 条既有 HAL） |

改后符号/表实测：
`rb_provide_rs485` = `W 0x08048644`、`rb_provide_rs232` = `W 0x0804864c`；
`g_rb_provide` = `d1670408 45860408 4d860408` ⇒ **{RJ45, RS485, RS232} 三槽全非空**。

### 5.3 项目标准口径（全量重编，零新增告警）

| 口径 | `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | heap | SRAM 余 |
|---|---|---|---|---|---|---|---|
| `PROTO=ALL DISP=1_263 FACTORY=STD` 本轮 | **172056** | **196264** | 1656 | **41324** | 125388 | 2564 | 1464B |
| 同口径 15:36 基线 | 170392 | 196024 | 1656 | 41196 | 125388 | 2564 | 1464B |
| Δ | +1664 | +240 | 0 | +128 | **0** | 0 | 0 |
| `PROTO=CQ DISP=1_263 FACTORY=STD` 本轮 | **158652** | **195592** | 852 | **39068** | 121900 | 2564 | 5756B |
| 同口径 15:36 基线 | 157004 | 195360 | 852 | 38940 | 121900 | 2564 | 5756B |
| Δ | +1648 | +232 | 0 | +128 | **0** | 0 | 0 |

- Δ 归因：text/rodata = 前一轮 PDF 命令面扩面（白名单 9→14、`'S'` 四形态、应答帧、8 条文案；
  本轮的 RB getter 只占 **+16B text**）+ 本轮长度口径扩展；`.ccmram +128` = 队列 `3×(82→114) = +96`
  与任务帧缓冲 `82→114 = +32`；**`.bss` 两口径都 ±0**（weak 合并，无额外 SRAM）。
- 模块自身：`.ccmram` 跨度 **1668B**（`nm`：msg_buf 114 + queue_buf 342 + cb 80 + 栈 1024 + TCB 100 = 1660 + 对齐 8）。

---

## ⑥ 需要用户做 / 提供（按优先级）

1. **EIDE：`EIDE: Reload Project` → 重新 Build → 烧录**（本轮源码已改；`build/Debug` 里 17:25 的
   elf/hex 是**修复前**的产物，不能直接烧）。烧录后按纪律擦 Sector1（`tool/flash_all.sh` 默认已做）。
2. **上位机实际发的一帧十六进制**（任意一条命令，最好含 `'T'`/`'D'`/`'O'` 各一帧）：
   - 若首字节不是 `FF`、第二字节不是 `DC` ⇒ 上位机用的可能不是这份协议；
   - 若是 `FE DB …` ⇒ 用的是**下载位图** `'B'`（本轮未实现，需补）；
   - 若整帧长 > 106 ⇒ 还有比 6 行更长的形态（需再放宽）；
   - 若是 103B 的 `'D'` ⇒ 与本轮修复一致（应已能显示）。
3. **DIP1 状态**（OFF=9600 / ON=115200）与**接的哪个口**（RS232=USART3 / RS485=USART1）；
   新疆协议无改波特率命令，波特率完全由 DIP1 决定。
4. 是否有**串口监听抓包**（上位机侧 TX / 板子侧 RX 各一段）—— 用于区分「上位机没发」/「发的是别的格式」/「板子没收到」。
5. 若现场等的是**应答**：本协议只有 `'D'/'C'/'Z'/'P'` 四类回 5 字节应答（本轮已实现）；
   `'T'/'O'/'E'/'F'/'J'/'K'/'S'/'V'` 是单向无应答（文档如此），上位机不应等它们。

**最小现场验证步骤**：擦 Sector1 后上电 → 确认 DIP1 与上位机一致 → 发一帧 `C`（清屏，
`FF DC 05 43 62`）→ 屏幕应清黑，且**串口应收到 `FF DC 05 43 62`**（应答）；发 `Z`
（`FF DC 05 5A 7B`）→ 应收到 `FF DC 05 5A 7B`。两帧都能过 ⇒ 收/发链路与协议面都通了；
都不响应 ⇒ 只剩「接线/波特率/上位机格式」三类现场因素（再按 2/3/4 索取证据）。

---

## 证据归档（`.analysis/xinjiang/r2_evidence/`）

| 文件 | 内容 |
|---|---|
| `Debug.r2.mk` | 等价 EIDE Debug 口径 Makefile（28 个 app 源文件） |
| `build_r2_dbg_fix.log` / `build_r2_dbg_nofix.log` / `build_r2_dbg_nofix_restored.log` | 用户目标口径「有/无提供者」A/B + 还原复编三次全量构建日志 |
| `build_r2_all.log` / `build_r2_cq.log` | `PROTO=ALL` / `PROTO=CQ` 全量重编日志（各 3 条既有 HAL 告警） |
| `size_*.txt` | 各口径 `arm-none-eabi-size -A` 段尺寸 |
| `nm_dbg_fix.txt` / `nm_dbg_nofix.txt` | 用户目标口径改后/改前符号表（`rb_provide_rs485/rs232`、`g_rb_provide`） |
| `nm_eide_user_build.txt` | **用户 EIDE 构建（`build/Debug`，17:25）符号表：只有 `rb_provide_rj45`，无 rs485/rs232** |
| `doc_v2/`（doc_v2.txt + img/pg-06/20/21/22.png + page_20/21/22.txt） | PDF 全文提取与含 hexdump 页的逐字校对素材 |

---

## ⑦ 声明

**未烧录、未 git commit、未改 git 配置、未改 `.eide/eide.yml`。** 本轮只改：
`Application/Src/ProtocolParser_XinJiang/app_xj_proto.c`、`Application/Inc/ProtocolParser_XinJiang/app_xj_proto.h`、
`Application/Src/ProtocolParser_XinJiang/app_xj_proto_parse.c`、`.analysis/xinjiang/xj_frame_sim.py`、
`.analysis/xinjiang/doc_v2/*`（新）、本报告、`doc/19` / `doc/CLAUDE.md` / `doc/06-04` 文档同步。
