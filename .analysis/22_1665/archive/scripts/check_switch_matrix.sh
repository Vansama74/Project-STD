#!/usr/bin/env bash
# 22-1665 驱动「开关矩阵」编译期自检（宿主，不需要硬件）
#
# 作用：对校准开关的每一种取值组合，单独语法编译驱动文件，验证
#   ① 合法组合能编过（不触发 _Static_assert / 不产生告警）；
#   ② 非法组合**必须**被 _Static_assert 拦下（口径自相矛盾不留到实机）。
#
# 用法：bash .analysis/22_1665/check_switch_matrix.sh
set -u

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
SRC="$ROOT/Device/Display/dev_display_22_1665.c"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

FLAGS=(
  -fsyntax-only -std=gnu23 -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
  -DUSE_HAL_DRIVER -DSTM32F407xx -DSTD_ALL_PROTO -Wall -Wextra -fshort-enums
  -I "$ROOT/Application/Inc" -I "$ROOT/Device/Inc" -I "$ROOT/Platform/Inc" -I "$ROOT/Kernel/Inc"
  -I "$ROOT/Core/Inc" -I "$ROOT/Drivers/CMSIS/Include"
  -I "$ROOT/Drivers/CMSIS/Device/ST/STM32F4xx/Include" -I "$ROOT/Drivers/STM32F4xx_HAL_Driver/Inc"
  -I "$ROOT/Middlewares/Third_Party/SEGGER_RTT" -I "$ROOT/Middlewares/Third_Party/cJSON"
  -I "$ROOT/Middlewares/Third_Party/LwIP/src/include" -I "$ROOT/Middlewares/Third_Party/LwIP/system"
  -I "$ROOT/Middlewares/Third_Party/FreeRTOS/Source/include"
  -I "$ROOT/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2"
  -I "$ROOT/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F" -I "$ROOT/Compiler"
)

pass=0; fail=0; bad=0

# 逐个组合：先给「应通过」的，再给「应被 assert 拦下」的
try () {
  local expect="$1"; shift
  local name="$1"; shift
  local out="$TMP/t.c"
  cp "$SRC" "$out"
  for kv in "$@"; do
    local key="${kv%%=*}" val="${kv#*=}"
    # 只替换该宏的默认值（文件里每条开关宏恰好定义一次）
    sed -i -E "s|^#define ${key} +\([^)]*\)|#define ${key} (${val}U)|" "$out"
  done
  local msg
  msg="$(arm-none-eabi-gcc "${FLAGS[@]}" "$out" 2>&1)"
  local rc=$?
  if [[ "$expect" == pass ]]; then
    if [[ $rc -eq 0 && -z "$msg" ]]; then
      printf '  ✓ [应通过] %-46s 编译干净\n' "$name"; pass=$((pass+1))
    else
      printf '  ✗ [应通过] %-46s 失败/告警：\n%s\n' "$name" "$msg"; bad=$((bad+1))
    fi
  else
    if [[ $rc -ne 0 ]] && grep -q "static assertion failed" <<<"$msg"; then
      printf '  ✓ [应拦下] %-46s 被 _Static_assert 拦下：%s\n' "$name" "$(grep -o '"[^"]*"' <<<"$msg" | head -1)"
      pass=$((pass+1))
    else
      printf '  ✗ [应拦下] %-46s 未被拦下（rc=%s）：\n%s\n' "$name" "$rc" "$msg"; bad=$((bad+1))
    fi
  fi
}

echo "== 1. 默认口径（与仓库当前宏一致，应零告警） =="
try pass "默认（SCAN_SEL=1 DUAL=0 SEG_LAYOUT=0 BLK_W=自动）"
echo
echo "== 2. 各结构开关单独取值（合法组合，应零告警） =="
try pass "SCAN_SEL=4（1/4 扫）"                _22_1665_SCAN_SEL=4
try pass "SCAN_SEL=2（1/2 扫）"                _22_1665_SCAN_SEL=2
try pass "SCAN_SEL=8（1/8 扫）"                _22_1665_SCAN_SEL=8
try pass "SCAN_SEL=16（1/16 扫）"              _22_1665_SCAN_SEL=16
try pass "DUAL=1（R1+G1 双链）"                _22_1665_CHAIN_DUAL=1
try pass "SEG_LAYOUT=1（段优先）"              _22_1665_CHAIN_SEG_LAYOUT=1
try pass "SEG_LAYOUT=2（片内 R/G 交错）"       _22_1665_CHAIN_SEG_LAYOUT=2
try pass "SEG_LAYOUT=2 + BLK_W=8"              _22_1665_CHAIN_SEG_LAYOUT=2 _22_1665_CHAIN_BLK_W=8
try pass "BLK_W=8（8x2 块）"                   _22_1665_CHAIN_BLK_W=8
try pass "BLK_W=4（4x4 块）"                   _22_1665_CHAIN_BLK_W=4
try pass "BLK_W=2（2x8 块）"                   _22_1665_CHAIN_BLK_W=2
try pass "BLK_W=1（一列 16 像素）"             _22_1665_CHAIN_BLK_W=1
try pass "G_FIRST=1"                           _22_1665_CHAIN_G_FIRST=1
try pass "LINE_REVERSE=1"                      _22_1665_CHAIN_LINE_REVERSE=1
try pass "BIT_REVERSE=1"                       _22_1665_CHAIN_BIT_REVERSE=1
try pass "DATA_ACTIVE_HIGH=0"                  _22_1665_CHAIN_DATA_ACTIVE_HIGH=0
try pass "BLUE_LINK=1"                         _22_1665_BLUE_LINK=1
try pass "DUAL=1 + SCAN_SEL=4"                 _22_1665_CHAIN_DUAL=1 _22_1665_SCAN_SEL=4
try pass "BLK_W=4 + SCAN_SEL=4"                _22_1665_CHAIN_BLK_W=4 _22_1665_SCAN_SEL=4
try pass "SEG_LAYOUT=1 + SCAN_SEL=8"           _22_1665_CHAIN_SEG_LAYOUT=1 _22_1665_SCAN_SEL=8
try pass "MODULE_ROWS=2（32x16 拼装）"         _22_1665_MODULE_ROWS=2
try fail "SEGMENTS=3（16 位片宽装不下 3 段）"  _22_1665_SEGMENTS=3
echo
echo "== 3. 探针三种模式（应零告警；探针代码仅在开启时编译） =="
try pass "PROBE=1（走点）"                     _22_1665_CHAIN_PROBE=1
try pass "PROBE=2（区间全亮）"                 _22_1665_CHAIN_PROBE=2
try pass "PROBE=3（半链）"                     _22_1665_CHAIN_PROBE=3
try pass "PROBE=1 + DUAL=1"                    _22_1665_CHAIN_PROBE=1 _22_1665_CHAIN_DUAL=1
echo
echo "== 4. 非法口径（必须被 _Static_assert 拦下） =="
try fail "DUAL=1 + SEG_LAYOUT=1（双链时段排布无意义）" _22_1665_CHAIN_DUAL=1 _22_1665_CHAIN_SEG_LAYOUT=1
try fail "SEG_LAYOUT=2 + BLK_W=16（块宽 > 每片像素数）" _22_1665_CHAIN_SEG_LAYOUT=2 _22_1665_CHAIN_BLK_W=16
try fail "SCAN_SEL=3（非法相数）"              _22_1665_SCAN_SEL=3
try fail "SEG_LAYOUT=3（非法段排布）"          _22_1665_CHAIN_SEG_LAYOUT=3
try fail "BLK_W=3（不整除 16）"                _22_1665_CHAIN_BLK_W=3
try fail "MODULE_COLS=2（多链模型不成立）"     _22_1665_MODULE_COLS=2
try fail "CHANNELS_PER_MODULE=2（非单链模型）" _22_1665_CHANNELS_PER_MODULE=2
try fail "PROBE=4（非法探针模式）"             _22_1665_CHAIN_PROBE=4

echo
echo "== 汇总：通过 ${pass} / 未通过 ${bad} =="
[[ $bad -eq 0 ]] || exit 1
