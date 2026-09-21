#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成「预期屏面」ASCII（烧录后对照用）：

1) 默认画面（复位后自动显示）——`app_default_display.c` 当前内容 = FONT_16 黑体「三」，
   渲染进驱动声明的 16×8 屏（16 宽 × 8 高），越界部分被基类按屏幕交集裁剪 →
   屏上应看到「三」的**上半**（字模第 0..7 行 = 第 1 条横 + 第 2 条横的上沿）。
   字模直接从字库 bin dump（**MX25L256** 布局 = 板上实际字库芯片：其 16pt GBK
   黑体「口」字模与上一轮现场观测逐点吻合，见 doc/01 §15）；bin 不在时退回内嵌兜底。
2) 探针模式 4（片锚点，POS=3）：8 个点，X ∈ {0,4,8,12} × Y ∈ {2,6}。
3) 探针模式 5（全链点亮）：全屏 128 点全亮。
4) 逐链位走点（模式 1）的轨迹说明：亮点沿 128 个位置逐帧推进。

第十四轮（2026-09-17）：默认 = **10 根候选脚全开**（`_22_1665_DATA_LINES = 0x3FF`），
R1 那条链（= 已知 16×8）的显示内容与上一轮**逐字节相同**；其余链只要挂在这 10 根
中的任意几根上，就会显示同一幅画面。逐脚图案见 `probe7_screens.md`。
"""
from __future__ import annotations

import pathlib

# ---- 字库 bin（MX25L256 布局；与 app_render.c 的 FONT_16 GBK HT 区块一致）----
FONT_BIN = ("/home/yystation/文档/创迪科技生产订单与技术资料/19 标准产品资料/升降栏杆机/"
            "5.软件（含平台软件、测试软件与嵌入式程序）/1.嵌入式程序/9I124D6521/"
            "字库芯片程序/W25Q256_FONT_14_16_20_24_32.bin")
MX25_16_HT = dict(base=1218, X=0, Y=8, Z=64,
                  idx=lambda h, l: (h - 0x81) * 190 + (l - 0x41))

# 内嵌兜底：MX25L256 bin 的 16pt GBK 黑体「三」（GBK 0xC8FD）字模（上方 bin 缺失时用）
GLYPH_FALLBACK = """
................
................
................
..############..
................
................
................
...##########...
...##########...
................
................
................
................
.##############.
................
................
""".strip("\n").split("\n")

W, H = 16, 8

CHIPS, CHIP_BITS = 8, 16
BLK_W = BLK_H = 4
BLK_COLS = 4
BLK_ROWS = CHIPS // BLK_COLS


def glyph_rows() -> list[str]:
    """dump FONT_16 黑体「三」的 16 行字模（失败则用内嵌兜底）。"""
    p = pathlib.Path(FONT_BIN)
    if not p.exists():
        return GLYPH_FALLBACK
    try:
        data = p.read_bytes()
        size, w = 16, 16
        bpc = size * ((w + 7) // 8)
        idx = MX25_16_HT["idx"](0xC8, 0xFD)
        fonf = idx * bpc
        sec, page, byte = fonf // 4096, (fonf % 4096) // 256, (fonf % 4096) % 256
        a = ((MX25_16_HT["base"] + sec + MX25_16_HT["X"]) * 4096
             + (page + MX25_16_HT["Y"]) * 256 + byte + MX25_16_HT["Z"])
        buf = data[a:a + bpc]
        if len(buf) < bpc:
            return GLYPH_FALLBACK
        rows = ["".join("#" if (buf[r * 2 + c // 8] >> (7 - c % 8)) & 1 else "."
                        for c in range(w)) for r in range(size)]
        return rows
    except Exception:
        return GLYPH_FALLBACK


def led_of(p: int) -> tuple[int, int]:
    c, i = divmod(p, CHIP_BITS)
    bc = (BLK_COLS - 1) - (c % BLK_COLS)
    br = c // BLK_COLS
    dx = 3 - (i % 4)
    dy = ((i // 4) + 2) % 4
    return (BLK_W * bc + dx, BLK_H * br + dy)


def show(pts, title):
    print(f"### {title}\n")
    print("```")
    for y in range(H):
        print("".join("#" if (x, y) in pts else "." for x in range(W)))
    print("```\n")


def main():
    # 1) 默认画面 = 「三」字模第 0..7 行（16×8 屏 = 字模上半）
    rows = glyph_rows()
    pts = {(x, y) for y, row in enumerate(rows[:H]) for x, ch in enumerate(row) if ch == "#"}
    show(pts, "预期①：默认画面（FONT_16 黑体「三」渲染进 16×8 屏 = 字模上半，"
              f"{len(pts)} 点）")

    # 2) 探针模式 4（POS=3）
    pts = {led_of(c * CHIP_BITS + 3) for c in range(CHIPS)}
    show(pts, f"预期②：探针模式 4（片锚点，POS=3）→ {len(pts)} 点，"
              f"{sorted(pts)}")

    # 3) 探针模式 5（全链点亮）
    pts = {led_of(p) for p in range(CHIPS * CHIP_BITS)}
    show(pts, f"预期③：探针模式 5（全链点亮）→ {len(pts)} 点全亮")

    # 4) 其余 BLK_COLS 候选下模式 4 的图案（若预期②对不上，按这张表换 BLK_COLS）
    for cols in (1, 2, 8):
        rows_n = CHIPS // cols
        s = set()
        for c in range(CHIPS):
            bc = (cols - 1) - (c % cols)
            br = c // cols
            i = 3
            s.add((BLK_W * bc + (3 - i % 4), BLK_H * br + ((i // 4) + 2) % 4))
        w, h = cols * BLK_W, rows_n * BLK_H
        print(f"### 备选：BLK_COLS={cols}（屏 {w}×{h}）下模式 4 应为\n")
        print("```")
        for y in range(h):
            print("".join("#" if (x, y) in s else "." for x in range(w)))
        print("```\n")


if __name__ == "__main__":
    main()
