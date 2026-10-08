#!/usr/bin/env python3
"""C/C++ 注释存量统计（只读工具）。

统计指定目录下 .c/.h 文件：
  - 总行数 / 注释行数 / 代码行数 / 空行数
  - 注释行占比（注释行 / 非空行，以及 注释行/总行数）
  - 最长连续注释块（连续注释行数）
  - 文件头注释块行数（文件起始第一段注释）
  - 注释块总数

识别规则（简化 C 状态机，够用于本工程）：
  - // 行注释
  - /* ... */ 块注释（跨行，按“注释行”计：某一行只要出现注释内容即计入）
  - 字符串/字符字面量中的 // 与 /* 不计为注释
  - 反斜杠续行的物理行单独计数（与 wc -l 对齐）

用法：
  python3 comment_stats.py --root <dir> --dirs Application Device Platform Kernel Core Compiler [--top 20] [--json out.json]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import dataclass, field

EXTS = (".c", ".h")


@dataclass
class FileStat:
    path: str
    total: int = 0
    code: int = 0
    comment: int = 0
    blank: int = 0
    mixed: int = 0  # 行内既有代码又有注释的行（已计入 code，同时计入 comment）
    max_block: int = 0
    blocks: int = 0
    header_block: int = 0


def analyze(path: str, relpath: str) -> FileStat:
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()  # 尾随换行的空串不单独计行

    st = FileStat(path=relpath)
    st.total = len(lines)

    in_block = False       # 是否在 /* */ 块注释中
    block_run = 0          # 当前连续注释行计数
    in_string = False      # 双引号字符串
    in_char = False        # 单引号字符
    code_seen = False      # 是否已出现首个代码行（此前所有注释行计入文件头）
    header_run = 0         # 文件头注释行累计

    for line in lines:
        has_code = False
        has_comment = False
        i = 0
        n = len(line)
        while i < n:
            c = line[i]
            nxt = line[i + 1] if i + 1 < n else ""
            if in_block:
                has_comment = True
                if c == "*" and nxt == "/":
                    in_block = False
                    i += 2
                    continue
                i += 1
                continue
            if in_string:
                if c == "\\":
                    i += 2
                    continue
                if c == '"':
                    in_string = False
                i += 1
                continue
            if in_char:
                if c == "\\":
                    i += 2
                    continue
                if c == "'":
                    in_char = False
                i += 1
                continue
            if c == "/" and nxt == "/":
                has_comment = True
                break
            if c == "/" and nxt == "*":
                in_block = True
                has_comment = True
                i += 2
                continue
            if c == '"':
                in_string = True
                has_code = True
                i += 1
                continue
            if c == "'":
                in_char = True
                has_code = True
                i += 1
                continue
            if not c.isspace():
                has_code = True
            i += 1

        if has_comment and has_code:
            st.code += 1
            st.comment += 1
            st.mixed += 1
        elif has_comment:
            st.comment += 1
        elif has_code:
            st.code += 1
        else:
            st.blank += 1

        if has_comment:
            if block_run == 0:
                st.blocks += 1
            block_run += 1
            st.max_block = max(st.max_block, block_run)
            if not code_seen:
                header_run += 1
                st.header_block = header_run
        else:
            block_run = 0
            if has_code:
                code_seen = True

    return st


def collect(root: str, dirs: list[str]) -> list[FileStat]:
    stats: list[FileStat] = []
    for d in dirs:
        base = os.path.join(root, d)
        if not os.path.isdir(base):
            continue
        for dirpath, _dirnames, filenames in os.walk(base):
            for fn in sorted(filenames):
                if not fn.endswith(EXTS):
                    continue
                full = os.path.join(dirpath, fn)
                rel = os.path.relpath(full, root)
                stats.append(analyze(full, rel))
    return stats


def fmt_row(st: FileStat) -> str:
    nonblank = st.total - st.blank
    pct = (st.comment / nonblank * 100) if nonblank else 0.0
    return f"| {st.path} | {st.total} | {st.code} | {st.comment} | {pct:.1f}% | {st.max_block} |"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--dirs", nargs="+", default=["Application", "Device", "Platform", "Kernel", "Core", "Compiler"])
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument("--json")
    ap.add_argument("--label", default="")
    args = ap.parse_args()

    stats = collect(args.root, args.dirs)
    if not stats:
        print("no files found", file=sys.stderr)
        return 1

    tot = sum(s.total for s in stats)
    code = sum(s.code for s in stats)
    comment = sum(s.comment for s in stats)
    blank = sum(s.blank for s in stats)
    mixed = sum(s.mixed for s in stats)
    nonblank = tot - blank

    label = args.label or args.root
    print(f"# 注释统计: {label}")
    print()
    print(f"- 文件数: {len(stats)}")
    print(f"- 总行数: {tot}")
    print(f"- 代码行(含行尾注释): {code}")
    print(f"- 注释行: {comment} (其中行内混排行 {mixed})")
    print(f"- 空行: {blank}")
    print(f"- 注释行/非空行: {comment / nonblank * 100 if nonblank else 0:.1f}%")
    print(f"- 注释行/总行: {comment / tot * 100 if tot else 0:.1f}%")
    print()

    print(f"## Top {args.top} 注释行数最多的文件")
    print()
    print("| 文件 | 总行 | 代码 | 注释 | 注释/非空 | 最长注释块 |")
    print("|---|---|---|---|---|---|")
    for st in sorted(stats, key=lambda s: -s.comment)[: args.top]:
        print(fmt_row(st))
    print()

    hdr = sorted(stats, key=lambda s: -s.header_block)[:15]
    print("## Top 15 文件头注释块最大")
    print()
    print("| 文件 | 头块行数 | 总行 | 注释行 | 注释/非空 |")
    print("|---|---|---|---|---|")
    for st in hdr:
        nonblank_f = st.total - st.blank
        pct = (st.comment / nonblank_f * 100) if nonblank_f else 0.0
        print(f"| {st.path} | {st.header_block} | {st.total} | {st.comment} | {pct:.1f}% |")
    print()

    blk = sorted(stats, key=lambda s: -s.max_block)[:15]
    print("## Top 15 最长连续注释块")
    print()
    print("| 文件 | 最长块行数 | 总行 | 注释行 |")
    print("|---|---|---|---|")
    for st in blk:
        print(f"| {st.path} | {st.max_block} | {st.total} | {st.comment} |")
    print()

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(
                {
                    "label": label,
                    "files": len(stats),
                    "total": tot,
                    "code": code,
                    "comment": comment,
                    "blank": blank,
                    "mixed": mixed,
                    "comment_pct_nonblank": round(comment / nonblank * 100, 2) if nonblank else 0,
                    "comment_pct_total": round(comment / tot * 100, 2) if tot else 0,
                    "per_file": [
                        {
                            "path": s.path,
                            "total": s.total,
                            "code": s.code,
                            "comment": s.comment,
                            "blank": s.blank,
                            "mixed": s.mixed,
                            "max_block": s.max_block,
                            "header_block": s.header_block,
                            "blocks": s.blocks,
                        }
                        for s in stats
                    ],
                },
                f,
                ensure_ascii=False,
                indent=1,
            )
        print(f"[json] -> {args.json}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
