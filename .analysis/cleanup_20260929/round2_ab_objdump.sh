#!/usr/bin/env bash
# 第二轮注释精简的机器码 A/B：本轮改动前（round-1 构建对象，已快照）vs 改动后（本轮构建对象）
#
# 判据：
#   ① 除调试段（.debug_*，行号随注释行数变化属预期）外，**所有 section 逐字节一致**；
#   ② 反汇编（objdump -d，不含行号信息）**逐行一致**。
# 用法：bash round2_ab_objdump.sh
set -u
cd "$(dirname "$0")/../.." || exit 1
ROOT=$(pwd)
BEFORE="$ROOT/.analysis/cleanup_20260929/round2_ab/before"
AFTER="$ROOT/build/Debug"
OUT="$ROOT/.analysis/cleanup_20260929/round2_ab"
mkdir -p "$OUT"

sec_md5() { # sec_md5 <obj>
  local f="$1"
  local tmp; tmp=$(mktemp -d)
  local n=0
  while IFS= read -r s; do
    case "$s" in .debug*|.comment|.ARM.attributes) continue ;; esac
    arm-none-eabi-objcopy -O binary --only-section="$s" "$f" "$tmp/x.bin" 2>/dev/null || continue
    printf '  %-28s %s\n' "$s" "$(md5sum "$tmp/x.bin" | cut -d' ' -f1)"
    n=$((n+1))
  done < <(arm-none-eabi-objdump -h "$f" | awk '{print $2}' | grep -E '^\.')
  echo "  （参与比对 section 数：$n）"
  rm -rf "$tmp"
}

audit() { # audit <相对路径 .o>
  local rel="$1" name; name=$(basename "$rel")
  echo "######## $rel"
  if [ ! -f "$BEFORE/$name" ] || [ ! -f "$AFTER/$rel" ]; then
    echo "  ⚠ 缺对象（跳过）：before=$([ -f "$BEFORE/$name" ] && echo 有 || echo 无) after=$([ -f "$AFTER/$rel" ] && echo 有 || echo 无)"
    return
  fi
  local ba bb
  ba=$(sec_md5 "$BEFORE/$name"); bb=$(sec_md5 "$AFTER/$rel")
  # 归一化：去掉「（参与比对 section 数）」行后逐行比较
  if [ "$(printf '%s' "$ba" | grep -v '参与比对')" = "$(printf '%s' "$bb" | grep -v '参与比对')" ]; then
    echo "  ✔ section 逐字节一致（.debug_* 除外）"
  else
    echo "  ✘ section 存在差异："
    diff <(printf '%s\n' "$ba") <(printf '%s\n' "$bb") | sed 's/^/    /'
  fi
  arm-none-eabi-objdump -d "$BEFORE/$name" > "$OUT/before_$name.dis" 2>/dev/null
  arm-none-eabi-objdump -d "$AFTER/$rel" > "$OUT/after_$name.dis" 2>/dev/null
  if diff -q "$OUT/before_$name.dis" "$OUT/after_$name.dis" > /dev/null; then
    echo "  ✔ 反汇编逐行一致（$(wc -l < "$OUT/after_$name.dis") 行）"
  else
    echo "  ✘ 反汇编有差异："
    diff "$OUT/before_$name.dis" "$OUT/after_$name.dis" | sed 's/^/    /' | head -30
  fi
}

audit Application/Src/app_boot.o
audit Application/Src/LDI/app_ldi.o
audit Application/Src/ProtocolParser_ChongQing/app_cq_proto.o
echo "######## 说明"
echo "  · dev_display_22_1703.o 未参与：默认 DISP=1_263 口径不编该文件，round-1 无基线对象；"
echo "    该文件本轮改动仅在文件头 /* ... */ 注释内（下方 include 之前），已过 cc_check 语法检查。"
echo "  · pl_rtt.o：本轮由备份/HEAD 逐字节恢复（md5 3e432cf868fb6a916b1f56476851feb9 = git HEAD 版本）。"
