#!/usr/bin/env bash
# 22-1665 驱动「模型矩阵」编译自检（宿主，不参与固件构建）
#
# 逐个组合用 `arm-none-eabi-gcc -fsyntax-only -Wall -Wextra` 编译驱动文件：
#   · 探针模式 0..9（7 = 逐脚标识；8 = 逐脚闪烁 + 纹理；9 = 逐链×逐脚内容扫描 = 第十七轮新增）
#   · 数据脚掩码 _22_1665_DATA_LINES（10 脚全开 0x3FF / 单脚 R1 0x01 / 地址脚单脚 0x40 /
#     任意组合 / 非法 0 与 0x400）
#   · 块排布 BLK_COLS = 1 / 2 / 4 / 8（屏面 4x32 / 8x16 / 16x8 / 32x4）
#   · 数据极性 1 / 0
#   · 上电引脚自检 / RTT 输出的开与关（含探针 8/9 + 无 RTT 的组合）
#   · 落点镜像旗标 _22_1665_XF_*（第十七轮新增：恒等/各单旗标/ROT180）
#   · 非法组合（BLK_COLS=3、PROBE=10、掩码越界/为空、探针 9 的脚掩码含 R1 / 链掩码越界 /
#     停留过短）必须被 _Static_assert 拦下
# 期望：合法组合 **零告警** 编过；非法组合编译失败。
set -u

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
SRC="$ROOT/.analysis/22_1665/archive/prev_round/dev_display_22_1665_round23_pre_refactor.c"
GCC=arm-none-eabi-gcc
INC=(-I "$ROOT/Application/Inc" -I "$ROOT/Device/Inc" -I "$ROOT/Platform/Inc" -I "$ROOT/Kernel/Inc"
     -I "$ROOT/Core/Inc" -I "$ROOT/Drivers/CMSIS/Include"
     -I "$ROOT/Drivers/CMSIS/Device/ST/STM32F4xx/Include"
     -I "$ROOT/Drivers/STM32F4xx_HAL_Driver/Inc" -I "$ROOT/Middlewares/Third_Party/SEGGER_RTT")
BASE=(-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DUSE_HAL_DRIVER -DSTM32F407xx
      -std=gnu23 -Wall -Wextra -fsyntax-only "${INC[@]}")

pass=0; fail=0

check() {                       # $1 = 描述, $2 = 期望(ok|bad), 其余 = -D 定义
    local desc="$1" want="$2"; shift 2
    local out
    if out=$("$GCC" "${BASE[@]}" "$@" "$SRC" 2>&1); then got=ok; else got=bad; fi
    if [[ "$got" == "$want" ]]; then
        if [[ -n "$out" ]]; then echo "  [警告?] $desc"; echo "$out" | head -5; pass=$((pass+1));
        else echo "  [通过] $desc"; pass=$((pass+1)); fi
    else
        echo "  [失败] $desc (期望 $want，实际 $got)"; echo "$out" | head -8; fail=$((fail+1))
    fi
}

echo "== 多链框架（第十六轮）：模式 × 探针 × 链数 × 极性 × 蓝策略 =="
for m in 0 1; do
    for p in 0 8; do
        check "MULTI_CHAIN=$m + PROBE=$p" ok -D_22_1665_MULTI_CHAIN=$m -D_22_1665_CHAIN_PROBE=$p
    done
    check "MULTI_CHAIN=$m + PROBE=7" ok -D_22_1665_MULTI_CHAIN=$m -D_22_1665_CHAIN_PROBE=7
    check "MULTI_CHAIN=$m + DATA_ACTIVE_HIGH=0" ok -D_22_1665_MULTI_CHAIN=$m -D_22_1665_DATA_ACTIVE_HIGH=0
    check "MULTI_CHAIN=$m + BLUE_AS_LIT=0" ok -D_22_1665_MULTI_CHAIN=$m -D_22_1665_BLUE_AS_LIT=0
    check "MULTI_CHAIN=$m + RTT=0 + 自检=0" ok -D_22_1665_MULTI_CHAIN=$m -D_22_1665_RTT_DIAG=0 \
          -D_22_1665_PIN_SELFTEST=0
done
for c in 1 2 3 4 6 8; do
    check "MULTI_CHAIN=1 + CHAIN_COUNT=$c" ok -D_22_1665_CHAIN_COUNT=$c
done
for c in 1 2 4 8; do
    check "MULTI_CHAIN=0 + BLK_COLS=$c（兼容屏面 4x32/8x16/16x8/32x4）" ok \
          -D_22_1665_MULTI_CHAIN=0 -D_22_1665_BLK_COLS=$c
done

echo "== 探针模式 0..9 =="
for p in 0 1 2 3 4 5 6 7 8 9; do
    check "CHAIN_PROBE=$p" ok -D_22_1665_CHAIN_PROBE=$p
done

echo "== 探针 9 专项（第十七轮）：脚掩码 / 链掩码 / 停留 / 链数 / 兼容模式 =="
check "PROBE=9 + 默认脚掩码（A/B/C/D）"        ok -D_22_1665_CHAIN_PROBE=9
check "PROBE=9 + 全部 10 脚（除 R1）"           ok -D_22_1665_CHAIN_PROBE=9 -D_22_1665_PROBE9_PINS=0x3FE
check "PROBE=9 + 单脚 A"                        ok -D_22_1665_CHAIN_PROBE=9 -D_22_1665_PROBE9_PINS=0x040
check "PROBE=9 + 六 RGB（不含 R1）"             ok -D_22_1665_CHAIN_PROBE=9 -D_22_1665_PROBE9_PINS=0x03E
check "PROBE=9 + 快档 250ms"                    ok -D_22_1665_CHAIN_PROBE=9 -D_22_1665_PROBE9_HALF_MS=250
check "PROBE=9 + 只扫链 0/2"                    ok -D_22_1665_CHAIN_PROBE=9 -D_22_1665_PROBE9_CHAINS=0x05
check "PROBE=9 + CHAIN_COUNT=1"                 ok -D_22_1665_CHAIN_PROBE=9 -D_22_1665_CHAIN_COUNT=1
check "PROBE=9 + CHAIN_COUNT=8"                 ok -D_22_1665_CHAIN_PROBE=9 -D_22_1665_CHAIN_COUNT=8
check "PROBE=9 + 兼容模式（MULTI_CHAIN=0）"     ok -D_22_1665_CHAIN_PROBE=9 -D_22_1665_MULTI_CHAIN=0
check "PROBE=9 + RTT=0 + 自检=0"                ok -D_22_1665_CHAIN_PROBE=9 -D_22_1665_RTT_DIAG=0 \
      -D_22_1665_PIN_SELFTEST=0
check "PROBE=9 + 极性 0"                        ok -D_22_1665_CHAIN_PROBE=9 -D_22_1665_DATA_ACTIVE_HIGH=0
check "PROBE=9 + BLK_COLS=2 + 兼容"             ok -D_22_1665_CHAIN_PROBE=9 -D_22_1665_MULTI_CHAIN=0 \
      -D_22_1665_BLK_COLS=2

echo "== 落点镜像旗标（第十七轮）：_22_1665_XF_* 宏值可 -D 覆盖、语义由 check_round17.py 复算 =="
check "旗标宏可覆盖（X=1）+ PROBE=9"           ok -D_22_1665_CHAIN_PROBE=9 -D_22_1665_XF_X=1
check "旗标宏可覆盖（ROT180 组合值）+ 兼容模式" ok -D_22_1665_MULTI_CHAIN=0 -D_22_1665_XF_ROT180=0xF

echo "== 数据脚掩码（10 脚全开 / 单脚 / 地址脚 / 组合） =="
check "DATA_LINES=0x3FF（10 脚全开，默认口径）"  ok -D_22_1665_DATA_LINES=0x3FF
check "DATA_LINES=0x3F（六脚 RGB 全开 = 上一轮口径）" ok -D_22_1665_DATA_LINES=0x3F
check "DATA_LINES=0x01（仅 R1 = 第十二轮 SDI 口径）" ok -D_22_1665_DATA_LINES=0x01
check "DATA_LINES=0x40（仅 A，地址脚单脚）"      ok -D_22_1665_DATA_LINES=0x40
check "DATA_LINES=0x200（仅 D，地址脚单脚）"     ok -D_22_1665_DATA_LINES=0x200
check "DATA_LINES=0x3C0（仅 A/B/C/D 四根地址脚）" ok -D_22_1665_DATA_LINES=0x3C0
check "DATA_LINES=0x0A（G1+R2）"                 ok -D_22_1665_DATA_LINES=0x0A
check "DATA_LINES=0x21（R1+B2）"                 ok -D_22_1665_DATA_LINES=0x21
check "DATA_LINES=0x3FF + PROBE=7"               ok -D_22_1665_DATA_LINES=0x3FF -D_22_1665_CHAIN_PROBE=7
check "DATA_LINES=0x01 + PROBE=7（单脚标识）"    ok -D_22_1665_DATA_LINES=0x01 -D_22_1665_CHAIN_PROBE=7
check "DATA_LINES=0x40 + PROBE=7（地址脚标识）"  ok -D_22_1665_DATA_LINES=0x40 -D_22_1665_CHAIN_PROBE=7
check "DATA_LINES=0x3FF + PROBE=8（逐脚闪烁）"   ok -D_22_1665_DATA_LINES=0x3FF -D_22_1665_CHAIN_PROBE=8
check "DATA_LINES=0x01 + PROBE=8（只测 R1）"     ok -D_22_1665_DATA_LINES=0x01 -D_22_1665_CHAIN_PROBE=8
check "DATA_LINES=0x03F + PROBE=8（只测六 RGB）" ok -D_22_1665_DATA_LINES=0x03F -D_22_1665_CHAIN_PROBE=8
check "DATA_LINES=0x3C0 + PROBE=8（只测四地址）" ok -D_22_1665_DATA_LINES=0x3C0 -D_22_1665_CHAIN_PROBE=8
check "DATA_LINES=0x3FF + PROBE=8（闪烁快档 250ms）" ok -D_22_1665_DATA_LINES=0x3FF -D_22_1665_CHAIN_PROBE=8 \
      -D_22_1665_PROBE8_HALF_MS=250
check "DATA_LINES=0x3FF + PROBE=8 + RTT=0（静默）" ok -D_22_1665_DATA_LINES=0x3FF -D_22_1665_CHAIN_PROBE=8 \
      -D_22_1665_RTT_DIAG=0
check "DATA_LINES=0x3FF + PROBE=8 + 自检=0"      ok -D_22_1665_DATA_LINES=0x3FF -D_22_1665_CHAIN_PROBE=8 \
      -D_22_1665_PIN_SELFTEST=0

echo "== 块排布 BLK_COLS（1/2/4/8）+ 极性 + 探针 8 =="
for c in 1 2 4 8; do
    check "BLK_COLS=$c, ACTIVE_HIGH=1" ok "-D_22_1665_BLK_COLS=$c"
    check "BLK_COLS=$c, ACTIVE_HIGH=0" ok "-D_22_1665_BLK_COLS=$c" -D_22_1665_DATA_ACTIVE_HIGH=0
    check "BLK_COLS=$c, ACTIVE_HIGH=0 + PROBE=7 + 0x3FF" ok "-D_22_1665_BLK_COLS=$c" \
          -D_22_1665_DATA_ACTIVE_HIGH=0 -D_22_1665_CHAIN_PROBE=7 -D_22_1665_DATA_LINES=0x3FF
    check "BLK_COLS=$c, PROBE=8 + 0x3FF" ok "-D_22_1665_BLK_COLS=$c" \
          -D_22_1665_CHAIN_PROBE=8 -D_22_1665_DATA_LINES=0x3FF
done

echo "== 非法组合（必须被 _Static_assert 拦下）=="
check "BLK_COLS=3（不整除 8）" bad -D_22_1665_BLK_COLS=3
check "BLK_COLS=5（不在允许集）" bad -D_22_1665_BLK_COLS=5
check "PROBE=10（越界）" bad -D_22_1665_CHAIN_PROBE=10
check "PROBE=4 + POS=16（越界）" bad -D_22_1665_CHAIN_PROBE=4 -D_22_1665_CHAIN_PROBE_POS=16
check "PROBE=8 + 纹波段为 0（停留 3 / 闪烁 3）" bad -D_22_1665_CHAIN_PROBE=8 \
      -D_22_1665_PROBE8_DWELL_HALVES=3 -D_22_1665_PROBE8_BLINK_HALVES=3
check "PROBE=9 + 脚掩码为空" bad -D_22_1665_CHAIN_PROBE=9 -D_22_1665_PROBE9_PINS=0
check "PROBE=9 + 脚掩码含 R1（0x001）" bad -D_22_1665_CHAIN_PROBE=9 -D_22_1665_PROBE9_PINS=0x001
check "PROBE=9 + 脚掩码第 11 位" bad -D_22_1665_CHAIN_PROBE=9 -D_22_1665_PROBE9_PINS=0x401
check "PROBE=9 + 链掩码为空" bad -D_22_1665_CHAIN_PROBE=9 -D_22_1665_PROBE9_CHAINS=0
check "PROBE=9 + 链掩码越界（0x10、4 链）" bad -D_22_1665_CHAIN_PROBE=9 -D_22_1665_PROBE9_CHAINS=0x10
check "PROBE=9 + 停留过短（100ms）" bad -D_22_1665_CHAIN_PROBE=9 -D_22_1665_PROBE9_HALF_MS=100
check "DATA_LINES=0（空掩码）" bad -D_22_1665_DATA_LINES=0
check "DATA_LINES=0x400（越界位，第 11 位）" bad -D_22_1665_DATA_LINES=0x400
check "CHAIN_COUNT=0（无链）" bad -D_22_1665_CHAIN_COUNT=0
check "CHAIN_COUNT=9（状态数 > 256）" bad -D_22_1665_CHAIN_COUNT=9
check "MULTI_CHAIN=0 + BLK_COLS=3（不整除 8）" bad -D_22_1665_MULTI_CHAIN=0 -D_22_1665_BLK_COLS=3

echo
echo "结果：通过 $pass / 失败 $fail"
exit $((fail > 0))
