#!/bin/bash
# 四条口径构建 + 段尺寸/告警采集（贵州治超专用 UDP 业务口，2026-09-14）
set -u
cd /home/yystation/Program/3833024/Project-STD-main || exit 1
OUT=.analysis/9k23881580/build_sweep.log
: > "$OUT"

run() {
  local tag="$1"; shift
  echo "===== [$tag] make $* =====" | tee -a "$OUT"
  make clean >/dev/null 2>&1
  # shellcheck disable=SC2068
  make -j8 "$@" >".analysis/9k23881580/build_${tag}.log" 2>&1
  local rc=$?
  echo "[$tag] exit=$rc" | tee -a "$OUT"
  grep -cE "warning:|error:" ".analysis/9k23881580/build_${tag}.log" | sed "s/^/[$tag] warning+error lines: /" | tee -a "$OUT"
  grep -E "warning:|error:" ".analysis/9k23881580/build_${tag}.log" | sort -u | head -20 | tee -a "$OUT"
  arm-none-eabi-size -A build/Debug/Project_STD.elf | grep -E "^\.(text|rodata|data|bss|ccmram|_user_heap_stack)" | tee -a "$OUT"
  md5sum build/Debug/Project_STD.elf | tee -a "$OUT"
  echo | tee -a "$OUT"
}

run default
run cq        PROTO=CQ
run d22       DISP=22_1703
run d22cq     DISP=22_1703 PROTO=CQ
run default2

echo "===== sweep done =====" | tee -a "$OUT"
