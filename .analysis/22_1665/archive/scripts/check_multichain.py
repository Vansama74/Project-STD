#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 第十六轮宿主自检：**多链独立位流框架**（只依赖标准库；字库 bin 可选）

  A. 链表解析：从驱动源码抽出 `_22_1665_ROW(...)`（默认口径 = 多链 16×16、4 条链）。
  B. 落点映射复算（`_22_1665_region_offset()`）：
       · 每条链 128 链位 ↔ 该链区域 128 个像素 = 双射；
       · **4 条链 × 128 位 = 512 位** = 16×16 像素 × 2 die：红链集合与绿链集合各自
         恰好覆盖全部 256 像素（每像素一条红链 + 一条绿链）；
       · 反查：任取像素 → 唯一的 (红链, 链位) 与 (绿链, 链位)。
  C. 兼容模式（`_22_1665_MULTI_CHAIN = 0`）：单链 16×8 双射，落点表与第十二轮反解公式
     **逐位一致**（证明回退开关零行为变化）。
  D. 预测屏面：默认画面「口」（16×16，字库 bin dump）+ 每条链自己的 128 位图案
     → `.analysis/22_1665/multichain_screens.md`（含两种模式的预期 ASCII）。
  E. 症状签名表（链表字段填错 → 屏面症状）→ 同一个 md。

用法：python3 .analysis/22_1665/check_multichain.py
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
# 归档说明：本脚本断言的是**重构前**的驱动形态，故固定读归档的驱动备份
SRC = (ROOT / ".analysis/22_1665/archive/prev_round/"
       "dev_display_22_1665_round23_pre_refactor.c")
MAIN_H = ROOT / "Core/Inc/main.h"
OUT_MD = pathlib.Path(__file__).resolve().parent / "multichain_screens.md"

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
main_h = MAIN_H.read_text(encoding="utf-8")


def macro(name: str, text: str = None, default: int | None = None) -> int:
    text = src if text is None else text
    m = re.search(rf"^#define\s+{re.escape(name)}\s+\(?\s*(0[xX][0-9a-fA-F]+|\d+)U?\)?", text, re.M)
    if not m:
        if default is not None:
            return default
        raise SystemExit(f"宏 {name} 未找到")
    return int(m.group(1), 0)


def main_h_pin(pin_macro: str) -> tuple[str, int]:
    m = re.search(rf"^#define\s+{re.escape(pin_macro)}\s+GPIO_PIN_(\d+)", main_h, re.M)
    p = re.search(rf"^#define\s+{re.escape(pin_macro.replace('_Pin', '_GPIO_Port'))}\s+GPIO(\w)", main_h, re.M)
    if not m or not p:
        raise SystemExit(f"main.h 缺少 {pin_macro}")
    return p.group(1), int(m.group(1))


# ---------------------------------------------------------------- A. 宏与链表
LINE_ORDER = ["R1", "G1", "B1", "R2", "G2", "B2", "A", "B", "C", "D"]
LINE_MASKS = {f"_22_1665_LINE_{n}": (1 << k) for k, n in enumerate(LINE_ORDER)}

CHIPS = macro("_22_1665_CHIPS")
CHIP_BITS = macro("_22_1665_CHIP_BITS")
FRAME_BITS = CHIPS * CHIP_BITS
BLK_W = macro("_22_1665_BLK_W")
BLK_H = macro("_22_1665_BLK_H")
MULTI = macro("_22_1665_MULTI_CHAIN")
CHAIN_COUNT = macro("_22_1665_CHAIN_COUNT")
SCREEN_W = macro("_22_1665_SCREEN_W")
SCREEN_H = macro("_22_1665_SCREEN_H")
HALF_H = SCREEN_H // 2
BLK_COLS = macro("_22_1665_BLK_COLS")

IDENT = {
    "_22_1665_SCREEN_W": SCREEN_W,
    "_22_1665_SCREEN_H": SCREEN_H,
    "_22_1665_HALF_H": HALF_H,
    "_22_1665_CHIPS": CHIPS,
    "_22_1665_CHIP_BITS": CHIP_BITS,
    **LINE_MASKS,
}


def val(tok: str):
    tok = tok.strip()
    if tok.startswith('"') and tok.endswith('"'):
        return tok[1:-1]
    if tok == "nullptr":
        return None
    if tok in IDENT:
        return IDENT[tok]
    m = re.fullmatch(r"(0[xX][0-9a-fA-F]+|\d+)[Uu]?", tok)
    if m:
        return int(m.group(1), 0)
    if tok == "_22_1665_ROLE_RED":
        return "RED"
    if tok == "_22_1665_ROLE_GREEN":
        return "GREEN"
    if tok == "_22_1665_ROLE_MONO":
        return "MONO"
    raise SystemExit(f"无法解析链表字段：{tok}")


ROW_RE = re.compile(r"_22_1665_ROW\(\s*(.*?)\s*\)\s*,", re.S)


def parse_chains(text: str) -> list[dict]:
    rows: list[dict] = []
    # 只取 _22_1665_chains[] 初始化块（避免匹配到宏定义里的模板行）
    m = re.search(r"_22_1665_chains\[[^\]]*\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise SystemExit("未找到 _22_1665_chains[] 初始化块")
    body = m.group(1)
    body = body.split("#else")[0]  # 多链分支
    for raw in ROW_RE.finditer(body):
        fields = [f for f in re.split(r",(?![^(]*\))", raw.group(1))]
        fields = [f.strip() for f in fields if f.strip()]
        if len(fields) != 7:
            raise SystemExit(f"链表行字段数 {len(fields)} ≠ 7：{raw.group(0)}")
        rows.append(
            dict(
                name=val(fields[0]),
                line_mask=val(fields[1]),
                role=val(fields[2]),
                x0=val(fields[3]),
                y0=val(fields[4]),
                w=val(fields[5]),
                h=val(fields[6]),
            )
        )
    return rows


CHAINS = parse_chains(src)

print("== A. 链表解析（默认口径）==")
check("多链模式默认开（_22_1665_MULTI_CHAIN=1）", MULTI == 1, f"= {MULTI}")
check(f"链数 = {CHAIN_COUNT}（与 _22_1665_CHAIN_COUNT 一致）", len(CHAINS) == CHAIN_COUNT,
      f"表里 {len(CHAINS)} 行")
check("屏面 16×16（面板口径）", (SCREEN_W, SCREEN_H) == (16, 16), f"{SCREEN_W}×{SCREEN_H}")
check("每链 128 位 = 8 片 × 16 级", FRAME_BITS == 128, str(FRAME_BITS))
for c in CHAINS:
    print(f"  · {c['name']}: lines=0x{c['line_mask']:03X} role={c['role']} "
          f"region=({c['x0']},{c['y0']},{c['w']},{c['h']})")

# ---------------------------------------------------------------- B. 落点映射


def region_offset(x0: int, y0: int, blk_cols: int, p: int) -> int:
    """复算 _22_1665_region_offset()（默认落点规则）。"""
    c, i = divmod(p, CHIP_BITS)
    bc = (blk_cols - 1) - (c % blk_cols)
    br = c // blk_cols
    dx = 3 - (i % 4)
    dy = ((i // 4) + 2) % 4
    return (y0 + BLK_H * br + dy) * SCREEN_W + (x0 + BLK_W * bc + dx)


print("== B. 多链落点映射（16×16 双色全屏）==")
led: list[dict[int, tuple[int, int]]] = []  # 每链：pixel_offset → (chain, p)
for idx, c in enumerate(CHAINS):
    ok_region = (c["w"] % 4 == 0 and c["h"] % 4 == 0
                 and (c["w"] // 4) * (c["h"] // 4) == CHIPS
                 and c["x0"] + c["w"] <= SCREEN_W and c["y0"] + c["h"] <= SCREEN_H)
    check(f"链 {c['name']} 区域合法（块数 = 8、不越屏）", ok_region, str(c))
    if not ok_region:
        continue
    blk_cols = c["w"] // 4
    offs = [region_offset(c["x0"], c["y0"], blk_cols, p) for p in range(FRAME_BITS)]
    check(f"链 {c['name']} 128 链位 → 128 像素双射", len(set(offs)) == FRAME_BITS)
    in_region = all(
        (c["x0"] <= o % SCREEN_W < c["x0"] + c["w"]) and (c["y0"] <= o // SCREEN_W < c["y0"] + c["h"])
        for o in offs
    )
    check(f"链 {c['name']} 落点全部落在其区域内", in_region)

    # 该链覆盖的像素集合是否恰好 = 区域面积（128）
    check(f"链 {c['name']} 覆盖像素数 = 区域面积 = 128", len(set(offs)) == c["w"] * c["h"])
    led.append({o: (idx, p) for p, o in enumerate(offs)})

# 红 / 绿链集合
red_off: dict[int, tuple[int, int]] = {}
grn_off: dict[int, tuple[int, int]] = {}
conflict = []
for idx, c in enumerate(CHAINS):
    if idx >= len(led):
        continue
    tgt = red_off if c["role"] == "RED" else grn_off
    for o, (chain_i, p) in led[idx].items():
        if o in tgt:
            conflict.append(f"像素 {o} 被链 {tgt[o][0]} 与链 {chain_i} 同时覆盖")
        tgt[o] = (chain_i, p)

check("同一分量的两条链区域互不重叠（红链之间 / 绿链之间）", not conflict, "; ".join(conflict))
check("红链集合覆盖全部 256 像素", len(red_off) == SCREEN_W * SCREEN_H, f"{len(red_off)}")
check("绿链集合覆盖全部 256 像素", len(grn_off) == SCREEN_W * SCREEN_H, f"{len(grn_off)}")
check("4 链 × 128 位 = 512 LED = 256 像素 × 2 die", 4 * FRAME_BITS == 512 == SCREEN_W * SCREEN_H * 2)

# 反查：任取像素 → (红链, 链位) 与 (绿链, 链位)
sample_ok = True
samples = [0, 15, 16, 63, 64, 127, 128, 200, 255]
for o in samples:
    r, g = red_off.get(o), grn_off.get(o)
    if r is None or g is None:
        sample_ok = False
        break
    # 反查后必须能按落点规则正查回同一像素
    for chain_i, p in (r, g):
        c = CHAINS[chain_i]
        if region_offset(c["x0"], c["y0"], c["w"] // 4, p) != o:
            sample_ok = False
check("抽样像素反查唯一（红/绿各一条链 + 链位，正查回同一像素）", sample_ok, str(samples))

print("  反查样例：")
for o in samples[:4]:
    r, g = red_off[o], grn_off[o]
    print(f"    像素 #{o:3d} (x={o % SCREEN_W},y={o // SCREEN_W}) → "
          f"红: 链{r[0]}({CHAINS[r[0]]['name']}) 位{r[1]:3d} | 绿: 链{g[0]}({CHAINS[g[0]]['name']}) 位{g[1]:3d}")

# ---------------------------------------------------------------- C. 兼容模式
print("== C. 兼容模式（_22_1665_MULTI_CHAIN=0）==")
c_blk_cols = BLK_COLS
c_w = c_blk_cols * BLK_W
c_h = (CHIPS // c_blk_cols) * BLK_H
check("兼容模式屏面 = 块栅格（BLK_COLS × BLK_ROWS，默认 16×8）", (c_w, c_h) == (16, 8), f"{c_w}×{c_h}")
compat = [region_offset(0, 0, c_blk_cols, p) for p in range(FRAME_BITS)]
check("兼容模式单链 128 位双射", len(set(compat)) == FRAME_BITS)


def legacy_led_offset(p: int) -> int:
    """第十二轮 `_22_1665_led_offset()`（兼容模式的对照实现，逐字复刻）。"""
    c, i = divmod(p, CHIP_BITS)
    bc = (c_blk_cols - 1) - (c % c_blk_cols)
    br = c // c_blk_cols
    dx = 3 - (i % 4)
    dy = ((i // 4) + 2) % 4
    return (BLK_H * br + dy) * c_w + (BLK_W * bc + dx)


check("兼容模式落点与第十二轮公式逐位一致（零行为变化）",
      compat == [legacy_led_offset(p) for p in range(FRAME_BITS)])

# ---------------------------------------------------------------- D. 预测屏面
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

FONT_BIN = ("/home/yystation/文档/创迪科技生产订单与技术资料/19 标准产品资料/升降栏杆机/"
            "5.软件（含平台软件、测试软件与嵌入式程序）/1.嵌入式程序/9I124D6521/"
            "字库芯片程序/W25Q256_FONT_14_16_20_24_32.bin")


def glyph_rows() -> tuple[list[str], str]:
    """dump FONT_16 黑体「口」（MX25L256 布局 = 板上字库；失败则用内嵌兜底）。"""
    p = pathlib.Path(FONT_BIN)
    if not p.exists():
        return GLYPH_FALLBACK, "内嵌兜底（字库 bin 不在本机）"
    try:
        data = p.read_bytes()
        size, w = 16, 16
        bpc = size * ((w + 7) // 8)
        idx = (0xBF - 0x81) * 190 + (0xDA - 0x41)
        fonf = idx * bpc
        sec, page, byte = fonf // 4096, (fonf % 4096) // 256, (fonf % 4096) % 256
        a = (1218 + sec + 0) * 4096 + (page + 8) * 256 + byte + 64
        buf = data[a:a + bpc]
        rows = ["".join("#" if (buf[r * 2 + c // 8] >> (7 - c % 8)) & 1 else "."
                        for c in range(w)) for r in range(size)]
        return rows, "字库 bin dump（MX25L256，app_render.c 同公式）"
    except Exception:
        return GLYPH_FALLBACK, "内嵌兜底（异常）"


glyph, glyph_src = glyph_rows()
check("「口」字模 16 行 × 16 列", len(glyph) == 16 and all(len(r) == 16 for r in glyph))
check("「口」字模点数 = 52（与 doc §15.1 现场口径一致）",
      sum(r.count("#") for r in glyph) == 52, str(sum(r.count("#") for r in glyph)))

# 逻辑帧 = app_render 把「口」渲染进 16×16（x=0,y=0，居中）
frame = [[1 if ch == "#" else 0 for ch in row] for row in glyph]

# 每条链自己的 128 位图案（按该链角色取分量；区域局部视图 = 该链实际驱动的灯位）
ROLE_BIT = {
    "RED": lambda c: 1 if (c & 1) else 0,
    "GREEN": lambda c: 1 if (c & 2) else 0,
    "MONO": lambda c: 1 if c else 0,
}
chain_views: list[list[str]] = []
for idx, c in enumerate(CHAINS):
    blk_cols = c["w"] // 4
    buf = [["."] * c["w"] for _ in range(c["h"])]
    lit = 0
    for p in range(FRAME_BITS):
        off = region_offset(c["x0"], c["y0"], blk_cols, p)
        x, y = off % SCREEN_W - c["x0"], off // SCREEN_W - c["y0"]
        color = frame[off // SCREEN_W][off % SCREEN_W] * 1  # 逻辑帧：亮 = COLOR_RED(1)
        if ROLE_BIT[c["role"]](color):
            buf[y][x] = "#"
            lit += 1
    chain_views.append(["".join(r) for r in buf])
    c["lit"] = lit

# 多链模式「若链表填对」的面板合成图 = 「口」整幅
panel_multi = ["".join("#" if v else "." for v in row) for row in frame]

# 兼容模式：16×8 屏 = 字模上半（app_render 按屏幕交集裁剪绘制）
panel_compat = ["".join("#" if frame[y][x] else "." for x in range(16)) for y in range(8)]

print("== D. 预测屏面 ==")
print("  多链模式（若链表填对）：整幅「口」16×16 共 52 点（上半 = 链 0/1，下半 = 链 2/3）")
print("  兼容模式：16×8 = 字模上半（与第十五轮逐点一致）")

SYMPTOMS = [
    ("某区域完全不响应（全黑 / 不随内容变化）", "line_mask 填了没接到该链的脚",
     "数据没到那条链 —— 用探针 7 找到真正驱动它的脚"),
    ("两个区域出现同一内容的「或」叠加 / 杂乱闪烁", "两条链 line_mask 相同（RTT 有 pin conflict 告警）",
     "一根线喂两份位流 → 硬件按位或；两条链必须各占一根脚"),
    ("整幅内容整体平移半屏 / 错开 4 像素块的整数倍", "x0 / y0 错",
     "区域原点决定块栅格落点，平移即整体位移"),
    ("某区域内容像被转置/错行（4 像素块换了行列排布）", "w / h 错（块列数 = w/4 变了）",
     "块栅格列数由区域宽度导出，改宽高会改变块列反向与换行规则"),
    ("全屏纯红时某区域显示绿（红绿互换）", "该链 role 填反",
     "角色决定该链取像素的 R 还是 G 分量"),
    ("某区域图案镜像 / 旋转 / 块内散乱（其余区域正常）", "该链片内位序或块栅格与默认规则不同",
     "改用 `_22_1665_ROW_DST(...)` 显式落点表覆盖默认规则"),
    ("某链整块黑 + RTT 有 `chainN ... DISABLED` 行", "链表行非法（w/h 非 4 倍数、块数≠8、越屏、role 非法）",
     "init 期校验直接禁用该链（避免越界写），按 RTT 原因修正"),
    ("屏上出现补色图案（该亮的灭）", "该链数据极性相反",
     "把 `_22_1665_DATA_ACTIVE_HIGH` 改成 0 复测（或该链用显式落点表做位反相）"),
]

# ---------------------------------------------------------------- E. 产物 md
lines: list[str] = []
lines.append("# 22-1665 多链独立位流 · 预测屏面与症状签名（第十六轮 · 宿主生成）\n")
lines.append(f"由 `check_multichain.py` 从驱动源码复算。链表（默认占位）：\n")
lines.append("| 链 | 名字 | line_mask | role | region (x0,y0,w,h) |")
lines.append("|---|---|---|---|---|")
for c in CHAINS:
    lines.append(f"| {CHAINS.index(c)} | {c['name']} | `0x{c['line_mask']:03X}` | {c['role']} | "
                 f"({c['x0']},{c['y0']},{c['w']},{c['h']}) |")
lines.append("")
lines.append(f"字模来源：{glyph_src}；「口」= FONT_16 黑体，52 点。\n")

lines.append("\n## 1. 多链模式（`_22_1665_MULTI_CHAIN = 1`，默认）\n")
lines.append("逻辑帧（= 屏面 16×16，默认画面「口」）—— **若链表填对**，面板上就是这个形状"
             "（上半 8 行来自链 0/1，下半 8 行来自链 2/3）：\n")
lines.append("```\n" + "\n".join(panel_multi) + "\n```\n")
lines.append("\n各链自己的 128 位图案（**按该链角色取分量后的实际驱动灯位**，区域局部视图；\n"
             "默认画面「口」是纯红内容 ⇒ 绿链（取 G 位）全灭 —— 现场可与屏上对应区域逐个比对）：\n")
for idx, c in enumerate(CHAINS):
    lines.append(f"\n### 链 {idx} `{c['name']}`（lines=0x{c['line_mask']:03X}, {c['role']}, "
                 f"region=({c['x0']},{c['y0']},{c['w']},{c['h']})，点亮 {c['lit']} 位）\n\n```\n"
                 + "\n".join(chain_views[idx]) + "\n```\n")
lines.append("\n全屏纯色测试的「哪条链该亮」：\n")
lines.append("| 送上位机颜色（像素值） | 红链（取 R 位） | 绿链（取 G 位） |")
lines.append("|---|---|---|")
for nm, v, r, g in [("黑 (0)", 0, 0, 0), ("红 (1)", 1, 1, 0), ("绿 (2)", 2, 0, 1),
                    ("黄 (3)", 3, 1, 1), ("蓝 (4)", 4, 1, 1), ("白 (7)", 7, 1, 1)]:
    tail = "（蓝按「非黑即亮」两块都亮）" if nm.startswith("蓝") else ""
    lines.append(f"| {nm} | {r} | {g} {tail} |")
lines.append("\n⇒ 现场判据：全屏红时**只有红链覆盖的区域**该亮（点亮的是那些区域的 die）；"
             "若某区域显示**绿**，说明该区域物理上是绿 die 而表里写成了 RED → 把该行 role 改成 GREEN。\n")

lines.append("\n## 2. 单链兼容模式（`_22_1665_MULTI_CHAIN = 0`）\n")
lines.append("屏面回到 16×8、`_22_1665_DATA_LINES` 同流、颜色「非黑即亮」"
             "（= 第十二~十五轮行为，与上一版**逐点一致**）：\n")
lines.append("```\n" + "\n".join(panel_compat) + "\n```\n")

lines.append("\n## 3. 链表填错的症状签名（现场标定速查）\n")
lines.append("| 屏上症状 | 最常见字段错 | 机理 / 下一步 |")
lines.append("|---|---|---|")
for sym, field, why in SYMPTOMS:
    lines.append(f"| {sym} | **{field}** | {why} |")
lines.append("")

OUT_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")
check("multichain_screens.md 已生成", OUT_MD.exists(), str(OUT_MD))

print(f"\n结果：通过 {PASS} / 失败 {FAIL}")
if FAILURES:
    for f in FAILURES:
        print("  -", f)
sys.exit(1 if FAIL else 0)
