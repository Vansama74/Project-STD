#!/usr/bin/env bash
# 22-1665 驱动「几何 / 开关」编译矩阵（宿主，不参与固件构建）
#
# 用 `arm-none-eabi-gcc -fsyntax-only -Wall -Wextra` 逐个组合编译驱动文件：
#   A. 合法口径必须**零告警编过**：
#       1×1（默认）；扩屏 2×1 / 1×2 / 2×2 / 3×2 / 1×5（通道上限）；
#       8×2（128×32）/ **8×4（128×64，工作区当前宏值）** / 12×1 / 6×2；
#       46×1（CCM 65024B，整片 64KB 区域内的边界点）；单模块 32×8 变体（同为 256 像素）；
#       链首口径开关 CHAIN_HEAD_IS_MODULE0=1（单模块与扩屏）
#   B. **旧非法口径的现行行为（第三十四轮起，实测写死）** —— 编译期防御已按用户裁决全部
#      删除，故这些口径**不再被拦下**：
#       · `warn` 期望：COLS=0 / ROWS=0 → 编译通过但有 `-Wtype-limits`（恒假比较）；
#       · `ok`（零告警）期望：PIXEL_ROW=24 / PIXEL_COL=32 / PIXEL_COL=30 / COLS=6
#         → **静默错口径 / 越界**（契约改由源码注释 + doc/01 §0.3 承担；
#         COLS=6 在 -O3 代码生成下另有 UB 告警，见 `check_eide_command.sh`）；
#   C. **链接期兜底（实测，快）**：本模组数组 > 整片 CCMRAM 区域 64KB 时由链接器报错 ——
#       47×1（对象零告警，.ccmram 66432B）最小链接 → `region CCMRAM overflowed by 896 bytes`；
#       1×1 对照 → 零溢出。
#   D. 陷阱回归：几何宏已是兄弟驱动同款**普通 `#define`** ⇒ 命令行 `-D` 只给「重定义告警 +
#       文件值胜出」（换口径必须走 `macro_override.py` 源码替换）。
#
# 注：几何已全派生（第二十四轮）——**改模块数不再需要补任何表**（除通道上限那张固定 5 组表）；
#     接线形态唯一（第二十九轮起删掉 `_22_1665_HUB_WIRING` / `_22_1665_BLUE_AS_LIT` /
#     `_22_1665_DATA_ACTIVE_HIGH` 三化石开关）；
#     **第三十四轮**：17 条 `_Static_assert`（含 CCM 区域物理守卫）全部删除 ——
#     原「非法口径」用例由「编译失败 + 可读断言」改为 B / C 两节的实测期望。
# 与 EIDE 完全同命令行的复现（含 -O3 的真实告警）见 `check_eide_command.sh`。
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SRC="$ROOT/Device/Display/dev_display_22_1665.c"
GCC=arm-none-eabi-gcc
INC=(-I "$ROOT/Application/Inc" -I "$ROOT/Device/Inc" -I "$ROOT/Platform/Inc" -I "$ROOT/Kernel/Inc"
     -I "$ROOT/Core/Inc" -I "$ROOT/Drivers/CMSIS/Include"
     -I "$ROOT/Drivers/CMSIS/Device/ST/STM32F4xx/Include"
     -I "$ROOT/Drivers/STM32F4xx_HAL_Driver/Inc" -I "$ROOT/Middlewares/Third_Party/SEGGER_RTT")
BASE=(-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DUSE_HAL_DRIVER -DSTM32F407xx
      -std=gnu23 -Wall -Wextra -fsyntax-only)
# 编对象用（C 节的最小链接需要真对象，不能用 -fsyntax-only）
BASE_C=(-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DUSE_HAL_DRIVER -DSTM32F407xx
        -std=gnu23 -Wall -Wextra -c)

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0

# src_for <tag> [宏名=值 ...]：打印源码路径；带几何覆盖时 = 源码替换后的临时副本
src_for() {
    local tag="$1"; shift
    if [ "$#" -eq 0 ]; then printf '%s\n' "$SRC"; return 0; fi
    python3 "$HERE/macro_override.py" "$SRC" "$TMP/$tag.c" "$@" || exit 3
    printf '%s\n' "$TMP/$tag.c"
}

check() {                       # $1 = 描述, $2 = 期望(ok|warn|bad), 其余 = 编译参数（末项须为源码路径）
    local desc="$1" want="$2"; shift 2
    local out rc got
    out=$("$GCC" "${BASE[@]}" "${INC[@]}" "$@" 2>&1); rc=$?
    if [[ $rc -eq 0 && -n "$out" ]]; then got=warn; elif [[ $rc -eq 0 ]]; then got=ok; else got=bad; fi
    if [[ "$got" != "$want" ]]; then
        echo "  [失败] $desc（期望 $want，实际 $got）"; echo "$out" | head -6; fail=$((fail+1))
    else
        echo "  [通过] $desc"; pass=$((pass+1))
    fi
}

# 注意：源码里的模块数宏是「用户在用的旋钮」——本矩阵的每个用例都显式钉死两个模块宏
# （第三十轮起经源码替换：普通 `#define` 下 `-D` 只写告警、文件值胜出），
# 否则「只覆盖其中一个」的用例会随源码当前值漂移（例：源码 2×2 时 11×1 会变成 11×2 → CCM 超限）。
PIN1X1=("_22_1665_MODULE_ROWS=1" "_22_1665_MODULE_COLS=1")

echo "== A. 合法口径（须零告警编过）=="
check "默认：1×1 模块 × 16×16 → 16×16 / 4 链段 / 128 时钟 / CCM 1664B" ok \
      "$(src_for base "${PIN1X1[@]}")"
check "扩屏 2×1 → 32×16（8 链段 / 256 时钟 / CCM 3072B）" ok \
      "$(src_for r2c1 _22_1665_MODULE_ROWS=2 _22_1665_MODULE_COLS=1)"
check "扩屏 1×2 → 16×32（8 链段 / 128 时钟 / CCM 3328B）" ok \
      "$(src_for r1c2 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=2)"
check "扩屏 2×2 → 32×32（16 链段 / 256 时钟 / CCM 6144B）" ok \
      "$(src_for r2c2 _22_1665_MODULE_ROWS=2 _22_1665_MODULE_COLS=2)"
check "扩屏 3×2 → 48×32（24 链段 / 384 时钟 / CCM 8960B）" ok \
      "$(src_for r3c2 _22_1665_MODULE_ROWS=3 _22_1665_MODULE_COLS=2)"
check "通道上限 1×5 → 16×80（2×5 = 10 通道 / CCM 8320B）" ok \
      "$(src_for r1c5 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=5)"
check "现场几何 8×2 → 128×32（CCM 23040B）" ok \
      "$(src_for r8c2 _22_1665_MODULE_ROWS=8 _22_1665_MODULE_COLS=2)"
check "工作区当前宏值 8×4 → 128×64（CCM 46080B；整片 PROTO=ALL 会链接期溢出，见 C 节注）" ok \
      "$(src_for r8c4 _22_1665_MODULE_ROWS=8 _22_1665_MODULE_COLS=4)"
check "旧界外 12×1 → 192×16（CCM 17152B，旧 16KB 预算下曾非法）" ok \
      "$(src_for r12c1 _22_1665_MODULE_ROWS=12 _22_1665_MODULE_COLS=1)"
check "旧界外 6×2 → 96×32（CCM 17408B，同上）" ok \
      "$(src_for r6c2 _22_1665_MODULE_ROWS=6 _22_1665_MODULE_COLS=2)"
check "竖排 11×1 → 176×16（组内 11 块串链 / CCM 15744B）" ok \
      "$(src_for r11c1 _22_1665_MODULE_ROWS=11 _22_1665_MODULE_COLS=1)"
check "区域上限边界 46×1 → 736×16（CCM 65024B ≤ 65536B；47×1 即超，见 C 节）" ok \
      "$(src_for r46c1 _22_1665_MODULE_ROWS=46 _22_1665_MODULE_COLS=1)"
check "单模块 32×8（同为 256 像素 / 块栅格 8×1）" ok \
      "$(src_for pix32x8 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=1 \
            _22_1665_MODULE_PIXEL_ROW=32 _22_1665_MODULE_PIXEL_COL=8)"
check "链首口径开关：CHAIN_HEAD_IS_MODULE0=1（多模块现场 A/B；仍带 #ifndef，用 -D）" ok \
      "$(src_for base "${PIN1X1[@]}")" -D_22_1665_CHAIN_HEAD_IS_MODULE0=1
check "链首口径开关 + 扩屏：1×2 + CHAIN_HEAD_IS_MODULE0=1" ok \
      "$(src_for r1c2 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=2)" \
      -D_22_1665_CHAIN_HEAD_IS_MODULE0=1

echo "== B. 旧非法口径的现行行为（无编译期防御，第三十四轮实测写死）=="
echo "  —— 下面这些口径第三十四轮以前被 _Static_assert 拦下；现在要靠人看注释 / 看链接期 ——"
check "模块数 0（MODULE_COLS=0）→ 编过但恒假比较告警（-Wtype-limits），不再拦下" warn \
      "$(src_for c0 "${PIN1X1[@]}" _22_1665_MODULE_COLS=0)"
check "模块数 0（MODULE_ROWS=0）→ 同上" warn \
      "$(src_for r0 _22_1665_MODULE_ROWS=0 _22_1665_MODULE_COLS=1)"
check "单模块 24×16（区域块数 12 ≠ 8）→ **零告警编过**（静默错口径）" ok \
      "$(src_for pix24 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=1 _22_1665_MODULE_PIXEL_ROW=24)"
check "单模块 16×32（区域块数 16 ≠ 8）→ 零告警编过（静默错口径）" ok \
      "$(src_for pix16x32 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=1 _22_1665_MODULE_PIXEL_COL=32)"
check "MODULE_PIXEL_COL=30（半屏高 15 非 4 的倍数）→ 零告警编过（静默错口径）" ok \
      "$(src_for pix30 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=1 _22_1665_MODULE_PIXEL_COL=30)"
check "列数 6（2×6 = 12 通道 > HUB75_CHANNEL_MAX = 10）→ -fsyntax-only 零告警编过" ok \
      "$(src_for r1c6 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=6)"
echo "  （COLS=6 在 -O3 代码生成下会报 UB 告警 —— 见 check_eide_command.sh；"
echo "    运行期后果 = 越界读 _22_1665_lines[5]）"

echo "== C. 链接期兜底（本模组数组 > 整片 CCMRAM 区域 64KB 时由链接器报错）=="
echo "  注：整片能否装下只能靠链接期 —— 现场当前宏值 8×4（本模组 46080B）+ 其他消费者"
echo "      35836B = 81916B > 65536B ⇒ PROTO=ALL 整片构建实测溢出 16380B（预存状态）；"
echo "      本节只验「本模组对象单独进区域」这一物理事实（最小链接：驱动对象 + 桩 + 真链接脚本）。"
cat > "$TMP/stub.c" <<'EOF'
/* 最小链接桩：只为把驱动对象放进真实链接脚本的 CCMRAM 区域（验证链接期兜底） */
#include "dev_display.h"
#include "pl_hub75.h"
const hub75_pin_t g_hub75_pin_r[HUB75_CHANNEL_MAX];
const hub75_pin_t g_hub75_pin_g[HUB75_CHANNEL_MAX];
const hub75_pin_t g_hub75_pin_b[HUB75_CHANNEL_MAX];
void dev_display_register(dev_display_t *dev) { (void)dev; }
EOF
"$GCC" "${BASE_C[@]}" "${INC[@]}" -o "$TMP/stub.o" "$TMP/stub.c" || { echo "  [失败] 链接桩编译失败"; fail=$((fail+1)); }
link_case() {   # link_case <描述> <期望(overflow|ok)> <tag> [宏名=值 ...]
    local desc="$1" want="$2" tag="$3"; shift 3
    local src obj elf out rc
    src="$(src_for "$tag" "$@")"
    obj="$TMP/$tag.o"; elf="$TMP/$tag.elf"
    if ! out=$("$GCC" "${BASE_C[@]}" "${INC[@]}" -o "$obj" "$src" 2>&1); then
        echo "  [失败] $desc（对象编译失败）"; echo "$out" | head -4; fail=$((fail+1)); return
    fi
    if [ -n "$out" ]; then
        echo "  [失败] $desc（对象编译有告警）"; echo "$out" | head -4; fail=$((fail+1)); return
    fi
    out=$("$GCC" -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -nostdlib -nostartfiles -Wl,-e,dev_display_22_1665_init \
          -T "$ROOT/Compiler/STM32F407XX_FLASH.ld" -o "$elf" "$TMP/stub.o" "$obj" 2>&1); rc=$?
    if [ "$want" = overflow ]; then
        if [ $rc -ne 0 ] && grep -q "region \`CCMRAM' overflowed" <<<"$out"; then
            echo "  [通过] $desc"; echo "         $(grep -o "region .CCMRAM. overflowed by [0-9]* bytes" <<<"$out")"
            pass=$((pass+1))
        else
            echo "  [失败] $desc（期望链接期 region overflow，实际 exit=$rc）"; echo "$out" | head -4
            fail=$((fail+1))
        fi
    else
        if [ $rc -eq 0 ] && ! grep -q "overflowed" <<<"$out"; then
            echo "  [通过] $desc"; pass=$((pass+1))
        else
            echo "  [失败] $desc（期望链接通过且零溢出，实际 exit=$rc）"; echo "$out" | head -4
            fail=$((fail+1))
        fi
    fi
}
link_case "47×1（CCM 66432B > 64KB）→ 对象零告警，链接期 overflow 896B" overflow \
      r47c1 _22_1665_MODULE_ROWS=47 _22_1665_MODULE_COLS=1
link_case "对照 1×1（CCM 1664B）→ 链接零溢出" ok \
      ok1x1 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=1
link_case "对照 8×4（CCM 46080B，工作区宏值）→ 单独进区域零溢出（整片溢出见上方注）" ok \
      ok8x4 _22_1665_MODULE_ROWS=8 _22_1665_MODULE_COLS=4

echo "== D. 陷阱回归（第三十轮：几何宏已无 #ifndef 守卫，-D 必须「不生效且出声」）=="
trap_out=$("$GCC" "${BASE[@]}" "${INC[@]}" -D_22_1665_MODULE_COLS=2 "$SRC" 2>&1)
if grep -qE '_22_1665_MODULE_COLS.*redefined' <<<"$trap_out"; then
    echo "  [通过] -D_22_1665_MODULE_COLS=2 → 重定义告警（文件值胜出）⇒ 换口径必须走源码替换"
    pass=$((pass+1))
else
    echo "  [失败] -D_22_1665_MODULE_COLS=2 未出现重定义告警——几何宏可能又被改回 #ifndef 守卫"
    echo "$trap_out" | head -5
    fail=$((fail+1))
fi

echo
echo "结果：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
