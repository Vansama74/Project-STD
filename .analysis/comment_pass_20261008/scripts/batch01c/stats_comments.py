#!/usr/bin/env python3
"""批 1c（缅甸 MM）注释工程规模统计：总行数 / 注释行数 / 文件头行数。

判定（有状态）：
  · 注释行 = `//` 行、以及 `/* ... */` 块内的每一行（含 `/**`、续行与 `*/` 行）；
    行尾注释所在的代码行**不计**为注释行。
  · 文件头行数 = 文件起始首个 `/** ... */` 块（被首个非注释代码行截断）。
用法：
    python3 .analysis/comment_pass_20261008/scripts/batch01c/stats_comments.py pre|cur
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
BATCH = ROOT / ".analysis/comment_pass_20261008"
FILES = [
    "Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h",
    "Application/Src/ProtocolParser_Myanmar/app_mm_proto.c",
    "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c",
    "Application/Src/app_factory_test_mm.c",
]


def count(path: Path) -> tuple[int, int, int]:
    """返回 (总行数, 纯注释行数, 文件头行数)。"""
    lines = path.read_text(encoding="utf-8").split("\n")
    if lines and lines[-1] == "":
        lines = lines[:-1]  # 尾换行不算一行

    in_block = False
    comment_only = 0
    header_lines = 0
    header_seen = False     # 是否已经历过文件头的起止（只认文件起始的那一块）
    header_done = False
    started = False         # 是否已遇到首个非空行

    for ln in lines:
        stripped = ln.strip()
        if not started:
            if not stripped:
                continue
            started = True
            if stripped.startswith("/**"):
                header_seen = True

        is_comment = False
        if in_block:
            is_comment = True
            if "*/" in ln:
                in_block = False
            if header_seen and not header_done:
                header_lines += 1
                if "*/" in ln:
                    header_done = True
        else:
            if stripped.startswith("/*"):
                is_comment = True
                in_block = "*/" not in ln
                if header_seen and not header_done:
                    header_lines += 1
                    if "*/" in ln:
                        header_done = True
            elif stripped.startswith("//"):
                is_comment = True
            if header_seen and not header_done and not is_comment:
                header_done = True  # 首个代码行截断（防御）
        if is_comment:
            comment_only += 1
    return len(lines), comment_only, header_lines


def main() -> int:
    mode = sys.argv[1] if len(sys.argv) > 1 else "cur"
    base = BATCH / "archive/batch01c" if mode == "pre" else ROOT
    print(f"== 口径：{mode}（{'归档 = git HEAD' if mode == 'pre' else '工作区现行'}）==")
    t_lines = t_com = 0
    for rel in FILES:
        n, c, h = count(base / rel)
        t_lines += n
        t_com += c
        print(f"  {rel:<62} 总行 {n:>4} / 注释行 {c:>4} / 文件头 {h:>3}")
    print(f"  {'合计':<62} 总行 {t_lines:>4} / 注释行 {t_com:>4}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
