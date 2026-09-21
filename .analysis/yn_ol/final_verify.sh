#!/usr/bin/env bash
# 云南治超裁决轮最终构建验证：三口径 clean 全量构建 + 段尺寸 + md5 + 告警统计
set -u
cd /home/yystation/Program/3833024/Project-STD-main
OUT=.analysis/yn_ol/final_verify.out.txt
: > "$OUT"

run_one() {
    local tag="$1"; shift
    echo "===== [$tag] make $* =====" | tee -a "$OUT"
    make clean >/dev/null 2>&1
    make -j8 "$@" > ".analysis/yn_ol/final_${tag}.log" 2>&1
    local rc=$?
    echo "build exit=$rc" | tee -a "$OUT"
    local w
    w=$(grep -c "warning:" ".analysis/yn_ol/final_${tag}.log" || true)
    echo "warn_total=$w" | tee -a "$OUT"
    grep "warning:" ".analysis/yn_ol/final_${tag}.log" | sed 's/^/  W: /' | tee -a "$OUT"
    arm-none-eabi-size -A build/Debug/Project_STD.elf | grep -E "\.text|\.rodata|\.data|\.ccmram|\.bss|_user_heap_stack|Total" | tee -a "$OUT"
    md5sum build/Debug/Project_STD.elf build/Debug/Project_STD.hex build/Debug/Project_STD.bin | tee -a "$OUT"
    # SRAM 用量 = data + bss + _user_heap_stack 等（取 size 全量非 ccmram/flash 段）
    echo | tee -a "$OUT"
}

run_one all_1263
run_one cq_1263 PROTO=CQ
run_one all_221665 DISP=22_1665
echo "===== DONE =====" | tee -a "$OUT"
