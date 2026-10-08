#!/usr/bin/env python3
"""refine_round1（契约行收敛）`git diff` 复核：每条 +/- 行去除注释（含跨行块注释状态）
后必须为空 —— 即被 diff 标记的行只能是注释行 / 块注释续行 / 空行。

判据：对每个被改文件解析 `git diff -U0` 的行号，取「改前（git HEAD = cd9ce318）」与
「改后（工作区）」两个版本，分别用有状态注释剥离器逐行求「代码残余」——被 diff 标记的行
其代码残余必须为空。代码 token 零改动另由 code_only_diff.sh 独立证明。

用法：python3 .analysis/comment_pass_20261008/scripts/refine_round1/diff_comment_only.py
"""
from __future__ import annotations

import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
FILES = [
    "Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h",
    "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c",
    "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c",
    "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_parse.c",
    "Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h",
    "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c",
    "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c",
    "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_default.c",
    "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.c",
    "Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h",
    "Application/Src/ProtocolParser_Myanmar/app_mm_proto.c",
    "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c",
    "Application/Src/app_factory_test_mm.c",
    "doc/18_注释规范.md",
    "doc/14_贵州治超屏协议/README.md",
    "doc/15_云南治超屏协议/README.md",
    "doc/16_缅甸2D费显协议/README.md",
]
HUNK = re.compile(r"^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@", re.M)
# 本脚本用于源码侧（注释行判定）；doc 侧的 +/- 行天然不是代码，单独列出不计入失败。


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
    return txt.splitlines()


def work_lines(rel: str) -> list[str]:
    return (ROOT / rel).read_text(encoding="utf-8").splitlines()


def diff_line_numbers(diff: str) -> tuple[list[int], list[int]]:
    """返回 (旧文件被删行号, 新文件被加行号)，1 起算。"""
    minus: list[int] = []
    plus: list[int] = []
    oi = ni = None
    for ln in diff.splitlines():
        if ln.startswith("@@"):
            m = HUNK.match(ln)
            if m:
                oi, ni = int(m.group(1)), int(m.group(2))
            continue
        if oi is None:
            continue
        if ln.startswith("---") or ln.startswith("+++"):
            continue
        if ln.startswith("-"):
            minus.append(oi)
            oi += 1
        elif ln.startswith("+"):
            plus.append(ni)
            ni += 1
        elif ln.startswith(" "):
            oi += 1
            ni += 1
    return minus, plus


def main() -> int:
    fail = 0
    for rel in FILES:
        try:
            pre = head_lines(rel)
        except subprocess.CalledProcessError:
            print(f"  ⚠ {rel} 不在 HEAD 中（跳过）")
            continue
        cur = work_lines(rel)
        pre_code = code_residue(pre)
        cur_code = code_residue(cur)
        diff = subprocess.run(["git", "diff", "-U0", "--", rel], cwd=ROOT,
                              capture_output=True, text=True, check=True).stdout
        minus, plus = diff_line_numbers(diff)
        bad = []
        for i in sorted(minus):
            if 1 <= i <= len(pre_code) and pre_code[i - 1]:
                bad.append(("-", i, pre_code[i - 1]))
        for i in sorted(plus):
            if 1 <= i <= len(cur_code) and cur_code[i - 1]:
                bad.append(("+", i, cur_code[i - 1]))
        is_doc = rel.startswith("doc/")
        if not minus and not plus:
            print(f"  – {rel} 无改动")
            continue
        if bad:
            if is_doc:
                print(f"  · {rel} doc 文件：+{len(plus)} / -{len(minus)}（doc 文本，不按代码行判）")
                continue
            # 行尾注释改写：被 diff 行「剥离行尾注释后」的代码残余在删/加两侧应为同一多重集
            # （如 `code; /* 旧 */` → `code; /* 新 */`）。代码 token 零改动由 code_only_diff.sh 独立证明。
            pre_c = Counter(r for s, _, r in bad if s == "-")
            cur_c = Counter(r for s, _, r in bad if s == "+")
            if pre_c == cur_c:
                print(f"  ✔ {rel} 仅注释变化（其中行尾注释改写 {sum(cur_c.values())} 行，"
                      f"代码残余多重集一致；token 流另有独立证明）")
                continue
            print(f"  ✘ {rel} 存在非注释 diff 行：")
            for sign, i, txt in bad[:10]:
                print(f"      {sign}{i}: {txt[:100]}")
            only_pre = pre_c - cur_c
            only_cur = cur_c - pre_c
            if only_pre or only_cur:
                print(f"      代码残余仅删侧 {dict(only_pre)} / 仅加侧 {dict(only_cur)}")
            fail = 1
        else:
            print(f"  ✔ {rel} +{len(plus)} / -{len(minus)}（仅注释/块注释续行/空行）")
    print(f"######## 结果：{'仅注释变化 ✔' if fail == 0 else '存在非注释改动，需复核'}")
    return fail


if __name__ == "__main__":
    sys.exit(main())
