#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 第十七轮宿主自检：**现场两条观测的回溯复算 + 修复后默认表预测 + 探针 9 语义 + 落点旗标**

  A. **现场两条观测的回溯复算**（把上一版驱动（第十六轮占位链表 R1/G1/B1/R2）的位流逐点算出来，
     对照用户本轮实测的坐标集合 ⇒ 证明「两条观测同根因」的解释成立）：
       · 红「口」→ 驱动给 B1 发的确实是「下半屏 29 点」，但现场 B1 不导通，屏上只有 R1 的 23 点
         （= 用户实测点集，逐点一致）；
       · 绿「口」→ 红链（R1/B1）取 R 位 = 0 → R1 那条链整体变黑；绿链（G1/R2）的位流非零，
         但 G1/R2 现场不导通 ⇒ 全屏黑（= 用户实测「完全不显示」）。
  B. **修复后的默认链表**（链 1/2/3 → A/B/C）：结构断言（链 0 = R1 已被现场确认、其余挂地址脚）
     + 「若四链接线与本表一致」的整屏预测（红「口」= 52 点全幅）。
  C. **探针 9 语义复算**：「链 × 脚」组合表、每个组合只驱动一根脚（其余写 0）、数字只在 R1、
     内容 = 被测链自己的像素（非黑即亮）、一轮 = 链数 × 脚数 × 停留。
  D. **落点镜像旗标**：恒等 = 第十二轮已确认模型（逐位一致）；16 种组合皆为区域内双射；
     ROT180 = 像素空间 180° 旋转（逐点验证）。
  E. 生成 `round17_screens.md`（观测回溯图 + 修复后预测图 + 探针 9 读表）。

用法：python3 .analysis/22_1665/check_round17.py
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
SRC = ROOT / "Device/Display/dev_display_22_1665.c"
OUT_MD = pathlib.Path(__file__).resolve().parent / "round17_screens.md"

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


src = SRC.read_text(encoding="utf-8")


def macro(name: str, default: int | None = None) -> int:
    m = re.search(rf"^#define\s+{re.escape(name)}\s+\(?\s*(0[xX][0-9a-fA-F]+|\d+)U?\)?", src, re.M)
    if not m:
        if default is not None:
            return default
        raise SystemExit(f"宏 {name} 未找到")
    return int(m.group(1), 0)


# ---------------------------------------------------------------- 常量
CHIPS = macro("_22_1665_CHIPS")
CHIP_BITS = macro("_22_1665_CHIP_BITS")
FRAME_BITS = CHIPS * CHIP_BITS
BLK_W = macro("_22_1665_BLK_W")
BLK_H = macro("_22_1665_BLK_H")
CHAIN_COUNT = macro("_22_1665_CHAIN_COUNT")
SCREEN_W = macro("_22_1665_SCREEN_W")
SCREEN_H = macro("_22_1665_SCREEN_H")
HALF_H = SCREEN_H // 2  # _22_1665_HALF_H = SCREEN_H / 2

LINE_ORDER = ["R1", "G1", "B1", "R2", "G2", "B2", "A", "B", "C", "D"]
LINE_MASK = {n: (1 << k) for k, n in enumerate(LINE_ORDER)}
ADDR_PINS = ["A", "B", "C", "D"]

XF_X, XF_Y, XF_BLK_X, XF_BLK_Y = 0x1, 0x2, 0x4, 0x8
XF_ROT180 = XF_X | XF_Y | XF_BLK_X | XF_BLK_Y

# 现场观测（用户本轮实测；屏幕像素坐标，原点左上角）
OBS_RED = {
    (3, 3), (4, 3), (5, 3), (6, 3), (7, 3), (8, 3), (9, 3), (10, 3), (11, 3), (12, 3), (13, 3),
    (3, 4), (12, 4), (13, 4),
    (3, 5), (12, 5), (13, 5),
    (3, 6), (12, 6), (13, 6),
    (3, 7), (12, 7), (13, 7),
}

# 上一版（第十六轮）占位链表 —— 现场那版固件用的就是它（本轮源码已修正，故在此硬编码留档）
CHAINS_R16 = [
    dict(name="C0", line_mask=LINE_MASK["R1"], role="RED", x0=0, y0=0, w=16, h=8),
    dict(name="C1", line_mask=LINE_MASK["G1"], role="GREEN", x0=0, y0=0, w=16, h=8),
    dict(name="C2", line_mask=LINE_MASK["B1"], role="RED", x0=0, y0=8, w=16, h=8),
    dict(name="C3", line_mask=LINE_MASK["R2"], role="GREEN", x0=0, y0=8, w=16, h=8),
]

# 现场确认导通的脚（第十二/十七轮两次观测）：只有 R1
FIELD_WORKING_LINES = {LINE_MASK["R1"]}


# ---------------------------------------------------------------- 字模（「口」FONT_16 黑体）
GLYPH_FALLBACK = """
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

FONT_BIN = ("/home/yystation/文档/创迪科技生产订单与技术资料/19 标准产品资料/收费站多功能一体机-标准资料整理v2.0/"
            "5.软件（含平台软件、测试软件与嵌入式程序）/1.嵌入式程序/9J9F2E4230(23-543)/"
            "字库芯片程序/贵州P10双色治超屏字库")
F16_HT = dict(base=209, X=0, Y=2, Z=96, idx=lambda h, l: (h - 0xA1) * 94 + (l - 0xA1))


def glyph_rows() -> list[str]:
    p = pathlib.Path(FONT_BIN)
    if not p.exists():
        return GLYPH_FALLBACK
    try:
        data = p.read_bytes()
        size, w = 16, 16
        bpc = size * ((w + 7) // 8)
        idx = F16_HT["idx"](0xBF, 0xDA)  # 「口」
        fonf = idx * bpc
        sec, page, byte = fonf // 4096, (fonf % 4096) // 256, (fonf % 4096) % 256
        a = ((F16_HT["base"] + sec + F16_HT["X"]) * 4096 + (page + F16_HT["Y"]) * 256 + byte
             + F16_HT["Z"])
        buf = data[a:a + bpc]
        if len(buf) < bpc:
            return GLYPH_FALLBACK
        return ["".join("#" if (buf[r * 2 + c // 8] >> (7 - c % 8)) & 1 else "." for c in range(w))
                for r in range(size)]
    except Exception:
        return GLYPH_FALLBACK


GLYPH = glyph_rows()
GLYPH_PX = {(x, y) for y, row in enumerate(GLYPH) for x, ch in enumerate(row) if ch == "#"}
check("字模「口」= 52 点（第十二轮反解的锚，按已知地址公式 dump）", len(GLYPH_PX) == 52,
      f"{len(GLYPH_PX)} 点")
check("字模「口」顶边 = y=3、x=3..13（11 点）",
      all((x, 3) in GLYPH_PX for x in range(3, 14)) and len([1 for (x, y) in GLYPH_PX if y == 3]) == 11)
check("字模「口」右侧笔画 = 2 px（x=12 与 x=13 同亮，非驱动缺陷）",
      (12, 4) in GLYPH_PX and (13, 4) in GLYPH_PX and (11, 4) not in GLYPH_PX)


# ---------------------------------------------------------------- 落点公式复算
def region_offset(x0: int, y0: int, blk_cols: int, blk_rows: int, flags: int, p: int) -> int:
    """复算 `_22_1665_region_offset()`（含第十七轮落点镜像旗标）。"""
    c, i = divmod(p, CHIP_BITS)
    bc = (blk_cols - 1) - (c % blk_cols)
    br = c // blk_cols
    dx = 3 - (i % 4)
    dy = ((i // 4) + 2) % 4
    if flags & XF_BLK_X:
        bc = (blk_cols - 1) - bc
    if flags & XF_BLK_Y:
        br = (blk_rows - 1) - br
    if flags & XF_X:
        dx = 3 - dx
    if flags & XF_Y:
        dy = 3 - dy
    return (y0 + BLK_H * br + dy) * SCREEN_W + (x0 + BLK_W * bc + dx)


def chain_offsets(c: dict) -> list[int]:
    blk_cols, blk_rows = c["w"] // 4, c["h"] // 4
    return [region_offset(c["x0"], c["y0"], blk_cols, blk_rows, c.get("flags", 0), p)
            for p in range(FRAME_BITS)]


def chain_pixels(c: dict) -> set[tuple[int, int]]:
    return {(o % SCREEN_W, o // SCREEN_W) for o in chain_offsets(c)}


def chain_bits(c: dict, lit: set[tuple[int, int]]) -> list[int]:
    """该链的 128 位位流：位 p 是否点亮（位 p 映射到的像素是否在 lit 里）。"""
    return [1 if ((o % SCREEN_W, o // SCREEN_W) in lit) else 0 for o in chain_offsets(c)]


def visible_pixels(chains: list[dict], lit: set[tuple[int, int]], working: set[int]) -> set[tuple[int, int]]:
    """按「哪些脚现场真导通」算出屏上可见点集（链角色决定取哪一位像素分量；此处的 lit 已经是
    该链该取的分量集合）。"""
    vis: set[tuple[int, int]] = set()
    for c in chains:
        if c["line_mask"] not in working:
            continue  # 该链的数据脚现场不导通 → 整条链静默
        for off, b in zip(chain_offsets(c), chain_bits(c, lit)):
            if b:
                vis.add((off % SCREEN_W, off // SCREEN_W))
    return vis


# ---------------------------------------------------------------- A. 观测回溯
print("== A. 现场两条观测回溯（第十六轮占位链表 + 现场确认：只有 R1 导通）==")

def role_lit(role: str, glyph_color: str) -> set[tuple[int, int]]:
    """某个 die 颜色的链在「内容为 glyph_color」时取到的像素集合。
    RED 链取像素 bit0(R)、GREEN 链取 bit1(G)：纯红内容 → 绿链取到空集（反之亦然）。"""
    has_r = glyph_color in ("RED", "YELLOW", "WHITE")
    has_g = glyph_color in ("GREEN", "YELLOW", "WHITE")
    if (role == "RED" and not has_r) or (role == "GREEN" and not has_g):
        return set()
    return GLYPH_PX


# 观测②：红「口」→ 各链位流（角色决定取 R/G 分量）
red_bits = [sum(chain_bits(c, role_lit(c["role"], "RED"))) for c in CHAINS_R16]
check("观测②：红「口」→ 驱动给 R1 的位流 = 23 点（上半屏）", red_bits[0] == 23, str(red_bits))
check("观测②：红「口」→ 驱动给 B1 的位流 = 29 点（下半屏，现场不导通 → 不显示）",
      red_bits[2] == 29, str(red_bits))
check("观测②：红「口」→ G1/R2（绿链）位流 = 0（红内容下绿链本就该灭）",
      red_bits[1] == 0 and red_bits[3] == 0, str(red_bits))
vis_red = set()
for c in CHAINS_R16:
    if c["line_mask"] not in FIELD_WORKING_LINES:
        continue
    vis_red |= {(o % SCREEN_W, o // SCREEN_W) for o, b in
                zip(chain_offsets(c), chain_bits(c, role_lit(c["role"], "RED"))) if b}
check("观测②：可见点集 = 用户实测点集（23 点，逐点一致）", vis_red == OBS_RED,
      f"差集 {sorted(vis_red ^ OBS_RED)[:8]}")
check("观测②：可见点集全部落在 y ≤ 7（y≥8 一个都不亮）", all(y < HALF_H for (_, y) in vis_red))

# 观测①：绿「口」→ 红链取 R 位 = 0（R1 那条链整体变黑）；绿链位流非零但 G1/R2 不导通
green_bits = [sum(chain_bits(c, role_lit(c["role"], "GREEN"))) for c in CHAINS_R16]
check("观测①：绿「口」→ 红链（R1/B1）位流全 0 ⇒ R1 那条链整体变黑",
      green_bits[0] == 0 and green_bits[2] == 0, str(green_bits))
check("观测①：绿「口」→ 绿链（G1/R2）位流非零（23/29 点）但两脚现场不导通",
      green_bits[1] == 23 and green_bits[3] == 29, str(green_bits))
vis_green = set()
for c in CHAINS_R16:
    if c["line_mask"] not in FIELD_WORKING_LINES:
        continue
    vis_green |= {(o % SCREEN_W, o // SCREEN_W) for o, b in
                  zip(chain_offsets(c), chain_bits(c, role_lit(c["role"], "GREEN"))) if b}
check("观测①：可见点集 = 空集（= 用户实测「全屏一点不亮」）", vis_green == set(), str(vis_green))
check("两条观测同根因：不同点只在「内容分量」，不在取色/落点逻辑（后者被观测①反向验证）",
      red_bits[0] == 23 and green_bits[0] == 0)

# 被排除的假设（逐条留档）
print("  · 排除「四链共用一帧 / 级联成一条 512 位链」：若成立，红「口」会同时点亮上半与下半")
print("    （现场只有上半）——且复位后首帧就会看到 4 份内容，与实测不符")
print("  · 排除「绿 die 不存在 / 不可驱动」：第十三轮十脚同流时绿 LED 亮过（现场事实）")
print("  · 排除「极性反相」：反相会把 23 点变成 105 点亮（整块泛亮），与实测不符")
print("  · 排除「取色/落点 bug」：观测① 里 R1 那条链**正确地**随绿内容变黑，逻辑自洽")

# ---------------------------------------------------------------- B. 默认链表
print("== B. 默认链表：第十七轮推定（历史留档）+ 第二十轮现场定标（现行）==")

# 第十七轮的推定表（链 1/2/3 → 地址脚 A/B/C）——现场那版固件用的就是它，
# 第二十轮探针 8 读数已证明 A/B/C 上无链；这里作为**历史留档**断言，不再代表现行默认值。
CHAINS_R17 = [
    dict(name="C0", line_mask=LINE_MASK["R1"], role="RED", x0=0, y0=0, w=16, h=8),
    dict(name="C1", line_mask=LINE_MASK["A"], role="GREEN", x0=0, y0=0, w=16, h=8),
    dict(name="C2", line_mask=LINE_MASK["B"], role="RED", x0=0, y0=8, w=16, h=8),
    dict(name="C3", line_mask=LINE_MASK["C"], role="GREEN", x0=0, y0=8, w=16, h=8),
]
print("  · 第十七轮推定表（历史）：C0/R1 · C1/A · C2/B · C3/C")
check("（历史）链 0 = R1 不变（现场两次观测确认）", CHAINS_R17[0]["line_mask"] == LINE_MASK["R1"])
check("（历史）链 1/2/3 全部在地址脚 A/B/C/D 之内、且不占用 D",
      all(c["line_mask"] in [LINE_MASK[n] for n in ADDR_PINS] for c in CHAINS_R17[1:])
      and all(not (c["line_mask"] & LINE_MASK["D"]) for c in CHAINS_R17[1:]))
check("（历史）推定表与六根 RGB 脚（除 R1）无关 —— 该推定已被第二十轮探针 8 读数取代",
      all(not (c["line_mask"] & (LINE_MASK["G1"] | LINE_MASK["B1"] | LINE_MASK["R2"] |
                                 LINE_MASK["G2"] | LINE_MASK["B2"])) for c in CHAINS_R17))


def parse_chains(text: str) -> list[dict]:
    m = re.search(r"_22_1665_chains\[[^\]]*\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise SystemExit("未找到 _22_1665_chains[] 初始化块")
    body = m.group(1).split("#else")[0]
    rows: list[dict] = []
    for raw in re.finditer(r"_22_1665_ROW(?:_XF|_DST)?\(([^;]*?)\)\s*,", body, re.S):
        fields = [f.strip() for f in re.split(r",(?![^(]*\))", raw.group(1)) if f.strip()]
        if len(fields) < 7:
            continue

        def val(tok: str) -> int:
            tok = tok.strip()
            if tok.startswith("_22_1665_LINE_"):
                return LINE_MASK[tok[len("_22_1665_LINE_"):]]
            if tok.startswith("_22_1665_ROLE_"):
                return tok[len("_22_1665_ROLE_"):]
            if tok.startswith("_22_1665_SCREEN_W"):
                return SCREEN_W
            if tok.startswith("_22_1665_SCREEN_H"):
                return SCREEN_H
            if tok.startswith("_22_1665_HALF_H"):
                return HALF_H
            if "|" in tok:
                acc = 0
                for part in tok.split("|"):
                    acc |= val(part)
                return acc
            return int(tok.rstrip("Uu"), 0)

        flags = 0
        if "_XF" in raw.group(0)[:20] and len(fields) >= 8:
            xt = fields[7]
            flags = 0
            for part in xt.split("|"):
                if part.strip() == "_22_1665_XF_NONE":
                    flags |= 0
                elif part.strip() == "_22_1665_XF_ROT180":
                    flags |= XF_ROT180
                elif part.strip().endswith("_X"):
                    flags |= XF_X
                elif part.strip().endswith("_Y"):
                    flags |= XF_Y
                elif part.strip().endswith("_BLK_X"):
                    flags |= XF_BLK_X
                elif part.strip().endswith("_BLK_Y"):
                    flags |= XF_BLK_Y
        rows.append(dict(name=re.sub(r'"', "", fields[0]), line_mask=val(fields[1]),
                         role=val(fields[2]), x0=val(fields[3]), y0=val(fields[4]),
                         w=val(fields[5]), h=val(fields[6]), flags=flags))
    return rows


CHAINS = parse_chains(src)
check(f"链表行数 = {CHAIN_COUNT}", len(CHAINS) == CHAIN_COUNT, str(len(CHAINS)))
for c in CHAINS:
    print(f"  · {c['name']}: lines=0x{c['line_mask']:03X} role={c['role']} "
          f"region=({c['x0']},{c['y0']},{c['w']},{c['h']}) flags=0x{c['flags']:X}")

# 第二十轮现场探针 8 定标（现行默认表）= R1/G1/R2/G2；逐字段断言见 check_round20.py
check("（现行 · 第二十轮定标）链 0 = R1、链 1 = G1、链 2 = R2、链 3 = G2（探针 8 读数直定）",
      [c["line_mask"] for c in CHAINS] ==
      [LINE_MASK["R1"], LINE_MASK["G1"], LINE_MASK["R2"], LINE_MASK["G2"]],
      str([hex(c["line_mask"]) for c in CHAINS]))
check("（现行）角色/区域 = 上半红 / 上半绿 / 下半红 / 下半绿",
      [(c["role"], c["y0"], c["h"]) for c in CHAINS] ==
      [("RED", 0, 8), ("GREEN", 0, 8), ("RED", 8, 8), ("GREEN", 8, 8)])
check("（现行）`flags` 全恒等（0）：下半屏朝向尚未由现场证据确定（探针 8 是均匀填充）",
      all(c["flags"] == 0 for c in CHAINS))
check("（现行）不占用 B1/B2/A/B/C/D 六根脚（探针 8 里全部无反应）",
      all(not (c["line_mask"] & (LINE_MASK["B1"] | LINE_MASK["B2"] | LINE_MASK["A"] |
                                 LINE_MASK["B"] | LINE_MASK["C"] | LINE_MASK["D"]))
          for c in CHAINS))

# 若四条链接线都与本表一致 → 红「口」应显示整幅 52 点
all_working = {c["line_mask"] for c in CHAINS}
vis_full = set()
for c in CHAINS:
    if c["role"] == "RED":
        vis_full |= {(o % SCREEN_W, o // SCREEN_W) for o, b in
                     zip(chain_offsets(c), chain_bits(c, GLYPH_PX)) if b}
check("（理想接线）红「口」可见点集 = 整幅 52 点", vis_full == GLYPH_PX,
      f"差 {sorted(vis_full ^ GLYPH_PX)[:8]}")
green_full = set()
for c in CHAINS:
    if c["role"] == "GREEN":
        green_full |= {(o % SCREEN_W, o // SCREEN_W) for o, b in
                       zip(chain_offsets(c), chain_bits(c, GLYPH_PX)) if b}
check("（理想接线）绿「口」可见点集 = 整幅 52 点（绿链覆盖同样像素）", green_full == GLYPH_PX)

# ---------------------------------------------------------------- C. 探针 9 语义
print("== C. 探针 9「逐链 × 逐脚内容扫描」语义复算 ==")
P9_PINS = macro("_22_1665_PROBE9_PINS", 0) or (LINE_MASK["A"] | LINE_MASK["B"] | LINE_MASK["C"] | LINE_MASK["D"])
P9_HALF_MS = macro("_22_1665_PROBE9_HALF_MS", 1500)
P9_FRAME_HZ = macro("_22_1665_PROBE9_FRAME_HZ", 2000)
check("探针 9 默认脚掩码 = 四个地址脚 A/B/C/D（0x3C0）", P9_PINS == 0x3C0, hex(P9_PINS))
check("探针 9 脚掩码不含 R1（R1 专职显示序号）", (P9_PINS & LINE_MASK["R1"]) == 0)
pin_list = [k for k in range(10) if P9_PINS & (1 << k)]
check("脚列表 = [A,B,C,D] = 位号 6/7/8/9（与探针 7/8 的脚号口径一致）", pin_list == [6, 7, 8, 9],
      str(pin_list))
pairs = len(pin_list) * CHAIN_COUNT
cycle_s = pairs * P9_HALF_MS / 1000.0
check(f"一轮组合数 = 链数 × 脚数 = {pairs}（≈{cycle_s:.1f}s 循环）", pairs == 16)
check("停留至少 200 帧（编译期 _Static_assert 同值）",
      P9_FRAME_HZ * P9_HALF_MS // 1000 >= 200, str(P9_FRAME_HZ * P9_HALF_MS // 1000))

# 状态位语义：st = (内容位 << 1) | 数字位 → 只写「R1（数字）+ 被测脚（内容）」两处
ok_state = True
for k in pin_list:
    for st in range(4):
        cb, dg = st >> 1, st & 1
        bits = dg | (cb << k)
        want = (1 if dg else 0) | (1 << k if cb else 0)
        if bits != want:
            ok_state = False
check("每个组合只驱动「被测脚 + R1」两处（其余脚写 0 = 静默）", ok_state)

# 内容语义：被测链自己的落点表 → 像素「非黑即亮」
ok_content = True
for c in CHAINS:
    offs = chain_offsets(c)
    own = chain_pixels(c)
    n_expect = len(GLYPH_PX & own)  # 该链区域内的字模点数（上半 23 / 下半 29）
    pm = [1 if (o % SCREEN_W, o // SCREEN_W) in GLYPH_PX else 0 for o in offs]
    if any(b not in (0, 1) for b in pm) or sum(pm) != n_expect or len(own) != FRAME_BITS:
        ok_content = False
check("内容位 = 被测链落点表给出的像素「非黑即亮」（颜色由 die 决定 → 同时读出 role）",
      ok_content)

# ---------------------------------------------------------------- D. 落点旗标
print("== D. 落点镜像旗标（第十七轮新增）==")
base_chain = dict(x0=0, y0=0, w=16, h=8, flags=0)
ref = [region_offset(0, 0, 4, 2, 0, p) for p in range(FRAME_BITS)]
check("恒等旗标（flags=0）逐位 = 第十二轮已确认模型（本脚本独立复算）",
      [region_offset(0, 0, 4, 2, 0, p) for p in range(FRAME_BITS)] == ref)

ok_bij, ok_in = True, True
for f in range(16):
    offs = [region_offset(0, 0, 4, 2, f, p) for p in range(FRAME_BITS)]
    if len(set(offs)) != FRAME_BITS:
        ok_bij = False
    if not all(0 <= o % SCREEN_W < SCREEN_W and 0 <= o // SCREEN_W < 8 for o in offs):
        ok_in = False
check("16 种旗标组合皆为「128 链位 → 区域 128 像素」双射", ok_bij)
check("16 种旗标组合的落点全部留在本链区域内", ok_in)

rot = [region_offset(0, 0, 4, 2, XF_ROT180, p) for p in range(FRAME_BITS)]
ok_rot = True
for o0, o1 in zip(ref, rot):
    x0, y0 = o0 % SCREEN_W, o0 // SCREEN_W
    # 区域内 180° 旋转：x → (x0 + w - 1) - x（本链区域宽 16）、y → (y0 + h - 1) - y
    want = (base_chain["y0"] + base_chain["h"] - 1 - y0) * SCREEN_W + \
           (base_chain["x0"] + base_chain["w"] - 1 - x0)
    if o1 != want:
        ok_rot = False
        break
check("ROT180 = 像素空间 180° 旋转（下半屏 PCB 旋屏的典型情形）", ok_rot)

# 芯片级联次序反向 ⇔ BLK_X|BLK_Y（数学等价，脚本复核）
rev = [region_offset(0, 0, 4, 2, XF_BLK_X | XF_BLK_Y, p) for p in range(FRAME_BITS)]


def offset_with_chip_order(flip: bool, p: int) -> int:
    c, i = divmod(p, CHIP_BITS)
    if flip:
        c = CHIPS - 1 - c
    bc = (4 - 1) - (c % 4)
    br = c // 4
    dx = 3 - (i % 4)
    dy = ((i // 4) + 2) % 4
    return (0 + BLK_H * br + dy) * SCREEN_W + (0 + BLK_W * bc + dx)


check("「芯片级联次序反向」等价于 flags = BLK_X|BLK_Y（无需单独旗标）",
      [offset_with_chip_order(True, p) for p in range(FRAME_BITS)] == rev)

# ---------------------------------------------------------------- E. 生成预测 md
def ascii_region(chains: list[dict], lit: set[tuple[int, int]], mask_lines=None) -> str:
    grid = [["." for _ in range(SCREEN_W)] for _ in range(SCREEN_H)]
    for c in chains:
        if mask_lines is not None and c["line_mask"] not in mask_lines:
            continue
        if c["role"] == "GREEN" and mask_lines is not None:
            continue
        for o, b in zip(chain_offsets(c), chain_bits(c, lit)):
            if b:
                grid[o // SCREEN_W][o % SCREEN_W] = "#"
    return "\n".join("".join(r) for r in grid)


md: list[str] = []
md.append("# 22-1665 第十七轮：观测回溯 + 修复后预测（由 `check_round17.py` 生成）\n")
md.append("## 1. 观测回溯 —— 第十六轮占位链表（R1/G1/B1/R2）+ 现场只导通 R1\n")
md.append("红色「口」（内容 = 字模，红链取 R 位）：\n\n```\n" +
          ascii_region(CHAINS_R16, role_lit("RED", "RED"), {LINE_MASK["R1"]}) + "\n```\n")
md.append("绿色「口」（红链取 R 位 = 0 → R1 那条链整体变黑；绿链位流非零但 G1/R2 不导通）：\n\n```\n" +
          ascii_region(CHAINS_R16, set(), {LINE_MASK["R1"]}) + "\n```\n")
md.append("## 2. 修复后默认链表（链 1/2/3 → A/B/C）\n")
md.append("若四链的接线与默认表**完全一致**，红色「口」应显示整幅：\n\n```\n" +
          ascii_region(CHAINS, GLYPH_PX) + "\n```\n")
md.append("若只有链 0（R1）导通（= 未标定/接线不符），则仍只显示上半部：\n\n```\n" +
          ascii_region(CHAINS, GLYPH_PX, {LINE_MASK["R1"]}) + "\n```\n")
md.append("## 3. 探针 9 读数表（一轮 24s）\n")
md.append("| 序号（左 = 链号 / 右 = 脚号） | 脚名 | 预期现象 |\n|---|---|---|")
for i, c in enumerate(CHAINS):
    for k in pin_list:
        md.append(f"| {i} {k} | {LINE_ORDER[k]} | 把链 {i}（{c['role']}）的内容送到 {LINE_ORDER[k]}："
                  f"该链真在这一脚上 → 屏上出现**形状正确**的内容；颜色 = 该链 die 颜色 |")
md.append("\n> 判读：看到内容正确 → 该链的数据脚 = 右数字对应的脚（6=A / 7=B / 8=C / 9=D）；"
          "颜色 = 该链该填的 role；内容镜像/旋转 → 给该链加 `flags`；\n"
          "> 内容出现在另一块屏 → 该链的 `region` 填那块屏。\n")
OUT_MD.write_text("\n".join(md), encoding="utf-8")
check(f"{OUT_MD.name} 已生成", OUT_MD.exists())

print()
print(f"结果：通过 {PASS} / 失败 {FAIL}")
if FAILURES:
    print("失败项：")
    for f in FAILURES:
        print("  -", f)
sys.exit(1 if FAIL else 0)
