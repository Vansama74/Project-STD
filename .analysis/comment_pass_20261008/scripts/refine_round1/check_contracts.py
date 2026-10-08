#!/usr/bin/env python3
"""契约行（收紧口径）核查：提取三模块主头文件「契约（改错静默致现场故障）」块，
按 ①~⑨ 拆分逐条给出字数与条数，判定 4~8 条、每条 ≤30 字。

用法：python3 .analysis/comment_pass_20261008/scripts/refine_round1/check_contracts.py
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
MODULES = {
    "YN_OL": "Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h",
    "GZ_OL": "Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h",
    "MM": "Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h",
}
MARK = "①②③④⑤⑥⑦⑧⑨"
LIMIT = 30


def contract_block(text: str) -> str:
    lines = text.splitlines()
    # 取文件头块（第一个 /** ... */）
    blk: list[str] = []
    if lines and lines[0].strip().startswith("/**"):
        for ln in lines:
            blk.append(ln)
            if ln.strip().endswith("*/"):
                break
    return "\n".join(blk)


def main() -> int:
    fail = 0
    for tag, rel in MODULES.items():
        blk = contract_block((ROOT / rel).read_text(encoding="utf-8"))
        if "契约（改错静默致现场故障）" not in blk:
            print(f"  ✘ {tag}: {rel} 文件头未找到「契约（改错静默致现场故障）」块")
            fail = 1
            continue
        body = blk.split("契约（改错静默致现场故障）", 1)[1]
        body = body.split("详见", 1)[0]
        # 去掉注释行前缀（` * `）与空行，再按 ①~⑨ 拆分、去掉尾部标点
        clean = []
        for ln in body.splitlines():
            s = ln.strip()
            if s.startswith("*"):
                s = s[1:].strip()
            if s:
                clean.append(s)
        body = " ".join(clean)
        parts = re.split(f"[{MARK}]", body)
        items = [p.strip().strip("；;。：:").strip() for p in parts if p.strip().strip("；;。：:")]
        print(f"== {tag}（{rel}）契约 {len(items)} 条")
        for i, it in enumerate(items, 1):
            over = "  ✘ 超 30 字" if len(it) > LIMIT else ""
            print(f"   {i}. [{len(it):2d} 字] {it}{over}")
            if len(it) > LIMIT:
                fail = 1
        if not (4 <= len(items) <= 8):
            print(f"  ✘ 条数 {len(items)} 不在 4~8 范围")
            fail = 1
        hdr = len(blk.splitlines())
        if hdr > 10:
            print(f"  ✘ 文件头 {hdr} 行 > 10")
            fail = 1
        print(f"   （文件头 {hdr} 行）")
    print(f"######## 契约核查：{'全部符合（4~8 条 / 每条 ≤30 字 / 文件头 ≤10 行）' if fail == 0 else '存在不符合项'}")
    return fail


if __name__ == "__main__":
    sys.exit(main())
