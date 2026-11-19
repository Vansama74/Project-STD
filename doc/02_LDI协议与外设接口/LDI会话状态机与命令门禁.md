# LDI 会话状态机与命令门禁

> **本文件是 `doc/02_LDI协议与外设接口/` 下 LDI 会话状态机的权威描述。**
> 其他文档（`doc/CLAUDE.md`、`doc/08_协议模块接入规则/`）涉及 LDI 状态、门禁、
> 断链复位的表述一律以本文为准；本文与代码不一致时以代码为准并回改本文。
>
> 最近一次代码对齐：2026-11-18（断链边沿复位 + 会话锚点 + E0H 身份校验 +
> 门禁拒绝回错误应答 + 0AH host 零值守卫，四项同日落地并完成板上回归）。

---

## 1. 三个状态

| 状态 | 含义 | 进入条件 |
|------|------|----------|
| `LDI_ST_UNINIT` | 未认证 | 上电初值；断链复位回退点；E0H 认证失败 |
| `LDI_ST_AUTHED` | 已认证，等待初始化 | 收到 E0H 且 `error_code == 0x00`（身份校验通过） |
| `LDI_ST_READY` | 初始化完成，正常工作 | 1AH 初始化请求处理成功（各模块自检全通过） |

状态只有一个写者语义：**E0H 推 UNINIT→AUTHED**（READY 态收到 E0H 直接忽略——迟到/重复的
认证应答不得把已初始化的设备打回 AUTHED）、**1AH 推 AUTHED→READY**、
**断链边沿推任意状态→UNINIT**。不存在 AUTHED/READY 之间的其他跳转。

## 2. 状态 × 命令矩阵

「配置类」= 出厂配置/巡检接口，**不受状态限制，任何状态都执行**：
`0AH`(IP 设置)、`0BH`(参数配置)、`0DH`(重启)、`1DH`(IP 采集)、
`1EH`(参数采集)、`21H`(搜索请求，UDP 广播)。

| 命令 | 方向 | UNINIT | AUTHED | READY |
|------|------|--------|--------|-------|
| 0AH / 0BH / 0DH / 1DH / 1EH / 21H | 平台→设备（配置） | ✅ | ✅ | ✅ |
| E0H 设备验证应答 | 平台→设备（对 0EH 的应答） | ✅（→AUTHED） | ✅（重锚） | **忽略**（保持 READY） |
| 1AH 设备初始化请求 | 平台→设备 | ❌ 回 A1H(0) | ✅ | ✅ |
| 1BH 控制/查询请求 | 平台→设备 | ❌ 回 B1H(0) | ❌ 回 B1H(0) | ✅ |
| 0EH 设备验证申请 | 设备→平台 | 设备主动发 | — | — |
| 0CH 状态监控上报 | 设备→平台 | — | 设备主动发 | 设备主动发 |
| 1CH 功能数据上报 | 设备→平台 | — | 运行中 | 运行中 |
| C0H 上报应答 / C1H 上报确认 | 平台→设备（对上报的应答） | 静默 | 静默 | 静默 |
| 0FH 升级包下载 | — | 本固件不处理（IAP 独立） | — | — |

> 实现：`ldi_cmd_allowed()`（`app_ldi.c:313`）只做「配置类直接放行 + 三状态白名单」，
> 不区分通道；通道维度由 probe 的协议绑定决定（LDI 绑 TCP Server / TCP Client / UDP）。

### 2.1 被拒命令的应答（2026-11-18 起）

门禁拒绝**不再静默丢帧**——平台按 `device_num` 解析 1AH/1BH 应答，无回应会被判超时。

| 被拒命令 | 应答 | 载荷 | 实现 |
|----------|------|------|------|
| 1AH | `A1H` | 通用头(20B) + `device_num = 0`（无模块） | `_ldi_init_error_rsp()`（`app_ldi_cmd.c:386`） |
| 1BH | `B1H` | 控制头(24B) + `device_num = 0`（无模块） | `_ldi_ctrl_error_rsp()`（`app_ldi_cmd.c:373`） |
| 0EH / 0CH / 1CH / C0H / E0H | **静默** | — | 协议无此方向应答，回了会与对端成环 |

**不能**用 `_ldi_error_rsp()`（回 head + status 1B）当 1AH/1BH 的拒绝应答——那会让上位机
按 `device_num` 解析时错位。1AH 的错误应答必须与 `cmd_init` 成功路径同构（只把
`device_num` 置 0）。分发入口：`ldi_handle_task()` 在门禁 false 时调用
`ldi_gate_reject_rsp(ch, cmd)`（`app_ldi_cmd.c:404`），发送前判 `ch != nullptr && ch->ops != nullptr`。

## 3. 周期发送

| 帧 | 触发条件 | 周期 | 发送通道 |
|----|----------|------|----------|
| 0EH 设备验证申请 | `state == UNINIT` | 3 秒（`last_cert_tick`） | **会话锚点优先**，无锚点回退 `CH_ID_TCP_CLIENT` |
| 0CH 状态监控上报 | `state == AUTHED` 或 `READY` | 5 秒（`last_rpt_tick`） | **会话通道**（`sess_ch_id` 现查） |

- 两者都在 `ldi_timer_task`（1 秒节拍，`app_ldi.c:568`）里按 tick 差值触发，不是精确周期。
- 发送前要求目标通道 `state == CH_STATE_UP`，否则本拍跳过（不累计、不补发）。
- 0EH **只在 UNINIT 发**：认证成功后立即停发；认证失败（E0H `error_code != 0`）保持
  UNINIT，3 秒后继续重发，直至成功。
- ETC 镜像心跳（`0A 08 0D`，3 秒）与 LDI 状态机**无关**，无条件发送，不受本文约束。

## 4. 会话锚点与断链边沿复位

### 4.1 锚点字段（`ldi_ctx_t`，`app_ldi.h`）

```c
uint8_t sess_ch_id;        // 完成认证的通道 ID（CH_ID_*），0 = 无会话
const channel_t *sess_ch;  // 会话通道实例地址（仅比较/判空，不解引用）
uint32_t sess_gen;         // 锚定时的通道注册代次（app_channel_link_gen 现查）
```

认证成功（E0H `error_code == 0x00` 且 `ch != nullptr`）时同时写入 `sess_ch`、
`sess_ch_id` 与 `sess_gen`。前两字段只用于比较与判空，`sess_gen` 用于识别「同一
静态实例被复用重连」——TCP Client/Server 的连接实例都是文件级 static，重连时
实例地址不变、`app_channel_register` 的注册代次必变。真正发送前一律用
`sess_ch_id` 现查 `app_channel_get()`，避免在连接任务销毁通道后持裸指针。

### 4.2 复位语义：边沿触发，只执行一次

`ldi_timer_task` 每秒取当前会话通道：

```
cur = (sess_ch_id != 0) ? app_channel_get(sess_ch_id) : app_channel_get(CH_ID_TCP_CLIENT)
```

先做**非法态自愈**：`sess_ch == nullptr` 但 `state != UNINIT`（1AH 置 READY 与
断链复位竞态的历史残留，现已被 `cmd_init` 复检挡住）→ 直接拉回 UNINIT 并清
`last_cert_tick/last_rpt_tick`。随后命中以下任一条件即**复位一次**：

- `sess_ch != nullptr` 且 `cur != sess_ch`（通道实例被替换），或
- `cur == nullptr`（通道已注销），或
- `cur->state != CH_STATE_UP`（连接断开），或
- `app_channel_link_gen(sess_ch_id) != sess_gen`（同一实例被复用重连）。

复位动作：`state = UNINIT; sess_ch = nullptr; sess_ch_id = 0;
last_cert_tick = last_rpt_tick = 0;`，随后本拍 `continue`。

另：`cmd_init` 置 READY 前必须复检 `state == AUTHED && sess_ch == ch &&
注册代次一致`，不复检会出现「READY 且无锚点」的永久非法态（永不复位、0CH 退化
为 Client 通道）。复检不过时保持原状态，3 秒内重发 0EH 重新认证（fail-closed）。

三点必须理解清楚：

1. **没有锚点时不拉平状态**。从未认证成功的设备（`sess_ch == nullptr`）不会被定时任务
   强行置 UNINIT——旧实现「TCP Client 不 UP 就无条件回 UNINIT」会让只连 TCP Server 的
   平台永远认证不上（认证成功又被 1 秒后的定时任务打回）。
2. **边沿而非电平**：复位后锚点清零，下一拍不再命中，因此不会每秒重复复位（不刷
   `last_cert_tick`、不影响 3 秒节拍）。
3. **复位后认证申请重新开始**：`last_cert_tick = 0` 使下一拍立即具备发送条件（3 秒窗口
   从复位时刻重新计），平台重连后最迟 3 秒内收到 0EH。

### 4.3 与「通道生命周期」的关系

TCP 连接任务断开时置 `ops = nullptr`、`state = DOWN` 并 `app_channel_register(..., nullptr)`，
因此 `app_channel_get()` 可能返回 `nullptr` 或新实例。锚点用**实例地址比较**正是为了识别
「同一 ID 换了实例」这一情形；`channel_send()` 自身另有 `ops == nullptr` 与
「实例必须仍注册在案」双重守卫，两层防护互不替代。

## 5. E0H 身份校验

| 本机身份（`lane_hex` + `cert`） | E0H 携带身份 | 结果 |
|---|---|---|
| 已配置（非全 0） | 与本机一致 | 接受：`error_code == 0` → AUTHED + 锚定会话；`!= 0` → 保持 UNINIT |
| 已配置 | 与本机不一致 | **直接丢弃**：不改状态、不回包（视为发给别的设备的应答） |
| 未配置（全 0） | 任意 | **豁免**，按上一行规则处理（否则新设备永远无法完成首次认证） |

- 比较字段：帧头 `lane_code`（DATA 偏移 5，5B）与 `cert_info`（DATA 偏移 10，8B）。
- 判定函数 `ldi_identity_unconfigured()`（`app_ldi.c:438`，导出）与 probe 的身份闸门
  **共用同一口径**（全 0 即未配置），避免两处判定漂移。
- 注意分层：probe 在收帧阶段已对非配置类命令做过一次身份比对（不一致 → `PROTO_PROBE_SKIP`），
  `cmd_rsp_cert` 内的比对是**第二层**——防的是身份在 probe 通过之后、命令执行之前被改写，
  以及未来新增通道绕过 probe 的情形。

## 6. host 字段的存储归属与 0.0.0.0 成因

| 字段 | 存储位置 | 出厂默认 | 说明 |
|------|----------|----------|------|
| `host_ip` / `host_port` | **仅 W25 LDI 配置记录（单副本）** | 无 | Sector1 `net_cfg` **不存** host；擦 Sector1 不会恢复 host，也不会破坏 host |
| `device_ip` / `device_port` / `gateway` / `netmask` | Sector1 `net_cfg`（真源）+ W25（镜像） | 有（`pl_net_get_ip`） | 见 `doc/07…/02_decoupling_solutions.md` §12 |

`0.0.0.0:0` 的成因：host 只在平台下发 0AH 时写入。若 0AH 无条件下发（或平台巡检帧
不带这两栏），旧实现会把 host **覆盖成 0** 并落盘到唯一副本——设备重启后
`app_tcp_client_set_remote(0.0.0.0, 0)` 使 TCP Client 永远 idle（「远端未配置不连」），
**永不连平台**，且无出厂默认可回退。

**0AH host 零值守卫（2026-11-18）**：`host_ip` 全 0 → 不覆盖 `cfg.host_ip`；
`host_port == 0` → 不覆盖 `cfg.host_port`（`app_ldi_cmd.c:484` 起，与 lane/cert 守卫同款）。
副作用：host 一旦写入就**无法再用 0AH 清成 0**——这是有意的（0 语义 = 「本次不下发该字段」，
不是「改成 0.0.0.0」）。确需清零只能整片擦除 W25 配置记录。

**身份字段零值语义（2026-11-19 用户裁决，取代 11-18 的逐字段守卫）**：

| 命令 | 语义 | 行为 |
|---|---|---|
| **0AH** | 0 = 本次不下发该字段（保护平台巡检帧） | lane/cert **成对判定**：任一全 0 → 整帧不更新身份（杜绝「旧 lane + 新 cert」混合身份）；网络字段照常写入 |
| **0BH** | **0 = 合法值**（显式设备参数配置，工厂/产测工具） | lane/cert 成对**整体写入**，允许清成全 0（未配置态；此后 E0H/probe 身份闸门按「未配置」豁免）；模块解析失败时 RAM 身份已生效但不落盘 |

返回值：0AH 因巡检帧常态**被忽略仍回 00H**（不视为失败）；0BH 写入失败（模块解析/落盘）回 01H。

## 7. 继承自历史归档的有效结论

以下四条来自 `LDI协议初始化与模块注册修复归档.md`（该文件已于 2026-11-18 删除，结论并入本文）：

1. **变长协议解析必须严格推进指针**：1AH/1BH/0BH 的 module 序列按 `device_type` 查表定长
   推进，推进错位会静默把后续 module 解析成垃圾。
2. **缓冲区尺寸必须与实际模块数一致**：`LDI_TX_BUF_SIZE`(512) 与 `LDI_PAYLOAD_MAX`(512)
   是硬上限，模块数增长必须先核算响应帧长度。
3. **注册表必须与上位机期望的模块集合一致**：`g_ldi.cfg.module_count` 与实际 `modules[]`
   不符会导致初始化状态无法进入 READY。
4. **状态卡在 AUTHED 的排障顺序**：先查 1AH 是否到达并被受理（门禁是否放行）→ 再查
   模块注册表与模块数 → 最后查变长解析指针推进。

## 8. 板上回归基线（2026-11-18，Debug 构建）

| # | 用例 | 预期 | 实测 |
|---|------|------|------|
| a | 未认证发 1BH（身份正确） | `B1H`，`device_num = 0` | ✅ 25B 应答，`device_num=0` |
| b | 未认证发 1AH（身份正确） | `A1H`，`device_num = 0` | ✅ 21B 应答，`device_num=0` |
| c | 错身份 E0H(00) 后发 1AH | 状态不变，1AH 仍被拒 | ✅ 仅 1 帧 `A1H device_num=0`，E0H 无应答 |
| d | 正确身份 E0H(00) → 1AH → 1BH | `A1H`/`B1H` 均 `device_num = 1`；5 秒后收到 0CH | ✅ 三帧齐全，0CH 发到会话通道 |
| e | 0AH 带全 0 host | 应答 `A0H/00`，`1DH` 读回 host 不变 | ✅ host 保持 `192.168.114.127:9529` |
| f | 认证后断链 → 重连发 1BH | 回到未认证，`B1H device_num = 0` | ✅ 25B 应答，`device_num=0` |

复现脚本：`%TEMP%\cursor-agent\ldi-repro-20261118\`（`ldi_tool.py` + `gate_probe.py`，
仓外临时设施，不入库）。

## 9. 代码位置

| 关注点 | 位置 |
|--------|------|
| 状态枚举 / 上下文（含锚点） | `Application/Inc/LDI/app_ldi.h`（`ldi_state_t` / `ldi_ctx_t`） |
| 状态门禁 | `Application/Src/LDI/app_ldi.c:313` `ldi_cmd_allowed()` |
| 门禁拒绝分发 | `Application/Src/LDI/app_ldi.c:416` → `ldi_gate_reject_rsp()` |
| 定时任务（周期发送 + 断链复位） | `Application/Src/LDI/app_ldi.c:568` `ldi_timer_task()` |
| 身份未配置判定 | `Application/Src/LDI/app_ldi.c:438` `ldi_identity_unconfigured()` |
| E0H 处理（校验 + 锚定） | `Application/Src/LDI/app_ldi_cmd.c:833` `cmd_rsp_cert()` |
| 1AH/1BH 拒绝应答 | `Application/Src/LDI/app_ldi_cmd.c:373/386/404` |
| 0AH host 零值守卫 | `Application/Src/LDI/app_ldi_cmd.c:484` `cmd_set_ip()` |

## 10. 模块记录 13B 布局与 v1/v2/v3 迁移（2026-11-18）

### 10.1 模块记录 = 13B，param 单独成字段

0BH/1EH 的 E4H（栏杆）/E6H（信息屏）模块在协议里是 **13B**：
`type(1) + index(1) + param(1) + vendor(10)`（附件2 §4.2/§4.5）。
`param` 语义：E4H = FilterTime（杂波过滤时间 ms）、E6H = FontLine（默认显示行数
01H~06H，00H=自适应）；其余类型无 param 字节。

W25 记录里的模块条目 `app_flash_ldi_module_cfg_t` 自 2026-11-18 起同为 **13B**
（`device_type + device_index + param + vendor[10]`）。改错静默错位——13B 是契约。

**修复前（P1-a）**：条目只有 12B（无 param 字段），0BH 把 13B 请求的
`vendor_len = mod_size - 2 = 11` 夹到 10，从 `ptr+2` 整段拷入 → `param` 混进
`vendor[0]`、真实 `vendor[9]` 永久丢失；1EH 又按 12B 拼，平台从第 2 个模块起错位。
**修复后**：0BH 按 `mod_size >= 13` 判定含 param，`param` 单独存、vendor 从 param
之后拷（满 10B）；1EH 按 `ldi_cfg_module_size()` 的定长整段拼接，应答总长恒
Σ mod_size。

### 10.2 1EH 应答定长表

| 设备类型 | 模块长度 | 结构 |
|---|---|---|
| E4H 栏杆 / E6H 信息屏 | **13B** | type + index + param + vendor[10] |
| E7H 通行信号灯 / E8H 报警器 / EAH 雨棚灯 / EBH 雾灯 | **4B** | type + index + vendor[2] |
| E9H 情报板 / F4H 语音（VOICE，查表同 VMS） | **12B** | type + index + vendor[10] |

E1H 帧长 = `21 + Σ mod_size`（head 20 + device_num 1 + 模块段）。

### 10.3 三级布局识别与迁移链（W25 槽区，无版本号）

| 布局 | 模块数 × 长度 | cfg | record | 说明 |
|---|---|---|---|---|
| v1（2026-11-17 前） | 6 × 12B | 106B | 116B | 只读迁移 |
| v2（2026-11-17 ~ 11-18） | 11 × 12B | 166B | 176B | 只读迁移；E4/E6 的 `vendor[0]` 实为 param |
| **v3（现行）** | **11 × 13B** | **178B** | **188B** | 唯一可写布局（≤256B 槽 ✓） |

装载 `app_flash_ldi_load_config()` 倒序扫描 16 个槽，逐级 CRC 试探 **v3 → v2 → v1**：
- v3 命中：直接返回。
- v2 命中：`_migrate_v2_to_cur()` 迁移——前部网络/身份字段同偏移直接搬；modules
  逐项转换，**E4H/E6H 右移还原**（`param = 旧vendor[0]`、`vendor[i] = 旧vendor[i+1]`、
  `vendor[9] = 0`），其余类型原样搬 + `param = 0`；随后立即 `app_flash_ldi_save_config()`
  以 v3 追加写回（旧记录不破坏）。右移依据经 git 复核：0BH 从最早的
  `3cf8283` 到 `dd59cb6`（引入 `vendor_len` 的唯一提交）一直是
  `mod_size - 2` 夹 10B 的混排写法，v1 时代记录同样混排。
- v1 命中：前部字段搬运 + modules 显式逐字段（`param = 0`、vendor 原样）+ 写回 v3。

### 10.4 P1-b：上电恢复 param/vendor

`ldi_ctx_init()` 分支 1（Sector1 有效）与分支 2（W25 有效）的模块同步循环，
修复前只拷 `device_index`，0BH 配的 param/vendor 重启即丢；现同步
`device_index + param + vendor[10]` 三项。

### 10.5 P2：1AH 错误应答统一 A1H 形状

`cmd_init()` 4 条畸形帧错误路径（帧长不足 / `device_num > 13` / 模块长度不足 /
`custom_init` 超长）原回旧格式 `_ldi_error_rsp`（head 20B + status 1B，第 21 字节
= 01H 会被平台读成 `device_num=1`）；现全部改用 `_ldi_init_error_rsp()`
（A1H + `device_num = 0`），与门禁拒绝路径同构。

### 10.6 板上回归（2026-11-18，Debug）

| # | 用例 | 预期 | 实测 |
|---|---|---|---|
| a | 旧固件 0BH 写 E6 param=03 + vendor A1..AA | — | ✅ 1EH 读回 `e6 01 03 A1..A9`（AA 丢失，缺陷复现） |
| b | 烧新固件后首次 1EH（v2→v3 迁移） | E6 = `e6 01 03 A1..A9 00`，总长 54B | ✅ 逐字节符合，E7/E8=4B、F4=12B |
| c | 新固件 0BH 写 E6 param=05 + vendor C1..CA | B0H=00 | ✅ 21B，status=00 |
| d | 紧随 1EH | E6 = `e6 05 05 C1..CA`（10B 全保留） | ✅ 总长 54B，`exact_len=True` |
| e | 0DH 重启后再 1EH（P1-b） | param/vendor 与下发一致 | ✅ `e6 05 05 C1..CA` 原样恢复 |
| f | UNINIT 发 1AH 短帧（门禁） | A1H，device_num=0 | ✅ 21B，第 21 字节 00 |
| g | E0H 认证后发 1AH 短帧（P2 路径） | A1H，device_num=0 | ✅ 21B，第 21 字节 00（修复前为 01） |
| h | AUTHED 发 1AH `device_num=20`（P2 路径） | A1H，device_num=0 | ✅ 21B，第 21 字节 00 |
| i | 合法 1AH（1 个 E6 模块）回归 | A1H，device_num=1，31B | ✅ `a1 … 01 e6 01 0000 01 00 …` |