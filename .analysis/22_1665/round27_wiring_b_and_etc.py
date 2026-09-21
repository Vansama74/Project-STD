#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""第二十七轮：模型 B（每列一口独立数据线）实现取证 + 四川 ETC 七行现象逐行解释。

两块内容：
  §A 四川 ETC（`0A + 显示方式 + 行号 + 数据 + 0D`）在 16×32（1×2）逻辑屏上的**穷举表**——
     逐行号给出「渲染进逻辑屏的什么位置、单块模组（旧固件模型 A，链首）能看到什么」，
     并与现场七条观测（发第一~七行 → 34 / 空 / 12 / 空×4）对照，找出唯一自洽的假设组合。
  §B 模型 B 的机器级取证（现行默认，`Device/Display/dev_display_22_1665.c`）——
     1×2 时每帧 128 时钟、组 0（PG9/PG10/PG15/PB6）载模块 0、组 1（PB8/PB9/PE1/PE2）载模块 1；
     两块模组各插一口 ⇒ HUB1 显逻辑上半、HUB2 显逻辑下半（对照旧固件模型 A：只写第一组、
     单块只留末 128 位 = 逻辑下半）。

用法：python3 .analysis/22_1665/round27_wiring_b_and_etc.py
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".analysis/22_1665"))

import check_host_differential as chd  # noqa: E402  （复用桩编译 / 引脚表 / 规格模型）

ETC_CMD = ROOT / "Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto_cmd.c"
ETC_PARSE = ROOT / "Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto_parse.c"
DRIVER = ROOT / "Device/Display/dev_display_22_1665.c"
# 第二十九轮：模型 A 代码路径与 `_22_1665_HUB_WIRING` 开关已删除（生产化收口）——
# 「模型 A（旧固件形态）」的对照锚定**删除前归档驱动**（行为与当时逐字节一致）：
ARCHIVE_SRC = (ROOT / ".analysis/22_1665/archive/finalize/"
               "driver_pre_finalize_20260917.c")
DEFAULT_DISPLAY = ROOT / "Application/Src/app_default_display.c"
# **现场前提快照**（第二十九轮新增）：本脚本 §B-2 取证的是第二十六/二十七轮当时的口径
# = 默认画面为测试串 `"1\n2"`（FONT_16）。该测试画面是**外部改动**（非驱动），第二十八轮后
# 已回改为标准欢迎画面 → 现场前提在现行树上不再成立。故 §B-2 的源码事实断言锚定归档快照
# （`archive/finalize/app_default_display_test_1n2_20260917.c`），现行树只做「记录」不判红 ——
# 历史脚本保持可解释、不让 verify_all.sh 变红。
FIELD_SNAP = (ROOT / ".analysis/22_1665/archive/finalize/"
              "app_default_display_test_1n2_20260917.c")

PASS = 0
FAIL = 0
FAILURES: list[str] = []


def check(desc: str, cond: bool, extra: str = "") -> None:
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [通过] {desc}")
    else:
        FAIL += 1
        FAILURES.append(f"{desc} {extra}".strip())
        print(f"  [失败] {desc} {extra}")


def info(desc: str) -> None:
    print(f"  [记录] {desc}")


# ================================================================
#  §A-0. 源码事实：ETC 行号 → 渲染参数（直接读源码，不猜）
# ================================================================
print("== §A-0. 源码事实：ETC 单行/全屏渲染参数 ==")
src_cmd = ETC_CMD.read_text(encoding="utf-8")
src_parse = ETC_PARSE.read_text(encoding="utf-8")

check("单行：`_sc_etc_render_line((uint8_t)(p->row - 1U), ...)`（行号 1~6 → 行索引 0~5）",
      "_sc_etc_render_line((uint8_t)(p->row - 1U), p->text, p->text_len)" in src_cmd)
check("单行：先整行清黑再渲染（`dev_display_fill(d, 0, row*FONT_16, d->screen_rows, FONT_16, BLACK)`）",
      "dev_display_fill(d, 0, (uint16_t)row * FONT_16, d->screen_rows, FONT_16," in src_cmd)
check("单行：`.y = row*FONT_16`、`.w = d->screen_rows`（屏宽）、`.h = FONT_16`、"
      "`word_wrap = false`、`h_align/v_align = ALIGN_LEFT_UP`、`FONT_16`",
      ".y = (uint16_t)row * FONT_16," in src_cmd
      and ".w = d->screen_rows," in src_cmd
      and ".h = FONT_16," in src_cmd
      and ".word_wrap = false," in src_cmd
      and ".h_align = ALIGN_LEFT_UP," in src_cmd
      and ".v_align = ALIGN_LEFT_UP," in src_cmd
      and ".font_size = FONT_16," in src_cmd)
check("全屏（行号 0）：先 `_sc_etc_clear_screen()` 再 `word_wrap = true`、"
      "`.w = screen_rows`（16）、`.h = screen_cols`（32）",
      src_cmd.count("_sc_etc_clear_screen();") >= 2
      and ".h = d->screen_cols," in src_cmd and ".word_wrap = true," in src_cmd)
check("清屏语义：数据首字节 0x20（空格）→ 行号 0 清全屏 / 行号 n 清第 n 行（不再渲染文本）",
      "if (p->text[0] == 0x20) {" in src_cmd and "if (p->row == 0U) {" in src_cmd)
check("probe：行号 > 6 → FAKE（行号 7 不认领、不显示，帧被重同步跳过）",
      "if (row > 6U) /* 单行行号 1~6，0=全屏 */" in
      (ROOT / "Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto.c")
      .read_text(encoding="utf-8"))
check("parse：mode=0x00 时 `text = raw[3]`、`row = raw[2]`（数据区紧跟行号）",
      "cmd.p.display.text     = &raw[3];" in src_parse)

# 逻辑屏几何（用户当前宏值 1×2 → 16 宽 × 32 高；两块 16×16 上下叠放）
W, H = 16, 32
CELL_W, CELL_H = 8, 16          # FONT_16 ASCII：宽 = size/2 = 8，高 = 16


# ================================================================
#  §A-1. ETC 渲染模拟（镜像 `_sc_etc_*` + `app_render` 的文本布局）
# ================================================================
class Screen:
    """16×32 逻辑屏（1B/像素的字符占位图；'.' = 黑）。"""

    def __init__(self) -> None:
        self.px = [["."] * W for _ in range(H)]

    def fill(self, x: int, y: int, w: int, h: int) -> None:
        """dev_display_fill 语义：起点越界丢弃 / 部分越界截断。
        （真板上 x 是 uint16_t、永不为负；此处加 x<0 防御，避免调用方算法失误时崩脚本。）"""
        if x >= W or y >= H or x + w <= 0 or y + h <= 0:
            return
        x0, y0 = max(0, x), max(0, y)
        w = min(x + w, W) - x0
        h = min(y + h, H) - y0
        for yy in range(y0, y0 + h):
            for xx in range(x0, x0 + w):
                self.px[yy][xx] = "."

    def glyph(self, gx: int, gy: int, ch: str) -> None:
        """dev_display_draw_bitmap 语义：与屏幕取交集裁剪绘制。"""
        for yy in range(max(0, gy), min(H, gy + CELL_H)):
            for xx in range(max(0, gx), min(W, gx + CELL_W)):
                self.px[yy][xx] = ch

    def band(self, y0: int, y1: int) -> str:
        """区域内可见字符（按列扫描、按首次出现排序 → 形如 '12' / '34'）。"""
        seen: list[str] = []
        for xx in range(W):
            for yy in range(y0, y1):
                c = self.px[yy][xx]
                if c != "." and c not in seen:
                    seen.append(c)
        return "".join(seen)

    def art(self, y0: int, y1: int) -> list[str]:
        return ["".join(row) for row in self.px[y0:y1]]


def render_text(scr: Screen, text: str, x: int, y: int, w: int, h: int, word_wrap: bool) -> None:
    """镜像 `app_render.c::_render_text`（ASCII 字形宽 8；LEFT_UP 对齐；越界截断/换行）。"""
    cur_x, cur_y = x, y
    # 测量趟：word_wrap=false 时超宽字形「跳过且不计入行宽」（源码同款 continue）
    line_w, line_widths = 0, []
    for _ch in text:
        if line_w + CELL_W > w:
            if word_wrap:
                line_widths.append(line_w)
                line_w = CELL_W
            continue
        line_w += CELL_W
    line_widths.append(line_w)
    # 渲染趟
    for ch in text:
        if cur_x + CELL_W > w:
            if word_wrap:
                cur_y += CELL_H
                if cur_y >= h:      # 行首已完全越出区域 → 终止（源码同款）
                    return
                cur_x = x
            else:
                continue            # 不换行：超出部分截断（后续字形同样跳过）
        scr.fill(cur_x, cur_y, CELL_W, CELL_H)
        scr.glyph(cur_x, cur_y, ch)
        cur_x += CELL_W


def etc_command(scr: Screen, row: int, text: str) -> None:
    """镜像 `_sc_etc_exec_display`：0x20=清屏（行号 0 全屏 / n 第 n 行）；0=全屏；1~6=单行。"""
    if text and text[0] == " ":
        if row == 0:
            scr.fill(0, 0, W, H)
        else:
            scr.fill(0, (row - 1) * CELL_H, W, CELL_H)
        return
    if row == 0:
        scr.fill(0, 0, W, H)                                   # 全屏：先清屏
        render_text(scr, text, 0, 0, W, H, True)               # 再按屏宽自动换行
        return
    idx = row - 1
    scr.fill(0, idx * CELL_H, W, CELL_H)                       # 单行：清本行
    render_text(scr, text, 0, idx * CELL_H, W, CELL_H, False)  # 不换行（截断）


TEXT = "1234567890"

print("== §A-1. 逐行号穷举（逻辑屏 16×32；FONT_16 ASCII 8px ⇒ 每行 2 字）==")
print("     行号 │ 模块 0 区域(y 0..15) │ 模块 1 区域(y16..31) │ 说明")
row_table: dict[int, tuple[str, str]] = {}
for row in list(range(0, 8)):
    scr = Screen()
    if row <= 6:
        etc_command(scr, row, TEXT)
        m0, m1 = scr.band(0, 16), scr.band(16, 32)
        note = {0: "全屏（清屏 + 按屏宽换行）", 1: "单行 y=0..15", 2: "单行 y=16..31",
                3: "单行 y=32..47（整行越界）", 4: "单行 y=48..63（越界）",
                5: "单行 y=64..79（越界）", 6: "单行 y=80..95（越界）"}[row]
    else:
        m0 = m1 = ""
        note = "probe 拒帧（row > 6）→ 不显示、屏幕保持原样"
    row_table[row] = (m0, m1)
    print(f"     {row}    │ {m0 or '（黑）':<20} │ {m1 or '（黑）':<20} │ {note}")

check("行号 0（全屏）：换行两行 = 模块 0 区域「12」、模块 1 区域「34」（第 3 行 y=32 ≥ h=32 → 不画）",
      row_table[0] == ("12", "34"), str(row_table[0]))
check("行号 1（单行 y 0..15）：模块 0 区域「12」（末 2 字被截断）、模块 1 区域黑",
      row_table[1] == ("12", ""), str(row_table[1]))
check("行号 2（单行 y16..31）：模块 1 区域「12」、模块 0 区域黑",
      row_table[2] == ("", "12"), str(row_table[2]))
check("行号 3~6（单行 y ≥ 32）：两块区域都黑（整行落在 32 行屏之外，fill 直接丢弃）",
      all(row_table[r] == ("", "") for r in range(3, 7)))
check("行号 7：probe 拒帧（不产生任何像素变化）", row_table[7] == ("", ""))

# ================================================================
#  §A-2. 七行现象对照：哪种「工具口径 + 观测前是否清屏」组合能复现现场？
# ================================================================
print("== §A-2. 现场七条观测（34 / 空 / 12 / 空 / 空 / 空 / 空）对照 ==")
OBSERVED = ["34", "", "12", "", "", "", ""]
# 旧固件（模型 A、1×2）：单块模组插 HUB1 = 链首，只保留末 128 位 = 模块 1 区域（y16..31）
VISIBLE = (16, 32)


def simulate(rows_sent: list[int], clear_before_each: bool) -> list[str]:
    scr = Screen()
    out: list[str] = []
    for r in rows_sent:
        if clear_before_each:
            scr = Screen()
        if r <= 6:
            etc_command(scr, r, TEXT)
        out.append(scr.band(*VISIBLE))
    return out


HYPS = [
    ("H1 1-based 行号（第一行=1）+ 不清屏（累积）", [1, 2, 3, 4, 5, 6, 7], False),
    ("H2 1-based 行号 + 每次观测前清屏", [1, 2, 3, 4, 5, 6, 7], True),
    ("H3 0-based 行号（第一行=0，即全屏）+ 不清屏（累积）", [0, 1, 2, 3, 4, 5, 6], False),
    ("H4 0-based 行号 + 每次观测前清屏", [0, 1, 2, 3, 4, 5, 6], True),
]
best = None
for name, rows_sent, clear in HYPS:
    got = simulate(rows_sent, clear)
    hits = sum(1 for a, b in zip(got, OBSERVED) if a == b)
    print(f"  {name}")
    print(f"      预测 {[g or '空' for g in got]}")
    print(f"      实测 {[o or '空' for o in OBSERVED]}   → 命中 {hits}/7")
    check(f"{name}：{'完全复现现场七条观测' if hits == 7 else '与现场不一致（预期）'}",
          hits == 7 if name.startswith("H4") else hits < 7, f"命中 {hits}/7")
    if hits == 7:
        best = name

check("唯一自洽组合 = 0-based 行号 + 观测前清屏（H4）", best is not None and best.startswith("H4"), str(best))
if best:
    info(f"→ 现场七行现象的完整解释 = {best}")

# 关键「不可能性」证明：单行命令永远只能出「12」
only = set()
for r in range(1, 7):
    scr = Screen()
    etc_command(scr, r, TEXT)
    only.add(scr.band(0, 16) or scr.band(16, 32))
check("不可能性证明：行号 1~6 的单行命令在 16px 宽屏上只可能出现「12」或空"
      "（`word_wrap=false` + `w=16` + 8px/字 → 前 2 字；行号 ≥3 整行越界 ⇒ 空）；"
      "任何观测到的「34」都必然来自全屏/滚屏（行号 0）⇒ 现场那一次发的不是单行帧",
      only - {""} == {"12"}, str(only))

# ================================================================
#  §A-3. 与屏幕几何无关的 ETC 口径问题（只记录 + 建议，不改 ETC 模块）
# ================================================================
print("== §A-3. ETC 模块的几何相关既有口径（记录，不改代码）==")
print("""
  · 行高硬编码 `FONT_16`（16px）：协议 6 行假设屏高 ≥ 96px；本 16×32 台架只有行 1~2 落在屏内，
    行 3~6 的 fill/draw 全部越界（`dev_display_fill` 起点越界丢弃、`draw_bitmap` 交集裁剪），
    表现为「发出去没反应」——属**几何上限**，不是缺陷（换 224×64 屏即可见 4 行）。
  · 单行 `word_wrap=false` → 按屏宽截断（16px 宽 = 2 个 ASCII 字）：协议单行语义如此，
    16px 宽屏上必然只显示前 2 字；全屏（行号 0）才按屏宽换行。
  · 单行只清自己那一行（不清全屏）：协议单行语义（保留其它行），与现场观测不冲突。
  · 建议（不改 ETC）：若要在 16×32 台架上逐行验证，请用**行号 0（全屏）**发「每行一段」的
    文本（换行按 16px 折行），或把台架换成 ≥96px 高的屏；16px 宽屏上「行」的可观测性
    天然只有 2 字窗口。
""")

# ================================================================
#  §B-1. 模型 B 机器级取证（现行默认）
# ================================================================
print("== §B-1. 模型 B（现行默认）：1×2 帧结构 / 引脚 / 归属 ==")
tmp = pathlib.Path("/tmp/22_1665_host")
tmp.mkdir(exist_ok=True)

# 两个模块共 2×16×16 = 512 像素：模块 0 全红、模块 1 全绿（行主序平铺）
pat_m0, pat_m1 = bytes([1] * 256), bytes([2] * 256)
pm = pat_m0 + pat_m1

chd.build(tmp / "r27_b_1x2", chd.NEW_SRC,
          {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 2})
# 模型 A 对照锚定**删除前归档驱动**（第二十九轮起模型 A 路径已从现行驱动删除）
chd.build(tmp / "r27_a_1x2", ARCHIVE_SRC,
          {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 2, "_22_1665_HUB_WIRING": 1})
cb = chd.body(chd.run(tmp / "r27_b_1x2", [pm]))[0]
ca = chd.body(chd.run(tmp / "r27_a_1x2", [pm]))[0]

check("模型 B：每帧 CLK = 128（= 128 × MODULE_ROWS；两组并行共享时钟）", cb["K"] == 128, str(cb["K"]))
check("模型 A（旧固件形态）：每帧 CLK = 256（= 128 × 模块总数）", ca["K"] == 256, str(ca["K"]))

touched_b = sorted({f"P{p}{b}" for p, val in cb["T"] for b in range(16)
                    if (val >> b) & 1 or (val >> (16 + b)) & 1})
touched_a = sorted({f"P{p}{b}" for p, val in ca["T"] for b in range(16)
                    if (val >> b) & 1 or (val >> (16 + b)) & 1})
G0 = {"PG9", "PG10", "PG15", "PB6"}
G1 = {"PB8", "PB9", "PE1", "PE2"}
print(f"  模型 B 帧内写过的引脚：{touched_b}")
print(f"  模型 A 帧内写过的引脚：{touched_a}")
check("模型 B：**两组 8 根脚逐帧都被驱动**（组 0 = PG9/PG10/PG15/PB6；组 1 = PB8/PB9/PE1/PE2）",
      set(touched_b) == G0 | G1, str(touched_b))
check("模型 A（旧固件形态）：只写第一组 4 根脚（第二组从未被驱动 → HUB2 全黑）",
      set(touched_a) == G0, str(touched_a))

# 逐组解回 16×16 图像：组 g 的切片 → 该组模块（模型 B：m = g；模型 A：块 0 = 模块 M-1）
def decode_module(slice_hex: str) -> list[str]:
    """把 128 链位切片按「单模块落点模型」解回该模块的 16×16 图像（'R' 红 / 'G' 绿 / '.' 灭）。

    切片每字节 = 一个时钟位的 4 位状态（bit j = 组内第 j 条线的电平）；线序 R1/G1/R2/G2
    对应半屏 0/0/1/1；链位 p → 本模块区域内的 (X, Y)（与驱动落点表同一算式）。
    """
    raw = bytes.fromhex(slice_hex)
    img = [["."] * 16 for _ in range(16)]
    for j in range(4):
        half = 0 if j < 2 else 1
        role = "R" if j % 2 == 0 else "G"
        for p in range(128):
            if not ((raw[p] >> j) & 1):
                continue
            o = chd.region_offset(0, half * 8, 16, 4, p)
            img[o // 16][o % 16] = role
    return ["".join(r) for r in img]


img_b0 = decode_module(cb["S"][0:128 * 2])          # 模型 B：组 0 = 模块 0
img_b1 = decode_module(cb["S"][128 * 2:256 * 2])    # 模型 B：组 1 = 模块 1
img_a0 = decode_module(ca["S"][0:128 * 2])          # 模型 A：块 0 = 模块 M-1 = 模块 1
check("模型 B：组 0 的 128 位 = **模块 0 区域像素**（输入模块 0 = 全红 ⇒ 解回全红 R）",
      all(set(r) == {"R"} for r in img_b0))
check("模型 B：组 1 的 128 位 = **模块 1 区域像素**（输入模块 1 = 全绿 ⇒ 解回全绿 G）",
      all(set(r) == {"G"} for r in img_b1))
check("模型 A：块 0（链首，单块模组所见）= **模块 1 区域像素**（全绿 G）⇒ 旧固件单块只见下半屏",
      all(set(r) == {"G"} for r in img_a0))

# ================================================================
#  §B-2. 现场预测（新固件）：默认画面与 ETC 行 0~7
# ================================================================
print("== §B-2. 新固件现场预测（两块模组各插一口：HUB1 = 组 0 = 逻辑上半，HUB2 = 组 1 = 逻辑下半）==")
src_default = FIELD_SNAP.read_text(encoding="utf-8")
cur_default = DEFAULT_DISPLAY.read_text(encoding="utf-8")
m = re.search(r'\.text\s*=\s*"([^"]*)"', src_default)
text_default = m.group(1).encode().decode("unicode_escape") if m else ""
info(f"现场前提快照 = {FIELD_SNAP.relative_to(ROOT)}（第二十六/二十七轮当时口径，逐字段归档）")
if re.search(r'\.text\s*=\s*"1\\n2"', cur_default) is None:
    info("现行 app_default_display.c 已回改为标准欢迎画面 ⇒ 现场测试前提撤销，§B-2 按历史快照断言")
else:
    info("现行 app_default_display.c 仍是当时的测试画面（与快照一致）")
check("现场前提：默认画面文本 = 两行 `1` / `2`（真换行 `\\n`）、FONT_16、h_align=CENTER、v_align=LEFT_UP",
      m is not None and "\\n" in (m.group(1) if m else "") and "FONT_16" in src_default
      and "ALIGN_CENTER" in src_default and "ALIGN_LEFT_UP" in src_default,
      repr(text_default))

# 用 §A-1 的渲染器复算默认画面：16×32、CENTER、两行 16px
scr = Screen()
scr.fill(0, 0, W, H)
y = 0
for line in text_default.split("\n"):
    line_w = len(line) * CELL_W
    x = 0 + (W - line_w) // 2          # h_align = CENTER（逐行）
    render_text(scr, line, x, y, W, H, False)
    y += CELL_H
info("默认画面「1\\n2」在 16×32 上的落点：模块 0 区域 → " + (scr.band(0, 16) or "（黑）")
     + "；模块 1 区域 → " + (scr.band(16, 32) or "（黑）"))
check("默认画面：上「1」（模块 0 区域）/ 下「2」（模块 1 区域）",
      scr.band(0, 16) == "1" and scr.band(16, 32) == "2")

print("""
  新固件（模型 B）现场预测表（单块模组 / 两块各插一口）：
  ┌────────────┬──────────────────────────┬──────────────────────────┐
  │ 命令       │ 单块接 HUB1（组 0）      │ 单块接 HUB2（组 1）      │
  ├────────────┼──────────────────────────┼──────────────────────────┤
  │ 上电默认   │ 「1」（逻辑上半）        │ 「2」（逻辑下半）        │
  │ ETC 行号 0 │ 「12」（换行第 1 行）    │ 「34」（换行第 2 行）    │
  │ ETC 行号 1 │ 「12」                   │ 无（该行在逻辑上半）     │
  │ ETC 行号 2 │ 无（该行在逻辑下半）     │ 「12」                   │
  │ ETC 行号 3~6 │ 无（整行 y ≥ 32 越界） │ 无（同左）               │
  │ ETC 行号 7 │ 无（probe 拒帧）         │ 无（同左）               │
  └────────────┴──────────────────────────┴──────────────────────────┘
  两块同时插：同一时刻 HUB1 显示逻辑上半、HUB2 显示逻辑下半 —— 行号 1 只出现在 HUB1、
  行号 2 只出现在 HUB2（两块屏合起来才是完整的 16×32 逻辑屏；行号 0 是两行文本上下分开）。
""")
check("预测表自洽：行号 1 只在 HUB1、行号 2 只在 HUB2、行号 0 拆成两块、行号 ≥3 两块皆无",
      row_table[1] == ("12", "") and row_table[2] == ("", "12")
      and row_table[0] == ("12", "34")
      and all(row_table[r] == ("", "") for r in range(3, 8)))

# 模型 B 的「单块接 HUB1 就该看到整块内容」机器级证据：组 0 切片 = 模块 0 图案的 1×1 产物
chd.build(tmp / "r27_1x1", chd.NEW_SRC, {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 1})
c11 = chd.body(chd.run(tmp / "r27_1x1", [pat_m0]))[0]
check("模型 B：单块接 HUB1 收到的 128 位 = 该模块图案在 1×1 口径下的编译产物逐字节一致"
      "（= 整块 16×16 内容，不再丢首 128 位）",
      cb["S"][0:256] == c11["S"], f"{cb['S'][:24]}… vs {c11['S'][:24]}…")
check("模型 B vs 模型 A：同一输入的帧状态**不同**（模型 B 组 0 = 模块 0、模型 A 块 0 = 模块 1）",
      cb["S"] != ca["S"])

# 源码事实：接线模型开关与数据线表
src_drv = DRIVER.read_text(encoding="utf-8")
check("源码事实（第二十九轮固化）：`_22_1665_HUB_WIRING` 开关**已删除**（唯一形态 = 每列一口）",
      "_22_1665_HUB_WIRING" not in src_drv
      or re.search(r"#define\s+_22_1665_HUB_WIRING", src_drv) is None)
check("源码事实：组 1 行 = 通道 2/3（R3/G3/R4/G4）—— 兄弟驱动通道序 2g / 2g+1",
      src_drv.count("&g_hub75_pin_r[2]") == 1 and src_drv.count("&g_hub75_pin_g[2]") == 1
      and src_drv.count("&g_hub75_pin_r[3]") == 1 and src_drv.count("&g_hub75_pin_g[3]") == 1)

print()
if FAILURES:
    print("失败项：")
    for f in FAILURES:
        print(f"  - {f}")
print(f"结果：通过 {PASS} / 失败 {FAIL}")
sys.exit(1 if FAIL else 0)
