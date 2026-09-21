#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 接线反解（第三版，已收敛）：验证「精确命中」模型 + 枚举全局块排布候选。

【已命中的局部模型】（由 solve_wiring 的位置换穷举 + level_cell_order 的 diff=0 得出）
  物理链 = 128 位 = 8 片 × 16 级   （= 驱动 512 位里**最后移入**的 128 位；其余 384 位被挤出链尾）
  内容侧默认表：链位 p = 32*y + 16*s + x  →  片 c = p//16、片内级 i = p%16
  片 c 的 16 个输出落在一个 4×4 LED 块内：
        块内  (dx, dy) = (3 - i % 4, ((i // 4) + 2) % 4)
  片 c 所在块在「块栅格」中的位置由全局排布候选给出（本轮未定死的那一维）

【为什么能断定链长 = 128】现场只看到字模第 3 行（11 点/段）：
  驱动 512 位里最后移入的 128 位 = 内容第 0..3 行 × 两段；字模 0..2 行是空的、
  第 3 行 = 11 点 → 恰好 11×2 = 22 点（观测 A）/ 11 点（观测 B）。
  若链长 ≥ 160 位，则内容第 4 行（3 点/段）也会留存 → 观测点数会多出 3~6 点。
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
G_PX = {(x, y) for y, r in enumerate(GLYPH) for x, c in enumerate(r) if c == "#"}

OBS_B = {(0, 4), (1, 4), (2, 4), (3, 4), (2, 5), (3, 5), (0, 6),
         (0, 7), (1, 7), (2, 7), (3, 7)}
OBS_A = {(0, 4), (1, 4), (2, 4), (3, 4), (4, 4), (5, 4), (6, 4), (7, 4),
         (2, 5), (3, 5), (6, 5), (7, 5), (0, 6), (4, 6),
         (0, 7), (1, 7), (2, 7), (3, 7), (4, 7), (5, 7), (6, 7), (7, 7)}

CHAIN_BITS = 128          # 物理链长（依据：观测点数 11 / 22）
CHIP_BITS = 16


# ------------------------------------------------------------------ 局部落点
def cell_of_level(i: int) -> tuple[int, int]:
    """片内级 i → 块内 (dx, dy)（与现场观测精确吻合的那条位置换）"""
    return (3 - (i % 4), ((i // 4) + 2) % 4)


# ------------------------------------------------------------------ 全局排布候选
# 块栅格：BW 列 × BH 行（BW*BH = 8），片号 c 按「行主序 / 列主序 × 正反」铺进栅格。
# 现场只钉死了片 6、7 落在同一块行的相邻两块（且 6 在右、7 在左）——下列候选都满足。
def blk_pos(c, BW, BH, order, rev_r, rev_c):
    if order == "row":       # c 按行主序：bc = c % BW, br = c // BW
        br, bc = c // BW, c % BW
    else:                    # 列主序：bc = c // BH, br = c % BH
        br, bc = c % BH, c // BH
    if rev_r:
        br = BH - 1 - br
    if rev_c:
        bc = BW - 1 - bc
    return br, bc


class Model:
    def __init__(self, BW, BH, order, rev_r, rev_c, level_rev=False, chain=CHAIN_BITS):
        self.BW, self.BH, self.order, self.rr, self.rc = BW, BH, order, rev_r, rev_c
        self.level_rev = level_rev
        self.chain = chain
        self.W, self.H = BW * 4, BH * 4

    def name(self):
        return (f"块栅格 {self.BW}x{self.BH} {self.order}序 rev=({self.rr},{self.rc}) "
                f"屏 {self.W}x{self.H} lvlrev={int(self.level_rev)}")

    def led_of(self, p):
        c, i = divmod(p, CHIP_BITS)
        if self.level_rev:
            i = CHIP_BITS - 1 - i
        br, bc = blk_pos(c, self.BW, self.BH, self.order, self.rr, self.rc)
        dx, dy = cell_of_level(i)
        return (4 * bc + dx, 4 * br + dy)

    def predict(self, color):
        """按驱动默认表：p = 32y + 16s + x，内容像素 (x,y)=P、段 s 由颜色决定。"""
        out = set()
        for p in range(self.chain):
            c, i = divmod(p, CHIP_BITS)
            seg = c % 2                     # 偶数片=R、奇数片=G（默认 SEG_LAYOUT=0）
            dst = (c // 2) * 16 + i         # 内容像素序号（驱动侧假设）
            x, y = dst % 16, dst // 16
            if (x, y) not in G_PX:
                continue
            on = (color & 1) if seg == 0 else (color & 2)
            if on:
                out.add(self.led_of(p))
        return out


def ascii_map(pts, x0, y0, w, h):
    return ["".join("#" if (x0 + i, y0 + j) in pts else "." for i in range(w)) for j in range(h)]


def show(pts, x0, y0, w, h, indent="    "):
    print("\n".join(indent + l for l in ascii_map(pts, x0, y0, w, h)))


def main():
    print(f"字模 |P| = {len(G_PX)}；观测 |A| = {len(OBS_A)}，|B| = {len(OBS_B)}")
    print("局部落点（精确命中）：(dx, dy) = (3 - i%4, (i//4 + 2) % 4)")
    print()

    ok = []
    for BW, BH in ((4, 2), (2, 4), (8, 1), (1, 8)):
        for order in ("row", "col"):
            for rr in (0, 1):
                for rc in (0, 1):
                    for lvlrev in (0, 1):
                        m = Model(BW, BH, order, rr, rc, lvlrev)
                        pa, pb = m.predict(0b011), m.predict(0b010)
                        da, db = len(pa ^ OBS_A), len(pb ^ OBS_B)
                        if da == 0 and db == 0:
                            ok.append(m)
    print(f"== 与两条观测**逐点完全一致**的全局排布候选：{len(ok)} 个 ==")
    for m in ok:
        print(f"   · {m.name()}")

    # 逐候选：探针模式 4（片锚点）预测
    print()
    print("== 各候选下「探针模式 4（POS=3）」预测（每片 1 点，共 8 点）==")
    for m in ok[:6]:
        pts = {m.led_of(c * 16 + 3) for c in range(8)}
        print(f"\n  {m.name()}  →  8 点坐标 {sorted(pts)}")
        show(pts, 0, 0, m.W, m.H)

    # 默认候选（最可能）的完整验证图
    m = ok[0]
    print()
    print(f"== 候选「{m.name()}」的完整复算 ==")
    print("观测 A（黄=R+G）预测：")
    show(m.predict(0b011), 0, 4, 8, 4)
    print("实测 A：")
    show(OBS_A, 0, 4, 8, 4)
    print("观测 B（绿=G）预测：")
    show(m.predict(0b010), 0, 4, 8, 4)
    print("实测 B：")
    show(OBS_B, 0, 4, 8, 4)


if __name__ == "__main__":
    main()
