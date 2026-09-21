#!/usr/bin/env bash
# 第三十二轮「零回归」机器级 A/B（宿主，不需要构建产物）
#
# 第三十二轮只做一件事：删除驱动**自设的 16KB CCM 预算断言**，改为「本模组数组 ≤ 整片
# CCMRAM 区域 64KB」的物理守卫（另加注释与文件头一句说明）。三者都不应改变产物 ——
# 本脚本把**改动前源码**（归档冻结件 `archive/round32/dev_display_22_1665_pre_round32.c`，
# md5 `efc96ba3208d008952d1946806880c47` = 第三十一轮报告记录的那份 8×2 现场文件）
# 与**现行源码**用**同一条编译命令行**各编一份驱动对象，然后：
#   ① 逐个 section 比 md5（排除 .debug* / 符号表 —— 两者的「源文件名」不同，
#      STT_FILE 记的是传给 gcc 的路径，属工具链记账、非代码差异）；
#   ② `objdump -d` 反汇编逐行比对（期望 0 行差异）。
#
# **几何口径钉死（两侧都用 macro_override.py 源码替换成 1×5 = CCM 8320B）**：
# 归档件仍带旧 16KB 断言，在 8×2（23040B）下**它自己编不过**，故 A/B 只能在 ≤16KB
# 的口径下做（1×5 同时是第三十轮记录口径）；8×2 的可编译性由
# `check_compile_matrix.sh` / `check_eide_command.sh` 与真实 `make DISP=22_1665` 证明。
#
# 编译命令行取「最新一份 build_round*.log」里驱动那份的实参（与固件构建逐字相同）；
# 没有日志时退回内置等价参数（`-Og -g -std=gnu23` + 全量 -I，与 Makefile 同口径）。
#
# 用法：bash .analysis/22_1665/check_round32_ab.sh
# 退出码：0 = 两项全过；非 0 = 失败项数。
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
CUR="$ROOT/Device/Display/dev_display_22_1665.c"
OLD="$HERE/archive/round32/dev_display_22_1665_pre_round32.c"
GCC=arm-none-eabi-gcc

pass=0; fail=0

if [ ! -f "$OLD" ]; then
    echo "结果：通过 0 / 失败 1"
    echo "  [失败] 缺改动前基准 $OLD（第三十二轮归档冻结件）"
    exit 1
fi

# ---- 编译命令行（取最新一份**含驱动编译命令**的日志；最近一次 make 不是 22_1665 口径也能工作）----
# shellcheck source=/dev/null
. "$HERE/pick_log.sh"
LOG="$(pick_log_1665)" || LOG=""
SRC_REL="Device/Display/dev_display_22_1665.c"
CC_LINE=""
[ -n "${LOG:-}" ] && [ -f "$LOG" ] && \
    CC_LINE="$(grep -m1 -- "-c -o build/Debug/Device/Display/dev_display_22_1665.o" "$LOG")"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/new" "$TMP/old"
# 几何钉死 1×5：归档件带旧 16KB 断言，只能在 ≤16KB 口径下编译（见文件头说明）
PIN=("_22_1665_MODULE_ROWS=1" "_22_1665_MODULE_COLS=5")
python3 "$HERE/macro_override.py" "$CUR" "$TMP/new/dev_display_22_1665.c" "${PIN[@]}" || exit 3
python3 "$HERE/macro_override.py" "$OLD" "$TMP/old/dev_display_22_1665.c" "${PIN[@]}" || exit 3
echo "几何钉死：${PIN[*]}（两侧源码替换，不随工作区宏值漂移）"

cd "$ROOT"
if [ -n "$CC_LINE" ]; then
    echo "编译命令行：取自 $LOG（与固件构建逐字相同）"
    NEW_CC="$(printf '%s\n' "$CC_LINE" | sed -e "s#-o build/Debug/Device/Display/dev_display_22_1665.o#-o $TMP/new.o#" \
                                                   -e "s#$SRC_REL#$TMP/new/dev_display_22_1665.c#" -e 's/^arm-none-eabi-gcc //')"
    OLD_CC="$(printf '%s\n' "$CC_LINE" | sed -e "s#-o build/Debug/Device/Display/dev_display_22_1665.o#-o $TMP/old.o#" \
                                                   -e "s#$SRC_REL#$TMP/old/dev_display_22_1665.c#" -e 's/^arm-none-eabi-gcc //')"
else
    echo "编译命令行：无 build_round*.log → 退回内置等价参数"
    INC=(-I "$ROOT/Application/Inc" -I "$ROOT/Device/Inc" -I "$ROOT/Platform/Inc" -I "$ROOT/Kernel/Inc"
         -I "$ROOT/Core/Inc" -I "$ROOT/Drivers/CMSIS/Include"
         -I "$ROOT/Drivers/CMSIS/Device/ST/STM32F4xx/Include"
         -I "$ROOT/Drivers/STM32F4xx_HAL_Driver/Inc" -I "$ROOT/Middlewares/Third_Party/SEGGER_RTT")
    BASE=(-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DUSE_HAL_DRIVER -DSTM32F407xx
          -DSTD_ALL_PROTO -std=gnu23 -Og -g -c)
    NEW_CC="${BASE[*]} ${INC[*]} -o $TMP/new.o $TMP/new/dev_display_22_1665.c"
    OLD_CC="${BASE[*]} ${INC[*]} -o $TMP/old.o $TMP/old/dev_display_22_1665.c"
fi

# shellcheck disable=SC2086
$GCC $NEW_CC || { echo "结果：通过 0 / 失败 1"; echo "  [失败] 现行源码编译失败"; exit 1; }
# shellcheck disable=SC2086
$GCC $OLD_CC || { echo "结果：通过 0 / 失败 1"; echo "  [失败] 改动前基准编译失败"; exit 1; }

# ---- ① section md5 ----
SECTIONS="$(arm-none-eabi-objdump -h "$TMP/new.o" | awk 'NF>=7 && $2 ~ /^\./ {print $2}' \
            | grep -vE '^\.(debug|symtab|strtab|shstrtab)' || true)"
sec_bad=0
sec_list=()
for sec in $SECTIONS; do
    arm-none-eabi-objcopy -O binary --only-section="$sec" "$TMP/new.o" "$TMP/new.bin" 2>/dev/null
    arm-none-eabi-objcopy -O binary --only-section="$sec" "$TMP/old.o" "$TMP/old.bin" 2>/dev/null
    if ! cmp -s "$TMP/new.bin" "$TMP/old.bin"; then
        sec_bad=$((sec_bad + 1))
        echo "  [差异] section $sec"
    fi
    sec_list+=("$sec")
done
if [ "$sec_bad" -eq 0 ]; then
    echo "  [通过] ${#sec_list[@]} 个 section 逐字节一致（不含 .debug*/符号表）"
    pass=$((pass + 1))
else
    echo "  [失败] $sec_bad 个 section 不一致"
    fail=$((fail + 1))
fi

# ---- ② 反汇编逐行 ----
arm-none-eabi-objdump -d "$TMP/new.o" | grep -v 'file format' >"$TMP/new.asm"
arm-none-eabi-objdump -d "$TMP/old.o" | grep -v 'file format' >"$TMP/old.asm"
if diff -q "$TMP/new.asm" "$TMP/old.asm" >/dev/null; then
    echo "  [通过] 反汇编逐行一致（$(wc -l <"$TMP/new.asm") 行，0 行差异）"
    pass=$((pass + 1))
else
    echo "  [失败] 反汇编有差异："
    diff "$TMP/new.asm" "$TMP/old.asm" | head -20
    fail=$((fail + 1))
fi

echo
echo "结果：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
