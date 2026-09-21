#!/usr/bin/env bash
# 22-1665 宿主「换口径重链」验证（不参与固件构建、不动 .build_stamp）
#
# 用途：在不改源码默认值、不切口径指纹的前提下，验证某个**几何 / 开关口径**能否
#       「编译 + 链接通过」并打印驱动对象的本模组 `.ccmram`。
#
#   **第三十轮起：几何宏 = 兄弟驱动同款普通 `#define`** ⇒ 命令行 `-D` 不再生效
#   （重定义告警 + **文件值胜出**）。故本脚本的几何一律写成 `宏名=值`，由
#   `macro_override.py` 做**源码替换**生成临时副本再编译；仍带 `#ifndef` 守卫的
#   `_22_1665_CHAIN_HEAD_IS_MODULE0` 继续用 `-D` 传。**给几何宏传 `-D` 会被直接拒绝**
#   （防「静默测错口径」）。
#   例：`bash .analysis/22_1665/link_variant.sh mod2x1 _22_1665_MODULE_ROWS=2`、
#       `bash .analysis/22_1665/link_variant.sh geom32x8 _22_1665_MODULE_PIXEL_ROW=32 \
#              _22_1665_MODULE_PIXEL_COL=8`、
#       `bash .analysis/22_1665/link_variant.sh flip1x2 _22_1665_MODULE_COLS=2 \
#              -D_22_1665_CHAIN_HEAD_IS_MODULE0=1`。
#
#   第二十九轮：`_22_1665_HUB_WIRING`（接线模型）/ `_22_1665_BLUE_AS_LIT`（蓝分量）/
#   `_22_1665_DATA_ACTIVE_HIGH`（数据极性）三开关已随生产化收口删除 → 对应变体
#   （wiringA*/blue0/invpol）退休；需要它们的历史口径取证请锚定
#   `.analysis/22_1665/archive/finalize/driver_pre_finalize_20260917.c`。
#
# 做法：用最近一次 make 的编译/链接命令（从最新的 build_roundNN.log 里抓），只把驱动那份 .o
#       （几何变体时连源码一起换成替换副本）换掉，输出到 /tmp（不覆盖 build/Debug 产物）。
#
# 用法：bash .analysis/22_1665/link_variant.sh <输出名> [宏名=值 ...] [-D宏名=值 ...]
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
# 取「最新一份含驱动编译命令 + 链接命令的构建日志」（不要求最近一次 make 是 22_1665 口径）
# shellcheck source=/dev/null
. "$HERE/pick_log.sh"
LOG="$(pick_log_1665 --need-link)" || { echo "缺少含 22_1665 驱动命令的构建日志（先跑一次 make）"; exit 1; }

NAME="$1"; shift
OUT="/tmp/22_1665_${NAME}.elf"
SRC_REL="Device/Display/dev_display_22_1665.c"

SRC_OVERRIDES=()
EXTRA_DEFS=()
for arg in "$@"; do
    case "$arg" in
        -D_22_1665_MODULE_ROWS=* | -D_22_1665_MODULE_COLS=* | \
        -D_22_1665_MODULE_PIXEL_ROW=* | -D_22_1665_MODULE_PIXEL_COL=*)
            echo "拒绝：几何宏已是普通 #define（兄弟驱动同款），-D 会被文件值静默覆盖；" >&2
            echo "      请改用「宏名=值」参数，本脚本会做源码替换。" >&2
            exit 2
            ;;
        -D*) EXTRA_DEFS+=("$arg") ;;
        *=*) SRC_OVERRIDES+=("$arg") ;;
        *) echo "无法识别的参数：$arg（几何用「宏名=值」，其余开关用「-D宏名=值」）" >&2; exit 2 ;;
    esac
done

cd "$ROOT"
CC_SRC="$SRC_REL"
if [ "${#SRC_OVERRIDES[@]}" -gt 0 ]; then
    CC_SRC="/tmp/22_1665_${NAME}_src.c"
    python3 "$HERE/macro_override.py" "$SRC_REL" "$CC_SRC" "${SRC_OVERRIDES[@]}"
    echo "[ok] 几何源码替换（$(printf '%s ' "${SRC_OVERRIDES[@]}")）→ $CC_SRC"
fi

CC_LINE=$(grep -m1 -- '-c -o build/Debug/Device/Display/dev_display_22_1665.o' "$LOG")
LD_LINE=$(grep -m1 -- '-o build/Debug/Project_STD.elf' "$LOG")

OBJ="/tmp/22_1665_${NAME}_drv.o"
CC_NEW=$(printf '%s\n' "$CC_LINE" | sed -e "s#-o build/Debug/Device/Display/dev_display_22_1665.o#-o $OBJ#" \
                                          -e "s#$SRC_REL#$CC_SRC#" -e 's/^arm-none-eabi-gcc //')
# shellcheck disable=SC2086
arm-none-eabi-gcc $CC_NEW "${EXTRA_DEFS[@]+"${EXTRA_DEFS[@]}"}"
echo "[ok] 驱动对象已编译（几何源码替换：${SRC_OVERRIDES[*]:-无}；额外定义：${EXTRA_DEFS[*]:-无}）"
echo -n "    驱动对象本模组 CCMRAM = "
arm-none-eabi-size -A "$OBJ" | awk '$1==".ccmram"{print $2 " B"}'

LD_NEW=$(printf '%s\n' "$LD_LINE" | sed -e "s#-o build/Debug/Project_STD.elf#-o $OUT#" \
                                          -e "s#build/Debug/Device/Display/dev_display_22_1665.o#$OBJ#" \
                                          -e 's/^arm-none-eabi-gcc //')
# shellcheck disable=SC2086
arm-none-eabi-gcc $LD_NEW
echo "[ok] 链接通过 → $OUT"
arm-none-eabi-size -A "$OUT" | grep -E '^\.(text|rodata|data|ccmram|bss|_user_heap_stack)' | sed 's/^/    /'
