#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 第二十轮宿主自检：**现场探针 8 读数 → 四链定标（R1 / G1 / R2 / G2）**

  A. **源码事实核实**（本轮推理的立足点，全部从源码/`main.h`/`pl_hub75.c` 实读）：
     ① 探针 8 的脚索引顺序 k = 0..9 ↔ R1/G1/B1/R2/G2/B2/A/B/C/D
        （`_22_1665_line_names[]` + `_22_1665_line_pin()` 的 k/3、k%3 推导 + main.h 端口位号）；
     ② 探针 8 的参与脚 = `_22_1665_DATA_LINES | R1`，状态位由被测脚序号 `s_probe8_k` 直接给出
        ⇒ **绕过链表逐脚驱动**（这正是它能看到 G1/R2/G2 真相的原因）；
     ③ 多链模式下探针 5/6 的状态位**只有 bit0** ⇒ 只拉高**链 0** 的脚
        （「十脚恒定全 1」的口径只在兼容模式 `MULTI_CHAIN=0` 下成立）；
     ④ 探针 0 = 正常显示 = 链表路径（红内容只喂 red 角色链、绿内容只喂 green 角色链）。
  B. **现场三条观测编码**（用户回报，逐字 → 断言）：
     ① `CHAIN_PROBE=0`：红「口」只在上半屏（y0-7），y≥8 全黑（旧表 R1/A/B/C 口径）；
     ② `CHAIN_PROBE=5`：上半屏 y0-7 红灯全亮、所有绿灯不亮（多链探针 5 = 只驱动链 0）；
     ③ `CHAIN_PROBE=8`：k=1 → 上半屏绿；k=3 → 下半屏红；k=4 → 下半屏绿；
        k=2 与 k=5..9 → 无反应（背景不亮）。
  C. **由读数解出的链表 = 源码默认表**（逐字段：掩码/角色/区域）+ `flags` 恒等
     + `_22_1665_CHAIN_PROBE` 默认值回到 `0U`。
  D. **新表下的位流/可见点集**：红「口」= 整幅 52 点（R1 上半 23 + R2 下半 29）；
     绿「口」= 整幅 52 点（G1/G2）；红内容不进绿链、绿内容不进红链（分量隔离）。
  E. **落点双射**：四条链各自 128 链位 ↔ 本链区域 128 像素（`flags` 恒等）；
     红链并集 / 绿链并集各覆盖 16×16 全屏 256 像素。
  F. 生成 `round20_screens.md`（新表预期 ASCII + 脚索引表 + 现场复验清单）。

用法：python3 .analysis/22_1665/check_round20.py
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
PL_HUB75 = ROOT / "Platform/Src/pl_hub75.c"
OUT_MD = pathlib.Path(__file__).resolve().parent / "round20_screens.md"

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
pl_hub75 = PL_HUB75.read_text(encoding="utf-8")


SYMS = {"_22_1665_LINE_ALL": 0x3FF, "_22_1665_ADDR_LINES": 0x3C0, "_22_1665_RGB_LINES": 0x03F}


def macro(name: str, default: int | None = None) -> int:
    m = re.search(rf"^#define\s+{re.escape(name)}\s+\(?\s*(0[xX][0-9a-fA-F]+|\d+)U?\)?", src, re.M)
    if m:
        return int(m.group(1), 0)
    m2 = re.search(rf"^#define\s+{re.escape(name)}\s+\(?\s*([A-Za-z_][A-Za-z_0-9]*)\s*\)?", src, re.M)
    if m2 and m2.group(1) in SYMS:
        return SYMS[m2.group(1)]
    if default is not None:
        return default
    raise SystemExit(f"宏 {name} 未找到")


# ---------------------------------------------------------------- 常量
CHIPS = macro("_22_1665_CHIPS")
CHIP_BITS = macro("_22_1665_CHIP_BITS")
FRAME_BITS = CHIPS * CHIP_BITS
BLK_W = macro("_22_1665_BLK_W")
BLK_H = macro("_22_1665_BLK_H")
CHAIN_COUNT = macro("_22_1665_CHAIN_COUNT")
SCREEN_W = macro("_22_1665_SCREEN_W")
SCREEN_H = macro("_22_1665_SCREEN_H")
HALF_H = SCREEN_H // 2
PROBE_DEFAULT = macro("_22_1665_CHAIN_PROBE")
DATA_LINES_DEFAULT = macro("_22_1665_DATA_LINES")
MULTI = macro("_22_1665_MULTI_CHAIN")

LINE_ORDER = ["R1", "G1", "B1", "R2", "G2", "B2", "A", "B", "C", "D"]
LINE_MASK = {n: (1 << k) for k, n in enumerate(LINE_ORDER)}
ADDR_PINS = ["A", "B", "C", "D"]

XF_X, XF_Y, XF_BLK_X, XF_BLK_Y = 0x1, 0x2, 0x4, 0x8
XF_ROT180 = XF_X | XF_Y | XF_BLK_X | XF_BLK_Y

# 现场探针 8 读数（用户回报，逐字）：k → (背景颜色, 背景所在区域)
P8_FIELD = {
    1: ("GREEN", "upper"),   # 数字「1」红、背景绿色显示在上半屏
    3: ("RED", "lower"),     # 数字「3」红、背景红色显示在下半屏
    4: ("GREEN", "lower"),   # 数字「4」红、背景绿色显示在下半屏
}
P8_SILENT = [2, 5, 6, 7, 8, 9]  # k=2(B1) / k=5(B2) / k=6..9(A..D)：背景不亮
P8_K0_REPORTED = False          # k=0（R1）用户未单独回报（其导通性由观测①②独立确认）

# 旧默认表（第十七轮推定，现场那版固件用的就是它；本轮定标后已被取代）——历史留档
CHAINS_R17 = [
    dict(name="C0", line_mask=LINE_MASK["R1"], role="RED", x0=0, y0=0, w=16, h=8),
    dict(name="C1", line_mask=LINE_MASK["A"], role="GREEN", x0=0, y0=0, w=16, h=8),
    dict(name="C2", line_mask=LINE_MASK["B"], role="RED", x0=0, y0=8, w=16, h=8),
    dict(name="C3", line_mask=LINE_MASK["C"], role="GREEN", x0=0, y0=8, w=16, h=8),
]

# 现场读数解出的真实接线（含 R1 的独立确认）：脚 → (die 颜色, 区域)
DECODED = {
    "R1": ("RED", "upper"),   # 观浏①②：R1 = 上半屏红（第十二/十七/二十轮三度确认）
    "G1": ("GREEN", "upper"), # 探针 8 k=1
    "R2": ("RED", "lower"),   # 探针 8 k=3
    "G2": ("GREEN", "lower"), # 探针 8 k=4
}


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

FONT_BIN = ("/home/yystation/文档/创迪科技生产订单与技术资料/19 标准产品资料/"
            "收费站多功能一体机-标准资料整理v2.0/5.软件（含平台软件、测试软件与嵌入式程序）/"
            "1.嵌入式程序/9J9F2E4230(23-543)/字库芯片程序/贵州P10双色治超屏字库")
F16_HT = dict(base=209, X=0, Y=2, Z=96, idx=lambda h, l: (h - 0xA1) * 94 + (l - 0xA1))


def glyph_rows() -> list[str]:
    p = pathlib.Path(FONT_BIN)
    if not p.exists():
        return GLYPH_FALLBACK
    try:
        data = p.read_bytes()
        size, w = 16, 16
        bpc = size * ((w + 7) // 8)
        idx = F16_HT["idx"](0xBF, 0xDA)
        fonf = idx * bpc
        sec, page, byte = fonf // 4096, (fonf % 4096) // 256, (fonf % 4096) % 256
        a = ((F16_HT["base"] + sec + F16_HT["X"]) * 4096 + (page + F16_HT["Y"]) * 256
             + byte + F16_HT["Z"])
        buf = data[a:a + bpc]
        if len(buf) < bpc:
            return GLYPH_FALLBACK
        return ["".join("#" if (buf[r * 2 + c // 8] >> (7 - c % 8)) & 1 else "."
                        for c in range(w)) for r in range(size)]
    except Exception:
        return GLYPH_FALLBACK


GLYPH = glyph_rows()
GLYPH_PX = {(x, y) for y, row in enumerate(GLYPH) for x, ch in enumerate(row) if ch == "#"}
check("字模「口」= 52 点（第十二轮反解的锚，按已知地址公式 dump）", len(GLYPH_PX) == 52,
      f"{len(GLYPH_PX)} 点")
GLYPH_UP = {(x, y) for (x, y) in GLYPH_PX if y < HALF_H}
GLYPH_DN = {(x, y) for (x, y) in GLYPH_PX if y >= HALF_H}
check("字模上半（y<8）= 23 点、下半（y≥8）= 29 点", len(GLYPH_UP) == 23 and len(GLYPH_DN) == 29,
      f"{len(GLYPH_UP)}/{len(GLYPH_DN)}")


# ---------------------------------------------------------------- 落点公式复算
def region_offset(x0: int, y0: int, blk_cols: int, blk_rows: int, flags: int, p: int) -> int:
    """复算 `_22_1665_region_offset()`（含落点镜像旗标）。"""
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
    return [region_offset(c["x0"], c["y0"], c["w"] // 4, c["h"] // 4, c.get("flags", 0), p)
            for p in range(FRAME_BITS)]


def chain_pixels(c: dict) -> set[tuple[int, int]]:
    return {(o % SCREEN_W, o // SCREEN_W) for o in chain_offsets(c)}


def chain_bits(c: dict, lit: set[tuple[int, int]]) -> list[int]:
    return [1 if ((o % SCREEN_W, o // SCREEN_W) in lit) else 0 for o in chain_offsets(c)]


def role_lit(role: str, content: str) -> set[tuple[int, int]]:
    """某 die 颜色的链在「内容为 content」时取到的像素集合（RED 取 R 位 / GREEN 取 G 位）。"""
    has_r = content in ("RED", "YELLOW", "WHITE")
    has_g = content in ("GREEN", "YELLOW", "WHITE")
    if (role == "RED" and not has_r) or (role == "GREEN" and not has_g):
        return set()
    return GLYPH_PX


def visible(chains: list[dict], content: str, working: set[int]) -> set[tuple[int, int]]:
    """按「哪些脚现场真导通」算出屏上可见点集。"""
    vis: set[tuple[int, int]] = set()
    for c in chains:
        if c["line_mask"] not in working:
            continue
        for o, b in zip(chain_offsets(c), chain_bits(c, role_lit(c["role"], content))):
            if b:
                vis.add((o % SCREEN_W, o // SCREEN_W))
    return vis


# ---------------------------------------------------------------- A. 源码事实核实
print("== A. 源码事实核实（探针 8 脚序 / 探针 5 路径 / 探针 0 路径）==")

# A1. 脚索引顺序表：源码名字表
m = re.search(r"_22_1665_line_names\[[^\]]*\]\s*=\s*\{(.*?)\};", src, re.S)
NAMES = re.findall(r'"([^"]+)"', m.group(1)) if m else []
check("源码 `_22_1665_line_names[]` = R1/G1/B1/R2/G2/B2/A/B/C/D（10 根，顺序固定）",
      NAMES == LINE_ORDER, str(NAMES))

# A2. k → 引脚：源码公式（k/3、k%3）+ pl_hub75 数组 + main.h 端口位号
def main_h_pin(name: str) -> tuple[str, int]:
    p = re.search(rf"^#define\s+HUB75_{re.escape(name)}_GPIO_Port\s+GPIO(\w+)", main_h, re.M)
    n = re.search(rf"^#define\s+HUB75_{re.escape(name)}_Pin\s+GPIO_PIN_(\d+)", main_h, re.M)
    if not p or not n:
        raise SystemExit(f"main.h 缺 HUB75_{name} 引脚宏")
    return p.group(1), int(n.group(1))


check("源码 `_22_1665_LINE_TO_CH(k)` = k/3（三根一组）",
      "((uint8_t)((k) / 3U))" in src)
check("源码 `_22_1665_LINE_TO_SIG(k)` = k%3（0=R / 1=G / 2=B）",
      "((uint8_t)((k) % 3U))" in src)
check("源码 `_22_1665_line_pin()`：k ≥ 6（RGB 根数）→ 取 A/B/C/D 引脚表",
      "if (k >= _22_1665_RGB_COUNT)" in src and "_22_1665_addr_pins[k - _22_1665_RGB_COUNT]" in src)

SIG_TABLE = {"R": "g_hub75_pin_r", "G": "g_hub75_pin_g", "B": "g_hub75_pin_b"}


def pin_array(name: str) -> list[str]:
    m = re.search(rf"const hub75_pin_t {name}\[HUB75_CHANNEL_MAX\]\s*=\s*\{{(.*?)\}};",
                  pl_hub75, re.S)
    return re.findall(r"HUB75_(\w+)_GPIO_Port", m.group(1)) if m else []


ARRAYS = {n: pin_array(n) for n in SIG_TABLE.values()}
check("pl_hub75.c 引脚表：R 组 = [R1, R2, ...] / G 组 = [G1, G2, ...] / B 组 = [B1, B2, ...]",
      ARRAYS["g_hub75_pin_r"][:2] == ["R1", "R2"] and ARRAYS["g_hub75_pin_g"][:2] == ["G1", "G2"]
      and ARRAYS["g_hub75_pin_b"][:2] == ["B1", "B2"], str(ARRAYS))
K2PIN: list[tuple[str, str, int]] = []
for k, nm in enumerate(LINE_ORDER):
    if k < 6:
        ch, sig = k // 3, k % 3
        arr = SIG_TABLE["RGB"[sig]]
        check(f"pl_hub75.c 的 {arr}[{ch}] = HUB75_{nm}（k={k} 经 k/3、k%3 落位正确）",
              len(ARRAYS[arr]) > ch and ARRAYS[arr][ch] == nm, str(ARRAYS[arr][:2]))
    else:
        nm_addr = ADDR_PINS[k - 6]
        check(f"k={k} → 地址脚 {nm_addr}（`_22_1665_addr_pins[k-6]`）", nm == nm_addr)
    port, pin = main_h_pin(nm)
    K2PIN.append((nm, port, pin))

EXPECT_PINS = [("R1", "G", 9), ("G1", "G", 10), ("B1", "G", 12), ("R2", "G", 15),
               ("G2", "B", 6), ("B2", "B", 7), ("A", "D", 4), ("B", "D", 5),
               ("C", "D", 6), ("D", "D", 7)]
check("k=0..9 → 端口/位号表 = R1(PG9)/G1(PG10)/B1(PG12)/R2(PG15)/G2(PB6)/B2(PB7)/A(PD4)/B(PD5)/C(PD6)/D(PD7)",
      K2PIN == EXPECT_PINS, str(K2PIN))
print("  探针 8 脚索引顺序表（k ↔ 脚名 ↔ 端口位号）：")
for k, (nm, port, pin) in enumerate(K2PIN):
    print(f"    k={k} → {nm:<3} (P{port}{pin})")

# A3. 探针 8 绕过链表：参与脚 = DATA_LINES | R1；状态位由被测脚序号直接给出
check("探针 8 的参与脚 = `_22_1665_DATA_LINES | _22_1665_LINE_R1`（R1 兼作序号显示器）",
      "_22_1665_DATA_LINES | _22_1665_LINE_R1" in src)
check("探针 8 状态位 = 「被测脚电平 << s_probe8_k」（逐脚直接驱动，不经链表 line_mask）",
      "bb << s_probe8_k" in src)
check("探针 8 的 scan 分支不读显示内容（`(void)src`）⇒ 与 prepare/链表解耦",
      "_22_1665_probe8_tick();" in src and "序号由本文件自绘，与显示内容无关" in src)

# A4. 探针 5（多链模式）：状态位只有 bit0 → 只拉高链 0 的脚
check("探针 5/6 的图案函数对全部链位返回 1（`return 1U; /* 5 / 6：全链点亮 */`）",
      "return 1U; /* 5 / 6：全链点亮 */" in src)
check("合并表的「多链」分支：状态位 i → 第 i 条链的 line_mask 并集（探针 5 的 st 恒 = 1）",
      "if ((st & (uint8_t)(1U << i)) != 0U)" in src and "st_bits |= (uint16_t)(_22_1665_chains[i].line_mask" in src)
check("兼容模式分支：`st != 0 → 全部参与脚一起亮`（这才是「十脚恒定全 1」的口径）",
      "st_bits = (st != 0U) ? wlines : 0U;" in src)


def probe5_high_pins(chains: list[dict]) -> set[int]:
    """复算「多链 + 探针 5/6」把哪几根脚拉高：st 恒 1 → 只有 bit0 → 只有链 0 的 line_mask。"""
    st_bits = 0
    for i, c in enumerate(chains):
        if 1 & (1 << i):
            st_bits |= c["line_mask"]
    return {k for k in range(10) if st_bits & (1 << k)}


h5_r17 = probe5_high_pins(CHAINS_R17)
h5_cur = probe5_high_pins([dict(name="C0", line_mask=LINE_MASK["R1"], role="RED", x0=0, y0=0, w=16, h=8),
                           dict(name="C1", line_mask=LINE_MASK["G1"], role="GREEN", x0=0, y0=0, w=16, h=8),
                           dict(name="C2", line_mask=LINE_MASK["R2"], role="RED", x0=0, y0=8, w=16, h=8),
                           dict(name="C3", line_mask=LINE_MASK["G2"], role="GREEN", x0=0, y0=8, w=16, h=8)])
check("多链 + 探针 5（旧表 R1/A/B/C）→ 恒高脚 = {R1}（= 观测②「只有上半屏红」，与 A/B/C 是否导通无关）",
      h5_r17 == {0}, str(sorted(h5_r17)))
check("多链 + 探针 5（第二十轮新表 R1/G1/R2/G2）→ 恒高脚仍 = {R1}（同一结论：探针 5 只认链 0）",
      h5_cur == {0}, str(sorted(h5_cur)))
h5_compat = {k for k in range(10) if DATA_LINES_DEFAULT & (1 << k)}
check("兼容模式 + 探针 5 → 恒高脚 = DATA_LINES 全部（默认 0x3FF = 十根）＝第十五行「整屏全亮」那次的口径",
      h5_compat == set(range(10)), str(sorted(h5_compat)))

# A5. 探针 0 = 链表路径（红内容只喂 red 角色链）
print("  探针 0（正常显示）：状态字节 = 各链「角色取分量」的位；未参与链的脚一个位都不写")


# ---------------------------------------------------------------- B. 现场三条观测编码
print("== B. 现场三条观测（用户回报）→ 断言 ==")

# 观测①：CHAIN_PROBE=0 + 红「口」，旧表（R1/A/B/C）+ 只有 R1 工作 → 只上半屏 23 点
working_old = {LINE_MASK["R1"]}
vis_old = visible(CHAINS_R17, "RED", working_old)
OBS1 = {(x, y) for (x, y) in GLYPH_PX if y < HALF_H}
check("观测①：`CHAIN_PROBE=0` 红「口」= 上半屏 23 点（y≥8 全黑）——旧表 R1/A/B/C 口径复算",
      vis_old == OBS1 and all(y < HALF_H for _, y in vis_old),
      f"差集 {sorted(vis_old ^ OBS1)[:8]}")
check("观测①：红内容只到「红角色链」（链 0 = R1、链 2 = B）——绿角色链（A/C）位流为 0",
      sum(chain_bits(CHAINS_R17[0], role_lit("RED", "RED"))) == 23
      and sum(chain_bits(CHAINS_R17[2], role_lit("RED", "RED"))) == 29
      and sum(chain_bits(CHAINS_R17[1], role_lit("GREEN", "RED"))) == 0
      and sum(chain_bits(CHAINS_R17[3], role_lit("GREEN", "RED"))) == 0)

# 观测②：CHAIN_PROBE=5 → 只拉高链 0 的脚（R1）→ 上半屏全亮红、绿一颗不亮
check("观测②：多链探针 5 只驱动链 0（R1）⇒ 上半屏 128 点全亮（红）、下半屏/绿灯必然不亮",
      h5_r17 == {0} and LINE_ORDER[0] == "R1")
check("观测②：与「链表里 A/B/C 是否导通」无关（探针 5 根本不碰这三根脚）——修正第十七轮旧解释",
      h5_r17 == {0})

# 观测③：探针 8 逐脚读数 → 解码接线
print("  探针 8 读数解码（k → 背景颜色 / 区域）：")
decoded_from_field: dict[str, tuple[str, str]] = {}
for k, (color, region) in sorted(P8_FIELD.items()):
    nm = LINE_ORDER[k]
    decoded_from_field[nm] = (color, region)
    print(f"    k={k} {nm:<3} → {color:5} / {region}")
for k in P8_SILENT:
    print(f"    k={k} {LINE_ORDER[k]:<3} → 无反应（背景不亮）")
print(f"    k=0 R1  → （用户未单独回报；由观测①②独立确认导通）" if not P8_K0_REPORTED else "")

check("探针 8 读数：k=1 → G1 上半绿", decoded_from_field.get("G1") == ("GREEN", "upper"))
check("探针 8 读数：k=3 → R2 下半红", decoded_from_field.get("R2") == ("RED", "lower"))
check("探针 8 读数：k=4 → G2 下半绿", decoded_from_field.get("G2") == ("GREEN", "lower"))
check("探针 8 读数：k=2（B1）与 k=5..9（B2/A/B/C/D）全部无反应 ⇒ 不与任何链相连",
      all(LINE_ORDER[k] not in decoded_from_field for k in P8_SILENT))
check("由上解出的四链（含 R1）= DECODED（R1 上半红 / G1 上半绿 / R2 下半红 / G2 下半绿）",
      decoded_from_field == {k: v for k, v in DECODED.items() if k != "R1"})
check("三条观测互不矛盾：观测①（红只上半）· 观测②（探针 5 只点链 0）· 观测③（探针 8 逐脚读到 G1/R2/G2）",
      vis_old == OBS1 and h5_r17 == {0} and len(decoded_from_field) == 3)


# ---------------------------------------------------------------- C. 源码默认表 = 读数结果
print("== C. 源码默认表（第二十轮定标）==")


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

        def val(tok: str):
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
            return int(tok.rstrip("Uu"), 0)

        flags = 0
        if "_XF" in raw.group(0)[:20] and len(fields) >= 8:
            for part in fields[7].split("|"):
                part = part.strip()
                if part == "_22_1665_XF_ROT180":
                    flags |= XF_ROT180
                elif part.endswith("_BLK_X"):
                    flags |= XF_BLK_X
                elif part.endswith("_BLK_Y"):
                    flags |= XF_BLK_Y
                elif part.endswith("_X"):
                    flags |= XF_X
                elif part.endswith("_Y"):
                    flags |= XF_Y
        rows.append(dict(name=re.sub(r'"', "", fields[0]), line_mask=val(fields[1]),
                         role=val(fields[2]), x0=val(fields[3]), y0=val(fields[4]),
                         w=val(fields[5]), h=val(fields[6]), flags=flags))
    return rows


CHAINS = parse_chains(src)
check(f"链表行数 = {CHAIN_COUNT}", len(CHAINS) == CHAIN_COUNT, str(len(CHAINS)))
for c in CHAINS:
    print(f"  · {c['name']}: lines=0x{c['line_mask']:03X} role={c['role']} "
          f"region=({c['x0']},{c['y0']},{c['w']},{c['h']}) flags=0x{c['flags']:X}")

EXPECT_CHAINS = [
    ("C0", "R1", "RED", 0, 0, 16, 8),
    ("C1", "G1", "GREEN", 0, 0, 16, 8),
    ("C2", "R2", "RED", 0, 8, 16, 8),
    ("C3", "G2", "GREEN", 0, 8, 16, 8),
]
check("默认表四行 = R1(上半红) / G1(上半绿) / R2(下半红) / G2(下半绿)（逐字段）",
      [(c["name"], next(n for n in LINE_ORDER if LINE_MASK[n] == c["line_mask"]), c["role"],
        c["x0"], c["y0"], c["w"], c["h"]) for c in CHAINS] == EXPECT_CHAINS,
      str([(c["name"], hex(c["line_mask"]), c["role"], c["y0"]) for c in CHAINS]))
check("默认表四行的 `flags` 全为恒等（0）：朝向尚未由现场证据确定（探针 8 是均匀填充）",
      all(c["flags"] == 0 for c in CHAINS), str([hex(c["flags"]) for c in CHAINS]))
check("默认表不占用 B1/B2/A/B/C/D 六根脚（现场实测无链）",
      all(not (c["line_mask"] & (LINE_MASK["B1"] | LINE_MASK["B2"] | LINE_MASK["A"]
                                 | LINE_MASK["B"] | LINE_MASK["C"] | LINE_MASK["D"]))
          for c in CHAINS))
check("默认表与 DECODED 逐脚一致（读数 → 表 = 零推断）",
      {next(n for n in LINE_ORDER if LINE_MASK[n] == c["line_mask"]): c["role"] for c in CHAINS}
      == {k: v[0] for k, v in DECODED.items()})
check("`_22_1665_CHAIN_PROBE` 默认值 = 0U（正常显示；现场读数已完成，探针归位）",
      PROBE_DEFAULT == 0, str(PROBE_DEFAULT))
check("`_22_1665_DATA_LINES` 默认值保持 0x3FF（探针候选脚不变）",
      DATA_LINES_DEFAULT == 0x3FF, hex(DATA_LINES_DEFAULT))
check("多链模式仍为默认（`_22_1665_MULTI_CHAIN = 1`）", MULTI == 1)


# ---------------------------------------------------------------- D. 新表位流 / 可见点集
print("== D. 新表位流分布与可见点集 ==")
red_chains = [c for c in CHAINS if c["role"] == "RED"]
grn_chains = [c for c in CHAINS if c["role"] == "GREEN"]
check("红角色链 = 链 0（R1, 上半）与链 2（R2, 下半）；绿角色链 = 链 1（G1, 上半）与链 3（G2, 下半）",
      [c["name"] for c in red_chains] == ["C0", "C2"]
      and [c["name"] for c in grn_chains] == ["C1", "C3"])

red_bits = {c["name"]: sum(chain_bits(c, GLYPH_PX)) for c in red_chains}
grn_bits = {c["name"]: sum(chain_bits(c, GLYPH_PX)) for c in grn_chains}
check("红「口」位流：R1 链 = 23 点（字模上半）、R2 链 = 29 点（字模下半）",
      red_bits == {"C0": 23, "C2": 29}, str(red_bits))
check("绿「口」位流：G1 链 = 23 点、G2 链 = 29 点（同一字模，按 die 分量分开喂）",
      grn_bits == {"C1": 23, "C3": 29}, str(grn_bits))
check("红内容不进绿链 / 绿内容不进红链（分量隔离：红链取 R 位、绿链取 G 位）",
      all(sum(chain_bits(c, role_lit(c["role"], "GREEN"))) == 0 for c in red_chains)
      and all(sum(chain_bits(c, role_lit(c["role"], "RED"))) == 0 for c in grn_chains))

all_working = {c["line_mask"] for c in CHAINS}
vis_red = visible(CHAINS, "RED", all_working)
vis_grn = visible(CHAINS, "GREEN", all_working)
check("（四链全导通）红「口」可见点集 = 整幅 52 点（上半 23 + 下半 29）", vis_red == GLYPH_PX,
      f"差 {sorted(vis_red ^ GLYPH_PX)[:8]}")
check("（四链全导通）绿「口」可见点集 = 整幅 52 点", vis_grn == GLYPH_PX,
      f"差 {sorted(vis_grn ^ GLYPH_PX)[:8]}")
check("红「口」的上/下半分布 = 23 / 29（与字模逐点一致）",
      len(vis_red & GLYPH_UP) == 23 and len(vis_red & GLYPH_DN) == 29)

# 若下半屏朝向相反（旋转 180°）：脚本的落点模型会把「像素集合」算成同一集合
# （dst 是双射，静态图案的点集与位序无关）⇒ **朝向无法用宿主复算判定**，只能现场看字形。
# 这是本轮唯一留给现场的开放项（见报告「下一步现场验证清单」与 doc/01 §21）。
print("  · 朝向开放项：若 R2/G2 的片内位序与 R1 不同（下半屏 PCB 旋屏），宿主算不出差异 —— "
      "需现场看字形是否上下颠倒/左右镜像，再决定是否给链 2/3 加 `_22_1665_XF_ROT180`")


# ---------------------------------------------------------------- E. 落点双射
print("== E. 落点双射与红/绿覆盖 ==")
ok_bij = True
ok_in = True
for c in CHAINS:
    offs = chain_offsets(c)
    if len(set(offs)) != FRAME_BITS:
        ok_bij = False
    if not all(c["x0"] <= o % SCREEN_W < c["x0"] + c["w"]
               and c["y0"] <= o // SCREEN_W < c["y0"] + c["h"] for o in offs):
        ok_in = False
check("四条链各自 128 链位 ↔ 本链区域 128 像素（双射）", ok_bij)
check("四条链的落点全部留在本链区域内", ok_in)

red_union: set[tuple[int, int]] = set()
grn_union: set[tuple[int, int]] = set()
for c in CHAINS:
    (red_union if c["role"] == "RED" else grn_union).update(chain_pixels(c))
check("红链并集覆盖 16×16 全屏 256 像素（红 die 满屏）", len(red_union) == SCREEN_W * SCREEN_H,
      str(len(red_union)))
check("绿链并集覆盖 16×16 全屏 256 像素（绿 die 满屏）", len(grn_union) == SCREEN_W * SCREEN_H,
      str(len(grn_union)))
check("4 链 × 128 位 = 512 LED = 256 像素 × 2 die", 4 * FRAME_BITS == 512)

# ---------------------------------------------------------------- F. 产物 md
def ascii_screen(chains: list[dict], content: str, working: set[int]) -> str:
    grid = [["." for _ in range(SCREEN_W)] for _ in range(SCREEN_H)]
    for c in chains:
        if c["line_mask"] not in working:
            continue
        for o, b in zip(chain_offsets(c), chain_bits(c, role_lit(c["role"], content))):
            if b:
                grid[o // SCREEN_W][o % SCREEN_W] = "#"
    return "\n".join("".join(r) for r in grid)


def ascii_region(c: dict, content: str) -> str:
    buf = [["."] * c["w"] for _ in range(c["h"])]
    for o, b in zip(chain_offsets(c), chain_bits(c, role_lit(c["role"], content))):
        if b:
            buf[o // SCREEN_W - c["y0"]][o % SCREEN_W - c["x0"]] = "#"
    return "\n".join("".join(r) for r in buf)


md: list[str] = []
md.append("# 22-1665 第二十轮：现场探针 8 定标（四链 = R1 / G1 / R2 / G2）"
          "——由 `check_round20.py` 生成\n")
md.append("## 1. 探针 8 脚索引顺序表（k = R1 区显示的数字）\n")
md.append("| k | 脚名 | 端口/位号 | 现场读数（用户回报） |")
md.append("|---|---|---|---|")
for k, (nm, port, pin) in enumerate(K2PIN):
    if k == 0:
        read = "（未单独回报；由观测①②确认导通：上半屏红）"
    elif k in P8_FIELD:
        color, region = P8_FIELD[k]
        read = f"背景**{'绿' if color == 'GREEN' else '红'}**色 · {'上半' if region == 'upper' else '下半'}屏"
    else:
        read = "无反应（背景不亮）"
    md.append(f"| {k} | {nm} | P{port}{pin} | {read} |")
md.append("")

md.append("## 2. 由读数解出的链表（= 源码默认表）\n")
md.append("| 链 | 脚 | 角色 | 区域 (x0,y0,w,h) | 现场依据 |")
md.append("|---|---|---|---|---|")
evidence_note = {
    "C0": "第十二/十七/二十轮三度确认（观测①：红「口」上半屏）",
    "C1": "探针 8 k=1 → 上半屏绿",
    "C2": "探针 8 k=3 → 下半屏红",
    "C3": "探针 8 k=4 → 下半屏绿",
}
for c in CHAINS:
    md.append(f"| {c['name']} | {next(n for n in LINE_ORDER if LINE_MASK[n] == c['line_mask'])} | "
              f"{c['role']} | ({c['x0']},{c['y0']},{c['w']},{c['h']}) | {evidence_note[c['name']]} |")
md.append("\n> B1(k=2) / B2(k=5) / A(k=6) / B(k=7) / C(k=8) / D(k=9) 六根脚现场**全程无反应**"
          "⇒ 不与任何链相连，链表一个都不占用。\n")

md.append("## 3. 新表预期屏面（`CHAIN_PROBE = 0`，四链全导通）\n")
md.append("红色「口」（红链取 R 位：R1 上半 23 点 + R2 下半 29 点 = 52 点）：\n")
md.append("```\n" + ascii_screen(CHAINS, "RED", all_working) + "\n```\n")
md.append("绿色「口」（绿链取 G 位：G1 上半 23 点 + G2 下半 29 点 = 52 点）：\n")
md.append("```\n" + ascii_screen(CHAINS, "GREEN", all_working) + "\n```\n")
md.append("各链自己的 128 位（红「口」内容；区域局部视图）：\n")
for c in CHAINS:
    md.append(f"\n### {c['name']}（{next(n for n in LINE_ORDER if LINE_MASK[n] == c['line_mask'])}，"
              f"{c['role']}，region=({c['x0']},{c['y0']},{c['w']},{c['h']})）\n\n```\n"
              + ascii_region(c, "RED") + "\n```\n")

md.append("\n## 4. 三条现场观测的复算\n")
md.append("- **观测①（`CHAIN_PROBE=0` 红「口」只在上半屏）**：旧表（R1/A/B/C）下红内容只喂"
          "链 0（R1，真链）与链 2（B，无链）⇒ 上半屏 23 点、y≥8 全黑 —— 逐点与实测一致。\n")
md.append("- **观测②（`CHAIN_PROBE=5` 上半屏红全亮、绿不亮）**：多链模式的探针 5/6 状态位只有 "
          "bit0 ⇒ `st_bits` = 链 0（R1）的 line_mask ⇒ **只拉高 R1 一根脚**"
          "（与链表其余行、与各脚是否导通都无关）。「十脚恒定全 1」只在兼容模式下成立。\n")
md.append("- **观测③（`CHAIN_PROBE=8` 逐脚读数）**：见 §1 表 —— 探针 8 的参与脚 = "
          "`DATA_LINES | R1`、状态位由被测脚序号直接给出 ⇒ **绕过链表逐脚驱动**，"
          "因此能看到 G1/R2/G2 三条此前「送内容无反应」的链。\n")

md.append("\n## 5. 下一步现场复验清单（`CHAIN_PROBE = 0` 默认口径，零改宏）\n")
md.append("1. **红「口」是否整幅 16 行完整出现？**（上半 R1 + 下半 R2）—— 预期 = 上面 §3 的图；\n")
md.append("2. **把默认画面颜色改成绿**（`app_default_display.c`）后，是否出现整幅绿色「口」？"
          "（G1 上半 + G2 下半；红链取 R 位 = 0 ⇒ 不自亮）\n")
md.append("3. **下半屏的字形是否正常 / 上下颠倒 / 左右镜像 / 有缺笔画？**"
          "—— 若颠倒/镜像 ⇒ 给链 2/链 3 加 `flags`（最常见 `_22_1665_XF_ROT180`）再烧一次；\n")
md.append("4. **RTT 核对**：`[22_1665] chain0..3` 应打 `lines=0x001/0x002/0x008/0x010`、"
          "`role=RED/GREEN/RED/GREEN`、`region=(0,0,16,8)/(0,0,16,8)/(0,8,16,8)/(0,8,16,8)`、"
          "`xf=0x0`；`out_lines=0x01B ports=2`；逐脚自检 4 行 R1/G1/R2/G2 全 PASS。\n")
md.append("\n## 6. 仍需现场回答的问题\n")
md.append("- **探针 8 时 k=0（R1）的读数**有没有？（用户只报了 k=1..9；R1 的导通性已由观测①②确认，"
          "这条只是补全读数表）\n")
md.append("- **探针 8 每根脚那 3 秒里，「前 1.5s 2 Hz 闪 / 后 1.5s 细密纹理」两段是否都看到了？**"
          "—— 判断「真被驱动」与「只跟电平（悬空/弱耦合）」的关键（本次读数是「背景亮/不亮」，"
          "尚不能区分这两者）。\n")
OUT_MD.write_text("\n".join(md), encoding="utf-8")
check(f"{OUT_MD.name} 已生成", OUT_MD.exists())

print()
print(f"结果：通过 {PASS} / 失败 {FAIL}")
if FAILURES:
    print("失败项：")
    for f in FAILURES:
        print("  -", f)
sys.exit(1 if FAIL else 0)
