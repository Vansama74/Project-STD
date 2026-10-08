#!/usr/bin/env bash
# 第二轮四口径全量构建 + 段尺寸/md5/告警采集（2026-09-29 修正轮）
#
# 口径：
#   ① ALL/1_263/STD（默认）      ② ALL/1_263/FACTORY=MM
#   ③ PROTO=CQ/1_263             ④ 无显示等价口径（emitted Makefile，产物落 .analysis/）
# 每口径全量重编（make clean 后 make），产物固定不在 build/Debug 之间混淆：
#   ①②③ 落 build/Debug（同一目录，逐次覆盖，采集在各自之后立即进行）
#   ④ 落 .analysis/myanmar/build_nodisp/EIDEMyanmarNoDisp
set -u
cd "$(dirname "$0")/../.." || exit 1
ROOT=$(pwd)
OUT="$ROOT/.analysis/cleanup_20260929/round2_build"
mkdir -p "$OUT"

sizes() { arm-none-eabi-size -A "$1" | grep -E "^\.(text|rodata|data|bss|ccmram|_user_heap_stack)"; }

run() { # run <tag> <elf> <make-args...>
  local tag="$1" elf="$2"; shift 2
  echo "############ $tag ############"
  make clean > /dev/null 2>&1
  make -j8 "$@" > "$OUT/$tag.log" 2>&1
  local rc=$?
  local errs warns
  errs=$(grep -c "error:" "$OUT/$tag.log")
  warns=$(grep -c "warning:" "$OUT/$tag.log")
  echo "exit=$rc  errors=$errs  warnings=$warns"
  grep "warning:" "$OUT/$tag.log" | sed 's/^.*\(warning:.*\)/\1/' | sort | uniq -c | sort -rn
  echo "--- 段尺寸（$elf）---"
  sizes "$ROOT/$elf" | tee "$OUT/$tag.size.txt"
  echo "--- elf md5 ---"
  md5sum "$ROOT/$elf" | tee "$OUT/$tag.md5.txt"
  echo
}

run all_1263_std build/Debug/Project_STD.elf
run all_1263_mm build/Debug/Project_STD.elf FACTORY=MM
run cq_1263 build/Debug/Project_STD.elf PROTO=CQ
run nodisp .analysis/myanmar/build_nodisp/EIDEMyanmarNoDisp/Project_STD.elf \
    -f .analysis/myanmar/build_nodisp/Makefile.mm_nodisp CONFIG=EIDEMyanmarNoDisp

echo "############ 收尾：恢复默认口径产物到 build/Debug（便于直接烧录/调试）############"
make clean > /dev/null 2>&1
make -j8 > "$OUT/final_all_1263_std.log" 2>&1
echo "exit=$?  errors=$(grep -c 'error:' "$OUT/final_all_1263_std.log")  warnings=$(grep -c 'warning:' "$OUT/final_all_1263_std.log")"
sizes "$ROOT/build/Debug/Project_STD.elf"
md5sum "$ROOT/build/Debug/Project_STD.elf"
echo "############ done ############"
