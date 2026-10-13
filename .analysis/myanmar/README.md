# .analysis/myanmar — 缅甸 2D 费显协议（车道灯开关量控制）分析归档

> 协议文档：`/home/yystation/文档/创迪科技生产订单与技术资料/01 产品软件/产品协议文档/2D缅甸费显协议.doc`
> 模块：`Application/Src/ProtocolParser_Myanmar/` + `Application/Inc/ProtocolParser_Myanmar/`（前缀 `mm_`/`MM_`）
> 接入记录：`doc/16_缅甸2D费显协议/README.md`（**§9 = EIDE 目标与内存账，§10 = 无显示驱动构建的边界防护，§13 = TEST 键箭头落点切换 + 缅甸版出厂测试**）

## 文件索引

| 文件 | 内容 |
|---|---|
| `doc_extract.txt` | 协议文档全文抽取（`soffice --convert-to txt`，83 行；定长 20 字节帧 / 5 命令 / 通信参数 / 示例帧逐字节） |
| `mm_frame_sim.py` | **宿主 probe/parse 推演 + 源码一致性断言**（镜像 `mm_probe_frame()` / `mm_parse_frame()`；含半帧、粘包、拆包、噪声重同步、**与四川治超 SC_OL 的同槽共存与抗误吞**、文档非本模块命令 SKIP、结束码末字节不判别；§9 干接点灯位/引脚/极性/**上电默认态**用例与「单一映射点 / 接管顺序（先灭电平→摘 EXTI→切输出→点亮红灯）/ 逐线清 EXTI / 禁止 `HAL_NVIC_DisableIRQ` / Application 层不含 HAL / **现行 `active_low=false`（高有效）**」等源码纪律断言；**§10 箭头落点运行时交换（2026-09-28）**：默认/交换态落点、红灯不受影响、可来回、开机回默认、probe 与 parse 不受影响；**§11 开机默认画面「红色叉」（2026-09-29）**：红叉几何（线宽 `max(2,h/24)`、四角覆盖、中心两支、屏内落笔）+ 源码断言（仅带显示驱动注册 / 回调判空 / 几何取自 `dev_display_get()` / COLOR_RED / 先清黑 / 提交帧））。**当前 158/158**（阶段一 77 → 阶段二 103 → 极性轮 108 → 厂测轮 142 → 红叉轮 159 → **2026-09-29 瘦身轮收敛为 157**〔删 4 条 RTT 日志断言、换 2 条更强断言〕→ **2026-10-13 编译期断言清理后 158**〔原「定长 20 字节编译期断言」改「契约注释存在 + 宿主复算」〕；**2026-09-28 现场实测翻转极性：高有效/点亮=输出高**；**2026-09-29 起上电默认 = 红叉亮（取代三路全灭）**） |
| `check_eide_targets.py` | **EIDE 目标校验（结构不变量口径）+ 编译集 / CCMRAM 预算**（2026-10-08 改写：**不再断言「Debug 必须 = 某固定集合」**——协议/显示模组编不编属可按需调整的选择，`.eide/eide.yml` 是 GUI 地盘、本脚本只读不改）。**I1~I8 结构不变量**：显示驱动 ≤1 / 零显示 = 合法无显示口径 / 基类 `dev_display.c` 必编 / 出厂测试恰好 1（标准版·缅甸版同名符号）/`{` 帧族 ≤1（无 `STD_ALL_PROTO` 时；帧族由源码事实 `g_brace_proto_guard` 推导）/ `PROTO_CHONGQING` ⇒ CQ 模块必编 / 编入模块的私有 `-I` 齐备 / defineList 含工程宏；**非可用目标**（incList 仍含已删 `Drivers/BSP/*`，如 Release）跳过断言只报告。另：按 EIDE 的 `isExcluded()` 虚拟路径语义推导编入/排除清单，与 `build/<target>/builder.params` **对拍只报信息**（yml 可能刚被 GUI 改过）；**当前集合信息输出**（显示驱动 / 厂测 / `{` 族 / defines / 条数）；CCM 预算多口径；`--emit-make <path> [--target T]` 生成等价口径 Makefile（无显示时 `SRC_DISPLAY` 置空）。**现行结果（2026-10-08）：14/15 通过，唯一失败 = Debug 的 I6「带 `PROTO_CHONGQING` 却排掉 CQ 模块」（C3 混编；交用户在 EIDE GUI 二选一「去宏 / 编 CQ」，脚本不改 yml）**；`Debug` 206 源文件、`Myanmar` 207 源文件，与 builder.params 逐条一致 |
| `eide_writeback_20260924/` | **2026-09-24 EIDE 回写事件证据归档**（Debug 构建报错修复轮）：`eide.yml.before_fix`（EIDE 回写后的坏版本，Debug 丢「缅甸」排除、误加 `ldi`+四川三排除）、`builder.params.debug_before_fix`（193 源文件 / incDirs 33 条无缅甸 —— 报错的直接现场）、`Project_STD.objlist.before_fix`（阶段三 206 条：含 LDI+四川三、不含缅甸）、`builder.params.debug_after_fix`（206 源文件 / incDirs 34 条，EIDE 按修复后 yml 重新生成）、`after_fix_build/`（修复后 EIDE 构建产物 elf/hex/bin + 我复跑 `unify_builder --rebuild` 的产物）。详见 doc/16 §9.1.1 |
| `factory_swap_20260928/` | **2026-09-28 TEST 键箭头落点切换轮的证据归档**：`REPORT.md`（本轮报告）+ `ld_mutual_exclusion.txt`（两出厂测试文件同编的 3 个 `multiple definition`）+ `disasm_and_ab.txt`（交换生效点反汇编 + 解析/映射层 A/B 0 差异）+ `app_mm_proto_cmd_pre_swap_reconstructed.c`（改写前源码重建件）+ `size_factory_{std,mm}.txt` + `build_summary.txt`。详见 doc/16 §13 |
| `check_display_guards.py` | **「整机无显示驱动」边界防护审计**（阶段三新增；**2026-09-29 起 16 条**，新增「缅甸红叉注册点判空 / 红叉回调判空」两条）：16 条守卫硬断言（文件 + 必须出现的代码片段）+ 全仓 `dev_display_get()` 调用点启发式分类（±10 行窗口判「已判空/三元」）。`--md <path>` 另存审计表。**当前 16/16 通过、95/95 调用点已覆盖（0 处待确认）**（调用点总数随 2026-09-29 瘦身轮删除 4 处 RTT 诊断调用点收敛） |
| `check_guard_disasm.py` | **「有显示时零行为变化」机器级对照**（阶段三新增）：逐函数比较改动前后的反汇编**操作序列**（寄存器名归一化、丢立即数、忽略栈帧/寄存器搬运噪声），硬判据 = **原操作零丢失**；对编译期代码生成差异（三元折叠 / `cbz`→`cmp+beq` / 寄存器重排 / 编址方式）保留一份**已复核白名单**并逐条打印。**当前函数 100 个（改动 19 个）、原操作丢失 0** |
| `ab_baseline/` | 上表的 A/B 基线：11 个受影响 TU 的**改动前** `.o` + `.dis`（其中 `app_rls_cmd` / `ah_mqtt` / `ah_mqtt_cmd` 三个对象由 `git show HEAD:<file>` 的原始源码重新编译得到）；`size_all_1263_before.txt` / `size_cq_1263_before.txt` = 标准口径改动前段尺寸。〔2026-10-12 工作区清理轮：`.o` / `.d` 已删（14 个，238KB）；`.dis` 与 `*_orig.c` 保留 ⇒ `check_guard_disasm.py` 只读 `.dis`、复跑不受影响（重生成 `.o` = 按改动前源码重编）〕 |
| `display_guard_audit.md` | `check_display_guards.py --md` 生成的审计表（守卫断言清单 + 全部调用点分类） |
| `build_nodisp/Makefile.mm_nodisp` | `check_eide_targets.py --emit-make` 生成的**无显示驱动**等价口径 Makefile（`SRC_DISPLAY =` 置空、`DEFINES` 无 `PROTO_CHONGQING`；**生成物内 `BUILD_DIR` 默认改写到 `build_nodisp/$(CONFIG)`，产物不落 `build/`**——2026-09-29 第二轮防再污染） |
| `build_all_after.log` / `build_cq_after.log` / `build_mm_nodisp.log` | 三轮全量重编日志（`PROTO=ALL DISP=1_263` / `PROTO=CQ` / 无显示等价口径），各含 3 条既有 HAL 告警（零新增） |
| `disasm_nodisp.txt` | 无显示等价口径 ELF 的全量反汇编（`dev_display_start` / `pl_tim_start_it` 调用者等证据的原始出处） |
| `ab_all_1263_with_mm.sizes.txt` 等 `*.sizes.txt` | 历史（阶段一/二）段尺寸存档：含/不含缅甸 A/B、CQ 口径、阶段二 EIDE 等价口径 |
| `README.md` | 本文件 |

## 运行

```bash
python3 .analysis/myanmar/mm_frame_sim.py                    # 协议推演，当前 158/158
python3 .analysis/myanmar/mm_frame_sim.py -v                 # 逐条用例明细
python3 .analysis/myanmar/check_eide_targets.py              # EIDE 结构不变量校验 + 当前集合/CCM 预算（现行 14/15；唯一失败 = Debug 的 PROTO_CHONGQING↔CQ 错配）
python3 .analysis/myanmar/check_eide_targets.py -v           # 加打完整编入/排除清单
python3 .analysis/myanmar/check_display_guards.py --md .analysis/myanmar/display_guard_audit.md
python3 .analysis/myanmar/check_guard_disasm.py              # A/B 反汇编对照（原操作零丢失）

# 无显示驱动等价口径链接验证（阶段三口径；产物落 .analysis/，不污染 build/）
python3 .analysis/myanmar/check_eide_targets.py --emit-make .analysis/myanmar/build_nodisp/Makefile.mm_nodisp
make -f .analysis/myanmar/build_nodisp/Makefile.mm_nodisp CONFIG=EIDEMyanmarNoDisp -B -j8
# ↑ 生成物内 BUILD_DIR 已默认改写到 build_nodisp/$(CONFIG)；如需换目录：
#   make -f ... BUILD_DIR=<dir> CONFIG=... -B -j8
```

## 关键结论（详见 doc/16）

1. 帧 = **定长 20 字节**（`FF` + `E0` + 命令 + 12 字节内容 + 5 字节结束码），**无校验、无应答**。
2. 文档只有 `0x10/0x40/0x80/0x90/0x0A` 五条命令，**没有 `0x0B`**（用户口径的「右下箭头」无文档出处）。
3. **干接点已真实驱动**（2026-09-24 用户裁决；**2026-09-28 现场实测翻转极性**）：**S1/S2/S3 = PE12/PE11/PE10**
   （复用原 SW1/SW2/SW3 端子改输出），**推挽、高电平有效**（点亮 = 输出**高** / 熄灭 = 输出低；
   证据：复位后红灯即亮 + 65 脚 PE12 实测 3.3V ⇒ **3.3V = 点亮**）、关灯=断开；
   接管时**按线**摘 EXTI（不碰 NVIC / 其它 EXTI 线，TEST 键 PD8 安全）。
   **上电默认显示红叉**（**2026-09-29 用户需求**，取代 2026-09-24 的「三路全灭」）：三路接管完成后
   **红灯 S1/PE12 点亮**（= 车道关闭态）、两个绿箭头熄灭；协议帧到达后照常按帧驱动。
   副作用：SW 输入语义失效 ⇒ RLS 干接点轮询在共存构建下可能误判（上电默认态下 SW3 读闭合仍判
   「车道关闭」；量产排除 RLS 即无影响）。
4. 与四川治超（SC_OL）**同为 RS232 槽 `0xFF` 首字节**：靠「本 probe 严格判据 + 非本模块命令需正签名」
   做到双向不误吞（宿主推演 §6 覆盖 11 条用例）。
5. **EIDE 缅甸目标（阶段三口径）**：**不带显示驱动**（`Device/Display` 下全部 `dev_display_*.c` 进排除表，
   基类 `dev_display.c` 保留）+ **去掉 `PROTO_CHONGQING`** + 协议集继承 Debug 全集（LDI / 四川 ETC·MTC·治超 保留）；
   等价口径实测链接通过：`.ccmram 16084B`（余 49452B）、SRAM 余 11128B、207 源文件、`nm` 无模组显示驱动符号。
   **2026-09-24 修复轮**：EIDE 回写曾把 `targets.Debug` 排除表改坏（丢「缅甸」排除、误加 `ldi`+四川三排除
   ⇒ Debug 构建报 `app_mm_proto.h: No such file or directory`）；已恢复 Debug 阶段三基线（编 LDI+四川三、
   排缅甸）**并在 Debug/Release 的 incList 兜底补上缅甸头文件目录**（误编入也不缺 `-I`）。
   **注（2026-09-28 用户裁决）**：暂保持 EIDE 回写后的**现行口径**（Debug 编入缅甸、排除
   LDI + 四川 ETC/MTC/治超），不恢复上文阶段三排除表；`check_eide_targets.py` 在该口径下
   预期 **40/45（口径差异，非失败回归；2026-09-28 厂测轮扩断言前为 30/35）**，上文「已恢复 Debug 阶段三基线」表述**以本裁决为准**。
   证据与备份见 `eide_writeback_20260924/`、doc/16 §9.1.1。
   **2026-09-29 复测**：无显示等价口径（`--emit-make` 重生成 `build_nodisp/Makefile.mm_nodisp` 后
   `make -f … CONFIG=EIDEMyanmarNoDisp -B`；**产物落 `.analysis/myanmar/build_nodisp/`**）**链接通过**：text **137632** / rodata **197000** /
   data 1584 / ccmram **16084** / bss **115796** / heap 2564 ⇒ SRAM 余 **11128B**、CCM 余 49452B；
   `nm` 模组驱动符号 0、红叉符号（`_mm_default_red_cross` / `_mm_cross_px`）在对象里（无显示 ⇒ 运行时
   不注册、不打印）。
6. **无显示构建的边界防护**（阶段三核心）：新增守卫助手 `dev_display_guard()`（限速告警前缀 `[disp]`，
   **无编译期开关**）+ 判空/降级路径（**2026-09-29 起 15 条**，新增「缅甸红叉注册点」）—— 无显示时
   **不建 `scan_task`、不启 TIM3/TIM4**，splash / 默认画面 / 渲染 / 滚动 / 光敏调光 / 出厂检测老化循环
   全部安全跳过；LDI E6 显示命令**回 `LDI_DEV_FAIL`**（诚实失败），LDI E9 VMS 静默跳过；
   缅甸红叉回调**不注册**（`dev_display_get() == nullptr` 时不调 `app_default_display_register`）。
   判据：`check_display_guards.py` **16/16 + 95/95**；`check_guard_disasm.py` 原操作零丢失
   （= 有显示时零行为变化）。
7. **TEST 键切换 0x0A ⇄ 0x0B 箭头落点 + 缅甸版出厂测试**（2026-09-28 新增，doc/16 §13）：
   `mm_arrow_map_toggle()` 在**单一生效点**（`mm_execute_cmd()` → `_mm_effective_lamp()`）互换两条命令的
   干接点落点（默认 `0x0A→S2/PE11`、`0x0B→S3/PE10`），**红灯 `0x90` 不受影响**、**开机复位不持久化**；
   probe / 解析 / 队列 / 映射表 / 极性零改动（A/B 反汇编 0 差异）。消费者 = 新增
   `Application/Src/app_factory_test_mm.c`（TEST 键只做切换、**不引用显示 API**），与标准版
   `app_factory_test.c` **二选一编译**：Makefile `FACTORY=STD|MM`（默认 STD，非法值 `$(error)`，进口径指纹）
   / EIDE `excludeList`（Debug 与 Myanmar 均编缅甸版）；同编 = 链接期 3 个 `multiple definition`
   （**天然守卫**，实测）。实测段尺寸（**2026-09-29 复核值**）：`FACTORY=STD` text 176576 / rodata 203672 / bss 124860（SRAM 余 1992B）；
   `FACTORY=MM` text 175184 / rodata 203640 / bss 124596（SRAM 余 2256B）；无显示口径（MM 版）
   `.ccmram 16084`、SRAM 余 11128B、链接通过。
8. **上电默认红叉 + 开机默认画面「红色叉（×）」**（**2026-09-29 新增**，doc/16 §14）：
   ① **车道灯板侧（主）** —— `mm_apply_init()` 接管完成后 `mm_apply_lamp(MM_LAMP_RED, true)` ⇒
   **红灯 S1/PE12 点亮**（输出高 = 车道关闭态）、两绿箭头熄灭（取代 2026-09-24 的「三路全灭」）；
   唯一动作点 + 影子状态/诊断日志同步 + 接管仍无毛刺；协议帧到达后照常按帧驱动。
   ② **屏侧（备；仅带显示驱动的构建）** —— `app_mm_proto.c` 的 `_mm_default_red_cross()` 经
   `app_default_display_register()` 注册（`dev_display_get() != nullptr` 才注册）：整屏清黑 + 两条
   对角线（`COLOR_RED`、几何取自 `dev_display_get()`、线宽 `max(2, h/24)`）。
   注册单槽首个生效 ⇒ `PROTO=ALL` 开发构建仍由山东先占槽（画面不变），EIDE Debug / Myanmar 目标
   生效（后者无显示驱动 ⇒ 不注册）。增量：`FACTORY=STD`/`MM` 两口径 **text +240 / rodata +24**。
