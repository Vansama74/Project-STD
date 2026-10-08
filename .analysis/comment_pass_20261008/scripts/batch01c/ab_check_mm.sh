#!/usr/bin/env bash
# 批 1c（缅甸 MM 注释规范化）A/B 对照：改前（archive/batch01c，= git HEAD）vs 改后（工作区）
#
# 目的：证明「只改注释 ⇒ 非调试段逐字节一致 + 反汇编逐行一致」。
#   ① 改前/改后同 flags 编译（flags 逐字取自 `make -n` 的缅甸模块编译命令行；
#      唯一差异 = 模块头文件搜索路径指向「改前头文件影子目录」或「仓库现行目录」）；
#   ② 段级 md5（排除 .debug* / .comment / .ARM.attributes）；
#   ③ objdump -d 逐行对照；
#   ④ 附整对象 md5（含 .debug*）与 strip-debug md5，说明差异仅来自调试段。
#
# 覆盖 TU：Application/Src/ProtocolParser_Myanmar/{app_mm_proto.c, app_mm_proto_cmd.c}
#          + Application/Src/app_factory_test_mm.c（含 app_mm_proto.h 依赖）
#
# 用法：bash .analysis/comment_pass_20261008/scripts/batch01c/ab_check_mm.sh
set -u
cd "$(dirname "$0")/../../../.." || exit 1

BATCH=.analysis/comment_pass_20261008
ARCH="$BATCH/archive/batch01c"
AB="$BATCH/ab/batch01c"
PRE="$AB/pre"
CUR="$AB/cur"
PRE_INC="$AB/inc"
mkdir -p "$PRE" "$CUR" "$PRE_INC"

CC=arm-none-eabi-gcc
CFLAGS_COMMON="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
-DUSE_HAL_DRIVER -DSTM32F407xx -DSTD_ALL_PROTO \
-I Application/Inc -I Application/Inc/IAP -I Application/Inc/LDI -I Application/Inc/Config \
-I Application/Inc/ProtocolParser_QingHai -I Application/Inc/ProtocolParser_SiChuang_ETC \
-I Application/Inc/ProtocolParser_SiChuang_MTC -I Application/Inc/ProtocolParser_SiChuang_Overload \
-I Application/Inc/ProtocolParser_GuiZhou -I Application/Inc/ProtocolParser_GuiZhou_Overload \
-I Application/Inc/ProtocolParser_YunNan -I Application/Inc/ProtocolParser_YunNan_Overload \
-I Application/Inc/ProtocolParser_ShanDong -I Application/Inc/ProtocolParser_ChongQing \
-I Application/Inc/ProtocolParser_Anhui -I __MM_INC__ \
-I Application/Inc/AH_MQTT -I Application/Inc/RLS -I Application/Inc/Channel \
-I Device/Inc -I Platform/Inc -I Kernel/Inc -I Core/Inc \
-I Drivers/CMSIS/Include -I Drivers/CMSIS/Device/ST/STM32F4xx/Include \
-I Drivers/STM32F4xx_HAL_Driver/Inc \
-I Middlewares/Third_Party/SEGGER_RTT -I Middlewares/Third_Party/cJSON \
-I Middlewares/Third_Party/LwIP/src/include -I Middlewares/Third_Party/LwIP/system \
-I Middlewares/Third_Party/FreeRTOS/Source/include \
-I Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 \
-I Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I Compiler \
-std=gnu23 -Og -g -Wall -Wextra -ffunction-sections -fdata-sections -fno-common \
-fno-exceptions -fshort-enums -MMD -MP -c"

MM_SRCS="app_mm_proto.c app_mm_proto_cmd.c"
MM_SRC_DIR=Application/Src/ProtocolParser_Myanmar
APP_SRCS="app_factory_test_mm.c"
APP_SRC_DIR=Application/Src
INC_DIR=Application/Inc/ProtocolParser_Myanmar

fail=0

# ---- 0) 准备改前源码/头文件（取自归档；归档 = 基线提交时的 git HEAD 内容）----
for f in $MM_SRCS; do cp "$ARCH/$MM_SRC_DIR/$f" "$PRE/$f"; done
for f in $APP_SRCS; do cp "$ARCH/$APP_SRC_DIR/$f" "$PRE/$f"; done
cp "$ARCH/$INC_DIR/app_mm_proto.h" "$PRE_INC/app_mm_proto.h"
echo "== 改前源码（归档）md5 =="
( cd "$ARCH" && md5sum "$MM_SRC_DIR"/*.c "$APP_SRC_DIR/app_factory_test_mm.c" "$INC_DIR"/*.h )

# ---- 1) 同 flags 编译「改前」（影子头文件）----
echo; echo "== 编译改前（影子头文件）=="
pre_ok=1
for f in $MM_SRCS; do
  $CC ${CFLAGS_COMMON/__MM_INC__/$PRE_INC} -o "$PRE/${f%.c}.o" "$PRE/$f" 2> "$PRE/${f%.c}.build.log" || pre_ok=0
  warn=$(grep -c 'warning:' "$PRE/${f%.c}.build.log")
  echo "  $f: exit=$([ -s "$PRE/${f%.c}.o" ] && echo 0 || echo 1) warnings=$warn"
done
for f in $APP_SRCS; do
  $CC ${CFLAGS_COMMON/__MM_INC__/$PRE_INC} -o "$PRE/${f%.c}.o" "$PRE/$f" 2> "$PRE/${f%.c}.build.log" || pre_ok=0
  warn=$(grep -c 'warning:' "$PRE/${f%.c}.build.log")
  echo "  $f: exit=$([ -s "$PRE/${f%.c}.o" ] && echo 0 || echo 1) warnings=$warn"
done
[ $pre_ok -eq 1 ] || { echo "改前编译失败"; exit 1; }

# ---- 2) 编译「改后」（仓库现行源码 + 仓库现行头文件）----
echo; echo "== 编译改后（仓库现行）=="
cur_ok=1
for f in $MM_SRCS; do
  $CC ${CFLAGS_COMMON/__MM_INC__/$INC_DIR} -o "$CUR/${f%.c}.o" "$MM_SRC_DIR/$f" 2> "$CUR/${f%.c}.build.log" || cur_ok=0
  warn=$(grep -c 'warning:' "$CUR/${f%.c}.build.log")
  echo "  $f: exit=$([ -s "$CUR/${f%.c}.o" ] && echo 0 || echo 1) warnings=$warn"
done
for f in $APP_SRCS; do
  $CC ${CFLAGS_COMMON/__MM_INC__/$INC_DIR} -o "$CUR/${f%.c}.o" "$APP_SRC_DIR/$f" 2> "$CUR/${f%.c}.build.log" || cur_ok=0
  warn=$(grep -c 'warning:' "$CUR/${f%.c}.build.log")
  echo "  $f: exit=$([ -s "$CUR/${f%.c}.o" ] && echo 0 || echo 1) warnings=$warn"
done
[ $cur_ok -eq 1 ] || { echo "改后编译失败"; exit 1; }

# ---- 3) 对照 ----
sec_md5() { # 打印「段名 md5」列表（排除调试/属性段）
  local f="$1" tmp; tmp=$(mktemp -d)
  while IFS= read -r s; do
    case "$s" in .debug*|.comment|.ARM.attributes) continue ;; esac
    arm-none-eabi-objcopy -O binary --only-section="$s" "$f" "$tmp/x.bin" 2>/dev/null || continue
    printf '  %-28s %s\n' "$s" "$(md5sum "$tmp/x.bin" | cut -d' ' -f1)"
  done < <(arm-none-eabi-objdump -h "$f" | awk '{print $2}' | grep -E '^\.')
  rm -rf "$tmp"
}

ALL_SRCS="$MM_SRCS $APP_SRCS"
for f in $ALL_SRCS; do
  b="${f%.c}"
  echo; echo "######## $f"
  echo "  整对象 md5（含 .debug*）：pre=$(md5sum "$PRE/$b.o" | cut -c1-32) cur=$(md5sum "$CUR/$b.o" | cut -c1-32)"
  arm-none-eabi-objcopy --strip-debug "$PRE/$b.o" "$PRE/$b.strip.o"
  arm-none-eabi-objcopy --strip-debug "$CUR/$b.o" "$CUR/$b.strip.o"
  echo "  strip-debug md5     ：pre=$(md5sum "$PRE/$b.strip.o" | cut -c1-32) cur=$(md5sum "$CUR/$b.strip.o" | cut -c1-32)"
  if diff <(sec_md5 "$PRE/$b.o") <(sec_md5 "$CUR/$b.o") > /dev/null; then
    echo "  ✔ 段级逐字节一致（.debug*/.comment/.ARM.attributes 除外；对照段数 = $(sec_md5 "$CUR/$b.o" | wc -l)）"
  else
    echo "  ✘ 段级差异："; diff <(sec_md5 "$PRE/$b.o") <(sec_md5 "$CUR/$b.o") | sed 's/^/    /'; fail=1
  fi
  arm-none-eabi-objdump -d "$PRE/$b.o" | grep -v 'file format' > "$PRE/$b.dis"
  arm-none-eabi-objdump -d "$CUR/$b.o" | grep -v 'file format' > "$CUR/$b.dis"
  if diff -q "$PRE/$b.dis" "$CUR/$b.dis" > /dev/null; then
    echo "  ✔ 反汇编逐行一致（$(wc -l < "$CUR/$b.dis") 行）"
  else
    echo "  ✘ 反汇编差异："; diff "$PRE/$b.dis" "$CUR/$b.dis" | sed 's/^/    /' | head -20; fail=1
  fi
  # 行号敏感串：非调试段不得含源码路径串（= 该 TU 无 __FILE__/断言路径消费者）
  leak_pre=$(strings "$PRE/$b.strip.o" | grep -c "$MM_SRC_DIR\|$APP_SRC_DIR")
  leak_cur=$(strings "$CUR/$b.strip.o" | grep -c "$MM_SRC_DIR\|$APP_SRC_DIR")
  if [ "$leak_pre" -eq 0 ] && [ "$leak_cur" -eq 0 ]; then
    echo "  ✔ 非调试段无源码路径串（无 __FILE__/assert_param 消费者，注释行号变动安全）"
  else
    echo "  ⚠ 非调试段含源码路径串：pre=$leak_pre cur=$leak_cur（需改判据为段级对照）"; fail=1
  fi
done

echo; echo "######## 结果：$([ $fail -eq 0 ] && echo '批 1c 全部一致（注释规范化零机器码影响）' || echo '存在差异，需人工复核')"
exit $fail
