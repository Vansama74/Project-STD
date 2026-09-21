# 03 网络协议接入（UDP 与 TCP）

> 通用规则见 `01`。本文讲网口专项：RJ45 共享 RB、UDP/TCP 逻辑通道、LwIP 约束、与串口协议的混合规则。

---

## 1. RJ45 共享 RB 与逻辑通道

- 物理 RB 只有一个：`RB_SLOT_RJ45`，容量 `RB_SIZE_RJ45 (1536U)`（`app_dispatch.h:37`，注释：≥ IAP 最大帧 1044 + 余量）。
- 逻辑通道共享该槽（`app_dispatch.h:47-58`）：

| CH_ID | 值 | 传输 | 通道实现（端口来源） |
|---|---|---|---|
| `CH_ID_TCP_SERVER` | 2 | LwIP TCP 服务器 | `app_tcp_server.c`（默认端口 9528，`app_net_boot` 按 Sector1 `net_cfg.port` 应用） |
| `CH_ID_TCP_CLIENT` | 3 | LwIP TCP 客户端 | `app_tcp_client.c`（默认 0.0.0.0:0 **未配置不连**；`app_tcp_client_set_remote` 配好后才 connect） |
| `CH_ID_UDP` | 4 | LwIP UDP | `app_udp.c`（端口 **10011 固定** `LDI_DISCOVERY_PORT`，创迪发现口 + IAP 共用；**贵州治超 2026-09-14 起也绑此口**作兼容通道） |
| `CH_ID_MQTT` | 5 | LwIP MQTT | `app_mqtt.c`（AH 平台，当前未激活） |
| `CH_ID_UDP_CQ` | 7 | LwIP UDP | `app_udp.c`（CQ 业务口：`PROTO_CHONGQING` 读 Sector1 `net_cfg.udp_port` 默认 20103；dev 共存构建固定 20103） |
| `CH_ID_UDP_GZOL` | 8 | LwIP UDP | `app_udp.c`（GZ_OL 专用业务口：读 Sector1 **`net_cfg.port`**，与 TCP 业务口同号不同协议栈；空/0 回退 9528；与 10011/CQ 同号跳过绑定——2026-09-14 用户裁决，doc/14 §13） |

- 协议侧提供 RB：`RB_PROVIDE_WEAK(rb_provide_rj45, RB_SIZE_RJ45)`（IAP `app_iap.c:20`、LDI `app_ldi.c:17`、CQ `app_cq_proto.c:43`）。
  **反例（2026-09-14，贵州治超 GZ_OL）**：串口协议追加网口绑定时**不要**再 provide，
  只用 `app_proto_acquire_buf(RB_SLOT_RJ45, RB_SIZE_RJ45)` 取用——多个同槽 provide 会由
  链接器合并为一份，但若某裁剪口径把既有提供者全排除会凭空多出 1536B；只 acquire 时
  该槽无提供者则返回 `nullptr`，协议应静默跳过网口绑定（GZ_OL `gz_ol_proto_init` 范式）。
- **每逻辑通道独立 mask + bind**（doc/05-01 §2.3）：LDI 一次 `ldi_module_init` 里 register 三次拿三个 mask，分别 bind TCP_SERVER/TCP_CLIENT/UDP（`app_ldi.c:242-252`），三 mask 共用 `g_ldi_msg_queue`。**禁止单 mask 绑多通道**。
- 风险门禁（doc/05-01 §2.4）：TCP 流 / UDP 报可能在同一 RJ45 RB 交错排列。以单网口业务为主时维持共享 1536；若 TCP 与 UDP 同时高压收发，评估拆逻辑 RB——新协议接入先按共享设计，超出预算再提案。

## 2. UDP 范例

### 2.1 IAP 升级协议（0x5A5A5A5A）

- 帧格式：`0x5A5A5A5A | seq(4B) | cmd(4B) | len(4B) | data | CRC32(4B)`（`app_iap.c:5-7` 注释；`FRAME_HEAD (0x5A5A5A5AU)` 定义于 `app_iap.h:12`；`FRAME_MIN_LEN (5U)`、`FRAME_MAX_LEN (5U+256U)` :9-10）。
- 只 bind `CH_ID_UDP`（`app_iap.c:39-53`）；与 LDI 同槽链式共存：首字节 0x5A vs 0xFF 快拒互斥。
- probe（`app_iap.c:96-155`）要点：首字节 `0x5A` 快拒 → 4B 帧头比对 `FRAME_HEAD` → len 合法性 ≤256 → 数据不足时**二次帧头扫描**（范围内出现另一 `0x5A` 立即 FAKE 重同步，防止伪头粘包）→ CRC32 校验 → READY。
- **广播应答**：`cmd01/cmd02` 回复经 `udp->src_ip` 回传，源 IP 置全 `0xFF`（255.255.255.255 广播）（`app_iap_cmd.c:60-63`）。依赖 `udp_channel_t` 在收包时记录 `src_ip[4]`/`src_port`（`app_udp.c`）。**已知限制（2026-08-21）**：src 快照是通道级单例，帧排队期间后续帧会覆盖快照——IAP 停等一问一答、低流量，竞态不修改，仅记录（见 doc/07 修订）。

### 2.2 LDI UDP 搜索（21H/12H）

- LDI 协议 bind UDP 通道用于「创迪发现口」搜索/应答（`app_udp.c` 注释：同端口 10011 承载 LDI 21H/12H 搜索与 IAP 升级，按帧头分流）。**注意区分两个端口语义**：10011 仅为搜索/上报的 UDP 传输渠道；12H 应答与 IAP 0x01 上报内容中的 `port` 字段是设备「配置功能端口」（TCP 业务口 9528，见 doc/07 §12 ④）。LDI probe 首字节 `0xFF` 快拒 + STX `0xFF 0xFF` + CRC16-XMODEM（`app_ldi.c:313-373`）。
- **12H 应答发送顺序（2026-08-21）**：广播为主（`app_udp_broadcast` 先发，复用常驻 conn 不占 netconn 池）、回源为辅（`channel_send` 后发、失败静默）——回源依赖通道级 src 快照，多协议交错下可能发错目标，广播是可靠路径。

### 2.3 UDP 通道实现要点（新协议发包侧）

- 应答直接 `channel_send(ch, data, len)`：`udp_ch_send` 用 `udp->src_ip` 重建 `ip_addr_t` 发回源地址（`app_udp.c`）——**通道层自动回源，协议层无需管理 IP**。
- **10011 端口语义**：该口是 STD 统一「发现口」+ IAP 升级口 + 地区协议（GZ_OL）兼容业务口。
  新增协议绑该口前先复核**首字节互斥**（IAP `0x5A` / LDI `0xFF` / CQ `'{'` 或 `FF FF`+12B），
  并在文档写清**外场上位机的目标端口**——源固件私口（如 9K23881580 的 10028）在本固件上
  不监听，联调必须把上位机目标改到本固件实际监听口（GZ_OL 先例见 doc/14 §11）。
- **第二条/第 N 条 UDP 实例范式（2026-09-14 起两个先例：`CH_ID_UDP_CQ`、`CH_ID_UDP_GZOL`）**：
  当协议需要**自己的服务口**（可配置或与既有口不同）时，在 `app_udp.c` **镜像实例**
  （`udp_xxx_task` + `udp_xxx_connect_task` + 私有 `_xxx_ch_send` + 模板 + 通道级单例），
  而不是让多条协议挤同一口：
  1. `app_dispatch.h` 增 `CH_ID_*` 并同步 `CH_ID_MAX`（`ch_proto_map[]`/`channels[]` 数组随之扩容）；
  2. 端口来源**显式定义**（先例：CQ = `net_cfg.udp_port`；GZ_OL = `net_cfg.port`）——每轮重建前
     重读（改配置 + 重启即生效），并写明**回退策略**（无效/0 → 回退出厂默认 + RTT 告警，
     **禁止静默绑定 0**）与**同口冲突策略**（与常驻口同号 → 跳过绑定 + RTT 告警并说明降级路径）；
  3. 实例由 `app_boot.c` 启动（无条件启动，与协议是否编入无关）；**必须在
     `app_net_boot_apply()` 之后**（端口已按 Sector1 应用）；
  4. 协议侧 `app_proto_bind_channel(mask, CH_ID_*)`；**同槽 RB 只 acquire 不 provide**；
  5. 资源核算（doc/06 纪律）：+1 常驻 netconn + 1 UDP PCB（取自 `MEMP_NUM_NETCONN`/
     `MEMP_NUM_UDP_PCB` 池，须核对余量）+ 2 任务栈（各 1KB，ucHeap）+ 通道实例静态 ~20B；
  6. 台账同步：doc/14（协议文档）、doc/CLAUDE.md 通道映射、doc/05-01 通道矩阵、doc/06-04 占用账。
- **源地址快照是通道级单例（每实例一份）**：`udp_channel_t.src_ip`/`src_port` 每次收包覆盖，
  帧经队列排队期间若有其它客户端发包会覆盖快照 → 协议应答可能发往最后发包者。
  **多实例并存不串台**（收包帧的 `frame_msg_t.ch` 决定回源走哪个实例），但同一实例内多客户端
  并发仍不保证逐帧回源（一问一答/停等式协议不受影响；IAP/LDI 既有已知限制，2026-08-21 记录）。
- 通道生命周期：`udp_task` 建 netconn/bind/派生 `udp_connect_task`；断链时 `udp_channel_deinit` 置 `ops=nullptr`、`state=DOWN`、`conn=NULL`（2026-08-21 补）、`app_channel_register(CH_ID_UDP, nullptr)`。协议层不要持有裸 `channel_t *` 跨断链使用——`channel_send` 对 `ops==nullptr` 有守卫（`app_dispatch.c`），但状态判断应像 LDI 那样经 `app_channel_get(CH_ID_*)` 每次现查。

## 3. TCP 范例（LDI 双通道）

- LDI 同时注册 TCP Server + TCP Client + UDP 三 mask（`app_ldi.c:242-252`）。
- **掩码隔离：同首字节协议的共存手段（2026-09-17 云南治超先例）**：`app_dispatch.c` 的
  `frame_dispatch_task` 先取 `proto = ch_proto_map[ch->ch_id]`，只有绑定在该通道上的协议才进
  probe 链（`app_dispatch.c:252`/内层 `:303`）。因此**首字节相同、但绑不同逻辑通道**的两个协议
  **不存在 probe 竞争**——云南治超（`'{'` 帧族）绑 TCP Server/Client，重庆 CQ（JSON 亦 `'{'` 开头、
  probe 做花括号深度扫描会抢认领 `7B 31 00 7D`）只绑 `CH_ID_UDP`/`CH_ID_UDP_CQ`，故 TCP 链上
  CQ probe 根本不会被调用；云南治超亦**不绑 10011** → CQ 行为零削弱、**未触碰 CQ 源码**。
  反向安全由命令字白名单保证（CQ JSON 第二字节 `'"'`、CQ 二进制首字节 `0xFF` → 本 probe FAKE；
  半帧 `avail<3` → WAIT 而调度器继续探测下一协议）。设计启示：**新协议与既有协议首字节冲突时，
  优先评估「换逻辑通道（掩码隔离）」而非改 probe 或依赖源码收录序**。详见 doc/15 §1/§7。
- **通道状态**：`channel_t.state`（`CH_STATE_UP/DOWN`，`app_dispatch.h:59-61`）。TCP 连接建立时 `tcp_channel_init` / `tcp_client_channel_init` 置 `CH_STATE_UP` + `app_channel_register`；断开时 conn 任务置 `ops=nullptr`、`state=DOWN`、`app_channel_register(..., nullptr)`。
- **断链状态机复位先例**：`ldi_timer_task` 每秒检查 `ch == nullptr || ch->state != CH_STATE_UP` → `g_ldi.state = LDI_ST_UNINIT`（`app_ldi.c`），断链后认证/上报状态自动回退。新协议若有会话状态机，必须设计等价的断链复位路径。
- **TCP Client 远端未配置不连（2026-09-18）**：编译期默认 `0.0.0.0:0`（IP 全 0 或 `port==0` 视为未配置）。`app_boot` 两口径仍 `app_tcp_client_start()`，但 idle **不** `netconn_new` / **不** connect，去重打 `[tcp_cli] idle (remote unset)`。远端只经 `app_tcp_client_set_remote` 注入（现行唯一调用方 = LDI 装载；绑本通道 ≠ 配置远端，YN_OL 先例）。排除 LDI 的镜像不再对历史硬编码 `192.168.2.17:9529` 外连。
- TCP 客户端非阻塞 connect + 4s 轮询（`tcp_client_task`），keepalive idle 10s / intvl 2s / cnt 3（`tcp_keepaliveinit`）——新协议用 TCP Client 模式可直接复用通道，不改 `app_tcp_client.c`。
- 若新协议需要新端口/新远端：用 `app_tcp_server_set_port()` / `app_tcp_client_set_remote(ip, port)`（后者释放 `client_disconnect_sem`：已连接则踢重连，idle 则下一秒进入 connect；改回哨兵则拆连接回 idle）；两个通道实例是单例，**多远端需求需要评估扩通道，不在接入范围内**。

## 4. LwIP 约束

- **广播依赖**：`lwipopts.h` 中 `LWIP_BROADCAST` 未定义，但 `IP_SOF_BROADCAST=1` + `IP_SOF_BROADCAST_RECV=1` 已生效（IAP 升级依赖，见 doc/CLAUDE.md「LwIP 配置要点」）；`MEMP_NUM_NETCONN=8`/`MEMP_NUM_UDP_PCB=8`（2026-08-21 覆盖，修复池满广播丢包；2026-09-14 重核：常驻 netconn **5** = UDP 10011 + UDP 20103 + UDP `net_cfg.port`(GZ_OL) + TCP listener + TCP client，叠加已连接客户端 6、广播回退临时 conn 峰值 **8 = 池容量**；UDP_PCB 常驻 3/8）。代码侧广播：`udp_task`/`udp_cq_task`/`udp_gzol_task` 建常驻 conn 时 `ip_set_option(conn->pcb.udp, SOF_BROADCAST)`（**该选项同时是收广播帧的前提**：`IP_SOF_BROADCAST_RECV=1` 下未置位的 pcb 收不到广播目的帧）；`app_udp_broadcast`/`app_udp_cq_broadcast` **复用常驻通道 conn**（`app_channel_get` 回验 + conn 非空）`netconn_sendto` 255.255.255.255，常驻未就绪回退临时 conn。新协议需要广播时调用 `app_udp_broadcast`（10011 口）或 `app_udp_cq_broadcast`（20103 口），勿自行 `netconn_new` 临时 conn（GZ_OL 专用口暂无 broadcast 封装，如需按其口径广播，参照 `app_udp_cq_broadcast` 增设）。
- **`pl_net_adapt.h` 禁止在任何 .h 中引用**（doc/CLAUDE.md 架构约束）：LwIP 类型（`struct netconn`、`ip_addr_t` 等）不得泄漏进协议头文件。协议 .c 需要 LwIP API 时只 include `pl_net.h`/`pl_net_adapt.h` 于 .c 内。
- **IP 类型隔离先例**：`udp_channel_t.src_ip` 用 `uint8_t[4]` 字节数组而非 `ip_addr_t`（`app_udp.c:160-164`），避免 Application 层暴露 middleware 类型。新协议存储对端地址照此办理。

## 5. 网络协议与串口协议混合规则

- **按槽绑定**：协议绑哪个 CH_ID 就吃哪个物理口的数据（RJ45 槽 / RS485 槽 / RS232 槽互不相通）。同一协议可同时绑网口和串口（如青海绑 RS485+RS232，LDI 绑三网口逻辑通道）——每通道一个 mask，probe 对 `ch` 参数通常不区分（除应答回源）。
- **同槽链式 probe**：RJ45 上 IAP/LDI 靠首字节（0x5A vs 0xFF）共存；RS485 上多地区协议靠帧头/第二字节共存（见 `02 §4`）。跨槽协议永远不存在 probe 竞争。
- **禁绑**：`CH_ID_RS232_1` 对任何协议（含网络协议）禁绑。
- 混合部署时注意 queue 深度与 payload 上限按**本协议绑定通道的最小 RB** 设计（如同时绑 RS485+RS232 时按 768 约束，绑 RJ45 时按 1536 约束）。