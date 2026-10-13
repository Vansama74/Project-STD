#!/usr/bin/env python3
"""refine_round2 doc 指针可解析性检查（两段）：

A. **源码 → doc**（三模块 17 文件，与 refine_round1 同口径）：抽取 `doc/...md` 路径与同块 `§x.y`，
   核对目标文件存在 + 章节存在（`## 8.` / `### 7.1` / `#### 13.5.1` 兼容）。
B. **doc → doc**（doc/14、doc/15、doc/16 正文内的跨文档指针）：同法核对；**排除修订记录**
   （`## N. 修订记录` 之后的段落按 doc/18 §2 第 5 条「历史条目例外」保留当时写法，不检查）。

用法：python3 .analysis/comment_pass_20261008/scripts/refine_round2/check_doc_pointers.py
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SRC_FILES = [
    "Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h",
    "Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.h",
    "Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto_parse.h",
    "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c",
    "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c",
    "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_parse.c",
    "Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h",
    "Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.h",
    "Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.h",
    "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c",
    "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c",
    "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_default.c",
    "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.c",
    "Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h",
    "Application/Src/ProtocolParser_Myanmar/app_mm_proto.c",
    "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c",
    "Application/Src/app_factory_test_mm.c",
]
DOC_FILES = [
    "doc/14_贵州治超屏协议/README.md",
    "doc/15_云南治超屏协议/README.md",
    "doc/16_缅甸2D费显协议/README.md",
]
DOC_PATH = re.compile(r"doc/[\w\u4e00-\u9fff/\-\.]+?\.md")
SEC = re.compile(r"§\s*(\d+(?:\.\d+)*)")


def sections(text: str) -> set[str]:
    out = set()
    for line in text.splitlines():
        m = re.match(r"^#{2,4}\s+§?(\d+(?:\.\d+)*)", line.strip())
        if m:
            out.add(m.group(1))
    return out


def check(rel: str, lines: list[str], start: int, end: int, label: str) -> tuple[int, int, int]:
    total = ok = fail = 0
    end = min(end, len(lines))
    for i in range(start, end):
        line = lines[i]
        m = DOC_PATH.search(line)
        if not m:
            continue
        target = ROOT / m.group(0)
        total += 1
        if not target.exists():
            print(f"  ✘ {rel}:{i + 1}  目标文档不存在：{m.group(0)}")
            fail += 1
            continue
        # 章节号只认「路径之后、同一行内」的 §x.y（避免把同一行里其它自引/其它指针的 § 误算进来）
        ctx = line[m.end():]
        secs = SEC.findall(ctx)
        if not secs:
            print(f"  ✔ {rel}:{i + 1}  {m.group(0)}（未给章节号）")
            ok += 1
            continue
        have = sections(target.read_text(encoding="utf-8"))
        missing = [s for s in secs if s not in have and s.split(".")[0] not in have]
        if missing:
            print(f"  ✘ {rel}:{i + 1}  {m.group(0)} 缺章节 §{'、§'.join(missing)}")
            fail += 1
        else:
            print(f"  ✔ {rel}:{i + 1}  {m.group(0)} §{'、§'.join(secs)}")
            ok += 1
    print(f"---- {label}：{ok}/{total} 通过，失败 {fail}")
    return ok, total, fail


def main() -> int:
    print("== A. 源码 → doc")
    ok = total = fail = 0
    for rel in SRC_FILES:
        path = ROOT / rel
        if not path.exists():
            print(f"  ✘ 源文件缺失：{rel}")
            fail += 1
            continue
        a, b, c = check(rel, path.read_text(encoding="utf-8").splitlines(), 0, 10**9, rel)
        ok += a
        total += b
        fail += c

    print("== B. doc → doc（排除修订记录段）")
    for rel in DOC_FILES:
        path = ROOT / rel
        lines = path.read_text(encoding="utf-8").splitlines()
        end = len(lines)
        for idx, ln in enumerate(lines):
            if re.match(r"^##+\s+\d+\.\s*修订记录", ln.strip()):
                end = idx
                break
        a, b, c = check(rel, lines, 0, end, f"{rel}（正文 1~{end} 行）")
        ok += a
        total += b
        fail += c

    print(f"######## doc 指针检查：{ok}/{total} 通过，失败 {fail}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
