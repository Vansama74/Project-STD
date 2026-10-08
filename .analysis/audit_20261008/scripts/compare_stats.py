#!/usr/bin/env python3
"""同名文件注释对比：本地工作区 vs phtty 分支。

用法:
  python3 compare_stats.py local.json phtty.json [--top 30]
"""
from __future__ import annotations

import argparse
import json


def load(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    return {e["path"]: e for e in data["per_file"]}


def pct(e: dict) -> float:
    nonblank = e["total"] - e["blank"]
    return (e["comment"] / nonblank * 100) if nonblank else 0.0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("local")
    ap.add_argument("phtty")
    ap.add_argument("--top", type=int, default=30)
    args = ap.parse_args()

    loc = load(args.local)
    pht = load(args.phtty)
    common = sorted(set(loc) & set(pht))
    only_local = sorted(set(loc) - set(pht))
    only_phtty = sorted(set(pht) - set(loc))

    print(f"共同文件: {len(common)} | 仅本地: {len(only_local)} | 仅 phtty: {len(only_phtty)}")
    print()

    rows = []
    for path in common:
        a, b = loc[path], pht[path]
        rows.append(
            (
                path,
                a["total"],
                b["total"],
                a["comment"],
                b["comment"],
                a["comment"] - b["comment"],
                pct(a),
                pct(b),
                pct(a) - pct(b),
            )
        )

    print(f"## 共同文件中「本地注释行数 − phtty」增量 Top {args.top}")
    print()
    print("| 文件 | 本地总行 | phtty总行 | 本地注释 | phtty注释 | Δ注释行 | 本地% | phtty% | Δ% |")
    print("|---|---|---|---|---|---|---|---|---|")
    for r in sorted(rows, key=lambda x: -x[5])[: args.top]:
        print(f"| {r[0]} | {r[1]} | {r[2]} | {r[3]} | {r[4]} | {r[5]:+d} | {r[6]:.1f}% | {r[7]:.1f}% | {r[8]:+.1f} |")
    print()

    print(f"## 共同文件中「本地注释占比 − phtty」差 Top {args.top}")
    print()
    print("| 文件 | 本地% | phtty% | Δ% | 本地注释 | phtty注释 |")
    print("|---|---|---|---|---|---|")
    for r in sorted(rows, key=lambda x: -x[8])[: args.top]:
        print(f"| {r[0]} | {r[6]:.1f}% | {r[7]:.1f}% | {r[8]:+.1f} | {r[3]} | {r[4]} |")
    print()

    # 汇总
    tot_a = sum(r[3] for r in rows)
    tot_b = sum(r[4] for r in rows)
    tot_la = sum(r[1] for r in rows)
    tot_lb = sum(r[2] for r in rows)
    print("## 共同文件汇总")
    print()
    print(f"- 本地: 总行 {tot_la} / 注释 {tot_a}")
    print(f"- phtty: 总行 {tot_lb} / 注释 {tot_b}")
    print(f"- 注释行差: {tot_a - tot_b:+d}")
    print()

    print("## 仅本地新增的文件（前 60 个，按注释行排序截断）")
    print()
    for p in only_local[:60]:
        print(f"- {p} (总行 {loc[p]['total']}, 注释 {loc[p]['comment']})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
