# Makefile 口径指纹 stamp 修复报告（构建正确性缺陷）

**日期**：2026-09-14
**范围**：`Project-STD-main/Makefile`（唯一代码改动）+ 5 份文档同步（见 §6）
**纪律遵守**：未改 22-1703 驱动 / 贵州治超协议业务代码；未改全局 git 配置；未 commit；
未执行任何 git 网络操作；默认口径保持 `DISP=1_263`、整机 7×4 = 224×64、搜索应答单播回源。

---

## 0. 摘要

| 项 | 结论 |
|---|---|
| 缺陷 | elf 依赖只有 `.o` 列表：源列表（`PROTO` 剔 LDI、`DISP` 换模组）与开关（`DEFINES`/`CFLAGS`）不在依赖图上 → 切口径静默复用上一口径 elf，**且旧口径对象仍参与新口径链接** |
| 修复 | 新增口径指纹 `build/$(CONFIG)/.build_stamp`（口径变量 + 源列表 + Makefile cksum），同时作为**每个 `.o` 与 elf** 的先决条件；内容变化才改写（`cmp` 后 `mv`），口径不变不触碰 |
| 附带 | `-MMD -MP` 头文件依赖；`.DEFAULT_GOAL := all`（stamp 规则在 `all` 之前，否则劫持默认目标） |
| 验证 | 四口径 + 往返切换 + 增量 + `make clean` 全通过（§3，9 次调用 + 早前 16 轮，全部 `EXIT=0`）；同口径 elf **md5 逐字节复现** |
| 更正 | 既有文档中 `PROTO=ALL` 两口径段尺寸为污染态（+416 text / +40 rodata）；干净值为 163652 / 163924（§4） |
| 遗留问题 | **6 条**（§7） |

---

## 1. 修改点（diff 级说明）

### 1.1 顶层默认目标（`Makefile:3-7`）

```make
# ---- Default Goal ----
# 显式指定默认目标：Makefile 中先于 all 定义的规则（如口径指纹 stamp）若成为
# 文件中第一个普通目标，会劫持无参数 `make` 的默认目标
.DEFAULT_GOAL := all
```

> 必要性（实测踩坑）：stamp 规则位于 `all` 之前，若不加这一行，`make -j8` 的默认目标
> 变成 `.build_stamp`——只跑 stamp 配方、**不编译不链接且静默返回 0**（本轮首跑即命中：
> `real 0m0.035s`、零输出、elf 未变）。

### 1.2 头文件依赖（`Makefile:117-120`）

```make
# 头文件依赖自动生成（.d 与 .o 同目录，-MP 为头文件补空规则防删除报错）：
# 头文件改动由 .d 记录触发重编译，不依赖手工全量重建
CFLAGS += -MMD -MP
```

### 1.3 口径指纹 block（`Makefile:434-477`）

```make
# ---- Header Dependencies ----
# -MMD 生成的依赖文件（首次构建时不存在，-include 静默忽略）
DEP_ALL = $(OBJ_ALL:.o=.d)

# ---- Build Configuration Stamp（口径指纹） ----
STAMP_FILE = $(BUILD_DIR)/.build_stamp

# Makefile 内容指纹（cksum：内容相同则同一值，改注释也会变——偏保守，保证不漏重建）
MAKEFILE_CKSUM := $(shell cksum Makefile 2>/dev/null | cut -d' ' -f1)

define BUILD_STAMP_TEXT
STAMP_VERSION=1
CONFIG=$(CONFIG)
TOOLCHAIN=$(TOOLCHAIN)
PROTO=$(PROTO)
DISP=$(DISP)
DEFINES=$(DEFINES)
CFLAGS=$(CFLAGS)
LDFLAGS=$(LDFLAGS)
MAKEFILE_CKSUM=$(MAKEFILE_CKSUM)
SRC_FILES=$(SRC_ALL)
endef
export BUILD_STAMP_TEXT

$(STAMP_FILE): FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' "$$BUILD_STAMP_TEXT" > $@.tmp
	@cmp -s $@.tmp $@ && rm -f $@.tmp || mv -f $@.tmp $@

# ---- Targets ----
.PHONY: all clean compile_commands FORCE

FORCE:
```

设计要点：

- **幂等**：`FORCE` 只保证配方每次执行；配方内部 `cmp` 相等即丢弃 `.tmp`，**不触碰**
  stamp 的 mtime → 口径不变时下游 `.o`/elf 全部保持最新（实测 no-op 0.0s）。
- **口径变化必重编**：`PROTO`/`DISP`/`CONFIG`/`TOOLCHAIN`/`DEFINES`/`CFLAGS`/`LDFLAGS`/
  源列表任一变化 → stamp 内容变 → 改写 → mtime 变新 → **全部 `.o` 重编译 + elf 重链接**。
  这同时消灭了旧缺陷的第二半（旧口径 `.o` 混入新口径链接）。`.o` 恒在「最近一次口径」
  下编译：切换到任一口径都会把该口径源列表内的每个 `.o` 判为过期。
- **未来开关自动纳入**：开关只要流入 `DEFINES`/`CFLAGS`/`LDFLAGS`/源列表即自动纳管；
  即便是不流入这些变量的新开关，改 Makefile 本身也会因 `MAKEFILE_CKSUM` 变化触发一次
  全量重建（偏保守，实测：仅改一行注释即触发 237 编译 + 1 链接；见 §3.2 V1）。
- **shell 兼容**：只用 `mkdir/printf/cmp/mv/cksum/cut`（POSIX）；stamp 内容经**环境变量**
  传递（`export BUILD_STAMP_TEXT` + `printf '%s\n' "$VAR"`），不做命令行拼接，规避引号/
  空格/多行转义问题。
- **不破坏 `make clean`**：clean 仍 `rm -rf $(BUILD_DIR)`（stamp/.d 随之消失）。

### 1.4 elf 规则（`Makefile:493-499`）

```make
# elf 依赖 = 源文件列表生成的全部 .o + 口径指纹 stamp（切口径必然重链接）+ 链接脚本
# 链接输入必须用 $(OBJ_ALL)（不可用 $^）——保持源文件收录序；initcall 段序依赖它
$(BUILD_DIR)/Project_STD.elf: $(OBJ_ALL) $(STAMP_FILE) $(LDSCRIPT)
	@echo "Linking $@"
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJ_ALL)
```

- 新增依赖：`$(STAMP_FILE)`（口径）与 `$(LDSCRIPT)`（链接脚本改动也重链接）。
- **链接输入改为 `$(OBJ_ALL)` 而非 `$^`**：`$^` 现在会包含 stamp 与 `.ld`，直接传给
  链接器会报文件格式错误；且必须保持源文件收录序（initcall 同层调用序依赖它）。
- `hex`/`bin` 依赖 elf，自动联动（实测三者 mtime 同秒更新）。

### 1.5 编译规则与 `-include`（`Makefile:506-518`）

```make
# ---- Compile Rule ----
# .o 同时依赖口径指纹 stamp：口径切换（DEFINES/源列表等变化）时全部重编译，
# 避免旧口径的 .o 参与新口径链接（stamp 内容不变则不触碰，增量不受影响）
$(BUILD_DIR)/%.o: %.c $(STAMP_FILE)
	@echo "Compiling $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) compile_commands.json

# 头文件依赖（-MMD 生成；首次构建缺失时静默忽略，不尝试构建）
-include $(DEP_ALL)
```

---

## 2. 修改前缺陷复现（EXIT 0 静默错产物）

会话开始时 elf 为上一轮的 `DISP=22_1703 PROTO=CQ` 产物（`.text` 150732 / `.ccmram` 39164），
直接执行默认口径构建：

```
$ make -j8                       # 期望 = PROTO=ALL / DISP=1_263
==== Build complete ====
   text	   data	    bss	    dec	    hex	filename
 348676	    940	 164600	 514216	  7d8a8	build/Debug/Project_STD.elf
EXIT=0
$ arm-none-eabi-size -A build/Debug/Project_STD.elf | grep -E '^\.text|^\.ccmram'
.text                150732
.ccmram              39164        # ← 22_1703 的 CCM 值：产物与命令行口径不符
```

另以「修复前 Makefile + 独立构建目录」复刻上一轮流程，**逐项复现**了文档记录的
`PROTO=ALL` 两行（见 §5.1），证明当时靠「删 `Project_STD.*`」并不能得到正确口径产物
（对象未重编，仍是上一 CQ 口径）。

---

## 3. 验证证据（全部实跑）

### 3.1 最终验证序列

脚本（可复现，等价于本节命令）：

```bash
# 依次执行；每步记录 make EXIT、编译/链接条数、耗时与 5 个段尺寸
make -j8                          # V1
make -j8                          # V2（增量）
make -j8 PROTO=CQ                 # V3
make -j8                          # V4（A→B→A 往返回 A）
make -j8 DISP=22_1703 PROTO=CQ    # V5
make -j8 DISP=22_1703             # V6
make -j8 DISP=22_1703             # V7（增量）
make -j8 DISP=22_1703 PROTO=CQ    # V8（C→B 反向切换）
make -j8                          # V9（回默认口径）
```

实测结果（`arm-none-eabi-size -A build/Debug/Project_STD.elf`）：

| # | 命令 | EXIT | 编译 | 链接 | 耗时 | .text | .rodata | .data | .ccmram | .bss |
|---|---|---|---|---|---|---|---|---|---|---|
| V1 | `make -j8` | 0 | **237** | **1** | 1.9s | 163652 | 197760 | 1664 | 37084 | 126348 |
| V2 | `make -j8`（同口径） | 0 | 0 | 0 | 0.0s | 163652 | 197760 | 1664 | 37084 | 126348 |
| V3 | `make -j8 PROTO=CQ` | 0 | **232** | **1** | 1.5s | 150476 | 197104 | 860 | 37084 | 122872 |
| V4 | `make -j8`（B→A 往返） | 0 | **237** | **1** | 1.5s | 163652 | 197760 | 1664 | 37084 | 126348 |
| V5 | `make -j8 DISP=22_1703 PROTO=CQ` | 0 | **232** | **1** | 1.5s | 150732 | 197192 | 940 | 39164 | 122872 |
| V6 | `make -j8 DISP=22_1703` | 0 | **237** | **1** | 1.6s | 163924 | 197840 | 1744 | 39164 | 126348 |
| V7 | `make -j8 DISP=22_1703`（同口径） | 0 | 0 | 0 | 0.0s | 163924 | 197840 | 1744 | 39164 | 126348 |
| V8 | `make -j8 DISP=22_1703 PROTO=CQ`（反向切换） | 0 | **232** | **1** | 1.5s | 150732 | 197192 | 940 | 39164 | 122872 |
| V9 | `make -j8`（回默认口径） | 0 | **237** | **1** | 1.6s | 163652 | 197760 | 1664 | 37084 | 126348 |

补充：V1 的 237 编译含一次「仅改注释」的 Makefile cksum 变化触发的全量重建（预期行为）。

**逐字节确定性**（同一口径在不同轮次/不同顺序下产物完全一致，`md5sum` elf）：

| 口径 | md5 | 复现轮次 |
|---|---|---|
| `ALL` `1_263` | `2f02963213367a45279cd5f45ab33746` | R1 = R4 = R8（往返）= R16（clean 后）= V1 = V4 = V9 = 最终态 |
| `CQ` `1_263` | `36ef6fb13a7c41906b507c8d10d4ed8a` | R3 = R10 = V3 |
| `ALL` `22_1703` | `3cecd9a32bfab51f11f095151e85d3d1` | R5 = R7 = R14 = V6 |
| `CQ` `22_1703` | `7ba2b3ef8ed918fbf9e3b1a6802816e6` | R6 = R12 = V5 = V8 |

`hex`/`bin` 与 elf 时间戳同秒联动（例：elf 14:56:03.926 / bin 14:56:03.930 / hex 14:56:03.932）。

### 3.2 增量行为（同口径重复 make 不重链接）

- V2、V7：编译 0 / 链接 0，耗时 0.0s，elf **md5 与 mtime 均未变**。
- stamp 幂等性直证（`.build_stamp` mtime 前后比对）：

```
$ stat -c '%y' build/Debug/.build_stamp     # 14:56:02.420386131
$ make -j8 ; echo $?                        # 0（0 编译 0 链接）
$ stat -c '%y' build/Debug/.build_stamp     # 14:56:02.420386131  ← 未被触碰
```

- 反向（口径切换必改写 stamp）：

```
switch CQ   : 14:56:02.420386131 -> 14:56:29.298752552
switch back : 14:56:29.298752552 -> 14:56:30.986901180
```

### 3.3 `make clean` 与从零构建

```
$ make clean
rm -rf build/Debug compile_commands.json          # EXIT=0（0.127s）
$ test -d build/Debug ; echo $?                   # 1 → 目录已删除
$ make -j8                                        # EXIT=0，237 编译 + 1 链接，1.7s
$ arm-none-eabi-size -A build/Debug/Project_STD.elf | grep -E '^\.(text|rodata|data|ccmram|bss)'
.text                163652
.rodata              197760
.data                  1664
.ccmram               37084
.bss                 126348
$ md5sum build/Debug/Project_STD.elf
2f02963213367a45279cd5f45ab33746                  # 与修复后既有产物逐字节一致
```

`-include $(DEP_ALL)` 在 clean 后（`.d` 全部缺失）无告警、不尝试构建缺失 `.d`。

### 3.4 头文件依赖（本轮新增 `-MMD -MP`）

```
$ touch Application/Inc/app_scroll.h
$ make -j8
Compiling Application/Src/app_scroll.c
Compiling Application/Src/RLS/app_rls_cmd.c
Compiling Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c
Compiling Application/Src/ProtocolParser_Anhui/app_anhui_proto_cmd.c
EXIT=0   Compiling=4  Linking=1
$ make -j8            # 再跑：0 编译 0 链接
==== Build complete ====
```

即：头文件改动只重编**依赖它的 4 个 TU**并重链接，不多不少（依赖图由 238 个 `.d` 表达）。

### 3.5 其它守卫与既有缺陷

| 检查 | 命令 | 结果 |
|---|---|---|
| 非法 `DISP` 仍报错 | `make DISP=bogus -j8` | `Makefile:67: *** DISP must be 1_263 or 22_1703, got 'bogus'。 停止。`，**EXIT=2** |
| `compile_commands` 目标 | `make compile_commands` | **EXIT=2，`SyntaxError: invalid syntax`** —— 用 `git show HEAD:Makefile` 的原始版本复测**同样失败**，且该 recipe 与 HEAD 逐字节一致 → **既有缺陷，非本轮引入**（见 §7 遗留 2） |
| scratch 目录清理 | `ls build/` | 仅 `Debug/ Release/ Testing/`（我创建的 `Scratch/`、`Measure/` 已删除） |
| 工作区改动面 | `git status --porcelain` | 业务代码零改动；`Makefile` 为本轮唯一被改的代码文件 |

---

## 4. 段尺寸比对表（基线 vs 修复后）

| 口径 | 基线（上一轮记录） | 修复后干净值 | 差 | 判定 |
|---|---|---|---|---|
| `ALL` `1_263` | text 164068 / rodata 197800 / data 1664 / ccm 37084 | text **163652** / rodata **197760** / data 1664 / ccm 37084 | **-416 / -40** | 基线为污染态，已更正（§5） |
| `CQ` `1_263` | text 150476 / data 860 / ccm 37084 | text **150476** / data 860 / ccm 37084 | 0 | 逐项一致 ✅ |
| `ALL` `22_1703` | text 164324 / rodata 197880 / data 1744 / ccm 39164 | text **163924** / rodata **197840** / data 1744 / ccm 39164 | **-400 / -40** | 基线为污染态，已更正（§5） |
| `CQ` `22_1703` | text 150732 / data 940 / ccm 39164 | text **150732** / data 940 / ccm 39164 | 0 | 逐项一致 ✅ |

**RAM 相关段（`.data` / `.ccmram` / `.bss` / SRAM 合计 / 余量）四条口径全部不变**——
即 `doc/06-04` §7 的内存账目与结论（SRAM 余量 496B / 4776B / 416B / 4696B）不受影响，
仅 `.text`/`.rodata` 及由 `.text` 派生的增量需要更正。

由干净 A/B 重算的增量（替换原污染值）：

| 项 | 原记录 | 干净值 | 依据 |
|---|---|---|---|
| 贵州治超 text | +2544 | **+2128**（`CQ` +2144） | 同树含/不含 GZ_OL 对照：161524→163652、148332→150476 |
| 贵州治超 rodata | +256 | **+216** | 197544→197760、196888→197104 |
| 22-1703 相对 1-263 text | +256 | **+272** | 163652→163924（`ALL`） |
| 22-1703 rodata / data / ccmram | +80 / +80 / +2080 | 不变 | 197760→197840 等 |

---

## 5. 根因取证（为什么基线差 416 字节）

### 5.1 复现「修复前流程」→ 精确复现基线

用 `CONFIG=Scratch` 独立目录 + `make -o build/Scratch/.build_stamp`（禁掉 stamp 依赖，
等价于修复前「删 elf 后 make」）重放上一轮流程：

```
STEP0  make -j8 CONFIG=Scratch DISP=22_1703 PROTO=CQ   # 造出与 14:37 同一状态
STEP1  删产物 → make -j8 CONFIG=Scratch                # ALL/1_263
       .text 164068 / .rodata 197800 / data 1664 / .ccmram 37084 / .bss 126348   ← 基线 1
       Compiling=6  Linking=1                          ← 只有 6 个文件重编（其余复用 CQ 对象）
STEP2  删产物 → make -j8 CONFIG=Scratch PROTO=CQ       #  CQ/1_263
       .text 150476 / .rodata 197104 / data 860 / .ccmram 37084 / .bss 122872
STEP3  删产物 → make -j8 CONFIG=Scratch DISP=22_1703   # ALL/22_1703
       .text 164324 / .rodata 197880                                          ← 基线 3
STEP4  删产物 → make -j8 CONFIG=Scratch DISP=22_1703 PROTO=CQ
       .text 150732 / .rodata 197192
```

四行与上一轮记录**逐项吻合**（含 CQ 两行）→ 证明：
① 修复后干净重采得到的 163652/163924 才是 `PROTO=ALL` 的正确值；
② 两项 CQ 值本就干净（对象口径与命令行一致），故修复前后一致；
③ 上一轮报告基线行用 `make -B`（全量重编）采样，因而干净——与本次干净重采
161524 / 148332 也逐项吻合（见 §5.4）。

### 5.2 对象级与符号级归因（416 text / 40 rodata 的构成）

污染树（`build/Scratch`）与干净树（`build/Debug`）逐对象比对，**只有 2 个对象尺寸不同**：

| 对象 | 干净 | 污染 | Δ |
|---|---|---|---|
| `Application/Src/Channel/app_udp.o` | 1469 | 1497 | **+28**（`_udp_cq_read_port` 走 Sector1 `udp_port` 分支） |
| `Application/Src/ProtocolParser_ChongQing/app_cq_proto.o` | 945 | 1001 | **+56**（`cq_proto_timer_task` 含心跳/故障屏计时） |

但 elf 级 `.text` 差 **416**、`.rodata` 差 **40**——差额来自 `--gc-sections`
**保留集合**变化（符号集合精确比对，两个 elf 的符号级差异仅 4 项 + 2 项变大）：

| 符号 | 大小 | 类型 | 说明 |
|---|---|---|---|
| `cq_render_fault_screen` | 316 | T | 定义在 `app_cq_proto_cmd.o`；干净 ALL 链接中被 GC 丢弃（污染链接里被保留） |
| `s_cq_fault_line2` | 17 | r | 故障屏文案（`CQ_FAULT_SCREEN=1` 分支） |
| `s_cq_fault_line0` | 13 | r | 同上 |
| `app_factory_test_active` | 12 | T | 故障屏引用链经 `app_factory_mode_interrupt` 拉活的工厂测试标志 |

合计 316+17+13+12+56+28 = **442 字节符号**，段面摊为 **+416 `.text` / +40 `.rodata`**（其余
为对齐/GC 布局效应）。RAM 段零差异 → 与实测 `.data`/`.bss`/`.ccmram` 完全相同吻合。

**归因链**：`-DPROTO_CHONGQING`（CQ 口径）→ `CQ_FAULT_SCREEN` 默认 1 → 陈旧
`app_cq_proto.o` 引用故障屏 → `app_cq_proto_cmd.o` 的 316B 函数 + 30B 文案 +
`app_factory_test_active` 被保留。这正是「旧口径对象混入新口径链接」的典型后果，
也是本轮 stamp 把 `.o` 纳入口径依赖要根治的对象。

### 5.3 `-MMD -MP` 不改变目标码（排除修复引入 codegen 变化）

```
$ CFLAGS=$(sed -n 's/^CFLAGS=//p' build/Debug/.build_stamp)
$ arm-none-eabi-gcc $CFLAGS                 -c -o /tmp/stampfix/with_mmd.o    .../app_cq_proto.c
$ arm-none-eabi-gcc ${CFLAGS/-MMD -MP/}     -c -o /tmp/stampfix/without_mmd.o .../app_cq_proto.c
$ md5sum /tmp/stampfix/with_mmd.o /tmp/stampfix/without_mmd.o
438f38b5f8cd21292dcc54982d2feb87  /tmp/stampfix/with_mmd.o
438f38b5f8cd21292dcc54982d2feb87  /tmp/stampfix/without_mmd.o      # 逐字节相同
```

（`-MMD` 仅多产出 `.d` 依赖文件，不进入目标码。）

### 5.4 干净 A/B 重算增量

用临时 makefile 片段（不入库）在独立目录 `CONFIG=Measure` 剔除 GZ_OL 目录后重编：

```
clean ALL/1_263 无 GZ_OL : .text 161524 / .rodata 197544 / .data 1664 / .ccmram 35948 / .bss 126332
clean CQ /1_263 无 GZ_OL : .text 148332 / .rodata 196888 / .data  860 / .ccmram 35948 / .bss 122856
```

两者与上一轮记录的「改动前基线」行**逐项吻合**（说明该行本就干净），据此得到 §4 表中
的干净增量（治超 +2128/+2144 text、+216 rodata；22-1703 text +272）。

---

## 6. 文档更新清单

| 文件 | 章节/位置 | 更新内容 |
|---|---|---|
| `Makefile` | 头部、CFLAGS、Object Files 段、elf 规则、编译规则、文件尾 | 本次修复本体（§1 全部）；注释指向 `doc/构建开关总表.md` §4 |
| `doc/构建开关总表.md` | 新增 **§4「口径指纹 stamp 与重链接依赖」**（含 §4.1 采样偏差归因）；原「§4 修订记录」→ **§5** 并新增 2026-09-14 条目 | 修复机制、使用纪律（不再手工删产物）、新增开关如何纳入、污染态偏差的符号级归因 |
| `doc/CLAUDE.md` | 「构建系统」章节（`Clang Release` 之后新增两条 bullet）+ 「选编口径」的**实测段尺寸**条 | 口径切换必重链接 / 不再删产物 / `-MMD -MP` 头依赖；段尺寸更正为 163652 / 163924 并标注原值与成因 |
| `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` | §7 采样背景、四口径表、表下新增**更正段**、增量账表、文件末尾 2026-09-14 修订条目 | 采样流程改为 stamp 机制；`PROTO=ALL` 两行 text/rodata 更正；增量更正（治超 +2128/+216、22-1703 +272）；原始数值标注为污染态 |
| `doc/14_贵州治超屏协议/README.md` | §6「内存与构建状态」四口径表 + 增量句 + 新增口径切换说明 | 同上一组数字更正；补 stamp 机制指引；SRAM/CCM 账目不变 |
| `doc/01_显示系统/22-1703模组驱动分析与迁移记录.md` | 「链接实测增量」表 | text 164068/164324/+256 → 163652/163924/**+272**；rodata → 197760/197840（+80 不变） |
| `.analysis/9k23881580/integration_report.md`（上一轮报告，**非 doc/**） | §3 表下新增更正引注；§5.1「发现 A」追加「已解决」条目 | 标注第 1/3 行为污染态并给出更正值；记录发现 A 的处置（指向本报告） |

> 未改动：`doc/03`（CQ 协议）、`doc/13`（安徽）等其余文档未含受影响的段尺寸数字
> （已用 `164068|164324|150476|150732|197800|197880|删产物|源列表变化不是 make 的依赖`
> 全库搜索确认；其余命中均为更正后的说明性引用）。

---

## 7. 遗留风险与未处理项（6 条）

1. **新增开关若既不入 `CFLAGS/DEFINES/LDFLAGS` 也不入源列表**：仍会被 `MAKEFILE_CKSUM`
   兜住（改 Makefile 即触发一次全量重建），代价是该次重建偏保守；若希望「只在该开关变化
   时才重建」，需把新变量追加进 `BUILD_STAMP_TEXT`（`STAMP_VERSION` 可同步 +1）。
2. **`make compile_commands` 目标既有缺陷（未修，超出本轮范围）**：recipe 内嵌 python
   one-liner 的 `\`+`for` 续行非法 → `SyntaxError`、`make EXIT=2`。已用 `git show
   HEAD:Makefile` 复测确认与本次改动无关；`doc/CLAUDE.md` 记录的 `bear --output …` 路径不受影响。
3. **`make clean` 会连带删除 EIDE 在同目录（`build/$(CONFIG)`）的构建产物**（`.obj/`、`.cache/`、
   `.lnp`、`.objlist`、`Project_STD.map.*` 等）——既有行为（`rm -rf $(BUILD_DIR)`），非本次引入；
   若需保留，应考虑给 EIDE 换独立输出目录。
4. **口径切换 = 全量重编**（实测 232~237 个 TU，`-j8` 约 1.5~1.9s；全量冷编译更久）：
   这是正确性要求下的必然代价（否则旧口径对象会混入链接）。若需部分增量，需给对象文件
   按口径分目录（`build/$(CONFIG)/$(PROTO)-$(DISP)/`），属后续可选优化。
5. **stamp 依赖「mtime 新于所有 `.o`」这一事实**：若有人手工 `touch -d` 把 `.o` 时间改成
   未来、或绕过 make 直接改 `build/Debug/*.o`，仍可能绕过重建。已通过「口径切换必改写
   stamp」保证常规路径正确；异常路径不在防护范围（可用 `make -B` 兜底）。
6. **并发两个 `make` 实例**共享 `build/$(CONFIG)/.build_stamp.tmp` 时无锁（`printf` 后
   `mv` 是原子改名，最坏是重复一次全量重建，不会产生错误的 elf）。EIDE 走自身构建系统、
   不读本 stamp，不构成竞争。

---

## 8. 复现与索引

- 关键命令：`make -j8` / `make -j8 PROTO=CQ` / `make -j8 DISP=22_1703` /
  `make -j8 DISP=22_1703 PROTO=CQ`；取值 `arm-none-eabi-size -A build/Debug/Project_STD.elf`。
- 本次验证脚本（等价逻辑可复制）：见 §3.1 的 9 条命令序列，或按 §3.1 脚本逐条执行。
- 原始日志（会话临时目录，易失）：`/tmp/stampfix/{R1..R16,final_V1..V9,emulate_prefix,step1b,hdrtest}.log|.summary`。
- 相关文档：`doc/构建开关总表.md` §4（机制权威）、§4.1（污染态归因）；
  `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` §7（内存账与更正）。
