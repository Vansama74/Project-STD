#!/usr/bin/env bash
# 注释精简 A/B：pre（重建的改动前源码）vs cur（现行源码），同 flags 编译
set -u
cd "$(dirname "$0")/../.." || exit 1
REC=".analysis/cleanup_20260929/round2_ab/reconstruct"
fail=0
sec_md5() {
  local f="$1" tmp; tmp=$(mktemp -d)
  while IFS= read -r s; do
    case "$s" in .debug*|.comment|.ARM.attributes) continue ;; esac
    arm-none-eabi-objcopy -O binary --only-section="$s" "$f" "$tmp/x.bin" 2>/dev/null || continue
    printf '  %-28s %s\n' "$s" "$(md5sum "$tmp/x.bin" | cut -d' ' -f1)"
  done < <(arm-none-eabi-objdump -h "$f" | awk '{print $2}' | grep -E '^\.')
  rm -rf "$tmp"
}
for base in app_boot.c app_ldi.c app_cq_proto.c dev_display_22_1703.c; do
  pre="$REC/$base.pre.o"; cur="$REC/$base.cur.o"
  echo "######## $base"
  if diff <(sec_md5 "$pre") <(sec_md5 "$cur") > /dev/null; then
    echo "  ✔ section 逐字节一致（.debug_* 除外；共 $(arm-none-eabi-objdump -h "$cur" | awk '{print $2}' | grep -cE '^\.(text|rodata|data|ccmram|bss)') 个代码/数据段数）"
  else
    echo "  ✘ section 差异："; diff <(sec_md5 "$pre") <(sec_md5 "$cur") | sed 's/^/    /'; fail=1
  fi
  arm-none-eabi-objdump -d "$pre" | grep -v "file format" > "$REC/$base.pre.dis"
  arm-none-eabi-objdump -d "$cur" | grep -v "file format" > "$REC/$base.cur.dis"
  if diff -q "$REC/$base.pre.dis" "$REC/$base.cur.dis" > /dev/null; then
    echo "  ✔ 反汇编逐行一致（$(wc -l < "$REC/$base.cur.dis") 行）"
  else
    echo "  ✘ 反汇编差异："; diff "$REC/$base.pre.dis" "$REC/$base.cur.dis" | sed 's/^/    /' | head -20; fail=1
  fi
done
echo "######## 结果：$([ $fail -eq 0 ] && echo '全部一致（注释精简零机器码影响）' || echo '存在差异，需人工复核')"
exit $fail
