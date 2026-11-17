# Part B：ProtocolParser_QingHai 接入（V1.0 治超站）

**状态**：`[已实装]`（2026-11-17 按 V1.0 全量重写，旧 2019 版实现已废弃）
**协议基线**：《治超站费显通信协议 V1.0》（2025-12-02 创迪添加 0x3A/0x3B/0x3C）
**协议原文**：`doc/04_青海高速费显协议/治超站费显通信协议V1.0.txt`
（上一版 `青海协议修改20191006.txt/.doc` 仅作历史留档，**不再是实现依据**）
**协议层内部架构**：Parse/Execute + Tagged Union
**构建开关**：`.eide/eide.yml` 的 `Debug` 已取消排除本目录（`Base` 仍作基线模板排除）

---

## B.1 定位

| 项 | 值 |
|---|---|
| 模块目录 | `Application/Src/ProtocolParser_QingHai/`、`Application/Inc/ProtocolParser_QingHai/` |
| 自注册入口 | `qh_proto_init`（`sw_app_initcall`） |
| 通道 | `CH_ID_RS485`（USART1）+ `CH_ID_RS232`（USART3）+ `CH_ID_UDP`（RJ45 槽，**端口 10011**） |
| 串口参数 | 9600 / 8 / N / 1（`Core/Src/usart.c`） |
| 帧家族 | `{` 帧族 → 定义 `g_brace_proto_guard` 链接期互斥守卫 |
| 队列 | `QH_QUEUE_DEPTH = 3`、`QH_PAYLOAD_MAX = 259`，队列体静态 SRAM |

> 协议原文要求「网口 UDP 2929」；**本工程按用户裁决改用 10011**（与 IAP/LDI/CQ 共用的网口槽）。
> `CH_ID_RS232_1`（USART6）是语音板 TTS 专用，本协议禁止绑定。

---

## B.2 帧格式

```
'{' (0x7B) | 命令字 (1B) | 参数长度 (1B) | 参数数据 (变长) | '}' (0x7D)
```

- 参数长度 = **二进制字节数**（不是 ASCII 数字），整帧长 = len + 4。
  判据：协议原文示例 `7B 35 04 1A 0F 0A 0E 7D`、`7B 3A 0C … 7D`。
- probe 首字节非 `0x7B` → `PROTO_PROBE_FAKE`；命令字非白名单 → 整帧 FAKE
  （交分发器 `rb_skip(1)` 重同步）。

---

## B.3 命令表（V1.0 全集）

| 命令字 | 枚举 | 含义 | 参数 | 成功应答 |
|---|---|---|---|---|
| `'1'` 0x31 | `QH_PCMD_HOST_QUERY` | 主机查询 | 无 | `7B 31 01 00 7D`（01 表示状态不正常） |
| `'2'` 0x32 | `QH_PCMD_MATRIX_SHOW` | 点阵显示 | `x0` 行号 `'0'`~`'4'`、`x1` 颜色、`x2..` GBK 文本 | `7B 32 01 00 7D` |
| `'3'` 0x33 | `QH_PCMD_CLEAR` | 全屏清除 | 无 | `7B 33 01 00 7D` |
| `'4'` 0x34 | `QH_PCMD_PERIPHERAL` | 通行灯 / 报警 | `x0`（hex 单字节）：0x01 红叉 / 0x02 绿箭 / 0x03 报警 / 0x04 关报警 | `7B 34 01 00 7D` |
| `'5'` 0x35 | `QH_PCMD_VOICE_SEG` | 语音段组合 | 每字节一个段号 0~45 | `7B 35 01 00 7D` |
| `0x3A` | `QH_PCMD_SET_IP` | IP/掩码/网关 | 12B：ip[4] mask[4] gw[4] | `7B 3A 01 00 7D` |
| `0x3B` | `QH_PCMD_FONT_SIZE` | 字体大小 | 0 自适应 / 1=16 / 2=24 / 3=32 | `7B 3B 01 00 7D` |
| `0x3C` | `QH_PCMD_BRIGHTNESS` | 亮度 | 0 自动 / 1 最暗 / 8 最亮 | `7B 3C 01 00 7D` |

**2019 版遗留命令 `'6'`/`'7'`/`'8'`/`'9'`/`'A'`/`'B'` 已全部移除**，probe 直接 FAKE。

失败应答：`7B <cmd> 01 01 7D`。攻击面收敛在 `qh_reply()` 一个函数里，
协议若给出正式应答帧格式，只改这一处。

---

## B.4 模块结构

```
probe      qh_proto_probe_frame   帧定界 + 命令字白名单（PURE：只 peek，无副作用）
  │
  ├─ frame_queue（静态 SRAM，深度 3）
  │
parse      qh_parse_frame         bytes → qh_parsed_cmd_t（纯函数，不碰 Device/）
  │
execute    qh_execute_cmd         清单 → Device/ 驱动，并统一应答
           ├─ qh_exec_matrix     '2' 显示
           ├─ qh_exec_clear      '3' 清屏
           ├─ qh_exec_peripheral '4' 车道灯 / 报警灯
           ├─ qh_exec_voice      '5' → qh_voice_segments()
           ├─ qh_exec_set_ip     0x3A → app_board_net_cfg_update()
           ├─ qh_exec_font       0x3B 字号状态
           └─ qh_exec_brightness 0x3C → 光敏范围 + dev_display_set_brightness()
```

**职责边界**：`app_qh_proto_parse.c` 不 include 任何 `Device/` 头；
所有设备调用只出现在 `app_qh_proto_cmd.c`。

---

## B.5 关键实现口径（协议有歧义处的收敛）

### B.5.1 长度字段
二进制字节数。**历史坑**：旧版本 Part B 曾写「长度是 ASCII 数字 `'0'~'9'`、总帧长 = 3 + len」，
与协议原文和实现均不符，已作废。按 ASCII 口径发 `7B 31 30 7D` 会让 probe 认为
len=0x30=48、等 52 字节，永远等不到 → 静默无响应。

### B.5.2 `'2'` 的行号与颜色
- 行号 `x0`：**ASCII**（协议明确标注 0x30~0x34），归一化为 `0 = 全屏 / 1~4 = 行号`。
- 颜色 `x1`：**单字节二进制**（0 红 / 1 绿 / 2 黄），与长度字段、`'4'` 外设码同一口径。
- 屏幕按 **4 行**布局（`QH_DISPLAY_ROWS`），行高 = 屏高 / 4。
  当前 `dev_display_1_969`（192×96）→ 行高 24px，自适应字号落 `FONT_24`，
  正好对应协议「每行 8 汉字或 16 ASCII」（192 / 8 = 24）。

### B.5.3 `'4'` 的通行灯 / 报警码
`7B 34 01 x0 7D`，`x0` 是**单字节十六进制**，原文的 `'01'` / `'02'` 只是便于阅读的 hex 写法，
不是两个 ASCII 字符：

| `x0` | 动作 | 硬件 |
|---|---|---|
| `0x01` | 通行灯显示**红叉** | `dev_io_lane_light(false)`（PD14 复位） |
| `0x02` | 通行灯显示**绿箭** | `dev_io_lane_light(true)`（PD14 置位） |
| `0x03` | 报警开 | `dev_io_flash_light(true)`（PD15） |
| `0x04` | 关闭报警 | `dev_io_flash_light(false)` |

取值域 0x01~0x04 之外 → `QH_PARSE_ERR_PARAM`，应答 `7B 34 01 01 7D`。
**不做 ASCII 兼容**（曾一度双收 `'0'?'1'`，2026-11-17 经确认后移除）。

### B.5.4 `'5'` 语音段
本板语音板是 RS232 TTS（`dev_rs232_voice_play` 收 GBK 文本），无法按 wav 编号取段。
实现把段号映射为等价中文文本（`app_qh_proto_voice.c` 表 `s_seg_text[0..45]`），
**按顺序拼接成一句后整体 TTS**：

| 帧 | 段号序列 | 拼接结果 |
|---|---|---|
| `7B 35 04 1A 0F 0A 0E 7D` | 26, 15, 10, 14 | 您好 + 请交费 + 十 + 元 = 「您好请交费十元」 |

与协议示例自洽。未定义段号（>45）跳过；拼接缓冲 128B 满则截断。

### B.5.5 `0x3B` 字号
`0` = 自适应（按行高选 32/24/16/14），`1/2/3` = 强制定 16/24/32。
字号是模块内状态 `s_qh_font_cfg`，**只影响后续显示**，不重刷当前内容。

### B.5.6 `0x3C` 亮度
光敏任务 `app_light_sensor_task` 每秒重写 `light_level`，**直接设亮度会在 1s 内被覆盖**。
因此手动档（1~8）同时把自动调光范围钉死为 `[level, level]`；
`0`（自动）恢复默认范围 `[LIGHT_SENSOR_MIN_LEVEL, LIGHT_SENSOR_MAX_LEVEL]`（4~8）。

### B.5.7 UDP 应答缓冲必须放 SRAM（不能是任务栈）

`qh_reply()` 的应答缓冲是 `static uint8_t rsp[5]`（`.bss`，SRAM）。

**这不是风格问题，否则会打死网口**：
- 协议任务栈由 `PL_TASK_STATIC_STORAGE()` 放在 **CCMRAM（0x10000000）**，
  而 CCM 由 CPU 专用总线访问，**ETH DMA 不可访问**；
- `udp_ch_send` → `netbuf_ref()` **只登记指针、不拷贝数据**；
- `pl_eth.c` 的 `low_level_output()` 是**零拷贝**：直接把 `pbuf->payload`
  填进 ETH DMA 发送描述符。

三者叠加 → 用栈数组应答时，DMA 去读 CCM 地址 → **总线错误、ETH DMA 停摆、网口彻底死**
（ping 随之无响应，直到看门狗约 33s 复位）。

**纪律**：任何走网口（`CH_ID_UDP*`）的 `channel_send`，负载必须是
`.bss` / `.data` / 静态数组 / 协议上下文里的持久缓冲，**绝不能是函数栈数组**。

现状核对（2026-11-17）：

| 协议 | 应答缓冲 | 结论 |
|---|---|---|
| LDI | `g_ldi.tx_buf`（全局） | ✅ |
| IAP | `pIAP_ReTmp`（静态） | ✅ |
| 重庆 CQ | `s_cq_bin_reboot_rsp`（静态）；`_cq_exec_bin_search()` 的广播帧 `buf[25]` 2026-11-17 改静态 | ✅ |
| 青海 | `static uint8_t rsp[5]`（本次修正） | ✅ |
| 贵州治超 | `_gz_ol_send_frame()` 的 `buf[]` 2026-11-17 改静态 | ✅ |
| 四川治超 | `_sc_ol_send_reply()` 的 `buf[]` 2026-11-17 改静态（当前只绑串口，预防） | ✅ |
| 云南治超 | `_yn_ol_send_frame()` 的 `buf[]` 2026-11-17 改静态（当前只绑串口，预防） | ✅ |
| 广西 / 山东 / 贵州 / 云南 / MTC / ETC / 新疆 | 局部数组或字面量，但**只绑 RS485/RS232** | ✅ 串口同步拷贝，无此风险 |
| AH_MQTT | `ReturnBuff[128]` 局部，走 MQTT/TCP 同步发布 | ✅ 非 UDP 零拷贝路径 |

**全仓核查结论（2026-11-17）**：不再有「UDP 发送 + 栈数组」组合。

### B.5.9 运行期兜底：`udp_payload_dma_reachable()`
根因修完后又补了一道守卫（`app_udp.c`），把「再犯一次的后果」从「网口死」降级为「这一帧发不出去」：

```c
static bool udp_payload_dma_reachable(const uint8_t *data, uint16_t len)
{
    /* 缓冲与 [_sccmram, _eccmram) 相交 → 在 CCM，DMA 不可达 */
}
```

- 段边界取链接脚本符号 `_sccmram` / `_eccmram`（`Compiler/STM32F407XX_FLASH.ld`），**不硬编码地址**。
- 覆盖全部 4 个 `netbuf_ref` 出口：`udp_ch_send`（10011）、`udp_cq_ch_send`、`udp_gzol_ch_send`、
  `_udp_broadcast_send`（广播）。
- 命中即拒绝发送（返回 `-1` / `ERR_ARG`）并累加 `g_udp_ccm_reject_cnt`。
- **诊断方式**：调试器 Watch 读 `g_udp_ccm_reject_cnt`（`app_udp.c` 定义，`.bss 0x200007b8`）。
  恒为 0 才正常；非 0 说明有协议又用了栈数组发 UDP。
- 配套：`app_dispatch.h` 的 `channel_send()` 声明补了「缓冲必须 DMA 可达且持久」的契约注释。

串口（RS485/RS232）不受影响：`pl_uart` 发送是同步拷贝进 DMA 缓冲，不经零拷贝路径。

### B.5.8 `0x3A` 网络配置
`app_board_net_cfg_get()` 取当前记录 → `app_board_net_cfg_update(ip, mask, gw, cur.port, cur.udp_port)`，
**只改 IP/掩码/网关，保留既有 TCP/UDP 端口**（避免覆盖 LDI/IAP/CQ 写过的端口值）。

---

## B.6 文件清单

| 文件 | 职责 |
|---|---|
| `app_qh_proto.h` | 命令枚举、参数结构体、解析结果、入口声明 |
| `app_qh_proto.c` | 注册（三通道）/ probe / 帧处理任务 |
| `app_qh_proto_parse.{h,c}` | `qh_parse_frame`（纯函数）+ `qh_char_to_cmd` + 参数解码 |
| `app_qh_proto_cmd.{h,c}` | `qh_execute_cmd` + 各 `qh_exec_*` + `qh_reply` 统一应答 |
| `app_qh_proto_voice.{h,c}` | 语音段 0~45 → 中文文本表 + TTS 播报 |

---

## B.7 验证

| 项 | 证据 |
|---|---|
| 单文件编译 | 4 个 `.c` 用工程原 flags 编译，`-Wall` 零警告 |
| 链接 | `unify_builder build` 退出码 0 |
| 段尺寸 | RAM 120232/128KB（91.73%）、CCMRAM 50716/64KB（77.39%）、FLASH 250344/768KB（31.83%）（2026-11-17 全量 clean rebuild） |
| map 符号 | `qh_proto_init` / `qh_proto_probe_frame` / `qh_parse_frame` / `qh_execute_cmd` / `qh_voice_segments` 均在 `build/Project_STD/Debug/Project_STD.map` |
| 产物 | `E:\3833024\build\Project_STD\Debug\Project_STD.hex` |

---

## B.8 与其它 `{` 帧族的关系

`g_brace_proto_guard` 使本协议与山东 / 贵州常规 / 云南常规 / 云南治超 / 四川MTC
**不能同时编入**（未定义 `STD_ALL_PROTO` 时链接报 `multiple definition`）。

命令字层面（V1.0 后）本 probe 只认领 `'1'~'5'` 与 `0x3A/0x3B/0x3C`：
- 与山东 `'1'~'5'` 仍重叠（`'7'`/`'8'` 已不再冲突）；
- 与贵州 / 云南 `'1'~'5'` 仍重叠（`'7'`/`'8'`/`'9'`/`'A'`/`'B'` 已不再冲突）；
- 与四川 MTC 的 `7B 41` / `7B 42` 不再冲突（`'A'`/`'B'` 已移出白名单）。

量产仍按 **EIDE 目录二选一**执行，命令字收敛只是降低误认领面，不替代排他编译。

---

## B.9 待确认项

1. **应答帧格式**：协议只写「显示器应答：已发送数据一致」，未给字段。
   现实现为 `7B <cmd> 01 <sta> 7D`，需向创迪确认正式格式。
2. **`'2'` 颜色码**：已按单字节二进制实现（0/1/2）；若上位机实际发 ASCII `'0'`~`'2'`，
   解析会判 `ERR_PARAM` 并回 `7B 32 01 01 7D`（有应答，便于定位），不会静默。
3. **网口端口**：协议原文 2929，本工程用 10011（用户裁决，非协议）。

已确认（2026-11-17）：
- **`'4'` 通行灯 / 报警码为单字节 hex**：`0x01` 红叉、`0x02` 绿箭、`0x03` 报警开、`0x04` 关报警。
  原文的 `'01'`/`'02'` 是 hex 书写形式，不是两个 ASCII 字符。