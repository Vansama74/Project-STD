#!/usr/bin/env python3
"""批 1a（YN_OL）doc 指针可解析性检查。

从被改源码中抽取 `doc/...` 路径引用与同行/同块的 `§x.y` 章节号，逐条核对：
  ① 目标文件存在；
  ② 若给出章节号，目标文件中存在同名章节（兼容 `## 8.` / `## §8` / `### 7.1` / `### §11.1`）。
用法：python3 .analysis/comment_pass_20261008/scripts/check_doc_pointers.py
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
FILES = [
    "Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h",
    "Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.h",
    "Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto_parse.h",
    "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c",
    "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c",
    "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_parse.c",
]

DOC_PATH = re.compile(r"doc/[\w\u4e00-\u9fff/\-\.]+?\.md")
SEC = re.compile(r"§\s*(\d+(?:\.\d+)*)")


def sections(text: str) -> set[str]:
    """文件中出现的章节号集合（## 8. / ## §8 / ### 11.1 ...）。"""
    out = set()
    for line in text.splitlines():
        m = re.match(r"^#{2,4}\s+§?(\d+(?:\.\d+)*)", line.strip())
        if m:
            out.add(m.group(1))
    return out


def main() -> int:
    total = ok = 0
    fail = 0
    for rel in FILES:
        path = ROOT / rel
        if not path.exists():
            print(f"  ✘ 源文件缺失：{rel}")
            return 1
        lines = path.read_text(encoding="utf-8").splitlines()
        for i, line in enumerate(lines, 1):
            m = DOC_PATH.search(line)
            if not m:
                continue
            target = ROOT / m.group(0)
            total += 1
            if not target.exists():
                print(f"  ✘ {rel}:{i}  目标文档不存在：{m.group(0)}")
                fail += 1
                continue
            # 章节号：先看本行，再回看本行所在注释块的前 4 行（指针可能写在块末行）
            ctx = " ".join(lines[max(0, i - 5):i])
            secs = SEC.findall(ctx)
            if not secs:
                print(f"  ✔ {rel}:{i}  {m.group(0)}（未给章节号）")
                ok += 1
                continue
            have = sections(target.read_text(encoding="utf-8"))
            missing = [s for s in secs if s not in have and s.split(".")[0] not in have]
            if missing:
                print(f"  ✘ {rel}:{i}  {m.group(0)} 缺章节 §{'、§'.join(missing)}")
                fail += 1
            else:
                print(f"  ✔ {rel}:{i}  {m.group(0)} §{'、§'.join(secs)}")
                ok += 1
    print(f"######## doc 指针检查：{ok}/{total} 通过，失败 {fail}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
