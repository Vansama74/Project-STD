# 缅甸 2D 费显：干接点极性现场实测翻转（低有效 → 高有效）— 2026-09-28 轮报告

> 模块：`Application/{Src,Inc}/ProtocolParser_Myanmar/`；接入记录：`doc/16_缅甸2D费显协议/README.md` §4/§11/§12。
> 本目录归档本轮证据：`build_before.log` / `build_after.log`（两次 `make -B -j8` 全量构建）、
> `sizes_before.txt` / `sizes_after.txt`（`arm-none-eabi-size -A`）、
> `eide_yml_state_20260928.txt`（EIDE 回写现状快照，见 §5）。

## 1. 现场证据（用户 2026-09-28 提供，决定性）

- 引脚：STM32F407ZGT6 LQFP144 **63/64/65 脚 = PE10/PE11/PE12**（= 现映射 S3/S2/S1）。
- **复位后不发任何命令：红色车道灯（红叉）即亮**；此时固件按旧「低有效」配置把三路输出 **3.3V**。
- 发 `FF E0 90 00 …`（红灯关）后，**65 脚（PE12）对地仍 3.3V、灯仍亮**。
- ⇒ **3.3V = 点亮 ⇒ 高电平点亮（高有效）**；旧 `active_low = true` 反了；RTT `[mm]` 日志正常（解析/执行链无问题）。

## 2. 代码改动（最小改动 = 单一映射点）

| 文件 | 改动 |
|---|---|
| `Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c` | ① L86/87/88 映射表三项 `.active_low` `true → false`；② L17 文件头极性段改高有效 + 记录现场证据；③ L83-84 表前注释；④ L112-114 `_mm_drive()` 注释（**表达式 `on != m->active_low` 一字未改**）；⑤ L126-129 接管顺序注释（灭电平 = 输出低、说明由映射表推导）；⑥ L161-163 `mm_apply_init()` 注释；⑦ L180 接管日志 `active-low → active-high` |
| `Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h` | 输出层契约（L43 附近）+ `mm_apply_lamp`/`mm_apply_init` 文档（L140 附近）：高有效 / 点亮 = 输出高 / 上电全灭 = 输出低 |

**初始化序列核查结论**：`mm_apply_init()` 的「先写 ODR = 灭电平」复用 `_mm_drive(m, false)` —— 与驱动动作**同一电平真源**（`high = on != active_low`），**无任何硬编码**。新极性下
`high = (false != false) = 0` ⇒ **预写 = 输出低（灭）**；随后按线摘 EXTI、切推挽输出，切换瞬间即为「灭」，不会先冲出一次「亮」，无毛刺。

## 3. 反汇编证据（两编译口径）

- **make `-Og`**（`build/Debug/Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.o`）：
  - `.rodata.s_mm_lamp_map` = `… 04 0c 00 00 | … 04 0b 00 00 | … 04 0a 00 00` ⇒ 三条 `active_low = 0x00`（false）；
  - `_mm_drive`：`ldrb r2,[r0,#10]`（active_low）→ `subs r2,r2,r1`（on）→ `it ne / movne r2,#1` → `bl pl_gpio_write(port,pin,high)`；
  - `mm_apply_init`：`movs r1,#0` 后 `bl _mm_drive`（on=false）→ `bl pl_gpio_release_exti` → `bl pl_gpio_config_output` → 影子状态写 0（顺序正确）。
- **EIDE `-O3`**（手工同 flags 复编 `/home/yystation/tmp-mm/mm_cmd_O3.o`，表字节同上）：
  - `mm_apply_init`：`ldrb r2,[r4,#10]`（= active_low，编译器把 `false != active_low` 折叠成 `active_low`）→ `bl pl_gpio_write` ⇒ 现行表值下 **写 0（低）**；顺序同上；
  - `mm_apply_lamp`：`eor.w r2,r4,r6`（r4=on、r6=active_low）→ `bl pl_gpio_write` ⇒ **on=1 → 高（点亮）/ on=0 → 低（熄灭）**。
- 字符串：`… (push-pull, active-high, all OFF; …)` 在对象/ELF 各 1 条，`active-low` **0 条**；`高有效:高=点亮` 与 `低有效:低=点亮` 均在（运行时按 `active_low` 分支，现行打「高有效」）。

## 4. A/B 段尺寸（`PROTO=ALL` `DISP=1_263`，`make -B -j8`）

| 项 | 改动前 | 改动后 | Δ |
|---|---|---|---|
| `.text` | 176288 | 176288 | **0** |
| `.rodata` | 203640 | 203648 | **+8**（接管日志 `active-low`→`active-high` 长 1 字符，`.rodata.mm_apply_init.str1.4` 116→117 引起的对齐增量） |
| `.data` | 1656 | 1656 | 0 |
| `.ccmram` | 39652 | 39652 | 0 |
| `.bss` | 124860 | 124860 | 0 |
| `._user_heap_stack` | 2564 | 2564 | 0 |

链接通过；告警 = 3 条既有 HAL `-Wunused-parameter`（`stm32f4xx_hal_flash_ex.c`），**零新增**。
elf md5（改动后 make 产物）`0a5201debad5d9608ff7a68a4ce2c428`；驱动对象 `.text._mm_drive` 20 / `.text.mm_apply_lamp` 208 / `.text.mm_apply_init` 96 / `.rodata.s_mm_lamp_map` 36（尺寸全同前，仅取值与日志字符串变）。
⚠ 该产物已被 EIDE 于 10:02 覆写（见 §5）；如需烧录 make 产物请先 `make clean && make`。

## 5. 外部发现：`.eide/eide.yml` 又被 EIDE 回写（本轮未动该文件）

收尾复跑 `check_eide_targets.py` = **30/35**（5 条失败），全部指向 `targets.Debug.excludeList`：
**缺「缅甸」排除**、**含 `ldi` + 四川 ETC/MTC/治超排除**（= EIDE 回写后的口径，与 2026-09-24 修复轮
记录的改动前状态相同）。
同期证据：EIDE 自己写的 `build/Debug/builder.params` 的 `sourceList` **含缅甸两文件、`/LDI/` = 0**、
`defines` 含 `PROTO_CHONGQING`；且 **EIDE 在本轮会话期间（10:02~10:03）仍在回写**
`build/Debug/Project_STD.objlist` / `builder.params` / **`.eide/eide.yml`（mtime 10:03:05）**。
⇒ 回写是**持续行为**（非一次性事故）。**本轮按指令未修改 yml**；快照与脚本输出见 `eide_yml_state_20260928.txt`；
**2026-09-28 用户裁决：暂保持该现行口径（Debug 编入缅甸、排除 LDI + 四川 ETC/MTC/治超），不恢复阶段三排除表**——
该口径下 `check_eide_targets.py` 预期 **30/35（口径差异，非回归）**；**如需恢复阶段三基线**（可选路径）才须在
**EIDE GUI 目标设置**里改（纯文件编辑会被再次冲掉）+ `EIDE: Reload Project`，以脚本 **35/35** 复核；
另见 doc/16 §9.1「2026-09-28 复现观测」（该注记在 §9.1，非 §9.1.1）。

## 6. 脚本结果

| 脚本 | 结果 |
|---|---|
| `.analysis/myanmar/mm_frame_sim.py` | **108/108**（103 → 108：§9 期望值全部翻转 + 新增 5 条源码断言） |
| `.analysis/myanmar/check_display_guards.py` | **14/14** 硬断言 + 97/97 调用点分类（与本模块极性无关，未改） |
| `.analysis/myanmar/check_guard_disasm.py` | 函数 100（改动 19），**原操作丢失 0**（未改） |
| `.analysis/myanmar/check_eide_targets.py` | **30/35** —— 失败与极性无关，为 §5 的 EIDE 回写所致（本轮未动 yml） |

## 7. 现场验证步骤（烧录后）

前置：DIP1 = OFF（9600）；RTT 通道 0 已接；`MM_RTT_DIAG` 默认开。

1. 复位 → 期望 `[mm] dry contacts taken over: S1=PE12 / S2=PE11 / S3=PE10 (push-pull, active-high, all OFF; …)`，
   PE10/PE11/PE12 实测 **≈0V**，三灯全灭；
2. `FF E0 90 01 3A×11 00×5` → `[mm] S1 (红灯) <- ON(开灯) => PE12 HIGH (高有效:高=点亮)`，PE12 = 3.3V、红灯亮；
3. `FF E0 90 00 …` → `OFF(关灯) => PE12 LOW`，PE12 = 0V、红灯灭；
4. `FF E0 0A 01/00 …` → PE11 = 3.3V/0V（左下箭头灯）；`FF E0 0B 01/00 …` → PE10 = 3.3V/0V（右下箭头灯）。
5. 烧录纪律：烧完擦 Sector1（0x08004000~0x08007FFF）；EIDE 构建会覆写 `build/Debug` 产物，
   要用 make 产物须 `make clean && make`。
