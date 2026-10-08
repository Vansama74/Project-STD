#!/usr/bin/env python3
"""注释/代码规模统计（代码瘦身轮 2026-09-29）

用法: stats.py <输出文件> [路径...]
统计每个文件的：总行数 / 空行 / 纯注释行 / 代码行 / 字节数 / 注释字符数。
注释剥离器处理字符串与字符字面量，避免把 "//" 之类误判为注释。
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def strip_comments(src: str):
    """返回 (去掉注释的代码, 注释文本)。字符串/字符字面量内的 // /* 不误判。"""
    out, cmt = [], []
    i, n = 0, len(src)
    state = None  # None | 'str' | 'chr' | 'line' | 'block'
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if state is None:
            if c == "/" and nxt == "/":
                state = "line"
                i += 2
                continue
            if c == "/" and nxt == "*":
                state = "block"
                i += 2
                continue
            if c == '"':
                state = "str"
            elif c == "'":
                state = "chr"
            out.append(c)
            i += 1
        elif state == "str":
            out.append(c)
            if c == "\\":
                out.append(nxt)
                i += 2
                continue
            if c == '"':
                state = None
            i += 1
        elif state == "chr":
            out.append(c)
            if c == "\\":
                out.append(nxt)
                i += 2
                continue
            if c == "'":
                state = None
            i += 1
        elif state == "line":
            if c == "\n":
                state = None
                out.append(c)
            else:
                cmt.append(c)
            i += 1
        else:  # block
            if c == "*" and nxt == "/":
                state = None
                i += 2
                continue
            cmt.append(c)
            if c == "\n":
                out.append("\n")  # 保持行号对齐
            i += 1
    return "".join(out), "".join(cmt)


def stats(path: Path):
    src = path.read_text(encoding="utf-8", errors="replace")
    code, cmt = strip_comments(src)
    raw_lines = src.split("\n")
    code_lines_all = code.split("\n")
    n = max(len(raw_lines), len(code_lines_all))
    blank = comment_only = code_lines = 0
    for i in range(n):
        r = raw_lines[i] if i < len(raw_lines) else ""
        c = code_lines_all[i] if i < len(code_lines_all) else ""
        if not r.strip():
            blank += 1
        elif not c.strip():
            comment_only += 1
        else:
            code_lines += 1
    return dict(lines=n, blank=blank, comment_lines=comment_only,
                code_lines=code_lines, bytes=len(src.encode()), comment_chars=len(cmt))


def main():
    out_path = Path(sys.argv[1])
    paths = sys.argv[2:]
    rows = []
    for p in sorted(paths):
        f = (ROOT / p) if not Path(p).is_absolute() else Path(p)
        if not f.is_file():
            continue
        s = stats(f)
        rows.append((p, s))
    tot = dict(lines=0, blank=0, comment_lines=0, code_lines=0, bytes=0, comment_chars=0)
    with out_path.open("w", encoding="utf-8") as fh:
        fh.write(f"{'file':<70} {'lines':>7} {'blank':>6} {'cmtline':>8} {'codeln':>7} {'bytes':>8} {'cmtchr':>8}\n")
        for p, s in rows:
            for k in tot:
                tot[k] += s[k]
            fh.write(f"{p:<70} {s['lines']:>7} {s['blank']:>6} {s['comment_lines']:>8} "
                     f"{s['code_lines']:>7} {s['bytes']:>8} {s['comment_chars']:>8}\n")
        fh.write(f"{'== TOTAL (' + str(len(rows)) + ' files)':<70} {tot['lines']:>7} {tot['blank']:>6} "
                 f"{tot['comment_lines']:>8} {tot['code_lines']:>7} {tot['bytes']:>8} {tot['comment_chars']:>8}\n")
    print(f"[stats] {out_path}（{len(rows)} 文件）合计: 行 {tot['lines']} / 纯注释行 {tot['comment_lines']} "
          f"/ 代码行 {tot['code_lines']} / 注释字符 {tot['comment_chars']}")


if __name__ == "__main__":
    main()
