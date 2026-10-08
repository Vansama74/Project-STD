# 代码瘦身轮报告（2026-09-29）：RTT 调试设施整体移除 + 注释精简

> 范围：`Application/` `Device/` `Platform/` `Kernel/` `Compiler/` `Core/`（仅用户代码段）
> `Makefile` `.eide/eide.yml`（必要处）；**不含** `Drivers/` `Middlewares/`（第三方）、`doc/` 与
> `.analysis/**/archive/**` 历史归档正文、`tool/`。
> 用户需求：「整理项目所有代码，范围：不保留 RTT 调试代码、注释（函数简介等）精简，不能影响实际业务功能。」
> 工作目录：`.analysis/cleanup_20260929/`（备份、清单、状态、构建取证、A/B 证据、本报告）。

## 0. 结论速览

| 维度 | 结果 |
|---|---|
| RTT 清零 | **自有代码 0 处代码引用**（含 include/调用/宏/门控）；SEGGER RTT 三个源文件已移出构建（磁盘保留）；elf 内 `SEGGER` 符号 **0 个** |
| 业务行为 | **零变化**（token 级「只删不改」审计 26 文件 / 违规 0；13 处函数级机器码差异全部为删除的调试语句，功能调用集不变） |
| 构建 | 四口径全部通过（`ALL/1_263/STD`、`FACTORY=MM`、`PROTO=CQ`、无显示等价口径），**仅 3 条既有 HAL 告警** |
| 段尺寸 | 默认口径 text **−9288** / rodata **−7936** / bss **−1300** / ccmram **±0**（SRAM 余量 1992B → **3296B**） |
| 注释精简 | 注释字符 **393,206 → 365,811（−27,395，−7.0%）**；行 **48,114 → 46,516（−1,598）**；纯注释行 −568；涉及 28 个文件 |
| 脚本 | 22_1665 `verify_all.sh --fast` **14 绿 / 1 跳过**；`mm_frame_sim.py` **157/157**；`check_display_guards.py` **16/16 + 95/95**；`check_guard_disasm.py` **原操作丢失 0**；`yn_ol_frame_sim.py` **184/184**；`check_eide_targets.py` **40/45**（5 条 = 既有 EIDE 口径差异，非本轮回归） |
| 文档 | `doc/CLAUDE.md`、`doc/构建开关总表.md`、`doc/06-04`、`doc/14`、`doc/15`、`doc/16` 已同步；**新增 `doc/17_代码瘦身轮_20260929.md`** |

## 1. RTT 调试代码清零（用户需求 ①）

### 1.1 清单（`inventory.md` / `rtt_sites.txt`，共 22 类）

| 类别 | 处置 | 结果 |
|---|---|---|
| `Platform/Src/pl_rtt.c`（包装 + `hw_pl_initcall` 注册） | 删除文件 | ✅（`Platform/Inc/pl_rtt.h` 本就不存在） |
| `Application/Inc/app_diag.h`（`APP_DIAG_BANNER` + 5 构建指纹宏） | 删除文件 | ✅ |
| Makefile：`APP_DIAG` / `APP_LWIP_DBG_LEVEL` 开关、`DIAG_TREE_HASH` + `DIAG_DEFS`、SEGGER 3 源文件、`-I Middlewares/Third_Party/SEGGER_RTT`、`Platform/Src/pl_rtt.c` | 删除 | ✅ |
| `.eide/eide.yml`：SEGGER 3 个 `files` 条目、2 处 `incList` 路径、`Platform/Src/pl_rtt.c` 条目、`RTT` 虚拟文件夹 | 删除 | ✅ **需在 EIDE 里执行 `EIDE: Reload Project`** |
| `app_boot.c`：开机横幅 / 延迟体检 / 堆探针（12 处调用）/ `osDelay(2500)` 体检窗口 / init_task 创建失败与 malloc 失败的 RTT 打印 | 删除（保留关中断停机止血） | ✅ |
| `app_dispatch.c`：`[disp] probe` 盲区 B 日志 + 限速态、无协议承载告警、丢帧告警、WAIT 预算到期证据、`PROBE_DBG_MIN_MS` / `DISPATCH_NOPROTO_DBG_MIN_MS` | 删除（**预算/重同步/重试/计数逻辑本体保留**） | ✅ |
| `app_net_boot.c` netcfg 告警、`app_board_net_cfg.c` `[fwver]` 7 处打印、`app_render.c/.h` 诊断注释 | 删除 | ✅ |
| `dev_display.c`：`[disp] no display driver linked` 限速告警 + 限速态 | 删除（**`dev_display_guard()` 判空语义保留**） | ✅ |
| `pl_task_guard.h` / `pl_eth.c` / `pl_net.c`：任务创建失败 RTT 报错 | 删除（判空/创建调用保留） | ✅ |
| `app_tcp_server.c` / `app_tcp_client.c`：`TCP_SRV_RTT_DIAG`/`TCP_CLI_RTT_DIAG` 全部日志 + 去重态 | 删除 | ✅ |
| `app_udp.c`：GZ_OL 端口回退/冲突/绑定成功告警 + 告警闩锁 | 删除（绑定结果只读 getter 保留） | ✅ |
| `app_gz_ol_proto_cmd.c`：`GZ_OL_RTT_DIAG` 全部（含 3 个诊断辅助函数） | 删除 | ✅ |
| `app_anhui_proto_cmd.c`：`ANHUI_RTT_DIAG` | 删除 | ✅ |
| `app_yn_ol_proto.c` / `_cmd.c`：`YN_OL_RTT_DIAG` 全部（含 3 个诊断函数） | 删除 | ✅ |
| `app_mm_proto_cmd.c`：`MM_RTT_DIAG`（每帧灯位日志、接管摘要、TEST 切换行）+ 端口名表 + 日志态 | 删除 | ✅ |
| `app_factory_test_mm.c`、各头文件里的诊断宏说明 | 删除/改写 | ✅ |
| `Core/Src/stm32f4xx_it.c` | **无需改动**：HardFault 本就是「存现场到全局变量 + 停机」，无 RTT 打印 | ✅ |
| `Core/Inc/main.h` 的 `LWIP_DEBUG` 等（CubeMX 生成区） | **保持不动**（由 `lwipopts.h` 覆盖） | ✅ |
| LwIP 调试输出（原经 `printf → _write → RTT`） | `Platform/Inc/lwipopts.h` 改为 `LWIP_DBG_TYPES_ON 0`（打印分支编译期常量假 ⇒ 零输出、零格式化开销、字符串不落 Flash），保留 `#ifdef LWIP_DEBUG` 门控以免第三方产生 unused 告警 | ✅ |
| printf 落地点（原由 SEGGER `_write` 承接） | `Core/Src/syscalls.c` 新增**无输出** `__io_putchar`（消除「弱符号解析为地址 0、误调用即跳 0」隐患） | ✅ |
| 第三方 `Middlewares/Third_Party/SEGGER_RTT/*`（3 个 .c + 2 个 .h） | **磁盘文件保留**，仅移出构建（恢复 = 加回源列表 + 补 `-I` + 从备份取回 `pl_rtt.c`） | ✅ |

### 1.2 清零举证

```bash
# ① 自有代码全仓扫描（预期 0 代码引用；仅 2 处「已删除」历史说明注释）
grep -rn "SEGGER\|pl_rtt\|app_diag\|APP_DIAG\|_RTT_DIAG\|LWIP_PLATFORM_DIAG" \
     Application Device Platform Kernel Compiler Core Makefile
#   → Platform/Inc/lwipopts.h:38（说明「原经 printf→调试通道落地」）
#   → Makefile:60（说明「两个开关已随 RTT 设施删除」）

# ② 产物级：链接后无任何 SEGGER 符号
arm-none-eabi-nm build/Debug/Project_STD.elf | grep -i segger     # → 0 行
```

## 2. 注释精简（用户需求 ②）

### 2.1 规模（工具 `stats.py`，253 个 `.c/.h`）

| 指标 | 改动前 | 改动后 | 差值 |
|---|---|---|---|
| 总行数 | 48,114 | 46,516 | **−1,598** |
| 纯注释行 | 10,445 | 9,877 | **−568** |
| 代码行（不含注释/空行） | 32,838 | 31,879 | −959（含 RTT 代码块删除） |
| **注释字符数** | **393,206** | **365,811** | **−27,395（−7.0%）** |
| 文件数 | 255 | 253 | −2（`pl_rtt.c`、`app_diag.h` 整文件删除） |

> 注：「代码行」下降主要来自 RTT 代码块（`#if *_RTT_DIAG` 整段、诊断辅助函数）——这与注释精简是同一轮工作的两面；
> 纯注释字符下降 27.4K 是注释精简本身的规模。

### 2.2 逐文件（注释字符减少 Top 25，见 `stats_before.txt` / `stats_after.txt`）

| 文件 | 注释字符 | 行数 |
|---|---|---|
| `Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c` | 7214 → 3773（−3441） | 334 → 218 |
| `Application/Src/app_dispatch.c` | 11246 → 8035（−3211） | 807 → 605 |
| `Application/Src/ProtocolParser_Myanmar/app_mm_proto.c` | 6612 → 3486（−3126） | 317 → 258 |
| `Application/Src/app_boot.c` | 5546 → 2665（−2881） | 468 → 243 |
| `Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h` | 8744 → 6254（−2490） | 325 → 286 |
| `Application/Src/app_factory_test_mm.c` | 2897 → 1037（−1860） | 119 → 77 |
| `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c` | 6704 → 4891（−1813） | 610 → 385 |
| `Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h` | 6267 → 4546（−1721） | 211 → 172 |
| `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c` | 6677 → 5069（−1608） | 464 → 254 |
| `Application/Src/Channel/app_tcp_server.c` | 3333 → 2267（−1066） | 284 → 178 |
| `Application/Src/Channel/app_tcp_client.c` | 2210 → 1215（−995） | 297 → 212 |
| `Application/Inc/app_diag.h`（删除） | 862 → 0 | 46 → 0 |
| 其余 16 个文件（anhui / yn_ol_cmd / lwipopts / pl_task_guard / dev_display / pl_eth / app_net_boot / app_udp / pl_rtt / app_board_net_cfg / pl_net / app_udp.h / app_render.h / app_render.c / 两个头文件） | 合计 −3,308 | — |

**精简口径**（已执行）：文件头压到「模块名 + 一句话职责 + 文档指针」；删除逐行复述代码的注释、轮次沿革长叙事
（改为一行 + `doc/` 指针）、已删设施的残留说明；**保留**硬件/协议不变量与契约（干接点极性、帧格式、BE16、
扫描时序、接管顺序）、纪律性警示（EXTI 逐线摘除、DMA 不可达 CCM、`{` 帧族互斥）、非显而易见的原因（`why`）与陷阱（如 GBK 尾字节 0x5C）。

### 2.3 剩余未精简清单（下一轮可直接照此推进）

`Core/Src/system_stm32f4xx.c`（13.5K，ST 生成）、`Core/Inc/stm32f4xx_hal_conf.h`（9.7K，ST 生成）**不在范围内**（非用户代码段）。
用户代码侧注释重镇（当前值）：`Application/Src/LDI/app_ldi_cmd.c` 12,181 / `Application/Src/app_dispatch.c` 8,035 /
`Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c` 6,926 / `Application/Src/app_render.c` 6,359 /
`Application/Src/LDI/app_ldi.c` 6,323 / `Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h` 6,254 /
`Application/Inc/LDI/app_ldi_cmd.h` 5,310 / `Device/Display/dev_display_22_1703.c` 5,245 /
`Device/Display/dev_display_22_1665.c` 5,242 / `Application/Src/Channel/app_udp.c` 5,240 /
`Application/Src/ProtocolParser_YunNan/app_yn_proto_cmd.c` 5,125 / `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c` 5,069 /
`Platform/Src/pl_eth.c` 4,913 / `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c` 4,891 /
`Device/Network/dev_dp83848.c` 4,798 / `Application/Src/ProtocolParser_ChongQing/app_cq_proto.c` 4,653 /
`Platform/Inc/lwipopts.h` 4,641（含本轮写入的新说明）/ `Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h` 4,546。
未逐文件精简的其余 ~200 个文件平均注释量较低（多为兄弟驱动同构的文件头 + 行内 why 注释），按同口径推进即可。

## 3. 构建基线（用户需求 ③）

命令：`make -j8`（GCC Debug）/ `make -j8 FACTORY=MM` / `make -j8 PROTO=CQ` /
`make -f .analysis/myanmar/Makefile.mm_nodisp CONFIG=EIDEMyanmarNoDisp -j8`（无显示等价口径）。

| 口径 | text | rodata | data | ccmram | bss | `_user_heap_stack` | SRAM 合计 / 余量 | elf md5 |
|---|---|---|---|---|---|---|---|---|
| `ALL/1_263/STD` 改动前 | 176576 | 203672 | 1656 | 39652 | 124860 | 2564 | 128896 / 2176 | — |
| **`ALL/1_263/STD` 改动后** | **167288** | **195736** | 1656 | 39652 | **123560** | 2560 | **127776 / 3296** | `f1856f2a42319fb007754861da2d856e` |
| 差值 | **−9288** | **−7936** | 0 | **0** | **−1300** | −4 | **+1120** | — |
| `FACTORY=MM` 改动前 → 后 | 175184 → **165752**（−9432） | 203640 → **195624**（−8016） | 1656 → 1656 | 39652 → 39652 | 124596 → **123296**（−1300） | 2564 → 2560 | 余 2256 → **3440** | `6e43aa0715986d84d6512be0bda2a734` |
| `PROTO=CQ` 改动前 → 后 | 163332 → **154044**（−9288） | 203008 → **195072**（−7936） | 856 → **852**（−4） | 37396 → 37396 | 121384 → **120084**（−1300） | 2560 → 2560 | 余 6408 → **7260** | `a1cacae790e2e79ad93efd2ebd1211e7` |
| 无显示等价口径 改动前 → 后 | 137632 → **131480**（−6152） | 197000 → **191928**（−5072） | 1584 → 1584 | 16084 → 16084 | 115796 → **114496**（−1300） | 2564 → 2560 | 余 10864 → **13912** | `4821cafc58a2b49e379163f16ba59088` |

- 告警：四口径各 **3 条**（全部为既有 `stm32f4xx_hal_flash_ex.c` 的 `unused parameter 'Banks'`）。
- 差异归因：`rodata −7936` + `text` 大部 = SEGGER RTT 打印字符串与格式化代码、LwIP 调试格式串（`LWIP_DBG_TYPES_ON 0`）、
  各协议诊断字符串；`bss −1300` = RTT 1KB 上行缓冲（`_acUpBuffer`）+ `s_pdbg_*` / `s_noproto_tick` / 诊断标志等静态态；
  `data −4`（CQ）= 删除的初始化静态量（`s_tcp_cli_last_err` 等）；`ccmram` 不变（RTT 不占 CCM）。
- **预期变化**：本次改动**有意**改变 elf 体积与调试输出能力（业务功能不变）。**烧录前核对**：
  `arm-none-eabi-size -A build/Debug/Project_STD.elf` 默认口径应为
  `.text=167288 .rodata=195736 .data=1656 .ccmram=39652 .bss=123560 ._user_heap_stack=2560`（详见 `doc/06-04` 与 `doc/17`）。
- **注释精简对二进制零影响**：注释精简完成后重建，段尺寸与精简前（`sizes/b3_clean.txt`）**逐字节一致**。

## 4. 行为不变证据

### 4.1 token 级「只删不改」审计（`check_token_equivalence.py`）

方法：以 `backup/sources_before.tar.gz`（改动前源码）为基准，剥注释 + 切 token（标识符/数字/字面量/预处理/逐字符标点），
用 `difflib` 求最小编辑脚本，断言「改动后 token 流 = 改动前的**子序列**」——任何新增/改写/重排都会失败。

```
[token-equiv] 文件 26（全部被改动的自有源码）：仅删除/已声明改写 26 / 整文件删除 0 / 违规 0；合计删除 token 7407
```
逐文件明细：`token_equiv_final.txt`。**9 处「已声明改写」**（白名单在工具内，逐条附理由）：

| 文件 | 改写 | 理由 |
|---|---|---|
| `Makefile` | 开关/指纹段 → 本轮说明注释 | 构建语义见 §3；新增注释文本 |
| `Platform/Inc/lwipopts.h` | `APP_LWIP_DBG_LEVEL` 覆盖块 → `LWIP_DBG_TYPES_ON 0` | LwIP 调试整体编出 |
| `Platform/Inc/pl_task_guard.h` | 删 RTT 打印分支（函数体 = `return tid`） | 判空语义不变 |
| `Device/Display/dev_display.c` | `dev_display_guard` 判断式简化为 `return (ptr != nullptr)` | 与被删告警块等价（原路径仅「返回 true / 走告警」两支） |
| `Application/Src/Channel/app_udp.c` | 删告警闩锁（纯打印限速态）+ 打印分支整块 | 绑定逻辑与计数不变 |
| `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c` | `const bool saved = yn_ol_screen_cfg_save();`+诊断分支 → `(void)yn_ol_screen_cfg_save();` | **写盘调用保留**，仅丢弃返回值 |
| `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c` | `_gz_ol_text_unescape_nl(…, &nl_count)` → `…, nullptr` | 换行计数仅供已删打印；被调函数已判空出参，**换行转换行为不变** |
| `Core/Src/syscalls.c` | 新增无输出 `__io_putchar` | 替代已移除的 RTT `_write` 落地点（消除跳 0 隐患） |
| 5 个文件的文件头注释重写（mm/yn_ol/anhui/…） | 仅注释文本 | 无 token 变化（注释不进 token 流，标注供审计可读） |

### 4.2 机器码 A/B（`ab_objdump.sh`，参考树 = 改动前源码 `/tmp/before_tree`，已实测基线一致）

27 个核心业务函数（mm 解析/驱动/映射、调度探测与分派、启动编排、显示守卫与位图、GZ_OL/Anhui/YN_OL 执行层、
TCP Server、GZ_OL UDP 服务、netcfg 版本落库、pl_net/pl_eth 初始化）：
**14 个反汇编逐条一致**；13 个有差异（`ab/*.diff`），差异**全部**位于被删除的调试语句及其引起的寄存器再分配处。

**关键反证：功能调用集不丢**（`bl` 目标集合 B ⊇ A，除已声明删除者）——

| 函数 | A 有而 B 无的 `bl` 目标 |
|---|---|
| `mm_apply_lamp` / `mm_apply_init` / `mm_arrow_map_toggle` | 仅 `<SEGGER_RTT_printf>` |
| `app_channel_dispatch` / `tcp_server_task` / `udp_gzol_task` / `app_board_net_cfg_fw_version_update` | 仅 `<SEGGER_RTT_printf>` |
| `init_task` | 仅 `<osDelay>`（删除的 2.5s 体检延时） |
| `vApplicationMallocFailedHook` | `<SEGGER_RTT_printf>` + 两个 `xPortGet*HeapSize`（仅服务已删打印） |
| `dev_display_guard` | `<SEGGER_RTT_printf>` + `<osKernelGetTickCount>`（限速告警用） |
| `pl_net_init` | `<SEGGER_RTT_printf>` + 两个 `xPortGet*HeapSize` |
| `frame_dispatch_task` | `<SEGGER_RTT_printf>`（4 → 0）+ 直连 `<rb_peek>`（3 → 0；**`_rb_head_fp` 调用 2 → 1**，被删的那次来自已删的 probe 诊断） |

`frame_dispatch_task` 功能路径逐项对拍（链接后 elf）：`rb_lock/rb_unlock/rb_avail/rb_read/rb_skip/bit_ctz/_rb_slot_of/
app_channel_get/osMessageQueuePut/osMessageQueueGet/osKernelGetTickCount` 条数**与改动前完全一致**；仅
`rb_peek` 的 2 处直连调用（诊断用）消失、`_rb_head_fp` 由 2 次调用降为 1 次（保留的正是 WAIT 预算指纹那一次）。
**结论：WAIT 预算 / 1 字节强制重同步 / 丢帧计数 / 通知重试等功能逻辑逐条保留。**

### 4.3 等价改写的行为论证

- `dev_display_guard`：原实现有显示 → `return true`；无显示 → 告警 + `return false`。改写后 `return (s_active_display != nullptr)` ——
  两支逐条等价（告警是唯一被删行为，属调试输出）。
- `pl_task_create_checked`：原实现在 `tid == nullptr` 时打印，随后原样返回 `tid`；改写后直接返回 `tid` —— 除打印外等价。
- `gz_ol` 的 `nullptr` 出参：`_gz_ol_text_unescape_nl` 内 `if (out_nl != nullptr) *out_nl = nl;` ⇒ 换行转换（唯一功能）不受影响。
- `yn_ol` 的 `(void)yn_ol_screen_cfg_save();`：调用保留（写盘副作用不变），仅丢弃返回值。
- LwIP：`LWIP_DBG_TYPES_ON 0` ⇒ `LWIP_DEBUGF` 打印分支恒假（官方开关语义），断言路径（`LWIP_PLATFORM_ASSERT`，`cc.h`）未动。

## 5. 脚本（校验工具）调整与结果

| 脚本 | 结果 | 本轮改动 |
|---|---|---|
| `.analysis/22_1665/verify_all.sh --fast` | **14 项全绿 / 1 跳过**（跳过 = `check_flash_artifact.sh`，因工作区 8×4 几何在 make 口径下 CCM 预存溢出，与本轮无关） | 无需改动 |
| `.analysis/myanmar/mm_frame_sim.py` | **157/157** | 4 条 RTT 日志断言 → 2 条更强断言（「无 SEGGER/RTT 残留」+「无 arrow map 打印/无硬编码引脚」）；新增 1 条 `SEGGER_RTT\|_RTT_DIAG\|app_diag` 零残留断言；`active-high/active-low` 与极性文案断言改为「已删且无残留」 |
| `.analysis/myanmar/check_display_guards.py` | **16/16 硬断言 + 95/95 调用点分类**（原 99/99；差值 = 随诊断删除的 `dev_display_get()` 调用点） | 无需改动（脚本按扫描计数，不硬编码 99） |
| `.analysis/myanmar/check_guard_disasm.py` | **原操作丢失 0**（92 函数 / 改动 27） | 新增两张「已声明」表：`RTT_ROUND_DELETED`（8 项整函数消失，含原因）+ `RTT_ROUND_REVIEWED`（8 项函数内指令减少，含原因）；未列入者仍判失败（检查强度不降） |
| `.analysis/myanmar/check_eide_targets.py` | **40/45**（5 条失败 = 2026-09-28 用户裁决的 Debug 现行口径差异，非回归）；`--emit-make` 仍可用（生成器从根 Makefile 取 `SRC_APPLICATION`/`DISP`/`INC_DIRS` 锚点，均保留） | 无需改动 |
| `.analysis/yn_ol/yn_ol_frame_sim.py` | **184/184** | §13.5/§13.6 两条 RTT 诊断断言 → 「无诊断残留」+「三处行为修复保留（单字节分片 / WAIT 预算头部指纹 / 编码容错）」 |
| `.analysis/heap/heap_ledger.py` | 预算不变（启动期峰值需求 13376B → 余 23480B；静态任务 23 个） | 无需改动 |

## 6. 文档同步（项目规则）

| 文档 | 更新内容 |
|---|---|
| `doc/CLAUDE.md` | ① 诊断开关条目整体改写为「2026-09-29 代码瘦身轮：RTT 调试设施已整体移除」（含判镜像替代手段、LwIP 处置、`printf` 承接）；② 内存布局：RTT Up 1KB 标为已删除 + 插入**现行四口径实测基线**；③ Platform 依赖树与 initcall 表去 `pl_rtt`；④ 调试章节：SEGGER RTT 段改写为「已移除 + 恢复方法」，原自证诊断/通道层诊断段落标为历史（并指明哪三处行为修复保留）；⑤ LwIP 调试档位段改写为 `LWIP_DBG_TYPES_ON 0` 口径；⑥ 构建卫生 R1 的核对尺寸改为新基线；⑦ 0x03 版本判断路径、堆纪律、GZ_OL 端口告警措辞随改 |
| `doc/构建开关总表.md` | 新增 **§0「本轮删除的开关」**（含现等效做法）+ 8 行开关标「〔2026-09-29 已删除〕」+ §5 新增本轮修订条目 |
| `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` | 顶部「最新采样」改为本轮四口径（原采样降为「上一条」）+ 变更记录新增本轮条目 |
| `doc/14_贵州治超屏协议/README.md` | 顶部加「RTT 已移除」横幅；§12.4/§12.8 标为已删除（保留历史）；§12.7 的「无 `[diag]` 横幅 = 旧镜像」判据标为失效并给替代；端口回退/同号跳过的「RTT 告警」措辞标注 |
| `doc/15_云南治超屏协议/README.md` | 顶部横幅（同上）；全篇「RTT 告警」措辞标注 |
| `doc/16_缅甸2D费显协议/README.md` | 顶部横幅（含本模块新段尺寸）；§10 守卫告警段改为「判空返回 false（告警已删）」；§13 期望表标注 RTT 列失效；脚本计数与调用点计数更新（157/157、95/95） |
| **新增** `doc/17_代码瘦身轮_20260929.md` | 本轮 as-built：删了什么 / 保留什么（含待裁决）/ 新基线表 / 行为不变证据方法 |

## 7. 需用户裁决 / 遗留项

1. **保留的只读观察 API（原 RTT 消费者已删，非 RTT 依赖、属公开接口）**：
   `app_dispatch_qfull_drops/resync_count/notify_drops`、`app_render_chip_info_get`、`app_render_glyph_probe`、
   `app_udp_gzol_bind_status(_str)`、`g_tcp_server_connected`、`s_mm_lamp_state` 等 —— **是否一并删除请示下**（本轮按「禁止改接口」保留）。
2. `Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c` 的诊断补丁 `xLastFailedAllocSize` 已无消费者（第三方文件未动）。
3. `.eide/eide.yml` 已改（移除 SEGGER 源/路径 + `pl_rtt.c`）：**请在 EIDE 中执行 `EIDE: Reload Project`**，
   否则 EIDE 内存模型回写会丢失本次编辑（并可能报 `undefined reference`）。
4. `.eide/eide.yml` 的 `targets.Release` 仍含历史垃圾路径（`Drivers/RTT/*`、`Drivers/BSP/*`）——本轮未动（该目标非可用构建入口）。
5. 注释精简剩余清单见 §2.3（可按目录继续）。
6. 未烧录、未提交、未执行任何 git 网络/写操作、未改 git 配置。

## 8. 证据文件索引（`.analysis/cleanup_20260929/`）

| 文件 | 内容 |
|---|---|
| `inventory.md` / `rtt_sites.txt` / `inventory_raw.txt` | RTT 清单（分类 + 原始行号） |
| `backup/sources_before.tar.gz` | 改动前源码整份备份（456 项，含未跟踪文件）；`backup/eide.yml.before` |
| `STATE.md` | 批次进度 / 断点续做说明 |
| `stats.py`、`stats_*`（`/tmp`） | 注释规模统计工具与前后数据 |
| `check_token_equivalence.py` / `token_equiv_final.txt` | token 级「只删不改」审计（含 9 处已声明改写白名单） |
| `ab_objdump.sh` / `ab/*.diff` | 27 个核心函数机器码 A/B 与差异明细 |
| `sizes/*.txt` + `sizes/*.build.log` | 四口径构建取证（段尺寸 / md5 / 告警；最终版 `final_*.txt`） |
| `cc_check.sh` | 单文件语法检查（与 Makefile 同口径） |
| `build_and_record.sh` | 构建取证助手 |
