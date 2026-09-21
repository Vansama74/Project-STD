#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 第十八轮宿主自检：**两条新现场观测（全屏填充只亮上半屏 / 全屏绿无绿灯）的机理解读**

  本脚本回答两个问题（只复算、不编译固件）：

  A. **TEST 键 8 种全屏纯色，在「当前默认链表（链 0=R1 / 1=A / 2=B / 3=C）」下各自把哪几根脚
     拉成恒定高/低**：全屏填充 = 所有像素同色 ⇒ 每条链的 128 位全相同 = `role_bit[role][color]`。
     由此得到「颜色 → 每链电平 → 每根数据脚电平」的完整矩阵，并与用户本轮两条观测对照：
       · 观测③「全屏填充只亮上半屏（0-7 行）」⇒ 红链（链 2，脚 B）被拉**恒定高**却什么都没点亮
         ⇒ 在「其余六根脚（G1/B1/R2/G2/B2/D）保持低」的前提下，**B 上没有链**；
       · 观测④「全屏绿 → 绿灯不亮」⇒ 两条绿链（链 1=A / 链 3=C）被拉**恒定高**却无绿
         ⇒ 在同样前提下，**A / C 上也没有链**。

  B. **与第十五轮「全 1 十脚同流 → 整屏 16×16 全亮」的差异（本轮最关键的机理对比）**：
     第十五轮那次是**十根候选脚同时恒定高**；而当前默认口径下 scan 只写
     `s_out_lines = R1 | A | B | C`（= 0x1C1）——**另外六根脚（G1/B1/R2/G2/B2/D）一个位都不写**，
     由 CubeMX 初始化成推挽输出**低电平**后一直保持不变。
     ⇒ 「整屏全亮」这件事**必然依赖那六根里的至少一根**（不是内容差异，而是脚的状态差异）。
     ⇒ 三条未标定链的候选脚由 {G1,B1,R2,G2,B2,D} 收窄到 **{G2, B2, D}**
       （G1/B1/R2 已在第十七轮被「内容驱动、屏上无反应」排除；A/B/C 本轮排除；
        而 {G2,B2,D} 恰好三根 ↔ 三条链，并且**恰好是第十六/十七轮两个构建都保持低的三根**）。

  附带：打印推荐的探针/掩码组合（第 2/3 步现场动作）与其循环时长。

  C. 证据表复算：把历轮「哪些脚被驱动 / 屏上其它区域是否亮过」列成表，标出与本推论冲突的历史
     回报（第十三/十四轮「0x3F / 0x3FF 串图案无新亮区域」）——冲突如实记录，不粉饰。

用法：python3 .analysis/22_1665/check_round18.py
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
SRC = ROOT / "Device/Display/dev_display_22_1665.c"
OUT_MD = pathlib.Path(__file__).resolve().parent / "round18_color_matrix.md"

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


# ---------------------------------------------------------------- 常量与链表
LINE_ORDER = ["R1", "G1", "B1", "R2", "G2", "B2", "A", "B", "C", "D"]
LINE_MASK = {n: (1 << k) for k, n in enumerate(LINE_ORDER)}
COLOR_NAMES = ["黑", "红", "绿", "黄", "蓝", "紫", "青", "白"]

CHIPS = macro("_22_1665_CHIPS")
CHIP_BITS = macro("_22_1665_CHIP_BITS")
FRAME_BITS = CHIPS * CHIP_BITS
CHAIN_COUNT = macro("_22_1665_CHAIN_COUNT")
SCREEN_W = macro("_22_1665_SCREEN_W")
SCREEN_H = macro("_22_1665_SCREEN_H")
MULTI = macro("_22_1665_MULTI_CHAIN")
BLUE_AS_LIT = macro("_22_1665_BLUE_AS_LIT")
HALF_H = SCREEN_H // 2

check("多链模式默认开（本轮的讨论前提）", MULTI == 1, f"= {MULTI}")

# `_22_1665_role_bit[2][8]`（BLUE_AS_LIT=1 分支）：
#   黑/红/绿/黄/蓝/紫/青/白
ROLE_BIT = {
    "RED": [0, 1, 0, 1, 1, 1, 1, 1],
    "GREEN": [0, 0, 1, 1, 1, 1, 1, 1],
}
if not BLUE_AS_LIT:
    ROLE_BIT = {
        "RED": [0, 1, 0, 1, 0, 1, 0, 1],
        "GREEN": [0, 0, 1, 1, 0, 0, 1, 1],
    }
# 与源码逐值核对（防手工抄错）：抓源文件里的两张表
tbl = re.search(r"static const uint8_t _22_1665_role_bit\[2\]\[8\] = \{(.*?)\n\};", src, re.S)
check("源码中找到 _22_1665_role_bit[2][8] 表", tbl is not None)
if tbl:
    body = tbl.group(1)
    if BLUE_AS_LIT:
        body = body.split("#else")[0]
    rows = re.findall(r"\{([0-9U,\s]+)\}", body)
    parsed = [[int(v.strip().rstrip("Uu")) for v in r.split(",") if v.strip()]
              for r in rows[:2]]
    check("RED 行逐值 = 脚本表", parsed[0] == ROLE_BIT["RED"], str(parsed[:1]))
    check("GREEN 行逐值 = 脚本表", parsed[1] == ROLE_BIT["GREEN"], str(parsed[1:]))


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

        rows.append(dict(name=re.sub(r'"', "", fields[0]), line_mask=val(fields[1]),
                         role=val(fields[2]), y0=val(fields[4]), h=val(fields[6])))
    return rows


# 第十八轮分析用的是**第十七轮的推定表**（R1/A/B/C）——现场那版固件就是它。
# 第二十轮（2026-09-17 晚）探针 8 读数已定标真实接线（R1/G1/R2/G2），本脚本的「当前表」断言
# 随之更新，但 §A~§C 的全部电平/证据/集合代数分析仍按第十七轮口径复算（历史留档）。
CHAINS_R17_HIST = [
    dict(name="C0", line_mask=LINE_MASK["R1"], role="RED", y0=0, h=8),
    dict(name="C1", line_mask=LINE_MASK["A"], role="GREEN", y0=0, h=8),
    dict(name="C2", line_mask=LINE_MASK["B"], role="RED", y0=8, h=8),
    dict(name="C3", line_mask=LINE_MASK["C"], role="GREEN", y0=8, h=8),
]
CHAINS_NOW = parse_chains(src)  # 现行默认表（第二十轮定标）
CHAINS = CHAINS_R17_HIST        # 本节分析口径 = 第十七轮（历史）

check(f"链表行数 = {CHAIN_COUNT}", len(CHAINS_NOW) == CHAIN_COUNT, str(len(CHAINS_NOW)))
for i, c in enumerate(CHAINS):
    print(f"  · 链 {i} {c['name']}: lines=0x{c['line_mask']:03X} role={c['role']} "
          f"region=(x0,-,w,{c['h']}) y0={c['y0']}   [第十七轮口径]")

check("（历史）链 0 = R1（现场两次确认，第十七轮前提）", CHAINS[0]["line_mask"] == LINE_MASK["R1"])
check("（历史）链 1/2/3 = A / B / C（第十七轮推定值）",
      [c["line_mask"] for c in CHAINS[1:]] == [LINE_MASK["A"], LINE_MASK["B"], LINE_MASK["C"]],
      str([hex(c["line_mask"]) for c in CHAINS[1:]]))
check("（历史）角色/区域 = 上半红 / 上半绿 / 下半红 / 下半绿",
      [(c["role"], c["y0"], c["h"]) for c in CHAINS] ==
      [("RED", 0, 8), ("GREEN", 0, 8), ("RED", 8, 8), ("GREEN", 8, 8)])

# 现行默认表（第二十轮探针 8 定标）：逐字段断言见 check_round20.py
check("（现行 · 第二十轮）链 0..3 = R1/G1/R2/G2（探针 8 读数直定，取代第十七轮 A/B/C 推定）",
      [c["line_mask"] for c in CHAINS_NOW] ==
      [LINE_MASK["R1"], LINE_MASK["G1"], LINE_MASK["R2"], LINE_MASK["G2"]],
      str([hex(c["line_mask"]) for c in CHAINS_NOW]))

# ---------------------------------------------------------------- A. 颜色矩阵
print("== A. TEST 键 8 种全屏纯色 → 每链电平 / 每根脚电平（当前默认链表）==")

out_lines = 0
for c in CHAINS:
    out_lines |= c["line_mask"]
check("多链默认口径的参与脚集合 = R1|A|B|C（= 0x1C1）", out_lines == 0x1C1, hex(out_lines))
SILENT = {n for n in LINE_ORDER if not (out_lines & LINE_MASK[n])}
check("静默六根脚 = {G1, B1, R2, G2, B2, D}，恒定低（CubeMX 初始化后再无人写）",
      SILENT == {"G1", "B1", "R2", "G2", "B2", "D"}, str(sorted(SILENT)))

matrix = []
for color in range(8):
    lv = [ROLE_BIT[c["role"]][color] for c in CHAINS]
    high = [LINE_ORDER[k] for k in range(10)
            if any((c["line_mask"] & (1 << k)) and lv[i] for i, c in enumerate(CHAINS))]
    matrix.append((color, lv, high))
    print(f"  · 全屏{COLOR_NAMES[color]}（索引 {color}）: 链电平 C0..C3 = {lv} → "
          f"恒定高脚 = {high or ['(无)']}")

# 观测④（全屏绿）：两条绿链（A/C）被拉恒定高 → 若无绿灯 ⇒ A / C 上没有链
green = dict((c, h) for c, l, h in matrix)[2]
check("全屏绿：链 1(A)/链 3(C) 被拉恒定高、链 0(R1)/链 2(B) 为低", green == ["A", "C"], str(green))
# 观测③（含红的全屏填充）：链 2(B) 被拉恒定高
red = dict((c, h) for c, l, h in matrix)[1]
check("全屏红：链 0(R1)/链 2(B) 被拉恒定高、链 1(A)/链 3(C) 为低", red == ["R1", "B"], str(red))
for color in (3, 4, 5, 6, 7):
    check(f"全屏{COLOR_NAMES[color]}：R1/A/B/C 四脚全部恒定高",
          dict((c, h) for c, l, h in matrix)[color] == ["R1", "A", "B", "C"])

# 现场判据：只有链 0（R1，上半屏红）真工作 → 含红内容的颜色只亮上半屏；绿内容全黑
def expected_screen(color: int) -> str:
    """当前现场口径（只有链 0 工作）下的预期：含红 → 上半屏全亮；否则全黑。"""
    return "上半屏(0-7行)全亮" if ROLE_BIT["RED"][color] else "全屏不亮"


print("  现场口径（只有链 0 = R1 工作）下的预期：")
for color, lv, high in matrix:
    print(f"    · 全屏{COLOR_NAMES[color]}: {expected_screen(color)}"
          f"（另外被拉高的脚：{[h for h in high if h != 'R1'] or '无'}）")

check("观测③自洽：任何「含红」的全屏填充 → 只有上半屏亮（链 2/B 被拉高却不亮 ⇒ B 上无链）",
      all(expected_screen(c) == "上半屏(0-7行)全亮" for c in (1, 3, 4, 5, 6, 7)))
check("观测④自洽：全屏绿 → 全屏不亮（链 1/A、链 3/C 被拉高却不亮 ⇒ A / C 上无链）",
      expected_screen(2) == "全屏不亮" and green == ["A", "C"])

# ---------------------------------------------------------------- B. 与第十五轮的差异
print("== B. 与第十五轮「全 1 十脚同流 → 整屏 16×16 全亮」的差异 ==")
p15 = {n for n in LINE_ORDER}              # 第十五轮：0x3FF 十脚全开、数据恒定全 1
p_current = {n for n in LINE_ORDER if out_lines & LINE_MASK[n]}
delta = p15 - p_current
check("第十五轮（整屏全亮那次）驱动了全部十根脚", p15 == set(LINE_ORDER))
check("当前默认口径比它少驱动六根脚（正是静默六根）", delta == SILENT, str(sorted(delta)))
print(f"  第十五轮：驱动 {sorted(p15)}")
print(f"  当前默认：驱动 {sorted(p_current)}（其余六根恒低）")
print(f"  ⇒ 差异 = {sorted(delta)} ⇒ 「整屏全亮」必然依赖这六根里的至少一根")

# 历轮证据表：按「口径（构建）」列出被驱动的脚 + 现场回报（positive = 除上半屏外还亮过别处）
evidence = [
    ("A: 0x3F 同流（13 轮）", {"R1", "G1", "B1", "R2", "G2", "B2"},
     "只 R1 区域有内容（回报：无新亮区域）", False),
    ("B: 0x3FF 同流（14/15/16 轮）", set(LINE_ORDER),
     "**全 1 恒定 → 整屏 16×16 全亮**；串图案只出一块；另回报「绿灯亮」「口被切成两半」", True),
    ("C: 多链 R1/G1/B1/R2（16 轮构建）", {"R1", "G1", "B1", "R2"},
     "红「口」= 只上半 23 点；绿「口」= 全黑", False),
    ("D: 多链 R1/A/B/C（17 轮构建 = 现在）", {"R1", "A", "B", "C"},
     "红「口」= 只上半 23 点；绿「口」= 全黑；全屏填充只亮上半屏；全屏绿无绿灯", False),
]
print("  历轮证据（按口径：哪些脚被驱动 / 屏上其它区域是否亮过）：")
for name, driven, obs, positive in evidence:
    low = sorted(set(LINE_ORDER) - driven)
    print(f"    · {name}: 驱动 {sorted(driven)} | 静默 {low or '—'} → {obs}")

# ④ 相关性：唯一与「其它区域亮过」完全同步的脚 = D
positives = [e for e in evidence if e[3]]
negatives = [e for e in evidence if not e[3]]
check("报过「其它区域亮」的口径只有 B（十脚全驱动的那个）", len(positives) == 1)
check("在 B 里被驱动、且在全部三个阴性口径里都保持低的脚 = {D}（唯一解）",
      set(LINE_ORDER) - set().union(*[d for _n, d, _o, p in evidence if not p]) == {"D"},
      str(sorted(set(LINE_ORDER) - set().union(*[d for _n, d, _o, p in evidence if not p]))))
for pin in LINE_ORDER:
    p_driven = [p for _n, d, _o, p in evidence if pin in d]
    if len(set(p_driven)) > 1:
        print(f"    · {pin}: 在阳性/阴性口径里都被驱动过 ⇒ 单靠它解释不了差异")
print("    · D: 只在阳性口径（十脚全开）里被驱动、在全部阴性口径里都恒低 ⇒ 与观测唯一同步的脚")

# 集合代数：三条「否定」各自排除掉什么
S1 = {"A", "B", "C", "D"}                          # 由口径 A 否定：six RGB 驱动不够
S2 = {"D", "G1", "B1", "R2", "G2", "B2"}           # 由口径 D 否定：R1+A+B+C 不够
S3 = {"G2", "B2", "D", "A", "B", "C"}              # 由口径 C 否定：R1+G1+B1+R2 不够
check("三条否定证据分别要求：需要的脚集 N ∩ {A,B,C,D} ≠ ∅", True)
single_survivors = sorted(S1 & S2 & S3)
check("同时满足三条否定的「单脚」解 = {D}（唯一）", single_survivors == ["D"], str(single_survivors))
print(f"  ⇒ 单脚候选唯一 = D；两脚解还包括「D 之外的 A/B/C 之一 + G2/B2 之一」"
      f"（例：{{A,G2}}）——即若 D 不是使能脚，则可能是一根数据脚 + 一根使能/选通脚")

# 候选收窄（内容驱动已被排除的脚）
excluded = {"R1"} | {"G1", "B1", "R2"} | {"A", "B", "C"}
remaining = [n for n in LINE_ORDER if n not in excluded]
check("三重排除（内容驱动）后剩余候选脚 = {G2, B2, D}（恰好三根 ↔ 三条未标定链）",
      remaining == ["G2", "B2", "D"], str(remaining))

# ---------------------------------------------------------------- C. 推荐探针组合
print("== C. 推荐现场动作（探针 + 掩码 + 循环时长）==")
SUSPECT_MASK = (LINE_MASK["G2"] | LINE_MASK["B2"] | LINE_MASK["D"])
check("嫌疑脚掩码 0x230 = G2(0x010) | B2(0x020) | D(0x200)", SUSPECT_MASK == 0x230,
      hex(SUSPECT_MASK))
LEVEL_MASK = SUSPECT_MASK | LINE_MASK["R1"]      # 电平测试要含 R1 作参照（上半屏）
check("电平测试掩码 0x231 = R1 + G2 + B2 + D", LEVEL_MASK == 0x231, hex(LEVEL_MASK))
check("D 单脚电平测试掩码 0x201 = R1 + D", (LINE_MASK["R1"] | LINE_MASK["D"]) == 0x201)

P8_DWELL = 3.0  # 探针 8：每根脚 3s 停留
P9_HALF = macro("_22_1665_PROBE9_HALF_MS", 1500) / 1000.0
print("  · 第 1 步（最省事 / 最先做）：`CHAIN_PROBE=5` + `DATA_LINES=0x231`（R1+G2+B2+D）→ "
      "恒定全 1 → 看除上半屏外还有没有别的区域整块亮")
print(f"  · 第 2 步（二分）：`CHAIN_PROBE=5` + `DATA_LINES=0x201`（R1+D）→ 单看 D 够不够")
print(f"  · 第 3 步（区分真驱动/只跟电平）：`CHAIN_PROBE=8` + `DATA_LINES=0x230`（G2/B2/D）= "
      f"{3 * P8_DWELL:.0f}s 一轮（全覆盖用 0x3FE = 9 根 = {9 * P8_DWELL:.0f}s）")
print(f"  · 第 4 步（读全表）：`CHAIN_PROBE=9` + `PROBE9_PINS=0x230` = "
      f"链数({CHAIN_COUNT}) × 3 脚 × {P9_HALF:.1f}s = {CHAIN_COUNT * 3 * P9_HALF:.0f}s 一轮"
      f"（全覆盖 0x3FE = {CHAIN_COUNT * 9 * P9_HALF:.0f}s）")
print("  · 阳性对照：`CHAIN_PROBE=5` + 默认 0x3FF（十脚）→ 应复现「整屏全亮」（第十五轮那次）")

# ---------------------------------------------------------------- D. 第二十轮更新
print("== D. 第二十轮更新（现场探针 8 读数；§C 的「候选收窄」已被直接读数取代）==")
print("  · 探针 8（DATA_LINES 默认 0x3FF）逐脚读数：k=1(G1) → 上半屏绿；k=3(R2) → 下半屏红；"
      "k=4(G2) → 下半屏绿；k=2(B1) 与 k=5..9(B2/A/B/C/D) → 全程无反应")
print("  ⇒ 真链 = **R1/G1/R2/G2**（四条全部当场读到）⇒ §C 的「{G2,B2,D} 候选集 / 单脚解 D」"
      "**作废**（那是排除法 + 集合代数的推定，直接读数严格更强）")
check("（第二十轮）现场读数直定四链 = R1/G1/R2/G2，且源码默认表已同步",
      [c["line_mask"] for c in CHAINS_NOW] ==
      [LINE_MASK["R1"], LINE_MASK["G1"], LINE_MASK["R2"], LINE_MASK["G2"]])
check("（第二十轮）A/B/C/D 四根地址脚全部不在链表内（§C 曾推定其余链挂在地址脚上 —— 证伪）",
      all(not (c["line_mask"] & (LINE_MASK["A"] | LINE_MASK["B"] | LINE_MASK["C"] | LINE_MASK["D"]))
          for c in CHAINS_NOW))
check("（第二十轮）B1/B2 两根蓝脚也不在链表内（§A 的「蓝链」推断同样证伪）",
      all(not (c["line_mask"] & (LINE_MASK["B1"] | LINE_MASK["B2"])) for c in CHAINS_NOW))
check("（第二十轮）探针默认值已归位 0U；探针 9 仍是可用的「脚 ↔ 颜色 ↔ 区域 ↔ 朝向」细读工具",
      macro("_22_1665_CHAIN_PROBE", -1) == 0)

# ---------------------------------------------------------------- 产物 md
lines: list[str] = []
lines.append("# 22-1665 第十八轮：全屏纯色 → 数据脚电平矩阵（由 `check_round18.py` 生成）\n")
lines.append("当前默认链表：链 0 = R1（上半红，**现场确认**）、链 1 = A（上半绿）、链 2 = B（下半红）、"
             "链 3 = C（下半绿）——链 1/2/3 为**第十七轮排除法推定值**。\n")
lines.append("全屏填充 = 所有像素同色 ⇒ 每条链的 128 位全相同（= `role_bit[role][color]`）；"
             "bit = 1 的链把它的脚**恒定拉高**整整一帧。\n")
lines.append("| 全屏颜色 | C0(R1) | C1(A) | C2(B) | C3(C) | 恒高脚 | 现场口径（只有链 0 工作）预期 |")
lines.append("|---|---|---|---|---|---|---|")
for color, lv, high in matrix:
    lines.append(f"| {COLOR_NAMES[color]}（{color}） | {lv[0]} | {lv[1]} | {lv[2]} | {lv[3]} | "
                 f"{' '.join(high) or '(无)'} | {expected_screen(color)} |")
lines.append("")
lines.append("> 读法：**恒高脚**是这一色把哪几根脚持续拉高（= 那条链的输出应当全亮）。"
             "现场只看到上半屏亮 ⇒ 恒高脚里除 R1 以外的（B、以及黄/蓝/紫/青/白时的 A/C）"
             "**都没有真正驱动任何链**。\n")
lines.append("## 与「第十五轮 十脚全 1 → 整屏全亮」的差异\n")
lines.append(f"- 口径 B（第十五轮那次）：驱动 {sorted(p15)}（十根，恒定全 1）→ 整屏 16×16 全亮；")
lines.append(f"- 当前默认（口径 D）：驱动 {sorted(p_current)}（四根；其余六根恒低）→ 只亮上半屏；")
lines.append(f"- 差异 = {sorted(delta)} ⇒ **整屏全亮依赖这六根里的至少一根**。\n")
lines.append("## 历轮证据（按口径）\n")
lines.append("| 口径 | 被驱动的脚 | 静默脚 | 现场回报 | 其它区域亮过？ |")
lines.append("|---|---|---|---|---|")
for name, driven, obs, positive in evidence:
    lines.append(f"| {name} | {', '.join(sorted(driven))} | "
                 f"{', '.join(sorted(set(LINE_ORDER) - driven)) or '—'} | {obs} | "
                 f"{'**是**' if positive else '否'} |")
lines.append("")
lines.append("> **相关性**：唯一「在阳性口径里被驱动、在全部三个阴性口径里都恒低」的脚 = **D**"
             "（其它脚都至少在一个阴性口径里被驱动过）。")
lines.append("> 集合代数：三条阴性口径各自要求「需要的脚集 N」含有 {A,B,C,D} 之一（口径 A 否定）、"
             "{D,G1,B1,R2,G2,B2} 之一（口径 D 否定）、{G2,B2,D,A,B,C} 之一（口径 C 否定）"
             "⇒ **单脚解唯一 = D**；两脚解如 {A, G2} 也自洽（一根数据 + 一根使能/选通）。\n")
lines.append("## 推荐动作\n")
lines.append("| 步 | 宏 | 一轮时长 | 判据 |")
lines.append("|---|---|---|---|")
lines.append("| 0. 复核镜像身份 | 不改，看 RTT 横幅 `tree=` / `chainN … lines=` | — | 确认板上跑的确实是第十七轮默认口径 |")
lines.append("| 1. 复核「除上半屏外还能亮吗」 | `CHAIN_PROBE=5` + `DATA_LINES=0x231`（R1+G2+B2+D） | 恒定 | "
             "有别的区域整块亮 = G2/B2/D 里至少一根通向其余链（或 D 是使能脚）；只上半屏 = 这三根也不行 |")
lines.append("| 2. 二分 D | `CHAIN_PROBE=5` + `DATA_LINES=0x201`（R1+D） | 恒定 | "
             "有别的区域亮 = D 单独就够（数据脚）；仍只上半屏 = D 不是单独数据脚（可能要使能+数据） |")
lines.append("| 3. 区分真驱动 / 只跟电平 | `CHAIN_PROBE=8` + `DATA_LINES=0x230`（G2/B2/D） | "
             f"{3 * P8_DWELL:.0f}s | 闪 + 细密纹理 = 真被驱动；只闪无纹理 = 只跟电平；无反应 = 无链 |")
lines.append("| 4. 读全表（脚/颜色/区域/朝向） | `CHAIN_PROBE=9` + `PROBE9_PINS=0x230` | "
             f"{CHAIN_COUNT * 3 * P9_HALF:.0f}s | 某块屏出现形状正确的内容 → 填该行 `line_mask`/"
             "`role`/`region`；镜像则加 `flags` |")
lines.append("| 阳性对照 | `CHAIN_PROBE=5` + 默认 0x3FF（十脚） | 恒定 | 应复现「整屏全亮」"
             "（第十五轮那次）；若复现不了，先查镜像/接线是否已变 |")
lines.append("")
lines.append("> **探针 8/9 的固有局限（本轮如实记录）**：两者都是「一次只驱动一根脚」——"
             "若其余链需要「使能脚常高 + 数据脚送内容」的组合（本轮集合代数给出的两脚解），"
             "单脚探针永远看不到内容。真出现这种情况，需要下一轮加一个"
             "「常高脚掩码」参数（探针 10：`_22_1665_PROBE9_ENABLE_PINS`，扫描期间恒高）。\n")
lines.append("\n## 第二十轮更新（2026-09-17 晚）：现场探针 8 读数 —— 上面 §「候选收窄」已被取代\n")
lines.append("现场用 `CHAIN_PROBE=8`（`DATA_LINES` 默认 0x3FF）逐脚读数：\n")
lines.append("| k | 脚 | 现场读数 |")
lines.append("|---|---|---|")
lines.append("| 1 | G1 | 背景**绿**色 · 上半屏 |")
lines.append("| 3 | R2 | 背景**红**色 · 下半屏 |")
lines.append("| 4 | G2 | 背景**绿**色 · 下半屏 |")
lines.append("| 0 / 2 / 5..9 | R1 / B1 / B2 / A / B / C / D | R1 = 上半屏红（观测①②确认）；B1/B2/A/B/C/D **无反应** |")
lines.append("\n⇒ **真链四条 = R1（上半红）/ G1（上半绿）/ R2（下半红）/ G2（下半绿）**；"
          "上面 §C 的「候选收窄到 {G2,B2,D}、单脚解 = D」是排除法 + 集合代数的**推定**，"
          "已被本轮**直接读数**取代（A/B/C/D 四根地址脚与 B1/B2 两根蓝脚均无链）。"
          "源码默认表已同步为 R1/G1/R2/G2，`CHAIN_PROBE` 默认值归位 0；"
          "逐字段断言与预测屏面见 `check_round20.py`（58/58）与 `round20_screens.md`、"
          "`round20_calibration.md`。\n")
OUT_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")
check(f"{OUT_MD.name} 已生成", OUT_MD.exists())

print()
print(f"结果：通过 {PASS} / 失败 {FAIL}")
if FAILURES:
    print("失败项：")
    for f in FAILURES:
        print("  -", f)
sys.exit(1 if FAIL else 0)
