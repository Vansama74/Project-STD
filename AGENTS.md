# 仓库规则（Project-STD）

> 本文件是 **Cursor / Claude Code / Codex / GitHub Copilot 共用的项目规则单一来源**（2026-11-19 起；
> 原 `.cursor/rules/{no-rtt-debug,no-dot-analysis,eide-only-build}.mdc` 已并入本文件）。
> 只放「违反了就出事」的硬约束；流程、参考与历史见 `doc/PROJECT_MANUAL.md`。

## ① 禁止 `.analysis/`

**不得**在仓库内创建或使用 `.analysis/` 目录存放：

- 分析报告 / 轮次记录 / 排查结论；
- 验证证据（A/B 编译产物、反汇编、段尺寸、md5、日志、截图）；
- 临时脚本（宿主推演 / 校验 / 生成器）与归档（改前源码备份、构建产物）。

`.analysis/` 已于 2026-10-13 按用户裁决**从仓库移除**（删除前最后一版完整存在于提交 `069be96`，
需要某个脚本 / 报告时用 `git show 069be96:<路径>` 或 `git checkout 069be96 -- <路径>` 取回）。

**临时产物的去处**：优先放**仓库外**（如 `%TEMP%\<任务名>\`），用完即清；构建产物只落 EIDE 输出目录
`E:\3833024\build\<项目名>\<target名>`；确需在仓库内留存时，先征得用户**明确同意**，并说明留存位置与清理方式。
文档中引用 `.analysis/**` 的段落**一律作废**（历史叙述保留但不得据此执行）。
现行验证 / 复现手段 = **EIDE** + 链接期段尺寸（`arm-none-eabi-size -A`）+ 调试器。

## ② 构建入口只允许 EIDE

**本仓库不含 `Makefile`**（2026-10-13 按用户裁决删除）。构建 / 链接 / 烧录一律用 **EIDE**
（VSCode 的 EIDE 插件：Build / Rebuild / Clean / Flash，或 `.vscode/tasks.json` 里的 `eide.project.*`）。

- **不得以任何方式重新引入 make 构建入口**：不新建 / 不恢复 `Makefile`，不新增以 `make` 为入口的脚本、
  任务或文档步骤。CubeMX 将来若再生成 `Makefile`（`Project_STD.ioc` 的 `TargetToolchain=Makefile`），
  **遇到即删除并告知用户**。
- **需要某口径时优先「按需点选」**：在日常 `Debug`（基线 `Base`）上对协议目录 / 显示驱动
  「排除 / 取消排除」；确需独立配置才新建 target（复制 `Debug` 后改 `defineList` / `excludeList`）。
  配方见 `doc/构建开关总表.md` §7。
- **改 EIDE 配置后必须执行 `EIDE: Reload Project`**：`.eide/eide.yml` 是 EIDE 的地盘，EIDE 会用自己的
  内存模型回写、冲掉外部直接编辑；外部改完 yml 后立刻 Reload，期间不要先做任何会触发保存的 GUI 操作。
- **输出目录 = `../build/<项目名>`**（相对项目根，实际落 `E:\3833024\build\<项目名>`），真实产物在
  `<outDir>/<target名>/`。**层级只能上溯一级**：写成 `../../build` 会落到 `E:\build`（2026-11-17 实际踩过）；
  **三个项目不得共用一个根目录**（EIDE 的 `.obj` 按项目内相对路径镜像，会互相覆盖）；
  outDir 必须用相对路径（绝对路径会让 `.clangd` 生成 clangd 解析不了的 `./E:/...`）。
- **烧录用 hex 在 `E:\3833024\build\` 根下**：各 target 的 `afterBuildTasks` 会在构建后把
  `build/<项目名>/<target>/<项目名>.hex` 复制成 `build/<项目名>.hex`（仅是该 target 最后一次构建的副本）；
  要确定具体 target 时仍以 `build/<项目名>/<target>/` 为准。临时 / 隔离产物放**仓库外**。

## ③ 业务代码禁止新增 RTT 调试（默认）

用户未明确要求时，**不得**在业务代码（`Application/**`、`Device/**`、`Platform/**`、`Kernel/**`、
`Core/**`、`Compiler/**` 的 .c/.h）中新增任何 RTT 调试代码，包括：

- `SEGGER_RTT_*` 调用（`SEGGER_RTT_Write` / `SEGGER_RTT_printf` 等）与 `#include "SEGGER_RTT.h"`；
- 自行封装的 `pl_rtt` 打印辅助函数、`*_RTT_DIAG` / `APP_DIAG` / `[diag]` / `[xxx]` 前缀诊断打印、
  限速告警宏等诊断设施；
- 仅为调试新增的 dump / 探针 / 自检打印函数，以及仅为调试而加的 include。

需要临时观察运行时状态时，优先改用**调试器**（Watch 窗口读全局变量、断点、`Core/Src/stm32f4xx_it.c`
的 HardFault 现场变量）；确需临时打印时，**先征得用户明确同意**，并在代码/回复中说明回退方式（改完即删或标注回退点）。

**RTT 底层属基础设施，不得删除/改动**（除非用户明确要求）：
`Middlewares/Third_Party/SEGGER_RTT/**`（三个源文件）、`Platform/Src/pl_rtt.c`、
`.eide/eide.yml` 对 RTT 三源文件与 include 路径（`Middlewares/Third_Party/SEGGER_RTT`）的收录。

## ④ 烧录纪律

- **任何烧录器（J-Link / OpenOCD / EIDE）烧完主固件后必须擦除 Sector1（`0x08004000~0x08007FFF`）恢复出厂态**：
  否则 Bootloader 条件 D（`app_info.size/crc32` 校验）失配 → 判「App 损坏」→ 设备永远进 Recovery。
  `tool/flash_all.sh` 已默认执行该纪律；手动烧录 / 单固件烧录同样遵守。
- **方案 B 布局（Sector1 记录 72B，`NetConfig_t` 含 `udp_port`）后三固件必须同步烧录**：旧 Bootloader/Recovery
  按 68B 偏移读 `config_crc` → 反复重建旧记录 → Sector1 永不稳定的同时可能误判进入 Recovery。
- `tool/flash_all.sh` 默认读 `build/Project_STD/Debug/Project_STD.hex`；**非 `Debug` target 必须显式
  `-a ../build/Project_STD/<Target>/Project_STD.hex`**，否则烧错镜像。

## ⑤ 内存分区与预算

- 分区宪法与占用账：`doc/06_SRAM内部分数据迁移/`（`02_memory_policy.md` = 分区宪法，
  `04_current_memory_occupancy.md` = 占用账）。**新增任务 / 动态对象前先按 §8 账本复算预算**。
- 链接期验收判据：`.ccmram ≤ 65536`、`.data + .bss + ._user_heap_stack ≤ 131072`；
  超区域由链接器报 `region CCMRAM overflowed by N bytes`。
- 能静态化的（全生命周期只创建一次）任务优先用 `pl_task_create_checked` + 静态栈落 CCMRAM；
  会被反复创建/退出的任务保持动态，但创建点必须判空。

## ⑥ 文档地图

| 目录 | 角色 |
|---|---|
| `doc/PROJECT_MANUAL.md` | 项目手册（构建 / 内存 / 启动 / 中断 / RTOS / 显示 / 各协议 / 网络 / 调试）——**当前态权威总入口** |
| `doc/00_变更历史.md` | 全仓历史流水归档（2026-11-27 收编；只供溯源，不作为执行依据） |
| `doc/01_显示系统` | 显示层权威：《显示系统驱动手册.md》（模组 1-260 / 1-577 / 1-543 / 1-969 / 1-263 / 22-1703 / 22-1665 + 滚动 / 自适应字号） |
| `doc/02_LDI协议与外设接口` | LDI 协议与光敏 / 字库 / 语音等外设接口 |
| `doc/03_重庆高速二代费显协议` | RLS 重庆协议接入记录 |
| `doc/04_青海高速费显协议` | 青海协议接入记录 |
| `doc/05_协议模块多协议兼容优化` | 多协议 RB / 绑定 / queue 深度权威（`01_architecture.md`） |
| `doc/06_SRAM内部分数据迁移` | 内存分区唯一权威；`04_current_memory_occupancy.md` 为占用账 |
| `doc/07_LDI与IAP配置解耦` | LDI↔IAP 配置解耦（`app_board_net_cfg`） |
| `doc/08_协议模块接入规则` | 新协议接入权威指引（规则 / 串口 / 网络 / 检查清单） |
| `doc/09_山东费显协议` | 山东协议接入记录 |
| `doc/10_贵州费显协议` | 贵州常规费显协议接入记录 |
| `doc/11_云南费显协议` | 云南常规费显协议接入记录 |
| `doc/12_缅甸费显项目适配` | 缅甸 8051→STD 替代方案设计 |
| `doc/13_安徽费显协议` | 安徽费显协议接入记录 |
| `doc/14_贵州治超屏协议` | 贵州治超 "TCLY" 帧族接入记录 |
| `doc/15_云南治超屏协议` | 云南治超 `{` 帧族接入记录 |
| `doc/16_缅甸2D费显协议` | 缅甸 2D 车道灯协议接入记录 |
| `doc/18_注释规范.md` | 注释规范 |
| `doc/19_新疆费显协议` | 新疆费额显示器协议接入记录 |
| `doc/20_LDI到四川ETC协议转义` | LDI → 四川 ETC 协议转义（镜像） |
| `doc/21_广西费显协议` | 广西费显协议接入记录 |
| `doc/构建开关总表.md` | 编译期开关总表、EIDE 口径配方（§7）与使用纪律（§8） |

## ⑦ 代码风格 / 注释

见 `doc/18_注释规范.md`（注释语言、段落规范、清理纪律）与仓库根 `.clang-format`。

## ⑧ 新协议接入

见 `doc/08_协议模块接入规则/`：`01` 接入规则、`02` 串口协议、`03` 网络协议、`04` 接入检查清单。
