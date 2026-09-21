# YN_OL TCP「连上无响应 / 需重启板子」—— 根因取证 · 修复 · 重烧 · 实测复验

- 日期：2026-09-18（10:22–11:20，本机时间 UTC+8）
- 板子：STM32F407ZGTx，`192.168.114.200:9528`（TCP 业务口），显示模组 `2200001665`（16×80 = 1 行 × 5 列模块）
- 板上镜像（最终）：`tree=7df8ac20`、elf md5 `71e94c8fafd1bf8a378d3b35f89679d8`、`built=Sep 18 2026 11:01:28`、
  `proto=ALL disp=22_1665 config=Debug toolchain=gcc`
- 性质：**代码修改 + 重编 + 三固件重烧 + 实机验证**；未提交 git、未改 git 配置、未动其它协议模块与引导链
- 原始证据：`.analysis/yn_ol/fix_verify/`（`rtt_log.py` / `mcu_watch.py` / `yn_test.py` + 全部 `*.log` / `*.elf`）

---

## 0. 结论摘要

| # | 现场现象 | 根因（实证） | 修复 | 验证 |
|---|---|---|---|---|
| ① | **TCP 连上后发数据零反应** | 发作镜像里**没有编入任何 `{` 帧族协议**（EIDE Debug `excludeList` 排除了 `ProtocolParser_YunNan_Overload`）：TCP 通道照常 accept/recv，但 `ch_proto_map[CH_ID_TCP_SERVER]=0` ⇒ 帧被静默吞掉（一个协议都没被探测，连 `[disp] probe` 也不打印） | 新增「通道无协议承载」告警 `[disp] ch=N has NO protocol bound -> rx data swallowed (build/excludeList check!)`（按通道限速 1s，宏 `DISPATCH_NOPROTO_DBG_MIN_MS`）；EIDE 侧由用户把 `GuiZhou_Overload` 换出、`YunNan_Overload` 放回（**改完须 EIDE: Reload Project**） | 板上 elf 的 `[yn_ol]`/`g_brace_proto_guard` 字符串计数 = 0；现场 RTT：有 `[tcp_srv] recv … 7b 31 00 7d`、**无 `[yn_ol] rx`**；修复后同一查询 `[yn_ol] rx → exec cmd=0x31 ret=0` 正常 |
| ② | **必须重启板子才恢复** | TCP Server「accept → 阻塞 `netconn_recv` 单客户端 → 断开 → 回 accept」串行循环的 accepted conn **无 keepalive / recv 无超时 / 不注册链路监听** ⇒ 对端「不发 FIN 就消失」时循环被**永久占死**：后续连接由内核完成握手（上位机显示「已连接」）但永不 accept/recv | `app_tcp_server.c` 新增 `tcp_server_keepaliveinit()`（`SOF_KEEPALIVE` + `keep_idle 10000`/`keep_intvl 2000`/`keep_cnt 3`，**参数与 `app_tcp_client.c` 既有口径一致**），accept 后调用 | 半开实验（DROP 掉 A 的 FIN）：**修复前 30s 零自愈**（上一轮）→ **修复后 17.0s 自愈**；RTT 实证 `[tcp_srv] close … reason=-13`（ERR_ABRT）后立即 `accept` 新客户端 |
| ③ | **「板子自己死机 ~30s 后重启」** | **外部 EIDE 重编 + 重烧造成的假象**（非固件缺陷） | 不改代码；新增取证行以便现场区分（横幅 `reset … bkp1(iwdg_cnt)`） | 抓到全过程：`build/Debug/Project_STD.elf` 被外部改写于 **10:41:18** → 板子 **10:41:24** 失联并重启 → 之后运行的是**另一份镜像**（符号地址全变）；期间 RTT 无 `[err]`、`g_fault_pc=0`（无 HardFault）、`tick` 连续每秒 1000、heap 稳定 20656、`RTC_BKP_DR1`（Bootloader 仅在「上次复位 = IWDG」时 +1）= **0** ⇒ 不是看门狗复位 |

**一句话**：现场「连上没反应」= **镜像漏编协议**（静默死路）；「要重启板子」= **半开连接占死串行服务循环**（已由 keepalive 根治，16s 自愈）；「板子自己重启」= **测试期间有人重编重烧**（已提供一行判据）。

---

## 1. 阶段 0：RTT 与固件身份

### 1.1 RTT 打通（解掉上一轮的 `RTT Control Block not found`）

`JLinkRTTLogger`（DLL V9.50，S/N 69403514）对本目标**始终**报 `RTT Control Block not found`，即使：
`-Device STM32F407ZG -If SWD -Speed 4000`、`-RTTSearchRanges "0x20000000 0x20000"`、`-RTTAddress <精确地址>`、
halt 后再试、CPU 运行中试，全部失败（`.analysis/yn_ol/fix_verify/*.txt`）。而用 J-Link Commander 直读 RAM
**确认控制块存在**（`_SEGGER_RTT` = `nm` 给出的地址，`acID = "SEGGER RTT"`，up-buffer 指针/尺寸/偏移齐全）。

⇒ **改用自研读取器** `rtt_log.py`（pylink-square，`JLINKARM` 直读内存）：按 `_SEGGER_RTT` 地址（或扫描 SRAM
签名 `SEGGER RTT`）定位控制块，自行解码 up-buffer 的 `WrOff/RdOff` 环形指针，读走后回写 `RdOff`。
附加能力：`--mcu` 同时**非侵入**（不 halt、不 reset）轮询 `g_fault_pc/cfsr/bfar`、`xLastFailedAllocSize`、
`xFreeBytesRemaining/Min`、`xTickCount`、`pxCurrentTCB→pcTaskName`（`mcu_watch.py` 为独立版）。

### 1.2 面板身份（RTT 横幅）

开机后首屏（`boot1_rtt.log` / `final2_boot.log`）：

```
[fwver] write ok ver=9K222B23E0
[diag] fw=9K222B23E0 built=Sep 18 2026 10:33:44 tree=3146c4f0
[diag] build proto=ALL disp=22_1665 config=Debug toolchain=gcc
[diag] display screen=16x80 code=2200001665 scan_lines=1 chans=1
[diag] netcfg VALID ip=192.168.114.200 mask=255.255.255.0 gw=192.168.114.1 port=9528 udp_port=20103
[diag] ports t0 udp10011=10011 tcp_biz=9528 cq_udp=20103 gzol_cached=9528
```

**注意两条与任务书假设不同的事实**（当场核实并纠正）：
1. **发作镜像（当时板上）** = EIDE Debug 构建、`tree=no-fingerprint`、`built=Sep 18 2026 10:17:07`，
   且 **IP 是 `192.168.1.142` / TCP 口 `10011`**（Sector1 被别人写过）——用 `192.168.114.200:9528` 探它是完全不存在的地址；
2. 同一镜像 **`arm-none-eabi-strings` 里 `yn_ol` 计数 = 0、`g_brace_proto_guard` = 0** ⇒ **它是漏编 YN_OL 的镜像**（根因 ①）。

---

## 2. 阶段 1：复现与取证

### 2.1 现场症状（①）当场复现（10:29，板上 = 漏编 YN_OL 的 EIDE 镜像）

```
10:29:54.302 [P] connect 192.168.1.142:10011 OK local=38886 dt=0.4ms
10:29:57.305 [P] 应答 0 帧: []                      ← 连上、零应答
10:30:12.152 [P] connect 192.168.1.142:10011 OK local=57856 dt=0.3ms
```
同刻 ping 正常（`0.162/0.187/0.213 ms`，0% 丢包）。RTT（同刻抓取，`rtt_live1.log`）：
```
TCP connection request 57856 -> 10011.
TCP connection established 57856 -> 10011.
[tcp_srv] accept 192.168.1.17:57856
tcp_recved: received 4 bytes, wnd 2144 (0).
[tcp_srv] recv ch=tcp_server len=4 head=7b 31 00 7d      ← 字节到了应用层
（此后 3s 内无任何 [yn_ol] rx / [disp] probe 行）
[tcp_srv] close  192.168.1.17:57856 reason=-15
```
⇒ **字节到通道层、`{` 帧没人认领、全链路零日志**：与「漏编 `{` 帧族协议 ⇒ `ch_proto_map=0` ⇒ 静默跳过整轮探测」完全一致。

### 2.2 崩溃窗复现（③）—— 抓到的是一次外部重烧

`hold`（单连接 + 每 10s 一次 `'1'`）+ RTT 抓取 + MCU 轮询（`repro1_rtt.log.mcu`）：
```
10:41:11.289 [mcu] tick=425164 task=scan_task heap free=20856 min=18688 ok
10:41:24.659 [mcu] tick=0      task=<空>      heap free=0 min=0   ← 全 RAM 归零 = 刚复位
10:41:27.105 [H] recv ERROR errno=104 (Connection reset by peer)   ← 我方连接被毁
10:41:27.107 [H2] connect OK dt=0.2ms                              ← 复位后 2.4s 该端口已可用
```
**决定性排除**：reset 前 13.4s 内 `tick` 仍精确每秒 +1000、`task=scan_task/IDLE` 正常；
且 IWDG 超时 = `256×4096/f_LSI` ≈ **22~62s**（LSI 17~47kHz）⇒ **看门狗不可能在 13.4s 内复位**。

真因（外部证据）：
```
10:41:13.565  .eide/eide.yml 被改写（YN_OL 换回、GZ_OL 换出）
10:41:18.613  build/Debug/Project_STD.elf 被改写（elf md5 由 301080a0… → 6f677d3a…）
10:41:24     板子失联并重启，之后运行新镜像（VTOR=0x08040000、Reset_Handler 0x08045D89 = 新 elf）
```
⇒ 「失联 ~数秒~30s 后重启 + 启动签名 ARP」= **EIDE 重编 + 重烧**（烧录期间 CPU 被 halt/reset、网络静默），
与本项目上一轮报告 §6 记录的 4 次「整体死机 ~30s 后看门狗复位」形态**完全一致**（本轮为唯一一次有 elf mtime/md5 铁证）。

### 2.3 诊断补强（本阶段同时落地，长期保留）

| 问题 | 处置 |
|---|---|
| LwIP 调试噪声把 1KB RTT 缓冲灌满 ⇒ **此后所有诊断静默丢失**（上一轮「死机前 RTT 无输出」真因） | `lwipopts.h` USER CODE 段把 `LWIP_DBG_MIN_LEVEL` 覆盖为 `APP_LWIP_DBG_LEVEL`（默认 `0x01` = WARNING）；`Makefile` 透传该 `-D`（进口径指纹）。实测：噪声行 **5~17 行/秒 → 0**（12s 窗口 0 行），`[diag]`/`[tcp_*]`/`[yn_ol]`/`[disp]` 全部保留 |
| 「板子自己重启」与「有人重烧」无法区分 | 横幅新增 `[diag] reset csr=0x… [iwdg/sft/por/pin/bor/lpw/wwdg] bkp0(force)=… bkp1(iwdg_cnt)=…`；数据源 `pl_sys_reset_cause()`（Platform 层，纯读不清标志）；**`bkp1 ≥ 1` = 上一次复位是 IWDG**（Bootloader 仅在 `RCC_FLAG_IWDGRST` 时 +1，主固件跑满 30s 后清 0）⇒ 一行判「真死机 vs 外部复位」。（`csr` 位因 Bootloader 跳转前 `HAL_RCC_DeInit()` 清标志而通常为 0，保留供将来 Bootloader 留档时使用） |
| 「通道无协议承载」零日志 | `[disp] ch=N has NO protocol bound …` 告警 |

---

## 3. 阶段 2：修复清单（file:line）

| 文件:行 | 改动 | 归类 |
|---|---|---|
| `Application/Src/Channel/app_tcp_server.c:95`（函数）/ `:223`（accept 后调用） | 新增 `tcp_server_keepaliveinit()`（`SOF_KEEPALIVE` + `keep_idle 10000` / `keep_intvl 2000` / `keep_cnt 3`，参数对齐 `app_tcp_client.c:246-253`） | **机制一（根因 ②）** |
| `Application/Src/Channel/app_tcp_server.c:127/181-189` | `netconn_new` 失败补 `[tcp_srv] netconn_new NULL (netconn pool exhausted; retry 500ms)`（同一失败段去重、成功复位） | 诊断 |
| `Application/Src/app_dispatch.c:63-64`（宏）/ `:386-403`（逻辑） | 新增 `DISPATCH_NOPROTO_DBG_MIN_MS`（默认 1000）与 `[disp] ch=N has NO protocol bound -> rx data swallowed (build/excludeList check!)`（按通道限速；`APP_DIAG_BANNER=0` 整段消除） | **根因 ①的可见性** |
| `Application/Src/app_boot.c:171-190` | 横幅新增 `reset csr=… [位解码] bkp0(force)=… bkp1(iwdg_cnt)=…` | **取证（根因 ③判定）** |
| `Platform/Inc/pl_sys.h:22-44`、`Platform/Src/pl_sys.c:47-70` | 新增 `pl_reset_cause_t` + `pl_sys_reset_cause()`（解码 `RCC->CSR`，**纯读不清标志**；`__HAL_RCC_GET_FLAG` 取位） | 取证（Platform 分层，不破坏 Application 禁碰 HAL 纪律） |
| `Platform/Inc/lwipopts.h:36-56` | `APP_LWIP_DBG_LEVEL`（默认 `0x01`）覆盖 `LWIP_DBG_MIN_LEVEL` | **取证前提** |
| `Makefile:66-75` | `APP_LWIP_DBG_LEVEL` 透传进 `DEFINES`（⇒ 进口径指纹 `.build_stamp`） | 构建开关 |

**未改**：CQ / 其它协议模块 / `dev_display_*` / LwIP 源码 / `Core/Inc/main.h`（CubeMX 生成区保持原样）/ Bootloader 与 Recovery 工程。

---

## 4. 阶段 3：构建（A/B 段尺寸）

```
make clean && make -j8 DISP=22_1665            # 默认 PROTO=ALL
```

| 口径 | `.text` | `.rodata` | `.data` | `.bss` | `.ccmram` | `._user_heap_stack` | SRAM 合计 | **SRAM 余** | 告警 |
|---|---|---|---|---|---|---|---|---|---|
| 修复前 `tree=3146c4f0`（elf `301080a0…`） | 175252 | 205104 | 1680 | 124776 | 44156 | 2560 | 129016 | **2056B** | 3 |
| **修复后 `tree=7df8ac20`（elf `71e94c8f…`）** | **174640** | **203152** | 1680 | 124816 | 44156 | 2560 | 129056 | **2016B** | 3 |
| Δ | **−612** | **−1952** | 0 | **+40** | 0 | 0 | +40 | −40 | 0 |

**归因（两组对照编译）**：
- LwIP 档位 `LEVEL_ALL → WARNING` 单独贡献：**`.text −1044` / `.rodata −2216`**（`make APP_LWIP_DBG_LEVEL=0x00` 对照）；
- 新增诊断与机制（复位原因行 + `[disp]` 告警 + keepalive + `netconn_new` 告警）净增：
  **`.text +432` / `.rodata +264` / `.bss +40`**（对修复前基线、LwIP 档位取 0x00）。

**其它口径不破坏**（三跑全链接、零新增告警，`build_other_kinds.txt`）：

| 口径 | `.text` | `.rodata` | `.bss` | `.ccmram` | SRAM 余 |
|---|---|---|---|---|---|
| `PROTO=ALL DISP=1_263` | 174400 | 202888 | 124812 | 38332 | 2020B |
| `PROTO=CQ DISP=1_263` | 161220 | 202240 | 121336 | 36076 | 6304B |
| `PROTO=ALL DISP=22_1665`（发货口径） | 174640 | 203152 | 124816 | 44156 | 2016B |

告警数全部为 **3**（既有 HAL `stm32f4xx_hal_flash_ex.c` `-Wunused-parameter`）。22_1665 显存不变
（1×5 = 8320B = `1408×5 + 256×5`）。

---

## 5. 阶段 4：烧录记录

```
bash tool/flash_all.sh -s 4000 --verify        # Bootloader + Recovery + Project-STD 一次会话；默认擦 Sector1
  → Flash SUCCESS，Elapsed 6s（Bootloader @0x08000000 / Recovery @0x08008000 / Project-STD @0x08040000）
```
**为什么擦 Sector1**：Bootloader 条件 D 会比对 Sector1 `app_info.size/crc32` 与 0x08040000 实机固件 CRC，
烧完不擦会把设备永久挡在 Recovery（项目纪律）。擦除后主固件用本构建默认 netcfg 落盘：
`192.168.114.200 / 255.255.255.0 / 192.168.114.1 / port 9528 / udp_port 20103` —— **与任务书口径一致**（实证见 §1.2）。

烧后身份确认：`[diag] fw=9K222B23E0 built=Sep 18 2026 11:01:28 tree=7df8ac20`、
`[diag] reset csr=0x00000003 [iwdg=0 …] bkp0(force)=0 bkp1(iwdg_cnt)=0`、
`netcfg VALID ip=192.168.114.200 … port=9528`、TCP 9528 可连（见 §6）。

---

## 6. 阶段 5：实测复验矩阵（板上 = `tree=7df8ac20`）

| # | 项 | 方法（脚本） | 结果 |
|---|---|---|---|
| 1 | **基线回归** | `yn_test.py baseline`（单连接 `'1'` ×3 + 重连 ×3） | **6/6 正常**（`7B 31 01 00 7D`，0.4~0.6ms） |
| 2 | **崩溃复现窗** | `yn_test.py hold`（单连接、每 10s 一次 `'1'`） | **312s 轮 24/24 应答**、**200s 轮 16/16 应答**，最大查询间隔 13.0s，**无死机、无复位**（RTT 全程无新横幅、`bkp1=0`） |
| 3 | **半开自愈（机制一）** | `yn_test.py halfopen`（iptables DROP 掉 A 的 FIN） | **B 在 17.0s 获服务**（修复前 30s 零自愈）；RTT 实证 `[tcp_srv] close 0.0.0.0:0 reason=-13`（ERR_ABRT，keepalive abort）→ 立即 `accept` B → `[yn_ol] rx/exec`；iptables 残留 **0** |
| 4 | 长时稳定性 | RTT + `--mcu` 轮询 340s | `tick` 连续（+1000/s）、`heap free=20856 / min=18688` 稳定、**0 次 tick STALL / 0 次 HardFault / 0 次 malloc 失败** |
| 5 | 无协议承载告警（机制二） | 静态复核（`ch_proto_map=0` 分支 + 门控）+ 代码路径 | 告警位于该分支；`APP_DIAG_BANNER=0` 时整段消除（零输出零开销） |
| 6 | 收尾可用性 | `yn_test.py baseline`（复跑） | **6/6 正常**，板子停在「已修复 + 验证通过」状态 |
| 7 | 环境干扰核对 | elf md5 + 进程扫描 | 全程 elf md5 不变（无外部重编/重烧） |

**RTT 关键轨迹（半开实验，`halfopen_rtt.log`）**：
```
[tcp_srv] accept 192.168.1.17:41200
[tcp_srv] recv ch=tcp_server len=4 head=7b 31 00 7d
[yn_ol] rx ch=TCP_SRV(2) len=4 hex=7b 31 00 7d -> sta=OK cmd=0x31
[yn_ol] exec cmd=0x31 ret=0
[tcp_srv] close  0.0.0.0:0 reason=-13          ← keepalive 探测失败 → abort（对端 FIN 已被 DROP）
[tcp_srv] accept 192.168.1.17:41201            ← 服务循环回 accept，B 获服务
```

**本轮全程查询/应答统计**：`baseline 6/6` + `hold 24/24` + `baseline 6/6` + `hold 16/16` + `halfopen` + `baseline 6/6` + `probe ×2`
= **≥ 60 次交互零失败**。

---

## 7. 阶段 6：文档同步（本轮已更新）

| 文档 | 更新内容 |
|---|---|
| `doc/15_云南治超屏协议/README.md` | **新增 §12**「现场问题端到端修复」（三类根因表 / 修复 file:line / 验证矩阵 / A/B 段尺寸 / **现场判读方法 §12.5** / 遗留风险 §12.6）；原「修订记录」顺延为 **§13** 并追加 2026-09-18 条目 |
| `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` | **新增 §8.4.1**（2026-09-18 三口径复采 + 归因）；文档头「最新采样」指针改指 §8.4.1 |
| `doc/CLAUDE.md` | ① 内存实测段新增 2026-09-18 现行数字与 Δ；② 网络子系统段新增「TCP Server 半开连接自愈」「LwIP 调试档位」「`[disp]` 无协议承载告警」三条；③ 云南治超屏协议段新增本轮回指块 |
| `doc/构建开关总表.md` | §5 修订记录新增 `APP_LWIP_DBG_LEVEL` 与 `DISPATCH_NOPROTO_DBG_MIN_MS` 两个开关的语义/动机/A-B 实测 |

---

## 8. 遗留风险与建议

1. **测试期间禁止 EIDE 构建/烧录**（最高优先）：外部重烧会让板子失联数十秒后重启，报文层与「固件死机」几乎不可区分
   —— 上一轮报告 §6 的 4 次「死机」极可能同源。判据：横幅 `bkp1(iwdg_cnt)`（=0 即非 IWDG）。
2. **RTT 必须被读走**：1KB `NO_BLOCK_SKIP`，开机横幅已占满 ⇒ 现场不接读取器就看不到任何后续诊断。
   取证用 `.analysis/yn_ol/fix_verify/rtt_log.py --drain --mcu`（`JLinkRTTLogger` 在本机不可用）。
3. **板内 LDI 配置把 TCP Client 远端指向 `192.168.0.127:9528`（不可达）** ⇒ 板子 1Hz 连接重试 + 每轮 ARP 网关
   （现场「1Hz ARP 心跳」的真身）。`MEMP_NUM_TCP_PCB` 仍取 LwIP 默认 **5**，频繁 connect/断开场景建议随通道数复核
   （既有风险，本轮未改）。
4. `pl_net.c` 链路回调**只在 link-up 通知**（`app_udp` 的「断链重建」实际从未被 down 事件触发）；
   本轮 keepalive 已覆盖半开场景，该缺陷建议单独一轮处理。
5. 工作区 22_1665 宏值为 **1×5（16×80）**，与现场 1×2 口径不同；本模组显存随 `COLS` 变化，改宏须重编重烧
   （步骤见 doc/01 §0.9）。
6. `build/Debug/Project_STD.elf` 在验证后又被「多口径编译检查」重建（内容等价、仅内嵌 `built=` 时间戳不同）
   —— **板上镜像的 elf 存档 = `.analysis/yn_ol/fix_verify/final_shipping.elf`**；板上 `tree=7df8ac20` 不受影响。

---

## 9. 交付物清单

| 文件 | 内容 |
|---|---|
| `.analysis/yn_ol/fix_and_verify.md` | 本报告 |
| `fix_verify/rtt_log.py` | 自研 RTT 读取器（`--drain` 倒积压 / `--mcu` 非侵入轮询 / `--reset`）；绕过 `JLinkRTTLogger` 故障 |
| `fix_verify/mcu_watch.py` | 独立 MCU 状态监视（故障寄存器 / heap / tick / 任务名） |
| `fix_verify/yn_test.py` | 参数化实测脚本（`baseline/hold/watch/liveness/probe/halfopen`；只发只读 `'1'`） |
| `fix_verify/*.log` | 原始证据：`boot1/2/3_rtt.log`、`repro1_rtt.log{,.mcu}`、`verify_hold_*`、`halfopen_rtt.log{,.mcu}`、`final3_rtt.log{,.mcu}`、`yn_test.log`、`build_*.log`、`build_other_kinds.txt` |
| `fix_verify/final_shipping.elf` / `base_no_ynol_fix.elf` / `fix_lwip_all.elf` | 发货镜像 / 修复前基线 / LwIP 全量档对照（A/B 复现用） |
| `fix_verify/prev_boot_banner.txt` | 发作镜像（漏编 YN_OL 的 EIDE 构建）RTT 残余，根因 ① 的直接物证 |
| `fix_verify/findrtt.jlink` / `rcc.jlink` / `state*.jlink` / `vtor.jlink` | J-Link 取证脚本（控制块搜索、RCC 复位原因、VTOR/堆栈读） |
