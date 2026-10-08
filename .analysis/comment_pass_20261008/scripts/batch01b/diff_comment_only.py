#!/usr/bin/env python3
"""批 1b（GZ_OL）`git diff` 复核：每条 +/- 行去除注释（含跨行块注释状态）后必须为空。

判据：对每个被改文件解析 `git diff -U0` 的行号，取「改前（git HEAD）」与「改后（工作区）」
两个版本，分别用**有状态**注释剥离器逐行求「代码残余」——被 diff 标记的行其代码残余必须为空
（= 该行是注释行、块注释续行或空行）。代码 token 零改动另由 code_only_diff.sh 独立证明。

用法：python3 .analysis/comment_pass_20261008/scripts/batch01b/diff_comment_only.py
"""
from __future__ import annotations

import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
FILES = [
    "Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h",
    "Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.h",
    "Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.h",
    "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c",
    "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c",
    "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.c",
    "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_default.c",
]
HUNK = re.compile(r"^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@")


def code_residue(lines: list[str]) -> list[str]:
    """逐行剥离注释（跨行状态），返回每行剩余的「代码」内容（strip 后）。"""
    out: list[str] = []
    in_block = False
    for ln in lines:
        buf = []
        i = 0
        while i < len(ln):
            if in_block:
                end = ln.find("*/", i)
                if end < 0:
                    i = len(ln)
                else:
                    in_block = False
                    i = end + 2
            else:
                b = ln.find("/*", i)
                s = ln.find("//", i)
                if s >= 0 and (b < 0 or s < b):
                    break  # 行注释吞掉剩余
                if b < 0:
                    buf.append(ln[i:])
                    break
                buf.append(ln[i:b])
                in_block = True
                i = b + 2
        out.append("".join(buf).strip())
    return out


def head_lines(rel: str) -> list[str]:
    txt = subprocess.run(["git", "show", f"HEAD:{rel}"], cwd=ROOT,
                         capture_output=True, text=True, check=True).stdout
    return txt.split("\n")


def main() -> int:
    fail = 0
    plus_total = minus_total = tail_total = 0
    for rel in FILES:
        cur = (ROOT / rel).read_text(encoding="utf-8").split("\n")
        old = head_lines(rel)
        cur_res = code_residue(cur)
        old_res = code_residue(old)
        cur_set, old_set = set(cur_res), set(old_res)
        if Counter(r for r in cur_res if r) != Counter(r for r in old_res if r):
            print(f"  ✘ {rel}  非空代码残余多重集不一致（代码文本有增删）")
            fail = 1
        diff = subprocess.run(["git", "diff", "-U0", "--", rel], cwd=ROOT,
                              capture_output=True, text=True, check=True).stdout
        plus = minus = tail = 0
        new_no = old_no = 0
        for ln in diff.splitlines():
            m = HUNK.match(ln)
            if m:
                old_no, new_no = int(m.group(1)), int(m.group(2))
                continue
            if ln.startswith("+++") or ln.startswith("---"):
                continue
            if ln.startswith("+"):
                plus += 1
                res = cur_res[new_no - 1]
                if res:
                    if res in old_set:  # 行尾注释改写：代码文本在改前版本中存在
                        tail += 1
                    else:
                        print(f"  ✘ {rel}:{new_no}(+) 新增/改动代码：{res[:100]}")
                        fail = 1
                new_no += 1
            elif ln.startswith("-"):
                minus += 1
                res = old_res[old_no - 1]
                if res:
                    if res in cur_set:
                        tail += 1
                    else:
                        print(f"  ✘ {rel}:{old_no}(-) 删除/改动代码：{res[:100]}")
                        fail = 1
                old_no += 1
            elif ln.startswith(" "):
                new_no += 1
                old_no += 1
        plus_total += plus
        minus_total += minus
        tail_total += tail
        note = f"，其中行尾注释改写 {tail} 行（代码部分未动，token 流另有独立证明）" if tail else ""
        print(f"  ✔ {rel}  +{plus} / -{minus}（注释/块注释续行/空行{note}）")
    print(f"######## 结果：+{plus_total} / -{minus_total}（行尾注释改写 {tail_total} 行），"
          f"{'仅注释变化 ✔' if fail == 0 else '存在代码行变化 ✘'}")
    return fail


if __name__ == "__main__":
    sys.exit(main())
