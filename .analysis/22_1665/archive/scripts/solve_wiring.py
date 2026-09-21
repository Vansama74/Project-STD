#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 接线反解器：拿「真实字模 + 现场两条观测」搜索真实物理接线。

现场观测（用户实测，屏面坐标，左上角 (0,0)）：
  A（默认画面「口」+ 黄色：R+G 两段都亮）22 点，全红
  B（同一「口」改成绿色：只 G 段亮）11 点
  结构关系：A = B ⊎ (B + 4 列) ⇒ R 段像与 G 段像同形、错开 4 列、互不相交

驱动侧（默认口径）：链位 p = 32*y + 16*seg + x（32 片 ×16 级；seg 0=R / 1=G），
内容颜色 c：R 段亮 = c&1，G 段亮 = c&2。

搜索：物理接线 φ: 链位 p → 屏面 (X, Y)，允许 φ 多对一（多个输出驱动同一 LED）。
打分：预测像 vs 实测像（对平移取最优）对称差之和（A、B 同时算）。
"""
from __future__ import annotations

import itertools
import pathlib

# ------------------------------------------------------------------ 真实字模
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
DRIVER_BITS = N_CHIPS * CHIP_BITS            # 512


def content_pixel_of(p):
    """默认口径：链位 p →（内容像素 (x,y), 段 seg）。"""
    c, s = divmod(p, CHIP_BITS)
    seg = c % 2
    dst = (c // 2) * 16 + s                   # 内容像素序号 y*16+x
    return dst % 16, dst // 16, seg


def lit_bits(color, head_rev=False, level_rev=False, nbits=DRIVER_BITS):
    """内容全屏同色 color 时，链位 p 是否发光（只考虑字模内的像素）。"""
    out = [0] * nbits
    for p in range(nbits):
        c, s = divmod(p, CHIP_BITS)
        if head_rev:
            c = N_CHIPS - 1 - c
        if level_rev:
            s = CHIP_BITS - 1 - s
        seg = c % 2
        dst = (c // 2) * 16 + s
        x, y = dst % 16, dst // 16
        if (x, y) not in G_PX:
            continue
        if seg == 0:
            out[p] = 1 if (color & 0b001) else 0
        else:
            out[p] = 1 if (color & 0b010) else 0
    return out


# ==================================================================== 模型族
class Model:
    def __init__(self, name):
        self.name = name

    def map(self, p):
        """链位 p → 屏面 (X, Y)（可多对一）"""
        raise NotImplementedError

    def predict(self, color, head_rev=False, level_rev=False, nbits=DRIVER_BITS):
        bits = lit_bits(color, head_rev, level_rev, nbits)
        return {self.map(p) for p in range(nbits) if bits[p]}


class Fold(Model):
    """屏面 LED(X,Y) 由内容像素折叠 1/kx、1/ky 而来（多对一 OR 合并）。
       seg 的 R/G 段整体错开 seg_dx 列（现场观测的 +4）。"""

    def __init__(self, kx, ky, ox, oy, transp, seg_dx, plane_first=0):
        super().__init__(f"fold({kx},{ky})+({ox},{oy}) t={transp} segdx={seg_dx} pf={plane_first}")
        self.kx, self.ky, self.ox, self.oy, self.transp, self.seg_dx = kx, ky, ox, oy, transp, seg_dx
        self.plane_first = plane_first

    def map(self, p):
        x, y, seg = content_pixel_of(p)
        a = (x + self.ox) // self.kx
        b = (y + self.oy) // self.ky
        X, Y = (b, a) if self.transp else (a, b)
        return (X + self.seg_dx * seg, Y)


def best_translation(pred, obs, lim=20):
    """允许整体平移对齐（屏面原点未知），返回 (对称差, 平移)"""
    best = None
    for dx in range(-lim, lim + 1):
        for dy in range(-lim, lim + 1):
            shifted = {(x + dx, y + dy) for x, y in pred}
            d = len(shifted ^ obs)
            if best is None or d < best[0]:
                best = (d, (dx, dy))
    return best


def ascii_map(pts, x0, y0, w, h):
    return ["".join("#" if (x0 + i, y0 + j) in pts else "." for i in range(w)) for j in range(h)]


def show(pts, pad=2):
    xs = [x for x, _ in pts] or [0]
    ys = [y for _, y in pts] or [0]
    x0, x1 = min(xs) - pad, max(xs) + pad
    y0, y1 = min(ys) - pad, max(ys) + pad
    print("\n".join("        " + l for l in ascii_map(pts, x0, y0, x1 - x0 + 1, y1 - y0 + 1)))


def main():
    print(f"字模 |P| = {len(G_PX)}   观测 |A| = {len(OBS_A)}  |B| = {len(OBS_B)}")
    results = []

    # ---- 折叠族（含转置、相位、段偏移）----
    for kx, ky in itertools.product((1, 2, 4, 8), repeat=2):
        if kx * ky > 64:
            continue
        for ox in range(kx):
            for oy in range(ky):
                for transp in (0, 1):
                    for seg_dx in (4, ):
                        m = Fold(kx, ky, ox, oy, transp, seg_dx)
                        pa = m.predict(0b011)
                        pb = m.predict(0b010)
                        da, ta = best_translation(pa, OBS_A)
                        db, tb = best_translation(pb, OBS_B)
                        # A/B 必须同一次平移（同屏同一次观测）→ 用联合最优
                        dmin, best_t, best_pa, best_pb = None, None, None, None
                        for dx in range(-8, 9):
                            for dy in range(-8, 9):
                                sa = {(x + dx, y + dy) for x, y in pa}
                                sb = {(x + dx, y + dy) for x, y in pb}
                                tot = len(sa ^ OBS_A) + len(sb ^ OBS_B)
                                if dmin is None or tot < dmin:
                                    dmin, best_t, best_pa, best_pb = tot, (dx, dy), sa, sb
                        results.append((dmin, len(best_pa), len(best_pb), m.name, best_pa, best_pb))

    results.sort(key=lambda r: (r[0], abs(r[1] - 22) + abs(r[2] - 11)))
    print("\n== 折叠族 top 6（联合平移最优）==")
    for tot, na, nb, name, pa, pb in results[:6]:
        print(f"\n[{tot:3d}] {name}  |predA|={na} |predB|={nb}")
        print("   pred B:");  show(pb)
    print("\n== 实测 B ==")
    show(OBS_B)
    print("\n== 实测 A ==")
    show(OBS_A)


if __name__ == "__main__":
    main()
