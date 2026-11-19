# LDI 协议实现手册（完整版）

> **协议来源**：《交路网函〔2024〕86 号 附件 2》（同目录 `交路网函〔2024〕86号 附件2.txt`）。
> **实现来源**：本工程 `Application/Src/LDI/`（app_ldi.c / app_ldi_cmd.c / app_ldi_cfg.c / app_ldi_device.c）。
> **会话状态机权威**：同目录 `LDI会话状态机与命令门禁.md`（本文与其冲突时以代码为准并回改）。
> 生成日期：2026-11-19。

---

## 1. 角色与分层

| 角色 | 含义 | 连接方式 |
|---|---|---|
| 服务端（平台/站级） | 外设控制服务，下发配置与控制 | 接受设备拨入（TCP Server），或主动连设备 |
| 客户端（车道设备） | 本固件（费显/车道屏/雨棚灯等） | **主动**拨号 `host_ip:host_port`（TCP Client），同时自身监听 `device_ip:device_port`（TCP Server）供配置工具直连 |

- 设备拨号的平台地址只存在 W25 的 LDI 配置记录（单副本），由 0AH 写入。
- 设备自身 TCP Server（出厂 9528）用于产测/配置工具直连（TCP Client 模式）。

## 2. 帧格式

```
STX(2B=FF FF) | VER(1B=00) | SEQ(1B) | LEN(4B, 大端, 高2字节保留) | DATA | CRC16(2B)
```

| 字段 | 说明 |
|---|---|
| STX | 固定 `FF FF` |
| VER | `00` |
| SEQ | 服务端主动帧 0xH；客户端主动帧 X0H；应答回显请求 SEQ |
| LEN | DATA 域长度（**不含** STX/VER/SEQ/LEN/CRC） |
| CRC | CRC16/XMODEM：init `FFFF`、poly `8408`、结果按位取反；覆盖 **VER..DATA 末尾** |

**两种通用头**：

| 头类型 | 长度 | 布局 | 使用者 |
|---|---|---|---|
| 标准头 | 20B | CmdType(1) + UnixTime(4, 秒) + LaneHex(5) + CertInfo(8) + Reserved(2) | 除 1BH/B1H/1CH/C1H 外全部命令 |
| 控制查询头 | 24B | CmdType(1) + UnixTime(8, **毫秒**) + LaneHex(5) + CertInfo(8) + Reserved(2) | 1BH / B1H / 1CH / C1H |

LaneHex 结构：网络号(2B BCD) + 站点号(2B BCD) + 车道号(1B BCD)；全部为 0 = 未配置身份。

## 3. 命令总表

| 命令 | 名称 | 方向 | 应答 | 传输 |
|---|---|---|---|---|
| 0AH | 设备 IP 信息设置 | 平台→设备 | A0H | TCP |
| 0BH | 设备参数配置 | 平台→设备 | B0H | TCP |
| 0CH | 状态监控上报 | 设备→平台 | C0H（平台确认） | TCP |
| 0DH | 设备重启 | 平台→设备 | D0H | TCP |
| 0EH | 设备验证申请 | 设备→平台 | E0H | TCP |
| 0FH | 升级包下载请求 | 平台→设备 | F0H | TCP |
| 1AH | 设备初始化 | 平台→设备 | A1H | TCP |
| 1BH | 控制/查询 | 平台→设备 | B1H | TCP |
| 1CH | 功能数据上报 | 设备→平台 | C1H | TCP |
| 1DH | 设备 IP 信息采集 | 平台→设备 | D1H | TCP |
| 1EH | 设备参数采集 | 平台→设备 | E1H | TCP |
| 21H | 设备搜索 | 平台→设备 | 12H | **UDP 10011**（创迪扩展） |

## 4. 逐命令详解

### 4.1 0AH 设备 IP 信息设置 / A0H

**0AH DATA（85B）**：标准头(20) + ServerIP1(4) + ServerPort1(2) + ServerIP2(4) + ServerPort2(2)
+ ServerIP3(4) + ServerPort3(2) + MQTTIP(4) + MQTTPort(2) + MQTTUser(10) + MQTTPwd(10)
+ MQTTQOS(1) + NtpIP(4) + DeviceIP(4) + DevicePort(2) + Gateway(4) + Netmask(4) + Reserved(2)。

**A0H DATA（21B）**：标准头(20) + ErrorCode(1)。`00H` 成功，其他失败。

**实现要点**
- 设备 IP/端口/掩码/网关写 Sector1 `net_cfg`（**重启生效**）；ServerIP1/Port1 写 W25 `host_ip/host_port`。
- **身份零值守卫（成对判定）**：LaneHex 或 CertInfo **任一全 0** → 本帧不更新身份（防平台巡检空身份帧抹零，仍回 00H）；两者都非 0 才成对写入。
- host 两栏同理：全 0 → 不覆盖（host 为单副本，写 0 会导致设备永不连平台）。
- W25 保存或 Sector1 同步任一步失败 → A0H `01H`。

### 4.2 0BH 设备参数配置 / B0H

**0BH DATA**：标准头(20) + DeviceNum(1) + N × 模块条目（**变长**）+ 尾部。

**模块条目长度（关键契约）**

| 类型 | 长度 | 布局 |
|---|---|---|
| E4H / E5H / E6H | **13B** | type(1) + index(1) + param(1) + vendor(10) |
| E7H / E8H / EAH / EBH | **4B** | type(1) + index(1) + vendor(2) |
| E9H / F4H | **12B** | type(1) + index(1) + vendor(10) |

- E4H param = FilterTime；E6H param = FontLine（00H 自适应，01H~06H）；其余类型无 param。
- 固定 13B 步长解析会读出垃圾（台架已实证），必须按类型查表推进。

**B0H DATA（21B）**：标准头 + ErrorCode。

**实现要点**
- **0 是合法值**：lane/cert 成对整体写入，允许清空身份（清空后 E0H/probe 按"未配置"豁免）。
- 身份先提交 RAM（模块解析失败时不落盘但运行态已生效）；`DeviceNum=0`、`> module_count`、未知模块类型、帧长不足 → B0H `01H` 且不落盘。

### 4.3 0DH 设备重启 / D0H

0DH DATA：标准头 + DeviceNum(1) + N×模块（本固件忽略载荷，整机复位）。
D0H DATA（21B）：标准头 + ErrorCode(00H 接收成功)。
实现：先回应答，`osDelay(100)` 后整机软件复位。

### 4.4 1DH 设备 IP 信息采集 / D1H

1DH DATA：标准头（20B）。
D1H DATA（86B）：标准头 + ErrorCode(1) + ServerIP1(4) + ServerPort1(2) + ServerIP2/3（各 4+2）+ MQTT 段（4+2+10+10+1）+ NtpIP(4) + DeviceIP(4) + DevicePort(2) + Gateway(4) + Netmask(4) + Reserved(2)。
实现：读 Sector1 `net_cfg`（单一真源）；记录无效时回退 netif 当前值。

### 4.5 1EH 设备参数采集 / E1H

1EH DATA：标准头（20B）。
E1H DATA：标准头 + DeviceNum(1) + 模块条目（**与 0BH 同构，变长**）。
实现：按 `g_ldi.cfg.modules` 整段拼接，模块长度查同一张表（13/4/12B）。

### 4.6 0CH 状态监控上报（设备→平台）

DATA：标准头(20) + RunningTime(4, 秒) + RunningStatus(1) + ConnectStatus(1) + DeviceNum(1)
+ N ×（type(1)+index(1)+可用状态(1)+异常编码(1)+运行状态(1)+厂商码(2)+型号码(2)+日期 BCD(4)+版本序列号(1)+内存使用率(1)+温度(1)+S(1)+个性化 S 字节）。
实现：AUTHED/READY 每 **5 秒**发送一次到会话通道；未认证不发送。

### 4.7 0EH 设备验证申请 / E0H

0EH DATA：标准头 + DeviceNum(1) + N×(type(1)+index(1))。
E0H DATA（21B）：标准头 + ErrorCode（00H 认证成功）。
实现：UNINIT 每 **3 秒**发 0EH；收到 E0H 且身份匹配 → AUTHED + 锚定会话通道。
身份校验：本机身份已配置时严格比对（不一致 → **静默丢弃**，视为发给别的设备）；本机身份全 0（未配置）→ 豁免，新设备可完成首次认证。

### 4.8 0FH 升级包下载 / F0H

0FH DATA：标准头 + DeviceType(1) + DeviceIndex(1) + UpdateVersion(10)。
F0H DATA：标准头 + DeviceType(1) + DeviceIndex(1) + UpdateMD5(10) + UpdateFilesize(4) + UpdateFile(N)。
实现：本固件回复"不支持/失败"；固件升级走 IAP/Recovery 流程（见 doc/07）。

### 4.9 1AH 设备初始化 / A1H

1AH DATA：标准头(20) + DeviceNum(1) + N×(type(1)+index(1)+接口协议版本(2)+S(1)+个性化 S 字节)。
A1H DATA：标准头 + DeviceNum(1) + N×(type+index+状态(1)+软件版本(2)+固件版本(2)+接口版本(2)+S(1)+个性化)。
实现：AUTHED 状态下受理，成功后进入 READY；拒绝时回 A1H 且 DeviceNum=0。

### 4.10 1BH 控制/查询 / B1H

**请求**：控制查询头(24B) + DeviceNum(1) + N ×（模块帧长度 2B 大端 + 模块帧）。
**应答**：控制查询头 + DeviceNum(1) + N ×（模块帧长度 2B + 模块帧）。

模块帧（控制）按类型：

| 类型 | 功能 01H 载荷 | 功能 02H 载荷 |
|---|---|---|
| E4H 栏杆机 | status(1)：00H 落杆 / 01H 抬杆 / 02H 长抬 | — |
| E6H 显示屏 | FontColor(1)+FontSize(1)+FontLine(1)+KeepTime(1)+Text(N, GBK, `_`=换行) | ClearType(1)：00H 文字清屏/01H 全红/02H 全绿 |
| E7H 信号灯 | Color(1)：01H 绿/02H 红/03H 黄 | — |
| E8H 报警器 | Status(1)+WorkMode(1)+KeepTime(1) | — |
| E9H 情报板 | FontColor+FontSize+FontLine+KeepTime+**Format(1)**+Text(N) | ClearType(1) |
| EAH 雨棚灯 | Color(1) | — |
| EBH 雾灯 | Status(1)+WorkMode(1) | — |
| F4H 语音 | Type(1)：00H 文字/01H 序列 + Length(2B) + Data(N) | — |

应答模块帧：type(1)+index(1)+FuncType(1)+CtlStatus(1)（00H 成功/01H 失败）；
E4H 额外带 UnixTime1(8) + UnixTime2(8)（毫秒）。

实现：E6/E7/E8/E9 走设备层执行并镜像到四川 ETC 帧（doc/20）；EA/EB/F4 当前为占位（不执行、按协议回状态），F4H 适配见专门任务。

### 4.11 1CH / C1H

结构同 1BH，方向相反（设备上报功能数据 / 平台确认）。本固件未主动发送 1CH；收到 1CH 静默处理。

### 4.12 21H / 12H（创迪扩展，UDP 10011）

21H DATA：CmdType(1)。
12H DATA：CmdType(1)+Ip(4)+Port(2)+Gateway(4)+Mask(4)+ErrorCode(1)。
实现：UDP 10011 应答（广播为主，回源单播容错）；应答前按 Sector1 刷新运行态网络字段。

## 5. 模块类型编码

| 编码 | 模块 | 0BH 条目 | 1BH 实现 |
|---|---|---|---|
| E1H | RSU 路侧单元 | 协议定义（未在本固件注册） | 未实现 |
| E2H | 高清车牌识别 | 同上 | 未实现 |
| E3H | 车型识别 | 同上 | 未实现 |
| E4H | 电动栏杆机 | 13B | 落杆/抬杆 |
| E5H | 车辆检测器 | 13B | 未实现（协议为上报类） |
| E6H | 信息显示屏 | 13B | 显示/清屏 ✅ |
| E7H | 通行信号灯 | 4B | 绿/红/黄 ✅ |
| E8H | 报警器 | 4B | 开/关/模式/保持 ✅ |
| E9H | LED 情报板 | 12B | 显示/清屏 ✅ |
| EAH | 雨棚信号灯 | 4B | 占位 |
| EBH | 雾灯 | 4B | 占位 |
| F4H | 语音播报 | 12B | 占位（独立任务适配中） |

## 6. 固件实现差异与裁决（2026-11-19）

| 项 | 协议原文 | 本固件裁决 |
|---|---|---|
| 身份 0 值 | 未定义 | **0AH**：成对守卫，任一全 0 → 整帧不下发身份（回 00H）；**0BH**：0 合法，可清空身份 |
| host 0 值 | 未定义 | 0AH 全 0 → 不覆盖（host 单副本，防永不连平台） |
| 模块条目长度 | 变长（按类型） | 严格按表推进：E4/E5/E6=13、E7/E8/EA/EB=4、E9/F4=12 |
| 应答状态码 | 00/其他 | 0AH/0BH 落盘失败回 01H；门禁拒绝的 1AH/1BH 回 A1H/B1H 且 DeviceNum=0 |
| 断链 | 未定义 | 会话锚点 + 边沿复位：锚定通道销毁/换代/掉线 → 回 UNINIT 重发 0EH |
| 发送阻塞 | 未定义 | 独立 TX 任务 + 队列（平台零窗口不阻塞处理/定时任务） |
| 1CH/0FH | 定义 | 本固件不主动发 1CH；0FH 回不支持 |
| **B1H 模块条目** | 协议 6.1.2：每模块前有 2 字节长度 | **本固件实际：无长度前缀**，条目固定 4B（type/index/func/status），B1H = 24B 头 + DeviceNum + N×4B（台架字节级实证 2026-11-19） |

## 7. 校验与示例

- CRC16/XMODEM 源码见协议附录 D；本工程实现 `Kernel/Src/crc_utils.c: crc16_xmodem()`。
- 示例帧（协议原文）：
  - 0AH：`FF FF 00 10 00 00 00 55 0A 69 04 54 95 20 13 14 52 00 01 23 45 67 89 AB CD EF 00 00 C0 A8 00 64 27 2C ... 1C 48`
  - 0BH：`FF FF 00 20 00 00 00 36 0B ... 04 E6 01 04 <vendor10> E7 01 00 00 E8 01 00 00 F4 01 <vendor10> 83 CF`
  - 1BH 显示：`FF FF 00 07 00 00 00 25 1B ... 01 00 0A E6 01 01 01 00 00 00 31 32 33 59 8D`

## 8. 待确认项

1. E4H 0BH 条目 param 语义（FilterTime）设备端是否需要执行。
2. 1CH 上报确认（C1H）是否需要本固件实现主动上报。
3. F4H Type=01H（语音序列）本板语音板不支持，需确认回退策略（回 01H 失败或忽略）。
4. 0FH 是否需接入 Recovery 升级流程（当前走 IAP）。
