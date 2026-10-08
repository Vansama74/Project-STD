#!/usr/bin/env bash
# 代码瘦身轮（2026-09-29）构建取证助手
# 用法: build_and_record.sh <tag> <make 参数...>
# 结果写入 .analysis/cleanup_20260929/sizes/<tag>.txt（段尺寸 + elf md5 + 告警计数）
set -u
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
TAG="$1"; shift
OUT_DIR="$ROOT/.analysis/cleanup_20260929/sizes"
mkdir -p "$OUT_DIR"
LOG="$OUT_DIR/${TAG}.build.log"
{
  echo "== tag: $TAG =="
  echo "== cmd: make $* =="
  echo "== date: $(date -Iseconds) =="
} > "$OUT_DIR/${TAG}.txt"
make "$@" > "$LOG" 2>&1
RC=$?
echo "make_rc=$RC" >> "$OUT_DIR/${TAG}.txt"
# 告警统计（排除既有 3 条 HAL 之后的新增告警）
grep -c "warning:" "$LOG" | sed 's/^/warning_lines=/' >> "$OUT_DIR/${TAG}.txt"
grep -h "warning:" "$LOG" | sed 's/^/  W: /' | sort -u >> "$OUT_DIR/${TAG}.txt"
ELF=$(find "$ROOT/build" -maxdepth 2 -name Project_STD.elf -newermt '-10 minutes' | head -1)
if [ -n "$ELF" ]; then
  echo "elf=$ELF" >> "$OUT_DIR/${TAG}.txt"
  md5sum "$ELF" | awk '{print "elf_md5="$1}' >> "$OUT_DIR/${TAG}.txt"
  arm-none-eabi-size -A "$ELF" >> "$OUT_DIR/${TAG}.txt"
fi
tail -3 "$LOG" >> "$OUT_DIR/${TAG}.txt"
exit $RC
