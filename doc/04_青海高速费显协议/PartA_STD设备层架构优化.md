# Part A：STD 设备能力复用与缺口分析（青海协议适配）

**状态**：`[已实装]`（2026-11-17 按 V1.0 治超站协议复核）
**目标**：确认青海协议对 `Device/` 层的需求边界——哪些直接复用、哪些需新增、哪些留在协议层。
**协议版本**：治超站费显通信协议 V1.0（2025-12-02 创迪版）
**协议原文**：`doc/04_青海高速费显协议/治超站费显通信协议V1.0.txt`
**实现细节**：见 `PartB_ProtocolParser_QingHai接入.md`

---

## A.0 架构决策

### A.0.1 是否保留 `dev_service` 共享层 → 否

| 方案 | 结论 |
|---|---|
| 新建跨协议共享层 `dev_service`（`dev_service_host_status_query()` / `dev_service_show_text_xy()`…） | **废弃**。青海所需外设（HUB75、车道灯、报警灯、光敏、RS485、网口）已由 `Device/` 提供，再包一层只是转发；应答帧回发也不属于设备能力 |
| 协议层直接调 `Device/` 驱动，仅「协议无关的能力缺口」才在 `Device/` 新增 | **采用**。与 LDI 现状一致（`app_ldi_device.c` 直接调 `app_render` / `dev_display_*` / `dev_io_ctrl`） |

### A.0.2 协议层内部结构 → Parse/Execute + Tagged Union

| 方案 | 结论 |
|---|---|
| 一把梭（每个 cmd handler 自己强转、解析、调驱动、回响应） | 命令少时简单，但解析/执行耦合，难定位裸机差异 |
| 事件驱动 Pub/Sub | 与 `app_dispatch` 的 `ring_buffer→probe→queue→协议任务` 重复成双重队列（多 RAM + memcpy + 上下文切换），并破坏「解析完立即回响应」的同步时序 |
| **Parse/Execute + Tagged Union** | **采用**。解析器 `qh_parse_frame` 产出 `qh_parsed_cmd_t` 清单（纯函数），执行器 `qh_execute_cmd` 消费清单调设备；在已有 frame_queue 之后同步调用，零新增队列 |

**清单设计原则**

- `cmd` 枚举 + `sta` 分级校验（`OK` / `ERR_FRAME` / `ERR_CMD` / `ERR_PARAM`）；
- union 内只放**协议域已解码值**（颜色索引 0/1/2、行号、段号、IP 字节、亮度档），
  不放设备域值（`COLOR_RED`、硬件亮度级）——映射留在执行器；
- 清单里的 `data` 指针指向 `frame_msg_t` 缓冲，必须同任务同步消费，禁止跨任务异步持有。

---

## A.1 V1.0 功能全景

| 序号 | 功能 | 分类 | 实现位置 |
|---|---|---|---|
| F01 | `{`…`}` 帧定界 + 命令字白名单 | 协议层 | `app_qh_proto.c` `qh_proto_probe_frame` |
| F02 | 命令字分发（`'1'~'5'`、`0x3A/0x3B/0x3C`） | 协议层 | `app_qh_proto_parse.c` `qh_char_to_cmd` |
| F03 | 主机查询应答 `7B 31 01 st 7D` | 协议层 | `app_qh_proto_cmd.c` `qh_reply` |
| F04 | 点阵显示（行号 + 颜色 + GBK 文本） | Device 已有 | `app_render` + `dev_display` |
| F05 | 全屏清除 | Device 已有 | `dev_display_fill` + `dev_display_commit_frame` |
| F06 | 通行灯红叉/绿箭 + 报警开/关 | Device 已有 | `dev_io_lane_light` + `dev_io_flash_light` |
| F07 | 语音段组合（0~45） | 协议层 + `232_voice` | `app_qh_proto_voice.c` + `dev_rs232_voice_play` |
| F08 | IP/掩码/网关写入 | Device 已有（Config） | `app_board_net_cfg_get/update` |
| F09 | 字体大小（自适应/16/24/32） | 协议层 | `qh_exec_font` 模块内状态 |
| F10 | 亮度（自动/1~8） | Device 已有 | `dev_display_set_brightness` + `app_light_sensor_set_range` |
| F11 | 三通道收发（RS485/RS232/UDP-10011） | Device 已有 | `app_dispatch` 通道框架 |
| F12 | 文字渲染（GBK→点阵→屏显） | Device 已有 | `app_render` |
| F13 | HUB75 颜色填充 / 清屏 | Device 已有 | `dev_display` / `RENDER_FILL` |
| F14 | TTS 语音播报（GBK 文本 → 语音板） | Device 已有（既有模块） | `dev_rs232_voice_play` |

**结论**：V1.0 的全部功能都在既有 `Device/` 能力内，**无需新增任何 Device 模块**，
`dev_rs232_voice` 在 2019 版接入时已落地。

---

## A.2 命令 → 设备调用矩阵

| cmd | 协议层动作 | Device/ 调用 |
|---|---|---|
| `'1'` | 判断显示驱动是否就绪 | `dev_display_get()` + `channel_send` |
| `'2'` | 行号→y、颜色→`display_color_t`、按行高选字号 | `app_render(RENDER_TEXT, ...)` |
| `'3'` | 全屏填黑并提交 | `dev_display_fill` + `dev_display_commit_frame` |
| `'4'` | 0x01 红叉 / 0x02 绿箭 / 0x03 报警 / 0x04 关报警（hex 单字节，不做 ASCII 兼容） | `dev_io_lane_light` + `dev_io_flash_light` |
| `'5'` | 段号→中文文本拼接 | `dev_rs232_voice_play` |
| `0x3A` | 保留现端口，只换 ip/mask/gw | `app_board_net_cfg_get` + `app_board_net_cfg_update` |
| `0x3B` | 存字号档 | —（影响后续 `'2'`） |
| `0x3C` | 手动档钉死光敏范围 | `app_light_sensor_set_range` + `dev_display_set_brightness` |

**与 LDI 的复用对比**：HUB75、车道灯、报警灯、亮度、光敏、RS232 通道全部同一套 `Device/` 接口，
无青海专用设备封装。

---

## A.3 设备能力缺口

| 能力 | 状态 | 入口 |
|---|---|---|
| HUB75 文字显示 | 已覆盖 | `app_render`（RENDER_TEXT，任意 x/y） |
| HUB75 颜色填充 / 清屏 | 已覆盖 | `app_render(RENDER_FILL)` / `dev_display_fill` |
| 车道灯 / 报警灯 | 已覆盖 | `dev_io_lane_light` / `dev_io_flash_light` |
| 亮度（手动 + 光敏自动） | 已覆盖 | `dev_display_set_brightness` + `app_light_sensor_set_range` |
| RS485 / RS232 / 网口 | 已覆盖 | `dev_rs485` / `dev_rs232` / `dev_eth` + 通道框架 |
| TTS 语音板 | 已覆盖 | `dev_rs232_voice_play`（`Device/Comm/`） |
| 网络配置落盘 | 已覆盖 | `app_board_net_cfg_update` |

**缺口：无。**

---

## A.4 分层架构

```
+--------------------------------------------------------------+
|  Protocol Layer（LDI / CQ / RLS / QH …）                       |
|    青海内部：                                                  |
|      qh_parse_frame()  bytes ──▶ qh_parsed_cmd_t（清单）        |
|      qh_execute_cmd()  清单  ──▶ 协议→设备映射 + 应答           |
|    协议特有：命令字映射、颜色/行号归一化、字号档、语音段表、      |
|              应答格式（qh_reply）                              |
+--------------------------------------------------------------+
|  Device Layer（Device/）                                       |
|    app_render / dev_display     HUB75 显示 / 亮度              |
|    dev_io_ctrl                  车道灯 / 报警灯                 |
|    dev_light_sensor             光敏自动调光                    |
|    dev_rs232 + 232_voice        RS232 / 语音板 TTS             |
|    dev_rs485 / dev_eth          485 / 网口                     |
+--------------------------------------------------------------+
|  Application/Config                                            |
|    app_board_net_cfg            Sector1 网络配置               |
+--------------------------------------------------------------+
```

### A.4.1 青海协议在分发链中的位置

```
RS485 / RS232 / UDP(10011)
  -> app_channel_dispatch
  -> ring_buffer -> app_dispatch 链式 probe
  -> qh_proto_probe_frame（'{' 快拒 + 命令字白名单 + '}' 校验）
  -> qh_queue（静态 SRAM，深度 3）
  -> qh_proto_handle_task
       qh_parsed_cmd_t cmd = qh_parse_frame(msg->data, msg->data_len);
       qh_execute_cmd(msg->ch, &cmd);   // 内部统一 qh_reply()
```

> UDP 与 IAP（`0x5A`）/ LDI（`0xFF`）共用 RJ45 槽 RB，三方均为首字节快拒，无链式探测冲突。

---

## A.5 编译期配置

构建入口只有 EIDE，**无 Makefile**。青海模块的启用/停用在 EIDE GUI 里对
`<virtual_root>/Application/protocol/ProtocolParser_QingHai` 做「取消排除 / 排除」：

- 现状：`Debug` 已取消排除（**日常构建含青海**）；`Base` 仍作为基线模板排除。
- 改配置后必须 `EIDE: Reload Project`，否则 EIDE 回写会冲掉外部编辑。
- **`{` 帧族互斥**：本模块定义了 `g_brace_proto_guard`，与山东/贵州常规/云南常规/云南治超/四川MTC
  同编（未定义 `STD_ALL_PROTO`）会链接报 `multiple definition`。
  详见 `doc/构建开关总表.md` §7.2。

---

## A.6 验收标准

| 验收项 | 标准 |
|---|---|
| 无 `dev_service` 层 | 全仓无 `dev_service_*` 调用 |
| 协议层直接复用 Device | `app_render` / `dev_display_*` / `dev_io_ctrl` / `dev_light_sensor` / `dev_rs232_voice` 由青海协议层直接调用 |
| 解析器纯函数 | `app_qh_proto_parse.c` 不 include 任何 `Device/` 头 |
| 执行器唯一出口 | 所有设备调用只出现在 `app_qh_proto_cmd.c` |
| 应答唯一出口 | 所有应答只经 `qh_reply()` |
| LDI / 其它协议不受影响 | 青海编入后 LDI、广西等既有协议行为不变（probe 首字节 / 命令字不重叠） |

---

## A.7 总结

| 项目 | 结论 |
|---|---|
| 是否保留 `dev_service` 共享层 | **否**，已砍掉 |
| 需新增的 Device 模块 | **0**（`232_voice` 在 2019 版接入时已落地） |
| 协议层保留逻辑 | 帧定界、命令字映射、颜色/行号归一化、字号档、语音段表、应答格式、IP 落盘编排 |
| 协议层内部架构 | Parse/Execute + Tagged Union，同步无额外队列 |
| 工作重心 | 协议解析器与应答契约（Part B），非设备层 |