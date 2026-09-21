#!/usr/bin/env bash
# `{` 帧族互斥守卫 g_brace_proto_guard 实测（云南常规 app_yn_proto ↔ 云南治超 app_yn_ol_proto）
#
# 语义：两模块各在 `#ifndef STD_ALL_PROTO` 内定义 `char g_brace_proto_guard;`。
#   · 量产口径（EIDE，无 STD_ALL_PROTO）：同时编入 → 链接期 multiple definition（强制二选一）；
#   · 开发口径（Makefile，-DSTD_ALL_PROTO）：守卫不生成 → 可共存（probe 注册序约束）。
# 本脚本从 build stamp 取真实 CFLAGS（去掉 -DSTD_ALL_PROTO），两文件各编一次，
# 再用 arm-none-eabi-ld -r 合并，打印实测结果。
#
# 用法：bash .analysis/yn_ol/guard_test.sh
set -u
cd "$(dirname "$0")/../.." || exit 1
OUT=.analysis/yn_ol/guard_test
mkdir -p "$OUT"

STAMP=build/Debug/.build_stamp
[ -f "$STAMP" ] || { echo "缺少 $STAMP（先做一次 make）"; exit 1; }
CFLAGS=$(sed -n 's/^CFLAGS=//p' "$STAMP")
[ -n "$CFLAGS" ] || { echo "stamp 内无 CFLAGS"; exit 1; }

# 去掉开发口径豁免宏 + 依赖生成（守卫判定只看 STD_ALL_PROTO 是否定义）
PROD_FLAGS=$(echo "$CFLAGS" | sed -e 's/-DSTD_ALL_PROTO//' -e 's/-MMD//' -e 's/-MP//')

echo "== 无 -DSTD_ALL_PROTO（量产口径）分别编译 =="
arm-none-eabi-gcc $PROD_FLAGS -c Application/Src/ProtocolParser_YunNan/app_yn_proto.c \
    -o "$OUT/yn_regular.o" || exit 1
arm-none-eabi-gcc $PROD_FLAGS -c Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c \
    -o "$OUT/yn_ol.o" || exit 1
echo "  yn_regular.o: $(arm-none-eabi-nm "$OUT/yn_regular.o" | grep -c g_brace_proto_guard) 个 g_brace_proto_guard"
echo "  yn_ol.o     : $(arm-none-eabi-nm "$OUT/yn_ol.o" | grep -c g_brace_proto_guard) 个 g_brace_proto_guard"

echo "== arm-none-eabi-ld -r 合并（期望：multiple definition）=="
arm-none-eabi-ld -r "$OUT/yn_regular.o" "$OUT/yn_ol.o" -o "$OUT/merged_prod.o" 2>&1 | tee "$OUT/merged_prod.err"
RC=${PIPESTATUS[0]}
echo "ld -r exit=$RC"
if [ "$RC" -ne 0 ] && grep -q "multiple definition of .g_brace_proto_guard" "$OUT/merged_prod.err"; then
    echo "GUARD=ENFORCED（量产口径二选一被链接期强制）"
else
    echo "GUARD=NOT-ENFORCED（异常：请检查守卫宏）"
fi

echo "== 加 -DSTD_ALL_PROTO（开发口径）合并（期望：成功，守卫不生成）=="
DEV_FLAGS="$PROD_FLAGS -DSTD_ALL_PROTO"
arm-none-eabi-gcc $DEV_FLAGS -c Application/Src/ProtocolParser_YunNan/app_yn_proto.c \
    -o "$OUT/yn_regular_all.o" || exit 1
arm-none-eabi-gcc $DEV_FLAGS -c Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c \
    -o "$OUT/yn_ol_all.o" || exit 1
arm-none-eabi-ld -r "$OUT/yn_regular_all.o" "$OUT/yn_ol_all.o" -o "$OUT/merged_all.o" 2>&1
echo "ld -r exit=$?"
