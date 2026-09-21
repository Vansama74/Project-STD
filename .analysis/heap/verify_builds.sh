#!/usr/bin/env bash
# 三口径全量重编 + 段尺寸/告警/静态栈归属采集（2026-09-17 堆修复验证）
#
# 顺序：CQ → 22_1665 → ALL/1_263（默认口径放在最后，使工作区最终留下的
# build/Debug/Project_STD.elf 是默认口径，便于直接烧录/调试）。
#
# 证据固定：
#   ① 每口径 exit code + warning 计数（应仅 3 条既有 HAL unused-parameter，0 error）
#   ② arm-none-eabi-size -A 段尺寸（含 .ccmram / .bss / ._user_heap_stack）
#   ③ 静态任务栈归属：nm 查 s_task_*_stack / s_idle_task_stack / s_timer_task_stack
#      必须落在 CCMRAM（0x1000_0000 段），ucHeap 必须仍在 SRAM（0x2000_0000 段）
set -u
cd "$(dirname "$0")/../.." || exit 1
ROOT=$(pwd)
OUT="$ROOT/.analysis/heap/verify"
mkdir -p "$OUT"

run() {
  local name="$1"; shift
  echo "############ $name ############"
  make clean > /dev/null 2>&1
  make -j8 "$@" > "$OUT/$name.log" 2>&1
  local rc=$?
  echo "exit=$rc  errors=$(grep -c 'error:' "$OUT/$name.log")  warnings=$(grep -c 'warning:' "$OUT/$name.log")"
  echo "--- warning 明细 ---"
  grep "warning:" "$OUT/$name.log" | sed 's/^.*\(warning:.*\)/\1/' | sort | uniq -c | sort -rn | head -6
  arm-none-eabi-size -A build/Debug/Project_STD.elf > "$OUT/$name.size.txt" 2>&1
  echo "--- 段尺寸 ---"
  grep -E "^\.(text|rodata|data|bss|ccmram|_user_heap_stack)" "$OUT/$name.size.txt"
  echo "--- 口径指纹 ---"
  grep -E "^(PROTO|DISP|CONFIG|TOOLCHAIN)=" build/Debug/.build_stamp
  echo "--- md5 ---"
  md5sum build/Debug/Project_STD.elf
  echo
}

nm_evidence() {
  echo "############ 静态栈 / ucHeap 归属（nm，当前 build/Debug/Project_STD.elf）############"
  arm-none-eabi-nm -n -S build/Debug/Project_STD.elf | grep -E "s_task_.*_(stack|tcb)|s_idle_task_(stack|tcb)|s_timer_task_(stack|tcb)|ucHeap" | sed 's/^/  /'
  echo "--- 其中落在 CCMRAM(0x1000xxxx) 的静态栈数量 ---"
  arm-none-eabi-nm -n build/Debug/Project_STD.elf | grep -E "s_task_.*_stack|s_idle_task_stack|s_timer_task_stack" | grep -c "^1000"
  echo "--- ucHeap 地址（必须 2000xxxx = SRAM）---"
  arm-none-eabi-nm -n -S build/Debug/Project_STD.elf | grep -w ucHeap | sed 's/^/  /'
}

run cq_1263 PROTO=CQ
run all_221665 DISP=22_1665
run all_1263
nm_evidence
echo "############ done ############"
