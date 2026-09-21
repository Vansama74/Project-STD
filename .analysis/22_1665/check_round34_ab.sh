#!/usr/bin/env bash
# 第三十四轮「零回归」机器级 A/B（宿主，不需要构建产物）
#
# 第三十四轮只做两件事（用户裁决）：① 删除全部编译期防御（17 条 `_Static_assert` +
# 仅断言使用的派生宏 `_22_1665_CCM_BYTES` / `_22_1665_BSRR_SLOTS`）；② 精简注释
# （去掉与 `doc/01` 重复的历史叙述 / 轮次记录 / 长篇推导）。
# 两者都不应改变产物 —— 本脚本把**改动前源码**（归档冻结件
# `archive/round34/dev_display_22_1665_pre_round34.c`，md5 `af7fe2b44be42b96a14e43c96e0c6e10`、
# 441 行、17 条断言）与**现行源码**用**同一条编译命令行**各编一份驱动对象，然后：
#   ① 逐个 section 比 md5（排除 .debug* / 符号表 / .ARM.attributes —— 注释与断言删除只改
#      「行号表」这类工具链记账，属非代码差异）；
#   ② `objdump -d` 反汇编逐行比对（期望 0 行差异）。
# 另加两条源码级自证：现行 0 条 `_Static_assert`、归档恰 17 条（确保比的是该比的两份）。
#
# 编译命令行取「最新一份 build_round*.log」里驱动那份的实参（与固件构建逐字相同）；
# 没有日志时退回内置等价参数（`-Og -g -std=gnu23` + 全量 -I，与 Makefile 同口径）。
#
# **几何口径钉死**：两侧源码都用 `macro_override.py` 源码替换，各跑两个口径 ——
# **1×5**（CCM 8320B，第三十/三十二轮记录口径）与 **8×4 = 128×64**（工作区当前宏值，
# 改动前 2026-09-18 起；两侧在该几何下都能编过：本模组 46080B ≤ 64KB 区域）。
# 钉死后本脚本不随「工作区当前宏值是用户在用的旋钮」漂移（对齐 README「口径解耦」纪律）。
#
# 用法：bash .analysis/22_1665/check_round34_ab.sh
# 退出码：0 = 全过；非 0 = 失败项数。
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
CUR="$ROOT/Device/Display/dev_display_22_1665.c"
OLD="$HERE/archive/round34/dev_display_22_1665_pre_round34.c"
GCC=arm-none-eabi-gcc

pass=0; fail=0

if [ ! -f "$OLD" ]; then
    echo "结果：通过 0 / 失败 1"
    echo "  [失败] 缺改动前基准 $OLD（第三十四轮归档冻结件）"
    exit 1
fi

# ---- 源码级自证：断言确实已清零 / 归档确实带 17 条 ----
# 只数**语句**（行首的 `_Static_assert(`）：归档里另有一处注释提及该词，不计数。
n_cur=$(grep -cE '^[[:space:]]*_Static_assert\(' "$CUR" || true)
n_old=$(grep -cE '^[[:space:]]*_Static_assert\(' "$OLD" || true)
if [ "$n_cur" -eq 0 ] && [ "$n_old" -eq 17 ]; then
    echo "  [通过] 断言条数：现行 $n_cur 条 / 归档 $n_old 条（第三十四轮删除的正是这 17 条）"
    pass=$((pass + 1))
else
    echo "  [失败] 断言条数异常：现行 $n_cur 条（期望 0）/ 归档 $n_old 条（期望 17）"
    fail=$((fail + 1))
fi
for gone in _22_1665_CCM_BYTES _22_1665_BSRR_SLOTS; do
    if ! grep -q "$gone" "$CUR"; then
        echo "  [通过] 仅断言使用的派生宏已删除：$gone 零残留"
        pass=$((pass + 1))
    else
        echo "  [失败] 仅断言使用的派生宏仍残留：$gone"
        fail=$((fail + 1))
    fi
done

# ---- 编译命令行 ----
# 取最新一份**含驱动编译命令**的日志（最近一次 make 不是 22_1665 口径也能工作）
# shellcheck source=/dev/null
. "$HERE/pick_log.sh"
LOG="$(pick_log_1665)" || LOG=""
SRC_REL="Device/Display/dev_display_22_1665.c"
CC_LINE=""
[ -n "${LOG:-}" ] && [ -f "$LOG" ] && \
    CC_LINE="$(grep -m1 -- "-c -o build/Debug/Device/Display/dev_display_22_1665.o" "$LOG")"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cd "$ROOT"

# ab_case <tag> <几何宏覆盖...>
ab_case() {
    local tag="$1"; shift
    local cur pre ok=1 s
    mkdir -p "$TMP/$tag"
    python3 "$HERE/macro_override.py" "$CUR" "$TMP/$tag/new.c" "$@" || { fail=$((fail + 1)); return; }
    python3 "$HERE/macro_override.py" "$OLD" "$TMP/$tag/old.c" "$@" || { fail=$((fail + 1)); return; }

    local N_CC O_CC
    if [ -n "$CC_LINE" ]; then
        N_CC="$(printf '%s\n' "$CC_LINE" | sed -e "s#-o build/Debug/Device/Display/dev_display_22_1665.o#-o $TMP/$tag/new.o#" \
                                                   -e "s#$SRC_REL#$TMP/$tag/new.c#" -e 's/^arm-none-eabi-gcc //')"
        O_CC="$(printf '%s\n' "$CC_LINE" | sed -e "s#-o build/Debug/Device/Display/dev_display_22_1665.o#-o $TMP/$tag/old.o#" \
                                                   -e "s#$SRC_REL#$TMP/$tag/old.c#" -e 's/^arm-none-eabi-gcc //')"
    else
        local INC=(-I "$ROOT/Application/Inc" -I "$ROOT/Device/Inc" -I "$ROOT/Platform/Inc" -I "$ROOT/Kernel/Inc"
                   -I "$ROOT/Core/Inc" -I "$ROOT/Drivers/CMSIS/Include"
                   -I "$ROOT/Drivers/CMSIS/Device/ST/STM32F4xx/Include"
                   -I "$ROOT/Drivers/STM32F4xx_HAL_Driver/Inc" -I "$ROOT/Middlewares/Third_Party/SEGGER_RTT")
        local BASE=(-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DUSE_HAL_DRIVER -DSTM32F407xx
                    -DSTD_ALL_PROTO -std=gnu23 -Og -g -c)
        N_CC="${BASE[*]} ${INC[*]} -o $TMP/$tag/new.o $TMP/$tag/new.c"
        O_CC="${BASE[*]} ${INC[*]} -o $TMP/$tag/old.o $TMP/$tag/old.c"
    fi

    # shellcheck disable=SC2086
    $GCC $N_CC || { echo "  [失败] A/B [$tag]：现行源码编译失败"; fail=$((fail + 1)); return; }
    # shellcheck disable=SC2086
    $GCC $O_CC || { echo "  [失败] A/B [$tag]：改动前基准编译失败"; fail=$((fail + 1)); return; }

    # ① section 逐字节
    local secs
    secs="$(arm-none-eabi-objdump -h "$TMP/$tag/new.o" | awk 'NF>=7 && $2 ~ /^\./ {print $2}' \
            | grep -vE '^\.(debug|symtab|strtab|shstrtab|ARM\.attributes|comment)' || true)"
    for s in $secs; do
        arm-none-eabi-objcopy -O binary --only-section="$s" "$TMP/$tag/new.o" "$TMP/$tag/new.bin" 2>/dev/null
        arm-none-eabi-objcopy -O binary --only-section="$s" "$TMP/$tag/old.o" "$TMP/$tag/old.bin" 2>/dev/null
        cmp -s "$TMP/$tag/new.bin" "$TMP/$tag/old.bin" || { ok=0; echo "        ✘ 段 $s 不一致"; }
    done
    # ② 反汇编逐行
    arm-none-eabi-objdump -d "$TMP/$tag/new.o" | grep -v 'file format' >"$TMP/$tag/new.asm"
    arm-none-eabi-objdump -d "$TMP/$tag/old.o" | grep -v 'file format' >"$TMP/$tag/old.asm"
    diff -q "$TMP/$tag/new.asm" "$TMP/$tag/old.asm" >/dev/null || { ok=0; echo "        ✘ 反汇编有差异"; }

    local ccm asm_lines
    ccm="$(arm-none-eabi-size -A "$TMP/$tag/new.o" | awk '$1==".ccmram"{print $2}')"
    asm_lines="$(wc -l <"$TMP/$tag/new.asm")"
    if [ "$ok" -eq 1 ]; then
        echo "  [通过] A/B [$tag]：$(wc -w <<<"$secs") 个段逐字节一致 + 反汇编 $asm_lines 行 0 差异（.ccmram ${ccm}B）"
        pass=$((pass + 1))
    else
        echo "  [失败] A/B [$tag]：断言/注释删除前后目标码有差异（.ccmram ${ccm}B）"
        fail=$((fail + 1))
    fi
}

if [ -n "$CC_LINE" ]; then
    echo "编译命令行：取自 $LOG（与固件构建逐字相同）"
else
    echo "编译命令行：无 build_round*.log → 退回内置等价参数"
fi
echo "几何口径：两侧源码替换，各跑 1×5（记录口径）+ 8×4（工作区当前宏值 128×64）"
echo "== A/B（改动前归档 vs 现行；断言/注释均不产生代码，期望 0 差异）=="
ab_case r1c5 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=5
ab_case r8c4 _22_1665_MODULE_ROWS=8 _22_1665_MODULE_COLS=4

echo
echo "结果：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
