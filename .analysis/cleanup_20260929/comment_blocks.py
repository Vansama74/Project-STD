#!/usr/bin/env python3
"""列出源文件里的「长注释块」（连续注释行 ≥ N），供第二轮注释精简定点定位。

用法：
    python3 .analysis/cleanup_20260929/comment_blocks.py <file> [min_lines]

判据：与上一轮精简口径一致——只关心**成块**的说明性注释（`/* ... */` 与连续 `//`），
行内短注释（跟在代码后的 why 注释）不算「长块」，不在此列。
"""
from __future__ import annotations

import re
import sys
from pathlib import Path


def blocks(lines: list[str], min_lines: int) -> list[tuple[int, int, int, str]]:
    out = []
    i, n = 0, len(lines)
    while i < n:
        s = lines[i].lstrip()
        # /* ... */ 块
        if s.startswith("/*"):
            j = i
            while j < n and "*/" not in lines[j]:
                j += 1
            end = min(j, n - 1)
            text = "".join(lines[i:end + 1])
            chars = len(re.sub(r"[ \t/*]", "", text))
            if (end - i + 1) >= min_lines:
                out.append((i + 1, end + 1, chars, text.splitlines()[0][:70]))
            i = end + 1
            continue
        # 连续 // 行
        if s.startswith("//"):
            j = i
            while j + 1 < n and lines[j + 1].lstrip().startswith("//"):
                j += 1
            text = "".join(lines[i:j + 1])
            chars = len(re.sub(r"[ \t/]", "", text))
            if (j - i + 1) >= min_lines:
                out.append((i + 1, j + 1, chars, text.splitlines()[0][:70]))
            i = j + 1
            continue
        i += 1
    return out


def main() -> int:
    path = Path(sys.argv[1])
    min_lines = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    bs = blocks(lines, min_lines)
    total = 0
    for a, b, chars, head in bs:
        total += chars
        print(f"  L{a:>5}-{b:<5} {b - a + 1:>3} 行 {chars:>5} 字符  {head}")
    print(f"[{path}] ≥{min_lines} 行的注释块 {len(bs)} 个，合计 {total} 字符（文件共 {len(lines)} 行）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
