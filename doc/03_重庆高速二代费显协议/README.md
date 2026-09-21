# 03 重庆高速二代费显协议

> 重庆高速二代费显协议族接入记录。
>
> - `PartA_STD设备层架构优化.md` — STD 设备层架构优化（协议分层 LDI/CQ/RLS）。
> - `PartB_ProtocolParser_ChongQing接入.md` — 重庆 CQ 高速二代费显协议（JSON `{` + 12B 二进制）接入记录。
> - 二进制 RLS 协议（`FF FE` 帧族）实现位于 `Application/Src/RLS/`（`app_rls.c` / `app_rls_cmd.c`）。

## 干接点车道状态显示（2026-09-09 接入）

RLS 模块新增三路干接点车道状态显示（`app_rls_cmd.c` 的 `rls_dry_contact_poll`，`app_rls.c` 的 `rls_handle_task` 队列 100ms 超时节拍驱动，**无新任务/新文件**）。

### 映射与优先级

| 干接点 | GPIO | 文本 | 颜色 |
| --- | --- | --- | --- |
| SW1 | PE12 | ETC专用 | 绿 `COLOR_GREEN` |
| SW2 | PE11 | ETC/人工 | 绿 `COLOR_GREEN` |
| SW3 | PE10 | 车道关闭 | 红 `COLOR_RED` |

低有效：按下/闭合 = 有效。**多路同时闭合**：按安全语义取优先级 车道关闭 > ETC专用 > ETC/人工，显示最高优先级文本（SW1+SW2 → ETC专用；含 SW3 的任意组合 → 车道关闭）。

### 行为

- **消抖**：`dev_key_get_state` 为直读 GPIO（无软件滤波），本模块两级采样确认——状态向量连续两拍（100ms 间隔）相同才提交，约 100ms 消抖窗口。
- **只在状态变化瞬间渲染一次**：FONT_24 全屏居中（`dev_display_get()` 屏幕尺寸自适配，UTF-8 字面量经 `FONT_ENC_UTF8` 自动转 GBK）；此后上位机 RLS bitmap 帧可正常覆盖，无变化时完全不渲染不抢屏。
- **三路全开** → 全屏填充黑（清除干接点残留显示）。
- **渲染互斥**：清屏/渲染/提交整段持 `app_scroll_render_lock`，与 app_scroll 滚动渲染串行（⑤ 渲染互斥，防混合帧）。
- 文本 `ETC专用`/`ETC/人工` 中 ASCII 部分经渲染引擎 ASCII 半宽字模路径，`/` 无需 GBK 字模。

### 待确认假设

1. GPIO↔语义映射顺序（SW1=ETC专用 / SW2=ETC/人工 / SW3=车道关闭，按用户列举顺序）。
2. 颜色默认：ETC专用/ETC/人工=绿、车道关闭=红。
3. 字号 FONT_24（协议 bitmap 为单色点阵，无既有干接点约定可循）。
4. 三路全开时的行为：全屏清黑。
5. 多路闭合优先级：车道关闭 > ETC专用 > ETC/人工。
6. 干接点仅在变化瞬间渲染一次，**不暂停/不屏蔽**协议 bitmap 帧的后续覆盖（帧到即正常显示）。
