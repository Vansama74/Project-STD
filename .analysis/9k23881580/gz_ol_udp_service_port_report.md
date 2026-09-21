# 贵州治超屏协议（GZ_OL）专用 UDP 服务口 —— 实施与验证报告（2026-09-14）

> **【修订 2026-09-15 · 用户裁决（现行口径）】** `0x40` 端口字段线上编码改为**高字节在前（BE16）**（有意不参考
> 协议文档/源固件）：写 10028 发 `27 2C`，`2C 27` 解析为 11303；`0x50`/`0x70` 应答同序。本报告中
> 「`2C 27`=10028（小端）」为历史口径；现行见 `doc/14_贵州治超屏协议/README.md` §13.5.1 与
> `port_10028_diagnosis.md` §8.8。

**任务**：为 GZ_OL 实现「专用 UDP 服务口」，让 `0x40` 修改端口对该协议真正生效。
**用户裁决**：新增一条 GZ_OL 专用 UDP 通道实例，绑定端口从 Sector1 `net_cfg.port` 读取
（与 TCP 业务口同号、不同协议栈，互不冲突）；**不改 Sector1 记录布局**、**不改 `0x40` 写哪个
字段**（仍写 `net_cfg.port`）、**不复用 CQ 专有 `udp_port`**。
**上一轮诊断**：`.analysis/9k23881580/gz_ol_port_setip_diagnosis.md`（语义不匹配结论 + 选项表）。

**结论一句话**：已按裁决落地 `CH_ID_UDP_GZOL` 专用实例（端口 = `net_cfg.port`，出厂默认 9528），
GZ_OL 四通道并存（RS485 / RS232 / 10011 兼容口 / 专用口），四构建口径链接通过、零新增告警，
宿主推演 89 用例通过；`0x40` 改端口后**专用口随新端口服务**，10011 仍可应答。

---

## ① 改动清单（diff 级）

### 1.1 已跟踪文件（`git diff --stat`）

```
 Application/Inc/Channel/app_udp.h |  16 ++-
 Application/Inc/app_dispatch.h    |   6 +-
 Application/Src/Channel/app_udp.c | 236 +++++++++++++++++++++++++++++++++++++-
 Application/Src/app_boot.c        |   3 +
 Platform/Inc/lwipopts.h           |  10 +-
 5 files changed, 260 insertions(+), 11 deletions(-)
```

| 文件 | 位置 | 改动 |
|---|---|---|
| `Application/Inc/app_dispatch.h` | `channel_id_t` | 新增 `CH_ID_UDP_GZOL = 8`；`CH_ID_MAX 8→9`；顺带修正 `CH_ID_UDP_CQ` 注释笔误（写 `net_cfg.port` → 实际读 `net_cfg.udp_port`） |
| `Application/Src/Channel/app_udp.c` | 文件头 + 尾部新增段 | 文件头注释改为「三类实例」；新增 GZ_OL 实例：`udp_gzol_task`（常驻 conn/bind/断链重建 + 端口读取 + 冲突跳过 + 告警闩锁）、`udp_gzol_connect_task`（recv → `app_channel_dispatch`）、`udp_gzol_ch_send`、`udp_gzol_link_listener`、`_udp_gzol_read_port`、`s_udp_gzol_ch`、`s_udp_gzol_channel_tmpl`、`udp_gzol_task_attr`、`udp_gzol_connect_attr`、`app_udp_gzol_get_port()`；`#include "SEGGER_RTT.h"`（回退/冲突告警） |
| `Application/Inc/Channel/app_udp.h` | 尾部新增声明 | `app_udp_gzol_start()`（inline）、`udp_gzol_task`、`udp_gzol_task_attr`、`app_udp_gzol_get_port()`；顺带修正 CQ 段注释（`net_cfg.port` → `net_cfg.udp_port`） |
| `Application/Src/app_boot.c` | `init_task` | `app_udp_cq_start()` 之后新增 `app_udp_gzol_start()`（**在 `app_net_boot_apply()` 之后**，见 ⑤ 时序） |
| `Platform/Inc/lwipopts.h` | 池注释 | 池值**维持 8/8**；注释按新基线重写（常驻 5、峰值 8 = 池容量；UDP_PCB 3/8） |

### 1.2 未跟踪文件（GZ_OL 模块，本仓库为新增模块，`git diff` 不显示）

| 文件 | 改动 |
|---|---|
| `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c` | 新增 `s_gz_ol_mask_gzol`；`gz_ol_proto_init` 增第四次 `app_proto_register`（同挂 RJ45 槽 RB）+ `app_proto_bind_channel(_, CH_ID_UDP_GZOL)` + `app_proto_set_frame_queue`；文件头注释改四通道 |
| `Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h` | 头部说明改四通道；`0x40` 行补 port 语义（专用口监听号 + TCP 业务口，同号不同协议栈） |
| `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c` | **仅注释**（`_gz_ol_net_payload` 与 `_gz_ol_exec_set_ip` 的 port 语义说明）；**行为零改动**，`GZ_OL_RTT_DIAG` 保持 =1 未动 |

### 1.3 未改动（纪律确认）

- `Device/Display/dev_display_22_1703.c`：**未触碰**（用户实验态，`ccmram=11420` 的差值全部来自该文件）。
- `.eide/eide.yml`：**未触碰**。
- 其它协议模块行为：**未改**（仅 `app_dispatch.h` 枚举扩容，对既有 CH_ID 值与语义无影响）。
- 未提交 commit、未做任何 git 网络操作、**未烧录**。

---

## ② 新实例的端口来源、回退策略、与 10011 并存

### 2.1 端口来源

```
_udp_gzol_read_port()  →  app_board_net_cfg_get(&cfg)
                          cfg.port ∈ [1,65535]  → g_udp_gzol_port = cfg.port   （返回 true）
                          否则                  → g_udp_gzol_port = 9528 + RTT 告警（返回 false）
```

- **每轮重建前重读**（`udp_gzol_task` 的 for 循环首句），与 CQ 实例同策略 → **`0x40` 落盘 + 重启生效**。
- 9528 的来源：`net_cfg.port` 两口径出厂默认（`app_net_boot.c` 的 `d_port`），非新常量语义。
- **不读 `udp_port`**（CQ 专有字段），记录布局与字段归属不变。

### 2.2 回退策略（不静默绑定 0）

| 情形 | 行为 | 理由 |
|---|---|---|
| 记录无效 / `port == 0` | 回退 **9528** + RTT `[gz_ol] net_cfg.port invalid -> UDP service port fallback 9528` | `netconn_bind(..., 0)` = **随机端口**，上位机不可达 → 必须显式回退并告警；正常路径由 `app_net_boot_apply()` 先行修复无效记录（写默认落盘），此分支为防御 |
| `port == 10011`（发现口） | **跳过绑定** + RTT `... port 10011 busy (resident 10011/20103) -> skip bind` | 同口二次 `bind` 必 `ERR_USE`；此时 GZ_OL 仍由保留的 `CH_ID_UDP` 承载（功能不受损），跳过避免 2s 周期空转 |
| `port == 20103`（CQ 业务口） | 同上（跳过 + 告警） | dev 共存口径 CQ 固定 20103；避免 `ERR_USE` 重试空转 |
| `bind` 失败（其它 err） | RTT 告警一次后按 2s 重试 | 池满等瞬时原因可自愈；告警闩锁防刷屏 |

**告警闩锁**：`s_udp_gzol_warned_port` 记录已告警的端口值，同一异常值只打印一次；绑定成功后清 0
（恢复正常即重新武装）。

### 2.3 与 10011 并存（保留策略 + 应答回源核实）

- **10011 绑定保留**（第三个 mask，`CH_ID_UDP`）——既有上位机/工具仍可发 10011，行为与上一轮一致。
- **两通道回源不串台（已核实）**：收包路径 `udp_gzol_connect_task` / `udp_connect_task` 各自把
  `netbuf_fromaddr/fromport` 快照写入**本实例**的 `s_udp_gzol_ch` / `s_udp_ch`（文件级单实例）；
  `app_channel_dispatch(ch, ...)` → `frame_dispatch_task` 把**收包通道指针**写入
  `frame_msg_t.ch`（`app_dispatch.c` 的 `msg->ch = ch`）→ 协议 `channel_send(msg->ch, ...)` →
  `channel_send` 的 `app_channel_get(ch->ch_id) == ch` 回验（**ch_id 不同：8 vs 4**，两实例互不回验
  通过对方）→ 各自的 `_ch_send` 用各自快照 `netconn_sendto`。**结论：从 10011 收到的帧回 10011 源端，
  从专用口收到的帧回专用口源端；两条通道并存不会串台**（同一实例内多客户端并发仍是既有单例快照
  限制，见 doc/14 §11「已知限制」）。
- **广播可收**：专用实例建常驻 conn 时置 `SOF_BROADCAST`（与 10011/CQ 一致）——
  `IP_SOF_BROADCAST_RECV=1` 下，未置该选项的 UDP pcb 收不到广播目的帧；上位机若按源固件习惯
  广播搜索帧，本实例可收（应答仍单播回源）。
- **同槽 probe 无冲突**：专用 mask 与 10011 mask 共用 `gz_ol_probe_frame`、同挂 RJ45 槽 RB；
  `frame_dispatch_task` 内层按「通知通道的 mask ∩ 已注册协议 ∩ 同一 RB」筛选，收包通道只带自己的
  mask → 同一帧**只探测一次**、只入队一次（四 mask 共用同一静态队列，无重复投递）。
  首字节 `0x54` 与 IAP/LDI/CQ 双向快拒（沿用既有复核结论）。

---

## ③ 枚举/数组扩容影响面清单（grep 全量）

`CH_ID_MAX 8 → 9`（`Application/Inc/app_dispatch.h:58`）。全仓受影响位置（`grep -rn`，含头文件）：

| 位置 | 内容 | 影响 |
|---|---|---|
| `Application/Inc/app_dispatch.h:56` | `CH_ID_UDP_GZOL = 8` | 新枚举值（未插队，既有 0~7 值不变） |
| `Application/Inc/app_dispatch.h:58` | `CH_ID_MAX = 9` | 数组尺寸唯一来源 |
| `Application/Inc/app_dispatch.h:112` | `proto_mask_t ch_proto_map[CH_ID_MAX]` | `.bss` **+4B**（`g_dispatch` 内） |
| `Application/Inc/app_dispatch.h:113` | `channel_t *channels[CH_ID_MAX]` | `.bss` **+4B** |
| `Application/Src/app_dispatch.c:154` | `app_proto_bind_channel` → `ch_proto_map[ch_id] \|= mask` | 无守卫（保持原样）；`CH_ID_UDP_GZOL` 由 `app_gz_ol_proto.c` 传入，值为 8 < 9 安全 |
| `Application/Src/app_dispatch.c:499-506` | `app_channel_register/get` 的 `if (ch_id < CH_ID_MAX)` | **守卫自动覆盖新值 8** → 注册/查询 `CH_ID_UDP_GZOL` 正常 |
| `Application/Src/app_dispatch.c:252/448` | `ch_proto_map[ch->ch_id]` 读 | 新枚举值查表命中原由 bind 写入的 mask |
| `-` | 全仓**无** `_Static_assert` 绑定 `CH_ID_MAX` / 通道数 | 无隐藏断言需同步（已 grep 确认） |
| `Application/Src/Channel/app_udp.c` | `app_channel_register(CH_ID_UDP_GZOL, ...)` / `nullptr` | init/deinit 配对，符合「deinit 置 nullptr」纪律 |
| `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c` | `app_proto_bind_channel(s_gz_ol_mask_gzol, CH_ID_UDP_GZOL)` | 唯一绑定者（协议侧） |
| 其它 | `MAX_CHANNELS(32)` 是 `ch_queue` 深度，与 `CH_ID_MAX` 无关；`PROTO_MAX_COUNT(32)` 为协议 mask 位宽，本轮 GZ_OL 用第 4 个 mask（全仓注册 mask 总数 26→27，仍 < 32） | 无影响 |

**结论**：无越界、无遗漏；新增枚举值位于末尾（不改变既有 ID 数值），派生数组同步扩容，
越界守卫自动覆盖。

---

## ④ LwIP 池与内存增量核算

### 4.1 LwIP netconn / UDP PCB 池（doc/06 纪律）

| 池 | 配置 | 常驻占用 | 峰值（含瞬态） | 结论 |
|---|---|---|---|---|
| `MEMP_NUM_NETCONN` | **8**（不变） | **5**：UDP 10011 + UDP 20103(CQ) + **UDP `net_cfg.port`(GZ_OL，新)** + TCP Server listener + TCP Client | **8** = 5 常驻 + 1（TCP Server 已连接客户端）+ 2（LDI/CQ 广播回退临时 conn，常驻就绪时不占用） | **维持 8，不扩池**：峰值恰好等于池容量，无溢出路径；广播回退仅在常驻 conn 未就绪（断链重建窗口）时短暂占用 |
| `MEMP_NUM_UDP_PCB` | **8**（不变） | **3**：10011 + 20103 + GZ_OL | 4（+1 广播回退临时 conn 的 pcb） | 余量 4，**维持 8** |

- 池对象在 LwIP 的 **static memp 区**（`.bss`），净增 1 常驻 netconn + 1 udp_pcb **取自既有池**
  → **静态内存零增量**（不扩池即无 bss 增长）。
- 风险提示（已写入 `lwipopts.h` 注释）：常驻数已到 5，若后续再新增 UDP 端口协议，必须先扩池
  或复用常驻 conn（否则广播回退静默丢包的历史缺陷会复发）。

### 4.2 静态内存增量（`arm-none-eabi-size -A` + `nm -S`，Makefile GCC Debug）

| 项 | 增量 | 实测构成 |
|---|---|---|
| `.text` | **+752** | `udp_gzol_task` 292B + `udp_gzol_connect_task` 184B + `udp_gzol_ch_send` 66B + `udp_gzol_link_listener` 20B + 端口读取/接线/常量 ≈190B |
| `.rodata` | **+296** | 任务属性（2×36B）+ 通道模板 8B + ops 表 4B + RTT 告警格式串 + 注释性字符串常量 |
| `.data` | **+8**（ALL）/ **+12**（CQ） | `g_udp_gzol_port` 2B + 通道模板对齐 |
| `.bss` | **+40** | `s_udp_gzol_ch` 20B + `s_udp_gzol_warned_port` 2B + `udp_gzol_disconnect_sem` 4B + `s_gz_ol_mask_gzol` 4B + `ch_proto_map[]`/`channels[]` 各 +4B + 对齐 |
| `.ccmram` | **+0** | 无新增 CCM 对象（RJ45 RB 仍只 acquire 不 provide） |
| `._user_heap_stack` | 0（2560/2564） | 链接期下限不变 |

**SRAM 余量结论**：`PROTO=ALL` `DISP=1_263` 余量 **496B → 448B**（128KB 内，余量仍为正，
四口径均链接通过）；`PROTO=CQ` `DISP=1_263` 4776 → **4720B**。
**没有出现「静态增量放不下」的情形** → 无需启动替代方案（纪律 6 未触发）。

### 4.3 任务栈与 ucHeap 水位（**本轮最大遗留风险，须现场标定**）

- 新增 **2 个常驻任务**：`udp_gzol_task`、`udp_gzol_connect_task`，各 `256×4 = 1KB`
  → ucHeap 常驻 **+2KB**（含 TCB 与 heap_4 块头 ≈ **+2.27KB**）。
  （断链时 connect 任务退出释放，重建时再分配；稳态即两任务共存。）
- `configTOTAL_HEAP_SIZE = 36KB`（`ucHeap` 实测 0x9000 = 36864B）。
- **堆水位从未标定**（`doc/05_协议模块多协议兼容优化/03_freertos_heap_side_effect.md` §4 待办：
  `xPortGetMinimumEverFreeHeapSize` 标定）。本轮**无硬件**（不烧录纪律），只能给静态审计：
  - dev 全协议口径（`PROTO=ALL`）任务清单：31～32 个 1KB 任务（`nm`/源码枚举：网络 13、
    协议与框架 18、Timer Svc 1）+ 3 个 512B（idle / half_sec / light_sensor）+ `init_task` 2KB（启动期瞬态）；
    按「栈 + TCB(100B→112B 块) + heap_4 块头(8B/次)」保守模型累计落在 36KB 包络附近甚至之上。
  - **该模型与实机可运行的事实并不自洽**（说明模型对"实际创建任务数/开销"存在高估），
    因此**绝对水位无法静态定论**；本报告只保证**相对增量（+2 任务 ≈ +2.27KB）是确定的**。
  - **现场必做**：临时在 `init_task` 末尾（`app_default_display()` 之前）加
    ```c
    SEGGER_RTT_printf(0, "[heap] free=%u minEver=%u\n",
                      (unsigned)xPortGetFreeHeapSize(),
                      (unsigned)xPortGetMinimumEverFreeHeapSize());
    ```
    跑完开机流程后读一次；若 `minEver` 余量 < 2KB（相对新增量的安全系数），按下面顺序降本：
    1. **合并任务**（首选，省 ~1.14KB）：把 `udp_gzol_connect_task` 的 recv 循环并入
       `udp_gzol_task`（单任务 bind + recv，断链时自行重建）——偏离 CQ 镜像结构但等价；
    2. **栈降档**（省 0.5KB/任务）：两任务 `256*4 → 192*4`（768B）；`128*4` 为下限不建议
       （netconn API 调用链较深）；
    3. 关掉不用的常驻通道（`app_udp_cq_start()` 在非 CQ 口径可条件化，省 2KB）——
       属跨模块决策，须用户裁决。
  - 量产（EIDE）口径任务数明显更少（排除 7 个地区协议 ≈ −8KB 栈），余量显著更宽；**本风险
    主要在 `PROTO=ALL` dev 口径**。

---

## ⑤ 四条口径构建结果与段尺寸表

命令：`make clean && make -j8 [PROTO=CQ] [DISP=22_1703]`（GCC Debug；口径指纹 stamp 保证
切口径全量重编 + 重链接）。**四口径 exit=0，告警仅 3 条既有 HAL `flash_ex` unused-parameter
（零新增告警）**。

| 口径 | text | rodata | data | ccmram | bss | heap_stack | 链接 | elf md5 |
|---|---|---|---|---|---|---|---|---|
| **默认** `PROTO=ALL` `DISP=1_263` | **164756** | 198384 | 1672 | 37084 | 126392 | 2560 | ✅ | `5e3b1d73393307419d45bfa19215ca2d` |
| `PROTO=CQ` | 151564 | 197728 | 872 | 37084 | 122916 | 2564 | ✅ | `d19be959da5b299d3d22191a0d1d8961` |
| `DISP=22_1703` | 164996 | 198464 | 1752 | **11420** ⚠ | 126392 | 2560 | ✅ | `8633f1052c43af5213a43714f1de07d7` |
| `DISP=22_1703` `PROTO=CQ` | 151804 | 197816 | 952 | **11420** ⚠ | 122916 | 2564 | ✅ | `f92dd3bb8964275f65a06dbacab9f986` |

- SRAM 合计/余量：默认 **130624（99.7%，余 448B）**；CQ 126352（余 4720B）；
  22_1703 130704（余 368B）/ 126432（余 4640B）。
- ⚠ **22_1703 两行 `ccmram=11420` 与历史基线（39164）不可比**：差值
  `31648 − 3904 = 27744` 全部来自用户实验态的 `Device/Display/dev_display_22_1703.c`
  （显存降到 512B×2），**与本轮改动无关**（本轮 ccmram 增量 = 0）。
- 段尺寸含默认开启的 `GZ_OL_RTT_DIAG`（置 0 后 text ≈ −288 / rodata ≈ −328）。
- 运行日志与逐口径采样：`.analysis/9k23881580/build_sweep.log`、`build_{default,cq,d22,d22cq,default2}.log`
  （`build_sweep.sh` 为本轮采集脚本）。
- **工作区终态（2026-09-14 17:08）**：`make clean && make -j8`（默认口径）→
  elf md5 **`5e3b1d73393307419d45bfa19215ca2d`**、hex md5 `fb5f85fa3335046bc6582b0404d993a0`；
  重复 `make -j8` **0 编译 0 链接、md5 不变**。
- **构建卫生事件（记录）**：本轮采集期间用户侧 EIDE 构建（17:06）以新 mtime 覆写了
  `build/Debug/` 共享产物（EIDE 日志显示其构建成功，编译列表含本轮改动的 `app_udp.c` 等），
  随后一次 `make -j8` 用 EIDE 对象**重链接出混合态 elf**（text 242456 / ccmram 5040）；
  已按 `doc/构建开关总表.md` §4.2 纪律 **`make clean && make`** 复原，上表与终态 md5 均为
  **make 干净构建**结果（`default` 与 `default2` 两次独立全量构建 md5 一致 → 可复现）。

---

## ⑥ 宿主推演（89 用例通过）

脚本：`~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/gz_ol_frame_sim.py`（**67 → 89 用例**，
`通过 89 / 失败 0`）。

新增 **§8「`0x40` → `0x70` 端口生效契约（专用 UDP 业务口 `CH_ID_UDP_GZOL`）」**，镜像固件：
`DeviceStore`（net_cfg 最小模型）+ `apply_setip`（0x40 落库）+ `net_boot_apply`（记录非法→写默认）
+ `gz_ol_service_port`（`_udp_gzol_read_port` + 冲突跳过）+ `build_search_ack`/`parse_search_ack`
（0x70 读记录回显）。关键断言输出（节选）：

```
== 8. 0x40 → 0x70 端口生效契约（专用 UDP 业务口 CH_ID_UDP_GZOL）==
  [通过] 0x40 端口 10028：0x70 回显新值: 10028
  [通过] 0x40 端口 10028：GZ_OL 服务口 = 新值（bound）: (10028, 'bound')
  [通过] 0x40 端口 9528（STD 出厂默认口）：0x70 回显新值: 9528
  [通过] 0x40 端口 1（下界）：GZ_OL 服务口 = 新值（bound）: (1, 'bound')
  [通过] 0x40 端口 65535（上界）：GZ_OL 服务口 = 新值（bound）: (65535, 'bound')
  [通过] 0x40 端口 8080（任意业务口）：GZ_OL 服务口 = 新值（bound）: (8080, 'bound')
  [通过] 0x40 端口 = 10011：专用实例跳过绑定（skip-busy，GZ_OL 仍由 CH_ID_UDP 承载）: (10011, 'skip-busy')
  [通过] 0x40 端口 = 20103（CQ 业务口）：专用实例跳过绑定: (20103, 'skip-busy')
  [通过] 0x40 端口 = 0：记录非法 → 专用实例回退 9528（fallback，不绑 0）: (9528, 'fallback')
  [通过] 0x40 端口 = 0：重启 app_net_boot_apply 写默认（port 9528 / IP 114.200）: 9528
  [通过] 0x40 不改 CQ 专有 udp_port（20103 保留）: 20103
结果：通过 89 / 失败 0
```

另同步修正 §7 的「落库契约」表述（`net_cfg.port` 的消费者从「仅 TCP 业务口」改为
「GZ_OL 专用 UDP 业务口 + TCP 业务口」），§1–§6 既有 67 用例全部保持通过（无回归）。

---

## ⑦ 现场复测步骤（发什么帧 / 发往哪个端口 / 看什么 / 如何判定）

**前提**：烧录**本轮 make 默认口径**产物（`build/Debug/Project_STD.hex`，md5 `fb5f85fa…`，
== 先确认 `make -j8` 为最后构建，EIDE 产物需 `make clean && make` 覆盖 ==）；
DIP1 = OFF（9600，串口联调用）；上位机/工具与设备同网段。

1. **基线（改端口前）**：向 **`<设备IP>:9528`** 发搜索帧
   `54 43 4C 59 00 00 00 00 11 00 00 00 60 00 00 00 00`（31B）→ 应回 `0x70` 帧，
   载荷 `[12..13]` = `38 25`（9528 小端）；再向 `:10011` 发同一帧 → 也应应答（兼容通道保留）。
2. **改端口**：向 `:9528`（或 `:10011`）发 0x40 帧，端口设 10028：
   ```
   54 43 4C 59 00 00 00 00 1F 00 00 00 40 00 00 00 C0 A8 01 64 FF FF FF 00 C0 A8 01 01 2C 27 00
   ```
   （IP 192.168.1.100、port 10028=0x272C 小端）→ 应回 `0x50`（**回显请求，非落盘证据**）→
   约 100ms 后设备自复位。
3. **端口生效判定（核心）**：重启后把目标改成 **192.168.1.100:10028**，发同一 `0x60` 搜索帧：
   - `0x70` 应答载荷 `[12..13]` 应为 `2C 27`（= 10028，读记录回显 → 落盘核验）；
   - 同时 **192.168.1.100:10011** 仍应应答（兼容口保留）。
4. **服务口判定**：向 **192.168.1.100:10028** 发 0x10 故障查询
   `54 43 4C 59 00 00 00 00 11 00 00 00 10 00 00 00 00` → 应回 2B `00 00` 的 `0x10` 帧
   （这条证明**专用口真的在 10028 上 serve**，而不只是记录落了盘）。
5. **RTT 观测**（可选，J-Link/CMSIS-DAP）：
   - 正常：无 `[gz_ol] ... fallback/busy/bind failed` 告警；
   - 若端口被改成 10011 或 20103：应看到一次 `[gz_ol] UDP service port ... busy ... -> skip bind`；
   - 若 Sector1 记录异常：应看到一次 `fallback 9528` 告警。
6. **判定矩阵**：

| 0x70 回显 | UDP 新端口 | UDP 10011 | 结论 |
|---|---|---|---|
| 新值 10028 | 有应答（0x10/0x60） | 有应答 | ✅ **裁决落地生效**（目标态） |
| 新值 10028 | 无应答 | 有应答 | 专用实例未绑定 → 抓 RTT 看 `fallback/busy/bind failed` 告警 |
| 旧值 9528 | 无应答 | 有应答 | 落盘失败 → 查 Sector1 写入路径（当前实现无此路径） |

**回归确认**：串口（RS485/RS232，DIP1=OFF）发同一 0x60 → 应答路径不变；`0x20/0x30/0x80` 行为不变。

---

## ⑧ 文档更新清单（本轮已同步）

| 文档 | 更新内容 |
|---|---|
| `doc/14_贵州治超屏协议/README.md` | §1 传输（四通道）+ `0x40` 行 + `0x70` 载荷说明；§3 模块设计（四通道/mask/实例启动）；§4 槽位表；**§6 内存与构建状态整表重采**（含增量账、任务/堆说明）；§8 Q10；§9 as-built（绑定/构建/推演 89 用例/联调状态）；§10 UDP 联调（两条通道）；§11 重写（两条 UDP 通道语义表 + 端口来源/回退/冲突 + 并存应答核实 + 后续项）；**§13 重写为「判定 → 用户裁决 → as-built → 复测矩阵 → 选项存档」**；§14 追加本轮修订记录（含 `GZ_OL_RTT_DIAG` 验收提醒） |
| `doc/CLAUDE.md` | 通道标识映射表 +第 8 行；模块全景表 +GZ_OL 实例行；GZ_OL 小节（绑定四通道 / `0x40` port 语义 / 回退与冲突）；启动流程 `app_udp_gzol_start()`；NetConfig `port` 字段语义（+专用 UDP 业务口）；内存布局与实测段尺寸（新数字 + 增量归因）；LwIP 池核算（常驻 5 / 峰值 8） |
| `doc/05_协议模块多协议兼容优化/01_architecture.md` | §2.1 逻辑通道表 +第 8 号通道与 CH_ID 枚举注；§4 兼容矩阵 +GZ_OL×专用口行、修正 GZ_OL 禁绑行；变更记录 +本轮条目 |
| `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` | 新增 **§7.2**（四口径重采 + 增量构成 + LwIP 池 + ucHeap + md5）；修订段追加本轮条目 |
| `doc/08_协议模块接入规则/03_网络协议接入_UDP与TCP.md` | §1 逻辑通道表（6 行 + 端口来源列）；§2.3 新增「**第二/第 N 条 UDP 实例范式**」（6 步 checklist：枚举/端口来源/回退与冲突/启动时序/只 acquire/资源核算/台账）；源快照「多实例不串台」澄清；§4 池核算与 `SOF_BROADCAST` 收广播前提 |
| `doc/08_协议模块接入规则/02_串口协议接入.md` | 串口协议追加网口绑定注（+专用口 + 指向 03 的新范式） |
| `Platform/Inc/lwipopts.h` | 池注释按新基线重写（代码值不变） |
| `doc/构建开关总表.md` | **无需改动**（本轮未新增编译期开关） |

> 说明：`doc/14`/`doc/06-04` 的 22_1703 两行 `ccmram` 仍标 `[待更新]`（用户实验态驱动，
> 不属本任务范围，已在两处注明差值来源）。

---

## ⑨ 遗留项

1. **堆水位未标定（最高优先）**：ucHeap +2 任务（≈+2.27KB）——现场按 §4.3 的 RTT 两行代码核对
   `xPortGetMinimumEverFreeHeapSize()`；不达标按 §4.3 的三档降本方案（合并任务 → 栈降档 →
   条件化 CQ 通道）执行。
2. **`GZ_OL_RTT_DIAG` 仍为 1**（`app_gz_ol_proto_cmd.c`，按要求**保持原样未动**）——
   验收后置 0（或删除宏与块），可回收 ≈ text 288 / rodata 328，SRAM 余量不变（余量由 RAM 段决定）。
3. **实机复测未做**（不烧录纪律）：§7 步骤待现场执行；专用口 + 10011 并存、`0x40` 改端口对称
   验证（新端口通 / 旧端口不通 / 10011 仍通）三项需现场闭环。
4. **搜索应答仍是单播回源**（源固件为广播）：若上位机只监听广播应答，需按 doc/14 §11 后续项
   增设 `app_udp_gzol_broadcast()`（镜像 CQ 封装，复用常驻 conn，不占池）——待联调确认。
5. **TCP 业务口同号迁移**：`0x40` 改端口会同时迁移 LDI/IAP 的 TCP 业务口（`net_cfg.port` 双消费者
   的既有语义）——已文档化，若产品要求彻底解耦需另立字段（会破坏 Sector1 72B 布局 → 三固件同步
   重烧，**不建议**）。
6. **池容量临界**：`MEMP_NUM_NETCONN=8` 常驻 5、峰值 8；后续任何新增常驻网络通道前必须先扩池
   或复用常驻 conn（注释已写进 `lwipopts.h`）。
7. **`0x50` 应答仍是「回显请求」**（沿用源固件行为），不能作为落盘证据——现场判定一律以
   `0x70` + 新端口实发为准（doc/14 §13.4 已写明）。

---

## 附：本轮关键命令与产物

```bash
# 四口径采集（脚本与日志）
bash .analysis/9k23881580/build_sweep.sh        # → build_sweep.log / build_*.log
# 终态（默认口径）干净构建
make clean && make -j8                          # md5 5e3b1d73393307419d45bfa19215ca2d
arm-none-eabi-size -A build/Debug/Project_STD.elf
# 宿主推演
python3 ~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/gz_ol_frame_sim.py
```

- 新增符号（`nm -S` 关键行）：`s_udp_gzol_ch` 0x14、`s_udp_gzol_warned_port` 0x2、
  `udp_gzol_disconnect_sem` 0x4、`s_gz_ol_mask_gzol` 0x4、`g_dispatch` 0x1dc（含扩容数组）、
  `udp_gzol_task` 0x124、`udp_gzol_connect_task` 0xb8、`udp_gzol_ch_send` 0x42。
- 上一轮报告（诊断，未改代码）：`.analysis/9k23881580/gz_ol_port_setip_diagnosis.md`。

---

## ⑩ 复测轮 ② 追加（2026-09-14 晚）：用户报「改端口 10028 仍无响应」→ 链路复核 + 四态判别

**现场（17:56）**：`0x40` 改端口为 **10028** 后向设备 IP 的 UDP **10028** 发数据无任何响应；
UDP **10011** 一切正常。**本报告 ⑦ 的复测步骤当时尚未被现场执行到「读 RTT 横幅」这一步**，
因此本轮把「板上实际状态」做成一次可读出的判定。

**复核结论（代码链无缺陷）**：`0x40` 解析（`app_gz_ol_proto_parse.c:154-166`，LE16 @ payload[12..13]）
→ 落库（`app_gz_ol_proto_cmd.c` `_gz_ol_exec_set_ip`：第 4 实参 = `net_cfg.port`、第 5 = 保留 `udp_port`）
→ 启动应用（`app_net_boot.c:38-69`）→ 专用实例端口读取与 bind（`app_udp.c:545-600`）
→ 四 mask 分发与单播回源（`app_gz_ol_proto.c:120-142`、`app_dispatch.c:447-490`）逐段核对**通过**；
UDP 与 TCP 端口号空间独立（LwIP `udp.c:110-111`），「与 TCP 业务口同号」不冲突；
IWDG 约 32s（`Core/Src/iwdg.c:41-42`）不打断 16KB 扇区擦写。
**唯一代码侧的「端口静默回滚」机制** = `app_net_boot.c:41-59` 的 `accept_write`
（记录 CRC 坏 / **IP 全 0** / port=0 → 用本构建默认**同时覆盖 IP 与 port**）。

**本轮改动（只读诊断，3 处；`make APP_DIAG=0` 时零差异）**：0x40 原始载荷 16B dump +
`port field payload[12..13]=2c 27 -> 10028 (LE16)`；横幅 `expect gzol=%u = net_cfg.port`；
启动 `netcfg INVALID at boot … -> accept_write defaults`。**诊断增量 A/B 更新**：
text **+2640** / rodata **+2976**（Flash **+5616B**）、RAM +0B（原 +2464/+2656）。
四口径重采（默认口径 text 167236 / rodata 201032 / ccmram 37084 / bss 126400，SRAM 余 440B；
CQ 154060 / 200384；22_1703 两口径 167492 / 201120 与 154316 / 200464，ccmram 39164），
全部链接通过、零新增告警。

**四态判别与现场清单**：`.analysis/9k23881580/port_10028_diagnosis.md`
（A 旧镜像 / B 启动回滚 / C 设计分支 / D 网络侧；含十六进制帧、RTT 判读行、TCP/UDP 对照矩阵）。
宿主推演同轮扩到 **99 用例通过**（新增 §9 四态判别契约 10 条；本文 ⑥ 的 89 条无回归）。
