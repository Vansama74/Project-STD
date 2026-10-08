#!/usr/bin/env bash
# refine_round1「仅注释变化」复核：剥离注释后 token 流必须逐字节一致
# （改前 = archive/refine_round1（提交 cd9ce318 = git HEAD），改后 = 工作区）。
# 方法：arm-none-eabi-gcc -fpreprocessed -dD -E -P（预处理模式：去注释、保留宏与指令），
#       再把所有空白折叠为单空格后比较。
# 用法：bash .analysis/comment_pass_20261008/scripts/refine_round1/code_only_diff.sh
set -u
cd "$(dirname "$0")/../../../.." || exit 1

ARCH=.analysis/comment_pass_20261008/archive/refine_round1
OUT=.analysis/comment_pass_20261008/ab/refine_round1/token
mkdir -p "$OUT"

FILES="
Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h
Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.h
Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto_parse.h
Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c
Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c
Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_parse.c
Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h
Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.h
Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.h
Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c
Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c
Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_default.c
Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.c
Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h
Application/Src/ProtocolParser_Myanmar/app_mm_proto.c
Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c
Application/Src/app_factory_test_mm.c
"

strip_comments() { # <file> -> stdout（去注释、折叠空白）
  arm-none-eabi-gcc -fpreprocessed -dD -E -P "$1" 2>/dev/null | tr -s '[:space:]' ' '
}

fail=0
n=0
for spec in $FILES; do
  base=$(echo "$spec" | tr '/' '_')
  strip_comments "$ARCH/$spec" > "$OUT/$base.pre.token"
  strip_comments "$spec"       > "$OUT/$base.cur.token"
  n=$((n + 1))
  if cmp -s "$OUT/$base.pre.token" "$OUT/$base.cur.token"; then
    printf '  ✔ %-64s 一致（%s 字节）\n' "$spec" "$(wc -c < "$OUT/$base.cur.token")"
  else
    printf '  ✘ %-64s token 流有差异：\n' "$spec"
    diff <(fold -w200 "$OUT/$base.pre.token" | tr ' ' '\n') \
         <(fold -w200 "$OUT/$base.cur.token" | tr ' ' '\n') | head -20 | sed 's/^/      /'
    fail=1
  fi
done
echo "######## 结果（$n 文件）：$([ $fail -eq 0 ] && echo '仅注释变化（代码 token 零改动）' || echo '存在代码差异，需复核')"
exit $fail
