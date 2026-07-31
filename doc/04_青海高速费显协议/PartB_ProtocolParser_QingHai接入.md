# Part B：ProtocolParser_QingHai 接入

**状态**：`[规划中]`
**前置条件**：CQ Part A 完成（`dev_service.*` / `dev_voice.*` 已就绪，QH 直接复用）
**协议版本**：青海高速费显协议 2019-10-06
**参考裸机工程**：`/home/yystation/Desktop/9K1030CE00`
**协议原文**：`/home/yystation/Desktop/9K1030CE00/07协议/青海协议修改20191006.txt`

> QH 协议解析器通过 `dev_service_*` 调用设备能力，不直接碰底层硬件。
> 所有协议特有逻辑（帧定界、命令分发、字段解析、格式编排）收敛在本模块内。

---

## B.1 背景与定位

### B.1.1 现有协议栈

| 协议 | 目录 | 帧形态 | 默认通道 | 编译宏 |
|------|------|--------|----------|--------|
| IAP | `Application/Src/IAP/` | 二进制 `0x5A5A5A5A` | UDP/10011 | — |
| LDI | `Application/Src/LDI/` | 二进制 `0xFFFF` + CRC16-XMODEM | TCP + UDP/10011 | `PROTO_LDI` |
| RLS | `Application/Src/RLS/` | 二进制 | RS485 | — |
| AH_MQTT | `Application/Src/AH_MQTT/` | MQTT topic | MQTT | — |
| 重庆（规划中） | `Application/Src/ProtocolParser_ChongQing/` | JSON `{...}` + 固定 12B 二进制 | UDP/20103 + UDP/10011 | `PROTO_CHONGQING` |
| **青海（新建）** | `Application/Src/ProtocolParser_QingHai/` | **ASCII `{cmd len data}`** | **RS232（串口）** | **`PROTO_QINGHAI`** |

### B.1.2 青海协议核心特征

青海协议是**基于串口的 ASCII 文本协议**，与 LDI（二进制 TCP/UDP）和 CQ（JSON UDP）有本质区别：

| 特征 | 青海协议 | LDI | CQ |
|------|----------|-----|-----|
| 传输层 | **串口 RS232** | TCP + UDP | UDP |
| 帧格式 | ASCII `{cmd len data}` | 二进制 0xFFFF + CRC16 | JSON `{...}` 或 12B 二进制 |
| 校验 | **无** | CRC-16/XMODEM | 无（JSON）/ CRC（BIN） |
| 命令编码 | ASCII 字符 `'1'~'B'` | 十六进制 0x0A~0x21 | JSON "cmd" 字段 |
| 状态机 | **无**（纯命令驱动） | UNINIT→AUTHED→READY | 无（心跳检测） |
| 显示行数 | **5 行**（16/24/32 点阵） | 8 行 | 8 行 |
| 上电默认 | "祝您一路平安" + 语音 | 网络搜索 | 网络搜索 |

### B.1.3 与裸机工程的关系

移植策略：**行为对齐裸机联调结果**（含已知与协议文档的差异），API/分层对齐本工程。**通过 Part A 的 `dev_service_*` 调用所有设备能力**。

### B.1.4 与 LDI 的产品关系

**编译期互斥**：青海固件不含 LDI，LDI 固件不含青海。编译切换通过 Makefile / `.eide` 的条件编译实现（如 `PROTO=QH`）。

---

## B.2 功能全景——协议特有功能清单

> 以下所有功能均属于"协议特有功能"，在 `ProtocolParser_QingHai` 层实现。
> 设备层功能（显示、语音、亮度、音量、通行灯、黄闪）通过 `dev_service_*` 调用，详见 Part A。

### B.2.1 协议特有功能矩阵

| 序号 | 功能 | 协议命令 | 实现位置 | 说明 |
|------|------|---------|---------|------|
| P01 | **ASCII 帧定界** | — | `app_qh_proto.c` probe | `'{'` 起始 + 长度字段 + `'}'` 结尾 |
| P02 | **命令字查表分发** | `'1'~'B'` | `app_qh_proto_cmd.c` | 11 条命令的分发表 + 处理函数 |
| P03 | **主机查询应答** | `'1'` | `app_qh_proto_cmd.c` | 构造 `{1 01 00}` 固定应答帧，串口回发 |
| P04 | **自检模式编排** | `'2'` | `app_qh_proto_cmd.c` | 全屏黄色 + 语音"系统正在加电自检" |
| P05 | **单行显示参数解析** | `'3'` | `app_qh_proto_cmd.c` | 解析颜色(0~2) + 行号(1~5) + GBK文本 |
| P06 | **全屏可编辑显示** | `'4'` | `app_qh_proto_cmd.c` | 解析颜色 + X/Y坐标 + 文本 + 换行符 |
| P07 | **清屏** | `'5'` | `app_qh_proto_cmd.c` | 调用 `dev_service_clear_screen` |
| P08 | **固定格式显示（客车）** | `'6'` | `app_qh_proto_cmd.c` | 解析 `\|` 分隔字段，构造 5 行格式化文本 + 语音 |
| P09 | **固定格式显示（货车）** | `'6'` | `app_qh_proto_cmd.c` | 同上，增加总重/超重行，0 吨不显示 |
| P10 | **礼貌用语索引映射** | `'7'` | `app_qh_proto_cmd.c` | `'0'~'3'` → 4 条固定 GBK 文本 |
| P11 | **亮度级别映射** | `'8'` | `app_qh_proto_cmd.c` | 协议 0~5 → 硬件 8 级查表映射 |
| P12 | **音量级别映射** | `'9'` | `app_qh_proto_cmd.c` | 协议 1~5 → TTS 音量标签 |
| P13 | **外设控制位解析** | `'A'` | `app_qh_proto_cmd.c` | bit0=绿灯, bit1=红灯, bit2=黄闪 |
| P14 | **费额语音文本构造** | `'6'/'B'` | `app_qh_proto_voice.c` | 金额数字 → "您好请交费XX.XX元" GBK 文本 |
| P15 | **上电欢迎语编排** | — | `app_qh_proto.c` init | "祝您一路平安" 显示 + 语音 |
| P16 | **串口通道绑定** | — | `app_qh_proto.c` init | 绑定 `CH_ID_RS232` |
| P17 | **编译期互斥** | — | Makefile | `PROTO_QINGHAI` 宏 |

---

## B.3 目录与文件规划

与 LDI 同级；源/头分离。**文件名统一使用 `app_qh_proto_*` 样式**：

```
Application/Src/ProtocolParser_QingHai/
  app_qh_proto.c          # 模块 initcall、probe、任务、帧分发
  app_qh_proto_cmd.c      # 命令分发表 + 各命令处理函数
  app_qh_proto_voice.c    # 语音相关：礼貌用语映射、费额金额→TTS文本
  app_qh_proto_cfg.c      # 青海侧配置（波特率等参数落盘）

Application/Inc/ProtocolParser_QingHai/
  app_qh_proto.h
  app_qh_proto_cmd.h
  app_qh_proto_voice.h
  app_qh_proto_cfg.h
```

命名规范：

| 项 | 规则 | 示例 |
|----|------|------|
| 目录名 | `ProtocolParser_QingHai` | — |
| 文件名 | `app_qh_proto_*.c` | `app_qh_proto_cmd.c` |
| 函数前缀 | `qh_proto_` | `qh_proto_cmd_one_line()` |
| 全局变量 | `g_qh_proto_` | `g_qh_proto_ctx` |
| 头文件守卫 | `APP_QH_PROTO_*_H` | `APP_QH_PROTO_CMD_H` |
| 静态变量 | `s_qh_` | `s_qh_font_color` |

### B.3.1 构建编入

| 文件 | 改动 |
|------|------|
| `Makefile` | `INC_DIRS` 增加 `Application/Inc/ProtocolParser_QingHai/`；`SRC_APPLICATION` 条件追加 |
| `.eide/eide.yml` | 增加同组源文件（放在 QH 条件组中） |

**编译期切换方案**：

```makefile
PROTO ?= LDI
ifeq ($(PROTO),QH)
  SRC_APPLICATION += $(wildcard Application/Src/ProtocolParser_QingHai/*.c)
  CFLAGS += -DPROTO_QINGHAI
else ifeq ($(PROTO),CQ)
  SRC_APPLICATION += $(wildcard Application/Src/ProtocolParser_ChongQing/*.c)
  CFLAGS += -DPROTO_CHONGQING
else
  SRC_APPLICATION += $(wildcard Application/Src/LDI/*.c)
endif
```

**无需引入 cJSON 等第三方库**——QH 是纯 ASCII 协议，手动解析即可。

---

## B.4 架构设计

### B.4.1 数据流

```
串口 RS232 收包 (CH_ID_RS232, USART3, 9600~115200bps)
  -> app_channel_dispatch
  -> ring_buffer (id=1, RB_GROUP_PROTO)
  -> frame_dispatch_task
  -> qh_proto_probe_frame
       +-- '{' -> 扫描到 '}' -> 长度校验 -> READY (ASCII 帧)
  -> g_qh_proto_frame_queue
  -> qh_proto_handle_task
       +-- cmd 查表 -> g_qh_proto_cmd_table[]
       +-- qh_proto_voice_*() 语音辅助
       +-- dev_service_*()（通过 CQ Part A 统一服务层调用设备能力）
  -> channel_send(ch, rsp, len)（串口回发应答帧，cmd '1' 专用）
```

### B.4.2 RS232 串口模型

QH 协议**仅使用 RS232 串口**，不涉及 UDP/TCP：

```
CH_ID_RS232 (index=5) -> USART3 -> 9600~115200bps（默认9600）
  -> 承载 QH 协议帧收发
  -> 同时承担 cmd '1' 应答帧回发
```

串口参数：
| 参数 | 默认值 | 可配置 |
|------|--------|--------|
| 波特率 | 9600 | 可通过 cfg 保存 |
| 数据位 | 8 | 固定 |
| 停止位 | 1 | 固定 |
| 校验位 | 无 | 固定 |

---

## B.5 Probe 与帧定界

### B.5.1 ASCII 帧格式

```
字段:  {    cmd    len    data[0..N]    }
长度:   1     1      1      N(=len)       1
示例:  7B   33    0A     xx xx xx...    7D
```

| 字段 | 长度 | 说明 |
|------|------|------|
| 帧头 | 1B | 固定 `'{'` (0x7B) |
| 命令字 | 1B | `'1'~'B'` (0x31~0x42) |
| 参数长度 | 1B | ASCII 数字 `'0'~'9'`，表示 data 字段字节数 |
| 参数数据 | 变长 | data[0]~data[N-1]，N = len 字段的 ASCII 值 |
| 帧尾 | 1B | 固定 `'}'` (0x7D) |

**总帧长 = 3 + len 字节**

### B.5.2 probe 逻辑

```c
qh_proto_probe_sta_t qh_proto_probe_frame(channel_t *ch, ring_buffer_t *rb,
                                           uint32_t *frame_len, uint8_t *aux)
{
    // 1. 读首字节，检查 '{'
    // 2. 读 cmd 字节（偏移1），检查 '1'~'B'
    // 3. 读 len 字节（偏移2），解析为数字 0~99
    // 4. frame_len = 3 + len
    // 5. 检查 rb 中是否有 frame_len 字节可用
    //    -> 有：PROTO_PROBE_READY
    //    -> 无：PROTO_PROBE_WAIT
    // 6. 首字节非 '{' -> PROTO_PROBE_FAKE（跳过1字节）
}
```

**无需花括号深度扫描**——QH 帧格式简单，`{` 和 `}` 不会在 data 区出现（data 是可显示 ASCII 文本或空格）。

### B.5.3 帧校验

QH 协议**无 CRC 校验**。probe 阶段仅校验：
1. 首字节 = `'{'`
2. 命令字 ∈ `'1'~'B'`（合法范围）
3. 长度字段为 ASCII 数字 `'0'~'9'`，且 `len <= FRAME_DATA_MAX_LEN`
4. 帧尾 = `'}'`（通过 rb_peek 检查尾字节）

---

## B.6 命令实现矩阵

### B.6.1 命令表

| cmd | ASCII | 处理函数 | 行为要点 | 设备层映射 | 一期 |
|-----|-------|----------|----------|-----------|------|
| `'1'` | 0x31 | `qh_proto_cmd_host_query` | 构造应答帧 `{1 01 00}`，串口回发 | `channel_send` | **必做** |
| `'2'` | 0x32 | `qh_proto_cmd_self_check` | 自检模式：全屏黄色 + 语音 | `dev_service_fullscreen` + `dev_service_voice_play` | **必做** |
| `'3'` | 0x33 | `qh_proto_cmd_one_line` | 颜色(0~2) + 行号(1~5) + GBK文本 | `dev_service_show_text` | **必做** |
| `'4'` | 0x34 | `qh_proto_cmd_full_screen` | 颜色 + X/Y + 文本 + `\n` 换行 | `dev_service_show_text` (XY版) | **必做** |
| `'5'` | 0x35 | `qh_proto_cmd_clear` | 全屏清除 | `dev_service_clear_screen` | **必做** |
| `'6'` | 0x36 | `qh_proto_cmd_fixed_display` | `\|` 分隔字段解析 + 5 行格式化 + 语音 | `dev_service_show_text` x5 + `qh_proto_voice_fee` | **必做** |
| `'7'` | 0x37 | `qh_proto_cmd_civil_voice` | 索引 `'0'~'3'` → 固定文本 → TTS | `dev_service_voice_play` | **必做** |
| `'8'` | 0x38 | `qh_proto_cmd_brightness` | 映射 0~5 → 硬件级 | `dev_service_set_brightness` | **必做** |
| `'9'` | 0x39 | `qh_proto_cmd_volume` | 映射 1~5 | `dev_service_voice_volume` | **必做** |
| `'A'` | 0x41 | `qh_proto_cmd_peripheral` | bit0=绿灯, bit1=红灯, bit2=黄闪 | `dev_service_lane_light` + `dev_service_yellow_flash` | **必做** |
| `'B'` | 0x42 | `qh_proto_cmd_voice_fee` | type + 5位数字 → 金额语音 | `qh_proto_voice_fee_digits` → `dev_service_voice_play` | **必做** |

### B.6.2 命令分发表

```c
typedef void (*qh_proto_cmd_fn_t)(channel_t *ch, const uint8_t *data, uint8_t len);

typedef struct {
    char                cmd_char;   // ASCII '1'~'B'
    qh_proto_cmd_fn_t   handler;
} qh_proto_cmd_entry_t;

static const qh_proto_cmd_entry_t g_qh_proto_cmd_table[] = {
    { '1', qh_proto_cmd_host_query },
    { '2', qh_proto_cmd_self_check },
    { '3', qh_proto_cmd_one_line },
    { '4', qh_proto_cmd_full_screen },
    { '5', qh_proto_cmd_clear },
    { '6', qh_proto_cmd_fixed_display },
    { '7', qh_proto_cmd_civil_voice },
    { '8', qh_proto_cmd_brightness },
    { '9', qh_proto_cmd_volume },
    { 'A', qh_proto_cmd_peripheral },
    { 'B', qh_proto_cmd_voice_fee },
};
```

命令查找：遍历表匹配 `cmd_char`，O(11) 线性扫描（命令数少，无需哈希）。

---

## B.7 关键命令详细设计

### B.7.1 cmd '1' — 主机查询

**协议格式**：
- 发送：`{1 00}`（无参数）
- 应答：`{1 01 00}`（正常）/ `{1 01 01}`（异常）

**实现**：
```c
void qh_proto_cmd_host_query(channel_t *ch, const uint8_t *data, uint8_t len)
{
    static const uint8_t rsp[] = { '{', '1', '0', '1', '0', '0', '}' };
    channel_send(ch, (uint8_t *)rsp, sizeof(rsp));
}
```

**裸机对齐**：裸机始终返回"正常"（0x00），不检查实际设备状态。STD 工程同样返回固定值。

### B.7.2 cmd '2' — 自检

**协议格式**：
- 发送：`{2 00}`（无参数）
- 行为：全屏显示固定内容 + 语音播报

**实现**：
```c
void qh_proto_cmd_self_check(channel_t *ch, const uint8_t *data, uint8_t len)
{
    // 全屏黄色（裸机行为：固定汉字信息及数字1~9交替显示）
    dev_service_fullscreen(2); // 2=黄色

    // 语音播报："系统正在加电自检"
    static const uint8_t voice_text[] = "系统正在加电自检";
    dev_service_voice_play(voice_text, sizeof(voice_text) - 1);
}
```

### B.7.3 cmd '3' — 单行显示

**协议格式**：
- 发送：`{3 len color row text...}`
- color: `'0'`=红, `'1'`=绿, `'2'`=黄
- row: `'1'`~`'5'`（对应设备行 0~4）
- text: GBK 文本

**裸机行为（已确认差异）**：
- 裸机 `inbuf[3]` 做 `fontColor = inbuf[3] - 0x30 + 1`（协议值 0→设备值 1）
- STD 工程 `dev_service_show_text` 的 color 参数对齐裸机设备值

**实现**：
```c
void qh_proto_cmd_one_line(channel_t *ch, const uint8_t *data, uint8_t len)
{
    if (len < 3) return; // 最少3字节：color + row + 1字节文本

    uint8_t color = data[0] - 0x30; // '0'->0, '1'->1, '2'->2
    uint8_t row   = data[1] - 0x31; // '1'->0, ..., '5'->4

    if (color > 2 || row > 4) return;

    uint8_t mapped_color = color + 1; // 对齐裸机：0->1(红), 1->2(绿), 2->3(黄)
    dev_service_show_text(row, FONT_16, mapped_color, &data[2], len - 2);
}
```

### B.7.4 cmd '4' — 全屏可编辑显示

**协议格式**：
- 发送：`{4 len color X Y text...}`
- color: `'0'`=红, `'1'`=绿, `'2'`=黄
- X: 起始 X 坐标（0~屏幕宽度）
- Y: 起始 Y 坐标（0~屏幕高度）
- text: GBK 文本，`\n`(0x0A) 换行
- 空格(0x20) 填充空位

**实现**：逐行拆分 text（按 `\n`），每行调用 `dev_service_show_text_xy(x, y + line*row_height, color, line_text, line_len)`。

### B.7.5 cmd '6' — 固定格式显示（核心命令）

**协议格式**：
- 发送：`{6 len color type|车型|金额|余额|信息1|信息2}`
- type: `'0'`=客车, `'1'`=货车
- 字段以 `'|'`(0x7C) 分隔

**客车模式（type='0'）— 5 行**：

| 行号 | 内容 | 格式示例 |
|------|------|---------|
| 1 | 车型 | `车型:   1    型` |
| 2 | 金额 | `金额: 12.50 元` |
| 3 | 余额 | `余额: 88.00 元` |
| 4 | 信息1 | 协议传入的文本 |
| 5 | 信息2 | 协议传入的文本 |

**货车模式（type='1'）— 5 行**：

| 行号 | 内容 | 格式示例 | 条件 |
|------|------|---------|------|
| 1 | 车型 | `车型:   3    型` | 始终显示 |
| 2 | 金额 | `金额: 25.80 元` | 始终显示 |
| 3 | 余额 | `余额: 50.00 元` | 始终显示 |
| 4 | 总重 | `总重: 12.50 吨` | > 0.1 吨才显示，否则空白行 |
| 5 | 超重 | `超重:  2.30 吨` | > 0.1 吨才显示，否则空白行 |

**金额格式化**：`tmp_amount + 0.001f`（裸机对齐：补偿浮点精度误差）

**语音播报**：金额 > 0.5 元时自动播报"您好请交费XX.XX元"（通过 `qh_proto_voice_fee_amount`）。

### B.7.6 cmd '7' — 礼貌用语

**协议格式**：
- 发送：`{7 01 index}`
- index: `'0'`~`'3'`

**索引映射表**（对齐裸机 `cmd_civil_voice_play`）：

| index | GBK 文本 |
|-------|---------|
| `'0'` | `您好！欢迎行驶贵州高速公路` |
| `'1'` | `请出示通行卡` |
| `'2'` | `谢谢合作！祝您一路平安` |
| `'3'` | `交易不成功，请走人工车道` |

**注意**：协议文档写的是"欢迎行驶高速公路"，但裸机实际使用的是"贵州高速公路"。**STD 工程对齐裸机**。

### B.7.7 cmd '8' — 亮度设置

**协议格式**：
- 发送：`{8 01 level}`
- level: `'0'`~`'5'`

**映射表**（对齐裸机 `cmd_screen_bright_set`）：

| 协议值 | 含义 | 硬件级 | `setlightflag` |
|--------|------|--------|----------------|
| `'0'` | 自动 | 8（最亮） | `true`（自动模式） |
| `'1'` | 手动 1 | 3 | `false` |
| `'2'` | 手动 2 | 4 | `false` |
| `'3'` | 手动 3 | 5 | `false` |
| `'4'` | 手动 4 | 7 | `false` |
| `'5'` | 手动 5 | 8 | `false` |

**实现**：协议层做 0~5 → 硬件级映射，调用 `dev_service_set_brightness(mapped_level)`。

### B.7.8 cmd '9' — 音量设置

**协议格式**：
- 发送：`{9 01 level}`
- level: `'1'`~`'5'`

**映射表**（对齐裸机 `cmd_voice_volume_set`）：

| 协议值 | TTS 音量帧字节 |
|--------|---------------|
| `'1'` | 0x31 (`[v01]`) |
| `'2'` | 0x33 (`[v03]`) |
| `'3'` | 0x35 (`[v05]`) |
| `'4'` | 0x37 (`[v07]`) |
| `'5'` | 0x39 (`[v09]`) |

**实现**：协议层做 1~5 → 音量标签映射，调用 `dev_service_voice_volume(mapped_level)`。

### B.7.9 cmd 'A' — 外设控制

**协议格式**：
- 发送：`{A 01 ctrl}`
- ctrl: 位域控制字节

**位域定义**（对齐裸机 `cmd_ws`）：

| bit | 设备 | 1=开 | 0=关 |
|-----|------|------|------|
| bit0 | 绿灯 | 绿灯亮 | 绿灯灭 |
| bit1 | 红灯 | 红灯亮 | 红灯灭 |
| bit2 | 黄闪 | 黄闪开 | 黄闪关 |

**裸机行为**：绿灯和红灯可同时亮（位独立控制）。STD 工程中 `dev_service_lane_light(green)` 为互斥接口（绿灯优先）。对齐裸机：bit0=1 且 bit1=0 -> 绿灯；bit1=1 -> 红灯（优先）。

### B.7.10 cmd 'B' — 费额语音播报

**协议格式**：
- 发送：`{B 06 type d0 d1 d2 d3 d4}`
- type: `'0'`=播报金额, `'1'`=播报剩余次数
- d0~d4: 5 位数字（万/千/百/十/个）

**实现**：构造"您好请交费XX元" GBK 文本 -> `dev_service_voice_play`。

---

## B.8 语音子系统

### B.8.1 青海语音模式

青海协议使用**纯文本 TTS 模式**（与 CQ 的索引组合模式不同）：

| 特征 | 青海 | 重庆 |
|------|------|------|
| TTS 方式 | **GBK 文本直接合成** | 索引组合 + 分隔符 |
| 费用播报 | "您好请交费XX.XX元" | 索引组合 |
| 预置短语 | 4 条（cmd '7'） | 数十条 |

### B.8.2 费额语音文本构造

**裸机对齐**：金额 + 0.001f（浮点精度补偿），整数和小数部分分别提取。

```c
// 费额语音文本构造
void qh_proto_voice_fee_amount(float amount)
{
    if (amount < 0.5f) return; // 金额不足0.5元不播报

    uint8_t text[64];
    amount += 0.001f;
    uint32_t integer = (uint32_t)amount;
    uint32_t decimal = ((uint32_t)((amount - integer) * 100.0f)) % 100;

    // "您好请交费" + 金额 + "元"
    // ... sprintf 构造文本 ...
    dev_service_voice_play(text, text_len);
}
```

### B.8.3 TTS 帧格式（已有 dev_voice 覆盖）

TTS 语音通过 `CH_ID_RS232_1`（USART6）发送给语音控制板。帧格式：

```
FD | 00 | [Len+2] | 01 | 01 | GBK text bytes...
```

由 `dev_service_voice_play` 内部组装，协议层无需关心。

---

## B.9 设备层桥接

`app_qh_proto_cmd.c` 的职责是**将 QH 协议语义转发到 `dev_service_*`**，而非直接调用底层设备驱动。

**原则**：QH 协议层**不直接碰** `app_render` / `dev_io_lane_light` / `dev_display_set_brightness` 等底层接口，全部通过 `dev_service_*` 调用。

**桥接示例**：

```c
void qh_proto_cmd_one_line(channel_t *ch, const uint8_t *data, uint8_t len)
{
    uint8_t color = data[0] - 0x30;
    uint8_t row   = data[1] - 0x31;
    uint8_t mapped_color = color + 1; // 对齐裸机映射
    dev_service_show_text(row, FONT_16, mapped_color, &data[2], len - 2);
}

void qh_proto_cmd_peripheral(channel_t *ch, const uint8_t *data, uint8_t len)
{
    uint8_t ctrl = data[0];
    bool green = (ctrl & 0x01) && !(ctrl & 0x02); // bit0=绿, bit1=红(优先)
    bool red   = (ctrl & 0x02);
    bool yellow = (ctrl & 0x04);

    if (red) dev_service_lane_light(false);      // 红灯
    else if (green) dev_service_lane_light(true); // 绿灯

    dev_service_yellow_flash(yellow, yellow ? 0 : 0); // 0=直到手动关闭
}
```

---

## B.10 配置与默认值

| 项 | 默认值 | 落点 |
|----|--------|------|
| 串口波特率 | 9600 | `app_qh_proto_cfg` |
| 显示行数上限 | 5 | 硬编码 |
| 字号 | 16 点阵 | 硬编码（裸机固定 16 点阵） |
| 字体 | 宋体 | 硬编码（裸机默认宋体） |
| 上电欢迎语 | "祝您一路平安" | 硬编码 GBK |

---

## B.11 已确认问题汇总

| 问题 | 确认结果 |
|------|----------|
| Q1. 与 LDI 的产品关系 | **编译期互斥**，运行时只执行一种 |
| Q2. 通信通道 | **RS232 串口**（CH_ID_RS232, USART3） |
| Q3. 语音通道 | **RS232 串口**（CH_ID_RS232_1, USART6），通过 `dev_service_voice_play` |
| Q4. 字号 | **固定 16 点阵**，协议不支持字号选择 |
| Q5. 字体 | **固定宋体**，协议不支持字体选择 |
| Q6. 行号 | **1~5**（对应设备行 0~4） |
| Q7. cmd '1' 应答 | **固定返回正常**，不检查实际设备状态 |
| Q8. cmd '2' 自检 | 全屏黄色 + 语音"系统正在加电自检" |
| Q9. cmd '7' 礼貌用语 | **对齐裸机**，使用"贵州高速公路"而非协议文档的"高速公路" |
| Q10. 亮度映射 | **查表法**，裸机 `cmd_screen_bright_set` 的 switch-case |
| Q11. 音量映射 | **查表法**，裸机 `cmd_voice_volume_set` 的 switch-case |
| Q12. 外设控制 | **位独立控制**，红灯优先于绿灯（对齐裸机） |
| Q13. 费额语音 | **纯文本 TTS**，"您好请交费XX.XX元" |
| Q14. 金额精度 | **+0.001f 补偿**（裸机对齐） |
| Q15. 上电欢迎语 | **"祝您一路平安"** 显示 + 语音 |
| Q16. 帧无校验 | QH 协议**无 CRC**，probe 仅校验帧头帧尾 |
| Q17. cmd '4' X/Y 坐标 | 裸机忽略 X/Y（硬编码 0,0），STD 工程**应透传到渲染层** |
| Q18. RS232 波特率 | **9600~115200 可调**，默认 9600 |
| Q19. cmd '6' 货车总重/超重 | **0 吨不显示**（显示空白行），对齐裸机 |
| Q20. 文件名 | **`app_qh_proto_*` 样式** |

---

## B.12 风险与约束

1. **串口粘包**：RS232 是流式传输，`{` 和 `}` 可能跨多个收包周期。probe 必须支持跨包扫描。
2. **帧无校验**：QH 协议无 CRC，串口干扰可能导致误解析。考虑在 probe 中增加 cmd 字节合法性检查（`'1'~'B'`）作为弱校验。
3. **浮点运算**：cmd '6'/'B' 的金额解析使用 `strtod` / `sprintf`，需要 `-lm` 链接。FreeRTOS 堆 32KB，限制帧长。
4. **编码**：源文件中文故障串必须按 GB2312 字节写入或用 `\x` 转义。
5. **内存**：cmd '6' 的 5 行缓冲区需要 5x64=320 字节栈空间。FreeRTOS 任务栈需 >= 512 字节。
6. **裸机差异**：cmd '7' 的"贵州高速公路"与协议文档不一致，需对齐裸机行为。

---

## B.13 实施阶段与验收

### 阶段 1 — 骨架

- 目录 + Makefile 条件编译
- `app_qh_proto.c`：initcall、probe、handle_task
- RS232 通道绑定（`CH_ID_RS232`）
- 空命令表可编译

**验收**：`make PROTO=QH -j8` 通过。

### 阶段 2 — 基础命令

- cmd '1' 查询应答
- cmd '3' 单行显示
- cmd '5' 清屏
- cmd '8' 亮度
- cmd '9' 音量

**验收**：串口工具发送对应帧，屏显与裸机对照。

### 阶段 3 — 核心显示 + 语音

- cmd '6' 固定格式显示（客车/货车）
- cmd '7' 礼貌用语
- cmd 'B' 费额语音
- cmd '4' 全屏可编辑显示

**验收**：上位机发送完整费额数据，屏显 + 语音与裸机对照。

### 阶段 4 — 外设 + 收尾

- cmd 'A' 外设控制
- cmd '2' 自检
- 上电欢迎语
- LDI 不受影响回归
- 文档归档

**验收**：全部 11 条命令逐项验证通过。

---

## B.14 验收标准

| 验收项 | 标准 |
|--------|------|
| `app_qh_proto.c/h` 编译 | `make PROTO=QH -j8` 通过 |
| LDI 不受影响 | `make PROTO=LDI -j8` 编译不变 |
| CQ 不受影响 | `make PROTO=CQ -j8` 编译不变 |
| cmd '1' 查询 | 串口发送 `{1 00}`，收到 `{1 01 00}` |
| cmd '3' 单行显示 | 发送文本，对应行正确显示 |
| cmd '5' 清屏 | 发送后全屏清除 |
| cmd '6' 客车 | 费额信息正确显示 + 语音播报 |
| cmd '6' 货车 | 费额/总重/超重正确显示 + 语音播报 |
| cmd 'A' 外设 | 绿灯/红灯/黄闪正确响应 |
| 亮度 0~5 | 各级别亮度正确切换 |
| 音量 1~5 | 各级别音量正确切换 |
| 接口头文件无循环依赖 | `app_qh_proto.h` 不依赖任何协议头文件 |
