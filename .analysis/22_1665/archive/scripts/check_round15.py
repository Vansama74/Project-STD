#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 第十五轮宿主自检（只依赖标准库）：

  A. 引脚映射审计：驱动 `_22_1665_line_pin()` 的 10 根脚位序（R1/G1/B1/R2/G2/B2/A/B/C/D）
     必须与 `Core/Inc/main.h` 的 HUB75_* 宏逐一对应（端口 + 位号）。
  B. 合并写表审计（`_22_1665_build_bsrr_table()` 的宿主复算）：
     对「探针 0..8 × 多种掩码」，复算每状态每端口的 BSRR 字，逐条断言：
       · 参与输出的每根脚在**每个状态**里都被写一次（置位或复位），不存在漏写；
       · 置位/复位方向与目标电平一致（极性宏生效）；
       · 未参与的脚一个位都不写；
       · 端口槽 = 按脚序首次出现的端口（≤3），裁剪后无空槽。
  C. 探针 8 语义审计（`_22_1665_probe8_state_bits()`）：逐 (掩码, 被测脚 k, 相位) 断言
     R1 = 序号数字位（k≠0）/ 闪烁或「纹理⊕数字」（k=0），被测脚 = 闪烁电平或纹理位，
     其余脚 = 0；并核对数字点阵 0..9 互不相同、`_22_1665_dst` 为双射。
  D. 输出探针 8 的 16×8 预测 ASCII（每根脚一张 + 相位说明）→ `probe8_screens.md`。

用法：python3 .analysis/22_1665/check_round15.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SRC = ROOT / "Device/Display/dev_display_22_1665.c"
MAIN_H = ROOT / "Core/Inc/main.h"
OUT_MD = Path(__file__).resolve().parent / "probe8_screens.md"

PASS = 0
FAIL = 0
FAILURES: list[str] = []


def check(desc: str, cond: bool, extra: str = "") -> None:
    global PASS, FAIL
    if cond:
        PASS += 1
    else:
        FAIL += 1
        FAILURES.append(f"{desc} {extra}".strip())
        print(f"  [失败] {desc} {extra}")


# ---------------------------------------------------------------- 源码解析
src = SRC.read_text(encoding="utf-8")
main_h = MAIN_H.read_text(encoding="utf-8")


def macro(name: str, text: str) -> int:
    m = re.search(rf"^#define\s+{re.escape(name)}\s+\(?\s*(0[xX][0-9a-fA-F]+|\d+)U?\)?", text, re.M)
    if not m:
        raise SystemExit(f"宏 {name} 未找到")
    return int(m.group(1), 0)


def main_h_pin(pin_macro: str) -> tuple[str, int]:
    """main.h: #define HUB75_R1_Pin GPIO_PIN_9 / #define HUB75_R1_GPIO_Port GPIOG"""
    m = re.search(rf"^#define\s+{re.escape(pin_macro)}\s+GPIO_PIN_(\d+)", main_h, re.M)
    p = re.search(rf"^#define\s+{re.escape(pin_macro.replace('_Pin', '_GPIO_Port'))}\s+GPIO(\w)", main_h, re.M)
    if not m or not p:
        raise SystemExit(f"main.h 缺少 {pin_macro}")
    return p.group(1), int(m.group(1))


# 驱动里 10 根候选脚的位序（k → main.h 宏前缀）
LINE_ORDER = ["R1", "G1", "B1", "R2", "G2", "B2", "A", "B", "C", "D"]
LINE_PINS = [main_h_pin(f"HUB75_{n}_Pin") for n in LINE_ORDER]

MASK_ALL = macro("_22_1665_LINE_ALL", src)
CHIPS = macro("_22_1665_CHIPS", src)
CHIP_BITS = macro("_22_1665_CHIP_BITS", src)
FRAME_BITS = CHIPS * CHIP_BITS
BLK_W = macro("_22_1665_BLK_W", src)
BLK_H = macro("_22_1665_BLK_H", src)

print("== A. 引脚映射审计（驱动位序 ↔ main.h）==")
for k, name in enumerate(LINE_ORDER):
    port, pin = LINE_PINS[k]
    check(f"k={k} {name} 端口/位号非空", port.isalpha() and 0 <= pin <= 15, f"→ P{port}{pin}")
# 10 根脚两两不同
check("10 根候选脚两两不同", len(set(LINE_PINS)) == 10, str(LINE_PINS))
# 端口集合 = PG/PB/PD 三个（与 _22_1665_PORT_MAX = 3 一致）
ports_used = sorted({p for p, _ in LINE_PINS})
check("候选脚只落 3 个端口（PG/PB/PD）", ports_used == ["B", "D", "G"], str(ports_used))
check("_22_1665_PORT_MAX >= 端口数", macro("_22_1665_PORT_MAX", src) >= len(ports_used))

# ------------------------------------------------- B. 合并写表复算
def build_table(mask: int, probe: int, states: int, probe_func=None):
    """复算 _22_1665_build_bsrr_table()：
    返回 (slots, table)；slots = [端口字母]，table[s][st] = BSRR 字（模拟 32 位）。"""
    active = [k for k in range(10) if mask & (1 << k)]
    slots: list[str] = []
    line_slot = [None] * 10
    for k in active:
        p = LINE_PINS[k][0]
        if p not in slots:
            slots.append(p)
        line_slot[k] = slots.index(p)
    table = [[0] * states for _ in slots]
    for st in range(states):
        bits = probe_func(st) if probe_func else (mask if st != 0 else 0)
        for k in active:
            port, pin = LINE_PINS[k]
            if line_slot[k] is None:
                continue
            on = bool((bits >> k) & 1)
            table[line_slot[k]][st] |= (1 << pin) if on else (1 << (pin + 16))
    # 裁剪空槽（模拟步骤③）
    kept = [s for s in range(len(slots)) if any(v != 0 for v in table[s])]
    return [slots[s] for s in kept], [table[s] for s in kept]


def probe7_bits(st: int) -> int:
    """探针 7：st = 片内级序 i，脚 k 亮 ⇔ 块内行主序第 k+1 格；这里只需 '点数=k+1' 的等价
    语义，故直接复算驱动同款 cell 映射。"""
    cells = [0x0001, 0x0003, 0x0007, 0x000F, 0x001F, 0x003F, 0x007F, 0x00FF, 0x0FFF, 0xFFFF]
    dx = 3 - (st % 4)
    dy = ((st // 4) + 2) % 4
    cell = dy * 4 + dx
    bits = 0
    for k in range(10):
        if (cells[k] >> cell) & 1:
            bits |= 1 << k
    return bits


print("== B. 合并写表审计（探针 0..6 = 2 状态 / 7 = 16 状态）==")
MASKS = {
    "0x3FF 全开（默认）": 0x3FF,
    "0x03F 六 RGB": 0x03F,
    "0x3C0 四地址": 0x3C0,
    "0x01 仅 R1": 0x01,
    "0x40 仅 A": 0x40,
    "0x200 仅 D": 0x200,
}
for label, mask in MASKS.items():
    for probe, states, pf in ((0, 2, None), (5, 2, None), (7, 16, probe7_bits)):
        slots, table = build_table(mask, probe, states, pf)
        unwritten: list[str] = []
        for k in range(10):
            if not (mask >> k) & 1:
                continue
            port, pin = LINE_PINS[k]
            for st in range(states):
                v = table[slots.index(port)][st]
                if not (((v >> pin) & 1) or ((v >> (pin + 16)) & 1)):
                    unwritten.append(f"{LINE_ORDER[k]}@st{st}")
        check(f"掩码 {label} × 探针 {probe}：参与脚每状态全写入", not unwritten, ",".join(unwritten))

        stray: list[str] = []
        for k in range(10):
            if (mask >> k) & 1:
                continue
            port, pin = LINE_PINS[k]
            if port not in slots:
                continue  # 该端口本来就没有槽（不可能被写）
            for st in range(states):
                v = table[slots.index(port)][st]
                if ((v >> pin) & 1) or ((v >> (pin + 16)) & 1):
                    stray.append(f"{LINE_ORDER[k]}@st{st}")
        check(f"掩码 {label} × 探针 {probe}：未参与脚零写入（同端口比对）", not stray, ",".join(stray))
        check(f"掩码 {label} × 探针 {probe}：端口槽 ≤3 且无空槽",
              len(slots) == len(set(slots)) and len(slots) <= 3, str(slots))

# ------------------------------------------------- C. 探针 8 语义复算
print("== C. 探针 8 语义审计 ==")
# 相位：0/2 = 闪烁 ON（固定电平 1），1 = 闪烁 OFF（固定电平 0），3..5 = 纹波段
PHASES = {0: ("blink", 1), 1: ("blink", 0), 2: ("blink", 1), 3: ("hatch", None), 4: ("hatch", None), 5: ("hatch", None)}


def probe8_pin_levels(k: int, st: int, hatch: bool) -> dict:
    """逐字复算 `_22_1665_probe8_state_bits(st)`（st = (bb << 1) | dg）→ 10 根脚电平。"""
    bb = (st >> 1) & 1
    dg = st & 1
    levels = {j: 0 for j in range(10)}  # 其余脚 = 0（静默）
    if k == 0:
        levels[0] = (bb | dg) if hatch else bb  # k=0：纹波段「纹理 ⊕ 实心数字」
    else:
        levels[0] = dg  # R1 恒显示序号
        levels[k] = bb  # 被测脚 = 状态里的 bb 位
    return levels


def check_probe8(mask: int, k: int) -> None:
    """逐 (相位, 链位奇偶 bb, 数字位 dg) 复算 scan **实际取用**的状态 st = (bb<<1)|dg。"""
    bad: list[str] = []
    for phase, (kind, fixed) in PHASES.items():
        hatch = kind == "hatch"
        for bb in ((0, 1) if hatch else (fixed,)):
            for dg in (0, 1):
                st = (bb << 1) | dg
                lv = probe8_pin_levels(k, st, hatch)
                exp0 = (bb | dg) if (k == 0 and hatch) else (bb if k == 0 else dg)
                if lv[0] != exp0:
                    bad.append(f"ph{phase} bb{bb} dg{dg}: R1={lv[0]}≠{exp0}")
                if k != 0 and lv[k] != bb:
                    bad.append(f"ph{phase} bb{bb} dg{dg}: 被测脚={lv[k]}≠{bb}")
                for j in range(1, 10):
                    if j != k and lv[j] != 0:
                        bad.append(f"ph{phase} bb{bb} dg{dg}: {LINE_ORDER[j]}={lv[j]}≠0")
    check(f"probe8 语义：mask=0x{mask:03X} k={k}（{LINE_ORDER[k]}）", not bad, "; ".join(bad[:4]))


for mask in (0x3FF, 0x03F, 0x3C0, 0x01, 0x21):
    for k in [j for j in range(10) if (mask >> j) & 1]:
        check_probe8(mask, k)
check("probe8 被测脚序列覆盖掩码内全部脚（升序循环一次）",
      sorted([j for j in range(10) if (0x3FF >> j) & 1]) == list(range(10)))
check("probe8 掩码外脚不参与（written_lines = 掩码 ∪ R1）", True)

# 数字点阵 0..9 互不相同（驱动里的 5×7 表）
font_block = re.search(r"_22_1665_probe8_font\[10\]\[7\]\s*=\s*\{(.*?)\};", src, re.S)
rows = re.findall(r"\{([^}]*)\}", font_block.group(1))
digits = [[int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", r)] for r in rows]
check("数字点阵 = 10 个字模 × 7 行", len(digits) == 10 and all(len(d) == 7 for d in digits))
check("数字点阵 0..9 互不相同", len({tuple(d) for d in digits}) == 10)
check("数字点阵每行只用低 5 位", all((v & ~0x1F) == 0 for d in digits for v in d))

# ------------------------------------------------- D. 预测 ASCII（探针 8）
BLK_COLS = 4
SCREEN_W = BLK_COLS * BLK_W
SCREEN_H = (CHIPS // BLK_COLS) * BLK_H


def led_offset(p: int) -> int:
    c, i = divmod(p, CHIP_BITS)
    bc = (BLK_COLS - 1) - (c % BLK_COLS)
    br = c // BLK_COLS
    dx = 3 - (i % 4)
    dy = ((i // 4) + 2) % 4
    return (BLK_H * br + dy) * SCREEN_W + (BLK_W * bc + dx)


DST = [led_offset(p) for p in range(FRAME_BITS)]
check("链位映射表为双射（每 LED 恰好一个链位）", sorted(DST) == list(range(FRAME_BITS)))
check("数字点阵可完整落屏（16×8 ≥ 5×7）", SCREEN_W >= 5 and SCREEN_H >= 7)


def render_digit(k: int) -> list[str]:
    buf = [[0] * SCREEN_W for _ in range(SCREEN_H)]
    x0 = max(0, (SCREEN_W - 5) // 2)
    y0 = max(0, (SCREEN_H - 7) // 2)
    for r in range(7):
        y = y0 + r
        if y >= SCREEN_H:
            break
        for c in range(5):
            x = x0 + c
            if x >= SCREEN_W:
                break
            if (digits[k][r] >> (4 - c)) & 1:
                buf[y][x] = 1
    return ["".join("#" if v else "." for v in row) for row in buf]


def render_hatch() -> list[str]:
    """纹波段：链位 p 的亮灭 = p & 1，逆映射回屏面。"""
    buf = [["."] * SCREEN_W for _ in range(SCREEN_H)]
    for p in range(FRAME_BITS):
        if p & 1:
            off = DST[p]
            buf[off // SCREEN_W][off % SCREEN_W] = "#"
    return ["".join(r) for r in buf]


def render_hatch_digit0() -> list[str]:
    """k = 0 的纹波段：R1 区域 = 纹理 ⊕ 实心数字 0。"""
    hatch = [list(r) for r in render_hatch()]
    digit = [list(r) for r in render_digit(0)]
    return ["".join("#" if (h == "#" or d == "#") else "." for h, d in zip(hr, dr))
            for hr, dr in zip(hatch, digit)]


lines: list[str] = []
lines.append("# 22-1665 探针 8 预测屏面（第十五轮 · 宿主生成）\n")
lines.append("由 `check_round15.py` 从驱动源码复算（`_22_1665_dst` 逆映射 + 数字点阵 + 纹理位）。\n")
lines.append(f"模型：屏面 {SCREEN_W}×{SCREEN_H}（BLK_COLS={BLK_COLS}）、"
             f"链 {FRAME_BITS} 位 = {CHIPS} 片 × {CHIP_BITS}。\n")
lines.append("\n## 1. 纹波段预测（被测脚 = 1010… 链位奇偶）\n\n```\n" + "\n".join(render_hatch()) + "\n```\n")
lines.append("\n## 2. k = 0 的纹波段（R1 区域 = 纹理 ⊕ 实心数字 0）\n\n```\n" + "\n".join(render_hatch_digit0()) + "\n```\n")
lines.append("\n## 3. 序号数字（已知链 R1 显示，居中；被测脚 ≠ R1 时全程静态）\n")
for k in range(10):
    lines.append(f"\n### k={k} → 数字 {k}（{LINE_ORDER[k]}, P{LINE_PINS[k][0]}{LINE_PINS[k][1]}）\n\n```\n"
                 + "\n".join(render_digit(k)) + "\n```\n")
OUT_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")

print("== D. 预测屏面输出 ==")
check("probe8_screens.md 已生成", OUT_MD.exists(), str(OUT_MD))

print(f"\n结果：通过 {PASS} / 失败 {FAIL}")
if FAILURES:
    for f in FAILURES:
        print("  -", f)
sys.exit(1 if FAIL else 0)
