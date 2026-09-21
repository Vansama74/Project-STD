# 15 云南治超屏协议（YN_1.3.0，`{` 帧族）

> 协议接入记录。协议文档：`/home/yystation/Desktop/22云南治超屏协议YN_1.3.0.doc`
> （标题《车道费额显示器通信协议》，修订历史 2019-08-29 ~ 2021-11-24，末版 YN_1.3.0；
> 文档开头明确「以下协议用于治超屏上面**不开发语音相关协议和固定格式显示协议**」）。
> 模块：`Application/Src/ProtocolParser_YunNan_Overload/` + `Application/Inc/ProtocolParser_YunNan_Overload/`
> （前缀 `yn_ol_` / 宏 `YN_OL_`，与云南常规费显 `ProtocolParser_YunNan` 的 `yn_` / `YN_` 严格区分）。
> 文档抽取存档：`.analysis/yn_ol/doc_extract.txt`（libreoffice txt 原文 + 逐条结构化整理）。
> 宿主推演：`.analysis/yn_ol/yn_ol_frame_sim.py`（**184 用例通过 / 失败 0**）。

**状态**：`[已落地]` `[Makefile PROTO=ALL/CQ 与 DISP=1_263/22_1665 三口径链接通过]` `[零新增告警]` `[§8 裁决 8 条已逐条落地]` `[宿主推演 184/184]` `[逐帧诊断已就绪（§11）]` `[待现场联调：按 §11.3 重烧并回传 [yn_ol] 行]`

---

## 1. 协议事实（帧格式 + 命令表）

**帧格式**（长度字段定界，**无校验**）：

| 偏移 | 长度 | 字段 | 取值/语义 |
|---|---|---|---|
| 0 | 1B | 帧头 | `7B`（ASCII `{`） |
| 1 | 1B | 命令字 | ASCII `'1'`~`'5'`/`'8'`/`'A'` 或二进制 `0x42`~`0x51`/`0x01`/`0x02` |
| 2 | 1B | 参数长度 | **二进制字节值**（= 参数区字节数，不是 ASCII 数字） |
| 3..N | 变长 | 参数区 | 含义随命令字 |
| N+1 | 1B | 帧尾 | `7D`（ASCII `}`） |

帧总长 = 参数长度 + 4；无 CRC/BCC（协议未定义任何校验字段）。
与云南常规费显协议**完全同构**（帧结构逐字节一致，差异只在命令集与语义）。

传输：**RS485 + RS232 + TCP Server + TCP Client**（四逻辑通道，每通道独立 mask、四 mask 共用同一静态队列）。
文档正文只写「信息显示牌（费显）通过串口与车道工控机相连」，但同时定义了 IP 配置命令
0x47/0x48/0x51 → 说明设备有网口；**2026-09-17 用户裁决：现场可能用网口控制 → 追加 TCP 双通道**
（复用既有 TCP Server/Client 通道，无新 LwIP 资源）。

> **TCP 通道与 CQ 的 probe 竞争（2026-09-17 裁决 Q1，实测 + 源码证据）**：本协议首字节 `'{'` 与重庆 CQ
> 的 JSON 帧**同首字节**，而 CQ probe（`cq_probe_frame`）对 `{` 开头数据做花括号深度扫描——它会把
> 本协议帧（如 `7B 31 00 7D`）当作完整 JSON 抢先 READY 认领并送 CQ 任务（JSON 解析失败后丢弃）。
> **解法不在源码收录序，而在通道掩码**：`app_dispatch.c` `frame_dispatch_task` 取
> `proto = ch_proto_map[ch->ch_id]`（`app_dispatch.c:252`、内层 `:303`），**只有绑定在该通道上的协议
> 才进该通道的 probe 链**；静态扫描全仓 `app_proto_bind_channel` 调用实测：
> - 本模块绑 `CH_ID_RS485` + `CH_ID_RS232` + `CH_ID_TCP_SERVER` + `CH_ID_TCP_CLIENT`；
> - **CQ 只绑 `CH_ID_UDP`（10011）+ `CH_ID_UDP_CQ`** → **TCP 双通道上 CQ probe 根本不会被调用**；
> - 本模块**不绑 `CH_ID_UDP`** → CQ 的 10011 行为与现状**逐字节不变（零削弱）**；
> - TCP 链上同槽协议实扫只有 LDI（首字节 `0xFF` 快拒）→ 本协议在 TCP 上独占 `'{'` 首字节。
>
> **反向安全（CQ 帧不被本 probe 误认/卡住）**：CQ JSON 第二字节恒为 `'"'`（0x22），不在本 probe 的
> 命令字白名单 → 立即 FAKE 放行；CQ 二进制帧首字节 `0xFF` → 首字节快拒；半帧（avail<3）→ WAIT，
> 而调度器在 WAIT 时**继续探测下一协议**、全链无消费才等更多字节 → 不阻塞 CQ 的深度扫描。
> 宿主推演 §9 用静态扫描出的绑定表 + 五种 probe 镜像做了链式竞争全矩阵（链序 × 半帧 × 粘包 × 混合粘包
> × CQ 分片，并含**反事实取证**：本模块帧若真落在 UDP 链上会被 CQ 抢走），见
> `.analysis/yn_ol/yn_ol_frame_sim.py` 9.1~9.32。

波特率 **9600~115200（默认 9600）** → 由全局 DIP1 选择（**现场 DIP1 置 OFF = 9600**）。
文本编码 GBK（`FONT_ENC_GBK` 直通渲染）。

**命令表**（21 条 + 2 条创迪扩展；治超屏裁剪版：'6'/'7'/'9' 明示不开发）：

| 命令字 | 文档语义 | 本实现行为 | 应答 |
|---|---|---|---|
| `'1'`（0x31） | 主机查询 | 解析 → 回固定「状态正常」帧 `7B 31 01 00 7D` | 有 |
| `'2'`（0x32） | 自检 | 惰性一次性任务：整屏居中显示数字 **1~9 交替**（500ms/步，可被下一帧打断）；**语音部分不实现**（文档明示不开发） | 无 |
| `'3'`（0x33） | 点阵单行任意显示 | 颜色(0红/1绿/2黄) + 行号('1'~'5'→0~4) + GBK 文本 → **先清行再渲染**，行高 = 当前字号，超宽截断；行 5 屏高不足时「执行但不落屏」。**颜色/行号接受 ASCII（`'0'~'2'`/`'1'~'5'`）或二进制（0x00~0x02/0x01~0x05）两种编码**（超集容错，2026-09-17 晚联调轮；见 §11.1 C） | 无 |
| `'4'`（0x34） | 点阵全屏可编辑 | 颜色 + X + Y + GBK 文本 → **先整屏清黑**再按 (x,y) 渲染、`word_wrap=true`；回车 `0x0A` 换行 / `0x0D` 跳过（渲染引擎原生）；坐标越界时仅清屏 | 无 |
| `'5'`（0x35） | 全屏清除 | 整屏清黑 + 提交 | 无 |
| `'6'`（0x36） | 固定格式显示 | **不实现**（文档明示治超屏不开发）→ probe 快拒 FAKE | — |
| `'7'`（0x37） | 礼貌用语语音 | **不实现**（语音不开发）→ probe 快拒 FAKE | — |
| `'8'`（0x38） | 显示亮度设定 | 参数 `0x00`/`'0'` → 恢复光敏自动调光；`'1'`~`'8'` → 挂起光敏 + 硬件档恒等映射（**超集**：文档口径 `'0'`~`'5'`，见 §8 Q3） | 无 |
| `'9'`（0x39） | 语音音量设定 | **不实现**（语音不开发）→ probe 快拒 FAKE | — |
| `'A'`（0x41） | 外设控制 | bit0 绿灯 / bit1 红灯 / bit2 黄闪报警（红优先）；文档附注（云南 P6 治超屏 192×96）：`00`=黄闪关、`04`=黄闪开 | 无 |
| `0x42` | 第一行清除 | 按当前字号行高清行（`row=0`） | 无 |
| `0x43` | 第二行清除 | 同上（`row=1`） | 无 |
| `0x44` | 第三行清除 | 同上（`row=2`） | 无 |
| `0x45` | 第四行清除 | 同上（`row=3`） | 无 |
| `0x46` | 第五行清除 | 同上（`row=4`） | 无 |
| `0x50` | 第六行清除（文档第 20 条） | 同上（`row=5`）；2021-11-24 版本标注「除 20」但本实现保留（屏高不足自然不落屏） | 无 |
| `0x47` | 修改 IP | ip4+mask4+gw4+port2 → 写 Sector1 **`net_cfg.port`**（`udp_port` 保留）→ 应答 0x51 回显 → `osDelay(100)` → `NVIC_SystemReset()`（STD 约定重启生效，对齐 GZ_OL `0x40` 先例） | 0x51 |
| `0x48` | 查询 IP | 读 Sector1 net_cfg → 应答 0x51（14B 载荷，单播回源；记录无效回全 0） | 0x51 |
| `0x49` | 设置屏体参数 | x0 字体（0宋/1仿/2楷/3黑）+ x1 字宽（0→16 / 1→24 / 2→32 点阵）→ 先更新模块运行态，再**持久化到 W25Qxx 独立 4KB 扇区**（12B 记录；上电装载；写失败/记录损坏回默认 + RTT 告警，不阻塞不重启），见 §7 | 无 |
| `0x51` | 查询 IP 返回值 | 仅**出站**应答命令字；入站静默丢弃（解析即 `ERR_CMD`） | — |
| `0x01` | 全屏点亮控制 | DATA0：01红/02绿/03黄（文档三色）+ 04蓝/05紫/06青/07白（扩展保留，与云南常规则一致） | 无 |
| `0x02` | 获取版本号 | 串口/网络回**裸 ASCII PROGRAM_CODE** + 屏幕居中显示「版本:PROGRAM_CODE」（FONT_SELF_ADAPT 红字） | 有（裸串） |
| 其它 | — | probe 快拒 / 解析整帧丢弃（协议未定义错误应答 → 静默） | — |

**`0x51` 应答载荷（14B）**：`ip(4) + mask(4) + gw(4) + port(2 **高字节在前 BE16**)`；
`port` = Sector1 `net_cfg.port`（STD 统一「配置功能端口」口径，与 LDI 12H / IAP 0x01 / GZ_OL 0x70 一致）。

**应答帧构造**：与请求同帧族（`{` + 命令字 + 长度 + 载荷 + `}`），一律 `channel_send` **单播回源**。

**上电效果**（**不实现**，2026-09-17 用户裁决 Q7）：文档「上电后显示『祝您一路平安』稍候熄灭」
不由本模块实现——原 `app_yn_ol_proto_default.c` 及其 `app_default_display_register` 注册、
5s 熄灭惰性任务已**删除**；上电画面走 STD 现有默认显示链路（`app_default_display.c`，
本模块不注册则回退系统默认画面）。文档原文另含语音播报 → 不实现（文档开头明示治超屏不开发语音）。

---

## 2. 与协议文档的不一致 / 采纳清单

| # | 项 | 文档 | 本实现（采纳） |
|---|---|---|---|
| 1 | '6'/'7'/'9' 命令 | 有定义（固定格式 / 礼貌用语语音 / 音量） | **不实现**（文档开头明示治超屏不开发语音与固定格式）；probe 层快拒，交同槽其它协议或逐字节重同步 |
| 2 | '3' 单行文本上限 | 一行 12 ASCII / 6 汉字（192px ÷ 16px 口径） | **不设解析上限**（1B 长度字段天然 ≤253）；超宽由渲染层按屏宽截断——同族协议一致处理 |
| 3 | '3' 行号范围 | `'1'`~`'5'` | 解析全接受（0~4）；行 5 在屏高不足时「执行但不落屏」（与云南常规行 5 口径一致） |
| 4 | '8' 亮度档位 | `'0'`~`'5'`（5 最亮；0 自动） | 接受超集：`0x00`/`'0'` = 自动；`'1'`~`'8'` = 手动档（与云南常规/硬件 8 档一致），见 §8 Q3 |
| 5 | `0x01` 颜色 | 01红/02绿/03黄（三色） | 接受 01~07（`display_color_t` 枚举直通，与云南常规扩展口径一致） |
| 6 | `0x02` 版本号串 | 示例 `YN_ZCP_P10_3.0`（源固件私有串） | 回编译期常量 **PROGRAM_CODE** + 屏显「版本:<PROGRAM_CODE>」（云南常规/贵州常规既有裁决口径） |
| 7 | 端口字段字节序 | **未写** | **高字节在前 BE16**（写 9528 发 `25 38`）——**已裁决**（2026-09-17 Q1 结论，对齐同厂 GZ_OL doc/14 §13.5.1 与本固件 LDI/IAP 既有口径；协议文档未写字节序） |
| 8 | `0x47` 应答与复位 | **未写** | 借 `0x51` 作「已写入」回执 + `osDelay(100)` 后软复位（STD 改 IP 统一重启生效）——**已裁决维持**（Q2） |
| 9 | `0x49` 持久化 | **未写** | **持久化**：W25Qxx 独立 4KB 扇区 12B 记录（magic + version + 字体/字宽 + CRC32），上电装载并生效——**已裁决**（Q5），见 §7 |
| 10 | '2' 自检文案 | 「固定汉字信息及数字 1~9 交替显示」 | 只实现**数字 1~9 交替**（汉字内容文档未给）；**本次 8 条裁决未涉及 → 维持现状**（§8 余项） |
| 11 | 屏体几何 | 修订历史多版本（192×96 / 192×80 / 224×64） | 渲染一律以 `dev_display_get()` 实际几何为准；**默认字号沿用 STD 工程既有默认 FONT_16 / FONT_ST**（与青海/贵州同口径，0x49 运行时可改）——**已裁决**（Q9） |

---

## 3. 模块设计与接入点

| 文件 | 职责 |
|---|---|
| `app_yn_ol_proto.h` | 帧常量、命令枚举、解析结构体、**0x49 持久化记录与 API**、对外 API、协议事实注释 |
| `app_yn_ol_proto_parse.h` / `app_yn_ol_proto_cmd.h` | 薄转发头（对齐四川治超 / 贵州治超风格） |
| `app_yn_ol_proto.c` | RB 提供（RS485/RS232 weak）+ 注册（**四通道四 mask**）+ `yn_ol_probe_frame` + `yn_ol_proto_handle_task` + **`g_brace_proto_guard` 互斥守卫** + `sw_app_initcall`（内含 `yn_ol_screen_cfg_load()` 上电装载） |
| `app_yn_ol_proto_parse.c` | `yn_ol_parse_frame()` 纯函数 + `yn_ol_cmd_from_byte()`（probe 白名单与 parse 共用的单一真源） |
| `app_yn_ol_proto_cmd.c` | 命令执行 + 应答组帧（`channel_send`）+ 屏体参数运行态与 **0x49 落盘/装载** + 自检任务 + RTT 诊断 |

- 任务：`yn_ol_handle_task`，栈 `256 * 4`（1KB，ucHeap），`osPriorityNormal`，帧缓冲 static（置 CCM）；
  另有 1 个**惰性创建**任务：`yn_ol_selftest`（'2' 触发，256×4，响应后自退）。
- 队列：`YN_OL_QUEUE_DEPTH = 3`，`YN_OL_PAYLOAD_MAX = 255`（帧 ≤259，与云南常规同构），**四 mask 共用**；
  队列体 + 控制块 + 任务帧缓冲 **置 CCMRAM**（1152B，见 §6）。
- 绑定：`CH_ID_RS485` + `CH_ID_RS232` + `CH_ID_TCP_SERVER` + `CH_ID_TCP_CLIENT`，每逻辑通道独立 mask、
  四 mask 共用同一静态队列（2026-09-17 裁决 Q1，见 §1 probe 竞争结论）；
  **不绑 `CH_ID_UDP`（10011）**（不与 CQ 抢槽，CQ 行为零影响）；
  **禁止** `CH_ID_RS232_1`（语音 TX 专用）。
- RB：`RB_PROVIDE_WEAK(rb_provide_rs485/rs232)` 与同槽协议 weak 合并；RJ45 槽**只 acquire 不 provide**
  （槽体由 IAP/LDI/CQ 提供，避免第二份 1536B 网口缓冲）；极端裁剪构建下同槽无提供者时
  `acquire` 返回 nullptr → 静默跳过网口绑定，串口双通道不受影响。
- 渲染互斥：所有「清屏/清行/渲染/提交」整段持 `app_scroll_render_lock`（与安徽动态滚动渲染串行）。
- **默认字号 = 沿用 STD 工程既有默认口径（FONT_16 / FONT_ST）**：与同为 `{` 帧族、协议未限定字号的
  青海（`app_qh_proto_cmd.c`）/贵州（`app_gz_proto_cmd.c`）一致，也与 `app_default_display.c` 的默认
  FONT_16 一致（2026-09-17 裁决 Q9：**不单独修改**）。0x49 可运行时改为 24/32 并持久化。

**probe 判定链**：

```
① avail == 0                    → FAKE   （禁止盲 WAIT 阻塞链式探测）
② peek[0] != '{'                → FAKE   （首字节快拒）
③ avail < 3                     → WAIT   （命令字 + 长度字段未到齐）
④ 命令字不在实现集合            → FAKE   （'6'/'7'/'9'/'B' 及未知；白名单 = yn_ol_cmd_from_byte）
⑤ frame_len = len + 4
  avail < frame_len             → WAIT   （整帧未到齐）
⑥ peek[frame_len-1] != '}'      → FAKE   （尾字节定界校验）
⑦ READY，*total_len = frame_len
```

---

## 4. 帧头冲突纪律（**`{` 帧族 → 必须 EIDE 互斥**）

| 槽 | 既有首字节 | 本协议首字节 |
|---|---|---|
| RS485 / RS232 | `0x0A` 四川ETC、`0x5A` 安徽、`0x7B` `{` 族（青海/山东/贵州常规/云南常规/四川MTC）、`0xFF` 四川治超 / RLS、`0x54` 贵州治超 | `0x7B` ⚠ **与 `{` 族完全相同** |
| TCP Server / TCP Client（RJ45 槽） | `0x5A5A5A5A` IAP（仅 UDP）、`0xFF 0xFF` LDI、`{`…CQ、`0x54` 贵州治超 | `0x7B` ⚠ **与 CQ JSON 同首字节**——但 **CQ 不绑 TCP 通道**，probe 掩码过滤后 CQ 在本链上不会被调用（见 §1 结论），TCP 链上唯一同槽协议 LDI 以 `0xFF` 快拒 → **本协议在 TCP 上独占 `'{'`** |
| UDP（10011 / 业务口） | 同上 + CQ | ➖ **本模块不绑 UDP**（与 CQ 同首字节且 CQ 收录序更早 → 必然被其深度扫描抢占；见 §1） |

- **结论：本协议属 `{` 帧族 → 定义 `g_brace_proto_guard` 编译期互斥守卫**（`#ifndef STD_ALL_PROTO`
  包裹 + `__attribute__((used))`）。量产构建多编入任一 `{` 帧族（青海/山东/贵州常规/四川MTC/云南常规/云南治超）
  即链接期 `multiple definition of 'g_brace_proto_guard'` 强制报错；
  **托管证据（本机实测，2026-09-17 裁决 Q8）**：
  - 对**云南常规 `app_yn_proto` + 云南治超 `app_yn_ol_proto` 这一对**：
    `arm-none-eabi-ld -r yn_regular.o yn_ol.o`（两者均不带 `-DSTD_ALL_PROTO`）→
    报 `multiple definition of 'g_brace_proto_guard'`（`yn_regular.o … first defined here`，ld 退出码 1）；
  - 同一对加 `-DSTD_ALL_PROTO` 复编后 `ld -r` 退出码 0（守卫宏被 `#ifndef` 移除，符号不存在）；
  - 与四川 MTC 的同款实测此前已通过（同样报 multiple definition）。
  → **云南常规与云南治超「量产必二选一」（EIDE 排除其一）**，见 §4 末条与 §8。
- **命令字层的重叠（同槽链式探测的现实，量产互斥后无此问题）**：

| 与谁 | 重叠命令字 | 后果（全协议 dev 构建，`STD_ALL_PROTO` 豁免守卫） |
|---|---|---|
| 云南常规（`yn_`） | `'1'`~`'5'`、`'8'`、`'A'`、**`0x42`**（本协议=第一行清除 / 常规=`'B'` 费额语音，**同字节歧义**） | 云南常规 probe 先注册（源码收录序在 YunNan_Overload 之前）先认领；语义基本一致的命令（'3'/'4'/'5'/'8'/'A'）表现同构，'2' 自检实现不同 |
| 四川 MTC（`sc_mtc_`） | `0x42`~`0x45`（MTC 认 `0x40`~`0x45` 的 `{` 帧族） | MTC probe 更早注册（源码收录序 SD→GZ→…→MTC→**YN→YN_OL**），其 `'}'` 定界扫描会先认领 `0x42`~`0x45` 帧 |
| 青海/山东/贵州常规 | `'1'`~`'5'`、`'8'`、`'A'`（各协议命令集为 `'1'`~`'9'`/`'A'`/`'B'`） | 同 `{` 族纪律；先注册者认领 |
| **本协议独有** | `0x46`、`0x47`、`0x48`、`0x49`、`0x50`、`0x51`（云南常规只认 `'1'`~`'9'`/`'A'`/`'B'`/`0x01`/`0x02`；MTC 上限 `0x45`） | 全协议构建下由本 probe 独占认领（离线证据：`yn_ol_frame_sim.py` §7，172 用例全通过） |

- **EIDE Debug 处置**：`.eide/eide.yml` 已把本模块 **3 个源文件**加入 `virtualFolder` + `incList`
  （`app_yn_ol_proto_default.c` 条目已随 Q7 删除），**同时保留 `excludeList` 排除项**（默认不编入）
  ——与贵州治超 GZ_OL 的当前处置一致。
  理由：EIDE Debug 现行编译集含**四川 MTC**（`{` 帧族），若移除本模块排除项而不排除 MTC，
  链接期即撞 `g_brace_proto_guard`。**切换方法**：从 `excludeList` 移除
  `<virtual_root>/Application/protocol/ProtocolParser_YunNan_Overload`，**并同时移除 MTC**
  （或排除其它 `{` 族目录，含**云南常规**——Q8 已实测两者的守卫互斥），然后 **`EIDE: Reload Project`**。

---

## 5. 构建口径

- **Makefile**：`INC_DIRS` 增 `Application/Inc/ProtocolParser_YunNan_Overload`（紧随 `ProtocolParser_YunNan`）；
  `SRC_APPLICATION` 增 **3 个源文件**（紧随云南常规之后；`app_yn_ol_proto_default.c` 已删，未收录）；
  **`PROTO=ALL` 与 `PROTO=CQ` 两口径均编入**（`PROTO=CQ` 只剔除 LDI 目录，与本模块无关；
  `STD_ALL_PROTO` 在 CQ 口径亦定义 → 守卫豁免，多 `{` 族共存由 probe 注册序约束）。
- **EIDE**：见 §4 末条（3 文件收录 + 保留排除 + 需 Reload；切换需与 MTC/云南常规等 `{` 族二选一）。
- ⚠ 外部改 `eide.yml` 后必须在 EIDE 执行 **`EIDE: Reload Project`**，期间不做任何触发保存的 GUI 操作。

---

## 6. 内存与构建状态（2026-09-17 裁决轮重采）

**放置策略**：队列体 + 控制块 + 任务帧缓冲置 `.ccmram`（`s_yn_ol_queue_buf` 801B +
`s_yn_ol_queue_cb` 80B + 任务 `msg_buf` 267B = **1148B**，对齐后 **1152B CCM**）。
理由：`PROTO=ALL` 口径 SRAM 余量仅数百 B，放不下 ~1.1KB 静态体；缓冲为 CPU 独占访问
（`osMessageQueue` 静态内存经 CPU memcpy，无 DMA/ETH），CCM 适用（CQ / GZ_OL 先例）。
SRAM 侧 **36B**（四 mask 16B + 队列句柄 4B + 屏体参数/自检运行态 4B + **0x49 记录缓冲 12B**）；
0x49 落盘经 `dev_w25qxx._write` 内部整扇区读-改-写，本模块**不持有 4KB 扇区镜像**。

| 口径（Makefile GCC Debug） | text | rodata | data | ccmram | bss | heap_stack | SRAM 合计 | SRAM 余量 |
|---|---|---|---|---|---|---|---|---|
| `PROTO=ALL` `DISP=1_263` | 170596 | 201736 | 1672 | **11164** | 126436 | 2564 | 130672 | **400B** |
| `PROTO=CQ` `DISP=1_263` | 157420 | 201088 | 872 | **11164** | 122960 | 2560 | 126392 | 4680B |
| `PROTO=ALL` `DISP=22_1665` | 172460 | 202896 | 1672 | **10460** | 126452 | 2564 | 130688 | **384B** |

（22_1665 行 ccmram 10460 为**第二十轮驱动定标态**：本模组 1792B + CQ 6377 + GZ_OL 1136 + YN_OL 1152 +
对齐；1_263 行 ccmram 11164 中本模组显存为该驱动文件当前 1×1 实验态 2496B。三口径互不可同编，见 §5。
**注（2026-09-17 第二十三轮）**：22_1665 驱动重构（去 RTT / 去探针 / 宏控几何）后该行 text/rodata/bss 变为
**170692 / 201744 / 126436**（SRAM 余 **400B**），**ccmram 10460 与本模组 1792B 不变**；本表其余数字属该轮 A/B 记录，不再重测。
**注（2026-09-17 第二十四轮）**：22_1665 几何彻底参数化（改模块数只改两个宏、链段/落点/帧长全派生）后，
该行 text/rodata 变为 **170772 / 201768**，**data 1672 / bss 126436 / ccmram 10460 / SRAM 余 400B 不变**，
tree `35b296d8`；**本模块（YN_OL）的增量与判据不受影响**。
**注（2026-09-17 第二十七/二十八轮）**：22_1665 接线模型改为默认「模型 B（每列一口独立线）」后，
该行 text/rodata 变为 **172164 / 202952**、**ccmram 39164（22_1665 本模组 3328B）**、bss 124712、SRAM 余 **2128B**；
第二十八轮收口整理（去 RTT 残留审计 + 格式对齐）**段尺寸零变化**。**工作区当前 22_1665 宏值 `MODULE_COLS = 5`（16×80）**
⇒ 该行 ccmram **44156**（本模组 8320B）/ bss 124716 / SRAM 余 2120B；以 `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` §0.7 为准。）

**注（2026-09-17 晚 联调修复轮 = 本轮，三口径全量重编实测）**：本轮修改 TCP 接收长度判据、
调度层 WAIT 预算、本模块 `'3'` 编码容错与逐帧诊断（§11），段尺寸以 `PROTO=ALL` `DISP=1_263`
（默认口径，heap 修复轮基线 171860/202592/38332/124712）为参照：

| 口径 | text | rodata | data | ccmram | bss | heap_stack | SRAM 合计 | SRAM 余量 | elf md5（当轮采样） |
|---|---|---|---|---|---|---|---|---|---|
| `ALL` `1_263` | **173556** | **203840** | 1672 | 38332 | 124732 | 2564 | 128968 | **2104B** | `34fd021addd8891607d6c2d1df2bb434` |
| `CQ` `1_263` | **160380** | **203184** | 872 | 36076 | 121256 | 2560 | 124688 | 6384B | `bd6e9fcd51b1d52569884bf501fbc654` |
| `ALL` `22_1665`（工作区 1×5） | **173812** | **204096** | 1672 | 44156 | 124736 | 2560 | 128968 | 2104B | `e72167414ec963537f6c48021c01dcf9` |

相对 heap 修复轮基线（171860 / 202592 / 1672 / 38332 / 124712 / 2560 / 128944 / 余 2128）：
**text +1696 / rodata +1248 / bss +20 / ccmram ±0**（bss 增量 = 调度层 WAIT 预算的
**按 RB 分槽**状态 `s_wait_active[3]` + `s_wait_tick[3]` + `s_wait_head_fp[3]`）；
SRAM 合计 128944 → **128968B（余 2128 → 2104B）**。**诊断设施 A/B**（同口径 `make APP_DIAG=0`：
text 169076 / rodata 198672 / bss 124732）⇒ **诊断设施当前总增量 Flash +9648B
（text +4480 / rodata +5168）、RAM +0**，其中本轮新增部分 = `[yn_ol]` 逐帧诊断 +
`[disp]` 预算到期告警 + post-boot `qfull/resync` 行。
零新增告警（唯一告警仍为 3 条既有 HAL `flash_ex`）。三口径复跑记录（含 hex/bin md5 与逐口径日志）
见 `.analysis/yn_ol/final_verify.out.txt` + `final_all_1263.log` / `final_cq_1263.log` /
`final_all_221665.log`；诊断 A/B 日志 `.analysis/yn_ol/final_ab_diagoff.log`。
**md5 含 `__DATE__/__TIME__`（横幅）→ 重新编译必然改变，镜像是哪份以板端 `[diag] fw=/built=/tree=` 为准。**

三口径**全部链接通过、零新增告警**（唯一告警为 3 条既有 HAL `stm32f4xx_hal_flash_ex.c`
`-Wunused-parameter 'Banks'`，行 948/1027/1063）。三口径段尺寸经**复跑逐项复现一致**（见 `.analysis/yn_ol/final_verify.out.txt`）。
产物 md5（2026-09-17 裁决轮最终全量重编；横幅内嵌 `__DATE__/__TIME__` → 重跑即变）：

```
ALL/1_263 （默认口径）elf 5095ba07ac53213aff135abf4d1706fd
                       hex 135f583d9575e045f7d6810200ff98aa
                       bin 3ba92705fc20de6ba33e5adf257e060c
CQ /1_263              elf a81cac0c45f62685e8aa369779e17fcb
                       hex 1a869c38defce51558acc179eaea4134
                       bin 5a4a87af6d4c082ec55e34f623ecf1b9
ALL/22_1665            elf 6556d01416d421f4c4f9a82aaac811e1
                       hex c5c70fbdd3e7ad20137ea1264e0fe30d
                       bin 3318ea4554e9f432ed13da10e80ba9fe
```

（最终三口径复跑脚本：`bash .analysis/yn_ol/final_verify.sh`；A/B 增量脚本仍为 `bash .analysis/yn_ol/ab_measure.sh`。）

> 横幅内嵌 `__DATE__/__TIME__`（`APP_DIAG_BANNER` 默认开）→ **重新编译必然改 md5**；
> 镜像身份以板端横幅 `fw=` + `built=` + `tree=` 为准。

**本模块增量（A/B 实测：宿主重链同一 `.o` 列表、仅增减本模块 3 个 `.o`）**：

| 口径 | text | rodata | data | ccmram | bss | SRAM 余量变化 |
|---|---|---|---|---|---|---|
| `ALL` `1_263` | **+3520** | **+680** | +0 | **+1152** | **+36** | 440 → **400B** |
| `PROTO=CQ` `1_263` | **+3520** | **+688** | +0 | **+1152** | **+36** | 4712 → **4680B** |
| `ALL` `22_1665` | **+3520** | **+680** | +0 | **+1152** | **+36** | 424 → **384B** |

（Flash 合计 **+4200 ~ +4208B**；CCM 1152B = 队列缓冲 801 + 控制块 80 + 任务帧缓冲 267 + 对齐 4；
SRAM 合计 **+40B / +32B / +40B**。）
**相对上一轮 4 文件模块（text +3232 / rodata +584 / bss +20）**：删除上电画面 `.o` 使代码减少，
但**新增 TCP 双通道注册 + 0x49 持久化（含 RTT 诊断）**净增 → **text +288 / rodata +96 / SRAM +16B**。
**SRAM 余量未被压负**（400B / 4680B / 384B 均 > 0）。

**ucHeap 影响**：常驻 1 个任务（`yn_ol_handle_task` 1KB）+ 1 个惰性任务
（`yn_ol_selftest` 256×4，收到 '2' 时创建、结束自退）——常驻支出 **+1KB 栈 + TCB/块头 ≈ +1.13KB**；
队列用静态内存（attr 指定 cb/mq），**不占 ucHeap**。上电画面删除后**已无 `yn_ol_blank` 5s 任务**。
FreeRTOS 堆水位尚未标定（`doc/05_协议模块多协议兼容优化/03_freertos_heap_side_effect.md` 待办，与 GZ_OL 同）。

> 复核说明：三口径模块增量均经 A/B 独立测量（宿主重链同一 `.o` 列表、仅增减本模块 3 个 `.o`）；
> 「无本模块」基线实测 `text 167076 / ccmram 10012 / bss 126400 / 余量 440B`（ALL/1_263，与既有
> 文档基线一致）、`text 153900 / ccmram 10012 / 余量 4712B`（CQ/1_263）、`text 168940 / ccmram 9308 /
> 余量 424B`（ALL/22_1665），证明增量可归因于本模块。复跑脚本：`bash .analysis/yn_ol/ab_measure.sh`。

---

## 7. `0x49` 屏体参数持久化（W25Qxx 12B 记录）

**裁决（Q5）**：`0x49`（字体 + 字宽）**需要持久化**——掉电保留、上电装载并生效。

**记录**（12B，仿 `Application/Src/LDI/app_ldi_cfg.c` 的 magic + 载荷 + CRC32 范式）：

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 4B | magic | `0x594E4F4C`（小端存 `'Y''N''O''L'`） |
| 4 | 2B | version | `1`（不匹配即视为无效） |
| 6 | 1B | font_type | `font_type_t`（0 宋 / 1 仿 / 2 楷 / 3 黑） |
| 7 | 1B | font_size | `FONT_16` / `FONT_24` / `FONT_32` |
| 8 | 4B | crc32 | 覆盖前 8B（STM32 硬件 CRC：poly `0x04C11DB7` / init `0xFFFFFFFF` / 无反转 / 无末异或） |

**存储位置 = `dev_storage_capacity(w25) - 12288`（倒数第三个 4KB 扇区）**。选址为**先验证再定**：

| 候选 | 核对结果（源码 + 静态扫描证据） | 结论 |
|---|---|---|
| ① 与 LDI 记录共享末扇区（`capacity - 4096`） | `dev_w25qxx._write` 对目标区间含非 `0xFF` 字节时**整扇区擦除后回写 4096B**（`dev_w25qxx.c:300`）；LDI 记录同样走 `dev_storage_write`（`app_ldi_cfg.c:70`）→ 两者互相擦除 | **不用**（须自备整扇区读-改-写保全 LDI 字节，风险高） |
| ② `capacity - 8192` 独立扇区 | **已被占用**：`app_render.c:372` `s_persist_addr = dev_storage_capacity(...) - 4096 * 2`（显示持久化记录） | **不用**（会与 app_render 记录互擦） |
| ③ **`capacity - 12288`**（本实现） | 扇区 2045（W25Q64，共 2048）/ 8189（MX25L256，共 8192）；全仓 `dev_storage_write/erase` 静态扫描**恰 3 个写入者**（`app_render.c` / `app_ldi_cfg.c` / 本模块）→ 本扇区独占；字库末字地址 ≈ 8,181,856B = **扇区 1997**（宿主按 94×94 全格保守上界 → 扇区 2018），MX25L256 字库止于**扇区 ~7498** | **采用**：独立扇区、零共享、无整扇区保全负担 |

地址取自 `dev_storage_capacity()`（不硬编码绝对地址）。**内存**：仅 12B 静态 SRAM 记录缓冲
（`s_yn_ol_cfg_rec`）；**无整扇区镜像**（读-改-写由 `dev_w25qxx._write` 内部完成）。

**语义**：
- **上电装载**：`sw_app_initcall` 的 `yn_ol_proto_init()` 末尾调 `yn_ol_screen_cfg_load()`。
  记录为空（全 `0xFF`）→ **静默**保持默认（首次上电正常路径）；magic / version / 字段范围 / CRC32
  任一不过 → 保持默认 + RTT 告警 `[yn_ol] 0x49 cfg INVALID at boot …`。
- **写入**：`0x49` 执行时先更新运行态，再 `yn_ol_screen_cfg_save()` 落盘；失败 → 运行态仍生效、
  掉电不保留 + RTT 告警 `persist FAILED (runtime only)`。
- **不阻塞、不重启**：装载/保存路径无阻塞等待、无 `NVIC_SystemReset`（宿主编译断言 10.19）。
- **生效范围**：装载/设置后的字号成为 `'3'` 单行、`'4'` 全屏、行清除（`0x42`~`0x46`、`0x50`）
  的行高与字形默认参数。

**默认值 = 沿用 STD 工程既有默认口径 `FONT_16` / `FONT_ST`**（裁决 Q9：**不单独修改**）：
与同为 `{` 帧族、协议未限定字号的青海（`app_qh_proto_cmd.c`）/贵州（`app_gz_proto_cmd.c`）一致，
也与 `app_default_display.c` 默认一致；云南常规用 `FONT_24` 是其协议明示 24 点阵所致，非口径冲突。

### 7.1 已删除：原上电默认画面（「祝您一路平安」+ 稍候熄灭）

**裁决（Q7）**：协议文档中的上电显示**不需要实现**。原 `app_yn_ol_proto_default.c` 及其
`app_default_display_register` 注册、5s 熄灭惰性任务（`yn_ol_blank`）与相关资源**已全部删除**；
`Makefile` / `.eide/eide.yml` / `CLAUDE.md` / 本文档中相关表述同步清理。
上电画面改由 STD 现有默认显示链路给出（`app_default_display.c`；本模块不注册则回退系统默认画面）。
（保留此小节仅为记录删除动作与原因，避免后续再从协议文档「上电效果」反推实现。）

---

## 8. 裁决结果（2026-09-17 用户对原待裁决清单 9 条的答复，逐条落地）

> 原 §8 待裁决清单 9 条（Q1~Q9）中，本次裁决覆盖 8 条；**原 Q4（'2' 自检文案）不在裁决范围 →
> 维持现状**，见文末「残余待确认项」。下表的「原 Q#」为原清单编号，「裁决#」为本次答复的条号。

| 裁决# | 原 Q# | 事项 | 结论 | 落地方式与证据 |
|---|---|---|---|---|
| 1 | Q6 | 是否需要网口控制 | **需要，注册 TCP** | 在 RS485 + RS232 之外**新增 `CH_ID_TCP_SERVER` + `CH_ID_TCP_CLIENT`**（四 mask 共用一队列，§3）。竞争处理**不靠源码收录序，而靠通道掩码隔离**：`app_dispatch.c:252/:303` 按 `ch_proto_map[ch->ch_id]` 过滤 → CQ 只绑 UDP/UDP_CQ，**TCP 链上 CQ probe 根本不被调用**；本模块**不绑 10011** → CQ 行为零削弱。反向安全：CQ JSON 第二字节 `'"'` 不在本 probe 白名单 → FAKE；半帧/粘包由 dispatcher「WAIT 继续下一协议」语义保证不卡链。证据：全仓绑定静态扫描 + 宿主推演 §9（9.1~9.32，含**反事实取证**：若绑 10011 本协议帧会被 CQ 抢走）。**未触碰 CQ 源码，无需确认项** |
| 2 | Q1 | `0x47`/`0x51` 端口字段字节序 | **维持高位在前 BE16，转正为已裁决** | 实现未变（写 9528 发 `25 38`）；本文档与头文件注释中「推断」措辞已改为「已裁决」；依据 = 同厂 GZ_OL（doc/14 §13.5.1）+ 本固件 LDI 12H/IAP 0x01 既有口径 |
| 3 | Q2 | `0x47` 应答与复位 | **维持当前策略，不改** | 回 `0x51`（14B 回显）→ `osDelay(100)` → `NVIC_SystemReset()`（对齐 GZ_OL `0x40`）；STD 约定「改 IP/端口重启生效」，唯一生效路径 |
| 4 | Q3 | `'8'` 亮度档位 | **维持当前策略，不改** | `0x00`/`'0'` = 恢复光敏自动；`'1'`~`'8'` = 挂起光敏 + 硬件 8 档恒等映射（文档口径 `'0'`~`'5'` 的超集） |
| 5 | Q5 | `0x49` 屏体参数是否持久化 | **需要持久化** | 新增 W25Qxx 独立 4KB 扇区（`capacity - 12288`）12B 记录 + 上电装载；选址与①/②候选的排除依据见 **§7**。失败/损坏 → 回退默认 + RTT 告警，不阻塞不重启 |
| 6 | Q7 | 协议文档的上电显示 | **不需要实现 → 删除** | `app_yn_ol_proto_default.c`、其注册与 5s 熄灭任务已删除；`Makefile` / `.eide/eide.yml` / `CLAUDE.md` / 本文档相关表述同步清理；见 **§7.1** |
| 7 | Q8 | 与云南常规的量产取舍 | **编译期强制二选一** | `g_brace_proto_guard` 对「云南常规 + 云南治超」这一对**实测生效**：两文件不带 `-DSTD_ALL_PROTO` 分别编译后 `arm-none-eabi-ld -r` 合并 → `multiple definition of 'g_brace_proto_guard'`（ld 退出码 1）；加 `-DSTD_ALL_PROTO` 复编后合并退出码 0。**量产（EIDE）必二选一（排除其一）**；复跑：`bash .analysis/yn_ol/guard_test.sh` |
| 8 | Q9 | 默认字号 | **与 STD 工程一致，不单独修改** | 默认 `FONT_16` / `FONT_ST`（与同为 `{` 帧族、协议未限定字号的青海/贵州一致；也与 `app_default_display.c` 默认一致）。文档中「由行清除命令推断 FONT_16」的表述已删除，改为「**沿用 STD 工程默认字号**」；0x49 可运行时改并持久化 |

**残余待确认项**（本次未覆盖或需现场核实）：
1. **原 Q4 `'2'` 自检显示内容与节奏**：本次 8 条裁决未涉及 → 维持「数字 1~9 交替、500ms/步、
   FONT_SELF_ADAPT 红字居中」（文档「固定汉字信息」内容未给）。
2. 上位机真实发帧与所用通道（串口 / TCP）待抓包确认。
3. `0x47` 载荷 LEN 字段实际取值（文档未给；本实现要求 ≥14、多余忽略）。
4. `'3'`/`'4'` 文本若上位机实际发 UTF-8（而非 GBK）需再评估。
5. 现场 DIP1（9600 → 置 OFF）、屏体几何、（若走 TCP）Server 监听端口 = `net_cfg.port`（出厂 9528）。

---

## 9. as-built 状态

| 项 | 状态 |
|---|---|
| probe / parse / 命令执行 | ✅ 已落地（19 个入站命令字：'1'~'5'/'8'/'A'、0x42~0x46、0x50、0x47~0x49、0x01、0x02；0x51 仅出站） |
| 绑定通道 | ✅ **RS485 + RS232 + TCP Server + TCP Client**（四 mask 共用一队列；**不绑 UDP 10011**） |
| 应答组帧 | ✅ '1'(4B) / 0x47、0x48→0x51(18B，BE16 端口) / 0x02（裸 ASCII），一律单播回源 |
| 上电默认画面 | ➖ **已删除**（裁决 Q7；上电走 STD 默认显示链路） |
| 0x49 屏体参数 | ✅ 运行态 + **持久化**（W25Qxx 独立扇区 12B 记录，上电装载；见 §7） |
| 自检 | ✅ '2' 数字 1~9 交替（惰性任务、可被下一帧打断、无语音） |
| 渲染互斥 | ✅ `app_scroll_render_lock` |
| 帧族互斥守卫 | ✅ `g_brace_proto_guard`（`ld -r` 实测：云南常规×云南治超 = multiple definition；见 §8 裁决 7） |
| 构建验证 | ✅ 三口径链接通过（§6 段尺寸与 md5）；本轮（联调修复轮）实测 text +1696 / rodata +1248 / bss +20 / ccmram ±0（对照 heap 修复轮基线），诊断设施 A/B **Flash +9648B / RAM +0** |
| 宿主推演 | ✅ `yn_ol_frame_sim.py` **184 用例通过 / 失败 0**（§1~§8 probe/parse/帧结构/互斥 97 + **§9 TCP 与 CQ 竞争 38** + **§10 0x49 持久化 24** + **§11 四通道回源 7** + **§12 源码断言 6** + **§13 联调轮容错/诊断断言 6** + **§3.24~3.29 编码容错镜像 6**） |
| 逐帧诊断 | ✅ `[yn_ol] rx/exec/drop`（`YN_OL_RTT_DIAG` 默认跟随 `APP_DIAG_BANNER`；单帧最多两次 RTT 打印；'3' 含屏体几何与落屏判读）——判读表见 **§11.2** |
| 实机联调 | 🔶 设备侧已在协议层修复（TCP 分片 / 卡帧预算 / '3' 编码容错，**§11.1**）；**待现场按 §11.3 重烧并回传 `[yn_ol]` 行** |
| 语音 / 固定格式显示 | ➖ 文档明示不开发，不接 |

---

## 10. 联调建议帧（按本实现推演构造，**须用真实上位机抓包核对后再用**）

| 用例 | 帧（hex） | 预期 |
|---|---|---|
| 主机查询 | `7B 31 00 7D` | 回 `7B 31 01 00 7D` |
| 全屏清除 | `7B 35 00 7D` | 整屏清黑 |
| 单行第 1 行红字 "A" | `7B 33 03 30 31 41 7D` | 第 1 行红色 "A" |
| 单行第 2 行绿字 "ETC车道" | `7B 33 09 31 32 45 54 43 B3 B5 B5 C0 7D` | 第 2 行绿色 "ETC车道" |
| 全屏红字 (0,44) "ETC车道已关闭"（文档示例） | `7B 34 10 30 00 2C 45 54 43 B3 B5 B5 C0 D2 D1 B9 D8 B1 D5 7D` | 清屏后 (0,44) 起红字 |
| 亮度自动 | `7B 38 01 00 7D` | 恢复光敏自动调光 |
| 绿灯开 | `7B 41 01 01 7D` | 绿灯亮、红/黄闪关 |
| 黄闪开（P6 治超屏口径） | `7B 41 01 04 7D` | 黄闪开 |
| 第一行清除 | `7B 42 00 7D` | 第 1 行清黑 |
| 第六行清除 | `7B 50 00 7D` | 第 6 行清黑（屏高不足时不落屏） |
| 设置屏体参数（宋体 16 点阵，**持久化**） | `7B 49 02 00 00 7D` | 后续 '3'/'4' 按 16 点阵；掉电后仍生效（§7） |
| 全屏点亮红色 | `7B 01 01 01 7D` | 整屏红 |
| 版本号 | `7B 02 01 00 7D` | 回裸 ASCII `9K13A127E0` + 屏显「版本:9K13A127E0」 |
| 查询 IP | `7B 48 00 7D` | 回 `7B 51 0E <ip4><mask4><gw4><port2 BE16> 7D`（`25 38`=9528） |
| 修改 IP（192.168.1.5/24 gw .1 port 9528，**BE16**） | `7B 47 0E C0 A8 01 05 FF FF FF 00 C0 A8 01 01 25 38 7D` | 回 0x51 回显 → 约 100ms 后软复位 |

**传输说明**：以上帧可走 **RS485 / RS232 串口**（波特率由 DIP1 选择，文档默认 9600 → **DIP1 置 OFF**），
也可走 **TCP 网口**（Server 监听 `net_cfg.port`，出厂 9528；本模块绑 `CH_ID_TCP_CLIENT` 只消费入站，**不** `set_remote`——未配置时 Client 任务 idle，不主动外连）。
**UDP 10011 不可用**（本模块不绑；与 CQ JSON 同首字节且 CQ 收录序更早）。四通道帧格式与语义完全一致，
应答一律单播回源通道（§8 裁决 1 / §1 probe 竞争结论）。

> 宿主脚本可生成/校验上述帧并输出链式竞争矩阵：`python3 .analysis/yn_ol/yn_ol_frame_sim.py`。

---

## 11. 逐帧诊断（`[yn_ol]` + `[tcp_*]` + `[disp]`）判读表 + 两轮联调修复（2026-09-17 晚）

> 背景（两轮现场症状）：
> **第一轮**：① 首次发「全屏点亮」无反应、重连后执行一次、此后不再受控需重启；② 「单行显示」没反应。
> **第二轮**（第一轮修复的镜像已烧录后仍见）：① TCP 连上板子后发命令 RTT 无反应、**再点一次「连接设备」
> （再建一次 TCP 连接）才打印解析数据**；② 「修改 IP」0x47 **完全无输出**。
> 本节 = 根因与修法（§11.1 第一轮三处、§11.5 第二轮三处盲区）、**RTT 判读表**（§11.2，含通道/调度/协议
> 三层前缀）、**现场步骤**（§11.3）与**三段判据**（§11.4）。诊断门控 `APP_DIAG_BANNER` /
> `YN_OL_RTT_DIAG` / `TCP_SRV_RTT_DIAG` / `TCP_CLI_RTT_DIAG` 默认全开（RTT 通道 0）。

### 11.1 本次修复（三处）

| # | 根因 | 位置 | 修法 |
|---|---|---|---|
| **A** | **TCP 1 字节分片被静默丢弃** → RB 头部留下永远凑不齐的残帧；重连清空 RB 后只能再执行一次，随后复发 | `Application/Src/Channel/app_tcp_server.c`（原 `if (len > 1)` 入调度）**与 `app_tcp_client.c`（同一缺陷，本轮一并修）** | 长度判据改为 `len > 0`（单字节分片交给 probe 链按 1 字节重同步语义处理；UDP 路径本就是 `len > 0`） |
| **B** | **WAIT 预算被「avail 增长」永久重置** → 残帧卡头时用户每发一帧新数据都把 1000ms 计时归零 ⇒ 强制重同步永不触发 ⇒ **整条 RB 永久不消费、后续帧全部无声丢弃** | `Application/Src/app_dispatch.c` `frame_dispatch_task` 的 `any_wait` 分支 | ① 进度判据改为**头部前缀指纹**（前 `min(4, avail)` 字节 + 参与字节数；头部不变则即使缓冲区更长也**不**重置计时）；② 预算 **500ms → 1000ms**（依据：9600bps 下最长帧 RLS 530B ≈ 552ms，500ms 会在长帧传完前到期、拦腰打断合法帧；1000ms 留 ~1.8× 余量，宏 `FRAME_WAIT_BUDGET_MS` 可覆盖）；③ 预算到期打 **`[disp]` 门控告警**（头部 8 字节 + avail + 累计次数）后再 `rb_skip(1)` |
| **C** | **'3' 单行的颜色/行号编码不匹配**（ASCII `'0'`/`'1'` 与二进制 `0x00`/`0x01` 两种上位机口径）→ 整帧 `ERR_PARAM` 静默丢弃 | `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_parse.c` | 新增 `_yn_ol_parse_color`（ASCII `'0'~'2'` 或 0x00~0x02）/ `_yn_ol_parse_row`（ASCII `'1'~'5'` 或 0x01~0x05）超集容错；'3' 颜色+行号、'4' 颜色均走该口径（**只放宽、不改语义**：越界值仍整帧丢弃；与已裁决的 '8' 亮度超集同风格） |

> **待现场确认的假设**：A/B 两处修复的因果关系基于代码路径推演（现场未抓 TCP 分片级日志）；
> 现场以 RTT 判读表（§11.2）为准——若重烧后仍出现「帧到了但屏幕无变化」，先看 `exec` 行的落屏判读、
> 再看是否有 `[disp] WAIT budget ... expired` 行。

### 11.2 诊断判读表（`[yn_ol]` / `[tcp_srv]` / `[tcp_cli]` / `[disp]` 四类前缀）

`[yn_ol]` 诊断门控 `YN_OL_RTT_DIAG`（默认跟随 `APP_DIAG_BANNER`，置 0 零输出零开销）。单帧输出：
**第 1 行 = 到达 + 解析**（来源通道 id/名、原始长度、原始 hex〔≤32B 全打，超长打前 32B + `cut`〕、
`sta`、命令字、关键字段），**第 2 行 = 执行结果或拒绝原因**。
表内后半部分为本轮（联调第二轮）新增的**通道层**（`[tcp_srv]`/`[tcp_cli]`）与**调度层**（`[disp] probe`/
`[disp] notify`）行——这三类行回答「字节有没有到板子 / 有没有进 probe / 通知有没有丢」，与 `[yn_ol]`
行配合可把症状定位到具体一层（判据见 §11.4）。

| RTT 输出（示例） | 判读 |
|---|---|
| `[yn_ol] rx ch=TCP_SRV(2) len=5 hex=7b 01 01 01 7d -> sta=OK cmd=0x01 fill_color=1`<br>`[yn_ol] exec cmd=0x01 ret=0` | 帧到达 TCP Server、解析执行成功（全屏点亮红） |
| 完全没有 `[yn_ol] rx` 行 | 帧**没到协议层**：查上位机连的通道/端口（TCP 监听 `net_cfg.port`，出厂 9528；串口看 DIP1 波特率）、开机 `[diag] chan ...` 行 |
| `... -> sta=ERR_FRAME` + `[yn_ol] drop sta=ERR_FRAME len=.. first=.. tail=.. declared=.. actual=..` | 帧头/帧尾/长度字段不符（对照第 1 行 hex；注意长度字段是**二进制字节值**，不是 ASCII 数字） |
| `... -> sta=ERR_CMD` + `[yn_ol] drop sta=ERR_CMD cmd=0x36` | 命令字不在本模块实现集合（`'6'/'7'/'9'` 文档明示治超屏不开发 → 拒） |
| `... -> sta=ERR_PARAM` + `[yn_ol] drop sta=ERR_PARAM cmd=0x33 declared=3 b0=0x33 b1=0x39 ...` | 参数长度/取值非法（b0/b1 = 参数区前两字节原始值） |
| `[yn_ol] rx ch=TCP_SRV(2) len=7 hex=7b 33 03 30 31 41 7d -> sta=OK cmd=0x33 color=0(ascii) row=0(ascii) text_len=1`<br>`[yn_ol] exec cmd=0x33 ret=0 color=0 row=0 text_len=1 y=0 screen_w=16 screen_h=32 rowh=16 -> ON-SCREEN` | '3' 单行：ASCII 编码；y=0 在屏内 → 应见第 1 行红字 |
| `[yn_ol] exec cmd=0x33 ret=0 ... y=32 screen_w=16 screen_h=32 rowh=16 -> **OFF-SCREEN(executed but invisible)**` | 行号换算后的 y 超出屏高 ⇒ **执行但不落屏**（协议语义正常）。16×32 屏 + FONT_16 只有 **2 行**可视：行号 '3'~'5' 本就不会显示；先发 `7B 49 02 00 00 7D` 可把字号改为 16 点阵/宋体重新确认，或改用行号 '1'/'2' |
| `[yn_ol] exec cmd=0x33 ... -> PARTIAL(bottom clipped, glyphs cut)` | 行内字形下缘被屏底裁掉（可见但缺底） |
| `... color=0(bin) row=1(bin)` | 该字段为**二进制编码**（上位机未用 ASCII）——两种都接受，此处仅标注来源 |
| `[disp] WAIT budget 1000ms expired -> resync 1B: avail=37 head8=7b 33 05 30 31 41 ... resync_total=3 (stuck frame head, see doc/15)` | **卡帧第一现场**：帧头匹配后 1s 内头部无前进 ⇒ 已强制 skip 1 字节重同步（head8 = 卡住的头部原始字节）。频繁出现 ⇒ 上位机帧被截断/长度字段错，或 TCP 分片异常 |
| `[disp] frame queue FULL -> drop: proto_idx=.. len=.. qfull=.. (protocol task stuck or too slow?)` | 协议帧队列满丢帧（`YN_OL_QUEUE_DEPTH=3`）；qfull 持续增长 = 处理任务被卡住/过载 |
| `[tcp_srv] listen port=9528 (accept loop)` | **通道层**（盲区 A，本轮新增）：TCP 监听就绪；无此行 = 该任务没跑起来（看 `[diag] chan tcp_srv=…`） |
| `[tcp_srv] bind FAIL port=.. err=..` | 监听端口绑定失败（如被占用），每 500ms 重试 |
| `[tcp_srv] accept 192.168.x.x:port` | 上位机 TCP 连接**已被板子接受**（含对端地址）。有 accept 但**没有**对应 `recv` = 该连接上根本没收到字节（工具行为/端口/发错连接） |
| `[tcp_srv] recv ch=tcp_server len=N head=…[ ..cut]` | 通道层收到字节（N = 本次 `netbuf` 分段长度，head = 前 ≤16B 原始 hex）。**分段多次出现说明 TCP 分片**；head 与上位机帧首字节比对可判「是不是本协议帧」 |
| `[tcp_srv] close  192.168.x.x:port reason=..` | 客户端断开（reason = `netconn_recv` 返回值；`ERR_CLSD`=−13 为对端正常关闭） |
| `[tcp_cli] idle (remote unset)` / `connecting` / `connected` / `connect FAIL` / `recv` / `close` | TCP **客户端**通道（YN_OL 亦绑 `CH_ID_TCP_CLIENT`）。**2026-09-18**：默认远端改为 `0.0.0.0:0`，未配置只打一次 `idle`、不 connect；`connect FAIL` 同错误码只打一行（仅远端已配置后） |
| `[disp] probe idx=6 sta=WAIT(1) avail=3 head8=7b 47 0e … -> wait more bytes (frame incomplete)` | **probe 决策第一现场**（盲区 B，本轮新增）：`{`（0x7B）帧已到 RB，有协议判「数据不够」（WAIT）→ 等更多字节。若约 1s 后仍无进展会跟进一条 `[disp] WAIT budget … expired` |
| `[disp] probe idx=6 sta=FAKE(2) avail=… head8=… -> no protocol claimed, resync 1B` | `{` 帧到达但**没有任何协议认领**（FAKE）→ 调度层逐字节重同步。看 `head8` 判「命令字不在白名单 / 长度字段与实际不符 / 尾字节不是 `7D`」；`idx` = 本条日志所报协议在注册表中的序号 |
| `[disp] notify queue FULL -> drop ch=.. notify_drop=.. (dispatch task stuck?)` | **通知队列满**（盲区 C 的兜底告警；本轮起先 20ms 重试一次，仍失败才打）。`notify_drop` 非 0 即说明曾发生「数据已进 RB 但无人唤醒分发任务」——与「再连一次才解析」直接相关 |
| `[yn_ol] 0x47 port field payload[12..13]=.. .. -> N (BE16); ip=… keep_udp_port=…`<br>`[yn_ol] 0x47 write ret=0 readback=valid port=N -> reboot in 100ms` | **0x47 修改 IP**：看到这两行 = 帧已到达协议层、端口按 BE16 解析、已写 Sector1，随后**重启属预期**。**完全没有这两行**（连 `[yn_ol] rx` 也没有）= 帧没到协议层 → 用上面 `[tcp_srv]` / `[disp] probe` 行分段定位 |
| `[diag] dispatch qfull=0 resync=0 notify_drop=0 (boot baseline; watch [disp] warnings)` | 开机基线（正常 0/0/0）；运行期增量看上面几条 `[disp]` 告警行 |
| `[diag] tip TCP diagnostics: [tcp_srv]/[tcp_cli] accept/recv/close; probe decisions [disp] probe ...` | 横幅末的提示行（本轮新增；仅提示相关诊断前缀，无判读含义） |

### 11.3 现场步骤（**本轮口径**，重烧后；含「两次连接」复现）

> 目标：把现场两条症状（① TCP 连上后首条命令 RTT 无输出、**再点一次「连接设备」才解析**；
> ② 「修改 IP」0x47 **完全无输出**）一次性定位到「通道层 / 探测层 / 通知层 / 协议层」四层中的哪一层。
> 关键：**每一步的 RTT 输出都要完整落盘**（三类行 `[tcp_srv]`/`[tcp_cli]`、`[disp]`、`[yn_ol]` 一起回传）。

1. **在 EIDE 中重新构建并烧录**（现场跑的是 EIDE 镜像；`.eide/eide.yml` 若被外部改动需先 `EIDE: Reload Project`），
   烧完按纪律**擦除 Sector1 恢复出厂态**（`bash tool/flash_all.sh` 默认行为；单固件烧录同理）。
   *（若改用 make 构建：EIDE 构建会覆写 `build/Debug/` 的 elf/hex/bin，此后 `make -j8` 可能**静默复用**
    EIDE 产物（实测口径不同也照样复用）——必须先 `make clean && make`，见 `doc/构建开关总表.md` §4.2 R1。）*
2. **连 RTT 并落盘**：JLinkRTTViewer，或
   `JLinkRTTLogger -Device STM32F407ZG -If SWD -Speed 4000 -RTTChannel 0 <file>`（推荐，证据可回传）。
3. **复位**，等 `[diag]` 横幅：确认 `fw=` / `built=` / `tree=`（＝这份镜像）、`display screen=WxH`、
   `dispatch qfull=0 resync=0 notify_drop=0`；并记下 `[diag] chan tcp_srv=.. port=..`（＝上位机该连的端口）。
   **注意 IP 口径**：EIDE Debug 的 `defineList` 含 `PROTO_CHONGQING` ⇒ 烧完擦掉 Sector1 后板子会以 CQ 口径
   默认 IP 启动（**192.168.1.5** / 端口 9528），横幅 `[diag] netcfg VALID ip=… port=…` 会直接给出实际值；
   上位机必须连这个 `ip:port`（用旧的 192.168.114.200 会连不上——那种情况下连 `accept` 都不会有）。
   另：EIDE Debug 的 excludeList 当前**已排除 LDI / 全部 `{` 族它省协议、只编 IAP + 云南治超（YN_OL）**，
   故本镜像里 `[yn_ol]` 行必然存在（若横幅里没有 `[yn_ol]` 相关输出，先查 yml 是否被改动、是否需 Reload）。
4. **点一次「连接设备」** → 应出现 `[tcp_srv] accept <ip>:<port>`（客户端连上来时还会看到
   `[tcp_cli] connecting/connect FAIL`，属正常，与本次症状无关）。
5. **发「全屏点亮」**（`7B 01 01 01 7D`）→ 期望 `[tcp_srv] recv …` + `[yn_ol] rx … cmd=0x01` + `exec cmd=0x01 ret=0`。
   **若此步无任何 `[tcp_srv] recv` 行** → 上位机没用这条连接发数据（工具行为），对应判据 ①。
6. **再点一次「连接设备」（复现现场症状）** → 观察是否「重连之后才出现第 5 步的解析行」（症状 ① 复现）。
   · 若重连后出现的 `recv` 与解析行是**第 5 步那条帧** → 通知/唤醒路径问题（判据 ③，本轮已修，看 `notify_drop`）；
   · 若重连后才**首次**出现 `recv` → 第 5 步的数据根本没用该连接发出（判据 ①）。
7. **发「修改 IP」0x47**：`7B 47 0E <ip4> <mask4> <gw4> <port2 BE16> 7D`
   （如写 192.168.114.200 / 255.255.255.0 / 192.168.114.1 / 9528 →
   `7B 47 0E C0 A8 72 C8 FF FF FF 00 C0 A8 72 01 25 38 7D`）→
   期望 `[yn_ol] rx … cmd=0x47` + `[yn_ol] 0x47 port field …` + `[yn_ol] 0x47 write ret=0 readback=valid`
   + **随后立刻重启（预期）**。
8. **回传整段 RTT 文本**（从 `[diag]` 横幅到 0x47 之后），不要只截一行——
   `[tcp_srv]`/`[tcp_cli]`/`[disp]`/`[yn_ol]` 四类行要一起发。

### 11.4 三段判据（本轮新增日志与既有 `[yn_ol]` 行的对应关系）

按「帧走到哪一层卡住」的顺序逐段排除：

| # | 现场观测 | 结论 / 下一步 |
|---|---|---|
| ① | 有 `[tcp_srv] accept`，但**没有**该连接的 `recv` | **上位机根本没在这条连接上发数据**（工具行为问题：发到了别的连接/别的端口，或「连接设备」按钮只建链不发帧）。去核对工具的发送目标端口（= `net_cfg.port`，出厂 9528）与它内部使用的 socket |
| ② | 有 `recv`，但**无** `[yn_ol] rx`，且伴随 `[disp] probe … sta=WAIT/FAKE` | **卡在探测/解析层**：`head8` 就是卡住的原帧头——`sta=WAIT` = 按长度字段算的整帧未到齐（看是否 TCP 分片/上位机漏发）；`sta=FAKE` = 无协议认领（命令字不在白名单 / 长度字段与实际长度不符 / 尾字节不是 `7D`）。若 1s 后跟着 `[disp] WAIT budget … expired` 说明已自动重同步 |
| ③ | 有 `[disp] notify queue FULL -> drop … notify_drop=N`，或「重连后才打印上一次发的帧」 | **通道通知被丢**（数据已进 RB 但没人唤醒分发任务）——本轮已修（20ms 重试 + 计数）；若现场仍出现，把 `notify_drop` 值回传（>0 即命中，且说明 20ms 内分发任务仍未排空，属重度过载，需另查谁在拖住分发任务） |
| ④ | 有 `[yn_ol] rx` 但**无** `exec` 行 | **解析拒绝**：紧跟的 `[yn_ol] drop sta=…` 给出原因（`ERR_FRAME` / `ERR_CMD` / `ERR_PARAM` 及原始字节） |
| ⑤ | 0x47 专项 | 看到 `[yn_ol] rx … cmd=0x47` 或 `[yn_ol] 0x47 …` 行 = 帧到达并已写 Sector1，**随后重启属预期**；完全没有 = 用 ①②③ 判链路；若只有 `rx` 无后续 = 看 `[yn_ol] drop`（`0x47` 要求 `declared ≥ 14`，少一个字节即 `ERR_PARAM`） |

### 11.5 本轮（联调第二轮）改动与增量

现场上一轮修复（§11.1 的 A/B/C）已烧入但仍见两条症状 ⇒ 本轮不猜根因，改为**把三处观测盲区补齐**
（A：通道层无日志；B：probe 决策无日志；C：通知丢失静默），并在调度层**修一处真实缺陷**。

| 盲区 | 位置 | 改动 | 日志/行为 |
|---|---|---|---|
| **A** | `Application/Src/Channel/app_tcp_server.c`、`app_tcp_client.c` | 新增门控日志（`TCP_SRV_RTT_DIAG` / `TCP_CLI_RTT_DIAG`，默认跟随 `APP_DIAG_BANNER`）：listen / bind FAIL / accept（对端 `ip:port`）/ recv（`len` + ≤16B hex）/ close（`reason=`） | `[tcp_srv] …` / `[tcp_cli] …`；单行 ≤96B，recv 循环里**每段一条**（不逐字节）；客户端连接失败同错误码只打一行 |
| **B** | `Application/Src/app_dispatch.c` `frame_dispatch_task` | 新增「`{`（0x7B）帧本轮**无任何协议消费**且判 WAIT/FAKE」时的决策日志，限速 = 三元组变化 + ≥ `PROBE_DBG_MIN_MS`（200ms），任何字节被消费即失效 | `[disp] probe idx=… sta=WAIT/FAKE(…) avail=… head8=… -> …`（probe 本身仍**纯函数**，日志在调度侧） |
| **C** | `Application/Src/app_dispatch.c` `app_channel_dispatch` | 通知投递由「`timeout=0` + 忽略返回值」改为「立即投递失败 → `DISPATCH_NOTIFY_RETRY_MS`（20ms）重试一次 → 仍失败 `g_dispatch.notify_drop++` + 门控告警」；新增 `app_dispatch_notify_drops()` getter，post-boot 体检行追加 `notify_drop=%u` | `[disp] notify queue FULL -> drop ch=… notify_drop=…`；`[diag] dispatch qfull=… resync=… notify_drop=…` |

**纪律遵守**：通道掩码/过滤语义未动；probe 契约与 READY/SKIP/FAKE/WAIT 语义未动；未改 git 配置、未提交。

**实测增量**（`PROTO=ALL` `DISP=1_263`，与 §6 的联调轮基线 173556/203840/1672/38332/124732/2564 对比）：

| 项 | text | rodata | data | bss | ccmram | SRAM 合计 / 余量 | elf md5（当轮采样） |
|---|---|---|---|---|---|---|---|
| **本轮后**（`APP_DIAG_BANNER=1`，默认口径） | **174996** | **204848** | 1680 | 124772 | 38332 | 129016 / **2056B** | `81dd185822345a848da284f3c2e349dc` |
| 本轮后（`APP_DIAG=0` 对照） | 169124 | 198672 | 1672 | 124736 | 38332 | 128968 / 2104B | `87ea3660007cde2e1fdad7414bcd2736` |
| 本轮后（`DISP=22_1665`，工作区 1×5） | 175252 | 205104 | 1680 | 124776 | **44156** | 129016 / **2056B** | `6b0489a67a034cfe9bf0d4b2034d5579` |
| **本轮增量**（相对联调轮基线） | **+1440** | **+1008** | +8 | +40 | **±0** | 余 2104 → **2056B** | — |

（md5 内嵌横幅 `__DATE__/__TIME__` ⇒ **每次重编必变**，镜像身份以板端 `[diag] fw=/built=/tree=` 为准；
四跑复现脚本 `bash .analysis/yn_ol/round2_verify.sh`，输出与逐口径构建日志
`.analysis/yn_ol/round2_verify.out.txt` + `round2_{diagoff,all_1263,all_221665,all_1263_final}.log`——
**四跑 exit=0、告警均为同样的 3 条既有 HAL**，段尺寸逐项复现一致。另按纪律补跑
`make clean && make -j8`（`.analysis/yn_ol/round2_makeclean_default.log`，默认口径 elf md5
`888c3abe14182b7baf2177134ebe85a8`）——本轮操作期间 EIDE 曾于 21:25 覆写 `build/Debug/` 产物，
之后的 `make -j8` **0 编译 0 链接、静默复用该 EIDE 产物**（口径不同也照样复用），故 `make clean`
是拿到 make 口径产物的唯一手段，详见 `doc/构建开关总表.md` §4.2 R1 本轮补充观测。）

- 本轮增量拆解：**诊断部分** +1392 text / +1008 rodata / +8 data / +36 bss（`[tcp_srv]`+`[tcp_cli]` 通道日志、
  `[disp] probe` 决策日志、post-boot `notify_drop` 字段）；**通知修复的常开部分** +48 text / +4 bss
  （重试逻辑 + `notify_drop` 计数器/getter）——后者与诊断开关无关，`APP_DIAG=0` 时仍在。
- **零开销验证**：`make APP_DIAG=0` 后 elf 内 `[tcp_srv]`/`[tcp_cli]`/`[disp] probe`/`notify queue FULL`
  字符串为 **0 条**（诊断变量全部在同一 `#if APP_DIAG_BANNER` 守卫内声明，diag-off 构建零告警）。
- 构建：`make clean && make -j8` 与 `make -j8 DISP=22_1665` 均 EXIT=0、**零新增告警**
  （唯一告警仍为 3 条既有 HAL `stm32f4xx_hal_flash_ex.c -Wunused-parameter`）。**镜像身份以板上
  `[diag] fw=/built=/tree=` 为准**（md5 内嵌 `__DATE__/__TIME__`，重编即变）。

---

## 12. 现场问题「TCP 连上无响应 / 需重启板子」根因取证与修复（2026-09-18 实机端到端）

> 本轮为**实机端到端**：RTT 取证（自研读取器，绕过 `JLinkRTTLogger` 的 `Control Block not found`；
> 详见 §12.5）、故障寄存器/heap/tick 非侵入轮询（不 halt）、三固件重烧（`tool/flash_all.sh` 一次会话 +
> 擦 Sector1）、28 次查询/应答回归 + 半开自愈实验 + 300s 长连接压测。
> 完整报告与原始日志：`.analysis/yn_ol/fix_and_verify.md` 与 `.analysis/yn_ol/fix_verify/`
> （`rtt_log.py` / `mcu_watch.py` / `yn_test.py` / `*.log`）。

### 12.1 三类根因（按实测证据强度排序）

| # | 症状 | 根因 | 决定性证据 |
|---|---|---|---|
| ① | **连上后发数据零反应**（发作时板上镜像） | **镜像里根本没有 `{` 帧族协议**：EIDE Debug 的 `excludeList` 把 `ProtocolParser_YunNan_Overload` 排除 ⇒ TCP 通道照常 accept/recv，但 `ch_proto_map[CH_ID_TCP_SERVER]=0`，帧被**静默吞掉**（一个协议都没被探测，连盲区 B 的 `[disp] probe` 也不会打印） | 板上 elf 的 `[yn_ol]`/`g_brace_proto_guard` 字符串计数 **0**；现场 RTT 有 `[tcp_srv] recv … head=7b 31 00 7d`、**无任何 `[yn_ol] rx`**；ping/TCP 握手全正常 |
| ② | **必须重启板子才恢复** | TCP Server「accept → 阻塞 `netconn_recv` 服务单客户端 → 断开 → 回 accept」串行循环的 accepted conn **无 keepalive、recv 无超时、TCP 通道不注册链路监听** ⇒ 对端「不发 FIN 就消失」（拔网线 / 交换机瞬断 / 上位机进程被杀 / 只关 UI 不关 socket）时循环被**永久占死**，后续连接内核握手成功（上位机显示已连接）却永远等不到应用层 accept/recv | 半开实验（DROP 掉 A 的 FIN）：**修复前**（上一轮）30s 零自愈；**修复后 17.0s 自愈**（= `keep_idle 10s + keep_cnt 3 × keep_intvl 2s`），RTT 留下 `[tcp_srv] close … reason=-13`（ERR_ABRT，看门狗式 keepalive abort） |
| ③ | **「板子自己死机 ~30s 后重启」** | **外部重编+重烧（EIDE/J-Link）造成的假象**，非固件缺陷 | 本轮实测抓到一次全过程：`build/Debug/Project_STD.elf` 被外部改写于 10:41:18 → 板子 10:41:24 失联并重启 → 之后运行的是**另一份镜像**（符号地址不同）；期间 RTT **无 `[err]`、无 HardFault（`g_fault_pc=0`）、`tick` 连续每秒 1000、heap 稳定 20656**；且复位后板上 `RTC_BKP_DR1`（Bootloader 仅在「上次复位是 IWDG」时 +1）= 0 ⇒ **不是看门狗复位** |

### 12.2 修复清单（file:line）

| 文件 | 改动 | 目的 |
|---|---|---|
| `Application/Src/Channel/app_tcp_server.c` | 新增 `tcp_server_keepaliveinit()`（`SOF_KEEPALIVE` + `keep_idle 10000` / `keep_intvl 2000` / `keep_cnt 3`，**参数与 `app_tcp_client.c` 既有口径一致**），在 accept 后调用；`netconn_new` 失败补 RTT 告警（原先完全静默 500ms 重试） | 修 ②；并让「netconn 池耗尽（连接超时）」与「循环占死（连上零应答）」在日志上可区分 |
| `Application/Src/app_dispatch.c` | `frame_dispatch_task` 新增**「通道无协议承载」告警**：`ch_proto_map[ch]=0` 时按通道限速 1s 打印 `[disp] ch=N has NO protocol bound -> rx data swallowed (build/excludeList check!)`；新增可调宏 `DISPATCH_NOPROTO_DBG_MIN_MS`（默认 1000，`-D` 可覆盖） | 修 ①的**可观测性**：这类「构建漏协议」故障此前 100% 静默 |
| `Application/Src/app_boot.c` | 横幅新增一行 `[diag] reset csr=… [iwdg/sft/por/pin/bor/lpw/wwdg] bkp0(force)=… bkp1(iwdg_cnt)=…` | 现场一眼区分「固件卡死被看门狗复位」与「外部烧录器复位/掉电」（`bkp1 ≥ 1` = 上一次是 IWDG 复位） |
| `Platform/Src/pl_sys.c` + `Platform/Inc/pl_sys.h` | 新增 `pl_sys_reset_cause()`（解码 `RCC->CSR` 复位标志，**纯读不清标志**） | 上一条的数据源；Platform 层提供，避免 Application 直接碰 HAL 寄存器（分层纪律） |
| `Platform/Inc/lwipopts.h`（USER CODE 段） | `LWIP_DBG_MIN_LEVEL` 由 `main.h` 的 `LEVEL_ALL` 收紧为 `APP_LWIP_DBG_LEVEL`（默认 `0x01 = LEVEL_WARNING`） | **根因取证前提**：LwIP 的 `tcp_slowtmr` 类 LEVEL_ALL 信息行实测 5~17 行/秒，1KB RTT 上行缓冲为 `NO_BLOCK_SKIP`（满则丢）⇒ 几十秒即灌满、**此后所有现场诊断静默丢失**（上一轮「死机前 RTT 无输出」的真因） |
| `Makefile` | 新增 `APP_LWIP_DBG_LEVEL` 透传（进 `DEFINES` ⇒ 进口径指纹 stamp）；默认不传 = 用头文件默认 0x01；`make APP_LWIP_DBG_LEVEL=0x00` 恢复全量 LwIP 日志 | 一键复原；并保证切口径必然全量重编 + 重链接 |

**不改**：`CQ` 及其它协议模块源码、`dev_display_*`、LwIP 源码、Bootloader/Recovery 工程（③ 的结论是环境问题，
未改引导链；`bkp0/bkp1` 用 Bootloader 既有语义）。

### 12.3 验证矩阵（板上 = `tree=7df8ac20`，`make -j8 DISP=22_1665`）

| 项 | 方法 | 结果 |
|---|---|---|
| 基线回归 | 单连接 `'1'` ×3 + 重连 ×3（`yn_test.py baseline`） | **6/6 正常**（应答 `7B 31 01 00 7D`，时延 0.4~0.6ms） |
| 崩溃复现窗 | 单连接存活 **200~312s**、每 10s 一次 `'1'`（原触发窗口 ~42s） | **40/40 全部应答**（200s 轮 16/16、312s 轮 24/24），**无死机、无看门狗复位**（RTT 无新横幅、`bkp1=0`） |
| 半开自愈（机制一） | DROP A 的 FIN → B 何时获服务 | **17.0s**（修复前 30s 零自愈）；RTT 实证 `[tcp_srv] close … reason=-13` → 回 accept → B 获服务；iptables 残留 0 |
| 无协议承载告警（机制二） | 静态复核 + 代码路径 | 新增告警位于 `ch_proto_map=0` 分支，诊断门控内（`APP_DIAG_BANNER=0` 零开销） |
| 长时稳定性 | RTT+MCU 轮询 340s 全程 | tick 连续（每秒 1000）、heap `free=20856 / min=18688` 稳定、**0 次 STALL / 0 次 HardFault / 0 次 malloc 失败** |
| 干扰核对 | 构建产物 md5 + 进程 | 全程 elf md5 不变（无外部重编/重烧） |
| 其它口径不破坏 | `PROTO=CQ DISP=1_263`、`PROTO=ALL DISP=1_263`、`PROTO=ALL DISP=22_1665` 三跑 | 全部链接通过、零新增告警（见 §12.4） |

### 12.4 段尺寸 A/B（Makefile GCC Debug，`PROTO=ALL DISP=22_1665`）

| 口径 | .text | .rodata | .data | .bss | .ccmram | SRAM 合计 | SRAM 余 |
|---|---|---|---|---|---|---|---|
| 修复前（`tree=3146c4f0`，elf `301080a0…`） | 175252 | 205104 | 1680 | 124776 | 44156 | 129016 | **2056B** |
| **修复后（`tree=7df8ac20`，elf `71e94c8f…`）** | **174640** | **203152** | 1680 | 124816 | 44156 | 129056 | **2016B** |
| Δ | **−612** | **−1952** | 0 | **+40** | 0 | +40 | −40 |

**归因**（两组实测）：① LwIP 档位 `LEVEL_ALL → WARNING` 单独贡献 **text −1044 / rodata −2216**（`APP_LWIP_DBG_LEVEL=0x00` 对照编译）；
② 本轮新增诊断与机制净增 **text +432 / rodata +264 / bss +40**（`APP_LWIP_DBG_LEVEL=0x00` 口径对修复前基线）。
显存 CCM 不变（22_1665 1×5 = 8320B，本模组口径 `1408M + 256·COLS`）；告警仍为既有 HAL 3 条。

### 12.5 现场判读方法（本轮新增，务必先读）

1. **RTT 必须被「读走」才有后续诊断**：RTT 上行 1KB 且 `NO_BLOCK_SKIP`（满则整条丢）。
   开机横幅 + 通道启动日志已占满约 1KB，此后若不接读取器，**任何现场诊断都进不来**。
   取证用 `.analysis/yn_ol/fix_verify/rtt_log.py`（`--drain` 先把积压倒出，再持续读；`--mcu` 同时轮询
   故障寄存器/heap/tick/任务名，**全程不 halt**）。`JLinkRTTLogger` 在本机对该目标报
   `RTT Control Block not found`（探针/DLL 9.50 组合问题），本轮改用自研读取器（按 `nm` 取 `_SEGGER_RTT`
   地址或扫描 SRAM 签名）绕开。
2. **横幅 `reset`/`bkp1` 行**：`bkp1(iwdg_cnt) ≥ 1` ⇒ 上一次复位是**固件卡死被看门狗复位**（真死机）；
   `bkp1 = 0` ⇒ 非 IWDG 复位（外部烧录器 SYSRESETREQ/NRST、掉电、软件复位）。`csr` 位因 Bootloader
   跳转前 `HAL_RCC_DeInit()` 清标志而通常为 0，留着供将来 Bootloader 改为留档时使用。
3. **`[disp] ch=N has NO protocol bound`** ⇒ 板上镜像没编进该通道的协议（**先查 EIDE 排除表 / 构建口径**，
   别再怀疑链路）；配合 `.eide/eide.yml` 改完必须 `EIDE: Reload Project`。
4. **`[tcp_srv] accept/recv/close`**：只有 `accept`+`recv` 没有 `[yn_ol] rx` ⇒ 帧进了 RB 但协议层没消费
   （本轮的机制 ①/`[disp]` 告警）；完全没有 `accept` ⇒ 服务循环被别人占死（机制 ②，看 `close reason=-13` 是否出现）。
5. **测试环境纪律**：**测试期间禁止 EIDE 构建 / 烧录**——外部重烧会让板子失联 10~30s 后重启，
   与「固件死机」在报文层几乎无法区分（本轮实测，见 §12.1 ③）。

### 12.6 遗留风险

- 板内 LDI 配置（W25Qxx）把 TCP Client 远端指向 `192.168.0.127:9528`（不可达）⇒ **仅当 LDI 编入
  并 `set_remote` 之后**才会 1Hz 重试 + 每轮 ARP 网关（现场「1Hz ARP 心跳」的真身）。
  **2026-09-18 通道层治理**：默认改为未配置不连（`0.0.0.0:0`）；排除 LDI 的镜像（含现行 EIDE
  YN_OL-only）不再自动外连，该 1Hz ARP 不再出现。LDI 编入且 W25 已有 host 时仍按配置连接
  （属既有风险，本轮未改 LDI 默认 / 未改 `MEMP_NUM_TCP_PCB`）。当前 `MEMP_NUM_TCP_PCB` 取 LwIP
  默认 5，频繁 connect/断开场景仍建议随通道数复核。
- `pl_net.c` 链路回调**只在 link-up 通知**（`app_udp` 三实例注册的「断链重建」实际从未被 down 事件触发）；
  本轮 keepalive 已覆盖半开场景，该缺陷留待单独一轮。
- 22_1665 工作区宏值为 1×5（16×80），非现场 1×2 口径；本模组显存随 `COLS` 变化，改宏须重编重烧。

---

## 13. 修订记录

- 2026-09-18 **TCP Client 未配置不连（通道层治理，非协议改动）**：`app_tcp_client` 默认远端改为
  `0.0.0.0:0`，任务仍启动但空转；排除 LDI 后不再对硬编码地址 1Hz SYN/ARP。YN_OL 仍绑
  `CH_ID_TCP_CLIENT`，`0x47` 仍只改设备自身 `net_cfg`。详见 `doc/CLAUDE.md` 网络子系统、
  doc/07 §12/§14、doc/08-03。

- 2026-09-17 晚 **联调第二轮（本轮）**：上一轮修复的镜像已现场烧录，症状仍在（TCP 连上后首条命令 RTT 无输出、
  **再点一次「连接设备」才解析**；「修改 IP」0x47 完全无输出）⇒ 本轮**不猜根因**，补齐三处观测盲区并修一处缺陷：
  **A** 通道层日志（`app_tcp_server.c`/`app_tcp_client.c`：listen/bind FAIL/accept/recv/close，门控
  `TCP_SRV_RTT_DIAG`/`TCP_CLI_RTT_DIAG`）；**B** 调度层 probe 决策日志（`{` 帧无协议消费时 `[disp] probe …`
  限速打印，`PROBE_DBG_MIN_MS=200`，probe 仍为纯函数）；**C** 通知投递缺陷修复（`app_channel_dispatch`：
  立即 + `DISPATCH_NOTIFY_RETRY_MS=20ms` 重试 → 仍失败 `notify_drop++` + 门控告警 + `app_dispatch_notify_drops()`
  + post-boot 体检行），**该修复非诊断、常开**（+48B text / +4B bss）。同步 §11.2 判读表（新增 12 行）、
  §11.3 现场步骤（改为含「两次连接」复现的 8 步）、**§11.4 三段判据**（含 0x47 专项行）、§11.5 改动与增量。
  0x47 链路复核结论：probe 白名单 → `declared ≥ 14` → BE16 解析 → 执行入口两条 `[yn_ol] 0x47 …` 日志
  → 写 Sector1 → 回 0x51 → `osDelay(100)` + `NVIC_SystemReset()`，**全路径均有日志、无需改代码**。
  构建：`make clean && make -j8`、`make -j8 DISP=22_1665`、`make APP_DIAG=0` 对照三跑 EXIT=0、零新增告警；
  段尺寸/md5 见 §11.5。**未烧录、未提交、未改 git 配置、未动无关文件**；现场部分明确标注「待现场判读」。
- 2026-09-17 晚 **联调修复轮**：现场两现象（TCP 控制首次无反应 / 之后不再受控需重启；'3' 单行无反应）
  根因定位与修复三处——**A** TCP 1 字节分片丢弃（`app_tcp_server.c` + `app_tcp_client.c` 同缺陷，`len > 1` → `len > 0`）；
  **B** 调度层 WAIT 预算被 avail 增长永久重置（`app_dispatch.c` 改「头部前缀指纹」进度判据 + 预算 500ms→1000ms +
  `[disp]` 到期告警）；**C** `'3'`/`'4'` 颜色、`'3'` 行号 ASCII/二进制**超集容错**（`app_yn_ol_proto_parse.c`）。
  本模块同时落地**逐帧诊断**（`[yn_ol] rx` / `exec` / `drop` 三函数，单帧最多两次 RTT 打印；
  `yn_ol_execute_cmd` 改为返回状态码；'3' 诊断含屏体几何与 `ON-SCREEN/PARTIAL/OFF-SCREEN` 判读）；
  post-boot 体检新增 `qfull/resync` 基线行（`app_boot.c`）。
  三口径全量重编通过（零新增告警），段尺寸/md5 见 §6；宿主推演 `yn_ol_frame_sim.py` 扩至 **184 用例全通过**
  （新增 §3.24~3.29 编码容错 6 + §13 源码断言 6）；**未烧录、未提交、未改 git 配置**。
- 2026-09-17 裁决轮：用户对原待裁决清单 9 条给出答复（覆盖 8 条），逐条落地：
  ① TCP 双通道绑定（四 mask 共队列；以**通道掩码隔离**解决与 CQ 的 `'{'` 同首字节竞争，
  **未触碰 CQ 源码**）；② 端口字段 BE16 转正；③ 0x47 应答+复位维持；④ 亮度超集维持；
  ⑤ `0x49` **持久化**（W25Qxx 独立扇区 `capacity - 12288` 12B 记录 + 上电装载，选址先验证后定，§7）；
  ⑥ 删除上电默认画面模块/注册/5s 任务并同步清理 Makefile/EIDE/文档（§7.1）；⑦ 守卫互斥对
  「云南常规 × 云南治超」`ld -r` 实测报 `multiple definition`，明确量产二选一；
  ⑧ 默认字号明确为**沿用 STD 工程默认** `FONT_16`/`FONT_ST`（去掉「由行清除命令推断」表述）。
  三口径重构建通过（零新增告警）、A/B 增量与 md5 见 §6；宿主推演扩至 **172 用例全通过**；
  `.eide/eide.yml` 删除 default 条目（**需 EIDE: Reload Project**）。
  **未烧录、未提交、未改 git 配置、未动无关文件。**
- 2026-09-17：新建。协议文档抽取（`.analysis/yn_ol/doc_extract.txt`，libreoffice txt + 结构化整理）；
  模块落地（3 头文件 + 3 源文件，初版含 4 源文件）；Makefile 双口径收录 + `eide.yml` 收录并排除（需 Reload）；
  三口径构建通过 + A/B 增量实测 + 守卫互斥实测；宿主推演；待裁决清单 9 条（原 §8）；
  doc/15 本文档 + `CLAUDE.md` / `doc/05-01` / `doc/06-04` / `doc/构建开关总表.md` 同步。
  **未烧录、未改 git 配置、未动其它协议模块。**
  **同日决策修订（初版不绑 RJ45 槽 → 本轮改为绑 TCP）**：初版按 GZ_OL 先例绑 `CH_ID_UDP`（10011）；
  复核 CQ probe（`cq_probe_frame` 对 `{` 开头数据做花括号深度扫描）后发现它会抢先认领本协议帧
  （`7B 31 00 7D` 被当作完整 JSON，CQ 收录序更早）→ 绑 10011 实际不可用，初版改为只绑
  RS485 + RS232；**本轮裁决 Q1 要求网口控制 → 改为绑 TCP Server/Client（掩码隔离，不与 CQ 同槽）**。
- 2026-09-18 **现场问题端到端修复轮（本文件 §12）**：对「YN_OL TCP 测试时连接后无响应 / 需重启」做
  实机端到端取证与修复。**RTT 打通**（自研 `rtt_log.py` 绕过 `JLinkRTTLogger` 的 Control Block not found；
  `--mcu` 非侵入轮询故障寄存器/heap/tick/任务名）。三类根因：① **发作镜像里没有 `{` 帧族协议**
  （EIDE Debug 排除 `ProtocolParser_YunNan_Overload`；`[tcp_srv] recv` 有、`[yn_ol] rx` 无，全程静默）；
  ② **accepted conn 无 keepalive** ⇒ 半开/孤儿连接永久占死串行服务循环（修复后自愈 17.0s，修复前 30s 零自愈）；
  ③ 「板子自己死机 30s 后重启」**实测为外部 EIDE 重编+重烧**（抓到 elf 被外部改写 10:41:18 → 板子 10:41:24
  重启且换了镜像；期间无 `[err]`/无 HardFault/heap 稳定/tick 连续、`bkp1=0` 非 IWDG）。
  **修复**：TCP Server keepalive（参数对齐 TCP Client）+ `netconn_new` 失败告警 + `[disp]`「通道无协议承载」
  告警（新增宏 `DISPATCH_NOPROTO_DBG_MIN_MS`）+ 横幅复位原因/引导计数行（新增 `pl_sys_reset_cause()`）
  + LwIP 调试档位收紧（`APP_LWIP_DBG_LEVEL`，默认 WARNING；新增 Makefile 透传开关）。
  **验证**：三固件重烧（`tool/flash_all.sh` + 擦 Sector1）后 `tree=7df8ac20`；baseline 6/6、长连接 200s/312s
  共 40/40 应答、半开自愈 17.0s、340s 无复位无异常、三口径重编通过零新增告警；
  A/B 段尺寸 `text −612 / rodata −1952 / bss +40`（另测 LwIP 档位单独贡献 text −1044 / rodata −2216）。
  **未提交 git、未改 git 配置**；测试期间未改其它协议模块与引导链。
