# TCP 连接建立成功后「发送数据板子无反应、必须重启板子才恢复」——只读排查报告

- 日期：2026-09-18
- 范围：云南治超屏协议（`yn_ol_`，`{` 帧族，绑 RS485+RS232+TCP Server+TCP Client）现场问题的候选机理逐一验证
- 性质：**只读排查**——未修改任何代码与文档；建议修复仅列于 §6，未落盘
- 症状原文：「有时候建立TCP连接后，发送数据板子没反应。需要重启板子再建立连接。」

## 0. 结论摘要（按可能性排序）

| 序 | 假设 | 可能性 | 与「重启才恢复」的吻合度 |
|---|---|---|---|
| ★1 | **TCP Server 单线程串行服务循环被「半开/孤儿连接」或「上位机第二条连接」永久占死**：accepted conn 无 keepalive、`netconn_recv` 无超时、TCP 通道不注册链路监听、LwIP 三次握手由 tcpip 线程完成而应用层 `netconn_accept` 永不执行 → 新连接「显示已连上」但数据只积压在 LwIP 内部 mbox，无人读取 | **最高** | 完全吻合 |
| 2 | 以太网 RX 停摆（PHY 链路瞬断后 RX 信号量/描述符不复位）——机理 1 的诱因场景常与之重合 | 中 | 吻合 |
| 3 | 通知/调度链路死锁或脏通知丢帧（2026-09-17 已修两处，只剩低概率窗口） | 低 | 部分吻合（只能解释「延迟」难解释「永久」） |
| 4 | LwIP netconn 池耗尽（`MEMP_NUM_NETCONN=8`）导致 listener 反复重建失败 | 低 | 与「连接成功」矛盾 |
| 5 | ucHeap 耗尽 → 任务创建失败 | 低 | 与「需手动重启」矛盾（会打 `[err]` + 33s 自动复位） |
| 6 | probe 永久 WAIT / RB 脏数据 | 已排除 | 矛盾（1000ms 预算已自愈，不需重启） |

**一句话根因（主假设）**：`tcp_server_task` 是「accept → 服务单一客户端（阻塞 recv）→ 断开 → 回 accept」的串行循环，而这条循环对「当前客户端已经死了」（半开连接，如直拔网线、交换机瞬断、上位机进程崩溃后残留）**完全没有检测手段**——没有 keepalive（对比 TCP Client 通道有 `tcp_keepaliveinit`）、没有 `netconn_set_recvtimeout`、TCP 通道不注册 `pl_net_register_link_listener`。半开连接把服务循环永久占住；此后上位机「重新连接」时 LwIP 内核照常完成三次握手（新连接进 `acceptmbox` 排队），上位机显示「连接成功」，但应用层永远不会 `netconn_accept`、永远没人 `netconn_recv` 这条新连接 → 发数据零反应 → **只有重启板子清空 TCP 栈才恢复**。「有时候」＝取决于故障前是否残留过连接（间歇性特征，与本机理完全一致）。

## 1. 症状的三个关键特征与判读

1. **「有时候」**（间歇性）→ 根因必然依赖某种历史状态（残留连接/连接次数/链路事件），而非必然发生的启动缺陷。
2. **「TCP 连接建立成功」** → 板子 IP 可达、TCP 栈在跑、三次握手完成。排除「netif 未配好 / 端口没监听」类必然性故障。
3. **「发数据无反应 + 重启才恢复」** → 状态卡死且**没有任何自愈路径**（无超时、无 keepalive、无链路重建），且卡死点不在 RTOS 调度器层（否则 IWDG 33s 会复位、设备会周期性重启，用户会观察到自动重启而不是「没反应」）。half_sec_task 一直在喂狗这一点可间接证明 RTOS 调度器本身活着，卡死的是 TCP 服务循环这个任务内部的串行状态。

## 2. 架构背景（本报告依赖的事实）

- TCP Server 通道是**单线程串行单客户端**服务循环（UAF 根治改造后，不再有「每客户端一任务」的并发模式）：
```140:241:Application/Src/Channel/app_tcp_server.c
void tcp_server_task(void *argument)
{
    (void)argument;

    for (;;) {
        struct netconn *conn = netconn_new(NETCONN_TCP);
        if (conn == NULL) {
            osDelay(500);
            continue;
        }
        ...
        netconn_listen(conn);
        ...
        /* 串行单客户端服务循环：同一 listener 反复 accept */
        for (;;) {
            struct netconn *newconn = NULL;
            err                     = netconn_accept(conn, &newconn);
            ...
            /* 服务该客户端直至断开 */
            tcp_channel_init(&s_tcp_ch, newconn, &g_tcp_server_channel_tmpl);
            ...
            while ((recv_err = netconn_recv(newconn, &buf)) == ERR_OK) {
                do {
                    netbuf_data(buf, &data, &len);
                    if (len > 0) { ... app_channel_dispatch(ch, (uint8_t *)data, len); }
                } while (netbuf_next(buf) >= 0);
                netbuf_delete(buf);
            }
            ...
            tcp_channel_deinit(&s_tcp_ch);
            netconn_close(newconn);
            netconn_delete(newconn);
            /* 回 accept 下一客户端 */
        }
        netconn_delete(conn);
    }
}
```
- LwIP 的**三次握手由 tcpip 线程完成**，应用层 `netconn_accept` 只是从 `acceptmbox` 取出已完成的连接：
```533:577:Middlewares/Third_Party/LwIP/src/api/api_msg.c
accept_function(void *arg, struct tcp_pcb *newpcb, err_t err)
{
  ...
  if (sys_mbox_trypost(&conn->acceptmbox, newconn) != ERR_OK) {
      ...tcp_abort...
  }
}
```
  即：**上位机显示「连接成功」≠ 应用层已 accept**。数据会积压在 `DEFAULT_TCP_RECVMBOX_SIZE=6`（`lwipopts.h:93`）的 recvmbox 里，直到有任务对它调 `netconn_recv`。

## 3. 逐条候选机理验证（按任务书顺序，均给代码证据）

### 机理 1：TCP Server accept 循环 / 连接生命周期——**主嫌疑，最高可能性**

**机理**：`tcp_server_task` 在 `netconn_accept` 之后进入「服务单一客户端的阻塞 recv 循环」（`app_tcp_server.c:169` → `:199`）。这条循环退出（回 accept）的唯一条件是 `netconn_recv` 返回非 `ERR_OK`，即对端正常 FIN/RST 或本地出错。以下三个事实叠加，制造了「循环被永久占死」的全部条件：

- **a. accepted conn 与 listener conn 都没有 keepalive**。全工程唯一的 `tcp_keepaliveinit` 定义在 TCP **Client** 通道，且只在 client 侧调用：
```246:253:Application/Src/Channel/app_tcp_client.c
__STATIC_INLINE void tcp_keepaliveinit(struct netconn *conn)
{
    if (conn == NULL || conn->pcb.tcp == NULL) return;
    ip_set_option(conn->pcb.tcp, SOF_KEEPALIVE);
    conn->pcb.tcp->keep_idle  = 10000;
    conn->pcb.tcp->keep_intvl = 2000;
    conn->pcb.tcp->keep_cnt   = 3;
}
```
```103:103:Application/Src/Channel/app_tcp_client.c
    tcp_keepaliveinit(conn);
```
  `app_tcp_server.c` 全文件无 `SOF_KEEPALIVE`；LwIP 的 keepalive 慢定时器**只处理设置了该选项的 pcb**（`tcp.c:1329`：`if (ip_get_option(pcb, SOF_KEEPALIVE) && ...)`）。⇒ 半开连接（对端无 FIN 消失）在板子上**永存**。
- **b. `netconn_recv` 无限阻塞**。`LWIP_SO_RCVTIMEO=1`（`lwipopts.h:126`）只是编译选项；server 侧从未调用 `netconn_set_recvtimeout`。⇒ 无数据时循环无限等待。
- **c. TCP 通道不注册链路监听**。`pl_net_register_link_listener` 的调用点只有 UDP 三个实例（`app_udp.c:146 / 351 / 566`），TCP Server/Client 两通道均无。且链路回调本身**只在 link up 时通知**：
```92:99:Platform/Src/pl_net.c
static void ethernet_link_status_updated(struct netif *netif)
{
    if (netif_is_up(netif) && netif_is_link_up(netif)) {
        bool link_up = netif_is_up(netif) && netif_is_link_up(netif);
        for (int i = 0; i < PL_NET_LINK_LISTENER_MAX; i++)
            if (g_link_listeners[i])
                g_link_listeners[i](link_up);
    }
}
```
  ⇒ 链路断开/恢复对 TCP 服务循环**零影响**（连 UDP 的「断链重建」其实也只对 up 事件生效）。

**触发场景（都是现场常见操作，解释「有时候」）**：
1. **半开连接**：上位机直拔网线 / 交换机瞬断 / 上位机进程崩溃后 OS 未发 FIN。板子无 keepalive、无重传（无待发数据则 RTO 定时器不启动）→ 旧连接在板子上永远 ESTABLISHED、`netconn_recv` 永远阻塞。
2. **上位机重复连接**：上位机「连接设备」再开第二条连接而不关第一条。第二条连接在内核层完成握手、进 `acceptmbox` 排队（`DEFAULT_ACCEPTMBOX_SIZE=6`，`lwipopts.h:95`，≤6 条排队，第 7 条才被 RST）；应用层还在服务第一条 → 第二条连接上的数据**无人读**。

**与症状吻合点**：连接建立成功（握手由内核完成）✓；发数据无反应（应用层没人 recv）✓；只有重启恢复（重启清空 TCP 栈、acceptmbox、半开连接）✓；间歇性（取决于故障前是否有残留连接）✓；RTOS 本身活着（喂狗正常、其它通道/显示正常）✓。
**矛盾点**：若上位机每次退出都正常 FIN（干净关闭），板子会自愈（recv 返回 ERR_CLSD → 回 accept），不需要重启。用户报告「需要重启」说明现场上位机存在「不 FIN 就消失」的路径（拔网线/崩溃/软件不关 socket 直接断开）——这正是本机理成立的前提，也是现场要确认的第一件事。

**现场验证/证伪（现有 RTT 诊断直接可用）**：
- 复现时（上位机显示已连接、发数据无反应）看 RTT：**没有新的 `[tcp_srv] accept <ip>:<port>` 行**（新连接未到应用层）且**没有 `[tcp_srv] close` 行**（旧连接未断）⇒ 机理 1 命中。
- 决定性实验：故障状态下让上位机**主动断开**（软件里点「断开」，确保发 FIN）→ 若板子立刻出现 `[tcp_srv] close reason=-13(ERR_CLSD)` 并随后恢复 accept、新连接立刻有 `recv` ⇒ 实锤「服务循环被旧连接占死」。
- 排除法：若故障时**有**新 `[tcp_srv] accept` 和 `[tcp_srv] recv` 行但无 `[yn_ol] rx` ⇒ 不是机理 1，转查机理 3/4。

### 机理 2：LwIP 资源耗尽（netconn 池）——低可能性，但存在一个无日志盲区

**机理**：`MEMP_NUM_NETCONN=8`（`lwipopts.h:134`）。常驻占用 = TCP Server listener 1 + accepted 1 + TCP Client 主 conn 1 + 连接 conn 1 + UDP 三实例 3 = 7；广播回退临时 conn 瞬时 +2 可到 9（`app_udp.c` 文件头注释）。若 netconn 泄漏（例如某条路径 `netconn_delete` 未走到），池耗尽后 `tcp_server_task` 的 `netconn_new` 持续返回 NULL：
```145:150:Application/Src/Channel/app_tcp_server.c
        struct netconn *conn = netconn_new(NETCONN_TCP);
        if (conn == NULL) {
            osDelay(500);
            continue;
        }
```
**吻合点**：重启恢复（池清零）✓。**矛盾点**：池耗尽时 listener 建不起来 → SYN 无人应答 → 上位机显示「连接失败/超时」，与「连接建立成功」直接矛盾；且本代码路径 500ms 重试 + 每次 `netconn_delete` 都成对（`app_tcp_server.c:157/232-237`），未发现泄漏点。
**验证**：本路径**没有任何 RTT 日志**（静默 500ms 重试）——这是本轮排查确认的盲区，建议补 `[tcp_srv] netconn_new NULL (pool exhausted)` 一行（见 §6 P2）。现场判别：若故障时上位机「连接超时」而非「连接成功」，才考虑本机理。

### 机理 3：动态任务创建失败（ucHeap 耗尽）——低可能性，与「手动重启」矛盾

**机理**：`tcp_server_task` 本身是动态创建（`app_tcp_server_start` → `osThreadNew`，`app_tcp_server.h:31-34`；`app_boot.c:313`），每连接任务（`tcp_client_conn_task`、`udp_*_connect_task`、`yn_ol_selftest_task`）也从 ucHeap 分配。若堆耗尽，`pl_task_create_checked` 会打 `[err] task '...' create FAILED (heap exhausted)`（`pl_task_guard.h:37-45`，恒开不走诊断门控）；更极端时命中 `vApplicationMallocFailedHook`：
```417:431:Application/Src/app_boot.c
void vApplicationMallocFailedHook(void) {
  ...
  SEGGER_RTT_printf(0, "[err] pvPortMalloc FAILED: ...");
  taskDISABLE_INTERRUPTS();
  for (;;) ;
}
```
```257:263:Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c
		if( pvReturn == NULL )
		{
			extern void vApplicationMallocFailedHook( void );
			xLastFailedAllocSize = xWantedSize;
			vApplicationMallocFailedHook();
		}
```
**矛盾点**：hook 会关中断死循环 → IWDG（≈33s）自动复位 → 设备表现为**周期自动重启**，而不是「没反应、等我手动重启」；且 RTT 必有 `[err]` 行。另外 2026-09-17 堆修复后启动期余量 23480B、23 个常驻任务已静态落 CCMRAM，每连接任务退出即归还（`tcp_client_conn_task` `osThreadExit()`，`app_tcp_client.c:243`），无累积泄漏证据。
**验证**：复现时查 RTT 有无 `[err]` 行、设备是否 33s 周期重启、post-boot `heap` 行余量是否异常。无 `[err]` + 无周期重启 ⇒ 排除。

### 机理 4：通知链路（ch_queue 通知丢失 / dispatch 卡死）——已修两处，仅剩低概率窗口

**现状（2026-09-17 已修）**：通知投递已加 20ms 重试 + `notify_drop` 计数 + `[disp] notify queue FULL` 告警（`app_dispatch.c:728-741`）；WAIT 预算改头部前缀指纹按 RB 分槽（`app_dispatch.c:595-617`）。
**剩余窗口**：
- a. **脏通知丢弃后 RB 数据无人认领**：`frame_dispatch_task` 对通知做通道回验，回验失败直接丢弃通知：
```363:363:Application/Src/app_dispatch.c
        if (ch == nullptr || app_channel_get(ch->ch_id) != ch)
            continue;
```
  场景：通道任务 `app_channel_dispatch` 刚把数据写进 RB 并入队通知，随即连接断开、通道 deinit 注销 → dispatch 处理到该通知时回验失败 → **数据留在 RB、通知被丢**，要等**下一条有效通知**才会被顺带扫出。这只能解释「上次发的帧重连后才被解析」（前两轮现场症状），**不能解释「永久无反应需重启」**——因为上位机持续发帧就会持续产生新通知。
- b. dispatch 任务被死锁/饿死：无证据路径（RB 锁只在 dispatch 内用 `rb_lock`，协议任务不碰；scan_task 不碰 RB；无优先级反转链）。若真死锁，重启恢复 ✓，但 `notify_drop` 会增长、且 `[disp] notify queue FULL` 会刷屏。
**验证**：复现时看 `[disp] notify queue FULL` / post-boot `notify_drop` 计数；若一直为 0 ⇒ 排除。

### 机理 5：协议侧 probe 永久 WAIT / CQ 抢帧——已排除

- **probe 永久 WAIT 已有自愈**：`yn_ol_probe_frame`（`app_yn_ol_proto.c:200-236`）的 WAIT（`avail < frame_len`，`:228`）受 `FRAME_WAIT_BUDGET_MS=1000ms` 头部指纹预算兜底，到期强制 `rb_skip(1)` 重同步并打 `[disp] WAIT budget ... expired`（`app_dispatch.c:595-617`）。残帧卡头最多卡 1s + 一次通知，**不需重启**。矛盾点：与「重启才恢复」不符。
- **CQ 抢帧不成立**：CQ 只绑 `CH_ID_UDP` / `CH_ID_UDP_CQ`（`app_cq_proto.c:115/119`），不绑 TCP；`frame_dispatch_task` 按 `ch_proto_map[ch->ch_id]` 过滤 ⇒ TCP 链上 CQ probe 根本不被调用。TCP 链上同槽只有 LDI（首字节 0xFF 快拒 `{`）。已核实无冲突路径。
- **验证**：复现时若出现 `[disp] probe idx=.. sta=WAIT/FAKE .. head8=..` 或 `[disp] WAIT budget .. expired` 行，说明帧到了调度层、自愈在工作，问题在更上层/更下层。

### 补充候选：以太网 RX 停摆（任务书未列，与机理 1 诱因重合）

**机理**：`ethernetif_input` 靠 `RxPktSemaphore` 唤醒（`pl_eth.c:338-346`，`HAL_ETH_RxCpltCallback` 释放，`pl_eth.c:62-66`）。若 RX 描述符链在 RX 池耗尽（`RxAllocStatus==RX_ALLOC_ERROR`）后重建失败、或链路瞬断后 DMA 不再产中断，则**所有入站帧（含新 SYN、数据、ping）全部收不到**；出站可能仍正常 → 上位机看到「连接还在」（其实半开）但发数据零反应。重启恢复 ✓。
**验证（关键区分实验）**：故障状态下用**另一台主机 ping 板子 IP**——ping 通 ⇒ 以太网层/TCP 栈活着，强化机理 1；ping 不通 ⇒ 本候选或 PHY 层问题（此时先查网线/交换机，再看 `[diag]` 的 PHY 链路状态）。

## 4. 综合排序（按「与症状吻合度」）

1. **机理 1（最高）**：TCP Server 串行服务循环被半开/孤儿连接或上位机第二条连接占死。全部症状吻合、代码证据最硬（keepalive/recvtimeout/链路监听三缺 + 握手与 accept 分离）。
2. **以太网 RX 停摆（中）**：与机理 1 的诱因场景（拔网线/交换机瞬断）高度重合，用 ping 实验 30 秒即可区分。
3. **机理 4 剩余窗口（低）**：只能解释「延迟解析」，不能解释「永久无反应」。
4. **机理 2（低）**：与「连接成功」矛盾；但 netconn_new NULL 无日志是确认过的盲区。
5. **机理 3（低）**：与「手动重启」矛盾（自动复位 + `[err]` 行）。
6. **机理 5（已排除）**：1000ms 预算自愈存在，不符「需重启」。

## 5. 下一轮现场验证方案（复用现有 RTT 诊断，一次抓包定论）

> 前提：板上镜像含 2026-09-17 晚联调第二轮诊断（`[tcp_srv]`/`[tcp_cli]`/`[disp] probe`/`[yn_ol]` 四类行 + post-boot `notify_drop`）。用 `JLinkRTTLogger` 落盘。

1. 复位，记下 `[diag]` 横幅：`dispatch qfull=0 resync=0 notify_drop=0`、`chan tcp_srv=.. port=..`。
2. 上位机连一次（应有 `[tcp_srv] accept <ip>:<port>`），发一条 `7B 01 01 01 7D`，确认正常路径 `recv → [yn_ol] rx cmd=0x01 → exec ret=0`。
3. **复现故障**：按现场方式（如拔网线再插回 / 上位机重复连接 / 直接杀上位机进程）制造「连接成功但无反应」。
4. 故障状态下按 §11.4 三段判据 + 下表逐项对照：

| 现场观测（故障时） | 结论 |
|---|---|
| 上位机显示已连接，但 RTT **没有**新的 `[tcp_srv] accept`，也**没有** `[tcp_srv] close` | **机理 1 命中**：服务循环被旧连接占死（半开或第二条连接在 backlog） |
| 上位机主动「断开」（真 FIN）后板子立刻出现 `[tcp_srv] close reason=-13` 并恢复 accept | 机理 1 实锤（旧连接占死） |
| 有 accept、有 `recv`，但无 `[yn_ol] rx`，伴随 `[disp] probe sta=WAIT/FAKE head8=..` | 转查调度/协议层（机理 4/5；head8 是原始帧头） |
| `[disp] notify queue FULL` / `notify_drop` 增长 | 机理 4 命中（通知丢失） |
| `[err] pvPortMalloc FAILED` / `[err] task '..' create FAILED` / 设备 33s 周期自动复位 | 机理 3 命中 |
| 上位机连接超时/失败（非成功） | 机理 2 方向（netconn 池），同时注意该路径当前无日志 |
| 另一台主机 ping 板子 IP 不通 | 以太网 RX/PHY 层问题（补充候选），先查物理链路 |
| ping 通、且上述 RTT 全部无输出 | 上位机发的数据根本没到这条连接（工具行为，前两轮判据 ① 重演） |

5. **给上位机侧的补充建议**（不涉及固件）：记录故障前后上位机的 socket 生命周期——是否「断开」按钮只关 UI 不关 socket、是否重复点击「连接设备」、故障前是否发生过拔网线/交换机瞬断/休眠。

## 6. 建议修复方向（仅建议，未实施）

- **P1（针对机理 1，核心）**：`tcp_server_task` 服务循环加固，任选组合：
  ① accepted conn 调 `tcp_keepaliveinit` 同款（`SOF_KEEPALIVE` + `keep_idle` 缩短到 30~60s，半开连接 1~2 分钟内判死退出循环）；或
  ② `netconn_set_recvtimeout(newconn, N)` + recv 返回 `ERR_TIMEOUT` 时**不 close、回 accept 再轮询**（需改成可重入轮询结构，避免断开正常空闲连接）；或
  ③ 注册链路监听并在**link down 时**（修复 `pl_net.c:92-99` 只在 up 通知的缺陷，link down 也回调）主动 `netconn_close` 当前客户端回 accept。
  **配套前提**：上位机必须保证正常断开（FIN）——若上位机做不到，①/② 是唯一自愈手段。
- **P2（机理 2 盲区）**：`app_tcp_server.c:145-150` `netconn_new` 失败时打一行 RTT（当前完全静默），与 UDP 的 gzol 实例告警对齐。
- **P3（机理 4 兜底，可选）**：`frame_dispatch_task` 回验失败丢弃通知时（`app_dispatch.c:363`），若该通道对应 RB 仍有数据，顺带无条件重扫一次该 RB（把「脏通知窗口」压成零）。
- **P4（可选，与本次症状弱相关）**：`pl_net.c` 链路回调补 link-down 通知（当前监听器参数恒 `true`，UDP「断链重建」其实从未被 down 事件触发过）。

## 7. 证据索引（file:line）

| 证据 | 位置 |
|---|---|
| 串行单客户端服务循环（accept→recv→close→回 accept） | `Application/Src/Channel/app_tcp_server.c:140-241` |
| recv 无限阻塞（无 recvtimeout） | `app_tcp_server.c:199`；`lwipopts.h:126`（选项存在但未使用） |
| keepalive 只在 TCP Client 侧设置 | `app_tcp_client.c:103`、`app_tcp_client.c:246-253`；server 全文件无 `SOF_KEEPALIVE` |
| LwIP keepalive 只处理设了选项的 pcb | `Middlewares/Third_Party/LwIP/src/core/tcp.c:1329` |
| 链路监听只被 UDP 三实例注册；回调只在 up 时通知 | `app_udp.c:146/351/566`；`Platform/Src/pl_net.c:92-99` |
| 三次握手由 tcpip 线程完成；应用 accept 只是取 acceptmbox；mbox 满 tcp_abort | `api_msg.c:533-577`；`lwipopts.h:95`（ACCEPTMBOX=6） |
| 新连接数据积压上限 | `lwipopts.h:93`（DEFAULT_TCP_RECVMBOX_SIZE=6） |
| netconn 池容量与 netconn_new NULL 静默重试 | `lwipopts.h:134`；`app_tcp_server.c:145-150` |
| 堆失败钩子（关中断死循环 + IWDG 33s 自动复位） | `app_boot.c:417-431`；`heap_4.c:257-263` |
| 任务创建判空报告（恒开） | `Platform/Inc/pl_task_guard.h:37-45`；`app_boot.c:313` |
| 通知重试 + notify_drop 计数 | `app_dispatch.c:728-741` |
| 脏通知回验丢弃 | `app_dispatch.c:363` |
| WAIT 预算（头部指纹 + 1000ms + 强制 resync） | `app_dispatch.c:595-617` |
| yn_ol probe（首字节快拒 / 命令字白名单 / 长度+尾字节校验） | `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c:200-236` |
| CQ 只绑 UDP/UDP_CQ（不绑 TCP） | `app_cq_proto.c:115/119` |
| 以太网收包线程（信号量驱动） | `Platform/Src/pl_eth.c:338-346`、`pl_eth.c:62-66` |

## 8. 排查纪律声明

本轮为**只读排查**：未修改任何源码、头文件、文档与构建产物；未烧录、未提交 git、未改 git 配置。所有修复建议仅存在于本报告 §6。
