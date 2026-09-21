#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""验证「块内 level→cell 位置换」假设：字模用到的 level 集合 {3..13}（11 个）
在某个 4 位地址位置换/取反下，是否恰好落在现场观测的 11 个 cell 上。

适用前提（由现场观测反推）：一个 4×4 块 = 一片（16 路）；同一段的 16 片映射到
同一个块（多对一），故点亮 cell 集合 = 字模 level 集合的像。
"""
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
G = {(x, y) for y, r in enumerate(GLYPH) for x, c in enumerate(r) if c == "#"}

# 观测 B（块内局部坐标，rows 4..7 → 0..3）
B = {(0, 0), (1, 0), (2, 0), (3, 0), (2, 1), (3, 1), (0, 2), (0, 3), (1, 3), (2, 3), (3, 3)}

levels = sorted({x for (x, y) in G})
print("字模用到的 level 集合 =", levels, "共", len(levels), "个")
print("观测 B 的 cell 集合 =", sorted(B))


def syms(S):
    out = []
    for m in range(8):
        s = set()
        for (x, y) in S:
            xx, yy = x, y
            if m & 4:
                xx, yy = yy, xx
            if m & 1:
                xx = 3 - xx
            if m & 2:
                yy = 3 - yy
            s.add((xx, yy))
        out.append((m, s))
    return out


res = []
for perm in itertools.permutations(range(4)):
    for inv in range(16):
        order = {}
        for i in range(16):
            b = [(i >> k) & 1 for k in range(4)]
            v = [b[perm[k]] ^ ((inv >> k) & 1) for k in range(4)]
            dx = v[0] | (v[1] << 1)
            dy = v[2] | (v[3] << 1)
            order[i] = (dx, dy)
        cells = {order[i] for i in levels}
        for m, s in syms(B):
            res.append((len(cells ^ s), len(cells), perm, inv, m, cells))

res.sort(key=lambda r: (r[0], r[1]))
print("\n== 最佳 8 条（diff = 与观测的对称差）==")
for diff, n, perm, inv, m, cells in res[:8]:
    print(f"\ndiff={diff}  |cells|={n}  perm={perm} inv={inv} sym={m}")
    for y in range(4):
        print("    " + "".join("#" if (x, y) in cells else "." for x in range(4)))

print("\n== 观测 B ==")
for y in range(4):
    print("    " + "".join("#" if (x, y) in B else "." for x in range(4)))
