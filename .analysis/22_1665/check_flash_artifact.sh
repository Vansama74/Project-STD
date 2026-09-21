#!/usr/bin/env bash
# 22-1665 烧录物自检（第二十三轮重写：核对重构后驱动的内容身份）
#
# 背景：`build/Debug` 是 make 与 EIDE 共用的输出目录，曾实测到 **elf 与 hex 来自两次不同
#       构建**（后者覆盖前者）——只对时间戳不足以判断「将要烧的 hex 是哪一份」。
#       本脚本核对 hex 的**内容身份**（驱动串 / 链名 blob / 去 RTT 证据 / 当前源码树 tree=）。
#
# 用法：bash .analysis/22_1665/check_flash_artifact.sh [hex 路径]
#       默认 build/Debug/Project_STD.hex
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HEX="${1:-$ROOT/build/Debug/Project_STD.hex}"
BIN="$(mktemp /tmp/1665_artifact.XXXX.bin)"
trap 'rm -f "$BIN"' EXIT

[ -f "$HEX" ] || { echo "✘ 找不到 $HEX"; exit 1; }

echo "== 22-1665 烧录物自检 =="
echo "  hex : $HEX"
echo "  md5 : $(md5sum "$HEX" | cut -d' ' -f1)"

arm-none-eabi-objcopy -I ihex -O binary "$HEX" "$BIN" || { echo "✘ hex→bin 失败"; exit 1; }

# 当前源码树的树哈希（与 Makefile 的 DIAG_TREE_HASH 同一算法）
TREE=$( { find "$ROOT"/Application "$ROOT"/Device "$ROOT"/Kernel "$ROOT"/Platform "$ROOT"/Core "$ROOT"/Compiler \
            -type f \( -name '*.c' -o -name '*.h' -o -name '*.ld' \) -print0 2>/dev/null \
          | LC_ALL=C sort -z | xargs -0 cat; cat "$ROOT"/Makefile; } | md5sum | cut -c1-8 )

pass=0; fail=0
chk() { # chk <描述> <条件 0/1>
    if [ "$2" -eq 0 ]; then echo "  ✔ $1"; pass=$((pass+1)); else echo "  ✘ $1"; fail=$((fail+1)); fi
}
has() { grep -aqF "$1" "$BIN"; }

has "2200001665" && rc=0 || rc=1
chk "含 22-1665 驱动（字符串 2200001665）" $rc

# 新链表名 blob：rodata 里 "R1\0 G1\0 R2\0 G2\0" 相邻出现（重构后形态的身份证，
# 对齐填充不敏感：只要求四个名字在同一 20B 窗口内按序出现）
python3 - "$BIN" <<'PY' && rc=0 || rc=1
import pathlib, sys
b = pathlib.Path(sys.argv[1]).read_bytes()
i, ok = b.find(b"R1\x00"), False
while i >= 0:
    win = b[i:i + 20]
    if b"G1\x00" in win and b"R2\x00" in win and b"G2\x00" in win:
        ok = True
        break
    i = b.find(b"R1\x00", i + 1)
sys.exit(0 if ok else 1)
PY
chk "含重构后链表名 blob（R1/G1/R2/G2 相邻）" $rc

# 第二十七轮：数据线表升级为「组 × 4 行」——组 1 名 blob（R3/G3/R4/G4）也必须相邻出现
python3 - "$BIN" <<'PY' && rc=0 || rc=1
import pathlib, sys
b = pathlib.Path(sys.argv[1]).read_bytes()
i, ok = b.find(b"R3\x00"), False
while i >= 0:
    win = b[i:i + 20]
    if b"G3\x00" in win and b"R4\x00" in win and b"G4\x00" in win:
        ok = True
        break
    i = b.find(b"R3\x00", i + 1)
sys.exit(0 if ok else 1)
PY
chk "含模型 B 组 1 链表名 blob（R3/G3/R4/G4 相邻；= 每列一口独立线的 as-built 证据）" $rc

# 去 RTT 证据：重构前的 [22_1665] 打印串必须不再出现在镜像里
has "[22_1665]" && rc=1 || rc=0
chk "不含旧驱动 RTT 串 '[22_1665]'（重构已去 RTT）" $rc
has "probe8 dwell" && rc=1 || rc=0
chk "不含探针串 'probe8 dwell'（探针设施已删除）" $rc
has "pin self-test" && rc=1 || rc=0
chk "不含上电回读自检串 'pin self-test'（已删除）" $rc

has "$TREE" && rc=0 || rc=1
chk "是当前源码树的 make 构建（内嵌 tree=$TREE）" $rc

echo
echo "  结果：$pass 通过 / $fail 失败   （tree=$TREE）"
[ "$fail" -eq 0 ] && echo "  ⇒ 这份 hex 可以烧（仍建议烧后看 RTT 横幅 fw/built/tree 复核；EIDE 构建后须 make clean && make）" \
                 || echo "  ⇒ **不要烧**：先 make clean && make -j8 DISP=22_1665 再重跑本脚本"
exit "$fail"
