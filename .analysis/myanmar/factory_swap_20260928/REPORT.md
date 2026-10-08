# 2026-09-28 缅甸轮：TEST 键切换 0x0A/0x0B 箭头落点 + 缅甸版出厂测试（证据归档）

> 模块：`Application/{Src,Inc}/ProtocolParser_Myanmar/` + 新增 `Application/Src/app_factory_test_mm.c`
> 接入记录：`doc/16_缅甸2D费显协议/README.md` §13（`§3/§7.1/§9.1/§12` 同轮同步）
> 构建开关登记：`doc/构建开关总表.md` §3/§4/§5

## 1. 需求

1. 在使用缅甸协议解析的构建里，**TEST 键（PD8）每按一次切换 `0x0A` / `0x0B` 两条命令的目标灯**，可来回：
   默认态 `0x0A→左下 PE11` / `0x0B→右下 PE10`；按下后互换；再按回默认。
2. **红灯 `0x90`/PE12 不受影响**；只交换 0x0A 与 0x0B 的落点。
3. **不做掉电保存**（每次开机回默认态）。
4. 新增**缅甸版出厂测试文件**（`app_factory_test_mm.c`），与标准版 `app_factory_test.c`
   **二选一编译**（EIDE `excludeList` 选择；同编即重复符号 = 天然守卫）。
5. **不改**开机默认灯态（「开机默认红叉」留待后续）、不动极性、不动 PE10/11/12 接管/EXTI。

## 2. 实现（单一生效点）

| 层 | 位置 | 是否受交换影响 |
|---|---|---|
| probe 判据 / 抗误吞正签名 | `app_mm_proto.c` `mm_probe_frame()`（**本文件本轮零改动**，mtime 2026-09-24 17:04） | **否** |
| 帧解析（纯函数） | `mm_parse_frame()` | **否**（A/B 反汇编逐条一致） |
| 队列 / 帧处理任务 | `mm_proto_handle_task()` | **否** |
| **命令 → 灯位** | `mm_execute_cmd()` → `_mm_effective_lamp()`（新增，28B） | **唯一生效点** |
| 灯位 → 引脚 / 极性 | `s_mm_lamp_map` / `_mm_drive()` | **否**（A/B 反汇编逐条一致） |
| `[mm]` 执行日志 | `mm_apply_lamp()` | 按最终灯位取表 ⇒ 如实反映实际灯/引脚 |

新增公开 API（`app_mm_proto.h`）：`mm_arrow_map_toggle()`（返回新状态 + 一行 RTT，`MM_RTT_DIAG` 门控）、
`mm_arrow_map_is_swapped()`；开关 `s_mm_arrow_swapped`（volatile bool，1B bss）；
`mm_apply_init()` **开机复位为默认态**（不持久化：cmd 文件无任何存储设备调用，宿主脚本断言）。

## 3. 构建验证（`TMPDIR=/home/yystation/tmp-mm`，`PROTO=ALL DISP=1_263`，全量 `-B -j8`）

| 口径 | .text | .rodata | .data | .ccmram | .bss | heap_stack | SRAM 合计 / 余量 |
|---|---|---|---|---|---|---|---|
| `FACTORY=STD`（默认） | 176336 | 203648 | 1656 | 39652 | 124860 | 2564 | 129080 / **余 1992B** |
| `FACTORY=MM` | 174944 | 203616 | 1656 | 39652 | 124596 | 2564 | 128816 / **余 2256B** |

- 两口径各 242 个编译单元 + 1 次链接，`Build complete`；**零新增告警**（仍 3 条既有 HAL
  `stm32f4xx_hal_flash_ex.c -Wunused-parameter`）。
- 无显示等价口径（`Makefile.mm_nodisp` 按新 yml 重生成，`CONFIG=EIDEMyanmarNoDisp`，207 文件）：
  `.text 137328 / .rodata 196968 / .data 1584 / .ccmram 16084 / .bss 115796 / heap_stack 2564`
  ⇒ SRAM 余 11128B、CCM 余 49452B；链接通过、零新增告警（证明**缅甸版出厂测试在无显示构建下安全**）。
- 口径指纹：`build/Debug/.build_stamp` 含 `FACTORY=MM` / `FACTORY=STD`；**不传 `-B` 切口径实测
  242 编译 + 1 链接（全量重编 + 重链接）**；同口径重复 `make` = **0 编译 0 链接**。

## 4. 互斥验证（天然守卫）

```
arm-none-eabi-ld -r build/Debug/Application/Src/app_factory_test.o \
                    build/Debug/Application/Src/app_factory_test_mm.o -o /tmp/both_factory.o
→ exit=1
  multiple definition of `g_factory_test'
  multiple definition of `app_factory_mode_interrupt'
  multiple definition of `app_factory_test_active'
```

（完整输出见 `ld_mutual_exclusion.txt`；两对象的导出符号集**完全相同**：
`nm -g --defined-only` 均为上表三项 —— 保证调用方 `app_dispatch.c` / `app_cq_proto.c` 两口径都可链接。）

**非法值守卫**：`make FACTORY=BOGUS -n` → `Makefile:121: *** FACTORY must be STD or MM, got 'BOGUS'。`

## 5. 驱动级 A/B（同树摘除交换代码复编，`-Og` 同 flags）

- `.text._mm_effective_lamp 28` / `.text.mm_arrow_map_is_swapped 12` / `.text.mm_arrow_map_toggle 168`
  / `.rodata.mm_arrow_map_toggle.str1.4 78` / `.bss.s_mm_arrow_swapped 1`（新增）；
  `mm_execute_cmd` 22 → 28（+6）、`mm_apply_init` 96 → 104（+8）。
- **`mm_parse_frame` / `mm_apply_lamp` / `_mm_drive` 反汇编逐条一致（0 行差异）** ⇒ 交换不触碰
  解析层与映射层（证据 `disasm_and_ab.txt`）。
- 改写前源码重建件：`app_mm_proto_cmd_pre_swap_reconstructed.c`（256 行；现行 318 行）。
- 链接后（STD 构建）：`.text +48 / .rodata +8`（`toggle` 与日志串被 `--gc-sections` 丢弃；
  唯一进入标准构建热路径的是 `_mm_effective_lamp` + 复位语句）。

## 6. 脚本

| 脚本 | 结果 |
|---|---|
| `.analysis/myanmar/mm_frame_sim.py` | **142/142 通过**（原 108：§10 交换语义 10 条 + 源码不变量 24 条） |
| `.analysis/myanmar/check_eide_targets.py` | **40/45**（5 条失败 = Debug 现行口径差异，非回归；原 30/35，新增 11 条断言全绿） |
| `.analysis/myanmar/check_display_guards.py` | 14/14 硬断言、97/97 调用点（新文件不引用显示 API ⇒ 无新增调用点） |
| `.analysis/myanmar/check_guard_disasm.py` | 100 函数 / 19 改动 / **原操作丢失 0** |
| `.analysis/heap/heap_ledger.py` | 正常运行（两版出厂测试按 1:1 记账：1KB 静态栈 + TCB） |

## 7. 文件清单

| 文件 | 变更 |
|---|---|
| `Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h` | 新增两个 API 声明 + 头注释（§13 指引） |
| `Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c` | 新增 ①b 段（`_mm_effective_lamp` / getter / toggle + RTT 日志）+ `mm_execute_cmd` 走交换 + `mm_apply_init` 复位 |
| `Application/Src/app_factory_test_mm.c` | **新增**（119 行；同名对外符号 + TEST 键切换 + 静态 CCMRAM 任务） |
| `Makefile` | 新增 `FACTORY ?= STD|MM` + `SRC_FACTORY_TEST` + `BUILD_STAMP_TEXT` 增 `FACTORY=` 行 |
| `.eide/eide.yml` | `files` 增 `app_factory_test_mm.c`；`targets.Debug` / `targets.Myanmar` 的 `excludeList` 增 `<virtual_root>/Application/app_factory_test.c` |
| `.analysis/myanmar/check_eide_targets.py` | 新增 11 条断言 + 地面真相「出厂测试差异 = 空 / 整对」放行 |
| `.analysis/myanmar/mm_frame_sim.py` | 新增 §10 交换语义（10 条）+ 源码不变量（24 条） |
| `.analysis/myanmar/Makefile.mm_nodisp` | 由 `--emit-make` 按新 yml 重生成（含 `app_factory_test_mm.c`） |
| 文档 | `doc/16` §3/§7.1/§9.1/§9.3/§12 + **新 §13**；`doc/构建开关总表.md` §3/§4/§5；`doc/CLAUDE.md`（构建系统 / initcall 表 / 模块全景 / 调试段 / 缅甸小节）；`doc/06-04`（新条目 + 任务表）；`doc/05-01` 变更记录 |

## 8. 未做 / 待确认

- **开机默认灯态（「开机默认红叉」）未动** —— 语义未确认，留待后续；`mm_apply_init()` 的
  初始电平（三路全灭 = 输出低）与接管顺序一字未改。
- 极性（`active_low=false`）、PE10/11/12 接管 / EXTI 逻辑、其它协议：未动。
- 未烧录、未提交、未改 git 配置。
- 现场未做（本轮为工程内验证）：TEST 键实际切换的示波器/万用表现场确认（测试矩阵见 doc/16 §13.4）。
- `build/Debug/` 产物被本轮构建刷新（原 elf md5 `d2feff2d23299ad1d54598fca31936d9` → 现为
  `FACTORY=STD` 默认口径新构建；EIDE 与本目录共用产物，EIDE 构建后须 `make clean && make`）。
