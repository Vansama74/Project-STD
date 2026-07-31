# Part A：STD 设备层架构优化（青海协议适配）

**状态**：`[规划中]`
**目标**：评估青海高速费显协议对 STD 设备层的能力需求，确认现有 `dev_service_*` 接口的覆盖范围，补充青海协议特有的设备层适配细节。
**协议版本**：青海高速费显协议 2019-10-06
**参考裸机工程**：`/home/yystation/Desktop/9K1030CE00`
**协议原文**：`/home/yystation/Desktop/9K1030CE00/07协议/青海协议修改20191006.txt`
**前置条件**：CQ Part A 的 `dev_service.*` / `dev_voice.*` 已就绪（可直接复用）

---

## A.0 架构决策记录

### 方案选型：cmd `'1'`~`'B'` 的归属

| 方案 | 描述 | 优缺点 | 结论 |
|------|------|--------|------|
| **方案 A（采用）** | cmd `'1'`~`'5'`、`'8'`~`'A'` 下沉 dev 层；cmd `'6'`、`'7'`、`'B'` 留协议层 | dev 层保持协议无关，协议层处理复杂业务逻辑 | **采用** |
| 方案 B（弃用） | 所有 cmd `'1'`~`'B'` 全部下沉 dev 层 | dev 层变厚，需感知协议格式，违背硬件抽象原则 | 弃用 |

### 判定标准

- **下沉 dev 层的条件**：参数简单、格式固定、可跨协议复用、dev 层已有或可便捷新增对应接口
- **保留协议层的条件**：解析逻辑复杂（如 `\|` 分隔字段）、业务逻辑与协议强绑定（如固定格式行构造、索引映射表）

### cmd 分配总表

| cmd | 功能 | 归属 | 理由 |
|-----|------|------|------|
| `'1'` | 主机状态查询 | **dev 层** | 标准查询，所有协议都可能支持，格式差异由 dev 层条件分支处理 |
| `'2'` | 自检模式 | **dev 层** | 仅调用 `fullscreen` + `voice_play`，协议层只触发即可 |
| `'3'` | 单行显示 | **dev 层** | 参数简单（color/row/text），`dev_service_show_text` 已就绪 |
| `'4'` | 全屏可编辑显示 | **dev 层** | 参数简单（color/X/Y/text），新增 `dev_service_show_text_xy` 即可 |
| `'5'` | 清屏 | **dev 层** | `dev_service_clear_screen` 已就绪 |
| `'6'` | 固定格式显示 | **协议层** | `\|` 分隔解析 + 固定行构造 + 条件逻辑，协议强绑定 |
| `'7'` | 礼貌用语 | **协议层** | 索引→文本映射表是协议特有的 |
| `'8'` | 亮度设置 | **dev 层** | `dev_service_set_brightness` 已就绪，协议层仅做 0~5→硬件级映射 |
| `'9'` | 音量设置 | **dev 层** | `dev_service_voice_volume` 已就绪 |
| `'A'` | 外设控制 | **dev 层** | `dev_service_lane_light` + `dev_service_yellow_flash` 已就绪，协议层仅解析 bit 位 |
| `'B'` | 费额语音 | **协议层** | 金额→文本构造逻辑是协议特有的 |

---

## A.1 青海协议功能全景——分类总表

> **分类原则**：
> - **dev 层命令**：参数简单、格式固定，dev 层提供统一入口，协议层仅做参数解析后调用 `dev_service_*()`。
> - **协议层命令**：解析逻辑复杂或业务逻辑与协议强绑定，协议层处理完毕后调用 `dev_service_*()` 完成硬件操作。
> - **基础能力**：与具体协议无关的硬件抽象，已有 `dev_service_*` 覆盖。

### A.1.1 全部功能清单

| 序号 | 功能描述 | 分类 | 实现位置 | 说明 |
|------|----------|------|----------|------|
| F01 | ASCII 帧定界 `{`...`}` 扫描 | **基础能力** | `app_qh_proto.c` probe | 青海特有的 `{cmd len data}` 帧格式 |
| F02 | cmd 字节查表分发 `'1'~`'B'` | **基础能力** | `app_qh_proto_cmd.c` | ASCII 命令字分发表 |
| F03 | 主机状态查询 | **dev 层** | `dev_service.c` | 新增 `dev_service_host_status_query()`，根据激活协议返回相应格式 |
| F04 | 单行显示：颜色 + 行号 + GBK 文本 | **dev 层** | `dev_service.c` | 已有 `dev_service_show_text()`，协议层解析参数后调用 |
| F05 | 全屏可编辑显示：颜色 + X/Y + 文本 | **dev 层** | `dev_service.c` | 新增 `dev_service_show_text_xy()`，协议层解析参数后调用 |
| F06 | 全屏清除 | **dev 层** | `dev_service.c` | 已有 `dev_service_clear_screen()` |
| F07 | 固定格式显示（客车）：5 行中文 + 费额语音 | **协议层** | `app_qh_proto_cmd.c` | 协议层解析 `\|` 分隔字段，构造固定格式行，调用 `dev_service_show_text` |
| F08 | 固定格式显示（货车）：5 行中文 + 费额语音 | **协议层** | `app_qh_proto_cmd.c` | 同上，增加总重/超重行，0 吨不显示 |
| F09 | 礼貌用语索引→GBK 文本映射 | **协议层** | `app_qh_proto_voice.c` | 协议层完成索引→文本映射，调用 `dev_service_voice_play` |
| F10 | 亮度设置：协议 0~5 → 硬件 8 级映射 | **dev 层** | `dev_service.c` | 已有 `dev_service_set_brightness()`，协议层做映射后调用 |
| F11 | 音量设置：协议 1~5 | **dev 层** | `dev_service.c` | 已有 `dev_service_voice_volume()`，协议层直传 |
| F12 | 外设控制：bit0=绿灯, bit1=红灯, bit2=黄闪 | **dev 层** | `dev_service.c` | 已有 `dev_service_lane_light` + `dev_service_yellow_flash`，协议层解析 bit 位后调用 |
| F13 | 费额语音：数字→金额文本→TTS 播报 | **协议层** | `app_qh_proto_voice.c` | 协议层构造"您好请交费XX.XX元"文本，调用 `dev_service_voice_play` |
| F14 | 自检模式：固定显示 + 语音 | **dev 层** | `dev_service.c` | 已有 `dev_service_fullscreen` + `dev_service_voice_play`，协议层触发即可 |
| F15 | 上电欢迎语："祝您一路平安" + 语音 | **协议层** | `app_qh_proto.c` (init) | 协议层构造欢迎语文本，调用 `dev_service_show_text` + `dev_service_voice_play` |
| F16 | 串口 RS232 收发（`CH_ID_RS232`） | **基础能力** | 已有 `app_rs232.c` | 通道已实现，青海绑定即可 |
| F17 | 文字渲染（GBK→点阵→屏显） | **基础能力** | 已有 `app_render` | 16 点阵单行/多行显示 |
| F18 | 全屏颜色填充 | **基础能力** | 已有 `dev_service_fullscreen` | CQ PartA 已定义 |
| F19 | 清屏 | **基础能力** | 已有 `dev_service_clear_screen` | CQ PartA 已定义 |
| F20 | 亮度调节（自动/手动） | **基础能力** | 已有 `dev_service_set_brightness` | CQ PartA 已定义 |
| F21 | TTS 语音播报（GBK 文本） | **基础能力** | 已有 `dev_service_voice_play` | CQ PartA 已定义 |
| F22 | TTS 音量设置 | **基础能力** | 已有 `dev_service_voice_volume` | CQ PartA 已定义 |
| F23 | 车道灯 GPIO 控制 | **基础能力** | 已有 `dev_service_lane_light` | CQ PartA 已定义 |
| F24 | 黄闪报警控制 | **基础能力** | 已有 `dev_service_yellow_flash` | CQ PartA 已定义 |

### A.1.2 分类统计

| 分类 | 数量 | 说明 |
|------|------|------|
| **dev 层命令** | 8 (F03~F06, F10~F12, F14) | 协议层仅解析参数，调用 `dev_service_*()` |
| **协议层命令** | 5 (F07~F09, F13, F15) | 协议层处理复杂业务逻辑，调用 `dev_service_*()` 完成硬件操作 |
| **基础能力** | 11 (F01~F02, F16~F24) | 已有 `dev_service_*` / `app_render` / 通道框架 |
| **需新增 dev 层接口** | 2 | `dev_service_host_status_query()` + `dev_service_show_text_xy()` |

**核心结论：需新增 2 个 dev 层接口，其余均由现有 `dev_service_*` 覆盖。协议层保留 5 个功能处理复杂业务逻辑。**

---

## A.2 青海协议对设备层的调用矩阵

### A.2.1 各命令 → 归属与调用映射

#### dev 层命令（协议层仅解析参数，调用 dev 层）

| cmd | ASCII | 协议层动作 | dev 层调用 | 备注 |
|-----|-------|-----------|-----------|------|
| `'1'` | 0x31 | 无（由 dev 层统一处理） | `dev_service_host_status_query()` | dev 层根据激活协议返回相应格式 |
| `'2'` | 0x32 | 无（由 dev 层统一处理） | `dev_service_fullscreen(YELLOW)` + `dev_service_voice_play("自检", 2)` | dev 层自检入口 |
| `'3'` | 0x33 | 解析 color(0~2) + row(1~5) + text | `dev_service_show_text(row-1, FONT_16, color, text, len)` | 单行显示 |
| `'4'` | 0x34 | 解析 color(0~2) + X + Y + text | `dev_service_show_text_xy(x, y, FONT_16, color, text, len)` | 全屏可编辑显示 |
| `'5'` | 0x35 | 无参数 | `dev_service_clear_screen()` | 清屏 |
| `'8'` | 0x38 | 映射 0~5→硬件级 | `dev_service_set_brightness(mapped_level)` | 需查表映射 |
| `'9'` | 0x39 | 直传 1~5 | `dev_service_voice_volume(level)` | 音量 |
| `'A'` | 0x41 | 解析 bit0/bit1/bit2 | `dev_service_lane_light(green)` + `dev_service_yellow_flash(on, 0)` | 外设 |

#### 协议层命令（协议层处理业务逻辑，调用 dev 层完成硬件操作）

| cmd | ASCII | 协议层动作 | dev 层调用 | 备注 |
|-----|-------|-----------|-----------|------|
| `'6'` | 0x36 | 解析 `\|` 分隔字段 + 构造固定格式行 | `dev_service_show_text(row, FONT_16, color, line, len)` x5 行 | 最复杂命令，协议层全权处理 |
| `'6'` | 0x36 | 同上 + 金额 > 0 时 | `dev_service_voice_play(text, len)` | 语音播报 |
| `'7'` | 0x37 | 索引→文本映射 | `dev_service_voice_play(text, len)` | 礼貌用语 |
| `'B'` | 0x42 | 解析 type + 5 位数字 + 构造语音文本 | `dev_service_voice_play(text, len)` | 费额语音 |

### A.2.2 与 CQ 协议的设备层复用对比

| dev 层接口 | 青海 QH | 重庆 CQ | LDI | 复用说明 |
|-----------|---------|---------|-----|---------|
| `dev_service_host_status_query` | cmd '1' | — | — | **新增：青海特有** |
| `dev_service_show_text` | cmd '3'/'6'/'7' | text1 | 文字显示 | 完全复用 |
| `dev_service_show_text_xy` | cmd '4' | — | — | **新增：青海特有** |
| `dev_service_clear_screen` | cmd '5' | text1(scr=0) | — | 完全复用 |
| `dev_service_fullscreen` | cmd '2' 自检 | full | 上电/故障 | 完全复用 |
| `dev_service_set_brightness` | cmd '8'（查表映射） | light1（直传 0~15） | 亮度调节 | 接口复用，映射逻辑不同 |
| `dev_service_voice_play` | cmd '2'/'6'/'7'/'B' | voice1 | — | 完全复用 |
| `dev_service_voice_volume` | cmd '9' | voice_control1 | — | 完全复用 |
| `dev_service_lane_light` | cmd 'A' | tra1 | 通行灯 | 编译期选后端 |
| `dev_service_yellow_flash` | cmd 'A' | warn1 | 黄闪 | 完全复用 |
| `channel_send(RS232)` | cmd '1' 应答 | — | — | **青海特有：串口回发应答帧** |

---

## A.3 设备层缺口分析

### A.3.1 已确认无缺口

| 能力 | 状态 | 说明 |
|------|------|------|
| 文字显示（单行/多行） | **已覆盖** | `dev_service_show_text` 已支持 line + font_size + color + GBK text |
| 清屏 | **已覆盖** | `dev_service_clear_screen` |
| 全屏颜色 | **已覆盖** | `dev_service_fullscreen` |
| 亮度调节 | **已覆盖** | `dev_service_set_brightness`（含自动模式） |
| TTS 语音播报 | **已覆盖** | `dev_service_voice_play` |
| TTS 音量设置 | **已覆盖** | `dev_service_voice_volume` |
| 车道灯控制 | **已覆盖** | `dev_service_lane_light`（GPIO 后端） |
| 黄闪报警 | **已覆盖** | `dev_service_yellow_flash` |
| RS232 串口收发 | **已覆盖** | `app_rs232.c` + `CH_ID_RS232` |

### A.3.2 需新增的 dev 层接口

| 序号 | 接口 | 用途 | 说明 |
|------|------|------|------|
| N1 | `dev_service_host_status_query()` | cmd `'1'` 主机状态查询 | 设备层统一入口，根据激活协议返回相应格式（青海：`{1 01 00}`） |
| N2 | `dev_service_show_text_xy()` | cmd `'4'` 全屏可编辑显示 | X/Y 坐标版本，内部调用 `app_render` 已有的 `render_cfg_t.x, y` |

### A.3.3 需关注的适配点（非缺口，但需验证）

| 序号 | 适配点 | 详情 | 风 |
|------|--------|------|------|
| M1 | 亮度映射表差异 | QH 协议 0~5 映射到硬件 8 级（查表法：0=自动, 1→3, 2→4, 3→5, 4→7, 5→8），CQ 协议 0~15 直传。`dev_service_set_brightness` 已支持 level 0~15，QH 协议层在调用前自行做 0~5→硬件级 的映射即可。 | 无 |
| M2 | 串口应答帧发送 | cmd `'1'` 需要通过 RS232 串口回发 `{1 01 00}` 应答帧。`channel_send(ch, data, len)` 已支持。 | 无 |
| M3 | 费额语音格式 | QH 协议的费额语音为"您好请交费XX.XX元"纯文本方式（TTS 合成），不同于 CQ 的索引组合方式。`dev_service_voice_play` 发送 GBK 文本即可满足。 | 无 |

---

## A.4 设备层架构

### A.4.1 分层架构

```
+--------------------------------------------------+
|  Protocol Layer (LDI / CQ / RLS / QH)             |
|  职责：帧解析 + 命令分发 + 协议特有的业务逻辑        |
|  例如：cmd '6' \| 分隔解析、cmd '7' 索引映射、      |
|       cmd 'B' 金额文本构造、cmd '8' 亮度映射、      |
|       cmd 'A' bit 位解析                           |
|  调用：dev_service_*() 高层接口                     |
+--------------------------------------------------+
|  Service Layer (dev_service.c / dev_service.h)    |  <-- CQ PartA 已建立
|  职责：协议无关的业务能力封装                       |
|  新增：dev_service_host_status_query()            |
|  新增：dev_service_show_text_xy()                 |
|  已有：fullscreen / clear_screen / show_text /     |
|        set_brightness / voice_play / voice_volume / |
|        lane_light / yellow_flash / reset           |
+--------------------------------------------------+
|  Device Layer                                      |
|  app_render / dev_display / dev_io_ctrl /          |
|  dev_voice / pl_net / pl_system                    |
+--------------------------------------------------+
```

### A.4.2 青海协议在分层中的位置

```
串口 RS232 (CH_ID_RS232)
  -> app_channel_dispatch
  -> ring_buffer -> frame_dispatch
  -> qh_proto_probe_frame (帧定界: '{' ... '}')
  -> qh_proto_handle_task (命令分发)
  |
  |--- dev 层命令（协议层仅解析参数，调用 dev 层）---
  |    cmd '1' -> dev_service_host_status_query()     <-- 主机状态查询
  |    cmd '2' -> dev_service_fullscreen(YELLOW)      <-- 自检
  |              + dev_service_voice_play("自检", 2)
  |    cmd '3' -> dev_service_show_text(...)          <-- 单行显示
  |    cmd '4' -> dev_service_show_text_xy(...)       <-- 全屏可编辑显示
  |    cmd '5' -> dev_service_clear_screen()          <-- 清屏
  |    cmd '8' -> dev_service_set_brightness(level)   <-- 亮度（映射后调用）
  |    cmd '9' -> dev_service_voice_volume(level)     <-- 音量
  |    cmd 'A' -> dev_service_lane_light(green)       <-- 外设
  |              + dev_service_yellow_flash(on, 0)
  |
  |--- 协议层命令（协议层处理业务逻辑，调用 dev 层完成硬件操作）---
  |    cmd '6' -> 解析 \| 分隔字段 + 构造固定格式行
  |              -> dev_service_show_text(...) x5 行   <-- 固定格式显示
  |              -> dev_service_voice_play(...)        <-- 语音播报（金额>0）
  |    cmd '7' -> 索引→文本映射
  |              -> dev_service_voice_play(...)        <-- 礼貌用语
  |    cmd 'B' -> 解析 type + 数字 + 构造语音文本
  |              -> dev_service_voice_play(...)        <-- 费额语音
```

### A.4.3 dev_service 接口完整清单

```c
/* ==================== 显示服务 ==================== */

/** 全屏填充颜色（协议无关）
 *  @param color  颜色索引：0=红, 1=绿, 2=黄, 3=灭/黑
 */
void dev_service_fullscreen(uint8_t color);

/** 清屏 */
void dev_service_clear_screen(void);

/** 显示文字（指定行、字号、颜色、GBK文本） */
void dev_service_show_text(uint8_t line, uint8_t font_size,
                           uint8_t color, const uint8_t *gbk_text, uint16_t len);

/** 显示文字（X/Y 坐标版本，用于全屏可编辑显示）
 *  @param x  X 坐标（像素）
 *  @param y  Y 坐标（像素）
 */
void dev_service_show_text_xy(uint8_t x, uint8_t y, uint8_t font_size,
                              uint8_t color, const uint8_t *gbk_text, uint16_t len);

/* ==================== 通行灯服务 ==================== */

/** 设置通行灯
 *  @param green  true=绿灯, false=红灯
 */
void dev_service_lane_light(bool green);

/** 黄闪报警控制
 *  @param enable  true=开启, false=关闭
 *  @param seconds 持续时间（秒），0=直到手动关闭
 */
void dev_service_yellow_flash(bool enable, uint16_t seconds);

/* ==================== 亮度服务 ==================== */

/** 设置屏幕亮度
 *  @param level  硬件级 0~15
 *    - level == 0：自动亮度模式（启用光敏传感器）
 *    - level 1~15：手动亮度
 */
void dev_service_set_brightness(uint8_t level);

/* ==================== 语音服务 ==================== */

/** TTS 合成播放（GBK 文本） */
void dev_service_voice_play(const uint8_t *gbk_text, uint16_t len);

/** TTS 设置音量（0~15） */
void dev_service_voice_volume(uint8_t level);

/* ==================== 系统服务 ==================== */

/** 系统复位（延时指定毫秒后复位） */
void dev_service_reset(uint32_t delay_ms);

/* ==================== 主机状态查询服务 ==================== */

/** 主机状态查询（协议无关）
 *  设备层统一入口，根据激活协议返回相应格式
 *  - 青海协议：返回 {1 01 00} 固定帧
 *  - 其他协议：根据协议格式返回相应数据
 *  - 扩展性：后续协议只需在 dev 层创建返回数据格式类型即可
 */
void dev_service_host_status_query(void);
```

---

## A.5 青海协议特有的适配细节

### A.5.1 dev 层命令适配

#### 主机状态查询（dev 层实现，协议层注册）

`dev_service_host_status_query()` 为 dev 层新增接口，统一处理主机状态查询（cmd `'1'`）。

| 原则 | 说明 |
|------|------|
| dev 层统一入口 | PC 指针进入 dev 层主机状态查询后，由 dev 层寻找当前激活的协议 |
| 协议格式无关 | dev 层只负责调度，返回数据格式由各协议自行定义 |
| 格式相同时可合并 | 若多个协议的返回格式相同，可在 dev 层判断激活协议时用"或"逻辑合并 |
| 格式不同时独立 | 若协议格式不同，只需在 dev 层中创建对应的返回数据格式类型 |

**青海协议返回帧格式**：

| 字段 | 长度 | 值 | 说明 |
|------|------|-----|------|
| `{` | 1 | 0x7B | 帧起始符 |
| cmd | 1 | `'1'` (0x31) | 命令字 |
| 数据 | 2 | `01 00` | 固定应答数据 |
| `}` | 1 | 0x7D | 帧结束符 |

完整帧：`{1 01 00}`，共 6 字节 ASCII。

**实现机制**：

```c
void dev_service_host_status_query(void) {
    #if defined(PROTO_QINGHAI)
        // 青海协议：返回 {1 01 00} 固定帧
        uint8_t rsp[] = "{1 01 00}";
        channel_send(CH_ID_RS232, rsp, sizeof(rsp) - 1);
    #elif defined(PROTO_XXX)
        // 其他协议（格式不同）：创建新的返回数据格式类型
        host_status_rsp_xxx_t xxx_rsp;
        // ... 构造 xxx_rsp
        channel_send(CH_ID_RS232, &xxx_rsp, sizeof(xxx_rsp));
    #endif
}
```

**扩展指南**：

1. **若返回格式与青海相同**：在 `dev_service_host_status_query()` 中用"或"逻辑合并（如 `#if defined(PROTO_QINGHAI) || defined(PROTO_XXX)`），无需新增代码。
2. **若返回格式不同**：在 dev 层中新增对应的返回数据格式类型（如 `host_status_rsp_xxx_t`），并在 `dev_service_host_status_query()` 中增加条件分支处理。

#### 自检模式（dev 层实现）

cmd `'2'` 为 dev 层统一入口，协议层无需处理。

**实现机制**：

```c
void dev_service_self_test(void) {
    dev_service_fullscreen(COLOR_YELLOW);           // 全屏黄色
    dev_service_voice_play("自检", 2);              // TTS 播报
}
```

#### 全屏可编辑显示（dev 层新增接口）

cmd `'4'` 新增 `dev_service_show_text_xy()`，内部调用 `app_render` 已有的 `render_cfg_t.x, y`。

**实现机制**：

```c
void dev_service_show_text_xy(uint8_t x, uint8_t y, uint8_t font_size,
                              uint8_t color, const uint8_t *gbk_text, uint16_t len) {
    render_cfg_t cfg;
    cfg.x = x;
    cfg.y = y;
    cfg.font_size = font_size;
    cfg.color = color;
    render_text(&cfg, gbk_text, len);
}
```

#### 亮度映射表（协议层实现，非 dev 层）

| 协议值 (data[0]) | 含义 | 硬件级 (level) | 说明 |
|------------------|------|---------------|------|
| `'0'` (0x30) | 自动 | 0 | 自动亮度模式 |
| `'1'` (0x31) | 最暗 | 3 | |
| `'2'` (0x32) | 次暗 | 4 | |
| `'3'` (0x33) | 中等 | 5 | |
| `'4'` (0x34) | 较亮 | 7 | |
| `'5'` (0x35) | 最亮 | 8 | |

裸机代码对齐：`cmd_screen_bright_set` 中的 switch-case 映射。

#### 颜色映射（协议层实现）

| 协议值 | 颜色 | dev_service 索引 |
|--------|------|-----------------|
| `'0'` (0x30) | 红色 | 0 |
| `'1'` (0x31) | 绿色 | 1 |
| `'2'` (0x32) | 黄色 | 2 |

#### 外设控制位映射（协议层实现）

| bit | 设备 | 控制逻辑 |
|-----|------|---------|
| bit0 | 绿灯 | 1=开, 0=关 |
| bit1 | 红灯 | 1=开, 0=关 |
| bit2 | 黄闪 | 1=开, 0=关 |

裸机行为：绿灯和红灯可同时亮（位独立控制），但 STD 工程中 `dev_service_lane_light(green)` 为互斥接口（绿灯优先）。对齐裸机：bit0=1 且 bit1=0 -> 绿灯；bit1=1 -> 红灯（优先）；bit0=1 且 bit1=1 -> 红灯优先（与裸机一致）。

### A.5.2 协议层命令适配

#### 固定格式显示 cmd `'6'`（协议层全权处理）

协议层负责 `\|` 分隔字段解析、固定格式行构造、条件逻辑，最终调用 `dev_service_show_text` 显示 5 行 + `dev_service_voice_play` 播报语音。

**协议层处理流程**：

```c
void qh_proto_cmd_fixed_display(uint8_t *data, uint16_t len) {
    // 1. 解析 \| 分隔字段（青海特有）
    char *fields[10];
    parse_delimited_fields(data, len, '|', fields);

    // 2. 构造固定格式行（青海特有）
    char line1[32], line2[32], line3[32], line4[32], line5[32];
    snprintf(line1, sizeof(line1), "车型:%s型", fields[0]);
    snprintf(line2, sizeof(line2), "金额:%s元", fields[1]);
    // ... 构造其他行

    // 3. 条件逻辑（青海特有）
    uint16_t weight = atoi(fields[2]);
    if (weight == 0) {
        // 0 吨不显示超重行
        line4[0] = '\0';
    }

    // 4. 调用 dev 层通用接口
    uint8_t color = parse_color(fields[0]);
    dev_service_show_text(0, FONT_16, color, line1, strlen(line1));
    dev_service_show_text(1, FONT_16, color, line2, strlen(line2));
    dev_service_show_text(2, FONT_16, color, line3, strlen(line3));
    dev_service_show_text(3, FONT_16, color, line4, strlen(line4));
    dev_service_show_text(4, FONT_16, color, line5, strlen(line5));

    // 5. 语音播报（金额 > 0）
    uint16_t amount = atoi(fields[1]);
    if (amount > 0) {
        char voice_text[64];
        snprintf(voice_text, sizeof(voice_text), "您好请交费%u.%02u元",
                 amount / 100, amount % 100);
        dev_service_voice_play(voice_text, strlen(voice_text));
    }
}
```

#### 礼貌用语 cmd `'7'`（协议层全权处理）

协议层负责索引→文本映射，最终调用 `dev_service_voice_play`。

**协议层处理流程**：

```c
// 青海特有的 4 条索引映射
static const char *qh_polite_phrases[] = {
    "您好",           // 索引 0
    "请交费",         // 索引 1
    "谢谢",           // 索引 2
    "祝您一路平安",    // 索引 3
};

void qh_proto_cmd_polite_phrase(uint8_t index) {
    if (index < sizeof(qh_polite_phrases) / sizeof(qh_polite_phrases[0])) {
        const char *text = qh_polite_phrases[index];
        dev_service_voice_play(text, strlen(text));
    }
}
```

#### 费额语音 cmd `'B'`（协议层全权处理）

协议层负责解析 type + 5 位数字，构造"您好请交费XX.XX元"文本，最终调用 `dev_service_voice_play`。

**协议层处理流程**：

```c
void qh_proto_cmd_fee_voice(uint8_t type, uint8_t *digits, uint8_t len) {
    // 1. 解析 5 位数字为金额（分）
    uint32_t amount = 0;
    for (int i = 0; i < len; i++) {
        amount = amount * 10 + (digits[i] - '0');
    }

    // 2. 构造语音文本（青海特有格式）
    char voice_text[64];
    snprintf(voice_text, sizeof(voice_text), "您好请交费%u.%02u元",
             amount / 100, amount % 100);

    // 3. 调用 dev 层通用接口
    dev_service_voice_play(voice_text, strlen(voice_text));
}
```

---

## A.6 编译期配置

### A.6.1 Makefile 条件编译

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

**与 LDI / CQ 编译期互斥**，运行时只执行一种协议。

### A.6.2 编译验证

| 配置 | 命令 | 预期 |
|------|------|------|
| 青海 | `make PROTO=QH -j8` | 编译通过 |
| 重庆 | `make PROTO=CQ -j8` | 编译通过（不受 QH 影响） |
| LDI | `make PROTO=LDI -j8` | 编译通过（不受 QH 影响） |

---

## A.7 设备层验收标准（Part A 范围）

| 验收项 | 标准 |
|--------|------|
| dev_service.c/h 编译 | `make PROTO=QH -j8` 通过 |
| LDI 不受影响 | `make PROTO=LDI -j8` 编译不变 |
| CQ 不受影响 | `make PROTO=CQ -j8` 编译不变 |
| 接口头文件无循环依赖 | `dev_service.h` 不依赖任何协议头文件 |
| RS232 通道绑定 | `app_rs232.c` 编译通过，`CH_ID_RS232` 可用 |
| 新增接口可调用 | `dev_service_host_status_query()` + `dev_service_show_text_xy()` 可被协议层调用 |

---

## A.8 总结

| 项目 | 结论 |
|------|------|
| dev 层需新增接口 | **是** -- `dev_service_host_status_query()` + `dev_service_show_text_xy()`（共 2 个） |
| dev 层已有接口可复用 | **是** -- `fullscreen` / `clear_screen` / `show_text` / `set_brightness` / `voice_play` / `voice_volume` / `lane_light` / `yellow_flash` / `reset`（共 9 个） |
| 是否需要新增 dev_voice 接口 | **否** -- 青海使用纯文本 TTS，`dev_service_voice_play` 即可 |
| 是否需要新增通道 | **否** -- RS232 通道已有 |
| 是否需要新增硬件驱动 | **否** -- 所有硬件驱动已就绪 |
| 协议层保留功能 | **5 个** -- 帧扫描、命令分发、固定格式显示(cmd `'6'`)、礼貌用语(cmd `'7'`)、费额语音(cmd `'B'`) |
| Part A 工作量 | **小** -- 新增 2 个 dev 层接口 + 编译期切换验证 |
| 重点 | **dev 层新增主机状态查询 + X/Y 坐标显示接口；协议层保留复杂业务逻辑；全部工作量在 Part B（协议解析器）** |
