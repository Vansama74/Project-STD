#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 **第二十五轮**：1×2（16×32）「两块模组都不显示」的宿主级取证。

回答三个问题（全部用**编译产物**跑出来的帧位流 + 独立复写的面板模型）：

  Q1  1×2 驱动的**逐字输出**是什么：
      pixel_map 分块 → frame_state[256] 逐位 → 每根数据线 256 位流 → 每时钟 BSRR 写序；
      并给出「模块 m 装在帧内哪 128 位」的机器级证据（用可区分的模块图案反查）。
  Q2  在**三种接线模型**下，每个物理模块各显示什么（ASCII 渲染，逐像素）：
      · SEPARATE（两级独立线 / 各占一个 HUB75 口，板端只驱动口 1 的 4 根脚）
        —— 板端口 1 的模组整帧收 256 时钟、只留最后 128 位；口 2 的模组一根脚都不动 → 全黑
      · CHAIN_NEAR_IS_LAST（同线级联，板端那块 = 链首 = 收末 128 位 = 现行代码的「模块 1」）
        —— 现行 1×2 代码在「单块插口 1」时的行为
      · CHAIN_NEAR_IS_FIRST（同线级联，板端那块 = 链首 = 收末 128 位 = 文档散文口径的「模块 0」）
        —— 反转模块索引后的行为（`-D_22_1665_CHAIN_HEAD_IS_MODULE0=1`）
  Q3  用户现场观测（默认画面全黑 / 按 TEST 出 "MD"）与哪个模型自洽。

用法：python3 .analysis/22_1665/round25_two_modules.py
输出：stdout（人读 + 可归档；报告引用其结论）
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
# 本脚本取证的是**模型 A（同线级联）**形态（第二十五轮现场口径）。模型 A 代码路径与
# `_22_1665_HUB_WIRING` 开关已于**第二十九轮删除**（现场验证完毕，生产化收口）——
# 故本历史脚本锚定**删除前归档驱动**（行为与当时逐字节一致），不再指向现行驱动：
#   · 归档：`.analysis/22_1665/archive/finalize/driver_pre_finalize_20260917.c`
#   · 恢复路径见 `.analysis/22_1665/README.md` 与 `archive/README.md`
SRC = (ROOT / ".analysis/22_1665/archive/finalize/"
       "driver_pre_finalize_20260917.c")

TMP = pathlib.Path("/tmp/22_1665_round25")
TMP.mkdir(exist_ok=True)

PIX_ROW, PIX_COL = 16, 16          # 单模块像素（22-1665：16×16）
SEG = 128                          # 单模块单线链段位数 = 8 片 × 16 位
LINES = ("R1", "G1", "R2", "G2")   # 数据线序（现场定标）
HALF = (0, 0, 1, 1)                # 线 j 覆盖的半屏（0 上 / 1 下）

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


# ================================================================
#  一、宿主编译 / 运行（与 check_host_differential.py 同一套桩 + harness）
# ================================================================
def build(out: pathlib.Path, src: pathlib.Path, over: dict[str, int]) -> None:
    # 本脚本取证的是**模型 A（同线级联）**形态（第二十五轮现场口径）；模型 B 为现行默认，
    # 其行为与预测见 round27（`.analysis/22_1665/round27_two_modules_wiring_b.py`）。
    # **第二十九轮**：`src` 恒为归档驱动（模型 A 路径已删），`HUB_WIRING=1` 仅对归档有效。
    over = {"_22_1665_HUB_WIRING": 1, **over}
    cmd = ["gcc", "-std=gnu23", "-O2", "-Wall", "-Wextra", "-funsigned-char",
           "-I", str(STUB), "-o", str(out), str(HARNESS), str(src)]
    cmd += [f"-D{k}={v}" for k, v in over.items()]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"宿主编译失败（{src.name}）：\n{r.stderr[:2000]}")
    if r.stderr.strip():
        raise SystemExit(f"宿主编译有告警（须零告警，{src.name}）：\n{r.stderr[:2000]}")


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
#  二、单模块落点模型（第十二轮现场反解，独立复写；用于把 128 位链段解回像素）
# ================================================================
def region_offset(x0: int, y0: int, screen_rows: int, blk_cols: int, p: int) -> int:
    c, i = divmod(p, 16)
    bc = (blk_cols - 1) - (c % blk_cols)
    br = c // blk_cols
    dx = 3 - (i % 4)
    dy = ((i // 4) + 2) % 4
    return (y0 + 4 * br + dy) * screen_rows + (x0 + 4 * bc + dx)


# 颜色角色表（BLUE_AS_LIT=1：蓝分量并入红绿）
ROLE_BIT = {
    "RED":   [0, 1, 0, 1, 1, 1, 1, 1],
    "GREEN": [0, 0, 1, 1, 1, 1, 1, 1],
}
LINE_ROLE = ("RED", "GREEN", "RED", "GREEN")


def decode_segment(seg: bytes, x0: int, y0: int, screen_rows: int, blk_cols: int,
                   blue_lit: int = 1) -> list[list[int]]:
    """把「单模块 128 位链段」（seg[p'] bit j = 线 j 电平）解回 16×16 颜色索引图。"""
    img = [[0] * PIX_ROW for _ in range(PIX_COL)]
    for j in range(4):
        for p in range(SEG):
            on = (seg[p] >> j) & 1
            if not on:
                continue
            o = region_offset(x0, y0 + HALF[j] * (PIX_COL // 2), screen_rows, blk_cols, p)
            yy, xx = divmod(o, screen_rows)
            bit = 0x01 if LINE_ROLE[j] == "RED" else 0x02
            img[yy - y0][xx - x0] |= bit
    return img


def ascii_img(img: list[list[int]]) -> list[str]:
    ch = {0: ".", 1: "R", 2: "G", 3: "Y"}
    return ["".join(ch[v & 3] for v in row) for row in img]


# ================================================================
#  三、图案
# ================================================================
def mod_pat(color: int) -> bytes:
    """单模块纯色图案（16×16）。"""
    return bytes([color & 7]) * (PIX_ROW * PIX_COL)


def tile(pat0: bytes, pat1: bytes, rows: int, cols: int) -> bytes:
    """把「模块 0 / 模块 1 各一张 16×16 图案」按行主序平铺成整屏 pixel_map。"""
    screen_rows = rows * PIX_ROW
    buf = bytearray(screen_rows * cols * PIX_COL)
    for m, pat in enumerate((pat0, pat1)):
        x0 = (m % rows) * PIX_ROW
        y0 = (m // rows) * PIX_COL
        for y in range(PIX_COL):
            off = (y0 + y) * screen_rows + x0
            buf[off:off + PIX_ROW] = pat[y * PIX_ROW:(y + 1) * PIX_ROW]
    return bytes(buf)


def two_block_pat(rows: int, cols: int, top: int, bottom: int) -> bytes:
    """整屏上半（逻辑 y < 16）= top 色、下半 = bottom 色（模块区分图案的另一种写法）。"""
    screen_rows = rows * PIX_ROW
    buf = bytearray(screen_rows * cols * PIX_COL)
    for y in range(cols * PIX_COL):
        c = top if y < PIX_COL else bottom
        for x in range(screen_rows):
            buf[y * screen_rows + x] = c
    return bytes(buf)


def text_boxes_pat(rows: int, cols: int, line1: int, line2: int) -> bytes:
    """模拟「两行文本」：第 1 行文本的**字形框**（y 0..15, x 0..15）填 line1、
    第 2 行（y 16..31, x 0..15）填 line2。真机字形框就是这个范围（FONT_16，w=16，
    ASCII 字形宽 8 → 每行最多 2 个字位在 x=0..15）。"""
    screen_rows = rows * PIX_ROW
    buf = bytearray(screen_rows * cols * PIX_COL)
    for y in range(cols * PIX_COL):
        c = line1 if y < PIX_COL else line2
        for x in range(PIX_ROW):
            buf[y * screen_rows + x] = c
    return bytes(buf)


# ================================================================
#  四、主流程
# ================================================================
def main() -> int:
    print("=" * 78)
    print("22-1665 第二十五轮：1×2（16×32）「两块模组都不显示」宿主级取证")
    print("=" * 78)

    print("\n== 0. 宿主编译（桩 + harness + 现行驱动；-Wall -Wextra 零告警）==")
    build(TMP / "d_1x2", SRC, {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 2})
    check("1×2（MODULE_ROWS=1 / MODULE_COLS=2）编过且零告警", True)
    build(TMP / "d_1x1", SRC, {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 1})
    check("1×1 编过且零告警（对照）", True)

    # ---- 图案集 ----
    CASE = [
        ("模块区分图案：模块0=全红 / 模块1=全绿", two_block_pat(1, 2, 1, 2)),
        ("模块区分图案：模块0=全红 / 模块1=全蓝", two_block_pat(1, 2, 1, 4)),
        ("两行文本代理：上行=红 / 下行=绿（1\\n2 的字形框）",
         text_boxes_pat(1, 2, 1, 2)),
        ("两行文本代理：上行=纯红块 / 下行=全黑（现行默认画面「只有第一行有内容」）",
         text_boxes_pat(1, 2, 1, 0)),
        ("单模块纯色对照：整屏全红（1×1 口径同图案）", mod_pat(1)),
    ]

    geom2, cases2 = run(TMP / "d_1x2", [p for _, p in CASE[:4]])
    geom1, cases1 = run(TMP / "d_1x1", [CASE[1][1], CASE[2][1], CASE[4][1]])
    print(f"  1×2 基类字段：{geom2}")
    print(f"  1×1 基类字段：{geom1}")
    check("1×2 基类字段 screen=16x32 buffer=512 scanline=256 modules=1x2",
          geom2 == dict(screen="16x32", buffer="512", scanline="256", modules="1x2"), str(geom2))
    check("1×1 基类字段 screen=16x16 buffer=256 scanline=128 modules=1x1",
          geom1 == dict(screen="16x16", buffer="256", scanline="128", modules="1x1"), str(geom1))
    check("1×2 每帧 CLK = 256 = 128 × 2 模块", all(c["K"] == 256 for c in cases2))
    check("1×2 每帧 BSRR 写序 = 256 时钟 × 2 端口 = 512 项",
          all(len(c["T"]) == 512 for c in cases2))
    check("1×1 每帧 CLK = 128（对照）", all(c["K"] == 128 for c in cases1))

    print("\n== 1. Q1：1×2 的逐字输出（模块索引 ↔ 帧内时钟块）==")
    # 用「模块0=红 / 模块1=绿」反查：块 a = state[a*128 .. a*128+128)
    s = cases2[0]["S"]
    blk0 = s[0 * SEG:(0 + 1) * SEG]
    blk1 = s[1 * SEG:(2) * SEG]
    img0 = decode_segment(blk0, 0, 0, 16, 4)
    img1 = decode_segment(blk1, 0, 0, 16, 4)
    col0 = sorted({v for row in img0 for v in row})
    col1 = sorted({v for row in img1 for v in row})
    print(f"  块 0 = state[  0..127]（最后移入 → 停在链首）解出颜色索引 {col0}（1=红 / 2=绿）")
    print(f"  块 1 = state[128..255]（最先移入 → 推到链尾）解出颜色索引 {col1}")
    check("块 0 装的是**模块 1**（全绿）的内容 → 链首/板端模块拿到的是模块索引 1",
          col0 == [2], f"{col0}")
    check("块 1 装的是**模块 0**（全红）的内容 → 链尾/远端模块拿到的是模块索引 0",
          col1 == [1], f"{col1}")

    print("\n  帧位流（模块区分图案，逐时钟；bit j = 线 j）示例（每 16 位一行，逗号=相邻时钟）")
    for lo in (128, 0):
        line = " ".join(f"{s[p]:X}" for p in range(lo + 127, lo - 1, -1))
        print(f"  {'先移入 → 链尾' if lo == 128 else '后移入 → 链首'}（state[{lo+127}..{lo}]，"
              f"扫描 p 递减即从左到右）：")
        print(f"    {line[:120]}{'…' if len(line) > 120 else ''}")

    print("\n== 2. Q2：三种接线模型下每块物理屏显示什么 ==")

    def show(title: str, imgs: list[tuple[str, list[list[int]]]]) -> None:
        print(f"\n  ── {title} ──")
        for name, img in imgs:
            print(f"   [{name}]")
            for row in ascii_img(img):
                print(f"    {row}")

    for ci, (case_name, _) in enumerate(CASE[:3]):
        s = cases2[ci]["S"]
        blkA = s[0:SEG]      # 最后移入 → 链首（板端那块）
        blkB = s[SEG:2 * SEG]  # 最先移入 → 链尾（远端那块）
        print(f"\n  ### 图案：{case_name}")
        show("模型 SEPARATE（两级独立线；板端只有口 1 的 4 根脚被驱动）",
             [("口 1 的模组（收 256 时钟、只留末 128 位 = 块 0 = 现行模块 M-1 的内容）",
               decode_segment(blkA, 0, 0, 16, 4)),
              ("口 2 的模组（4 根数据脚从未被写 → 全黑）", [[0] * PIX_ROW for _ in range(PIX_COL)])])
        show("模型 CHAIN（同线级联；现行代码 _22_1665_CHAIN_HEAD_IS_MODULE0=0）",
             [("板端（链首）模组 = 块 0 = 模块 1 的内容", decode_segment(blkA, 0, 0, 16, 4)),
              ("远端（链尾）模组 = 块 1 = 模块 0 的内容", decode_segment(blkB, 0, 0, 16, 4))])
        show("模型 CHAIN-FLIP（同线级联；-D_22_1665_CHAIN_HEAD_IS_MODULE0=1：块交换）",
             [("板端（链首）模组 = 块 0 = 模块 0 的内容", decode_segment(blkB, 0, 0, 16, 4)),
              ("远端（链尾）模组 = 块 1 = 模块 1 的内容", decode_segment(blkA, 0, 0, 16, 4))])
        if ci == 0:
            print("   （ASCII 图例：R=红 G=绿 Y=黄 .=灭；每块 16 行 × 16 列 = 一块 16×16 面板的物理像素）")

    print("\n== 3. Q3：现场观测的自洽性核对 ==")
    # 观测①：默认画面（现行源码文本 "一/n二"：FONT_16、w=16 → 只有首个字形 一 落在 y0..15、其余被截）
    #        → 逻辑内容只存在于**上半屏**（= 模块 0 区域）；模块 1 区域全黑。
    # 观测②：按 TEST 一次 → "FW:...\nMD:..." → 第 2 行（"MD"）落在 y=16..31 = 模块 1 区域。
    obs = [
        ("默认画面（内容仅在上半屏 = 模块 0 区域）",
         text_boxes_pat(1, 2, 1, 0)),
        ("TEST 一次（FW 在第 1 行 = 模块 0 区域 / MD 在第 2 行 = 模块 1 区域）",
         text_boxes_pat(1, 2, 1, 2)),
    ]
    _, oc = run(TMP / "d_1x2", [p for _, p in obs])
    for (name, _), case in zip(obs, oc):
        blkA = case["S"][0:SEG]
        head = decode_segment(blkA, 0, 0, 16, 4)   # 链首/板端（单块插口 1）看到的内容
        lit_rows = sorted({yy for yy in range(16) for v in head[yy] if v})
        top_half = any(v for yy in range(0, 8) for v in head[yy])
        print(f"  · {name}")
        print(f"      单块插口 1（模型 SEPARATE / CHAIN 的链首）→ 点亮行 {lit_rows or '（无）'}；"
              f"上半区(0..7) {'有' if top_half else '无'}内容")
    check("默认画面在链首块（模块 1 区域）无任何内容 → 单块插口 1 时**全黑** = 现场观测①",
          all(v == 0 for row in decode_segment(oc[0]["S"][0:SEG], 0, 0, 16, 4) for v in row))
    check("TEST 的 FW/MD 两行中，第 2 行（MD）落在链首块（模块 1 区域）→ "
          "单块插口 1 时**只显示 MD** = 现场观测②",
          any(v for row in decode_segment(oc[1]["S"][0:SEG], 0, 0, 16, 4)[8:] for v in row))

    print("\n== 4. 驱动改动自证：默认口径与改动前逐字节一致；flip 口径 = 块交换 ==")
    PRE = (ROOT / ".analysis/22_1665/archive/prev_round/"
           "dev_display_22_1665_round25_pre_order.c")
    if PRE.exists():
        # 4.1 默认口径（CHAIN_HEAD_IS_MODULE0=0）必须与改动前**逐字节**一致（含多模块口径）
        for tag, over in (("1x1", {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 1}),
                          ("1x2", {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 2}),
                          ("2x1", {"_22_1665_MODULE_ROWS": 2, "_22_1665_MODULE_COLS": 1}),
                          ("2x2", {"_22_1665_MODULE_ROWS": 2, "_22_1665_MODULE_COLS": 2})):
            a = TMP / f"pre_{tag}"
            b = TMP / f"new_{tag}"
            build(a, PRE, over)
            build(b, SRC, over)
            pats = [two_block_pat(over["_22_1665_MODULE_ROWS"],
                                  over["_22_1665_MODULE_COLS"], 1, 2),
                    text_boxes_pat(over["_22_1665_MODULE_ROWS"],
                                   over["_22_1665_MODULE_COLS"], 1, 0),
                    mod_pat(5)]
            _, ca = run(a, pats)
            _, cb = run(b, pats)
            check(f"{tag}：默认口径改动前/后逐字节一致"
                  "（frame_state / 扫描写序 / CLK 数 / set_row）", ca == cb)

        # 4.2 flip 口径（=1）：块交换 —— 链首那块改看**模块 0** 的内容
        flip12 = TMP / "flip_1x2"
        build(flip12, SRC, {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 2,
                            "_22_1665_CHAIN_HEAD_IS_MODULE0": 1})
        _, cf = run(flip12, [CASE[0][1]])
        s2 = cf[0]["S"]
        col0 = sorted({v for row in decode_segment(s2[0:SEG], 0, 0, 16, 4) for v in row})
        col1 = sorted({v for row in decode_segment(s2[SEG:2 * SEG], 0, 0, 16, 4) for v in row})
        check("flip 口径（=1）：块 0 装模块 0（全红）、块 1 装模块 1（全绿）——"
              "链首那块改看模块 0 内容", col0 == [1] and col1 == [2], f"{col0} / {col1}")

        # 4.3 flip 口径对 M = 1 零影响
        a1 = TMP / "new_1x1"
        f1 = TMP / "flip_1x1"
        build(f1, SRC, {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 1,
                        "_22_1665_CHAIN_HEAD_IS_MODULE0": 1})
        _, ca1 = run(a1, [CASE[4][1]])
        _, cf1 = run(f1, [CASE[4][1]])
        check("flip 口径在 M = 1 时与默认口径逐字节相同（1×1 零影响）", ca1 == cf1)
    else:
        check(f"改动前基线快照存在（{PRE.name}）", False)

    print("\n" + "=" * 78)
    if FAIL:
        print(f"结果：通过 {PASS} / 失败 {FAIL}")
    else:
        print(f"结果：通过 {PASS} / 失败 {FAIL}（全部断言通过）")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
