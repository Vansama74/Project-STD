#!/usr/bin/env bash
# 22-1665 宿主验证汇总入口（一次跑完所有现行脚本，输出通过 / 失败汇总）
#
# 用法：
#   bash .analysis/22_1665/verify_all.sh          # 全部（含 link_variant 换口径重链）
#   bash .analysis/22_1665/verify_all.sh --fast   # 只跑快速项（跳过 link_variant）
#
# 前置：
#   · 快速项不需要构建产物，只读源码 / 字库 bin，并用 EIDE 原命令 / 宿主编译复现；
#   · `check_flash_artifact.sh` 需要 build/Debug 里是**当前源码树的 make 构建**、且含 22_1665
#     驱动（EIDE 构建覆写、或最近一次 make 不是 22_1665 口径时该项会失败 —— 先
#     `make PROTO=CQ DISP=22_1665 -j8`）。**注意**：`PROTO=ALL` + 工作区当前宏值 8×4
#     （本模组 46080B）+ 其他消费者 35836B = 81916B > 64KB ⇒ **ALL/22_1665 链接期必然溢出**
#     （预存状态，与断言/注释删除无关）——故本脚本在**没有**合格产物时把该项记为
#     「跳过（前置不满足）」并打印原因，不静默算通过、也不算失败；
#   · `link_variant.sh` 需要一份**最新**的构建日志（`build_round*.log`，由 `make DISP=22_1665`
#      或 `make PROTO=CQ DISP=22_1665` 生成；它从最新日志里抓驱动编译/链接命令）。
#
# 退出码 = 失败项数（0 = 全绿）。
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
cd "$ROOT"

FAST=0
[ "${1:-}" = "--fast" ] && FAST=1

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASS_ITEMS=0
FAIL_ITEMS=0
SKIP_ITEMS=0
LINES=()

_report() { # _report <名称> <ok|bad> <说明>
    if [ "$2" = ok ]; then
        PASS_ITEMS=$((PASS_ITEMS + 1))
        LINES+=("  ✔ $1：$3")
    else
        FAIL_ITEMS=$((FAIL_ITEMS + 1))
        LINES+=("  ✘ $1：$3")
    fi
}

# 前置不满足（不是通过、也不是失败）：显式记账 + 打印原因，避免「静默跳过」
_skip() { # _skip <名称> <说明>
    SKIP_ITEMS=$((SKIP_ITEMS + 1))
    LINES+=("  ⚠ $1：跳过（前置不满足）— $2")
}

# run_count <显示名> <行匹配正则> <命令...>：解析形如「通过 58 / 失败 0」「23 通过 / 0 失败」的收尾行
run_count() {
    local name="$1" re="$2"
    shift 2
    local out="$TMP/out.txt"
    "$@" >"$out" 2>&1
    local line p f
    line="$(grep -E "$re" "$out" | tail -1)"
    if [ -z "$line" ]; then
        _report "$name" bad "未见结果行（原始输出见下）"
        sed 's/^/      | /' "$out" | tail -10
        return
    fi
    read -r p f < <(printf '%s' "$line" | python3 -c '
import re, sys
s = sys.stdin.read()

def pick(pre_pattern, post_pattern, default):
    """先试「关键字后紧跟数字」，再试「数字后紧跟关键字」（两种计数格式都能读）。"""
    m = re.search(pre_pattern, s)
    if m:
        return m.group(1)
    m = re.search(post_pattern, s)
    if m:
        return m.group(1)
    return default

p = pick(r"通过\s*(\d+)", r"(\d+)\s*通过", 0)
f = pick(r"失败\s*(\d+)", r"(\d+)\s*失败", 1)
print(p, f)
')
    if [ "${f:-1}" -ne 0 ]; then
        _report "$name" bad "通过 ${p:-?} / 失败 ${f:-?}（原始输出见下）"
        sed 's/^/      | /' "$out" | tail -10
    else
        _report "$name" ok "通过 $p / 失败 0"
    fi
}

# run_marker <显示名> <成功标记正则> <命令...>：只判「输出里出现该标记」
run_marker() {
    local name="$1" re="$2"
    shift 2
    local out="$TMP/out.txt"
    "$@" >"$out" 2>&1
    if grep -qE "$re" "$out"; then
        _report "$name" ok "输出含成功标记"
    else
        _report "$name" bad "未见成功标记（原始输出见下）"
        sed 's/^/      | /' "$out" | tail -10
    fi
}

echo "== 22-1665 宿主验证汇总 =="
echo "   根目录：$ROOT"
echo

run_count "check_equivalence.py（几何零回归：语义级 1×1 对照 + 机器码锚定删除前归档驱动）" \
    "通过 [0-9]+ / 失败 [0-9]+" python3 "$HERE/check_equivalence.py"

run_count "check_host_differential.py（机器级差分：宿主编译产物 1×1 新=旧 / 多口径 = 规格模型）" \
    "通过 [0-9]+ / 失败 [0-9]+" python3 "$HERE/check_host_differential.py"

run_count "check_driver.py（结构 + 源码卫生 + 无编译期防御；按源码宏值）" \
    "通过 [0-9]+ / 失败 [0-9]+" python3 "$HERE/check_driver.py"

run_count "check_driver.py --geom 1x1（1×1 基线几何自检 + 「口」52 点）" \
    "通过 [0-9]+ / 失败 [0-9]+" python3 "$HERE/check_driver.py" --geom 1x1

run_count "check_driver.py --geom 2x1（扩屏几何自检）" \
    "通过 [0-9]+ / 失败 [0-9]+" python3 "$HERE/check_driver.py" --geom 2x1

run_count "check_driver.py --geom 2x2（四模块几何自检）" \
    "通过 [0-9]+ / 失败 [0-9]+" python3 "$HERE/check_driver.py" --geom 2x2

run_count "check_eide_command.sh（EIDE 原命令复现：合法口径零告警 / 旧非法口径现行行为 + -O3 零回归 A/B）" \
    "通过 [0-9]+ / 失败 [0-9]+" bash "$HERE/check_eide_command.sh"

run_count "check_compile_matrix.sh（编译矩阵：合法编过 / 旧非法口径现行行为 / CCM 越区域链接期兜底）" \
    "通过 [0-9]+ / 失败 [0-9]+" bash "$HERE/check_compile_matrix.sh"

run_count "check_round30_ab.sh（第三十轮零回归：现行 vs 改动前归档，section 逐字节 + 反汇编 0 行差异）" \
    "通过 [0-9]+ / 失败 [0-9]+" bash "$HERE/check_round30_ab.sh"

run_count "check_round32_ab.sh（第三十二轮零回归：CCM 自设上限删除，现行 vs 改动前归档，section 逐字节 + 反汇编 0 行差异）" \
    "通过 [0-9]+ / 失败 [0-9]+" bash "$HERE/check_round32_ab.sh"

run_count "check_round34_ab.sh（第三十四轮零回归：断言/注释删除，现行 vs 改动前归档，section 逐字节 + 反汇编 0 行差异）" \
    "通过 [0-9]+ / 失败 [0-9]+" bash "$HERE/check_round34_ab.sh"

# 烧录物自检的前置：hex 必须是**当前树的 make 构建**且含 22_1665 驱动。
# 不满足时显式跳过（ALL/22_1665 在 8×4 宏值下链接期必然溢出，见文件头说明）。
ART_HEX="$ROOT/build/Debug/Project_STD.hex"
ART_TREE=$( { find "$ROOT"/Application "$ROOT"/Device "$ROOT"/Kernel "$ROOT"/Platform "$ROOT"/Core \
                  "$ROOT"/Compiler -type f \( -name '*.c' -o -name '*.h' -o -name '*.ld' \) -print0 2>/dev/null \
              | LC_ALL=C sort -z | xargs -0 cat; cat "$ROOT"/Makefile; } | md5sum | cut -c1-8 )
art_ok=0
if [ -f "$ART_HEX" ] && arm-none-eabi-objcopy -I ihex -O binary "$ART_HEX" "$TMP/art.bin" 2>/dev/null; then
    if grep -aqF "2200001665" "$TMP/art.bin" && grep -aqF "$ART_TREE" "$TMP/art.bin"; then
        art_ok=1
    fi
fi
if [ "$art_ok" -eq 1 ]; then
    run_count "check_flash_artifact.sh（烧录物内容身份）" \
        "结果：[0-9]+ 通过 / [0-9]+ 失败" bash "$HERE/check_flash_artifact.sh"
else
    _skip "check_flash_artifact.sh（烧录物内容身份）" \
          "build/Debug/Project_STD.hex 不是「当前树（tree=$ART_TREE）+ 22_1665」的 make 产物 —— 当前宏值 8×4（本模组 46080B）下**任何 make 口径都装不下**：PROTO=ALL 溢出 16380B、PROTO=CQ 溢出 14124B（均为预存状态，与断言/注释删除无关；只有 EIDE Debug 子集能装：57484B）。要恢复查此项：把几何减到 8×2（ALL 口径 58876B，实测链接通过）或改用 EIDE 构建并接受它无 tree= 指纹"
fi

run_count "round25_two_modules.py（1×2 面板级取证：链首块归属 / 三种接线模型 / 改动前后逐字节）" \
    "通过 [0-9]+ / 失败 [0-9]+" python3 "$HERE/round25_two_modules.py"

run_count "round26_two_hubs.py（双 HUB 口现象判定：\"1\\n2\" 落点 / 链首块归属 / H1-H2-H3 / 第二组脚未驱动）" \
    "通过 [0-9]+ / 失败 [0-9]+" python3 "$HERE/round26_two_hubs.py"

run_count "round27_wiring_b_and_etc.py（模型 B 取证：ETC 七行现象穷举 / 两组 8 脚驱动 / 现场预测表）" \
    "通过 [0-9]+ / 失败 [0-9]+" python3 "$HERE/round27_wiring_b_and_etc.py"

if [ "$FAST" -eq 0 ]; then
    # 第二十九轮：`_22_1665_HUB_WIRING` / `_22_1665_BLUE_AS_LIT` / `_22_1665_DATA_ACTIVE_HIGH`
    # 三开关已删除 → 其换口径变体（wiringA1x2 / wiringA11x1 / blue0 / invpol）一并退休；
    # 历史口径的取证锚定 `.analysis/22_1665/archive/finalize/` 归档驱动（见 README）。
    #
    # **第三十轮**：四个几何宏改成兄弟驱动同款**普通 `#define`**（不再有 `#ifndef` 守卫）
    # ⇒ 命令行 `-D_22_1665_MODULE_*=…` 只会得到「重定义告警 + 文件值胜出」（静默测错口径）。
    # 故几何一律写成 `宏名=值`（link_variant.sh 交给 macro_override.py 做**源码替换**生成临时
    # 副本再编译）；仍带 `#ifndef` 守卫的 `_22_1665_CHAIN_HEAD_IS_MODULE0` 继续用 `-D` 传。
    # **第三十二轮**：驱动自设 16KB CCM 预算删除（改为 ≤ 整片 CCMRAM 64KB 物理守卫）
    # ⇒ 新增 `mod8x2`（128×32 现场几何）换口径重链。
    # **第三十四轮**：每个变体**两个模块轴全部显式钉死**（原先只钉一轴，会随工作区宏值漂移：
    # 例如工作区 COLS=4 时 `mod2x1` 实际变成 2×4 —— 本模组 CCM 从 3072 变 12288，
    # 且 `geom32x8` 会变成 M=32 = 46080B 而链接期溢出）。变体语义 = 名字所示几何，
    # 不随「工作区当前宏值」漂移。
    for v in "geom32x8 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=1 _22_1665_MODULE_PIXEL_ROW=32 _22_1665_MODULE_PIXEL_COL=8" \
             "mod2x1 _22_1665_MODULE_ROWS=2 _22_1665_MODULE_COLS=1" \
             "mod2x2 _22_1665_MODULE_ROWS=2 _22_1665_MODULE_COLS=2" \
             "mod8x2 _22_1665_MODULE_ROWS=8 _22_1665_MODULE_COLS=2" \
             "flip1x2 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=2 -D_22_1665_CHAIN_HEAD_IS_MODULE0=1" \
             "cols5 _22_1665_MODULE_ROWS=1 _22_1665_MODULE_COLS=5"; do
        # shellcheck disable=SC2086
        set -- $v
        vn="$1"; shift
        run_marker "link_variant.sh $vn（换口径编译 + 链接）" \
            "\[ok\] 链接通过" bash "$HERE/link_variant.sh" "$vn" "$@"
    done
fi

echo
echo "-------- 汇总 --------"
for l in "${LINES[@]}"; do echo "$l"; done
echo
echo "合计：$PASS_ITEMS 项全绿 / $FAIL_ITEMS 项失败 / $SKIP_ITEMS 项跳过（前置不满足）"
[ "$FAIL_ITEMS" -eq 0 ] && echo "⇒ 全部通过" || echo "⇒ 有失败项，请先看上方对应输出"
exit "$FAIL_ITEMS"
