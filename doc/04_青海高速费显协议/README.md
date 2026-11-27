# 04 青海高速费显协议

> 青海治超站费显协议（`{` 帧族）接入记录。协议原文：`治超站费显通信协议V1.0.txt`；
> 实现细节与帧格式权威：`PartB_ProtocolParser_QingHai接入.md`。
> 本文由原 `PartA_STD设备层架构优化.md`（设备能力复用分析）压缩而来（2026-11-27），
> 过程推演见 `doc/00_变更历史.md`。

## 架构决策

| 决策 | 结论 |
|---|---|
| 共享服务层 `dev_service` | **废弃**——青海所需外设（HUB75、车道灯、报警灯、光敏、RS485、网口）已由 `Device/` 提供，再包一层只是转发；应答帧回发也不属于设备能力 |
| 协议层内部结构 | **Parse/Execute + Tagged Union**——解析器 `qh_parse_frame` 产出 `qh_parsed_cmd_t` 清单（纯函数），执行器 `qh_execute_cmd` 消费清单调设备；零新增队列 |
| Device 层新增 | **0**——V1.0 全部功能在既有 `Device/` 能力内（`dev_rs232_voice` 在 2019 版接入时已落地） |

## 命令 → 设备调用矩阵

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

与 LDI 复用同一套 `Device/` 接口，无青海专用设备封装。

## 设备能力缺口

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

## 分发链位置与编译期配置

```
RS485 / RS232 / UDP(10011)
  -> app_channel_dispatch -> ring_buffer -> app_dispatch 链式 probe
  -> qh_proto_probe_frame（'{' 快拒 + 命令字白名单 + '}' 校验）
  -> qh_queue（静态 SRAM，深度 3）-> qh_proto_handle_task
       qh_parse_frame(msg->data, msg->data_len) -> qh_execute_cmd(msg->ch, &cmd)
```

- UDP 与 IAP（`0x5A`）/ LDI（`0xFF`）共用 RJ45 槽 RB，三方均为首字节快拒，无链式探测冲突。
- 构建仅 EIDE：启用/停用 = GUI 对 `ProtocolParser_QingHai` 取消排除/排除，改后必须 `EIDE: Reload Project`。
- **`{` 帧族互斥**：本模块定义 `g_brace_proto_guard`，与山东/贵州常规/云南常规/云南治超/四川 MTC
  同编（未定义 `STD_ALL_PROTO`）会链接报 `multiple definition`。详见 `doc/构建开关总表.md` §7.2。

## 验收标准

| 验收项 | 标准 |
|---|---|
| 无 `dev_service` 层 | 全仓无 `dev_service_*` 调用 |
| 解析器纯函数 | `app_qh_proto_parse.c` 不 include 任何 `Device/` 头 |
| 执行器唯一出口 | 所有设备调用只出现在 `app_qh_proto_cmd.c` |
| 应答唯一出口 | 所有应答只经 `qh_reply()` |
| 不影响其它协议 | 青海编入后既有协议行为不变（probe 首字节 / 命令字不重叠） |
