# YN_OL 联调第二轮：通道层 / 调度层观测盲区补齐 + 通知投递缺陷修复（2026-09-17 晚）

> 现场状态：上一轮修复（TCP `len > 0`、WAIT 预算前缀判据、`[yn_ol]` 逐帧诊断）**已烧录**，
> 但仍有两条症状：
> ① 上位机 TCP 连上板子 IP 后，软件发「全屏点亮」时 RTT 无反应；**再点一次「连接设备」
> （再建一次 TCP 连接）之后，RTT 才打印出解析数据**；
> ② 上位机发「修改 IP」（0x47）指令，RTT **完全没有**解析到。
>
> 本轮原则：**不猜根因**，先把「字节到哪一层、卡在哪一步」变成 RTT 里可直接读出的事实，
> 同时修掉在代码复核中发现的一处真实缺陷（通知投递）。
> 硬件在用户手上 —— **凡未上机验证的结论均在本文中标注「待现场判读」**。

---

## 1. 根因复核结论（代码级，非上机结论）

### 1.1 已定位的三处「观测盲区」（本轮修的就是它们）

| 盲区 | 位置（改前实态） | 为什么它让现场两条症状「无法判读」 |
|---|---|---|
| **A 通道层无日志** | `Application/Src/Channel/app_tcp_server.c` `tcp_server_task` 对 `netconn_accept` / 每次 `netconn_recv` / 断开**全程静默**；`app_tcp_client.c` 同理（YN_OL 也绑 `CH_ID_TCP_CLIENT`） | 无法回答「字节到底有没有到板子、到在哪条连接上、来了几段」——症状①「重连才解析」既可能是「工具没用那条连接发」也可能是「通知丢了」，两种解释在改前**都不可证伪** |
| **B probe 决策无日志** | `Application/Src/app_dispatch.c` `frame_dispatch_task`：协议 probe 返回 WAIT / FAKE 时**静默**（无消费就 skip 1 字节或等字节） | 「帧到了但被拒 / 永远等不齐」与「帧压根没到」在 RTT 上**长得一模一样**（都没有 `[yn_ol] rx`）→ 无法区分探测层与通道层 |
| **C 通知投递失败静默** | `app_dispatch.c` `app_channel_dispatch()` 末尾 `osMessageQueuePut(g_dispatch.ch_queue, &ch, 0, 0)` —— **超时 0 且返回值被忽略** | 队列满时通知被**静默丢弃**：数据虽已写进 RB，但没有事件唤醒 `frame_dispatch_task`，必须等**下一条**通知到达才被顺带处理。**与症状①「再连一次/再发一次才解析」的时序特征吻合**（重连/再发本身产生新通知，把上一次积压的帧顺带带走） |

### 1.2 三段判据（本轮新增日志 ↔ 现场症状的对应关系）

按数据流顺序逐段排除（现场判读用，详见 `doc/15` §11.4）：

| # | 现场看到的 RTT | 结论 |
|---|---|---|
| ① | 有 `[tcp_srv] accept <ip>:<port>` 但**没有**该连接的 `recv` | 上位机**没在这条连接上发数据**（工具行为/端口/发错连接），与固件无关 |
| ② | 有 `recv`，但无 `[yn_ol] rx`，且伴随 `[disp] probe … sta=WAIT/FAKE` | **卡在探测层**：`head8` = 卡住的原帧头；`WAIT` = 按长度字段算整帧未到齐，`FAKE` = 无协议认领（命令字不在白名单 / 长度字段与实际不符 / 尾字节不是 `7D`） |
| ③ | 有 `[disp] notify queue FULL -> drop … notify_drop=N`，**或**「重连后才打印上一次发的帧」 | **通道通知被丢**（数据已进 RB 无人唤醒分发任务）——本轮已修（20ms 重试 + 计数）；仍出现则说明 20ms 内分发任务仍未排空（重度过载，另查谁拖住它） |
| ④ | 有 `[yn_ol] rx` 但无 `exec` 行 | 解析拒绝，紧跟的 `[yn_ol] drop sta=…` 给出原因（`ERR_FRAME`/`ERR_CMD`/`ERR_PARAM`） |

### 1.3 0x47「修改 IP」链路复核（症状②）—— 代码侧结论：**链路完整、全路径有日志、无需改代码**

复核路径（`app_yn_ol_proto_parse.c` / `app_yn_ol_proto_cmd.c`）：

1. probe：首字节 `0x7B` + 命令字白名单（`yn_ol_cmd_from_byte()` 含 `0x47`）+ 长度/尾字节校验
   （`frame_len = declared + 4`，尾字节必须 `0x7D`）；
2. 解析：`case YN_OL_PCMD_SET_IP`，要求 **`declared ≥ 14`**（多余字节忽略），端口字段
   **高字节在前 BE16**（`data[12]<<8 | data[13]`）；
3. 执行：`_yn_ol_exec_set_ip()` —— 进函数即打
   `[yn_ol] 0x47 port field payload[12..13]=.. .. -> N (BE16); ip=… keep_udp_port=…`，
   写 Sector1 后打 `[yn_ol] 0x47 write ret=.. readback=.. port=.. -> reboot in 100ms`，
   回 `0x51`（14B 回显）→ `osDelay(100)` → `NVIC_SystemReset()`（**重启属预期**）；
4. 帧处理任务对**每帧**先打 `[yn_ol] rx ch=… len=… hex=… -> sta=… cmd=…`（`YN_OL_RTT_DIAG` 默认跟随
   `APP_DIAG_BANNER`，现场默认开启）。

⇒ 症状②「RTT 完全没有」只能是**帧没到协议层**（通道层/探测层），因此判读必须依赖本轮新增的
`[tcp_srv]` + `[disp] probe` 行。**未排查到 0x47 代码路径缺陷。**

> 需要现场核对的一点（**待现场判读**）：上位机发的 0x47 载荷是否 ≥14B、端口两字节是否为
> 高字节在前（写 9528 应发 `25 38`）。若发成 `38 25` 会被解析为 14373（仍会执行、会重启），
> 若少一个字节则解析层直接 `ERR_PARAM`，此时 `[yn_ol] drop sta=ERR_PARAM cmd=0x47 declared=..` 可见。

---

## 2. 改动清单（5 个文件，净增 ≈ 320 行，其中注释占多数）

| 文件 | 净增行（含注释） | 内容 |
|---|---|---|
| `Application/Src/Channel/app_tcp_server.c` | ≈ 96 | 门控 `TCP_SRV_RTT_DIAG`（默认跟随 `APP_DIAG_BANNER`）：`listen` / `bind FAIL` / `accept FAIL` / `accept <ip>:<port>` / `recv ch=tcp_server len=N head=<≤16B hex[ ..cut]>` / `close <ip>:<port> reason=err`；`recv` 循环**每段一条**（不逐字节）；`recv_err` 记录断开原因并 `(void)` 消除告警 |
| `Application/Src/Channel/app_tcp_client.c` | ≈ 72 | 门控 `TCP_CLI_RTT_DIAG`：`connecting` / `connected` / `connect FAIL err=..`（**同错误码只打一行**，成功连接后复位去重状态，防每秒重试刷屏）/ `recv` / `close reason=..`，前缀 `[tcp_cli]` |
| `Application/Src/app_dispatch.c` | ≈ 136 | ① 盲区 B：`{`（0x7B）帧**无任何协议消费**且判 WAIT/FAKE 时打 `[disp] probe idx=… sta=WAIT/FAKE(…) avail=… head8=… -> …`；限速 = 「(idx, sta, 头部指纹) 有变化」+ ≥ `PROBE_DBG_MIN_MS`(200ms)；任何字节被消费即失效；**probe 本身仍为纯函数（日志在调度侧）**。② 盲区 C：`app_channel_dispatch` 通知投递改为「立即 → `DISPATCH_NOTIFY_RETRY_MS`(20ms) 重试 → 仍失败 `notify_drop++` + 门控告警」；新增 getter `app_dispatch_notify_drops()`。③ 两个新宏 `#ifndef` 可覆盖 |
| `Application/Inc/app_dispatch.h` | ≈ 8 | `dispatch_ctx_t` 新增 `notify_drop` 计数（注释说明原静默缺陷）+ getter 声明 |
| `Application/Src/app_boot.c` | ≈ 9 | post-boot 体检行追加 `notify_drop=%u`（基线 0/0/0）+ 一行 `[diag] tip TCP diagnostics: …` |
| **文档** | — | `doc/15`（§11 标题/引子、§11.2 判读表 +12 行、§11.3 现场步骤改 8 步、**§11.4 三段判据**、**§11.5 改动与增量**、§12 修订记录）；`doc/构建开关总表.md`（§1 新增 4 个开关条目 + `APP_DIAG_BANNER` 行补 2026-09-17 复测、§5 修订记录）；`doc/CLAUDE.md`（YN_OL 小节 + 调试小节） |

**纪律**：通道掩码/过滤语义未动；probe 契约与 READY/SKIP/FAKE/WAIT 语义未动；未改 git 配置、未提交、未动无关文件。

---

## 3. 构建结果（三口径 + 诊断 A/B，全部复跑落盘）

复跑脚本 `bash .analysis/yn_ol/round2_verify.sh` → 输出 `round2_verify.out.txt`，
逐口径日志 `round2_{diagoff,all_1263,all_221665,all_1263_final}.log`。**四跑 exit=0**，
告警均为**同样的 3 条既有 HAL**（`stm32f4xx_hal_flash_ex.c` 行 948/1027/1063 `-Wunused-parameter`），
**零新增告警**。

| 口径 | text | rodata | data | bss | ccmram | SRAM 合计 / 余量 |
|---|---|---|---|---|---|---|
| `ALL` `1_263`（默认，诊断开） | **174996** | **204848** | 1680 | 124772 | 38332 | 129016 / **2056B** |
| `ALL` `1_263` + `APP_DIAG=0` | 169124 | 198672 | 1672 | 124736 | 38332 | 128968 / 2104B |
| `ALL` `22_1665`（工作区 1×5） | **175252** | **205104** | 1680 | 124776 | **44156** | 129016 / **2056B** |

**本轮增量**（相对上一轮联调轮基线 `173556 / 203840 / 1672 / 38332 / 124732 / 2564`）：

- text **+1440** / rodata **+1008** / data +8 / bss **+40** / ccmram **±0**；SRAM 余量 **2104 → 2056B**（仍 >0）。
- 拆解：**诊断部分** +1392 text / +1008 rodata / +8 data / +36 bss；
  **通知修复的常开部分** +48 text / +4 bss（与诊断开关无关，`APP_DIAG=0` 时仍在）。
- **零开销验证**：`make APP_DIAG=0` 后 elf 内 `[tcp_srv]` / `[tcp_cli]` / `[disp] probe` /
  `notify queue FULL` 字符串 **0 条**；诊断变量全部在同一 `#if APP_DIAG_BANNER` 守卫内声明 →
  diag-off 构建零告警（无「已设未用」）。
- md5 内嵌横幅 `__DATE__/__TIME__` ⇒ 每次重编必变，**镜像身份以板端 `[diag] fw=/built=/tree=` 为准**
  （本轮复跑 elf md5 见 `round2_verify.out.txt`）。

### 3.1 构建期发现：EIDE 产物覆写 `build/Debug/`，`make -j8` 静默复用它（R1 风险实测命中）

本轮操作期间 **EIDE 于 21:25 完成一次构建**（CQ 口径 + `DISP=22_1665`：`.text=136904 .rodata=110768
.ccmram=19724`，内含本轮新增诊断字符串），覆盖了 `build/Debug/Project_STD.elf/.hex/.bin`。紧接着的
`make -j8`（默认口径 `ALL`/`1_263`）**0 编译 0 链接**，其打印的 size 即 EIDE 产物尺寸——**口径都不同
也照样复用**（stamp 规则只比 mtime，不比「elf 是哪个口径产出的」）。
按纪律执行 **`make clean && make -j8`** 后恢复 make 口径：`.text=174996 .rodata=204848 .data=1680
.ccmram=38332 .bss=124772 ._user_heap_stack=2564`（SRAM 129016B / 余 2056B）、elf md5
`888c3abe14182b7baf2177134ebe85a8`（日志 `.analysis/yn_ol/round2_makeclean_default.log`）。

⇒ 现场烧录前**务必确认 flash 产物来源**：若最后一次构建是 EIDE，请直接烧 EIDE 产物（或先
`make clean && make` 再烧 hex）；给 `doc/构建开关总表.md` §4.2 R1 已补此条实测观测。

> ⚠ **本轮 `make` 产物是 `PROTO=ALL` + `DISP=1_263` 口径（显示驱动 = 1_263），不是现场板上的
> 22_1665 模组口径**，因此：
> - 我的 `make clean` 已删除 EIDE 在 `build/Debug/` 下的产物 —— **现场请用 EIDE 重新构建后再烧录**
>   （EIDE Debug = CQ netcfg + 22_1665 + IAP + YN_OL，正是需要的口径与模组）；
> - 若一定要用 make 产物烧现场板，必须 `make -j8 DISP=22_1665`（`ALL` 口径，含全部协议）——
>   但现场联调只需 YN_OL，故 EIDE 口径更贴近现场。

---

## 4. 给用户的现场步骤与要回传的内容

### 4.1 步骤（重烧后，含「两次连接」复现）

1. **EIDE 重新构建并烧录**（`.eide/eide.yml` 若被外部改动，先 `EIDE: Reload Project`）；
   烧完**擦除 Sector1**（`bash tool/flash_all.sh` 默认行为；单固件烧录同理）。
2. **接 RTT 并落盘**（推荐）：`JLinkRTTLogger -Device STM32F407ZG -If SWD -Speed 4000 -RTTChannel 0 <file>`，
   或 JLinkRTTViewer（手动复制全文）。
3. **复位**，等横幅，确认：`fw=` / `built=` / `tree=`、`display screen=WxH`、
   `dispatch qfull=0 resync=0 notify_drop=0`、`chan tcp_srv=.. port=..`（＝上位机该连的端口）。
4. **点一次「连接设备」** → 期望 `[tcp_srv] accept <ip>:<port>`。
5. **发「全屏点亮」**（`7B 01 01 01 7D`）→ 期望 `[tcp_srv] recv …` + `[yn_ol] rx … cmd=0x01` + `exec … ret=0`。
6. **再点一次「连接设备」**（复现现场症状①）→ 观察第 5 步的解析行是「现在才出现（＝那条帧一直没被处理）」
   还是「第 6 步后才有 `recv`（＝第 5 步的数据根本没用这条连接发）」。
7. **发「修改 IP」0x47**：
   `7B 47 0E C0 A8 72 C8 FF FF FF 00 C0 A8 72 01 25 38 7D`
   （＝192.168.114.200 / 255.255.255.0 / 192.168.114.1 / 9528，端口高字节在前）→
   期望 `[yn_ol] rx … cmd=0x47` + 两条 `[yn_ol] 0x47 …` + **随后重启（预期）**。
8. **回传整段 RTT 文本**（从横幅到 0x47 之后），**四类行都要**：`[tcp_srv]`、`[tcp_cli]`、
   `[disp]`（尤其 `probe` / `notify` / `WAIT budget`）、`[yn_ol]`。**不要只截一行。**

### 4.2 要回传的内容（判读表，简版）

| 回传内容 | 说明 |
|---|---|
| 完整 RTT 文本 | 见步骤 8；这是本轮判读的唯一依据 |
| `[disp] notify_drop` 是否非 0 | >0 = 通知确实被丢过（本轮已修 + 计数）；配合 `notify queue FULL` 行一起看 |
| 若有 `[disp] probe …`：`sta` + `head8` | `WAIT` = 长度字段算出的整帧没到齐；`FAKE` = 命令字/长度/尾字节不匹配（`head8` 就是原始帧头） |
| 0x47 实际发出的一帧（工具侧的 hex） | 核对 `declared ≥ 14` 与端口字节序（写 9528 = `25 38`） |
| 板上横幅 `fw=` / `built=` / `tree=` | 证明板上是这份镜像（md5 会随编译时间变，不能作身份） |

**待现场判读**：本轮所有「症状①/② 的真实根因」结论都必须由上述 RTT 文本给出——
本文只提供判据与已修的缺陷（通知投递），**未上机验证**任何现场行为。
