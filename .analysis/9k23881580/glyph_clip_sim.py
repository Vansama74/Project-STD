#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""glyph_clip_sim.py — 「字形部分裁剪绘制 / 渲染行跳过」宿主侧逻辑等价验证

对应改动（2026-09-14，用户裁决）：
  ① Device/Display/dev_display.c  dev_display_draw_bitmap()
       越界由「整块丢弃」改为「屏幕交集逐像素裁剪」；
  ② Application/Src/app_render.c   _render_text() 两处折行后的行跳过判定
       `cur_y + line_h > cfg->h → return` 改为 `cur_y >= cfg->h → return`
       （行框下缘越界但行首仍在区域内 → 保留绘制，交由显示层裁剪）。

本脚本不依赖硬件与工程头文件，按上述两处现行实现逐句复刻关键算法，并与
**独立参考模型**（逐像素数学定义：像素可见 ⇔ 落在屏幕内 ∧ 落在位图矩形内 ∧
该位为 1）对拍；同时给出旧实现（改动前）用于「变更前/后」逐案对照。

用法：python3 glyph_clip_sim.py     # 全部用例通过时退出码 0，末尾打印统计
"""

import itertools
import random
import sys

# ---------------------------------------------------------------- 位图工具
def pack_bitmap(w, h, bit_fn):
    """MSB-first 行打包：byte(row, col) 的 bit(0x80 >> col%8)；row_bytes=(w+7)//8。"""
    rb = (w + 7) // 8
    bm = bytearray(rb * h)
    for r in range(h):
        for c in range(w):
            if bit_fn(r, c):
                bm[r * rb + c // 8] |= 0x80 >> (c % 8)
    return bytes(bm), rb


def bit_at(bm, rb, row, col):
    return bool(bm[row * rb + col // 8] & (0x80 >> (col % 8)))


# ------------------------------------------------- 显示层：新实现（裁剪绘制）
def draw_new(sw, sh, x, y, w, h, bm):
    """dev_display_draw_bitmap 现行实现（越界裁剪）。返回 {px: True} 集合。"""
    drawn = set()
    if x >= sw or y >= sh:                       # 起点越界早退（与 fill 对齐）
        return drawn
    vis_w = w if x + w <= sw else sw - x         # 只可能裁右/下边界（无符号坐标）
    vis_h = h if y + h <= sh else sh - y
    if vis_w <= 0 or vis_h <= 0:
        return drawn
    rb = (w + 7) // 8
    for row in range(vis_h):
        base = row * rb
        for col in range(vis_w):
            if bm[base + col // 8] & (0x80 >> (col % 8)):
                drawn.add(((x + col), (y + row)))
    return drawn


# ------------------------------------------------- 显示层：旧实现（整块丢弃）
def draw_old(sw, sh, x, y, w, h, bm):
    drawn = set()
    if x >= sw or y >= sh:
        return drawn
    if x + w > sw or y + h > sh:                 # 改动前的整块早退
        return drawn
    rb = (w + 7) // 8
    for row in range(h):
        for col in range(w):
            if bm[row * rb + col // 8] & (0x80 >> (col % 8)):
                drawn.add((x + col, y + row))
    return drawn


# ------------------------------------------------------------ 独立参考模型
def oracle(sw, sh, x, y, w, h, bm):
    """数学定义：屏幕内 ∧ 位图矩形内 ∧ 该位为 1（与实现无关）。"""
    drawn = set()
    rb = (w + 7) // 8
    for px in range(max(0, x), min(sw, x + w)):
        for py in range(max(0, y), min(sh, y + h)):
            if bit_at(bm, rb, py - y, px - x):
                drawn.add((px, py))
    return drawn


def classify(sw, sh, x, y, w, h):
    inside = (x + w <= sw) and (y + h <= sh) and x < sw and y < sh
    start_out = (x >= sw) or (y >= sh)
    partial = (not inside) and (not start_out) and w > 0 and h > 0
    return inside, start_out, partial


# --------------------------------------------------------------- 用例生成
SCREENS = [(32, 16), (224, 64), (96, 96)]
BOXES = []          # (标签, w, h) —— 覆盖 FONT_16/24/32 × ASCII/GBK 字形框
for size in (16, 24, 32):
    BOXES.append(("F%d-ASCII" % size, size // 2, size))
    BOXES.append(("F%d-GBK" % size, size, size))
BOXES += [("full-screen-restore", 32, 16), ("wide", 64, 8), ("tall", 8, 64)]


def bit_patterns(w, h, seed=0):
    rnd = random.Random(seed * 7919 + w * 131 + h)
    pats = {
        "all-on": lambda r, c: True,
        "all-off": lambda r, c: False,
        "checker": lambda r, c: (r + c) % 2 == 0,
        "corner-bits": lambda r, c: (r, c) in {(0, 0), (0, 7), (0, 8), (0, w - 1),
                                               (h - 1, 0), (h - 1, w - 1), (1, 1)},
        "glyph-frame": lambda r, c: r in (0, h - 1) or c in (0, w - 1) or r == c,
        "rand": lambda r, c: rnd.getrandbits(1) == 1,
    }
    return pats


def test_display_layer():
    total = 0
    ok = 0
    diffs = []
    for (sw, sh) in SCREENS:
        for (label, w, h) in BOXES:
            xs = sorted({0, 1, 7, sw - w - 1 if sw - w - 1 >= 0 else 0, sw - w, sw - w + 1,
                         sw - 2, sw - 1, sw, sw + 1, sw // 2}, key=lambda v: (v < 0, v))
            ys = sorted({0, 1, 7, sh - h - 1 if sh - h - 1 >= 0 else 0, sh - h, sh - h + 1,
                         sh - 2, sh - 1, sh, sh + 1, sh // 2}, key=lambda v: (v < 0, v))
            for (x, y) in itertools.product(xs, ys):
                if x < 0 or y < 0 or x > sw + 1 or y > sh + 1:
                    continue
                for pname, fn in bit_patterns(w, h, seed=hash((sw, sh, w, h, x, y)) & 0xFF).items():
                    bm, _ = pack_bitmap(w, h, fn)
                    new = draw_new(sw, sh, x, y, w, h, bm)
                    old = draw_old(sw, sh, x, y, w, h, bm)
                    ref = oracle(sw, sh, x, y, w, h, bm)
                    total += 1
                    inside, start_out, partial = classify(sw, sh, x, y, w, h)
                    # 断言 1：新实现 == 独立参考模型（任意几何、任意位模式）
                    assert new == ref, ("新实现偏离参考模型", sw, sh, x, y, w, h, pname)
                    # 断言 2：完全在屏内 → 新旧逐像素一致（224×64 零变化底线）
                    if inside:
                        assert new == old, ("屏内用例行为变化", sw, sh, x, y, w, h, pname)
                    # 断言 3：起点越界 → 新旧都空
                    if start_out:
                        assert not new and not old, ("起点越界用例", sw, sh, x, y, w, h)
                    # 断言 4：空矩形不落笔
                    if w == 0 or h == 0:
                        assert not new
                    # 断言 5：差异只允许出现在「部分越界」用例，且旧丢整块、新画交集
                    if new != old:
                        assert partial, ("不允许的差异类别", sw, sh, x, y, w, h, pname)
                        assert old == set() or old.issubset(new), ("差异方向异常", sw, sh, x, y, w, h)
                        diffs.append((sw, sh, label, x, y, w, h, pname, len(new), len(old)))
                    ok += 1
    return total, ok, diffs


# ------------------------------------------------------ 渲染层：行跳过模型
def _wrap_text(glyphs, font_size, cfg_w):
    """测量趟（复刻 _render_text 前半）：返回每行宽度列表 line_widths。"""
    widths, line_w = [], 0
    for gw in glyphs:
        if line_w + gw > cfg_w:
            widths.append(line_w)
            line_w = gw
        else:
            line_w += gw
    widths.append(line_w)
    return widths


def _render_pass(glyphs, font_size, cfg_x, cfg_y, cfg_w, cfg_h, rule):
    """渲染趟（复刻折行与行跳过路径；word_wrap=True、h_align=LEFT、等宽字形）。

    rule='old' → 折行后 `cur_y + line_h > cfg->h → return`（改动前）
    rule='new' → 折行后 `cur_y >= cfg->h → return`（现行）
    返回到达 dev_display_draw_bitmap 的字形框列表 [(cur_x, cur_y, gw, font_size)]。
    注意 C 实现折行后**不 continue**：触发折行的字形在新行照常绘制（本模型一致）。
    """
    boxes = []
    cur_x, cur_y = cfg_x, cfg_y
    for gw in glyphs:
        if cur_x + gw > cfg_w:
            cur_y += font_size
            cur_x = cfg_x
            if (cur_y >= cfg_h) if rule == 'new' else (cur_y + font_size > cfg_h):
                return boxes
        boxes.append((cur_x, cur_y, gw, font_size))
        cur_x += gw
    return boxes


def test_render_layer():
    total = 0
    cases = []
    # 32×16：0x20 台架（区域 = 剩余屏幕，X=Y=0），16/24/32 × {1 字, 多字}
    for size in (16, 24, 32):
        for n in (1, 2, 6, 14):
            cases.append(("32x16", 32, 16, 0, 0, 32, 16, size, [size] * n))
    # 224×64：整机（区域 = 全屏），多字号 × 多长度 —— 新旧必须完全一致
    for size in (16, 24, 32):
        for n in (1, 3, 7, 14, 30):
            cases.append(("224x64", 224, 64, 0, 0, 224, 64, size, [size] * n))
            cases.append(("224x64-ascii", 224, 64, 0, 0, 224, 64, size, [size // 2] * n))
    # 区域高非字号整数倍（云南/贵州 '4' 坐标任意 y 的边角；192×96 = 1-969 屏）：记录差异
    cases.append(("band-offset", 192, 96, 0, 10, 192, 86, 24, [24] * 6))   # 单行，无差异
    cases.append(("band-offset", 192, 96, 0, 10, 192, 86, 24, [24] * 25))  # 第 4 行 82..105 越屏

    same_on_224x64 = 0
    delta_224x64 = []
    assert_32x16 = {"16": None, "24": None, "32": None}
    for (tag, sw, sh, cx, cy, cw, ch, size, glyphs) in cases:
        old_boxes = _render_pass(glyphs, size, cx, cy, cw, ch, 'old')
        new_boxes = _render_pass(glyphs, size, cx, cy, cw, ch, 'new')
        total += 1
        if tag != "32x16":
            if old_boxes == new_boxes:
                same_on_224x64 += 1
            else:
                # 允许的唯一差异类别：末行行框下缘越屏（行首仍在区域/屏内）——
                # 旧规则整行丢弃，新规则画出可见部分；其余字形框必须完全一致。
                extra = [b for b in new_boxes if b not in old_boxes]
                assert extra and old_boxes == new_boxes[:len(old_boxes)], \
                    ("非预期差异", tag, size, len(glyphs))
                for (bx, by, bw, bh) in extra:
                    assert by < sh and by + size > sh, ("越屏行以外的差异", tag, size, by)
                    assert by >= 0 and bx >= 0
                base = len(old_boxes) and old_boxes[-1] or None
                delta_224x64.append((tag, size, len(glyphs), len(old_boxes), len(new_boxes),
                                     base, extra[0], [b for b in extra if b not in (base, extra[0])]))
        if tag == "32x16":
            # 旧规则：24/32 首行框越屏 → 显示层整块丢弃 → 屏上无像素
            # 新规则：首行框保留 → 显示层裁剪 → 至少画出上部可见部分
            def union_px(boxes, draw_fn):
                px = set()
                for (bx, by, bw, bh) in boxes:
                    px |= draw_fn(sw, sh, bx, by, bw, bh, b"\xff" * (((bw + 7) // 8) * bh))
                return px
            old_px = union_px(old_boxes, draw_old)
            new_px = union_px(new_boxes, draw_new)
            if size == 16:
                # 16pt 字形全在屏内：新旧一致（台架现有表现零变化）
                assert old_px == new_px and new_px, ("16pt 台架行为变化", glyphs)
            else:
                assert old_px == set(), ("旧规则下 24/32 本应全空", size, len(glyphs))
                assert new_px, ("新规则下 24/32 应有可见像素", size, len(glyphs))
                ys = {py for (_, py) in new_px}
                xs = {px for (px, _) in new_px}
                assert min(ys) == 0 and max(ys) < sh, "可见部分应贴屏顶（字形上部）"
                assert max(xs) < sw and min(xs) >= 0
            assert_32x16[str(size)] = (len(old_px), len(new_px))
    return total, same_on_224x64, assert_32x16, delta_224x64


def main():
    t1, ok1, diffs = test_display_layer()
    print("== 显示层 draw_bitmap 穷举 ==")
    print("   用例数 %d（全部通过：新实现==参考模型；屏内用例新旧一致）" % ok1)
    print("   新旧差异用例 %d 条（全部为『部分越界』：新画交集 / 旧丢整块）" % len(diffs))
    shown = {}
    for (sw, sh, label, x, y, w, h, pname, n_new, n_old) in diffs:
        key = (sw, sh, label, pname)
        if key not in shown:
            shown[key] = (sw, sh, label, x, y, w, h, pname, n_new, n_old)
    for k, v in list(shown.items())[:6]:
        print("     e.g. screen=%dx%d %s pos=(%d,%d) box=%dx%d pat=%s -> new=%d px, old=%d px"
              % (v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9]))

    t2, same2, c32, d224 = test_render_layer()
    print("== 渲染层行跳过模型 ==")
    print("   用例数 %d；其中整屏/边角用例 %d 条：完全一致 %d 条，末行下缘越屏差异 %d 条"
          % (t2, same2 + len(d224), same2, len(d224)))
    for (tag, size, n, n_old, n_new, base, first, rest) in d224[:8]:
        print("     差异: %s FONT_%d glyphs=%d 行框数 old=%d new=%d；"
              "新增行盒 base=%s 首=%s" % (tag, size, n, n_old, n_new, base, first))
    print("   32×16 台架（区域 32×16、X=Y=0）可见像素数 新/旧：")
    for k in ("16", "24", "32"):
        print("     FONT_%-2s -> new=%d px / old=%d px" % (k, c32[k][1], c32[k][0]))
    print("ALL PASS")


if __name__ == "__main__":
    sys.exit(main())
