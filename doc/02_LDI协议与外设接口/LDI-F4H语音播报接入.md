# LDI F4H 语音播报接入（1BH 控制请求）

> 规范：`交路网函〔2024〕86号 附件2.txt` §25「语音播报设备接口」
> 落地日期：2026-11-19　范围：LDI 0x1B 控制请求的 F4H 分支 + `dev_rs232_voice` 发送链路

## 1. 背景

`cmd_ctrl()` 的 F4H 分支长期是空实现（`(void)ctrl; // TODO: dev_voice_ctrl(ctrl)`）。
更要紧的是：F4H 在默认配置里就是已注册模块（`app_ldi.c` 的 `g_ldi.cfg.modules`
含 E6/E7/E8/F4，`module_count = 4`），而 1BH 的逐模块状态是在进 switch 之前按
「类型是否在注册表」预置的：

```c
rsp->modules[i].status = found ? 0x00 : 0x01;
```

空实现不写状态 ⇒ F4H 恒回 `CtlStatus = 0x00`（成功）。上位机看到成功、语音板
一声不响，故障被完全掩盖。本次按「**只有数据真的经语音板发出才算成功**」的口径
补齐该分支。

## 2. 协议要求（附件2 §25.1 播放提示音 01H）

请求的 module 段（`mod_len` 从 DeviceType 起的全部字节）：

| 偏移 | 字节 | 字段 | 取值 |
|---|---|---|---|
| 0 | 1 | DeviceType | 固定 `F4H` |
| 1 | 1 | DeviceIndex | 模块序号，从 `01H` 起 |
| 2 | 1 | DeviceFuncType | 功能编号，本命令固定 `01H` |
| 3 | 1 | Type | `00H` 按文字播报；`01H` 按指定语音序列；`02H~FFH` 其他语音格式 |
| 4 | 2 | Length | 语音内容字节长度 N，最大 `FFFFH`，**大端** |
| 6 | N | Data | 语音内容，**GBK 编码** |

应答（B1H）按规范 §6.1.2 逐模块带 2 字节大端长度前缀，模块响应帧固定 4 字节：
`00 04` + DeviceType(`F4H`) + DeviceIndex + DeviceFuncType(`01H`) + CtlStatus(`00H` 成功 / `01H` 失败)。

**本实现只支持 `Type = 00H`（按文字播报）**：本板语音板是 RS232 TTS，按 GBK 文本
合成，无法按「语音序列号」取段，也没有其他语音格式的编解码能力。

## 3. as-built

### 3.1 判定顺序（`Application/Src/LDI/app_ldi_cmd.c` `cmd_ctrl()` 的 F4H 分支）

`fixed = sizeof(ldi_module_head_t) + sizeof(ldi_ctrl_voice_t)` = 2 + 4 = **6**；
`mod_len` 是 module 段总长（含 DeviceType/DeviceIndex），`avail = mod_len - fixed`
即 Data 实际携带的字节数。

| # | 判定 | 不通过时 |
|---|---|---|
| a | `fixed = 6` | — |
| b | `mod_len >= fixed + 1`（至少 1 字节 Data） | `CtlStatus = 01H` |
| c | `DeviceFuncType == 01H` | `CtlStatus = 01H` |
| d | `Type == 00H`（按文字播报） | `CtlStatus = 01H` |
| e | `text_len = Length[0]<<8 \| Length[1]`；`avail = mod_len - fixed` | — |
| f | `text_len != 0` 且 `text_len <= DEV_RS232_VOICE_MAX_TEXT(200)` 且 `text_len == avail` | `CtlStatus = 01H` |
| g | `ldi_device_voice_ctrl(ctrl->data, text_len)` 返回 true（整帧确实发出） | `CtlStatus = 01H` |

**成功 = 数据真的经 `dev_rs232_voice` 发出**：`CtlStatus = 00H` 只在 a~g 全过、
且 `pl_uart_send` 把整帧送完（返回值等于帧长）时出现。

Length 与帧内实际字节数**必须严格相等**：多截少补都不做，避免「播报内容与上位机
以为的不一致」。

### 3.2 失败场景清单

- module 段短于 7 字节（装不下 Type+Length+1 字节 Data）；
- `DeviceFuncType != 01H`（本设备 F4H 只有 01H 一个功能）；
- `Type = 01H`（语音序列）/ `02H~FFH`（其他格式）——协议允许、本硬件不支持，**不发任何东西**；
- `Length = 0`；
- `Length > 200`（`DEV_RS232_VOICE_MAX_TEXT`，语音板单帧缓冲上限）；
- `Length` 与 `avail` 不一致（帧数据被截断或多出垃圾字节）；
- 语音板发送失败/超时（`pl_uart_send` 返回 -1）。

另外，若 F4H 不在 `g_ldi.cfg.modules`（编译期默认表，见 `app_ldi.c`；0BH 只同步
index/param/vendor，不增删模块），进 switch 之前状态就已置 `01H`，与本分支无关。

### 3.3 涉及文件

| 文件 | 内容 |
|---|---|
| `Application/Src/LDI/app_ldi_cmd.c` | `cmd_ctrl()` 的 `case LDI_DEV_TYPE_VOICE` 分支（判定 a~g）+ 新增 `#include "dev_rs232_voice.h"` |
| `Application/Src/LDI/app_ldi_device.c` / `Application/Inc/LDI/app_ldi_device.h` | `bool ldi_device_voice_ctrl(const uint8_t *gbk_text, uint16_t text_len)`——薄封装，直通 `dev_rs232_voice_play()`，与 E6/E7/E8 同层 |
| `Device/Comm/dev_rs232_voice.c` / `Device/Inc/dev_rs232_voice.h` | `dev_rs232_voice_play()` / `dev_rs232_voice_volume()` 改返回 `bool`；发送超时按帧长推导（见 §4） |

## 4. 驱动修正：固定 100ms 超时 → 按帧长推导

**原缺陷**：`dev_rs232_voice_send()` 固定 `pl_uart_send(uart, frame, len, 100U)` 并丢弃
返回值。语音口 USART6 为 **9600 8N1**，约 **1.04 ms/字节**，100 ms 只够发约 96 字节；
超过该长度的播报文本会在**中途被中止**（`pl_uart_send` 返回 -1），上位机侧毫无察觉。

**修正**：

```c
uint32_t timeout_ms = (uint32_t)len * 2U + 50U;   /* ≈ 1.9 倍余量 + 50ms 启动裕度 */
return pl_uart_send(uart, frame, len, timeout_ms) == (int32_t)len;
```

- 最长帧 = 200 + 5 = 205 字节 ⇒ 超时 460ms 封顶，仍远小于任何协议侧超时；
- 发送是阻塞式的（`pl_uart_send` 中断驱动、等待期间让出 CPU）：B1H 在该帧发完后才入队，
  最长文本 ≈ 215ms（205B × 1.04ms）——期间其他任务照常运行，只是 LDI 任务被串行化；
- `dev_rs232_voice_play`：空指针 / `len == 0` / `len > 200` / `payload_len > 0xFF` / 发送失败 → `false`；
- `dev_rs232_voice_volume`：发送成功 → `true`，否则 `false`；
- **影响面**：8 个省级协议（青海/重庆/贵州/云南/四川 MTC/安徽/广西/新疆）调用的
  `dev_rs232_voice_play` 是同一入口，长文本被截断的问题一并消除。这些调用点都忽略
  返回值，签名从 `void` 改为 `bool` **不产生任何改动**。

## 5. 明确不改动

- **0CH 状态上报**：F4H 没有定义「运行状态」语义，`ldi_device_fill_sta()` 不处理 F4H
  （`running_status` 保持默认 `00H`）；本次不新增 F4H 运行状态缓存。F4H 在 0CH 里的
  **可用状态**（`available_status` / `error_code`）由模块注册表派生，见
  `LDI会话状态机与命令门禁.md` §11。
- **E5/EA/EB「假成功 00H」：已不成立（2026-11-19 复核）**。车检/雨棚灯/雾灯
  仍是 TODO 空操作，且**没有登记**进 `LDI_MODULE_LIST`（`Application/Inc/LDI/app_ldi.h`）
  ⇒ 1BH 在进 switch 之前就按「类型不在注册表」把 `CtlStatus` 预置为 `01H`，回的是
  **失败**，不是成功。**E4 栏杆机按项目决策不实现**（2026-11-19 删除其空操作分支与
  payload 结构体）：同样未注册，恒回 `01H`，无硬件动作。
- **E9H 情报板：同样未登记**。它虽然有 switch 分支，但只在「已登记」时才调 `vms_ctrl()`；
  未登记一律不改屏并回 `01H`——否则会出现「屏变了但回失败、主机重试反复刷屏」。
- **音量**：附件2 §25 只有「播放提示音（01H）」一个子命令，没有音量子命令；
  LDI 侧不引入音量控制（`dev_rs232_voice_volume` 仍供省级协议使用）。
- **E6/E7/E8 分支、0CH 帧结构、门禁与应答构造**：F4H 接入轮一行未改；同日稍后的
  模块门禁/状态上报修正（X-macro 注册表、1AH READY 规则、0CH 逐字段口径）见
  `LDI会话状态机与命令门禁.md` §11。

## 6. 测试

### 6.1 帧头约定（从结构体实测）

`ldi_frame_t`：`STX(2B=FF FF) | VER(1B=00) | SEQ(1B) | LEN(4B 大端，高 2 字节保留=0) |
DATA | CRC(2B 大端)`。

- **CRC** = `crc16_xmodem(&ver, 6 + data_len)`，即覆盖 **VER ~ DATA 末字节**（不含 STX，
  不含 CRC 自身）；算法 `Kernel/Src/crc_utils.c`：init `0xFFFF`、反射多项式 `0x8408`、
  末取反。
- 1BH 的 DATA = `ldi_ctrl_head_t(24B) | device_num(1B) | N × [mod_len(2B 大端) | module 段]`，
  `ldi_ctrl_head_t` = CmdType(`1B`) + UnixTime(`8B`) + LaneHex(`5B`) + CertInfo(`8B`) + Reserve(`2B`)。
- 1BH 的 LaneHex 偏移 9、CertInfo 偏移 14（相对 DATA）。

### 6.2 前置条件（否则设备不认帧或不应答）

1. **身份**：设备侧 `g_ldi.cfg_valid` 为真且 lane/cert 不全为 0 时，帧内 `LaneHex` /
   `CertInfo` **必须与设备一致**，否则 `ldi_probe_frame()` 判 `PROTO_PROBE_SKIP`，
   **整帧被丢弃、连应答都没有**。身份全 0（从未被 0AH/0BH 配置过）时跳过比对。
   下面两帧用的是附件2 的示范值（Lane `20 13 14 52 00`、Cert `01 23 45 67 89 AB CD EF`），
   **不是通用值**——现场请替换为设备实际身份；未配置身份的设备可直接发送。
2. **会话状态**：1BH 只在 **READY** 状态被处理（`ldi_cmd_allowed()`）。
   未认证/未初始化时设备回 **B1H 且 `device_num = 0`**（无逐模块状态），既不是成功
   也不是失败。测试前须完成 E0H 认证 + 1AH 初始化（详见
   `LDI会话状态机与命令门禁.md`）。发往 **TCP 业务口（默认 9528）** 或 LDI 已绑定的
   任一通道（TCP Server / TCP Client / UDP）均可。

### 6.3 用例 1：文字播报（成功路径）

文本「测试」GBK = `B2 E2 CA D4`（4 字节），`mod_len = 6 + 4 = 10 = 0x000A`。

```
FF FF 00 02 00 00 00 25 1B 00 00 01 9A 38 64 5D E3 20 13 14 52 00 01 23 45 67 89 AB CD EF 00 00 01 00 0A F4 01 01 00 00 04 B2 E2 CA D4 5F 51
```

逐字段：

| 段 | 字节 | 说明 |
|---|---|---|
| STX / VER / SEQ | `FF FF` `00` `02` | SEQ 任意，应答回显 |
| LEN | `00 00 00 25` | DATA = 37B = 24 + 1 + 2 + 10 |
| CtrlHead | `1B` `00 00 01 9A 38 64 5D E3` `20 13 14 52 00` `01 23 45 67 89 AB CD EF` `00 00` | 时间戳不参与校验（应答用设备 RTC 重建） |
| device_num / mod_len | `01` `00 0A` | 1 个模块，module 段 10B |
| module | `F4` `01` `01` `00` `00 04` `B2 E2 CA D4` | F4H / Index 1 / 功能 01H / Type 00H / Length 4 / 「测试」 |
| CRC | `5F 51` | `crc16_xmodem(VER..DATA)` |

**预期**：B1H 模块段为 `00 04 F4 01 01 00`（2B 大端长度前缀 + 4B 模块响应帧，
`CtlStatus = 00H`），语音板播报「测试」。
把 Data 换成任意 GBK 文本并同步改 `Length`、`mod_len`（+N）与 CRC 即可；`Length = 0`
或与实际字节数不符会被判 `01H`。

### 6.4 用例 2：Type = 01H（失败路径）

同样文本，仅 `Type` 由 `00` 改 `01`（CRC 随之变化）：

```
FF FF 00 02 00 00 00 25 1B 00 00 01 9A 38 64 5D E3 20 13 14 52 00 01 23 45 67 89 AB CD EF 00 00 01 00 0A F4 01 01 01 00 04 B2 E2 CA D4 C0 84
```

**预期**：B1H 中模块段 `00 04 F4 01 01 01`（`CtlStatus = 01H`），语音板**不出声**。

（其他失败用例同理：`DeviceFuncType` 改 `02`、`Length` 改成 `00 05`（比实际多 1 字节）
或 `00 00`，均回 `01H`。CRC 需重算。）

### 6.5 验收步骤

1. EIDE `Debug` target 构建通过（本模块无新增告警）；
2. 设备进入 LDI READY 后，向 TCP 业务口发 §6.3 帧 → 听语音板播报「测试」，
   B1H 中 F4H 模块段为 `00 04 F4 01 01 00`（`CtlStatus = 00H`）；
3. 再发 §6.4 帧 → 无声，B1H 中 F4H 模块段为 `00 04 F4 01 01 01`
   （`CtlStatus = 01H`，对照 `00H` 证明状态不再是假成功）；
4. （可选）发一条 200 字节以上的文本 → 收到 `01H`（超过语音板单帧上限，协议层拒绝，
   不会静默截断）。
