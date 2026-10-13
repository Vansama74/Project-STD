#!/usr/bin/env bash
# 契约行收敛轮（refine_round2）A/B 对照：改前（archive/refine_round2 = 提交 c61c895 = git HEAD）
# vs 改后（工作区）。
#
# 目的：证明「只改注释 ⇒ 非调试段逐字节一致 + 反汇编逐行一致」。
#   ① 改前/改后同 flags 编译（flags 逐字取自 `make -n` 的对应编译命令行；唯一差异 = 该模块
#      头文件搜索路径指向「改前头文件影子目录」或「仓库现行目录」）；
#   ② 段级 md5（排除 .debug* / .comment / .ARM.attributes）；
#   ③ objdump -d 逐行对照；
#   ④ 附整对象 md5（含 .debug*）与 strip-debug md5，说明差异仅来自调试段。
#
# 覆盖 TU（10 个）：YN_OL 3 + GZ_OL 4 + MM 2 + app_factory_test_mm.c 1
# 用法：bash .analysis/comment_pass_20261008/scripts/refine_round2/ab_check_refine.sh
set -u
cd "$(dirname "$0")/../../../.." || exit 1

BATCH=.analysis/comment_pass_20261008
ARCH="$BATCH/archive/refine_round2"
AB="$BATCH/ab/refine_round2"
PRE="$AB/pre"
CUR="$AB/cur"
SH_YN="$AB/inc/yn_ol"
SH_GZ="$AB/inc/gz_ol"
SH_MM="$AB/inc/mm"
mkdir -p "$PRE" "$CUR" "$SH_YN" "$SH_GZ" "$SH_MM"

INC_YN=Application/Inc/ProtocolParser_YunNan_Overload
INC_GZ=Application/Inc/ProtocolParser_GuiZhou_Overload
INC_MM=Application/Inc/ProtocolParser_Myanmar
SRC_YN=Application/Src/ProtocolParser_YunNan_Overload
SRC_GZ=Application/Src/ProtocolParser_GuiZhou_Overload
SRC_MM=Application/Src/ProtocolParser_Myanmar

CC=arm-none-eabi-gcc
CFLAGS_COMMON="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
-DUSE_HAL_DRIVER -DSTM32F407xx -DSTD_ALL_PROTO \
-I Application/Inc -I Application/Inc/IAP -I Application/Inc/LDI -I Application/Inc/Config \
-I Application/Inc/ProtocolParser_QingHai -I Application/Inc/ProtocolParser_SiChuang_ETC \
-I Application/Inc/ProtocolParser_SiChuang_MTC -I Application/Inc/ProtocolParser_SiChuang_Overload \
-I Application/Inc/ProtocolParser_GuiZhou -I $INC_GZ \
-I Application/Inc/ProtocolParser_YunNan -I $INC_YN \
-I Application/Inc/ProtocolParser_ShanDong -I Application/Inc/ProtocolParser_ChongQing \
-I Application/Inc/ProtocolParser_Anhui -I $INC_MM \
-I Application/Inc/AH_MQTT -I Application/Inc/RLS -I Application/Inc/Channel \
-I Device/Inc -I Platform/Inc -I Kernel/Inc -I Core/Inc \
-I Drivers/CMSIS/Include -I Drivers/CMSIS/Device/ST/STM32F4xx/Include \
-I Drivers/STM32F4xx_HAL_Driver/Inc \
-I Middlewares/Third_Party/SEGGER_RTT -I Middlewares/Third_Party/cJSON \
-I Middlewares/Third_Party/LwIP/src/include -I Middlewares/Third_Party/LwIP/system \
-I Middlewares/Third_Party/FreeRTOS/Source/include \
-I Middlewares/Third_Party/FreeRTOS/Source/CMSIS_v2 \
-I Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I Compiler \
-std=gnu23 -Og -g -Wall -Wextra -ffunction-sections -fdata-sections -fno-common \
-fno-exceptions -fshort-enums -MMD -MP -c"
# 注：上方 CMSIS_v2 路径按仓库实况修正（FreeRTOS/Source/CMSIS_RTOS_V2）
CFLAGS_COMMON=${CFLAGS_COMMON/CMSIS_v2/CMSIS_RTOS_V2}

# 影子头文件（改前模块头 = 归档）
cp "$ARCH/$INC_YN"/*.h "$SH_YN/"
cp "$ARCH/$INC_GZ"/*.h "$SH_GZ/"
cp "$ARCH/$INC_MM"/*.h "$SH_MM/"

echo "== 改前源码（归档 = c61c895）md5 =="
( cd "$ARCH" && md5sum "$SRC_YN"/*.c "$INC_YN"/*.h "$SRC_GZ"/*.c "$INC_GZ"/*.h "$SRC_MM"/*.c "$INC_MM"/*.h Application/Src/app_factory_test_mm.c )

fail=0

# ---- 逐 TU 编译「改前 / 改后」----
# 参数：<模块标签 ym|gz|mm> <源目录> <模块头现行目录> <改前影子目录> <源文件…>
compile_module() {
  local tag="$1" srcdir="$2" incdir="$3" shdir="$4"; shift 4
  local f b flags_pre flags_cur
  for f in "$@"; do
    b="${f%.c}"
    flags_pre="${CFLAGS_COMMON/-I $incdir/-I $shdir}"
    flags_cur="$CFLAGS_COMMON"
    $CC $flags_pre -o "$PRE/$b.o" "$ARCH/$srcdir/$f" 2> "$PRE/$b.build.log" || fail=1
    $CC $flags_cur -o "$CUR/$b.o" "$srcdir/$f" 2> "$CUR/$b.build.log" || fail=1
    printf '  [%s] %-28s pre_warn=%s cur_warn=%s\n' "$tag" "$f" \
      "$(grep -c 'warning:' "$PRE/$b.build.log")" "$(grep -c 'warning:' "$CUR/$b.build.log")"
  done
}
echo; echo "== 编译改前（影子头文件）/ 改后（仓库现行）=="
compile_module yn "$SRC_YN" "$INC_YN" "$SH_YN" app_yn_ol_proto.c app_yn_ol_proto_cmd.c app_yn_ol_proto_parse.c
compile_module gz "$SRC_GZ" "$INC_GZ" "$SH_GZ" app_gz_ol_proto.c app_gz_ol_proto_cmd.c app_gz_ol_proto_default.c app_gz_ol_proto_parse.c
compile_module mm "$SRC_MM" "$INC_MM" "$SH_MM" app_mm_proto.c app_mm_proto_cmd.c
$CC ${CFLAGS_COMMON/-I $INC_MM/-I $SH_MM} -o "$PRE/app_factory_test_mm.o" "$ARCH/Application/Src/app_factory_test_mm.c" 2> "$PRE/app_factory_test_mm.build.log" || fail=1
$CC $CFLAGS_COMMON -o "$CUR/app_factory_test_mm.o" "Application/Src/app_factory_test_mm.c" 2> "$CUR/app_factory_test_mm.build.log" || fail=1
printf '  [mm] %-28s pre_warn=%s cur_warn=%s\n' app_factory_test_mm.c \
  "$(grep -c 'warning:' "$PRE/app_factory_test_mm.build.log")" "$(grep -c 'warning:' "$CUR/app_factory_test_mm.build.log")"

sec_md5() { # 打印「段名 md5」列表（排除调试/属性段）
  local f="$1" tmp; tmp=$(mktemp -d)
  while IFS= read -r s; do
    case "$s" in .debug*|.comment|.ARM.attributes) continue ;; esac
    arm-none-eabi-objcopy -O binary --only-section="$s" "$f" "$tmp/x.bin" 2>/dev/null || continue
    printf '  %-28s %s\n' "$s" "$(md5sum "$tmp/x.bin" | cut -d' ' -f1)"
  done < <(arm-none-eabi-objdump -h "$f" | awk '{print $2}' | grep -E '^\.')
  rm -rf "$tmp"
}

OBJS=$(cd "$CUR" && ls *.o | grep -v strip | sort)
for o in $OBJS; do
  b="${o%.o}"
  echo; echo "######## $b"
  echo "  整对象 md5（含 .debug*）：pre=$(md5sum "$PRE/$o" | cut -c1-32) cur=$(md5sum "$CUR/$o" | cut -c1-32)"
  arm-none-eabi-objcopy --strip-debug "$PRE/$o" "$PRE/$b.strip.o"
  arm-none-eabi-objcopy --strip-debug "$CUR/$o" "$CUR/$b.strip.o"
  if cmp -s "$PRE/$b.strip.o" "$CUR/$b.strip.o"; then
    echo "  ✔ strip-debug 对象逐字节一致（md5=$(md5sum "$CUR/$b.strip.o" | cut -c1-32)）"
  else
    echo "  ✘ strip-debug 对象有差异"; fail=1
  fi
  if diff <(sec_md5 "$PRE/$o") <(sec_md5 "$CUR/$o") > /dev/null; then
    echo "  ✔ 段级逐字节一致（.debug*/.comment/.ARM.attributes 除外；对照段数 = $(sec_md5 "$CUR/$o" | wc -l)）"
  else
    echo "  ✘ 段级差异："; diff <(sec_md5 "$PRE/$o") <(sec_md5 "$CUR/$o") | sed 's/^/    /'; fail=1
  fi
  arm-none-eabi-objdump -d "$PRE/$o" | grep -v 'file format' > "$PRE/$b.dis"
  arm-none-eabi-objdump -d "$CUR/$o" | grep -v 'file format' > "$CUR/$b.dis"
  if diff -q "$PRE/$b.dis" "$CUR/$b.dis" > /dev/null; then
    echo "  ✔ 反汇编逐行一致（$(wc -l < "$CUR/$b.dis") 行）"
  else
    echo "  ✘ 反汇编差异："; diff "$PRE/$b.dis" "$CUR/$b.dis" | sed 's/^/    /' | head -20; fail=1
  fi
  leak_pre=$(strings "$PRE/$b.strip.o" | grep -c 'Application/\|ProtocolParser_[A-Za-z_]*/' || true)
  leak_cur=$(strings "$CUR/$b.strip.o" | grep -c 'Application/\|ProtocolParser_[A-Za-z_]*/' || true)
  if [ "$leak_pre" -eq 0 ] && [ "$leak_cur" -eq 0 ]; then
    echo "  ✔ 非调试段无源码路径串（无 __FILE__/assert_param 消费者，注释行号变动安全）"
  else
    echo "  ⚠ 非调试段含源码路径串：pre=$leak_pre cur=$leak_cur（需改判据为段级对照）"; fail=1
  fi
done

echo; echo "######## 结果：$([ $fail -eq 0 ] && echo 'refine_round2 全部一致（注释收敛零机器码影响）' || echo '存在差异或编译失败，需人工复核')"
exit $fail
