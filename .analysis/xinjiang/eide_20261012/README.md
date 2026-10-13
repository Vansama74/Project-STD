# 新疆模块 EIDE 收录轮（2026-10-12）证据索引

任务：把 `Application/{Src,Inc}/ProtocolParser_XinJiang/` 收录进 `.eide/eide.yml` 并编入 `targets.Debug`。
**未运行 EIDE、未触发任何 EIDE 构建、未 git commit、未改 git 配置、未改源码、未动其它既有文件。**

## 改了什么（`.eide/eide.yml`，相对备份 +10 行 / −0 行）

| # | 位置 | 内容 |
|---|---|---|
| 1 | `virtualFolder → Application → protocol` | 新增 `ProtocolParser_XinJiang` 虚拟文件夹，含 3 个 .c（插在 `rls` 之后；候选池 249 → 252） |
| 2 | `targets.Debug.defineList` | 追加 `XJ_FAULT_SCREEN=0` |
| 3 | `targets.Debug.incList` | 追加 `Application/Inc/ProtocolParser_XinJiang` |
| 4 | `targets.Myanmar.incList` | 追加同一路径（兜底，防误编入缺 `-I`） |
| 5 | `targets.Myanmar.excludeList` | 追加 `<virtual_root>/Application/protocol/ProtocolParser_XinJiang`（3 文件全排除） |
| 6 | `targets.Release` | **不动**（incList 仍含已删除的 `Drivers/BSP/*` 等 = 历史残留、非可用入口） |

## 关键结论

- Debug 编入 **206 → 209** 文件；Myanmar 编入 **207 不变**（排除 42 → 45）；Release 不变。
- `check_eide_targets.py`：**改前 14/15 → 改后 14/15**（唯一失败 = 既有 Debug `PROTO_CHONGQING` ↔ CQ 模块错配 I6）。
- Debug 等价口径真编译（同旗标 A/B）：`.text +1616` / `.rodata +248` / `.ccmram +1544` / `.bss +32`，
  告警仍 3 条既有 HAL；`XJ_FAULT_SCREEN=0` 净效果 = text −288 / rodata −16 / bss −8（与 doc/19 §6 一致）。
- `XJ_FAULT_SCREEN=0` 生效判据：链接后 `xj_fault_screen_show` / 「系统维护中」= 0/0；同 `.o` = 1/1；
  正对照 `build/xj_b2/Project_STD.elf` = 1/1。新疆符号（含 CCMRAM 队列/任务栈）在镜像内。
- Myanmar 等价 Makefile 改前 == 改后（逐字节），等价构建仍链接通过、0 个新疆符号。

## 本轮产物

| 文件 | 说明 |
|---|---|
| `eide.yml.before_xj` | `.eide/eide.yml` 改前原文（md5 `fe52ce12d30b60a4f090a0bc081e4fa2`） |
| `apply_yaml_edits.py` | 一次性外科式编辑脚本（含锚点断言、幂等保护） |
| `check_before.txt` / `check_after.txt` / `check_after_verbose.txt` | 结构不变量校验（改前 / 改后 / 改后含完整编入排除清单） |
| `Debug.before.mk` / `Debug.after.mk` / `Debug.after_faulton.mk` / `Myanmar.before.mk` / `Myanmar.after.mk` | `--emit-make` 生成的等价口径 Makefile（`after_faulton` = 手工去掉 `-DXJ_FAULT_SCREEN=0` 的对照） |
| `build_before.log` / `build_after.log` / `build_after_faulton.log` / `build_myanmar.log` | 四次全量构建日志（独立 `BUILD_DIR`，未触碰 `build/Debug/`） |
| `size_before.txt` / `size_after.txt` / `size_after_faulton.txt` / `size_myanmar.txt` | `arm-none-eabi-size -A` 段尺寸记录 |

产物构建目录（`build/` 已被 `.gitignore` 覆盖）：`build/xj_eide_dbg_before`、`build/xj_eide_dbg`、
`build/xj_eide_dbg_faulton`、`build/xj_eide_mm`。
〔2026-10-12 工作区清理轮：这四个目录已删除（中间产物，共 ~41MB）；本目录的 `build_*.log` / `size_*.txt` /
`Debug.*.mk` / `assertions.txt` 为证据、保留，重跑各自对应命令（见 `build_*.log` 首部）即可重建目录。
清理记录见 `.analysis/cleanup_20261012_workspace/README.md`。〕

## 段尺寸汇总（等价口径，`-Og -g`，各自全量 `-B -j12`）

| 口径 | `.text` | `.rodata` | `.data` | `.ccmram` | `.bss` | `._user_heap_stack` | .o 数 | 告警 |
|---|---|---|---|---|---|---|---|---|
| Debug 改前 | 133544 | 192232 | 1632 | 17260 | 116420 | 2564 | 206 | 3 |
| Debug 改后（新疆 + 开关 0） | 135160 | 192480 | 1632 | 18804 | 116452 | 2564 | 209 | 3 |
| Debug 对照（新疆 + 开关 1） | 135448 | 192496 | 1632 | 18804 | 116460 | 2564 | 209 | 3 |
| Myanmar（无显示）改后 | 132504 | 191968 | 1584 | 16084 | 116176 | 2560 | 207 | 3 |

## 待办 / 提示

1. **用户在 EIDE 执行 `EIDE: Reload Project`**（GUI 里确认 3 个文件在 Debug 编译列表内、Myanmar 中被排除）；
   Reload 后 EIDE 会按自己格式回写 yml，属预期。
2. `doc/CLAUDE.md`、`doc/构建开关总表.md` 里「EIDE 待用户在 GUI 收录」的表述现已过时 —— 超出本轮授权未改。
3. 前置检查：Debug 口径内已有四川 ETC/MTC/治超为 RS485/RS232 提供 RB 体（本模块只 acquire 不 provide）。


---

## 2026-10-12 第二轮补注（本文件「待办/提示」第 3 条的更正）

> 本文件写于 15:49，当时断言「**前置检查**：Debug 口径内已有四川 ETC/MTC/治超为 RS485/RS232 提供 RB 体
> （本模块只 acquire 不 provide）」——**该断言在 16:25 之后不再成立**：用户在 EIDE GUI 里给
> `targets.Debug.excludeList` 补了 `ProtocolParser_SiChuang_{Overload,MTC,ETC}` 三行（相对本目录
> `eide.yml.before_xj` 备份的**唯一**后续差异），Debug 目标变成「IAP + LDI + 新疆」⇒ **RS485/RS232 两槽
> 皆无提供者** ⇒ 新疆模块 `app_proto_acquire_buf()` 双双失败、init 静默 return（不注册/不绑定）⇒
> `app_boot.c` 通道门控连 `app_rs485_start/app_rs232_start` 也跳过（UART RX 都没挂）⇒ 现场「发帧零反应」。
>
> 实测证据（`build/Debug` 17:25 那次 EIDE 构建）：`nm` **无 `rb_provide_rs485/rs232`**、
> `g_rb_provide` 表 = `791c0408 00000000 00000000`、`Project_STD.objlist` 仅 28 个 app 源文件。
>
> 该缺陷已在本模块内部修复（自备两槽 `RB_PROVIDE_WEAK`，见 `doc/19` §2.2/§6.3.1），
> 因此「目标内必须有其它协议提供 RB」这条前置条件**已作废**。完整取证与修复见
> `.analysis/xinjiang/round2_pdf_fulltext_and_rb.md`。
