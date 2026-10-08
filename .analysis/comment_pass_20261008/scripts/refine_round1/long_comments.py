#!/usr/bin/env python3
"""列出版本范围内「长注释行」（> 100 字符）便于人工判断是否还需精简。

用法：python3 .analysis/comment_pass_20261008/scripts/refine_round1/long_comments.py [--code]
      --code：只列代码行超长（用于确认代码行未被动过）
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
FILES = [
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

only_code = "--code" in sys.argv
n = 0
for rel in FILES:
    for i, ln in enumerate((ROOT / rel).read_text(encoding="utf-8").splitlines(), 1):
        s = ln.strip()
        if only_code and (s.startswith("*") or s.startswith("/*") or s.startswith("//")):
            continue
        if len(ln) > 100:
            n += 1
            print(f"{rel}:{i} ({len(ln)}) {s[:130]}")
print(f"######## 长行合计 {n}")
