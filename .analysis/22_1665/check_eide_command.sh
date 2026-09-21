#!/usr/bin/env bash
# 22-1665 **EIDE 原命令**复现（照抄 EIDE 构建日志里的驱动编译命令行，只把 -o / 源码换到临时目录）
#
# 为什么单独一条：`check_compile_matrix.sh` 用精简命令求快（-fsyntax-only、少 -I）；
# 本脚本复现**现场真正用的那条命令**（EIDE 3.27.2 → unify_builder → arm-none-eabi-gcc 15.2.1，
# -std=gnu23 -O3 -Wall -g 全量 -I/-D），用于回答「EIDE 里改宏会不会又报错」。
#
# 命令来源：EIDE 构建日志（2026-09-17 17:07:52 那次失败构建，用户终端日志
# `/home/yystation/.cursor/projects/home-yystation-Program-3833024-Project-STD-main/terminals/6.txt`），
# 其中失败原文 = 静态断言 `链表行数须 = 4 × MODULE_ROWS × MODULE_COLS`（当时 MODULE_ROWS 被改成 2）。
#
# **第三十轮**：四个几何宏 = 兄弟驱动同款**普通 `#define`** ⇒ 几何口径一律用「宏名=值」走
# **源码替换**（`macro_override.py` 生成临时副本），`-D` 只对仍带 `#ifndef` 守卫的
# `_22_1665_CHAIN_HEAD_IS_MODULE0` 有效；本节末尾另有 **-O3 零回归 A/B**
# （改动前基准 `.analysis/22_1665/archive/round30/dev_display_22_1665_pre_round30.c` vs 现行）。
#
# **第三十四轮**：17 条 `_Static_assert` 全部按用户裁决删除（含「≤ 整片 CCMRAM 区域 64KB」
# 物理守卫）⇒ 原「非法口径须编译失败 + 可读断言消息」的用例全部改写为**实测的新期望**：
#   · `warn_case`（编过但必须出现指定告警）：`MODULE_ROWS=0` → `-Wdiv-by-zero`（落点表除以
#     0 模块/行）；`MODULE_COLS=6` → `-O3` 下 `-Waggressive-loop-optimizations`（UB：越界读
#     `_22_1665_lines[5]` —— 第 5 行不存在，表只预声明 `HUB75_CHANNEL_MAX/2 = 5` 行）；
#   · `ok_case`（**零告警编过 = 静默错口径**）：`MODULE_COLS=0`（-O3 -Wall 下静默；
#     `-Wextra` 才会报恒假比较）、单模块 24×16 / 16×32 / 30、`MODULE_ROWS=47`（CCM 66432B，
#     越区域只能由**链接期**兜底 —— 见 `check_compile_matrix.sh` C 节）。
#
# 用法：bash .analysis/22_1665/check_eide_command.sh
# 退出码 = 失败项数（0 = 全绿）。
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SRC_REL="./Device/Display/dev_display_22_1665.c"
PRE_SRC_REL=".analysis/22_1665/archive/round30/dev_display_22_1665_pre_round30.c"
GCC="${GCC:-/home/yystation/EnvTools/embedded-toolchain/arm-gnu-toolchain/bin/arm-none-eabi-gcc}"
[ -x "$GCC" ] || GCC=arm-none-eabi-gcc

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ---- EIDE 原命令（逐字来自构建日志；-o 与源码路径由调用方给）----
eide_cc() {  # eide_cc <输出.o> <源码路径> [-D...]
    local out="$1" src="$2"; shift 2
    ( cd "$ROOT" && "$GCC" -c -xc -std=gnu23 \
        -ICore/Inc -IDrivers/CMSIS/Include -IDrivers/CMSIS/Device/ST/STM32F4xx/Include \
        -IDrivers/STM32F4xx_HAL_Driver/Inc -IMiddlewares/Third_Party/LwIP/src/include \
        -IMiddlewares/Third_Party/LwIP/system -IMiddlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 \
        -IMiddlewares/Third_Party/FreeRTOS/Source/include \
        -IMiddlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -ICompiler -IApplication/Inc \
        -IDevice/Inc -IKernel/Inc -IPlatform/Inc -IMiddlewares/Third_Party/SEGGER_RTT \
        -IApplication/Inc/Channel -IApplication/Inc/IAP -IApplication/Inc/LDI -IApplication/Inc/Config \
        -IApplication/Inc/AH_MQTT -IApplication/Inc/RLS -IApplication/Inc/ProtocolParser_QingHai \
        -IApplication/Inc/ProtocolParser_SiChuang_ETC -IApplication/Inc/ProtocolParser_SiChuang_MTC \
        -IApplication/Inc/ProtocolParser_SiChuang_Overload -IApplication/Inc/ProtocolParser_ShanDong \
        -IApplication/Inc/ProtocolParser_GuiZhou -IApplication/Inc/ProtocolParser_GuiZhou_Overload \
        -IApplication/Inc/ProtocolParser_YunNan -IApplication/Inc/ProtocolParser_YunNan_Overload \
        -IApplication/Inc/ProtocolParser_ChongQing -IApplication/Inc/ProtocolParser_Anhui \
        -IMiddlewares/Third_Party/cJSON \
        -D"USE_HAL_DRIVER" -D"STM32F407xx" -D"PROTO_CHONGQING" \
        -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -O3 -Wall -g \
        -ffunction-sections -fdata-sections -funsigned-char --specs=nano.specs --specs=nosys.specs \
        -fcallgraph-info -fstack-usage -o "$out" -MMD "$src" "$@" )
}

# src_for_of <源路径> <tag> [宏名=值 ...]：几何覆盖 → 源码替换后的临时副本路径
src_for_of() {
    local src="$1" tag="$2"; shift 2
    if [ "$#" -eq 0 ]; then printf '%s\n' "$src"; return 0; fi
    python3 "$HERE/macro_override.py" "$src" "$TMP/${tag}.c" "$@" || exit 3
    printf '%s\n' "$TMP/${tag}.c"
}
src_for() { local tag="$1"; shift; src_for_of "$SRC_REL" "$tag" "$@"; }

pass=0; fail=0; n=0

ok_case() {  # ok_case <描述> <源码> [-D...]
    local desc="$1" src="$2"; shift 2
    local out rc
    n=$((n + 1))
    out=$(eide_cc "$TMP/ok_$n.o" "$src" "$@" 2>&1); rc=$?
    if [[ $rc -ne 0 || -n "$out" ]]; then
        echo "  [失败] $desc（exit=$rc；须零报错零告警）"; echo "$out" | head -6; fail=$((fail+1))
    else
        echo "  [通过] $desc"; pass=$((pass+1))
    fi
}

warn_case() {  # warn_case <描述> <期望命中的告警片段> <源码> [-D...]
    local desc="$1" needle="$2" src="$3"; shift 3
    local out rc
    n=$((n + 1))
    out=$(eide_cc "$TMP/warn_$n.o" "$src" "$@" 2>&1); rc=$?
    if [[ $rc -ne 0 ]]; then
        echo "  [失败] $desc（期望编过 + 告警，实际编译失败）"; echo "$out" | head -6; fail=$((fail+1))
    elif ! grep -qF "$needle" <<<"$out"; then
        echo "  [失败] $desc（编过但未见告警 '$needle'）"; echo "$out" | head -6; fail=$((fail+1))
    else
        echo "  [通过] $desc"; pass=$((pass+1))
    fi
}

bad_case() {  # bad_case <描述> <期望命中的断言消息片段> <源码> [-D...]（第三十四轮起仅留作历史入口）
    local desc="$1" needle="$2" src="$3"; shift 3
    local out rc
    n=$((n + 1))
    out=$(eide_cc "$TMP/bad_$n.o" "$src" "$@" 2>&1); rc=$?
    if [[ $rc -eq 0 ]]; then
        echo "  [失败] $desc（期望编译失败，实际编过）"; fail=$((fail+1))
    elif ! grep -qF "$needle" <<<"$out"; then
        echo "  [失败] $desc（编译失败但未见可读断言消息 '$needle'）"; echo "$out" | head -6; fail=$((fail+1))
    else
        echo "  [通过] $desc"; pass=$((pass+1))
    fi
}

echo "== EIDE 原命令复现（$(basename "$GCC") @ $($GCC -dumpversion 2>/dev/null)）=="
echo "  命令来源：EIDE 构建日志（terminals/6.txt，2026-09-17 17:07:52）"
echo "  第二十九轮：接线/蓝分量/极性三开关已删除（HUB_WIRING / BLUE_AS_LIT / DATA_ACTIVE_HIGH）——"
echo "  对应口径用例一并退休（历史口径锚定 archive/finalize 归档驱动）"
echo "  第三十轮：几何宏改普通 #define → 几何用例改「宏名=值」源码替换"
echo "  第三十四轮：编译期防御全部删除 → 原「非法口径」用例改写为实测期望（warn / 静默编过）"
echo "== 合法口径（须零报错零告警编过）=="
ok_case "工作区当前宏值 8×4 = 128×64（本模组 46080B；每列一口独立数据线）" \
        "$(src_for cur)"
ok_case "改宏 MODULE_ROWS=2（**用户那次报错的宏值**）→ 32×16" \
        "$(src_for r2 _22_1665_MODULE_ROWS=2)"
ok_case "改宏 MODULE_COLS=2 → 16×32" "$(src_for c2 _22_1665_MODULE_COLS=2)"
ok_case "改宏 2×2 → 32×32" \
        "$(src_for r2c2 _22_1665_MODULE_ROWS=2 _22_1665_MODULE_COLS=2)"
ok_case "改宏 3×2 → 48×32" \
        "$(src_for r3c2 _22_1665_MODULE_ROWS=3 _22_1665_MODULE_COLS=2)"
ok_case "通道上限 1×5（2×5 = 10 通道）→ 16×80" \
        "$(src_for r1c5 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=5)"
ok_case "现场几何 8×2 → 128×32（CCM 23040B）" \
        "$(src_for r8c2 _22_1665_MODULE_ROWS=8 _22_1665_MODULE_COLS=2)"
ok_case "旧界外 12×1 → 192×16（CCM 17152B，旧 16KB 预算下曾失败）" \
        "$(src_for r12c1 _22_1665_MODULE_ROWS=12 _22_1665_MODULE_COLS=1)"
ok_case "链首口径开关 -D_22_1665_CHAIN_HEAD_IS_MODULE0=1（多模块现场 A/B；仍带 #ifndef）" \
        "$(src_for cur)" -D_22_1665_CHAIN_HEAD_IS_MODULE0=1
ok_case "现场 A/B 组合 CHAIN_HEAD_IS_MODULE0=1 + 1×2 → 16×32" \
        "$(src_for c2 _22_1665_MODULE_COLS=2)" -D_22_1665_CHAIN_HEAD_IS_MODULE0=1

echo "== 旧非法口径的现行行为（无编译期防御：编过但有告警 / 静默编过；第三十四轮实测）=="
warn_case "MODULE_ROWS=0 → 编过，但落点表除以 0 模块/行（-Wdiv-by-zero）" \
          "division by zero" "$(src_for rows0 _22_1665_MODULE_ROWS=0)"
warn_case "MODULE_COLS=6 → 编过，但 -O3 判定「越界读 _22_1665_lines」为 UB（-Waggressive-loop-optimizations）" \
          "undefined behavior" "$(src_for cols6 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=6)"
ok_case "MODULE_COLS=0 → -O3 -Wall 下**零告警**编过（静默；仅 -Wextra 会报恒假比较）" \
        "$(src_for cols0 _22_1665_MODULE_COLS=0)"
ok_case "单模块 24×16 → 零告警编过（块栅格 12 ≠ 8：静默错口径）" \
        "$(src_for pix24 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=1 _22_1665_MODULE_PIXEL_ROW=24)"
ok_case "单模块 16×32 → 零告警编过（静默错口径）" \
        "$(src_for pix1632 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=1 _22_1665_MODULE_PIXEL_COL=32)"
ok_case "MODULE_PIXEL_COL=30 → 零告警编过（半屏高 15 非 4 的倍数：静默错口径）" \
        "$(src_for pix30 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=1 _22_1665_MODULE_PIXEL_COL=30)"
ok_case "MODULE_ROWS=47（CCM 66432B）→ 对象零告警编过；越区域由**链接期**兜底（check_compile_matrix.sh C 节）" \
        "$(src_for r47 _22_1665_MODULE_ROWS=47 _22_1665_MODULE_COLS=1)"

echo "== 零回归 A/B（第三十轮：文件头瘦身 + 几何宏改普通 #define；同一 EIDE -O3 命令）=="
echo "  改动前基准 = $PRE_SRC_REL"
ab_case() {  # ab_case <tag> [宏名=值 ...]
    local tag="$1"; shift
    local cur pre secs a b ok=1 s
    cur=$(src_for "abcur_$tag" "$@")
    pre=$(src_for_of "$PRE_SRC_REL" "abpre_$tag" "$@")
    eide_cc "$TMP/abcur_$tag.o" "$cur" >/dev/null 2>&1
    eide_cc "$TMP/abpre_$tag.o" "$pre" >/dev/null 2>&1
    if [ ! -f "$TMP/abcur_$tag.o" ] || [ ! -f "$TMP/abpre_$tag.o" ]; then
        echo "  [失败] A/B [$tag]：两侧未编出对象"; fail=$((fail+1)); return
    fi
    # ① 反汇编逐条一致（含符号名与地址；objdump 首部文件名与 .debug_* 行号信息不计）
    arm-none-eabi-objdump -d "$TMP/abcur_$tag.o" | grep -v 'file format' > "$TMP/abcur_$tag.s"
    arm-none-eabi-objdump -d "$TMP/abpre_$tag.o" | grep -v 'file format' > "$TMP/abpre_$tag.s"
    cmp -s "$TMP/abcur_$tag.s" "$TMP/abpre_$tag.s" || ok=0
    # ② 代码/数据段逐段逐字节一致
    secs=$(arm-none-eabi-objdump -h "$TMP/abcur_$tag.o" | awk '$2 ~ /^\.(text|rodata|data|bss|ccmram|hw_initcall)/ {print $2}')
    for s in $secs; do
        a=$(arm-none-eabi-objcopy -O binary --only-section="$s" "$TMP/abcur_$tag.o" /dev/stdout | md5sum)
        b=$(arm-none-eabi-objcopy -O binary --only-section="$s" "$TMP/abpre_$tag.o" /dev/stdout | md5sum)
        [ "$a" = "$b" ] || { ok=0; echo "        ✘ 段 $s 不一致"; }
    done
    local ccm; ccm=$(arm-none-eabi-size -A "$TMP/abcur_$tag.o" | awk '$1==".ccmram"{print $2}')
    if [ "$ok" -eq 1 ]; then
        echo "  [通过] A/B [$tag]：$(wc -w <<<"$secs") 个段逐字节一致 + 反汇编逐条一致（.ccmram ${ccm}B）"
        pass=$((pass+1))
    else
        echo "  [失败] A/B [$tag]：改动前后目标码有差异（.ccmram ${ccm}B）"; fail=$((fail+1))
    fi
}
ab_case default _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=5
ab_case 1x1 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=1
ab_case 1x2 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=2

echo
echo "结果：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
