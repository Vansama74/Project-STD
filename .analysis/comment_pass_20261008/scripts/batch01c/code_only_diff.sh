#!/usr/bin/env bash
# 批 1c「仅注释变化」复核：剥离注释后 token 流必须逐字节一致（改前归档 vs 工作区现行）。
# 方法：arm-none-eabi-gcc -fpreprocessed -dD -E -P（预处理模式：去注释、保留宏与指令），
#       再把所有空白折叠为单空格后比较（消除因注释行增删导致的换行/缩进差异）。
# 用法：bash .analysis/comment_pass_20261008/scripts/batch01c/code_only_diff.sh
set -u
cd "$(dirname "$0")/../../../.." || exit 1

ARCH=.analysis/comment_pass_20261008/archive/batch01c
OUT=.analysis/comment_pass_20261008/ab/batch01c
mkdir -p "$OUT"

strip_comments() { # <file> -> stdout（去注释、折叠空白）
  arm-none-eabi-gcc -fpreprocessed -dD -E -P "$1" 2>/dev/null | tr -s '[:space:]' ' '
}

fail=0
for spec in "Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h" \
            "Application/Src/ProtocolParser_Myanmar/app_mm_proto.c" \
            "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c" \
            "Application/Src/app_factory_test_mm.c"; do
  base=$(basename "$spec")
  strip_comments "$ARCH/$spec" > "$OUT/$base.pre.token"
  strip_comments "$spec"       > "$OUT/$base.cur.token"
  if cmp -s "$OUT/$base.pre.token" "$OUT/$base.cur.token"; then
    printf '  ✔ %-26s 去注释 token 流逐字节一致（%s 字节）\n' "$base" "$(wc -c < "$OUT/$base.cur.token")"
  else
    printf '  ✘ %-26s token 流有差异：\n' "$base"
    diff <(fold -w200 "$OUT/$base.pre.token" | tr ' ' '\n') \
         <(fold -w200 "$OUT/$base.cur.token" | tr ' ' '\n') | head -20 | sed 's/^/      /'
    fail=1
  fi
done
echo "######## 结果：$([ $fail -eq 0 ] && echo '仅注释变化（代码 token 零改动）' || echo '存在代码差异，需复核')"
exit $fail
