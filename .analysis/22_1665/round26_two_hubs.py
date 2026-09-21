#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 **第二十六轮**：现场「两块模组各插一个 HUB 口」现象判定。

现场事实（2026-09-17 傍晚，用户原话）：
  「我现在接了两张模组，一张接第一个 hub 一张接第二个 hub，默认显示 "1\\n2" 16 点阵，
    应该第一模组显示 1，第二张模组显示 2。实际第一张模组显示 2，第二张模组没有显示。」
（宏值 = `_22_1665_MODULE_ROWS=1` / `_22_1665_MODULE_COLS=2` → 逻辑屏 16 宽 × 32 高，模块数 M=2。）

本脚本用**现行驱动的编译产物**（宿主 x86-64 直跑）+ 独立复写的两个模型回答：

  §1 默认画面 "1\\n2" 在 16×32 上的**真实落点**（判定链第一环）：
     源码事实（正则断言）+ 独立复写的 `_render_text` 两趟布局 → 逐行字形框；
     并断言第二行**不会被裁剪**（text_h = 2×16 = cfg.h = 32）。
  §2 帧位流归属（机器级）：每时钟写序 → 验证「先移入 = state[255] … 最后移入 = state[0]」；
     块 0 = 链首模块（贴数据脚那块）留下的 128 位、块 1 = 链尾模块的 128 位；
     用单模块落点模型把两块各解回 16×16 图像并**逐像素**对比输入字形。
  §3 三种接线假设（H1 两口并联 / H2 各占独立一组脚 / H3 物理级联）逐块预测 vs 现场。
  §4 `_22_1665_CHAIN_HEAD_IS_MODULE0` 开关的现场效果（能让第一块出「1」，但第二块仍黑）。
  §5 「第二组数据脚从未被驱动」的机器级证据（全帧写过的 (端口,位) 集合）。
  §6 现场可立即执行的实验清单（打印，供现场照做）。

用法：python3 .analysis/22_1665/round26_two_hubs.py
输出：stdout（人读 + 可归档）；同目录报告 `round26_two_hubs.md` 引用其结论。
"""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST = ROOT / ".analysis/22_1665/host"
STUB = HOST / "stub"
HARNESS = HOST / "host_driver.c"
# 本脚本取证的是「第二十六轮当时板上固件」= **模型 A（同线级联）+ 只写第一组 4 根脚**。
# 模型 A 代码路径与 `_22_1665_HUB_WIRING` 开关已于**第二十九轮删除**（生产化收口）——
# 故本历史脚本锚定**删除前归档驱动**（行为与当时逐字节一致），不再指向现行驱动：
#   · 归档：`.analysis/22_1665/archive/finalize/driver_pre_finalize_20260917.c`
#   · 恢复路径见 `.analysis/22_1665/README.md` 与 `archive/README.md`
SRC = (ROOT / ".analysis/22_1665/archive/finalize/"
       "driver_pre_finalize_20260917.c")
DEFAULT_DISPLAY = ROOT / "Application/Src/app_default_display.c"
# **现场前提快照**（第二十九轮新增）：本脚本取证的是第二十六轮当时的口径
# = 默认画面为测试串 `"1\n2"`。该测试画面是**外部改动**（非驱动），第二十八轮后已回改为
# 标准欢迎画面 → 「现场前提」在现行树上不再成立。故本文的源码事实断言**锚定归档快照**
# （`archive/finalize/app_default_display_test_1n2_20260917.c`，逐字段记录当时 `_render_welcome`），
# 并对现行树只做「记录」不做判红 —— 历史脚本保持可解释、不让 verify_all.sh 变红。
FIELD_SNAP = (ROOT / ".analysis/22_1665/archive/finalize/"
              "app_default_display_test_1n2_20260917.c")
RENDER = ROOT / "Application/Src/app_render.c"
PL_HUB75 = ROOT / "Platform/Src/pl_hub75.c"

TMP = pathlib.Path("/tmp/22_1665_round26")
TMP.mkdir(exist_ok=True)

PIX_ROW, PIX_COL = 16, 16          # 单模块像素（22-1665：16×16）
SEG = 128                          # 单模块单线链段位数 = 8 片 × 16 位
HALF = (0, 0, 1, 1)                # 线 j（R1/G1/R2/G2）覆盖的半屏：0 上 / 1 下
LINE_ROLE = ("RED", "GREEN", "RED", "GREEN")
ROLE_BIT = {                       # BLUE_AS_LIT=1：蓝分量并入红绿
    "RED":   [0, 1, 0, 1, 1, 1, 1, 1],
    "GREEN": [0, 0, 1, 1, 1, 1, 1, 1],
}

# 宿主桩的引脚掩码（host_driver.c；与真板 Core/Inc/main.h 一致）
HOST_PIN = {                      # 线 j → (端口, 掩码位)
    0: ("G", 0x0200),             # R1 = PG9
    1: ("G", 0x0400),             # G1 = PG10
    2: ("G", 0x8000),             # R2 = PG15
    3: ("B", 0x0040),             # G2 = PB6
}
# 第二组数据线候选（兄弟驱动通道序 = 通道 2/3 的 R/G；**假设，须现场核对**）
CAND_GROUP2 = {("B", 8), ("B", 9), ("E", 1), ("E", 2)}

PASS = 0
FAIL = 0


def check(desc: str, cond: bool, extra: str = "") -> None:
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [通过] {desc}")
    else:
        FAIL += 1
        print(f"  [失败] {desc} {extra}")


def info(desc: str) -> None:
    """只记录、不判红（历史前提类事实用）。"""
    print(f"  [记录] {desc}")


# ================================================================
#  一、宿主编译 / 运行（与 check_host_differential.py 同一套桩 + harness）
# ================================================================
def build(out: pathlib.Path, over: dict[str, int]) -> None:
    # 本脚本取证的是「第二十六轮当时板上固件」= **模型 A（同线级联）+ 只写第一组 4 根脚**；
    # 第二十七轮起默认 = 模型 B（每列一口独立线，组 1 逐帧驱动）——见 round27。
    over = {"_22_1665_HUB_WIRING": 1, **over}
    cmd = ["gcc", "-std=gnu23", "-O2", "-Wall", "-Wextra", "-funsigned-char",
           "-I", str(STUB), "-o", str(out), str(HARNESS), str(SRC)]
    cmd += [f"-D{k}={v}" for k, v in over.items()]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"宿主编译失败：\n{r.stderr[:2000]}")
    if r.stderr.strip():
        raise SystemExit(f"宿主编译有告警（须零告警）：\n{r.stderr[:2000]}")


def run(binary: pathlib.Path, patterns: list[bytes]) -> tuple[dict, list[dict]]:
    r = subprocess.run([str(binary)], input=b"".join(patterns), capture_output=True)
    if r.returncode != 0:
        raise SystemExit(f"宿主运行失败：{r.stderr[:1000].decode(errors='replace')}")
    geom, cases, cur = None, [], None
    for line in r.stdout.decode().splitlines():
        if line.startswith("GEOM "):
            geom = dict(kv.split("=") for kv in line[5:].split())
        elif line.startswith("CASE "):
            cur = {}
            cases.append(cur)
        elif line.startswith("S "):
            cur["S"] = bytes.fromhex(line[2:])
        elif line.startswith("T "):
            f = line[2:].split()
            cur["T"] = [(x[0], int(x[1:], 16)) for x in f[1:]] if len(f) > 1 else []
        elif line.startswith("K "):
            cur["K"] = int(line[2:])
        elif line.startswith("R "):
            cur["R"] = line[2:]
    return geom, cases


# ================================================================
#  二、两个独立复写模型
# ================================================================
def region_offset(x0: int, y0: int, screen_rows: int, blk_cols: int, p: int) -> int:
    """单模块落点模型（第十二轮现场反解，独立复写；与驱动 `_22_1665_region_offset` 同式）。"""
    c, i = divmod(p, 16)
    bc = (blk_cols - 1) - (c % blk_cols)
    br = c // blk_cols
    dx = 3 - (i % 4)
    dy = ((i // 4) + 2) % 4
    return (y0 + 4 * br + dy) * screen_rows + (x0 + 4 * bc + dx)


def decode_segment(seg: bytes, screen_rows: int) -> list[list[int]]:
    """把「单模块 128 位链段」（seg[p'] bit j = 线 j 电平）解回 16×16 颜色索引图。"""
    img = [[0] * PIX_ROW for _ in range(PIX_COL)]
    for j in range(4):
        for p in range(SEG):
            if not ((seg[p] >> j) & 1):
                continue
            o = region_offset(0, HALF[j] * (PIX_COL // 2), screen_rows, 4, p)
            yy, xx = divmod(o, screen_rows)
            bit = 0x01 if LINE_ROLE[j] == "RED" else 0x02
            img[yy][xx] |= bit
    return img


def ascii_img(img: list[list[int]]) -> list[str]:
    ch = {0: ".", 1: "R", 2: "G", 3: "Y"}
    return ["".join(ch[v & 3] for v in row) for row in img]


def render_text_layout(text: str, font_size: int, w: int, h: int,
                       h_align_center: bool = True, word_wrap: bool = False):
    """独立复写 `app_render.c::_render_text` 的**两趟布局**（FONT_16 / ASCII 文本）。

    返回 [(x0, y0, gw, font_size), ...]：每个字形的落笔框（左上角 + 尺寸）。
    口径（逐条对应源码）：
      · ASCII 字形宽 = font_size / 2（`_glyph_width_px`）；
      · 测量趟 `'\\n'` → 记账当前行宽、行数 +1；渲染趟 `'\\n'` → `cur_y += line_h`；
      · v_align = ALIGN_LEFT_UP → 不加上偏移；h_align = ALIGN_CENTER → 每行 x0 += (w - 行宽)/2；
      · word_wrap 关闭时「超宽不换行、截断且不计宽」，且**没有** `cur_y >= h` 早退分支。
    """
    line_h = font_size
    lines: list[list[tuple[int, int]]] = [[]]      # 每行 [(char, gw)]
    for ch_ in text:
        if ch_ == "\n":
            lines.append([])
            continue
        gw = font_size // 2                        # ASCII
        if sum(g for _, g in lines[-1]) + gw > w and not word_wrap:
            continue                               # 截断（不计宽、不落笔）
        lines[-1].append((ch_, gw))

    text_h = len(lines) * line_h
    y0 = 0
    if text_h > h:                                 # LEFT_UP：不加偏移（居中方会）
        pass
    boxes = []
    for li, ln in enumerate(lines):
        lw = sum(g for _, g in ln)
        x = (w - lw) // 2 if h_align_center else 0
        for _, gw in ln:
            boxes.append((x, y0 + li * line_h, gw, line_h))
            x += gw
    return boxes, text_h


# 8×16 合成字形（MSB-first 行打包，与真实字库同布局；只为「一眼看出哪块是哪一行」）
GLYPHS = {
    "1": ["........",
          "...#....",
          "..##....",
          "...#....",
          "...#....",
          "...#....",
          "...#....",
          "...#....",
          "...#....",
          "...#....",
          "...#....",
          "...#....",
          "...#....",
          "...#....",
          ".#####..",
          "........"],
    "2": ["........",
          "..####..",
          ".#....#.",
          "......#.",
          "......#.",
          ".....#..",
          "....#...",
          "...#....",
          "..#.....",
          ".#......",
          "#.......",
          "#.......",
          "#.......",
          "#.....#.",
          "#######.",
          "........"],
}


def blit_glyph(buf: bytearray, screen_rows: int, x: int, y: int, color: int, glyph: list[str]) -> None:
    """把 8×16 合成字形按 MSB-first 画进整屏缓冲（等价 `dev_display_draw_bitmap` 的位序）。"""
    for row, bits in enumerate(glyph):
        for col, ch_ in enumerate(bits):
            if ch_ == "#":
                buf[(y + row) * screen_rows + (x + col)] = color


def glyph_box_pattern(text: str, colors: dict[str, int], rows: int, cols: int) -> bytes:
    """按 `render_text_layout` 的落点把**合成字形**画进整屏 pixel_map。

    真机流程 = `dev_display_fill(框, 黑)` + `dev_display_draw_bitmap(字形)`；
    这里等价地：框内先清黑，再按位图画字形（MSB-first，8 列 → 1 字节/行）。
    """
    screen_rows = rows * PIX_ROW
    buf = bytearray(screen_rows * cols * PIX_COL)
    boxes, _ = render_text_layout(text, 16, screen_rows, cols * PIX_COL)
    for (x, y, gw, gh), ch_ in zip(boxes, [c for c in text if c != "\n"]):
        for yy in range(y, y + gh):                       # fill(框, 黑)
            for xx in range(x, x + gw):
                buf[yy * screen_rows + xx] = 0
        blit_glyph(buf, screen_rows, x, y, colors[ch_], GLYPHS[ch_])
    return bytes(buf)


def glyph_module_ref(ch_: str, color: int) -> list[list[int]]:
    """单个 16×16 模块收到「第 k 行内容」时应有的像素图（字形框 x=4、局部 y=0）。"""
    img = [[0] * PIX_ROW for _ in range(PIX_COL)]
    for row, bits in enumerate(GLYPHS[ch_]):
        for col, c in enumerate(bits):
            if c == "#":
                img[row][4 + col] = color
    return img


# ================================================================
#  三、主流程
# ================================================================
def main() -> int:
    print("=" * 78)
    print("22-1665 第二十六轮：两块模组各插一个 HUB 口（HUB1 显 \"2\" / HUB2 全黑）判定")
    print("=" * 78)

    # ------------------------------------------------------------------
    print("\n== 0. 宿主编译（现行驱动；-Wall -Wextra 零告警）==")
    print("   用户当前宏值：_22_1665_MODULE_ROWS=1 / _22_1665_MODULE_COLS=2（逻辑屏 16×32）")
    build(TMP / "d_1x2", {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 2})
    check("1×2（= 用户当前宏值）编过且零告警", True)
    build(TMP / "d_flip", {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 2,
                           "_22_1665_CHAIN_HEAD_IS_MODULE0": 1})
    check("1×2 + `-D_22_1665_CHAIN_HEAD_IS_MODULE0=1`（现场 A/B 口径）编过且零告警", True)
    build(TMP / "d_1x1", {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 1})
    check("1×1（对照 / 硬件基线）编过且零告警", True)

    geom2, _ = run(TMP / "d_1x2", [])
    print(f"  基类字段：{geom2}")
    check("1×2 基类字段 screen=16x32 buffer=512 scanline=256 modules=1x2",
          geom2 == dict(screen="16x32", buffer="512", scanline="256", modules="1x2"), str(geom2))

    # ------------------------------------------------------------------
    print("\n== 1. 判定链第一环：默认画面 \"1\\n2\"（FONT_16）在 16×32 上的真实落点 ==")

    # 1.1 源码事实（正则断言，不靠记忆）
    # 现场前提（默认画面 = 测试串 "1\n2"）锚定**归档快照**；现行树只记录（见文件头说明）
    src_disp = FIELD_SNAP.read_text(encoding="utf-8")
    cur_disp = DEFAULT_DISPLAY.read_text(encoding="utf-8")
    src_render = RENDER.read_text(encoding="utf-8")
    info(f"现场前提快照 = {FIELD_SNAP.relative_to(ROOT)}（第二十六轮当时口径，逐字段归档）")
    if re.search(r'\.text\s*=\s*"1\\n2"', cur_disp) is None:
        info("现行 Application/Src/app_default_display.c 已回改为标准欢迎画面"
             "（「欢迎行驶\\n高速公路」/ FONT_SELF_ADAPT）⇒ 现场测试前提撤销，本项按历史快照断言")
    else:
        info("现行 Application/Src/app_default_display.c 仍是当时的测试画面（与快照一致）")
    check('现场前提：默认画面文本 = "1\\n2"（C 转义真换行）',
          re.search(r'\.text\s*=\s*"1\\n2"', src_disp) is not None)
    check("现场前提：FONT_16 + FONT_HT",
          re.search(r"\.font_size\s*=\s*FONT_16", src_disp) is not None and
          re.search(r"\.font_type\s*=\s*FONT_HT", src_disp) is not None)
    check("现场前提：h_align=CENTER / v_align=ALIGN_LEFT_UP（word_wrap 未设 = 关）",
          "ALIGN_CENTER" in src_disp and "ALIGN_LEFT_UP" in src_disp and
          "word_wrap" not in src_disp)
    check("现场前提：w = screen_rows（16）/ h = screen_cols（32）",
          "screen_rows" in src_disp and "screen_cols" in src_disp)
    check("app_render.c：ASCII 字形宽 = size/2（`_glyph_width_px`）",
          re.search(r"return\s*\(charset\s*==\s*FONT_ENC_ASCII\)\s*\?\s*size\s*/\s*2", src_render)
          is not None)
    check("app_render.c：'\\n' 渲染趟走 `cur_y += line_h`（换行推进一行）",
          re.search(r"==\s*'\\n'\s*\)\s*\{\s*\n\s*cur_y\s*\+=\s*line_h", src_render) is not None)
    check("app_render.c：`cur_y >= cfg->h` 早退只存在于 word_wrap 分支（本画面 word_wrap=关）",
          src_render.count("if (cur_y >= cfg->h) return;") == 2 and
          src_render.count("word_wrap") > 0)

    # 1.2 独立布局模型
    boxes, text_h = render_text_layout("1\n2", 16, 16, 32)
    print(f"  独立复写布局模型输出：字形框 {boxes}；文本高 {text_h}（cfg.h = 32）")
    check("两行字形框 = [(4,0,8,16), (4,16,8,16)]（水平居中 (16-8)/2 = 4）",
          boxes == [(4, 0, 8, 16), (4, 16, 8, 16)], str(boxes))
    check("第 1 行字形框整体落在**模块 0 区域**（y 0..15 = 逻辑上半屏）",
          boxes[0][1] + boxes[0][3] <= 16)
    check("第 2 行字形框整体落在**模块 1 区域**（y 16..31 = 逻辑下半屏）",
          boxes[1][1] >= 16 and boxes[1][1] + boxes[1][3] <= 32)
    check("文本高 32 = cfg.h → **两行都放得下**（第二行不被裁剪、不被丢弃）",
          text_h == 32)
    print("  结论：模块 0 区域装「1」、模块 1 区域装「2」——两行都真实落屏（非空）。")

    # 1.3 生成 pixel_map（字形框代理图案；上半「1」= 红、下半「2」= 绿，便于逐块辨认）
    pat_1n2 = glyph_box_pattern("1\n2", {"1": 1, "2": 2}, 1, 2)
    check("pixel_map 尺寸 = 512B（16×32）", len(pat_1n2) == 512)

    # ------------------------------------------------------------------
    print("\n== 2. 帧位流归属（机器级：现行 1×2 驱动的实际输出）==")
    _, cases = run(TMP / "d_1x2", [pat_1n2])
    c = cases[0]
    st, trace = c["S"], c["T"]
    check("每帧 CLK = 256（= 128 × 2 模块）且 frame_state 长 256B", c["K"] == 256 and len(st) == 256)
    check("每帧 BSRR 写序 = 256 时钟 × 2 端口（G/B）= 512 项", len(trace) == 512)

    # 2.1 扫描序：第 k 个时钟发出的应是 state[255-k]（先移入 = p 最大 = state[255]）
    def expect_writes(st_byte: int) -> list[tuple[str, int]]:
        out = []
        for j in range(4):
            port, mask = HOST_PIN[j]
            val = mask if (st_byte >> j) & 1 else (mask << 16)
            out.append((port, val))
        # 驱动按端口槽顺序 flush：槽 0 = GPIOG（R1/G1/R2 合并），槽 1 = GPIOB（G2）
        merged: dict[str, int] = {}
        for port, val in out:
            merged[port] = merged.get(port, 0) | val
        return [("G", merged["G"]), ("B", merged["B"])]

    order_ok = True
    for k in range(256):
        if trace[2 * k:2 * k + 2] != expect_writes(st[255 - k]):
            order_ok = False
            break
    check("扫描序 = p 递减（第 k 个时钟 = state[255-k]）：先移入 state[255]、最后移入 state[0]",
          order_ok)

    # 2.2 分块 + 解回单模块图像
    blk_head = st[0:SEG]        # 最后移入 → 停在链首（贴着数据脚那块）
    blk_tail = st[SEG:2 * SEG]  # 最先移入 → 被推到链尾
    img_head = decode_segment(blk_head, 16)
    img_tail = decode_segment(blk_tail, 16)
    print("\n  [链首模块（贴数据脚那块，单块单独接时就是它）解回 16×16 像素]")
    for row in ascii_img(img_head):
        print("    " + row)
    print("  [链尾模块（本级联链上的第二块）解回 16×16 像素]")
    for row in ascii_img(img_tail):
        print("    " + row)

    # 2.3 逐像素断言：块 0 = 第 2 行「2」、块 1 = 第 1 行「1」
    ref_line2 = glyph_module_ref("2", 2)   # 第 2 行（模块 1 区域）→ 绿色「2」
    ref_line1 = glyph_module_ref("1", 1)   # 第 1 行（模块 0 区域）→ 红色「1」
    check("块 0（最后移入 → 停在链首）内容 == **模块 1 区域（第 2 行 =「2」）**（逐像素）",
          img_head == ref_line2)
    check("块 1（最先移入 → 推到链尾）内容 == **模块 0 区域（第 1 行 =「1」）**（逐像素）",
          img_tail == ref_line1)
    lit_head = [row for row in range(16) if any(img_head[row])]
    lit_tail = [row for row in range(16) if any(img_tail[row])]
    print(f"  链首块点亮行 {lit_head[0]}..{lit_head[-1]}（字形框局部 y 0..15）、"
          f"链尾块点亮行 {lit_tail[0]}..{lit_tail[-1]}")

    # ------------------------------------------------------------------
    print("\n== 3. 三种接线假设 vs 现场观测（HUB1 显「2」/ HUB2 全黑）==")
    print("""
  现场两条观测：① HUB1 那块显示「2」；② HUB2 那块**完全没有显示**（默认画面下）。
  逐假设预测（由上面机器级块内容推出；「真级联」= 模组 A 的 SDO 接模组 B 的 SDI）：

  ┌────┬────────────────────────────┬──────────────────────┬──────────────────────┬────────────┐
  │ 假设 │ 接线                        │ HUB1 那块显示         │ HUB2 那块显示         │ 与现场     │
  ├────┼────────────────────────────┼──────────────────────┼──────────────────────┼────────────┤
  │ H1 │ 两口 4 根数据脚**并联**（同一组 MCU 脚）│ 末 128 位 = 「2」     │ 也是链首、同样「2」   │ ❌ 否证（HUB2 应亮「2」）│
  │ H2 │ **两口各占独立一组脚**（硬件 = 模型 B）│ 末 128 位 = 「2」     │ 该组脚从未被驱动 → 黑 │ ✅ 完全自洽 │
  │ H3 │ 模组 A(口 1) → 模组 B(口 2) **物理级联**│ 末 128 位 = 「2」     │ 首 128 位 = 「1」     │ ❌ 否证（HUB2 应亮「1」）│
  └────┴────────────────────────────┴──────────────────────┴──────────────────────┴────────────┘

  结论：**只有 H2（每口一组独立数据脚 = 模型 B 硬件）能同时解释两条观测**。
  · H1 被「HUB2 全黑」否证：并联时两块收到的位流完全相同，不可能一块亮一块黑
    （除非 HUB2 的 CLK/LAT/OE 另走一路且未接——见 §6 实验 E4 的示波器判据）。
  · H3 被「HUB2 全黑」否证：真级联时链尾那块必然保留**最先移入**的 128 位 = 模块 0 =「1」，
    现场应看到「1」而不是黑；若仍是黑，只能是级联信号没接通/模组没供电，
    那已等价于「第二块没有任何数据源」，修复路径与 H2 相同（补接或补驱动）。
""")

    # ------------------------------------------------------------------
    print("== 4. 现场 A/B 开关 `_22_1665_CHAIN_HEAD_IS_MODULE0=1` 的效果 ==")
    _, cf = run(TMP / "d_flip", [pat_1n2])
    sf = cf[0]["S"]
    img_f_head = decode_segment(sf[0:SEG], 16)
    img_f_tail = decode_segment(sf[SEG:2 * SEG], 16)
    check("flip 口径：链首块改看**模块 0**（第 1 行 =「1」）", img_f_head == ref_line1)
    check("flip 口径：链尾块改看**模块 1**（第 2 行 =「2」）", img_f_tail == ref_line2)
    print("""
  行为：开关只把「帧内哪 128 位归哪个模块」**对调**，不增加任何被驱动的引脚。
  · 单块接 HUB1（或真级联的链首）→ 默认口径「2」，flip 口径「1」（**能对上"第一块显示 1"的期望**）；
  · HUB2 那块：两种口径下**仍然全黑**（该组脚从未进过 `_22_1665_lines`，见 §5）。
  ⇒ flip 是「先让第一块内容对」的现场开关，**不是**双口问题的修复。
     编译：make -j8 DISP=22_1665 时加 `-D_22_1665_CHAIN_HEAD_IS_MODULE0=1`
     （或 EIDE 的 defineList 加同名宏；改完 EIDE 需 Reload Project）。
""")

    # ------------------------------------------------------------------
    print("== 5. 「第二组数据脚从未被驱动」的机器级证据 ==")
    touched = set()
    for port, val in trace:
        for bit in range(16):
            if (val >> bit) & 1 or (val >> (16 + bit)) & 1:
                touched.add((port, bit))       # 置位 / 复位都算「该脚被写过」
    shown = sorted(f"P{p}{b}" for p, b in touched)
    print(f"  全帧 512 项 BSRR 写序中出现过的引脚（置位或复位）：{shown}")
    check("写过的引脚恰为 4 根：PG9 / PG10 / PG15 / PB6（= _22_1665_lines[4]）",
          touched == {("G", 9), ("G", 10), ("G", 15), ("B", 6)}, str(sorted(touched)))
    cand = CAND_GROUP2
    check("候选「第二组」4 根脚（R3/G3/R4/G4 = PB8/PB9/PE1/PE2）**一次都没写过**",
          (touched & cand) == set(), str(sorted(touched & cand)))
    src_drv = SRC.read_text(encoding="utf-8")
    check("源码事实（模型 A 编译）：组 0 行 = 现行第一组 4 根（`g_hub75_pin_r[0]/g[0]/r[1]/g[1]`）",
          src_drv.count("&g_hub75_pin_r[0]") == 1 and src_drv.count("&g_hub75_pin_g[0]") == 1 and
          src_drv.count("&g_hub75_pin_r[1]") == 1 and src_drv.count("&g_hub75_pin_g[1]") == 1)
    check("源码事实（第二十七轮更新）：组 1 行 = 通道 2/3 = R3/G3/R4/G4（PB8/PB9/PE1/PE2）"
          "—— 模型 B（现行默认）逐帧驱动该组",
          src_drv.count("&g_hub75_pin_r[2]") == 1 and src_drv.count("&g_hub75_pin_g[2]") == 1 and
          src_drv.count("&g_hub75_pin_r[3]") == 1 and src_drv.count("&g_hub75_pin_g[3]") == 1)
    print("""
  引脚表（`Platform/Src/pl_hub75.c` → `Core/Inc/main.h`）：
    R1=PG9  G1=PG10  R2=PG15  G2=PB6   ← 现行 4 根（现场第二十轮逐脚定标）
    R3=PB8  G3=PB9   R4=PE1   G4=PE2   ← 兄弟驱动通道序里的「第二组」（**假设，须现场核对**）
  ⇒ 若 HUB2 的 4 根数据脚确实来自第二组，则该口**一根脚都没被写过** → 必然全黑。
""")

    # ------------------------------------------------------------------
    print("== 6. 现场实验清单（每次只改一件事；判读唯一）==")
    print("""
  前置：RTT 可见时先看开机横幅是否有 `[diag] display screen=16x32 code=2200001665 modules=1x2`
        （有 = 板上是 1×2 镜像；无 = 旧镜像，先重烧；本轮已用 nm 核对板上 elf：
         `_22_1665_frame_state` 256B / `pixel_map` 512B / `chain_dst` 2048B ⇒ M=2 ✔）

  E1 互换两口：把 HUB2 那块换到 HUB1、原 HUB1 那块换到 HUB2
     → 预期：原来黑的那块在 HUB1 上显示出「2」，原来出「2」的换到 HUB2 后变黑
     说明：口的问题（HUB2 那一路没有数据），模组本身是好的。
     （若黑的那块**换到 HUB1 依旧黑** ⇒ 模组/排线/供电问题，先查硬件，别改固件。）

  E2 单块接 HUB2，用 1×1 口径（`MODULE_ROWS=1 / MODULE_COLS=1`）烧一版
     → 预期：全黑 ⇒ HUB2 不是并联、也没有可用数据脚（模型 B 成立）
       若显示「1」⇒ HUB2 与 HUB1 是同一组脚（并联）或共用数据，需回到 §3 的 H1 变体排查

  E3 单块接 HUB1，1×1 口径
     → 预期：显示「1」（单模块收 128 位 = 整屏内容；默认画面第 1 行）
     说明：第一块模组 + HUB1 通路完全正常（已知良好基线）。

  E4 示波器/逻辑分析仪测 HUB2 连接器：CLK(PD1)/LAT(PD3)/OE(PD0)/任一根数据脚
     → 预期：CLK 有 128×M 个脉冲/帧（帧周期 500µs，≈0.26~0.5MHz 突发）；
        LAT 每帧一个窄脉冲；OE 有 PWM；数据脚**无任何跳变**
     说明：「只有数据脚没驱动」⇒ 模型 B，补第二组脚即可；
       「CLK/LAT/OE 也没有」⇒ HUB2 整口未接 MCU（或硬件未装配），需查板。

  E5 万用表通断档：从 MCU 候选脚（PB8/PB9/PE1/PE2，必要时加 PE0/PE3）量到 HUB2 座的
     R/G 数据 pin（**必须现场实测，不要照搬"通道序"假设**）→ 得到第二组 4 根脚的真实归属
     说明：这是实现「HUB1 出 1 / HUB2 出 2」**唯一必需**的硬件信息。

  E6 若两块模组背面有第二个 16P 座（OUT）：把第二块串到第一块的 OUT（真级联），
     先不动固件（默认口径）→ 预期链首「2」、链尾「1」；
     再烧 `-D_22_1665_CHAIN_HEAD_IS_MODULE0=1` → 预期链首「1」、链尾「2」（正好符合期望）
     说明：级联成立 ⇒ **零代码改动**即可达到「一块 1、一块 2」。（口径见 §4）

  E7 用 1×1 口径 + 单块 HUB1：把默认画面文本临时改成 "2"（或只画第 2 行内容）
     → 预期显示「2」；说明第二块模组单独也能显示，问题只在通路，不在模组。
""")

    # ------------------------------------------------------------------
    print("=" * 78)
    print("结论：现场现象 = **H2（两口各占一组独立数据脚，硬件 = 模型 B）** +")
    print("      驱动只写第一组 4 根脚（R1/G1/R2/G2 = PG9/PG10/PG15/PB6）；")
    print("      HUB1 那块的 128 位移位寄存器留下的正是**帧末 128 位 = 模块 1 = 第 2 行 =「2」**，")
    print("      HUB2 那块的 4 根数据脚（第二组）从未被写过 → 全黑。")
    print("      ⇒ 达成「HUB1 出 1 / HUB2 出 2」需要**第二组数据脚的真实脚位**（或改成真级联接线）。")
    print("=" * 78)
    if FAIL:
        print(f"结果：通过 {PASS} / 失败 {FAIL}")
    else:
        print(f"结果：通过 {PASS} / 失败 {FAIL}（全部断言通过）")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
