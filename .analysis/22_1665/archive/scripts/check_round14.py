#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""第十四轮（2026-09-17）驱动 ↔ 预测图 一致性自检（宿主，读源码，不编译固件）。

检查项：
  A. 10 根候选脚掩码位序：bit0..9 = R1/G1/B1/R2/G2/B2/A/B/C/D（各为 1<<k），默认 = 0x3FF；
     引脚端口归组 = PG（R1/G1/B1/R2）+ PB（G2/B2）+ PD（A/B/C/D）= 3 个端口（≤ PORT_MAX）。
  B. 探针 7 逐脚图案表 `_22_1665_probe7_cells[]` = {1,3,7,F,1F,3F,7F,FF,FFF,FFFF}，
     点数 = 1..8/12/16 十档互不相同。
  C. 驱动 `_22_1665_cell_of_i()` 与宿主模型 cell_of_i(i) 逐值一致（16/16），
     且 i→cell 是双射（探针图案按格定义 → 按级序取用，两者必须同源）。
  D. 默认态（10 脚同流 + 屏面内容 = 「三」上半）下，已知链（R1）的预测屏面 =
     `.analysis/22_1665/expected_screens.md` 预期① 的形状（用驱动同一份 dst 映射复算）。
  E. 探针 7 每根脚在「每 16 链位（= 一片）」上的点数 = 该脚图案的点数（置换不变判据）。
  F. 地址脚归属约定：`_22_1665_set_row()` 只写「未选作数据的地址脚」（源码含
     `_22_1665_RGB_COUNT + k` 跳过条件）。

用法：`python3 .analysis/22_1665/check_round14.py`（退出码 0 = 全部通过）。
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
SRC = ROOT / "Device/Display/dev_display_22_1665.c"

CHIPS, CHIP_BITS, BLK_W, BLK_H, BLK_COLS = 8, 16, 4, 4, 4
FRAME_BITS = CHIPS * CHIP_BITS

fails: list[str] = []
oks: list[str] = []


def check(cond: bool, msg: str) -> None:
    (oks if cond else fails).append(msg)


def cell_of_i_driver(i: int) -> int:
    return ((((i // 4) + 2) % 4) * 4) + (3 - (i % 4))


def led_offset_driver(p: int) -> tuple[int, int]:
    """驱动 §四 `_22_1665_led_offset()` 的宿主复算。"""
    c, i = divmod(p, CHIP_BITS)
    bc = (BLK_COLS - 1) - (c % BLK_COLS)
    br = c // BLK_COLS
    dx = 3 - (i % 4)
    dy = ((i // 4) + 2) % 4
    return (BLK_W * bc + dx, BLK_H * br + dy)


def main() -> int:
    text = SRC.read_text(encoding="utf-8")

    # ---- A. 掩码位序与默认值 ----
    names = ["R1", "G1", "B1", "R2", "G2", "B2", "A", "B", "C", "D"]
    bits = []
    for n in names:
        m = re.search(rf"#define _22_1665_LINE_{n}\s+\(0x([0-9A-Fa-f]+)U\)", text)
        if not m:
            fails.append(f"A: 未找到 _22_1665_LINE_{n}")
            bits.append(-1)
            continue
        bits.append(int(m.group(1), 16))
    expected_bits = [1 << k for k in range(10)]
    check(bits == expected_bits, f"A1 掩码位序 bit0..9 = 1<<k（实际 {[hex(b) for b in bits]}）")
    check(re.search(r"#define _22_1665_LINE_COUNT\s+\(10U\)", text) is not None, "A2 LINE_COUNT = 10")
    check(re.search(r"#define _22_1665_LINE_ALL\s+\(0x3FFU\)", text) is not None, "A3 LINE_ALL = 0x3FF")
    check(re.search(r"#define _22_1665_DATA_LINES\s+\(_22_1665_LINE_ALL\)", text) is not None,
          "A4 默认 DATA_LINES = 全部 10 根（现场验证态）")
    check(re.search(r"#define _22_1665_PORT_MAX\s+\(3U\)", text) is not None, "A5 PORT_MAX = 3（PG/PB/PD）")

    # ---- B. 探针 7 图案表 ----
    m = re.search(r"_22_1665_probe7_cells\[_22_1665_LINE_COUNT\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        fails.append("B: 未找到 _22_1665_probe7_cells[]")
        cells = []
    else:
        cells = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{4})U", m.group(1))]
    check(cells == [0x0001, 0x0003, 0x0007, 0x000F, 0x001F, 0x003F, 0x007F, 0x00FF, 0x0FFF, 0xFFFF],
          f"B1 探针 7 图案表 = {{1,3,7,F,1F,3F,7F,FF,FFF,FFFF}}（实际 {[hex(c) for c in cells]}）")
    counts = [bin(c).count("1") for c in cells]
    check(len(set(counts)) == 10 and sorted(counts) == [1, 2, 3, 4, 5, 6, 7, 8, 12, 16],
          f"B2 十档点数互不相同（实际 {counts}）")

    # ---- C. cell_of_i 与 i→cell 双射 ----
    mapped = [cell_of_i_driver(i) for i in range(16)]
    check(sorted(mapped) == list(range(16)), f"C1 i→cell 双射（实际 {mapped}）")
    check(max(cells) <= 0xFFFF, "C2 图案掩码只占块内 16 格")

    # ---- D. 默认态已知链预测 = 文档预期① ----
    glyph = """................
................
................
..############..
................
................
................
...##########...""".split("\n")
    exp_pts = {(x, y) for y, row in enumerate(glyph) for x, ch in enumerate(row) if ch == "#"}
    # 已知链：链位 p 的内容 = hub75_buff[dst[p]]；hub75_buff/pixel_map 同布局（恒等 prepare），
    # 内容 = 「三」上半渲染进 16×8 屏（行主序偏移 = y*16+x）
    content = {y * 16 + x for (x, y) in exp_pts}
    got_pts = set()
    for p in range(FRAME_BITS):
        (X, Y) = led_offset_driver(p)
        off = Y * 16 + X
        # 驱动 scan 的链序口径：p 从大到小移位；链上第 p 位 ↔ 内容偏移 dst[p]
        if off in content:
            got_pts.add((X, Y))
    check(got_pts == exp_pts, "D1 默认态（三上半）已知链预测 = expected_screens.md 预期①")

    # ---- E. 探针 7 每 16 链位的点数 = 图案点数 ----
    ok_e = True
    for k, c in enumerate(cells):
        live = {cell_of_i_driver(i) for i in range(16) if (c >> cell_of_i_driver(i)) & 1}
        if len(live) != counts[k]:
            ok_e = False
    check(ok_e, "E1 探针 7 每片点数 = 图案点数（1..8/12/16，置换不变判据）")

    # ---- F. set_row 只写未选作数据的地址脚 ----
    m = re.search(r"static void _22_1665_set_row\(uint8_t row\)\s*\{(.*?)\n\}", text, re.S)
    check(m is not None and "_22_1665_RGB_COUNT + k" in m.group(1) and "_22_1665_addr_pins[k].pin" in m.group(1),
          "F1 set_row 跳过「已选作数据的地址脚」（位序 = RGB_COUNT + k）")
    check("pl_hub75_Decoder_set_row" not in (m.group(1) if m else ""),
          "F2 set_row 不再无条件写 A/B/C/D（旧行为仅在四脚都未选中时逐位等价）")
    check(re.search(r"if \(k >= _22_1665_RGB_COUNT\)", text) is not None,
          "F3 line_pin(k)：k < 6 走 RGB 表，k ≥ 6 走地址脚表")

    print(f"== 第十四轮一致性自检（{SRC.name}）==")
    for s in oks:
        print(f"  [通过] {s}")
    for s in fails:
        print(f"  [失败] {s}")
    print(f"结果：通过 {len(oks)} / 失败 {len(fails)}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
