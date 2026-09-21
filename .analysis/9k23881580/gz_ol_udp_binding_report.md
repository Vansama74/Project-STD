# 贵州治超屏协议（GZ_OL）增绑 UDP `CH_ID_UDP`（10011）——实施与验证报告

**执行人**：实施 + 构建验证（单会话）
**日期**：2026-09-14
**范围**：只改 `Application/{Src,Inc}/ProtocolParser_GuiZhou_Overload/` 两文件 + 文档；**未改** `.eide/eide.yml`、其它协议、`Makefile`；未 commit；未做 git 网络操作。
**结论**：四构建口径（`PROTO=ALL/CQ` × `DISP=1_263/22_1703`）**全部链接通过、零新增告警**；
SRAM 余量 **496B / 4776B / 416B / 4696B**（与增绑前逐字节相同）；CCM 余量 1_263 口径 **28452B**，
22_1703 口径实测 54116B（与文档基线 26372B 不可比——并发驱动改动所致，**逐字节归因见 §5.5**）；
默认口径 elf md5 **`ef0eeaa2ede395242990ec8b49b7acd0`**。帧头互斥与应答路径核对均通过（§3/§4）。

---

## 0. 摘要

| 项 | 结果 |
|---|---|
| 绑定实现 | `gz_ol_proto_init` 第 3 次 `app_proto_register`（挂 **RJ45 槽 RB**）+ `app_proto_bind_channel(_, CH_ID_UDP)`；三通道三 mask 共用同一静态队列 |
| RJ45 RB | **不新增缓冲**：只 `acquire`，槽体由 IAP/LDI/CQ weak 提供（两 make 口径 + EIDE Debug 均有提供者，§2.3） |
| 帧头互斥（RJ45 槽） | **无冲突**：`0x54` 与 IAP `0x5A`/LDI `0xFF`/CQ `'{'` 或 `FF FF`+12B 双向首字节快拒（§3） |
| 应答路径 | **可用**：`channel_send` → `app_channel_get(CH_ID_UDP)` 回验 → `udp_ch_send` 用通道缓存的 `src_ip/src_port` 单播回源；字节序链已核对（§4） |
| 内存 | text +64 / bss +4（多 1 个 mask 变量），rodata/data/ccmram **+0**；SRAM 合计逐字节不变（§5.4） |
| 口径语义差异 | 源固件设备侧 UDP 口 **10028 + 广播应答**，本实现 **10011 + 单播回源** → 联调上位机须改目标端口（§7-1/2） |

---

## 1. 前置协调与工作区状态（并发检查）

1. **并发构建检查（开工时，15:36）**：`ps -ef | grep -E 'make|arm-none-eabi-gcc'` **无匹配**（仅有空闲的 `arm-none-eabi-gdb`，CPU 0:00，无 OpenOCD 连接）；stamp 复核任务报告
   `.analysis/9k23881580/stamp_verify_report.md` **已生成**（15:16），已完整阅读，未与其结论矛盾，**无需轮询等待**。
2. **共享产物被 EIDE 覆盖（复核报告 R1 高风险的实例）**：开工时 `build/Debug/Project_STD.elf`（mtime **15:33**）
   是 **EIDE Debug 产物**而非 make 任何口径（同目录含 `.obj/`、`.lnp`、`.objlist`、`builder.params` 等 EIDE 独有产物；
   其 `.ccmram = 5040` ≠ make 四口径任一值）。make 侧 237 个 `.o`（15:15）与 `.build_stamp` 完好。
   本轮**未执行 `make clean`**（避免再清 EIDE 的 `.obj` 缓存）；借本次源码改动触发重编+重链接，最终产物为 make 默认口径，
   并经「重复 `make -j8` = 0 编译 0 链接、md5 不变」确认（§5.3）。
3. **另一路并发源码改动（与本次无关，但影响 22_1703 口径基线）**：`Device/Display/dev_display_22_1703.c`
   于 **15:26** 被另一路修改（`find -newermt 15:16` 全仓仅此文件 + 本次两文件），其 `_22_1703_pixel_map`/`_22_1703_hub75_buff`
   当前各为 **512B**（`nm` 实测 `0x200`）、模组 `.ccmram` 贡献 **3904B**（map 实测 `0xf40`），而文档既记 31648B。
   **本次未触碰该文件**，22_1703 口径的 CCM 差异已逐字节归因（§5.5）。

---

## 2. 代码改动（diff 级）

### 2.1 `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c`

**(a) 文件头注释**：`绑定 RS485 + CH_ID_RS232 双通道（UDP 待评估）` → 三通道说明 + 10011/10028 差异警示 + **RJ45 RB 纪律**（不 provide）：

```diff
- * 绑定 RS485 + CH_ID_RS232 双通道（README Q10 推荐 A：串口双通道即覆盖源固件
- * 串口三路行为；UDP 绑定待联调确认上位机实际通道后再评估）。
+ * 绑定 RS485 + CH_ID_RS232 + CH_ID_UDP 三通道（2026-09-14 增绑 UDP，用户裁决）：
+ *   - 串口双通道覆盖源固件串口三路行为（README Q10）；
+ *   - UDP 先绑 STD 既有 10011 通道（CH_ID_UDP）用于**最快验证上位机是否走网口**。
+ *     ⚠ 联调语义差异：源固件 9K23881580 设备侧 UDP 口是 **10028** 且搜索应答走广播；
+ *     本轮绑的是 STD 10011 通道 + 单播回源（channel_send）→ 上位机须发往 10011，
+ *     按源固件默认端口 10028 发送收不到（详见 doc/14 README §11）。
...
+ * RJ45 RB 纪律：本模块**不** provide rb_provide_rj45 —— 网口槽体由 IAP/LDI/CQ 等
+ *   同槽协议 weak 提供（三者在全协议与 PROTO=CQ 口径均编入）；本模块只 acquire，
+ *   避免任何裁剪构建下多出一份 1536B 缓冲（PROTO=ALL 口径 SRAM 余量仅 496B）。
```

**(b) mask 变量**：新增第三个 mask（唯一新增的静态 RAM 对象，4B）：

```diff
-RB_PROVIDE_WEAK(rb_provide_rs485, RB_SIZE_RS485);
-RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232);
-
-static proto_mask_t s_gz_ol_mask;
-static proto_mask_t s_gz_ol_mask_rs232;
+/* 地区协议通道 RB：与青海/四川/山东/贵州/云南/安徽等同槽 weak 合并。
+ * RJ45 槽体不在此 provide（由 IAP/LDI/CQ 提供，见文件头「RJ45 RB 纪律」）。 */
+RB_PROVIDE_WEAK(rb_provide_rs485, RB_SIZE_RS485);
+RB_PROVIDE_WEAK(rb_provide_rs232, RB_SIZE_RS232);
+
+static proto_mask_t s_gz_ol_mask;        /* CH_ID_RS485（RS485 槽 RB） */
+static proto_mask_t s_gz_ol_mask_rs232;  /* CH_ID_RS232（RS232 槽 RB） */
+static proto_mask_t s_gz_ol_mask_udp;    /* CH_ID_UDP 10011（RJ45 槽 RB，同槽 weak 提供） */
```

**(c) init 流程**（`acquire` → `register` → `bind`，与 CQ/LDI 多通道写法一致；队列在三 mask 注册后统一挂）：

```diff
     ring_buffer_t *rb_rs232 = app_proto_acquire_buf(RB_SLOT_RS232, RB_SIZE_RS232);
     if (rb_rs232 != nullptr) {
         s_gz_ol_mask_rs232 = app_proto_register(gz_ol_probe_frame, rb_rs232);
         if (s_gz_ol_mask_rs232 != 0)
             app_proto_bind_channel(s_gz_ol_mask_rs232, CH_ID_RS232);
     }
 
+    /* 网口槽（CH_ID_UDP = 10011，LDI 发现口 + IAP 同口共享）：独立 mask 挂 RJ45 RB。
+     * 槽体由 IAP/LDI/CQ weak 提供（本模块不 provide，见文件头）；若极端裁剪构建下
+     * 同槽无任何提供者，acquire 返回 nullptr → 静默跳过网口绑定，串口双通道不受影响。 */
+    ring_buffer_t *rb_rj45 = app_proto_acquire_buf(RB_SLOT_RJ45, RB_SIZE_RJ45);
+    if (rb_rj45 != nullptr) {
+        s_gz_ol_mask_udp = app_proto_register(gz_ol_probe_frame, rb_rj45);
+        if (s_gz_ol_mask_udp != 0)
+            app_proto_bind_channel(s_gz_ol_mask_udp, CH_ID_UDP);
+    }
+
     s_gz_ol_queue = osMessageQueueNew(GZ_OL_QUEUE_DEPTH, GZ_OL_MSG_SIZE, &s_gz_ol_queue_attr);
     app_proto_set_frame_queue(s_gz_ol_mask, s_gz_ol_queue);
     if (s_gz_ol_mask_rs232 != 0)
         app_proto_set_frame_queue(s_gz_ol_mask_rs232, s_gz_ol_queue);
+    if (s_gz_ol_mask_udp != 0)
+        app_proto_set_frame_queue(s_gz_ol_mask_udp, s_gz_ol_queue);
```

**(d) probe 注释补网口语义**（**判定逻辑零改动**）：⑥ 的长度上界说明改为「三槽取保守下界
`RB_SIZE_RS485-1`（RJ45 槽 1535 更宽不影响：结构合法帧 ≤256，超 256 走 ⑨ SKIP）」，
并补「网口槽链式共存」一段（IAP/LDI/CQ 对 `0x54` 快拒、本 probe 对 `0x5A`/`0xFF`/`0x7B` 快拒）。
长度上界仍用 `RB_SIZE_RS485-1U`（第 168 行）：语义是「不得超 RB 可用容量以免永久 WAIT」，
三槽取保守值即可，**不引入行为变化**。

> 未改动项（遵守约束）：probe 判定链、`gz_ol_parse_frame`、命令执行/应答组帧、
> `app_scroll_render_lock` 渲染互斥、上电画面、队列放置（CCM 1136B）、任务栈。

### 2.2 `Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h`

```diff
- * 传输：RS485 / RS232（源固件另有 UDP，本接入按 README Q10 推荐先只绑串口双通道）。
+ * 传输：RS485 + RS232 + **UDP 10011**（CH_ID_UDP，2026-09-14 增绑；源固件设备侧
+ * UDP 口为 **10028** 且搜索应答走广播 → 上位机按源固件默认端口发帧收不到，须发 10011，
+ * 详见 doc/14 README §11）。RJ45 槽 RB（1536B）由 IAP/LDI/CQ weak 提供，本模块只
+ * acquire 不 provide。
...
 _Static_assert(GZ_OL_PAYLOAD_MAX <= RB_SIZE_RS485, "GZ_OL frame must fit RS485 RB");
 _Static_assert(GZ_OL_PAYLOAD_MAX <= RB_SIZE_RS232, "GZ_OL frame must fit RS232 RB");
+_Static_assert(GZ_OL_PAYLOAD_MAX <= RB_SIZE_RJ45, "GZ_OL frame must fit RJ45 RB (UDP 10011)");
```

### 2.3 RJ45 槽 RB 提供者核实（`nm` + `git grep RB_PROVIDE_WEAK`）

`RB_PROVIDE_WEAK(rb_provide_rj45, RB_SIZE_RJ45)` 的出现位置：`app_iap.c:17`、`app_ldi.c:17`、
`app_cq_proto.c:43`、`ah_mqtt.c:11`（多 TU 同名 weak → 链接器保留一份，**只占 1536B**）。

| 口径 | 该槽提供者 | 证据 |
|---|---|---|
| `make PROTO=ALL`（含 LDI） | IAP / LDI / CQ | 三 TU 均编入（`SRC_APPLICATION` 未过滤 IAP/CQ） |
| `make PROTO=CQ` | IAP / CQ | `Makefile:410-412` 仅 `filter-out Application/Src/LDI/%`；实测 elf：`W rb_provide_rj45` + `b rb_provide_rj45_buf.2` |
| EIDE Debug（排除 CQ） | IAP / LDI | 15:11 EIDE elf 中 `W rb_provide_rj45`（已由复核报告 §1 记载；15:33 同） |

→ **没有出现「某口径缺提供者」的情况**，故按用户纪律**不 provide、不硬加缓冲**；代码仍保留
`nullptr` 静默降级路径（未来若新增极端裁剪构建亦然）。

---

## 3. RJ45 槽帧头互斥核对（问题②）

**同槽协议 probe 首字节判别（代码级证据）**：

| 协议 | probe 位置 | 首字节判定 | 对 `0x54` 帧的结果 |
|---|---|---|---|
| IAP（`0x5A5A5A5A`） | `app_iap.c:107-114` | `avail==0 → FAKE`；`first != 0x5A → FAKE`（在任何 WAIT 之前） | **FAKE** |
| LDI（`0xFF 0xFF`） | `app_ldi.c:408-415` | `avail==0 → FAKE`；`first != 0xFF → FAKE`；后续 `stx[1]!=0xFF`/`ver!=0` → FAKE | **FAKE** |
| 重庆CQ | `app_cq_proto.c:148-219`（`cq_probe_frame`） | `first=='{'` → JSON 花括号深度扫描（深度>16/累计>1044 → FAKE）；`first==0xFF` → **12B 精确比对**重启/搜索两帧（`FF FF 00 00 00 00 00 02 …`），前缀不匹配 → FAKE；**其余首字节 → FAKE** | **FAKE** |
| AH_MQTT | `ah_mqtt.c:127-137` | 任意非空即 READY（弱判别） | 不参与：`sw_app_initcall` 已注释（`ah_mqtt.c:209`）未注册；且只 `bind_channel(_, CH_ID_MQTT)`（`ah_mqtt.c:205`），与 10011 不同逻辑通道 |
| **贵州治超 GZ_OL** | `app_gz_ol_proto.c:146-148` | `avail==0 → FAKE`；`head != GZ_OL_GUIDE0(0x54) → FAKE` | READY（自己的帧） |

**反向核对（GZ_OL 不误吞别人的帧）**：本 probe 对首字节 `0x5A`/`0xFF`/`0x7B` 均
在第一步 FAKE（`head != 0x54`），且链路注册序（Makefile 收录序）为
**IAP → LDI → CQ → … → GZ_OL**（`Makefile:352-392`），前三个 probe 先判。

**对「某个 CQ 二进制帧首字节可为 0x54」的专门核查**：不存在。CQ probe 只认两类
——`{` 起始的 JSON（首字节 `0x7B`）与两个**固定模板**的 12B 二进制帧（模板首两字节均为
`FF FF`，`app_cq_proto.c:47-48`）；其余首字节一律 FAKE。故 `0x54` 在 RJ45 槽**唯一属于 GZ_OL**。

**残余风险（理论级，接受）**：`frame_dispatch_task` 在「所有 probe 都 FAKE」时按 1 字节
重同步（`app_dispatch.c`）。若某协议帧**已被判 FAKE/SKIP**（即本身不合法）且其字节流中恰好在
某偏移出现 `54 43 4C 59` + 合法长度 + 尾字节 `0x00`，GZ_OL probe 会误认领。与既有各协议
链式共存风险同源（任何「4 字节引导串 + 长度 + 尾字节」定界协议都一样），非本次引入；缓解：
4 字节引导串全匹配 + `len` 上下界 + 尾字节三重校验，且被误认领的帧本身已是非法帧。

---

## 4. 应答路径核对（问题③）

**结论：UDP 通道上的 `0x70`/`0x50`（及 `0x10`）单播回源可用，无需修正。**

1. **协议侧**：`gz_ol_execute_cmd(msg->ch, …)` 用 `frame_msg_t.ch`（= 收帧通道指针，
   由 dispatcher 填 `msg->ch = ch`）→ `_gz_ol_send_frame()` → `channel_send(ch, buf, total)`。
2. **框架侧**（`app_dispatch.c` `channel_send`）：`ch->ops != nullptr` + **`app_channel_get(ch->ch_id) == ch`** 回验
   + `ops->send != nullptr`。`CH_ID_UDP` 的通道实例是 `app_udp.c` 的**文件级单实例 `s_udp_ch`**
   （UAF 根治设计），`udp_channel_init` 注册 `&s_udp_ch.me`（`app_udp.c:174`），回验必然通过。
3. **通道侧**：`udp_ch_send`（`app_udp.c:96-113`）用 `udp->src_ip[4]` + `udp->src_port`
   重建目的地址后 `netconn_sendto(conn, …)`；`src_ip/src_port` 在**每包收齐时于 dispatch 之前**
   写入（`app_udp.c:199-206`）。
4. **字节序链核对（逐层）**：接收 `udp.c:234` `src = lwip_ntohs(udphdr->src)`（**主机序**）
   → `netbuf_fromport` 即主机序 → 存入 `src_port`；发送 `netconn_sendto` 把 `port` 直接写入
   `buf->port`（`api_lib.c:922-927`）→ `udp_sendto(..., dst_port)` → `udp.c:786`
   `udphdr->dest = lwip_htons(dst_port)`（**入参主机序**）。语义自洽，无字节序缺陷。
5. **已知限制（与 IAP/LDI 同源，2026-08-21 已记录）**：`src_ip`/`src_port` 是**通道级单例快照**，
   帧在队列/任务排队期间若另一客户端发包会覆盖快照 → 应答发往「最后发包者」。本协议应答
   均为一问一答/停等（`0x10`/`0x50`/`0x60`→`0x70`），单客户端联调不受影响；
   多客户端并发下不保证逐帧回源（要根治需把源地址随帧入队，`frame_msg_t` 无该字段，超出本轮）。
6. **`0x40` 改 IP 后复位**：`_gz_ol_send_frame` → `osDelay(100)` → `NVIC_SystemReset()`。
   `netconn_sendto` 经 tcpip 线程同步处理后返回（非 fire-and-forget），31B 帧在 100ms 内必然
   发出，复位前应答可送达（与源固件 `delay_ms(30)` 同语义、余量更大）。

---

## 5. 构建验证（问题④）

### 5.1 命令、结果与耗时（`-j8`，GCC Debug，`arm-none-eabi-size -A`，日志 `/tmp/gzol_udp/*.log`）

| # | 命令 | EXIT | 编译 | 链接 | 耗时 |
|---|---|---|---|---|---|
| B1 | `make -j8`（默认；增量：改动的 3 TU + 链接） | 0 | 3 | 1 | 0.35s |
| B2 | `make -j8 PROTO=CQ` | 0 | 232 | 1 | 1.82s |
| B3 | `make -j8 DISP=22_1703` | 0 | 237 | 1 | 1.59s |
| B4 | `make -j8 DISP=22_1703 PROTO=CQ` | 0 | 232 | 1 | 1.54s |
| B5 | `make -j8`（回默认口径） | 0 | 237 | 1 | 1.63s |
| B6 | `make -j8`（注释定稿后增量） | 0 | 3 | 1 | — |
| B7 | `make -j8`（重复） | 0 | **0** | **0** | no-op，md5 不变 |

口径切换全部由 stamp 触发全量重编 + 重链接（B2/B3/B4/B5），无需手工删产物。
**告警**：全量构建仅 3 条既有 `HAL_Driver` `-Wunused-parameter`；`gz_ol` 相关 0 告警；
`ReadLints` 对两目录 0 诊断。

### 5.2 段尺寸与余量（本轮实测）

| 口径 | .text | .rodata | .data | .ccmram | .bss | heap_stack | SRAM 合计 | **SRAM 余量** | CCM 余量 |
|---|---|---|---|---|---|---|---|---|---|
| `PROTO=ALL` `DISP=1_263` | 163716 | 197760 | 1664 | 37084 | 126352 | 2560 | 130576（99.6%） | **496B** | 28452B |
| `PROTO=CQ` `DISP=1_263` | 150524 | 197104 | 860 | 37084 | 122876 | 2560 | 126296 | **4776B** | 28452B |
| `PROTO=ALL` `DISP=22_1703` | 163940 | 197840 | 1744 | 11420 ⚠ | 126352 | 2560 | 130656 | **416B** | 54116B ⚠ |
| `PROTO=CQ` `DISP=22_1703` | 150764 | 197192 | 940 | 11420 ⚠ | 122876 | 2560 | 126376 | **4696B** | 54116B ⚠ |

**四条口径 SRAM 余量全部为正**，且与增绑前基线**逐字节相同**。

### 5.3 最终工作区状态（默认口径）

- 口径：**`PROTO=ALL` `DISP=1_263`**（GCC Debug）。
- `build/Debug/Project_STD.elf` **md5 = `ef0eeaa2ede395242990ec8b49b7acd0`**（大小 2055 236B）；
  hex md5 `b7b6e26c39e0833ba9958adf2ab1277f`、bin md5 `6ae9919d5d8193d0a2769abecf19b209`。
- 段尺寸：`.text 163716 / .rodata 197760 / .data 1664 / .ccmram 37084 / .bss 126352`
  （SRAM 130576B、CCM 37084B）。
- **确定性/幂等**：B5（全量重编）与 B1/B6（增量）md5 逐字节一致；B7 重复 `make` 0 编译 0 链接、
  md5 与文件 mtime 均不变 → 产物就是当前源码的默认口径 make 产物，可直接烧录。
- 说明：开工时被 EIDE 覆盖的 `build/Debug/` 已由 make 重写；EIDE 残留的 `.obj/`、`.lnp`
  等文件仍在（本轮未 `make clean`，保留 EIDE 缓存）。

### 5.4 与基线比对（本模块增量）

| 口径 | 基线（增绑前，同树仅本模块不同） | 本轮实测 | 增量 |
|---|---|---|---|
| `PROTO=ALL` `DISP=1_263` | text 163652 / rodata 197760 / data 1664 / ccmram 37084 / bss 126348 | text 163716 / bss 126352 | **text +64 / bss +4**，其余 +0，SRAM 合计 +0 |
| `PROTO=CQ` `DISP=1_263` | text 150476 / data 860 / ccmram 37084 / bss 122872 | text 150524 / bss 122876 | **text +48 / bss +4**，其余 +0，SRAM 合计 +0 |
| `PROTO=ALL` `DISP=22_1703` | text 163924 / ccmram 39164 | text 163940 / ccmram 11420 | text +16；**ccmram −27744 全部来自并发驱动改动**（下） |
| `PROTO=CQ` `DISP=22_1703` | text 150732 / data 940 / ccmram 39164 | text 150764 / data 940 / ccmram 11420 | text +32；同上 |

- **bss +4** = 新增 `s_gz_ol_mask_udp`（`nm` 实测 `bss.s_gz_ol_mask_udp = 4`）；
  SRAM 合计不变是因为 `._user_heap_stack` 对齐空隙由 2564 → 2560（−4）吸收。
- **text 增量的口径差异（+64 vs +48）与 22_1703 两行的 +16/+32**：`app_gz_ol_proto.c`
  本身与 `PROTO`/`DISP` 无关（同一 TU），差异来自链接期对齐/`--gc-sections` 保留集变化
  （22_1703 两行还叠加并发驱动改动的 −text）。**决策相关结论（SRAM/CCM 余量）不受影响**。
- 模块级证据（`arm-none-eabi-size -A` 对象）：`app_gz_ol_proto.o` → `.ccmram 1136`（未变）、
  `.bss.s_gz_ol_mask*` 各 4、`.text.gz_ol_proto_init 208`。

### 5.5 ⚠ 22_1703 口径 CCM 差异归因（另一路改动，非本次）

- 现状：`Device/Display/dev_display_22_1703.c`（mtime **15:26**）中
  `_22_1703_pixel_map` = `_22_1703_hub75_buff` = **512B**（`nm`：`0x200` 各一），
  `.ccmram` 段总 3904B（map：`dev_display_22_1703.o .ccmram = 0xf40`）；文档既记该模组 31648B。
- 差值核对：**39164 − 11420 = 27744 = 31648 − 3904**，逐字节吻合 → 该差异 **100% 来自驱动改动**；
  本模块 CCM 1136B 未变。
- 该口径 SRAM 两行（130656 / 126376）与历史基线**逐字节一致**。
- 结论：**22_1703 两行的 CCM 数字标记 `[待更新]`**（doc/14 §6、doc/06-04 §7.1 已加标记），
  待显示驱动定稿后重采；本轮不修，也不改他人的文件。

---

## 6. 文档更新清单（问题⑤）

| 文档 | 更新位置 | 内容 |
|---|---|---|
| `doc/14_贵州治超屏协议/README.md` | §1 传输 | RS485+RS232 → **+UDP 10011**；RJ45 RB 不 provide；端口差异指向 §11 |
| | §3 模块设计与接入点 | `app_gz_ol_proto.c` 职责（不 provide RJ45）；绑定行 → 三通道三 mask；RB 行 → 只 acquire + 降级路径；probe 链 ⑥ 上界说明；新增「网口槽链式共存」段 |
| | §4 帧头冲突纪律 | RJ45 行 → 「已绑，`0x54` 唯一」；新增反向证据（IAP/LDI/CQ probe 行号 + CQ 无 `0x54` 帧 + AH_MQTT 未注册） |
| | §6 内存与构建状态 | 四口径表重采；SRAM 侧 13B → 17B；新增「UDP 增绑增量」段；新增 22_1703 `[待更新]` 归因块 |
| | §8 Q10 / §9 as-built / §10 | Q10 采纳 → 已绑 UDP + 差异说明；绑定行 → 三通道；联调行 → 10011；联调帧段补 UDP 说明 |
| | **新增 §11**（UDP 通道绑定与联调注意）+ §12 修订记录 | 绑定事实、端口语义对照表（10028/广播 vs 10011/单播）、应答路径核对、3 条可选后续 |
| `doc/CLAUDE.md` | 文档地图行（`doc/14`）+ 贵州治超小节 | 绑定三通道；SRAM 侧 17B；RB 只 acquire；10028/10011 差异；帧头互斥补网口复核结论 |
| `doc/05_协议模块多协议兼容优化/01_architecture.md` | §4 RB/绑定规划表 | 新增 RJ45 + 贵州治超行（只 acquire 不 provide） |
| | §4 兼容矩阵 | 新增「贵州治超 + IAP/LDI/CQ（10011 同槽）」行（双向快拒证据 + 注册序 + 不占第二份缓冲）；`CH_ID_RS232_1` 行补 UDP |
| | §4 变更记录 | 新增「2026-09-14（GZ_OL 增绑 UDP 10011）」条（增量 + 端口差异） |
| `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` | §7.1（新增小节）+ 文末修订 | UDP 增绑重采表 + 干净 A/B 增量 + 22_1703 `[待更新]` 归因 + 默认口径 md5 |
| `doc/08_协议模块接入规则/03_网络协议接入_UDP与TCP.md` | §1 表 + RB 提供 bullet + §2.3 | `CH_ID_UDP` 行 → GZ_OL 也绑；新增「串口协议追加网口绑定只 acquire 不 provide」范式；新增「10011 端口语义/私有口复现成本」「src 快照单例限制」两条 |
| `doc/08_协议模块接入规则/02_串口协议接入.md` | §1 表下 | 新增「串口协议可同时绑网口（每通道独立 mask）」提示（GZ_OL 先例 + 不 provide RJ45） |
| 未改 | `.eide/eide.yml`、`Makefile`、其它协议、`doc/01/03/13` 等 | 无绑定或内存口径变化 |

---

## 7. 遗留项（问题⑥）

1. **复现源固件 10028 口（可选后续，本轮不做）**：需新增 UDP 通道实例（新 `CH_ID` + `app_udp.c`
   镜像/参数化实例），并核算 `MEMP_NUM_UDP_PCB` / `MEMP_NUM_NETCONN`（当前各 8，dev 共存
   baseline 已用 4：10011 + 20103 + TCP Server + TCP Client）、每实例 1KB 任务栈（ucHeap）
   与 netconn PCB 开销。**联调若证明上位机只认 10028，需开此项**。
2. **广播 vs 单播**：本实现搜索应答单播回源（复用 `channel_send`，符合 STD 通道语义）；
   源固件为广播 + 固定发对端 10028。若上位机只监听广播应答，可改用
   `app_udp_broadcast(data, len)`（10011 口广播、复用常驻 conn 不占池）——**待用户/联调裁决**。
3. **`0x40` 端口字段语义**：本实现写 Sector1 `net_cfg.port`（STD 口径 = TCP 业务口），
   源固件语义是该设备自身 UDP 口（10028）；若上位机按「改 UDP 口」使用，应改为写
   `udp_port` 或新增字段（doc/14 §8 Q5 / §11 已标注待裁决）。
4. **源地址快照单例**（§4-5）：多客户端并发时不能逐帧回源；根治需把 `src_ip/src_port`
   随帧入队（改 `frame_msg_t` 公共结构，影响全部协议，未做）。
5. **22_1703 口径内存账待重采**：另一路驱动改动（显存暂为 512B×2）定稿后，需重采
   doc/14 §6 与 doc/06-04 §7/§7.1 的 22_1703 两行（本轮已加 `[待更新]` 标记）。
6. **实机联调未做**：DIP1=OFF（9600）、UDP 发往 10011、抓包核对 6 个联调帧（doc/14 §10）
   与搜索应答语义；`0x10` 故障位、换行符字面量等既有待确认项不变（doc/14 §8）。

---

## 8. 证据索引

- 构建日志：`/tmp/gzol_udp/{B1_default,B_CQ,B_221703,B_221703CQ,B5_default_restore,B6_default_final}.log`
- 复核输入：`.analysis/9k23881580/stamp_verify_report.md`（已读，无冲突）
- 关键命令：`arm-none-eabi-size -A build/Debug/Project_STD.elf`；`arm-none-eabi-nm [-S]`；
  `md5sum`；`grep -E 'Compiling|Linking'`；`ps -ef | grep -E 'make|arm-none-eabi-gcc'`
- 关联文档：`doc/14_贵州治超屏协议/README.md`（§3/§4/§6/§11）、`doc/CLAUDE.md`（贵州治超小节）、
  `doc/05_协议模块多协议兼容优化/01_architecture.md`（§4）、`doc/06_.../04_current_memory_occupancy.md`（§7.1）、
  `doc/08_协议模块接入规则/{02,03}`
