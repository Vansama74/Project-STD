#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 接线反解器 v2：模线性接线族穷举。

物理接线模型（覆盖 交织 / 蛇形 / 块 / 折叠 / 转置 / 环绕塌缩(多对一)）：

    屏面坐标  X = (A*chip + B*level + C) mod PW
              Y = (D*chip + E*level + F) mod PH
    其中  chip = 链位 p //16（可整体反向）、level = p %16（可整体反向）
    驱动侧默认口径：chip = 2*y + seg  ⇒  A*chip 同时携带 y 与段信息

只要 PW*PH < 512，就把「多输出驱动同一 LED」显式表达出来（取模合并）——
这正是现场 22 点 / 11 点这组「折叠」数字的来源候选。

打分：预测 A（黄=R+G）、B（绿=G）与实测的两组点集，允许一次共同的整体平移
（屏面原点未知），取对称差之和最小。
"""
from __future__ import annotations

import itertools

GLYPH = """
................
................
................
...###########..
...#........##..
...#........##..
...#........##..
...#........##..
...#........##..
...#........##..
...#........##..
...#........##..
...#........##..
...###########..
...#........##..
................
""".strip("\n").split("\n")
G_PX = {(x, y) for y, row in enumerate(GLYPH) for x, ch in enumerate(row) if ch == "#"}

OBS_B = {(0, 4), (1, 4), (2, 4), (3, 4), (2, 5), (3, 5), (0, 6),
         (0, 7), (1, 7), (2, 7), (3, 7)}
OBS_A = {(0, 4), (1, 4), (2, 4), (3, 4), (4, 4), (5, 4), (6, 4), (7, 4),
         (2, 5), (3, 5), (6, 5), (7, 5), (0, 6), (4, 6),
         (0, 7), (1, 7), (2, 7), (3, 7), (4, 7), (5, 7), (6, 7), (7, 7)}

CHIP_BITS = 16
N_CHIPS = 32


def ascii_map(pts, x0, y0, w, h):
    return ["".join("#" if (x0 + i, y0 + j) in pts else "." for i in range(w)) for j in range(h)]


def show(pts, pad=2):
    xs = [x for x, _ in pts] or [0]
    ys = [y for _, y in pts] or [0]
    x0, x1 = min(xs) - pad, max(xs) + pad
    y0, y1 = min(ys) - pad, max(ys) + pad
    print("\n".join("    " + l for l in ascii_map(pts, x0, y0, x1 - x0 + 1, y1 - y0 + 1)))


class Lin:
    def __init__(self, A, B, C, D, E, F, PW, PH, head_rev, level_rev):
        self.A, self.B, self.C, self.D, self.E, self.F = A, B, C, D, E, F
        self.PW, self.PH, self.head_rev, self.level_rev = PW, PH, head_rev, level_rev

    def name(self):
        return (f"X=({self.A}*c+{self.B}*s+{self.C})%{self.PW} "
                f"Y=({self.D}*c+{self.E}*s+{self.F})%{self.PH} "
                f"head={self.head_rev} lvl={self.level_rev}")

    def lit(self, color):
        out = set()
        for p in range(N_CHIPS * CHIP_BITS):
            c, s = divmod(p, CHIP_BITS)
            if self.head_rev:
                c = N_CHIPS - 1 - c
            if self.level_rev:
                s = CHIP_BITS - 1 - s
            seg = c % 2
            dst = (c // 2) * 16 + s
            x, y = dst % 16, dst // 16
            if (x, y) not in G_PX:
                continue
            on = (color & 1) if seg == 0 else (color & 2)
            if not on:
                continue
            X = (self.A * c + self.B * s + self.C) % self.PW
            Y = (self.D * c + self.E * s + self.F) % self.PH
            out.add((X, Y))
        return out


def joint_score(m, lim=10):
    pa = m.lit(0b011)
    pb = m.lit(0b010)
    best = None
    for dx in range(-lim, lim + 1):
        for dy in range(-lim, lim + 1):
            sa = {(x + dx, y + dy) for x, y in pa}
            sb = {(x + dx, y + dy) for x, y in pb}
            tot = len(sa ^ OBS_A) + len(sb ^ OBS_B)
            if best is None or tot < best[0]:
                best = (tot, (dx, dy), sa, sb)
    return best


def main():
    panels = [(8, 4), (16, 4), (8, 8), (16, 8), (16, 16), (4, 4), (32, 8), (32, 16)]
    res = []
    for (PW, PH) in panels:
        for head_rev in (0, 1):
            for level_rev in (0, 1):
                for A in range(PW):
                    for B in range(PW):
                        for C in range(PW):
                            for D in range(PH):
                                for E in range(PH):
                                    for F in range(PH):
                                        m = Lin(A, B, C, D, E, F, PW, PH, head_rev, level_rev)
                                        tot, t, sa, sb = joint_score(m)
                                        if tot <= 12:
                                            res.append((tot, m.name(), sa, sb, t))
    res.sort(key=lambda r: r[0])
    print(f"命中（联合对称差 <=12）{len(res)} 条；前 12 条：")
    for tot, name, sa, sb, t in res[:12]:
        print(f"\n[{tot:3d}] {name}  shift={t}  |A|={len(sa)} |B|={len(sb)}")
        print("  pred B:"); show(sb, pad=1)
        print("  obs  B:"); show(OBS_B, pad=1)


if __name__ == "__main__":
    main()
