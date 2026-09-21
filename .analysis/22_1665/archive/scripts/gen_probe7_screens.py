#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成探针模式 7「逐脚标识」的**预测屏面 ASCII**（现场对照用）。

第十四轮（2026-09-17）：候选脚从 6 根扩到 **10 根**（6 数据脚 + A/B/C/D 四根行址脚）。

模型（doc/01 22-1665 记录 §15，现场反解唯一解）：
    链位 p → 片 c = p/16、级序 i = p%16
    块：bc = (BLK_COLS-1) - (c % BLK_COLS)、br = c / BLK_COLS
    块内：(dx,dy) = (3 - i%4, (i/4 + 2) % 4)
    屏面：(X,Y) = (4*bc+dx, 4*br+dy)

探针 7 的逐脚图案（必须与 `Device/Display/dev_display_22_1665.c`
`_22_1665_probe7_cells[]` / `_22_1665_cell_of_i()` 逐条一致）：
    第 k 根脚 = 4×4 块内「行主序前 k+1 格」点亮 → 每块点数 = k + 1（k = 0..9）
    ⇒ 点数 1..8 + 12 + 16，十档互不相同；「整行 / 两行 / 三行 / 满块」四档一眼可辨。
    点数判据**对链内位序置换不变**（一片 16 位里亮几个灯），所以即使其余链的片内
    位序与已知链不同，也能按点数认脚。

输出：每根脚一张 16×8（默认 BLK_COLS=4）预测图 + 合成图 + BLK_COLS 1/2/8 备选。
"""
from __future__ import annotations

CHIPS, CHIP_BITS = 8, 16
BLK_W = BLK_H = 4
FRAME_BITS = CHIPS * CHIP_BITS

# 逐脚图案（行主序格掩码，bit = dy*4+dx）—— 必须与驱动 `_22_1665_probe7_cells[]` **逐条一致**：
#   前 8 根 = 「行主序前 k+1 格」；后 2 根取「整三行(12)」与「满块(16)」两档、
#   点数 1..8 + 12 + 16 共十档互不相同，且「整行 / 两行 / 三行 / 满块」一眼可辨。
CELLS = [0x0001, 0x0003, 0x0007, 0x000F, 0x001F, 0x003F, 0x007F, 0x00FF, 0x0FFF, 0xFFFF]

# (序号, 名称, 引脚, 说明) —— 顺序 = 掩码 bit 序（bit k ↔ 第 k 根）
PINS = [
    (0, "R1", "PG9", "1 点/块"),
    (1, "G1", "PG10", "2 点/块"),
    (2, "B1", "PG12", "3 点/块"),
    (3, "R2", "PG15", "4 点/块（块内第 0 行整行）"),
    (4, "G2", "PB6", "5 点/块"),
    (5, "B2", "PB7", "6 点/块"),
    (6, "A", "PD4", "7 点/块（行址脚当数据线）"),
    (7, "B", "PD5", "8 点/块（块内前两行整满，行址脚）"),
    (8, "C", "PD6", "12 点/块（前三行，行址脚）"),
    (9, "D", "PD7", "16 点/块（整块实心，行址脚）"),
]


def cells_of(k: int) -> int:
    """第 k 根脚的行主序格掩码（bit = dy*4+dx）——与驱动 `_22_1665_probe7_cells[]` 同表。"""
    return CELLS[k]


def cell_of_i(i: int) -> int:
    """片内级序 i → 块内行主序格号 —— 与驱动 `_22_1665_cell_of_i()` 同式。"""
    dx = 3 - (i % 4)
    dy = ((i // 4) + 2) % 4
    return dy * 4 + dx


def line_on(k: int, p: int) -> bool:
    return bool((cells_of(k) >> cell_of_i(p % CHIP_BITS)) & 1)


def led_of(p: int, blk_cols: int) -> tuple[int, int]:
    c, i = divmod(p, CHIP_BITS)
    bc = (blk_cols - 1) - (c % blk_cols)
    br = c // blk_cols
    dx = 3 - (i % 4)
    dy = ((i // 4) + 2) % 4
    return (BLK_W * bc + dx, BLK_H * br + dy)


def screen(k: int, blk_cols: int) -> list[str]:
    rows = CHIPS // blk_cols
    w, h = blk_cols * BLK_W, rows * BLK_H
    pts = {led_of(p, blk_cols) for p in range(FRAME_BITS) if line_on(k, p)}
    return ["".join("#" if (x, y) in pts else "." for x in range(w)) for y in range(h)]


def block_tile(k: int, blk_cols: int = 4) -> list[str]:
    """单块的 4×4 图案（把该脚图案限制在片 0 的 16 个链位上）。"""
    pts = {led_of(p, blk_cols) for p in range(CHIP_BITS) if line_on(k, p)}
    x0 = min(x for x, _ in pts)
    y0 = min(y for _, y in pts)
    return ["".join("#" if (x, y) in pts else "." for x in range(x0, x0 + 4))
            for y in range(y0, y0 + 4)]


def count(k: int) -> int:
    return bin(cells_of(k)).count("1")


def main() -> None:
    print("# 探针模式 7「逐脚标识」预测屏面（第十四轮：10 根候选脚）\n")
    print("> 生成脚本 `.analysis/22_1665/gen_probe7_screens.py`（模型的 ASCII 版；")
    print("> 与驱动 `_22_1665_probe7_cells[]` / `_22_1665_cell_of_i()` 逐条对应）。")
    print("> 预测前提：**该脚驱动的链与已知链同构**（片内位序/块栅格/链长都与 §15 模型一致）。")
    print("> 若某条链的片内位序不同，形状会变形——但**每 16 链位的点数** = k+1 不变，照样能认脚；")
    print("> 若某条链数据极性相反，出现的是**补色图案**，读数取 16 − 点数（见 §3 反查表）。\n")

    print("## 0. 判读总表（先看这张）\n")
    print("| bit | 脚 | 引脚 | 每块点数 | 单块 4×4 形状（已知链模型） |")
    print("|---|---|---|---|---|")
    for k, name, gpio, desc in PINS:
        tile = block_tile(k)
        print(f"| {k} | `{name}` | {gpio} | **{count(k)}** | `" + "` / `".join(tile) + "` |")
    print()

    print("## 1. 逐脚预测屏面（默认 `_22_1665_BLK_COLS=4`，屏面 16×8）\n")
    for k, name, gpio, desc in PINS:
        img = screen(k, 4)
        print(f"### bit{k}：`{name}`（{gpio}）— {desc}\n")
        print("```")
        for row in img:
            print(row)
        print("```\n")

    print("## 2. 合成图（10 根脚同时输出，各自驱动自己的链）\n")
    print("模组上这 10 根脚各自驱动**不同**的链 → 屏上不同区域各自重复出现下表对应图案。")
    print("下面把 10 张预测图并排画出（**位置是示意**，实际哪块区域对应哪根脚要看现场）：\n")
    for start, label in ((0, "bit0..bit4：R1 / G1 / B1 / R2 / G2"),
                         (5, "bit5..bit9：B2 / A / B / C / D")):
        imgs = [screen(k, 4) for k in range(start, start + 5)]
        print(f"```\n（左起：{label}）")
        for r in range(8):
            print("   ".join(img[r] for img in imgs))
        print("```\n")

    print("## 3. 备选屏面形状（现场若发现块排布不是「一行 4 块」，切 `_22_1665_BLK_COLS`）\n")
    for cols in (1, 2, 8):
        rows = CHIPS // cols
        w, h = cols * BLK_W, rows * BLK_H
        print(f"### `_22_1665_BLK_COLS={cols}`（屏面 {w}×{h}）\n")
        for start in (0, 5):
            imgs = [screen(k, cols) for k in range(start, start + 5)]
            names = " / ".join(PINS[k][1] for k in range(start, start + 5))
            print("```")
            for r in range(h):
                print("   ".join(img[r] for img in imgs))
            print("```")
            print(f"（左起：{names}）\n")

    print("## 4. 补色（极性相反）反查表\n")
    print("若某区域出现的是**亮点变灭点**的补色图案，按「16 − 观察点数」查下表：\n")
    print("| 观察点数 | 反查脚（bit） |")
    print("|---|---|")
    for k, name, gpio, _ in PINS:
        print(f"| {16 - count(k)} | `{name}`（bit{k}） |")
    print("\n> 例：整块全灭（0 点）= 16 − 16 → bit9 `D` 的补色（或该链未接线）；"
          "15 点 = 16 − 1 → bit0 `R1` 的补色。\n")
    print("> 若十档都读不出来（例如所有区域都是同一形状），把 `_22_1665_DATA_ACTIVE_HIGH` 改成 0")
    print("> 再烧一次即可换极性；也可把 `_22_1665_DATA_LINES` 缩成单脚（如 `0x01`）逐根确认。\n")


if __name__ == "__main__":
    main()
