#!/usr/bin/env python3
"""refine_round2 契约核查（收紧口径）：

① 主头文件「契约（改错静默致现场故障）」块条数与每条字数（本轮期望：YN_OL 4 / GZ_OL 5 / MM 7，
   每条 ≤30 字、文件头 ≤10 行）；
② **契约引用一致性**：模块内所有 `契约 N`（含 `.c` 文件头「本文件实现 …」）N 必须落在条数范围内，
   且不得出现已删除编号（如 YN_OL ⑤ / GZ_OL ⑥ / MM ⑧）；
③ 契约行**不带日期**（`20\\d\\d`）与**不带理由引导词**（"因为/理由/已裁决/用户裁决"）。

用法：python3 .analysis/comment_pass_20261008/scripts/refine_round2/check_contracts.py
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
MODULES = {
    "YN_OL": {
        "hdr": "Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h",
        "files": [
            "Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h",
            "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c",
            "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c",
            "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_parse.c",
        ],
        "expect": 4,
        "deleted": ["⑤"],
    },
    "GZ_OL": {
        "hdr": "Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h",
        "files": [
            "Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h",
            "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c",
            "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c",
            "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.c",
            "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_default.c",
        ],
        "expect": 5,
        "deleted": ["⑥"],
    },
    "MM": {
        "hdr": "Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h",
        "files": [
            "Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h",
            "Application/Src/ProtocolParser_Myanmar/app_mm_proto.c",
            "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c",
            "Application/Src/app_factory_test_mm.c",
        ],
        "expect": 7,
        "deleted": ["⑧"],
    },
}
MARK = "①②③④⑤⑥⑦⑧⑨"
LIMIT = 30
CN = {c: i + 1 for i, c in enumerate(MARK)}


def contract_block(text: str) -> list[str]:
    lines = text.splitlines()
    blk: list[str] = []
    if lines and lines[0].strip().startswith("/**"):
        for ln in lines:
            blk.append(ln)
            if ln.strip().endswith("*/"):
                break
    return blk


def main() -> int:
    fail = 0
    for tag, cfg in MODULES.items():
        blk = contract_block((ROOT / cfg["hdr"]).read_text(encoding="utf-8"))
        mark = "契约（改错静默致现场故障）"
        if mark not in "\n".join(blk):
            print(f"  ✘ {tag}: {cfg['hdr']} 文件头未找到「{mark}」块")
            fail = 1
            continue
        body = "\n".join(blk).split(mark, 1)[1]
        body = body.split("详见", 1)[0]
        clean = []
        for ln in body.splitlines():
            s = ln.strip().lstrip("*").strip()
            if s:
                clean.append(s)
        items = [p.strip().strip("；;。：:").strip()
                 for p in re.split(f"[{MARK}]", " ".join(clean))
                 if p.strip().strip("；;。：:")]
        print(f"== {tag}（{cfg['hdr']}）契约 {len(items)} 条（期望 {cfg['expect']}）")
        if len(items) != cfg["expect"]:
            print(f"  ✘ 条数 {len(items)} ≠ 期望 {cfg['expect']}")
            fail = 1
        for i, it in enumerate(items, 1):
            problems = []
            if len(it) > LIMIT:
                problems.append(f"超 {LIMIT} 字")
            if re.search(r"20\d\d", it):
                problems.append("含日期")
            if re.search(r"理由|已裁决|用户裁决|因为", it):
                problems.append("含理由/裁决")
            flag = ("  ✘ " + "、".join(problems)) if problems else ""
            print(f"   {i}. [{len(it):2d} 字] {it}{flag}")
            if problems:
                fail = 1
        hdr = len(blk)
        print(f"   （文件头 {hdr} 行）")
        if hdr > 10:
            print("  ✘ 文件头 > 10 行")
            fail = 1

        # 契约引用一致性
        allow = set(range(1, len(items) + 1))
        for rel in cfg["files"]:
            for ln_no, ln in enumerate((ROOT / rel).read_text(encoding="utf-8").splitlines(), 1):
                if "契约" not in ln:
                    continue
                for ch in re.findall(f"[{MARK}]", ln):
                    n = CN[ch]
                    if n not in allow:
                        print(f"  ✘ {rel}:{ln_no} 引用已删除/越界编号 契约 {ch}")
                        fail = 1
        # 已删除编号不得残留（正文任何位置）
        for rel in cfg["files"]:
            txt = (ROOT / rel).read_text(encoding="utf-8")
            for ch in cfg["deleted"]:
                if f"契约 {ch}" in txt:
                    print(f"  ✘ {rel} 残留已删除编号 契约 {ch}")
                    fail = 1
    print("######## 契约核查：" + ("全部符合（条数/字数/无日期理由/引用一致）" if fail == 0 else "存在不符合项"))
    return fail


if __name__ == "__main__":
    sys.exit(main())
