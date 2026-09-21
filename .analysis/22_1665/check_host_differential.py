#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 **机器级**差分对拍：把驱动真的编到 x86-64 上跑，逐字节比对输出。

为什么需要它：`check_equivalence.py` 是**语义级**对照（Python 复写两边模型）。
本脚本直接运行**两份编译产物**给出机器级证据：

  A. 1×1 零回归：新驱动（默认 = 每列一口独立数据线，与模型 A 在 M=1 等价）vs 旧驱动（第二十四轮形态）
     逐字节一致（frame_state / 每帧 BSRR 写序 / CLK 数 / set_row 电平，全部用例）
  B. 全部口径（1×1 / 2×1 / 1×2 / 2×2 / 链首 flip 变体，每列一口独立数据线）：
     编译产物 vs **独立复写的规格模型**（区域公式 + 时钟/归属 + 颜色表 + 极性）
     逐字节一致；并交叉验证「组内每模块的 128 位切片 = 该模块图案的 1×1 编译产物输出」
     （第二十九轮：模型 A 变体与 BLUE_AS_LIT / DATA_ACTIVE_HIGH 变体随开关删除一并退休）
  C. 基类字段自证：screen / buffer / scanline / modules 与宏口径一致
  D. 帧长：CLK 数 = 128 × MODULE_ROWS（各组并行）；
     BSRR 写序 = 逐时钟按组、按端口（组 0 = G+B；组 1 = B+E）

做法：`.analysis/22_1665/host/` 下的桩头文件（`dev_display.h` / `pl_hub75.h` / `initcall.h`）
把 HAL 换成记录设施，harness 从 stdin 读每用例的 pixel_map、打印 prepare/scan/set_row 结果。

用法：python3 .analysis/22_1665/check_host_differential.py [--cases N]
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import macro_override  # noqa: E402  （同目录：几何宏源码替换工具）

HOST = ROOT / ".analysis/22_1665/host"
STUB = HOST / "stub"
HARNESS = HOST / "host_driver.c"
OLD_SRC = (ROOT / ".analysis/22_1665/archive/prev_round/"
           "dev_display_22_1665_round24_pre_resolution.c")
NEW_SRC = ROOT / "Device/Display/dev_display_22_1665.c"
MAIN_H = ROOT / "Core/Inc/main.h"

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


# ================================================================
#  引脚（真板 main.h 口径）：组 g 的 4 行 = 通道对 2g / 2g+1 的 R/G
#  组 0 = R1/G1/R2/G2 = PG9/PG10/PG15/PB6；组 1 = R3/G3/R4/G4 = PB8/PB9/PE1/PE2
# ================================================================
_MAIN_H = MAIN_H.read_text(encoding="utf-8")


def main_h_pin(base: str) -> tuple[str, int]:
    m = re.search(rf"^#define\s+{base}_Pin\s+GPIO_PIN_(\d+)", _MAIN_H, re.M)
    p = re.search(rf"^#define\s+{base}_GPIO_Port\s+GPIO(\w)", _MAIN_H, re.M)
    if not m or not p:
        raise SystemExit(f"main.h 缺少 {base}")
    return f"P{p.group(1)}", 1 << int(m.group(1))


def group_lines(g: int) -> list[tuple[str, int]]:
    return [main_h_pin(f"HUB75_R{2 * g + 1}"), main_h_pin(f"HUB75_G{2 * g + 1}"),
            main_h_pin(f"HUB75_R{2 * g + 2}"), main_h_pin(f"HUB75_G{2 * g + 2}")]


# 与 harness（host/host_driver.c）的引脚表核对：两份必须一致，否则差分无意义
_HARNESS = HARNESS.read_text(encoding="utf-8")


def harness_pins() -> list[tuple[str, int]]:
    """从 harness 的 g_hub75_pin_r/g 前 4 项取 (端口, 掩码)。"""
    out: list[tuple[str, int]] = []
    for arr, n in (("g_hub75_pin_r", 2), ("g_hub75_pin_g", 2)):
        body = re.search(rf"const hub75_pin_t {arr}\[[^\]]*\]\s*=\s*\{{(.*?)\n\}};",
                         _HARNESS, re.S)
        if not body:
            raise SystemExit(f"harness 缺少 {arr}")
        pairs = re.findall(r"\{&s_p([gbe]),\s*(0x[0-9a-fA-F]+)U\}", body.group(1))
        out += [(f"P{p.upper()}", int(v, 16)) for p, v in pairs[:n]]
    # 顺序 = 通道 0..3 的 R/G 混合：r0,g0,r1,g1 → 重排成「组 0 的 4 行、组 1 的 4 行」
    # harness 数组顺序 = 通道号顺序，故取 r[0],g[0],r[1],g[1] 即组 0；r[2],g[2],r[3],g[3] 即组 1
    return out


_r = re.search(r"const hub75_pin_t g_hub75_pin_r\[[^\]]*\]\s*=\s*\{(.*?)\n\};", _HARNESS, re.S)
_g = re.search(r"const hub75_pin_t g_hub75_pin_g\[[^\]]*\]\s*=\s*\{(.*?)\n\};", _HARNESS, re.S)
if not _r or not _g:
    raise SystemExit("harness 引脚表解析失败")
R_PINS = [(f"P{p.upper()}", int(v, 16)) for p, v in
          re.findall(r"\{&s_p([gbe]),\s*(0x[0-9a-fA-F]+)U\}", _r.group(1))]
G_PINS = [(f"P{p.upper()}", int(v, 16)) for p, v in
          re.findall(r"\{&s_p([gbe]),\s*(0x[0-9a-fA-F]+)U\}", _g.group(1))]


def harness_group_lines(g: int) -> list[tuple[str, int]]:
    return [R_PINS[2 * g], G_PINS[2 * g], R_PINS[2 * g + 1], G_PINS[2 * g + 1]]


# ================================================================
#  规格模型（**独立复写**：只按 driver 文件头 / doc/01 §0 的算式，不看实现）
#  · 单模块落点：链位 p → 片 c=p/16、级序 i=p%16；块栅格 blk_cols=w/4；
#    bc=(blk_cols-1)-(c%blk_cols)、br=c/blk_cols；dx=3-(i%4)、dy=((i/4)+2)%4
#  · 接线（唯一形态）= 每列一口独立线：组 g（= 模块列 g）占通道对 2g/2g+1，
#    各组并行、每帧 128×MODULE_ROWS 时钟；组内模块（横向）同线级联，块 0 收末 128 位
#  · 状态字节 bit j = 组内第 j 条数据线电平（线序 R/G × 上半/下半 → 半屏 0/0/1/1、角色 R/G/R/G）
#  · 颜色表（已固化）：蓝分量并入红绿（双色屏无蓝 die，两 die 都亮）
# ================================================================
LINE_ROLE = ["RED", "GREEN", "RED", "GREEN"]
LINE_HALF = [0, 0, 1, 1]

ROLE_TAB_LIT = {          # 第二十九轮固化：蓝分量并入红绿（唯一实现）
    "RED":   [0, 1, 0, 1, 1, 1, 1, 1],
    "GREEN": [0, 0, 1, 1, 1, 1, 1, 1],
}


def role_on(role: str, color: int) -> bool:
    return bool(ROLE_TAB_LIT[role][color & 0x07])


def region_offset(x0: int, y0: int, screen_rows: int, blk_cols: int, p: int) -> int:
    c, i = divmod(p, 16)
    bc = (blk_cols - 1) - (c % blk_cols)
    br = c // blk_cols
    dx = 3 - (i % 4)
    dy = ((i // 4) + 2) % 4
    return (y0 + 4 * br + dy) * screen_rows + (x0 + 4 * bc + dx)


def expected_state(pixel_map: bytes, rows: int, cols: int, pix_row: int, pix_col: int,
                   flip: int = 0) -> bytes:
    """规格模型：每列一口独立数据线（唯一形态）。`flip` = 链首口径（CHAIN_HEAD_IS_MODULE0）。"""
    modules = rows * cols
    seg = 128
    screen_rows = rows * pix_row
    half = pix_col // 2
    blk_cols = pix_row // 4
    groups = cols
    group_modules = rows
    clocks = seg * group_modules
    out = bytearray(seg * modules)
    for g in range(groups):
        base_g = g * clocks
        for local in range(group_modules):
            m = g * rows + local
            block = local if flip else group_modules - 1 - local
            x0 = (m % rows) * pix_row
            y0 = (m // rows) * pix_col
            for j in range(4):
                yj = y0 + LINE_HALF[j] * half
                for p in range(seg):
                    o = region_offset(x0, yj, screen_rows, blk_cols, p)
                    if role_on(LINE_ROLE[j], pixel_map[o]):
                        out[base_g + block * seg + p] |= 1 << j
    return bytes(out)


# 真板端口名（main.h：PG/PB/PE）→ harness 记录名（G/B/E）
PORT_SHORT = {"PG": "G", "PB": "B", "PE": "E"}


def expected_bsrr(state_byte: int, lines: list[tuple[str, int]]) -> list[tuple[str, int]]:
    """组状态字节 → 该组该时钟的 (端口, BSRR 字) 写序（按端口首次出现顺序，端口名同 harness）。
    极性已固化：状态位 1 → 数据脚置位、0 → 复位。"""
    lines = [(PORT_SHORT[p], pin) for p, pin in lines]
    ports: list[str] = []
    for port, _pin in lines:
        if port not in ports:
            ports.append(port)
    vals = {p: 0 for p in ports}
    for j, (port, pin) in enumerate(lines):
        on = bool((state_byte >> j) & 1)
        vals[port] |= pin if on else (pin << 16)
    return [(p, vals[p]) for p in ports]


# ================================================================
#  图案（单模块像素图；多模块 = 逐模块平铺，每模块取不同的图案）
# ================================================================
def module_patterns(count: int, pix_row: int, pix_col: int) -> list[bytes]:
    out = []
    for k in range(count):
        buf = bytearray(pix_row * pix_col)
        for y in range(pix_col):
            for x in range(pix_row):
                o = y * pix_row + x
                kind = k % 8
                if kind == 0:      # 红「口」框
                    buf[o] = 1 if (2 <= x <= pix_row - 3 and 2 <= y <= pix_col - 3
                                   and (x in (2, pix_row - 3) or y in (2, pix_col - 3))) else 0
                elif kind == 1:    # 绿斜纹
                    buf[o] = 2 if (x + y) % 3 == 0 else 0
                elif kind == 2:    # 全白（四线全亮 + 蓝分量）
                    buf[o] = 7
                elif kind == 3:    # 单点红
                    buf[o] = 1 if (x, y) == (pix_row // 2, pix_col // 2) else 0
                elif kind == 4:    # 上半屏满红
                    buf[o] = 1 if y < pix_col // 2 else 0
                elif kind == 5:    # 下半屏满绿
                    buf[o] = 2 if y >= pix_col // 2 else 0
                elif kind == 6:    # 八色棋盘（含蓝/紫/青，专门打颜色表）
                    buf[o] = (x + y) % 8
                else:              # 伪随机八色
                    buf[o] = (x * 7 + y * 13 + (x * y) % 5) % 8
        out.append(bytes(buf))
    return out


def tile(pats: list[bytes], rows: int, cols: int, pix_row: int, pix_col: int) -> bytes:
    """把「每模块一个图案」的清单平铺成整屏 pixel_map（行主序模块序）。"""
    modules = rows * cols
    screen_rows = rows * pix_row
    buf = bytearray(screen_rows * cols * pix_col)
    for m in range(modules):
        pat = pats[m % len(pats)]
        x0 = (m % rows) * pix_row
        y0 = (m // rows) * pix_col
        for y in range(pix_col):
            row_off = (y0 + y) * screen_rows + x0
            buf[row_off:row_off + pix_row] = pat[y * pix_row:(y + 1) * pix_row]
    return bytes(buf)


# ================================================================
#  编译 / 运行
# ================================================================
def read_macro(src: pathlib.Path, name: str, over: dict[str, int], default: int | None = None) -> int:
    if name in over:
        return over[name]
    text = src.read_text(encoding="utf-8")
    m = re.search(rf"^#define\s+{re.escape(name)}\s+\(?(\d+)U?", text, re.M)
    if not m:
        if default is not None:
            return default
        raise SystemExit(f"宏 {name} 未找到")
    return int(m.group(1))


def read_geom(src: pathlib.Path, over: dict[str, int]) -> tuple[int, int, int, int]:
    return (read_macro(src, "_22_1665_MODULE_ROWS", over),
            read_macro(src, "_22_1665_MODULE_COLS", over),
            read_macro(src, "_22_1665_MODULE_PIXEL_ROW", over),
            read_macro(src, "_22_1665_MODULE_PIXEL_COL", over))


def build(out: pathlib.Path, src: pathlib.Path, over: dict[str, int]) -> None:
    # 第三十轮：四个几何宏是**普通 `#define`**（兄弟驱动同款）⇒ 覆盖走源码替换
    # （`macro_override.materialize`：`-D` 只会得到「重定义告警 + 文件值胜出」）；
    # 其余宏（如 `_22_1665_CHAIN_HEAD_IS_MODULE0`，仍带 `#ifndef` 守卫）照旧用 `-D`。
    src_file, defines = macro_override.materialize(src, over, out.name)
    cmd = ["gcc", "-std=gnu23", "-O2", "-Wall", "-Wextra", "-funsigned-char",
           "-I", str(STUB), "-o", str(out), str(HARNESS), str(src_file)]
    cmd += [f"-D{k}={v}" for k, v in defines.items()]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"宿主编译失败：{src.name}\n{r.stderr[:2000]}")
    if r.stderr.strip():
        raise SystemExit(f"宿主编译有告警（须零告警）：{src.name}\n{r.stderr[:2000]}")


def run(binary: pathlib.Path, patterns: list[bytes]) -> list[dict]:
    data = b"".join(patterns)
    r = subprocess.run([str(binary)], input=data, capture_output=True)
    if r.returncode != 0:
        raise SystemExit(f"宿主运行失败：{binary}\n{r.stderr[:1000].decode(errors='replace')}")
    cases: list[dict] = []
    cur = None
    for line in r.stdout.decode().splitlines():
        if line.startswith("GEOM "):
            cases.append({"GEOM": dict(kv.split("=") for kv in line[5:].split())})
        elif line.startswith("CASE "):
            cur = {}
            cases.append(cur)
        elif line.startswith("S "):
            cur["S"] = line[2:]
        elif line.startswith("T "):
            f = line[2:].split()
            cur["T"] = [(x[0], int(x[1:], 16)) for x in f[1:]] if len(f) > 1 else []
        elif line.startswith("K "):
            cur["K"] = int(line[2:])
        elif line.startswith("R "):
            cur["R"] = line[2:]
    return cases


def geom_of(cases: list[dict]) -> dict:
    return next(c["GEOM"] for c in cases if "GEOM" in c)


def body(cases: list[dict]) -> list[dict]:
    return [c for c in cases if "S" in c]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", type=int, default=24, help="模块图案数（8 结构化 + 其余随机）")
    args = ap.parse_args()

    tmp = pathlib.Path("/tmp/22_1665_host")
    tmp.mkdir(exist_ok=True)

    print("== -1. 引脚口径：真板 main.h vs 宿主 harness（组 0 / 组 1）==")
    for g in range(2):
        check(f"组 {g} 四根脚一致（main.h = harness）：{group_lines(g)}",
              group_lines(g) == harness_group_lines(g),
              f"{group_lines(g)} vs {harness_group_lines(g)}")

    # 口径：几何 + 链首口径（第二十九轮起接线形态唯一 = 每列一口独立数据线，
    # 模型 A / BLUE_AS_LIT / DATA_ACTIVE_HIGH 变体已随开关删除一并退休）
    # **两个模块数轴都显式钉死**（第三十轮起 = 源码替换，见 build()）：源码里的宏值是用户
    # 在用的旋钮，基线/回归用例不得随它漂移（且宿主侧 `pl_hub75_bsrr_t` 在 x86-64 上比真板大
    # ⇒ 只钉一个轴时 CCM 预算会失真，例：源码 COLS=5 + 只钉 ROWS=2 ⇒ 宿主模型 10 模块触发
    # CCM 断言）
    configs: dict[str, dict[str, int]] = {
        "1x1": {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 1},
        "2x1": {"_22_1665_MODULE_ROWS": 2, "_22_1665_MODULE_COLS": 1},
        "1x2": {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 2},
        "2x2": {"_22_1665_MODULE_ROWS": 2, "_22_1665_MODULE_COLS": 2},
        "1x2_flip": {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 2,
                     "_22_1665_CHAIN_HEAD_IS_MODULE0": 1},
    }
    print("== 0. 宿主编译（桩头文件 + harness + 驱动源码，-Wall -Wextra 零告警）==")
    build(tmp / "old_1x1", OLD_SRC, {})
    check("旧驱动（改动前现场调通版）1×1 编过且零告警", True)
    for name, over in configs.items():
        build(tmp / f"new_{name}", NEW_SRC, over)
        check(f"新驱动 {name} 编过且零告警", True)

    # 图案：8 结构化 + 随机（固定种子，可复现）
    pix_row, pix_col = read_geom(NEW_SRC, configs["1x1"])[2:]
    n_rand = max(0, args.cases - 8)
    seed = 0x1665_CAFE
    rnd = []
    for _ in range(n_rand):
        buf = bytearray(pix_row * pix_col)
        for i in range(len(buf)):
            seed = (seed * 1103515245 + 12345) & 0xFFFFFFFF
            buf[i] = (seed >> 16) & 0x07
        rnd.append(bytes(buf))
    module_pats = module_patterns(8, pix_row, pix_col) + rnd
    print(f"  图案：8 结构化 + {n_rand} 伪随机（种子固定，可复现）= {len(module_pats)} 个单模块图案")

    # ---- A. 1×1 零回归：新 vs 旧（机器级逐字节）----
    print("== A. 1×1 零回归：新驱动 vs 旧驱动（同一 harness、同一 stdin）==")
    old_cases = body(run(tmp / "old_1x1", module_pats))
    single_cases = body(run(tmp / "new_1x1", module_pats))  # 1×1 = 显式钉死口径（源码替换）
    check("两二进制用例数一致", len(old_cases) == len(single_cases) == len(module_pats),
          f"{len(old_cases)} vs {len(single_cases)}")
    diff = [i for i, (a, b) in enumerate(zip(old_cases, single_cases)) if a != b]
    check(f"全部 {len(module_pats)} 用例逐字节一致（frame_state / BSRR 写序 / CLK 数 / set_row）",
          not diff, f"差异用例 {diff[:6]}")
    check("1×1 基类字段：screen=16x16 buffer=256 scanline=128 modules=1x1",
          geom_of(run(tmp / "new_1x1", [module_pats[0]]))
          == dict(screen="16x16", buffer="256", scanline="128", modules="1x1"))
    check("1×1 每帧 CLK 数 = 128、set_row 电平 = 0000、写序 = 逐时钟 2 端口",
          all(r["K"] == 128 and r["R"] == "0000" and len(r["T"]) == 2 * 128 for r in single_cases))

    # ---- B/C/D. 各口径：编译产物 vs 独立规格模型 ----
    for name, over in configs.items():
        rows, cols, pix_row, pix_col = read_geom(NEW_SRC, over)
        flip = read_macro(NEW_SRC, "_22_1665_CHAIN_HEAD_IS_MODULE0", over, 0)
        modules = rows * cols
        seg = 128
        groups = cols
        clocks = seg * rows
        print(f"== {name}（{rows}×{cols} 模块 → {rows*pix_row}×{cols*pix_col}，每列一口独立线"
              f" / {modules} 模块 / {clocks} 时钟 / {4*modules} 链段"
              f"{'' if not flip else ' / CHAIN_HEAD_IS_MODULE0=1'}）==")

        # 每用例：模块 mm 用 module_pats[(c + 7·mm) % K]（7 与 2/4 互质 → 各模块图案互不相同）
        pats, idx_sets = [], []
        for c in range(len(module_pats)):
            idxs = [(c + 7 * mm) % len(module_pats) for mm in range(modules)]
            idx_sets.append(idxs)
            pats.append(tile([module_pats[i] for i in idxs], rows, cols, pix_row, pix_col))

        cases = body(run(tmp / f"new_{name}", pats))
        gm = geom_of(run(tmp / f"new_{name}", pats[:1]))
        check(f"{name} 基类字段自证：screen={rows*pix_row}x{cols*pix_col} "
              f"buffer={rows*pix_row*cols*pix_col} scanline={seg*modules} modules={rows}x{cols}",
              gm == dict(screen=f"{rows*pix_row}x{cols*pix_col}",
                         buffer=str(rows * pix_row * cols * pix_col),
                         scanline=str(seg * modules), modules=f"{rows}x{cols}"), str(gm))

        bad_state, bad_trace, bad_clocks, bad_row, bad_cross = [], [], [], [], []
        for i, (pat, idxs, case) in enumerate(zip(pats, idx_sets, cases)):
            exp = expected_state(pat, rows, cols, pix_row, pix_col, flip)
            if case["S"] != exp.hex():
                bad_state.append(i)
            if case["K"] != clocks:
                bad_clocks.append(i)
            if case["R"] != "0000":
                bad_row.append(i)
            exp_trace = [e
                         for p in range(clocks - 1, -1, -1)
                         for g in range(groups)
                         for e in expected_bsrr(exp[g * clocks + p], group_lines(g))]
            if case["T"] != exp_trace:
                bad_trace.append(i)
            # 交叉验证：组内每模块的 128 位切片 = 该模块图案的 1×1 编译产物
            # （块 0 = 末 128 位 = 链首）
            slices, want = [], []
            for g in range(groups):
                for b in range(rows):
                    slices.append(case["S"][(g * clocks + b * seg) * 2:
                                            (g * clocks + (b + 1) * seg) * 2])
                    local = b if flip else rows - 1 - b
                    want.append(single_cases[idxs[g * rows + local]]["S"])
            if slices != want:
                bad_cross.append(i)
        check(f"{name} frame_state[{seg*modules}] 与规格模型逐字节一致（{len(pats)} 用例）",
              not bad_state, f"差异用例 {bad_state[:6]}")
        check(f"{name} BSRR 写序（{len(cases[0]['T'])} 项/帧 = 时钟 × 组 × 端口）与规格模型逐字节一致",
              not bad_trace, f"差异用例 {bad_trace[:6]}")
        check(f"{name} 每帧 CLK 数 = {clocks}（= 128 × MODULE_ROWS，各组并行）", not bad_clocks)
        check(f"{name} set_row(0) 电平 = 0000", not bad_row)
        check(f"{name} 链切片自洽（组内每模块切片 = 该模块图案的 1×1 编译产物，块 0 = 链首）",
              not bad_cross, f"差异用例 {bad_cross[:6]}")

    print()
    if FAILURES:
        print("失败项：")
        for f in FAILURES:
            print(f"  - {f}")
    print(f"结果：通过 {PASS} / 失败 {FAIL}")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
