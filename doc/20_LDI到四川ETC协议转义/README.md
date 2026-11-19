# LDI → 四川 ETC 协议转义（镜像）

> 落地日期：2026-10-15 | 状态：**已实现并上机运行（镜像心跳已在双口实测确认；心跳闪屏问题已结案，见 §11）** | 开关形式：源文件排除 / 包含
> （2026-10-15 增补：周期心跳 `0A 08 0D`，3 秒定时、无条件，由 LDI 定时任务驱动）

## 1. 需求背景

费显设备现由 **LDI 协议**驱动（网口 TCP / UDP；1BH 控制查询含 E6 信息显示屏显示 / 清屏、E7 通行信号灯、
E8 报警器等模块）。现场监控设备挂在 **RS485 / RS232 总线**上（纯监听，总线上仅它一台），
且只能解析**四川 ETC 费显协议帧**（1D 文档，`0A ... 0D`）。

需求：**LDI 命令成功执行时**，把业务内容翻译为四川 ETC 帧，从 **RS485 与 RS232 两个串口发出同一份帧**
（单向，不等待应答、不占限速、不去重），供总线上那台监控设备解析。

另需**周期性心跳**：每 3 秒无条件发送一次 `0A 08 0D`（与 LDI 命令无关，命令字按现场口径 `0x08`）。

明确不做：**E9 VMS 情报板不镜像**；LDI 的应答路径、显示效果、状态上报一律不变。

## 2. 接入层级与数据流

```
LDI 帧 (网口) → app_ldi_cmd.c（1BH 解析）
                    ↓ 成功路径
            app_ldi_device.c（E6/E7/E8 设备能力层，执行硬件/渲染）
                    ↓ 成功执行后调用转义钩子（weak，见 §5）
            app_ldi_etc_bridge.c（组帧）
                    ↓ app_channel_get(CH_ID_RS485) / (CH_ID_RS232) + channel_send()
            RS485 / RS232 双口同发 ETC 帧 → 总线监听设备
```

- 钩子只在**成功路径**调用（`return LDI_DEV_OK` 之前）；失败路径（参数非法 / 无显示驱动 / 非法命令字）
  不产生任何 ETC 输出。
- 钩子由 LDI 设备能力层触发，而**不是**在 LDI 协议解析层 → LDI 协议目录无需感知转义模块的存在；
  映射信息（实际显示形态、改写后文本）在设备层已确定。
- **心跳是第二条、独立的触发路径**（不经设备能力层）：

```
ldi_timer_task()（每秒节拍，差值 ≥3000ms）
        ↓ app_ldi_etc_heartbeat()（无条件；在 LDI 通道检查之前）
app_ldi_etc_bridge.c（组帧）
        ↓ app_channel_get(CH_ID_RS485) / (CH_ID_RS232) + channel_send()
RS485 / RS232 双口同发 0A 08 0D
```

## 3. 事件 → 出站帧映射表

| LDI 事件 | 触发点（`app_ldi_device.c`） | ETC 出站帧 |
|---|---|---|
| E6 显示控制成功（font_line 1~6，实际落到该行） | `display_show()` 尾部（L257） | `0A 00 <行号> <GBK文本> 0D` |
| E6 显示控制成功（font_line=0，或指定行未落屏而按全屏渲染） | 同上 | `0A 00 00 <GBK文本> 0D` |
| E6 清屏成功（02H 功能，任意 clear_type） | `display_clear()` 尾部（L277） | `0A 00 00 20 0D` |
| E6 KeepTime 到期自动清屏 | `ldi_device_timer_poll()` 清屏分支（L396） | `0A 00 00 20 0D` |
| E7 通行信号灯 = 绿（LDI 色码 01H） | `ldi_device_lane_signal_ctrl()` 成功分支（L313） | `0A 37 0D` |
| E7 通行信号灯 = 红（02H） | 同上（L319） | `0A 36 0D` |
| E7 通行信号灯 = 黄（03H） | 同上（L325） | **不发**（无 ETC 对应帧） |
| E8 报警器显式开始（status=01H） | `ldi_device_alarm_ctrl()` 尾部（L378） | `0A 38 0D` |
| E8 报警器显式停止（status=00H） | 同上（L348） | `0A 39 0D` |
| E8 KeepTime 到期自动停止 | `ldi_device_timer_poll()` → `alarm_stop()` | **不发**（不补关帧，见 §6） |
| 周期性心跳（3 秒定时，无条件） | `ldi_timer_task()`（每秒节拍，差值 ≥3000ms 触发） | `0A 08 0D`（见 §6 第 7 条） |
| E9 VMS 情报板（显示 / 清屏） | — | **不镜像** |

出站帧字节依据四川 ETC 帧结构（`app_sc_etc_proto_parse.c` / `app_sc_etc_proto_cmd.c`）：

| 帧 | 字节 | 含义 |
|---|---|---|
| 显示 | `0A 00 <row> <data...> 0D` | row 0 = 全屏静态；row 1~6 = 单行；data 为 GBK 文本 |
| 清屏 | `0A 00 00 20 0D` | 行号 0 + 数据首字节 `0x20` = 清全屏 |
| 灯控红 | `0A 36 0D` | ETC `LIGHT_RED` |
| 灯控绿 | `0A 37 0D` | ETC `LIGHT_GREEN` |
| 报警开 | `0A 38 0D` | ETC 黄闪开（本设备由报警器 IO 承接） |
| 报警关 | `0A 39 0D` | ETC 黄闪关 |
| 心跳 | `0A 08 0D` | 3 秒周期无条件发送（现场口径命令字 `0x08`，非协议文档心跳 `0x50`） |

## 4. 出站帧字节示例

| 场景 | 输入（LDI 侧已改写） | 出站字节 |
|---|---|---|
| 全屏「显示123」 | `font_line=0`，文本 `CF D4 CA BE 31 32 33` | `0A 00 00 CF D4 CA BE 31 32 33 0D` |
| 第 3 行「AB」 | `font_line=3`，文本 `41 42` | `0A 00 03 41 42 0D` |
| 两行文本 | `font_line=0`，文本 `CF D4 CA BE` + `0A` + `CF D4 CA BE` | `0A 00 00 CF D4 CA BE 0A CF D4 CA BE 0D` |
| 清屏 | E6 02H（任意 clear_type） | `0A 00 00 20 0D` |
| 绿灯 | E7 01H | `0A 37 0D` |
| 红灯 | E7 02H | `0A 36 0D` |
| 报警开 / 关 | E8 status 01H / 00H | `0A 38 0D` / `0A 39 0D` |
| 心跳（每 3 秒，无条件） | LDI 定时任务节拍 | `0A 08 0D` |

## 5. 接入方式（弱耦合钩子）

- `Application/Inc/app_ldi_etc_bridge.h`：5 个钩子声明（LDI 侧调用、转义模块实现）：
  - `app_ldi_etc_display(uint8_t row, const uint8_t *text, uint16_t len)` — `row` 0=全屏 / 1~6=单行
  - `app_ldi_etc_clear(void)`
  - `app_ldi_etc_lamp(uint8_t color)` — LDI 色码 01 绿 / 02 红 / 03 黄
  - `app_ldi_etc_alarm(bool on)`
  - `app_ldi_etc_heartbeat(void)` — 周期心跳，**由 LDI 定时任务每 3 秒无条件调用**
    （不是事件触发钩子：无 LDI 命令、TCP 通道断开时照发；帧字节 `0A 08 0D`）
  - 契约：`text` 指针（内含换行 `0x0A`）**仅调用期间有效**，实现方须立即拷贝。
- `Application/Src/LDI/app_ldi_device.c` 内提供这 5 个函数的 **`__attribute__((weak))` 空实现**
  （L26~L52）；`Application/Src/app_ldi_etc_bridge.c` 提供强实现 → 链接期自动覆盖。
  **转义模块被 EIDE 排除时**：弱空实现生效，LDI 全部行为与应答与改动前逐条一致
  （心跳同样不发，零链接问题）。

## 6. 默认决策（实现采用，供复核）

1. **LDI 文本中的 `_` 由执行层就地改写为 `0x0A`**（既有行为，本模块不改）；转义直接透传**改写后**文本，
   不做二次转换 —— 即出站帧里的 `0x0A` 已是换行符。
2. **长度限制**：ETC 单行帧数据 ≤ 24B、全屏帧数据 ≤ 145B（帧总长上限 149B）；
   超长**截断**且保持 GBK 双字节边界不被劈开（首字节 ≥ `0x81` 视为双字节，放不下整字时回退）；
   转义时**丢弃 `0x0D` 字节**（防破坏 ETC 帧的 `0x0D` 定界）。
3. **E7 黄灯（LDI 色码 03H）无 ETC 对应帧 → 不发送**（调用点仍调用，由转义模块决定不发）。
4. **E8 报警器 KeepTime 到期自动停止 → 不补发关帧**（只镜像显式开/关命令）：
   钩子挂在 `ldi_device_alarm_ctrl()` 的成功分支，**未**挂在 `alarm_stop()` 内部。
5. **清屏统一发 ETC 清屏帧 `0A 00 00 20 0D`**：E6 02H 的任意 `clear_type`
   （00 文字清屏 / 01 全红 / 02 全绿 / 03 黄）与 KeepTime 到期清屏均为这一帧（ETC 侧无颜色清屏语义）。
6. **不做上电/切换后的显示快照补发**：只镜像钩子挂点之后发生的命令；
   设备重启、LDI 通道切换、上位机重连等不会触发补发。
7. **周期心跳 `0A 08 0D` 无条件发送**：由 `ldi_timer_task()` 的 1 秒节拍按
   `osKernelGetTickCount()` 差值 ≥ 3000ms 触发（首次约在上电后 3 秒），
   **不依赖 LDI 命令、不依赖 LDI 的 TCP 通道状态**（心跳块位于该任务的通道检查之前）。
   **命令字按现场口径取 `0x08`，与 ETC 协议文档的心跳命令 `0x50`（`0A 50 0D`）不是同一帧**
   ——本设备只按规定字节发，不解析监听侧是否应答。
   转义模块未编入时心跳随 weak 空实现一并消失（零发送）。

## 7. EIDE 开关操作（排除 / 包含）

- 开关形式 = **源文件排除 / 包含**（无编译期宏、无 initcall）。
- **默认编入**：`.eide/eide.yml` 的 `virtualFolder` → `Application` 文件列表已收录
  `Application/Src/app_ldi_etc_bridge.c`（位于 `app_default_display.c` 之后），
  **任何 target 的 `excludeList` 都不含它**。
- **关闭转义**：在 EIDE GUI 把 `Application/Src/app_ldi_etc_bridge.c` 加入当前 target 的排除表
  → 链接回退到 LDI 侧弱空实现（零输出、零告警）。
- ⚠ **改完 yml 必须立刻执行 `EIDE: Reload Project`**：EIDE 用自己的内存模型回写 `.eide/eide.yml`，
  外部编辑会被冲掉；Reload 期间不要先做任何会触发保存的 GUI 操作。
- 头文件路径 `Application/Inc` 已在两个 target 的 `incList` 内，点选后无需再加 `-I`。

## 8. 依赖（现场生效前提）

转义输出走 `app_channel_get(CH_ID_RS485 / CH_ID_RS232)`；两条串口通道**按协议绑定启动**
（`app_boot.c` 通道编排先查 `app_dispatch_channel_has_proto`，无绑定即跳过，UART RX 也不挂）。
因此现场口径必须**同时编入**：

1. **LDI 模块**（`Application/protocol/ldi`，5 个文件）——否则网口命令进不来；
2. **四川 ETC 协议模块**（`ProtocolParser_SiChuang_ETC`，3 个文件）——它绑定 RS485 + RS232，
   保证两条串口通道被启动（转义帧才发得出去）；
3. **本转义文件** `Application/Src/app_ldi_etc_bridge.c`（默认编入）；
4. 需要屏上有实际显示时再编一个显示驱动（7 选 1；不编则 E6 显示按 `LDI_DEV_FAIL` 应答，
   转义钩子也不会被调用）。

**`Debug` target 现状（2026-10-15 核对）**：编 LDI + 四川 ETC/MTC/治超 + `dev_display_1_263.c`
（`ProtocolParser_SiChuang_ETC` 未被排除；target 编入集可随时在 GUI 点选调整，以 `.eide/eide.yml` 现状为准）。

## 9. 已知限制

- **发送由 UART 层串行化（2026-10-15 起）**：`channel_send` 落 `pl_uart_send`，该函数自
  2026-10-15 起为**中断驱动发送 + 完成信号量等待**（等待期间任务阻塞让出 CPU，不再是
  3 ms 级轮询忙等），并持**每 UART 发送互斥**（`osMutex`，优先级继承）⇒ 转义帧与既有协议应答
  **不再可能在线路上交错**（后到者等待前帧发完）。本模块自身仍只用栈上数据、无全局可变状态、无锁。
- **双口顺序发送**：RS485 先、RS232 后，两次 `pl_uart_send` 串行（各自等发送完成，超时上限 100ms）；
  发送耗时随之叠加（149B @9600 ≈ 155ms/口），**LDI 处理任务在极端情况下被阻塞**（帧通常远小于上限）。
- **[预存口径，本轮未改] 长帧撞 100ms 超时**：9600 下 100ms ≈ 96B，而本模块全屏帧上限 149B、
  语音 TTS 上限 205B ⇒ 单个 >96B 帧会以 `-1` 返回且余下字节未发出（总线上是截断帧，
  接收侧无帧尾 `0x0D` 即丢弃）。改中断驱动前后行为一致（都是写到超时为止）；
  是否放宽属**调用方超时口径**，见 §11 遗留事项。
- **栈占用**：`app_ldi_etc_display` 单帧栈帧实测 **176B**（其中帧缓冲 149B，`-O3` 反汇编
  `push {r4,r5,r6,r7,lr}` + `sub sp,#156`），落在 `ldi_handle_task`（1KB，静态 CCMRAM）现有调用链之上。
- **不做去重/限速**：同一 LDI 命令重复下发即重复发送（按需求约定）。
- **心跳是常驻总线流量**：每 3 秒固定 2 帧（RS485 + RS232 各一），与 LDI 命令无关，长时间持续；
  心跳与业务帧共用同一条无锁 UART 发送路径（见上条），极端情况下两者在总线上相邻/交错出现。
- **心跳节拍受任务负载影响**：触发条件是「1 秒节拍上差值 ≥ 3000ms」，若 `ldi_timer_task`
  被长帧发送（最大 149B ≈ 155ms/口）拖慢，心跳顺延到下一个满足条件的节拍（不会连发补帧）。
- **出站不回声**：RS485 发送期间方向引脚（PA8）置位（DE 使能 / RE 关闭），本机不接收自己发出的字节；
  即便某接线形态下出现回声，`0A 08 0D` 的第二字节 `0x08` 不在四川 ETC 命令集内
  （`app_sc_etc_proto.c` probe 的 `default` 分支）→ 一字节内 FAKE 重同步，不产生显示/灯控动作。
- **文本长度以 LDI 侧为准**：LDI 自身限制每帧文本 ≤ 128B（`display_params_ok`），
  超出部分由本模块按 §6 第 2 条截断。
- **上机状态**：本模块已编入 EIDE 构建并现场运行——心跳帧 `0A 08 0D` 已在 RS485/RS232 双口
  实测确认（每 ~3.006 s 成对，见 §11）；**E6/E7/E8 命令的监听侧逐字节比对仍属现场待办**
  （真机字节流、监听设备兼容性待现场确认）。

## 10. 验证步骤

**A. 单文件编译（已完成，零错误零新增告警）**

```bash
# 从 build/Debug/compile_commands.json 提取三个源文件的编译命令，
# 去掉 -o/-MMD/输入文件后分别编译，产物落 /tmp（.o/.ci/.su 同目录，不写仓库）：
<arm-none-eabi-gcc> ... -o /tmp/Project-STD-main/hb_build/app_ldi_device.o    Application/Src/LDI/app_ldi_device.c
<arm-none-eabi-gcc> ... -o /tmp/Project-STD-main/hb_build/app_ldi_etc_bridge.o Application/Src/app_ldi_etc_bridge.c
<arm-none-eabi-gcc> ... -o /tmp/Project-STD-main/hb_build/app_ldi.o            Application/Src/LDI/app_ldi.c
```

判据：退出码 0、**输出 0 行**（无 error、无 warning）。符号核对：
`app_ldi_device.o` 中 5 个钩子为 `W`（weak），`app_ldi_etc_bridge.o` 中为 `T`（强）→ 覆盖关系成立；
`app_ldi.o` 中 `app_ldi_etc_heartbeat` 为 `U`（未定义引用，由强实现满足）。

**B. 宿主字节验证**：`/tmp/ledi_bridge_test/test_ldi_etc_bridge.py`（46 条断言 = 原 36 + 心跳新增 10，
全部通过）覆盖 §4 的帧字节、单行/全屏截断与 GBK 边界回退、`0x0D` 丢弃、换行 `0x0A` 透传、
通道为空时静默跳过、双口一致性，以及心跳帧 `0A 08 0D`（字节/帧长/双口一致/通道缺失静默跳过/
3 秒周期节拍 3000·6000·9000ms/不依赖命令与通道状态）。（脚本与输出仅存 `/tmp`，不入库。）

**C. 全项目链接（已在 EIDE 执行）**：`EIDE: Reload Project` → 核对 Application 文件列表 →
Build（判据：链接通过 + 仅 3 条既有 HAL 告警 + `.ccmram ≤ 65536`、
`.data + .bss + ._user_heap_stack ≤ 131072`）——**2026-10-15 构建通过**（段尺寸见 doc/06 §8.4.8）。

**D. 现场联调**：监听设备接 RS485/RS232，用 LDI 上位机下发 E6/E7/E8 命令，
在监听侧逐条比对 §4 的字节流；重点核对换行文本、超长文本截断、清屏与灯控。
另：静默观察 ≥10 秒，确认两条串口各自每 3 秒出现一次 `0A 08 0D`（心跳），且不被 LDI 断链影响。
（**进展**：心跳帧已在现场总线/双口实测确认（§11 结论：`[tx] ch=0/ch=1` 每 ~3.006 s 成对）；
E6/E7/E8 命令的逐字节监听侧比对仍属现场待办。）

## 11. 现场排查记录：心跳与「屏幕闪烁」（2026-10-15，**已结案：根因定位 + 修复 + 现场验证通过**）

> **根因**：`pl_uart_send` 原用 `HAL_UART_Transmit` 轮询忙等（3B@9600 ≈3.1ms/口、双口 ≈6.25ms 不让出 CPU），
> 饿死 FreeRTOS Timer 守护任务（优先级 2）；TIM3 行同步事件经该任务中转 ⇒ 发送窗口内 `scan_task` 停摆
> ≈6ms（≈12 个扫描周期），行地址/LAT 停在末行而 TIM4 的 OE 独立 PWM ⇒ 该横带被多点亮 ≈12 倍 = 「闪一下」。
> **修复**：`pl_uart_send` 改中断驱动 + 让出等待（`Platform/Src/pl_uart.c`，对外 API 与发送语义不变）。
> **验证**：修复构建烧录后 36 秒纯心跳窗口复测，闪烁消失、用户确认通过；排查期经批准的临时 RTT 探针已全部回收
> （业务代码零 `SEGGER_RTT_*`，仅 RTT 底层保留）。
> **判别依据**：心跳软件路径不写屏（仅 UART 寄存器 + PA8 BSRR）；纯心跳窗口零帧仍闪且与心跳 1:1 ⇒ 排除
> 「帧触发的显示写入路径」；闪的是本机 HUB75 模组（非对端屏）。六轮观测的原始过程记录见 git 历史（2026-11-18 前版本）
