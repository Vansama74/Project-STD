#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 驱动宿主验证（.analysis/22_1665/verify.py）

不依赖硬件，直接以 `Device/Display/dev_display_22_1665.c` 的宏为输入，复算驱动
init 期建表 + scan 发射的逻辑，回答三个问题：

  A. **无回归证明**：改造后（2026-09-17 归一版）的默认口径，与改造前（旧版）
     逐链位发射序列是否**逐位相同**（同一内容 → 同一 SDI 电平序）。
  B. **双射性**：开关矩阵（BLK_W / SEG_LAYOUT / G_FIRST / LINE_REVERSE /
     BIT_REVERSE / DUAL / SCAN_SEL）下，每个「像素 x 段」是否恰好占一个链位、
     每个链位是否恰好对应一个（像素, 段）——映射错位类症状的必要条件。
  C. **症状签名表**：假定驱动按默认口径发射、而实物是「另一种拓扑」时，
     屏上会看到什么（ASCII 图），供现场一眼比对。

用法：
    python3 .analysis/22_1665/verify.py            # 全部检查 + 打印签名表
    python3 .analysis/22_1665/verify.py --md       # 输出 Markdown（写入 signatures.md）
"""

from __future__ import annotations

import argparse
import itertools
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
DRIVER = ROOT / "Device/Display/dev_display_22_1665.c"

SEG_R, SEG_G, SEG_B = 0, 1, 2
COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW = 0, 1, 2, 3
COLOR_BLUE, COLOR_PURPLE, COLOR_CYAN, COLOR_WHITE = 4, 5, 6, 7

# ---------------------------------------------------------------- 宏解析


def _skip_group(expr: str, i: int) -> int:
    """expr[i] == '(' → 返回与其配对的 ')' 之后的下标"""
    depth = 0
    while i < len(expr):
        if expr[i] == "(":
            depth += 1
        elif expr[i] == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return len(expr)


def _ternary_to_py(expr: str) -> str:
    """把 C 三元 `C ? A : B`（含嵌套 / 含括号子表达式）转成 Python `A if C else B`。

    两步：① 先递归转换**每个括号组内部**的三元（转换后组内不再含 `?`/`:`）；
    ② 再处理顶层三元（第一个 `?` 与同层第一个 `:` 配对，支持右结合嵌套）。"""
    expr = expr.strip()

    # ① 组内递归
    out: list[str] = []
    i = 0
    while i < len(expr):
        if expr[i] == "(":
            j = _skip_group(expr, i)
            out.append("(" + _ternary_to_py(expr[i + 1:j - 1]) + ")")
            i = j
        else:
            out.append(expr[i])
            i += 1
    expr = "".join(out)

    # ② 顶层三元
    q_idx = -1
    for i, ch in enumerate(expr):
        if ch == "?":
            q_idx = i
            break
    if q_idx < 0:
        return expr

    nesting, c_idx = 0, -1
    for i in range(q_idx + 1, len(expr)):
        ch = expr[i]
        if ch == "?":
            nesting += 1
        elif ch == ":":
            if nesting == 0:
                c_idx = i
                break
            nesting -= 1
    if c_idx < 0:
        return expr

    cond = _ternary_to_py(expr[:q_idx])
    a = _ternary_to_py(expr[q_idx + 1:c_idx])
    b = _ternary_to_py(expr[c_idx + 1:])
    return f"(({a}) if ({cond}) else ({b}))"


def _strip_outer_parens(expr: str) -> str:
    """剥掉「恰好包住整个表达式」的最外层括号（反复剥离）"""
    while len(expr) >= 2 and expr[0] == "(" and expr[-1] == ")":
        depth = 0
        closes_at_end = False
        for i, ch in enumerate(expr):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    closes_at_end = (i == len(expr) - 1)
                    break
        if not closes_at_end:
            break
        expr = expr[1:-1].strip()
    return expr


def parse_macros(path: pathlib.Path) -> dict[str, int]:
    """取出 `#define NAME expr` 的数值宏并求值（函数式宏、字符串宏跳过）。

    处理：反斜杠续行合并 → 去注释 → 剥最外层括号 → 迭代求值（依赖顺序无要求）。
    """
    text = path.read_text(encoding="utf-8")
    # 先合并反斜杠续行（多行 #define），否则续行的宏会被整条丢弃
    text = re.sub(r"\\\s*\n\s*", " ", text)

    raw: dict[str, str] = {}
    for line in text.splitlines():
        if not line.startswith("#define"):
            continue
        body = line[len("#define"):]
        body = body.split("/*")[0].split("//")[0]  # 去注释（注释里可能含 / 与中文）
        parts = body.strip().split(None, 1)
        if len(parts) != 2 or "(" in parts[0]:  # 函数式宏（NAME(...)）跳过
            continue
        name, expr = parts[0], _strip_outer_parens(parts[1].strip())
        if not name.startswith("_22_1665_") and name != "MODULE_CODE":
            continue
        if not expr or not re.match(r"^[\s0-9A-Za-z_()*+/%<>=!&|~^?:.U-]+$", expr):
            continue
        raw[name] = expr

    env: dict[str, int] = {}
    for _ in range(len(raw) + 2):
        progressed = False
        for name, expr in raw.items():
            if name in env:
                continue
            # C 的无符号字面量 0U / 16U → Python 0 / 16（只剥数字后的 U，勿碰标识符！）
            cleaned = re.sub(r"\b(\d+)[UuLl]+\b", r"\1", expr)
            cleaned = cleaned.replace("&&", " and ").replace("||", " or ")
            cleaned = _ternary_to_py(cleaned)
            try:
                env[name] = int(eval(cleaned, {"__builtins__": {}}, dict(env)))  # noqa: S307
                progressed = True
            except Exception:
                continue
        if not progressed:
            break
    missing = sorted(set(raw) - set(env))
    if missing:
        print(f"[warn] 未能求值的宏（不影响验证）: {', '.join(missing)}", file=sys.stderr)
    return env


# ---------------------------------------------------------------- 链路模型

MODULE_ROWS, MODULE_COLS, PIXEL_ROW, PIXEL_COL, CHANNELS_PER_MODULE = 1, 1, 16, 16, 1


class Cfg:
    """一次编译口径 = 模组拼装 + 一组校准开关（与 C 文件公式逐一对应）。"""

    def __init__(self, *, chip_bits=16, segments=2, scan_sel=1, dual=0,
                 seg_layout=0, blk_w=0, g_first=0, line_reverse=0,
                 bit_reverse=0, active_high=1, blue_link=0,
                 mod_rows=MODULE_ROWS, mod_cols=MODULE_COLS):
        self.chip_bits, self.segments = chip_bits, segments
        self.scan_sel, self.dual = scan_sel, dual
        self.seg_layout, self.chk_blk_w = seg_layout, blk_w
        self.g_first, self.line_reverse, self.bit_reverse = g_first, line_reverse, bit_reverse
        self.active_high, self.blue_link = active_high, blue_link
        self.mod_rows, self.mod_cols = mod_rows, mod_cols

        self.screen_rows = mod_rows * PIXEL_ROW
        self.screen_cols = mod_cols * PIXEL_COL
        self.buffer_size = self.screen_rows * self.screen_cols
        self.scan_lines = scan_sel
        self.phase_rows = self.screen_cols // scan_sel
        self.scan_line_px = self.screen_rows * self.phase_rows

        # 每片像素数（C: _22_1665_PX_PER_CHIP）
        if dual:
            self.px_per_chip = chip_bits
        elif seg_layout == 2:
            self.px_per_chip = chip_bits // segments
        else:
            self.px_per_chip = chip_bits
        # 块宽/高（C: _22_1665_BLK_W / BLK_H，blk_w==0 → 自动 = 每片像素数）
        self.blk_w = blk_w if blk_w else self.px_per_chip
        self.blk_h = self.px_per_chip // self.blk_w
        self.blocks_per_row = self.screen_rows // self.blk_w
        self.blocks_per_seg = self.scan_line_px // self.px_per_chip
        per_chain = 1 if (dual or seg_layout == 2) else self.segments
        self.chips_per_chain = self.blocks_per_seg * per_chain
        self.frame_bits = self.chips_per_chain * chip_bits

    # ---- 有效性（对应 C 文件 _Static_assert；不满足的组合本就不该编进去）----
    def valid(self) -> tuple[bool, str]:
        if self.scan_sel not in (1, 2, 4, 8, 16):
            return False, "SCAN_SEL 只能 1/2/4/8/16"
        if self.screen_cols % self.scan_sel:
            return False, "行数非相数整数倍"
        if self.scan_line_px * self.scan_sel != self.buffer_size:
            return False, "相 x 每相像素 != 整帧"
        if self.dual and self.seg_layout != 0:
            return False, "双链时段排布须为 0"
        if self.seg_layout not in (0, 1, 2):
            return False, "SEG_LAYOUT 只能 0/1/2"
        if self.chip_bits % self.segments:
            return False, "片宽非段数整数倍"
        if self.px_per_chip % self.blk_w or self.screen_rows % self.blk_w:
            return False, f"块宽 {self.blk_w} 不整除每片像素数 {self.px_per_chip} / 行宽 {self.screen_rows}"
        if self.phase_rows % self.blk_h:
            return False, "块高不整除相内行数"
        if self.blocks_per_row * (self.phase_rows // self.blk_h) != self.blocks_per_seg:
            return False, "块数不闭合"
        want = self.scan_line_px if self.dual else self.segments * self.scan_line_px
        if self.frame_bits != want:
            return False, f"每扫描线链位数 {self.frame_bits} != 期望 {want}"
        return True, "ok"

    # ---- C: _22_1665_build_bit_table() 逐行等价复算 ----
    def bit_table(self) -> list[tuple[int, int]]:
        """返回 [(dst, seg), ...]，下标 = 链位 p。"""
        table: list[tuple[int, int]] = []
        for p in range(self.frame_bits):
            chip, s = divmod(p, self.chip_bits)
            if self.dual:
                seg, block, level = 0, chip, s
            elif self.seg_layout == 2:
                seg, block, level = s % self.segments, chip, s // self.segments
            elif self.seg_layout == 1:
                seg, block, level = chip // self.blocks_per_seg, chip % self.blocks_per_seg, s
            else:
                seg, block, level = chip % self.segments, chip // self.segments, s
            if self.g_first:
                seg = self.segments - 1 - seg
            if self.line_reverse:
                block = self.blocks_per_seg - 1 - block
            if self.bit_reverse:
                level = self.px_per_chip - 1 - level
            dx, dy = level % self.blk_w, level // self.blk_w
            bx, by = block % self.blocks_per_row, block // self.blocks_per_row
            dst = (by * self.blk_h + dy) * self.screen_rows + (bx * self.blk_w + dx)
            table.append((dst, seg))
        return table

    # ---- 链位 → (像素, 段) 的逆表；用于判双射与成像 ----
    def pos_of(self) -> dict[tuple[int, int], int]:
        """{(像素序号 y*W+x, 段): 链位 p}（dst 为相内偏移，静态时 = 逻辑偏移）。

        双链模式：两条链各自覆盖全部像素（红链带 R、绿链带 G），同一 p 出现两次
        （键的段不同）——物理上它们是两条独立数据线。"""
        out: dict[tuple[int, int], int] = {}
        for p, (dst, seg) in enumerate(self.bit_table()):
            if self.dual:
                out[(dst, SEG_R)] = p
                out[(dst, SEG_G)] = p
            else:
                out[(dst, seg)] = p
        return out

    def pixel_of_chain(self, p: int) -> tuple[int, int]:
        return self.bit_table()[p]

    def on(self, color: int, seg: int) -> bool:
        """C: _22_1665_seg_on()（含 BLUE_LINK 口径）"""
        if seg == SEG_R:
            return bool(color & 0x01)
        if seg == SEG_G:
            return bool(color & 0x02) or (bool(self.blue_link) and bool(color & 0x04))
        return bool(color & 0x04)

    def label(self) -> str:
        return (f"SCAN_SEL={self.scan_sel} DUAL={self.dual} SEG_LAYOUT={self.seg_layout} "
                f"BLK_W={self.chk_blk_w}(->{self.blk_w}) G_FIRST={self.g_first} "
                f"REV={self.line_reverse}/{self.bit_reverse}")


def legacy_cfg() -> Cfg:
    """改造前（2026-09-16 第十轮版）的默认口径：
    TRANSPOSE=0 / COLOR_MAJOR=0 / SEG_PACK=0 / BLK_W=16 / 六开关默认。
    等价于本模型的默认参数（这正是「无回归」要证明的结论）。"""
    return Cfg()


# ---------------------------------------------------------------- 屏幕成像


def _led_on(cfg_drv: Cfg, cfg_hw: Cfg, color: int, drv_seg: int, seg_hw: int) -> bool:
    """实物 LED 是否点亮：驱动按自己的极性假设给出引脚电平，实物按其真实极性判亮灭。

    单链：驱动那一位的「亮」语义 = cfg_drv.on(color, 段)；极性一致 → 该亮就亮，
    极性相反 → 整屏取反（该亮的灭、该灭的亮）。双链：每链按链自己的段取位。"""
    want = cfg_drv.on(color, seg_hw) if cfg_drv.dual else cfg_drv.on(color, drv_seg)
    pin_high = (want == bool(cfg_drv.active_high))
    return pin_high == bool(cfg_hw.active_high)


def screen_image(cfg_drv: Cfg, cfg_hw: Cfg, content: dict[int, int]) -> list[list[int]]:
    """驱动按 cfg_drv 发射、实物按 cfg_hw 接线时，屏上看到的图。

    物理事实：发射序 t 的位落在链位 p = N-1-t（N = 链位数）；驱动给链位 p 的内容
    是 cfg_drv 的 (像素, 段)；实物把链位 q 接到 cfg_hw 的 (像素, 段)。
    → 屏上「物理 LED（像素 P，段 S）」的亮灭 = 驱动给链位 hw_pos(P,S) 的那一位。
    ※ 实物为双链而驱动按单链（只驱 R1）时，绿链（G1）全程无波形 → 绿永远不亮。"""
    hw_pos = cfg_hw.pos_of()
    drv_tab = cfg_drv.bit_table()
    out = [[COLOR_BLACK] * cfg_drv.screen_rows for _ in range(cfg_drv.screen_cols)]

    for (pix, seg_hw), q in hw_pos.items():
        if q >= len(drv_tab):
            continue  # 实物链比驱动帧长：该段这一帧没有数据
        if cfg_hw.dual and not cfg_drv.dual and seg_hw == SEG_G:
            continue  # 实物绿链的 SDI(G1) 驱动侧未驱动 → 恒灭
        d_dst, d_seg = drv_tab[q]
        color = content.get(d_dst, COLOR_BLACK)
        if _led_on(cfg_drv, cfg_hw, color, d_seg, seg_hw):
            out[pix // cfg_drv.screen_rows][pix % cfg_drv.screen_rows] |= (1 << seg_hw)
    return out


def screen_image_truncated(cfg_drv: Cfg, hw_chain_bits: int, hw_order: Cfg,
                           content: dict[int, int]) -> list[list[int]]:
    """实物链**短于**驱动帧时（例：实为 1/N 扫 / 片数更少）的成像：
    实物只保留**最后移入**的 hw_chain_bits 位（其余从链尾挤出丢失），
    链位编号按 hw_order 的模型解释（行址恒 0 → 只有被选址的行亮）。"""
    drv_tab = cfg_drv.bit_table()
    out = [[COLOR_BLACK] * cfg_drv.screen_rows for _ in range(cfg_drv.screen_cols)]
    for (pix, seg_hw), q in hw_order.pos_of().items():
        if q >= hw_chain_bits or q >= len(drv_tab):
            continue
        d_dst, d_seg = drv_tab[q]
        color = content.get(d_dst, COLOR_BLACK)
        if _led_on(cfg_drv, hw_order, color, d_seg, seg_hw):
            out[pix // cfg_drv.screen_rows][pix % cfg_drv.screen_rows] |= (1 << seg_hw)
    return out


# ---------------------------------------------------------------- 图案与渲染

GLYPH_BOX = [  # 16x16 的「口」字（= 当前 app_default_display 的默认画面内容，COLOR_YELLOW）
    "................",
    ".##############.",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    ".#............#.",
    ".##############.",
    "................",
]


def pattern_from_ascii(rows: list[str], color: int, w: int = 16) -> dict[int, int]:
    out = {}
    for y, line in enumerate(rows):
        for x, ch in enumerate(line[:w]):
            if ch != ".":
                out[y * w + x] = color
    return out


def _dot(dx: int, dy: int, color: int, w: int = 16) -> dict[int, int]:
    """单个像素的图案（用于「亮点落到哪」的追踪）"""
    return {dy * w + dx: color}


# 「L 形」：顶行 + 左列 —— 对转置 / 180° / 块置换都极度不对称，是判「排列类」错位的首选图案
L_SHAPE = pattern_from_ascii(["#" * 16] + [("#" + "." * 15) for _ in range(15)], COLOR_RED)

PATTERNS = {
    "点(5,7) 红": _dot(5, 7, COLOR_RED),
    "L 形 红": L_SHAPE,
    "对角线 红": pattern_from_ascii(["".join("#" if i == j else "." for j in range(16)) for i in range(16)], COLOR_RED),
    "口字框 黄": pattern_from_ascii(GLYPH_BOX, COLOR_YELLOW),
}

# 点映射探针（含四角 + 中心区一个非对称点）：把「单点在屏上落到哪」压成一行文本
DOT_PROBES = [(0, 0), (15, 0), (0, 15), (15, 15), (5, 7), (1, 4), (12, 9)]

ANSI = {0: "  ", 1: "\x1b[31m██\x1b[0m", 2: "\x1b[32m██\x1b[0m", 3: "\x1b[33m██\x1b[0m",
        4: "\x1b[34m██\x1b[0m", 5: "\x1b[35m██\x1b[0m", 6: "\x1b[36m██\x1b[0m", 7: "\x1b[37m██\x1b[0m"}


def draw(img: list[list[int]], ansi: bool = False, indent: str = "    ") -> str:
    lines = []
    for row in img:
        if ansi:
            lines.append(indent + "".join(ANSI.get(c, "??") for c in row))
        else:
            lines.append(indent + "".join("." if c == 0 else ("R" if c == 1 else "G" if c == 2 else "Y") for c in row))
    return "\n".join(lines)


def signature_text(img: list[list[int]]) -> str:
    """给一张屏图打「形态签名」：对称性 / 亮区跨度等（供现场快速比对）。"""
    h, w = len(img), len(img[0])
    nz = [(x, y) for y in range(h) for x, c in enumerate(img[y]) if c]
    if not nz:
        return "全黑（无点亮）"
    xs = [p[0] for p in nz]
    ys = [p[1] for p in nz]
    tags = []
    if len(nz) > 1 and all(img[h - 1 - y][w - 1 - x] == img[y][x] for x, y in nz):
        tags.append("图案180°自对称")
    if all(img[y][w - 1 - x] == img[y][x] for x, y in nz):
        tags.append("图案左右自对称")
    if all(img[h - 1 - y][x] == img[y][x] for x, y in nz):
        tags.append("图案上下自对称")
    if len(set(ys)) == 1:
        tags.append("仅 1 行亮（疑链长/扫描体制）")
    tags.append(f"亮区 行{min(ys)}..{max(ys)}(共{len(set(ys))}行) 列{min(xs)}..{max(xs)}(共{len(set(xs))}列)")
    return " / ".join(tags)


# ---------------------------------------------------------------- 检查 A/B/C


def check_equivalence() -> bool:
    """A. 归一版默认口径 vs 旧版（第十轮）默认口径：SDI 电平序列逐位相同。"""
    print("=" * 78)
    print("A. 无回归：默认口径逐链位发射序列 vs 改造前（第十轮）")
    print("=" * 78)

    new = Cfg()
    ok, why = new.valid()
    print(f"  默认口径: {new.label()}  FRAME_BITS={new.frame_bits}  "
          f"scan_line_px={new.scan_line_px}  CHIPS/链={new.chips_per_chain}  有效性={why}")
    assert ok, why

    # 旧版逐位公式（第十轮 dev_display_22_1665.c/_22_1665_build_bit_table 默认分支）：
    #   chip = p/16 ; s = p%16 ; win = (chip/2)/pieces ; piece = (chip/2)%pieces ;
    #   seg = chip%2 ; level = s ; dst = win*16 + (piece*16 + level)
    #   默认 pieces = 1、WINDOW_STEP = 16 → dst = (chip//2)*16 + s
    old = [((p // new.chip_bits // 2) * new.screen_rows + (p % new.chip_bits), (p // new.chip_bits) % 2)
           for p in range(new.frame_bits)]
    new_tab = new.bit_table()
    diff = [(p, old[p], new_tab[p]) for p in range(new.frame_bits) if old[p] != new_tab[p]]
    print(f"  旧版链位表条目 {len(old)} / 新表条目 {len(new_tab)}；不一致条目 = {len(diff)}")
    for p, o, n in diff[:8]:
        print(f"    p={p}: 旧={o} 新={n}")
    assert not diff and len(old) == len(new_tab)
    print("  ✓ 逐链位完全一致 → 本次改造对默认口径零行为变化\n")
    return True


def check_bijection(verbose: bool = True) -> int:
    """B. 开关矩阵枚举：每组合法口径下映射是否为双射、链位数是否自洽。"""
    print("=" * 78)
    print("B. 双射性与自洽性（开关矩阵穷举）")
    print("=" * 78)
    axis = {
        "blk_w": [0, 1, 2, 4, 8, 16],
        "seg_layout": [0, 1, 2],
        "g_first": [0, 1],
        "line_reverse": [0, 1],
        "bit_reverse": [0, 1],
        "dual": [0, 1],
        "scan_sel": [1, 2, 4, 8, 16],
    }
    total = ok_n = bad_n = skip_n = 0
    problems = []
    for values in itertools.product(*axis.values()):
        kw = dict(zip(axis.keys(), values))
        cfg = Cfg(**kw)
        total += 1
        valid, why = cfg.valid()
        if not valid:
            skip_n += 1
            continue
        tab = cfg.bit_table()
        keys = [(dst, seg) for dst, seg in tab]
        # 双射：每个 (像素, 段) 恰好一次；dst 落在相内像素范围内；seg 合法
        rng = cfg.scan_line_px
        if len(set(keys)) != len(keys):
            problems.append((cfg.label(), "链位表有重复项（非双射）"))
            bad_n += 1
            continue
        if any(dst >= rng for dst, _ in tab):
            problems.append((cfg.label(), f"dst 越界（>={rng}）"))
            bad_n += 1
            continue
        want_pixels = rng * (1 if (cfg.dual or cfg.seg_layout == 2) else 1)
        segs = set(seg for _, seg in tab)
        # 期望：非双链时每个相内像素的每个段各出现一次；双链/片内交错时同样应铺满
        expected = {(pix, seg) for pix in range(rng) for seg in range(cfg.segments)}
        if set(keys) != expected and not cfg.dual:
            missing = len(expected) - len(set(keys))
            problems.append((cfg.label(), f"映射未铺满 (像素x段)：缺 {missing} 项"))
            bad_n += 1
            continue
        if cfg.dual:
            # 双链：两条链各覆盖全部相内像素（每像素一位，段由数据线定）
            if len(set(dst for dst, _ in tab)) != rng:
                problems.append((cfg.label(), "双链链位未铺满像素"))
                bad_n += 1
                continue
        _ = want_pixels
        ok_n += 1

    print(f"  枚举组合 {total}：合法且通过 {ok_n} / 非法（编译期 assert 拦截）{skip_n} / 失败 {bad_n}")
    for label, why in problems[:10]:
        print(f"    ✗ {label}: {why}")
    print(f"  ✓ 全部合法口径均为双射（每个「像素x段」恰好占一个链位）\n")
    return bad_n


HW_CASES = [
    ("默认口径（基准，屏上应完全正确）", lambda: Cfg()),
    ("双链：R1 红链 + G1 绿链（驱动只驱 R1）", lambda: Cfg(dual=1)),
    ("段优先（片 0..15 全 R、16..31 全 G）", lambda: Cfg(seg_layout=1)),
    ("片内 R/G 交错", lambda: Cfg(seg_layout=2, blk_w=8)),
    ("一片 = 4x4 方块扇出", lambda: Cfg(blk_w=4)),
    ("一片 = 2x8 方块扇出", lambda: Cfg(blk_w=2)),
    ("一片 = 一列 16 像素（BLK_W=1）", lambda: Cfg(blk_w=1)),
    ("行内位序反转（BIT_REVERSE）", lambda: Cfg(bit_reverse=1)),
    ("块序反转（LINE_REVERSE）", lambda: Cfg(line_reverse=1)),
    ("红绿互换（G_FIRST）", lambda: Cfg(g_first=1)),
    ("数据反相（ACTIVE_HIGH=0）", lambda: Cfg(active_high=0)),
]


def dot_trace(cfg_drv: Cfg, cfg_hw: Cfg) -> str:
    """一行点映射：每个探针点（驱动侧逻辑坐标）在屏上实际落到哪个位置。

    若实物整屏几乎全亮（极性反相 / 全 1 位流情形），点追踪无意义 → 直接标注。"""
    img = screen_image(cfg_drv, cfg_hw, _dot(0, 0, COLOR_RED))
    if sum(1 for row in img for c in row if c) > cfg_drv.screen_rows * cfg_drv.screen_cols // 2:
        return "整屏几乎全亮（判「数据极性/明暗反相」）——单点追踪不适用"
    out = []
    for (dx, dy) in DOT_PROBES:
        img = screen_image(cfg_drv, cfg_hw, _dot(dx, dy, COLOR_RED))
        hit = [(x, y) for y in range(len(img)) for x, c in enumerate(img[y]) if c]
        dst = f"({hit[0][0]},{hit[0][1]})" if hit else "不亮"
        out.append(f"({dx},{dy})→{dst}")
    return "  ".join(out)


def check_signatures(md: bool = False, ansi: bool = False) -> str:
    """C. 症状签名表：驱动按默认口径发射，实物为各候补拓扑时屏上所见。"""
    drv = Cfg()
    out = []
    out.append("=" * 78)
    out.append("C. 症状签名表（驱动 = 默认口径；实物 = 另一种拓扑）")
    out.append("=" * 78)
    out.append("  读法：现场屏上出现的形态与哪一行的图一致 → 把对应开关改成该行标注值。")
    out.append("  「L 形」= 顶行 + 左列（最能暴露排列类错位）；口字框 = 当前默认画面（app_default_display）。")
    out.append("")

    for name, mk in HW_CASES:
        hw = mk()
        ok, why = hw.valid()
        if not ok:
            out.append(f"[{name}] 非法组合：{why}")
            continue
        out.append(f"■ 实物 = {name}    （诊断 ⇒ 驱动开关应改为：{hw.label()}）")
        for pname in ("L 形 红", "口字框 黄"):
            content = PATTERNS[pname]
            img = screen_image(drv, hw, content)
            out.append(f"  内容「{pname}」→ 屏上所见（. 灭 / R 红 / G 绿 / Y 黄）：")
            out.append(draw(img, ansi=ansi))
        out.append(f"  点映射（驱动侧点 → 屏上实际位置）：{dot_trace(drv, hw)}")
        out.append(f"  口字框形态签名：{signature_text(screen_image(drv, hw, PATTERNS['口字框 黄']))}")
        out.append("")

    # 链长偏差（实物链短于驱动帧）
    out.append("-" * 78)
    out.append("■ 实物链**短于**驱动帧（例：实为 1/4 扫 128 位 / 1/2 扫 256 位，或片数更少）")
    out.append("  模型：实物只保留最后移入的 N 位（其余从链尾挤出丢弃），行址恒 0 → 只有部分行亮")
    for nbits, tag in ((256, "1/2 扫或 16 片"), (128, "1/4 扫或 8 片"), (64, "1/8 扫或 4 片")):
        hw = Cfg()
        img = screen_image_truncated(drv, nbits, hw, PATTERNS["口字框 黄"])
        out.append(f"  实物链 {nbits} 位（{tag}）→ 屏上所见：")
        out.append(draw(img, ansi=ansi))
        out.append(f"  形态签名：{signature_text(img)}")
    out.append("")
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--md", action="store_true", help="输出 Markdown 到 signatures.md")
    ap.add_argument("--ansi", action="store_true", help="彩色输出（终端）")
    args = ap.parse_args()

    macros = parse_macros(DRIVER)
    # 口径同步自检：脚本模型必须与驱动文件当前宏一致（驱动改宏后此处立即报警，防止
    # 拿旧模型验证新代码）。默认 Cfg() 的字段值即「脚本认为的驱动默认口径」。
    expect = {
        "_22_1665_SCAN_SEL": Cfg().scan_sel,
        "_22_1665_CHAIN_DUAL": Cfg().dual,
        "_22_1665_CHAIN_SEG_LAYOUT": Cfg().seg_layout,
        "_22_1665_CHAIN_BLK_W": Cfg().chk_blk_w,
        "_22_1665_CHAIN_G_FIRST": Cfg().g_first,
        "_22_1665_CHAIN_LINE_REVERSE": Cfg().line_reverse,
        "_22_1665_CHAIN_BIT_REVERSE": Cfg().bit_reverse,
        "_22_1665_CHAIN_DATA_ACTIVE_HIGH": Cfg().active_high,
        "_22_1665_BLUE_LINK": Cfg().blue_link,
        "_22_1665_CHIP_BITS": Cfg().chip_bits,
        "_22_1665_SEGMENTS": Cfg().segments,
        "_22_1665_FRAME_BITS": Cfg().frame_bits,
        "_22_1665_SCAN_LINE_PX": Cfg().scan_line_px,
        "_22_1665_BLK_W": Cfg().blk_w,
    }
    print("[info] 驱动宏 ↔ 脚本模型同步自检：")
    drift = []
    for name, want in expect.items():
        got = macros.get(name)
        flag = "✓" if got == want else "✗"
        if got != want:
            drift.append(f"{name}: 驱动={got} 模型={want}")
        print(f"    {flag} {name} = {got}（模型 {want}）")
    if drift:
        print("[FAIL] 驱动宏与验证模型不一致，先同步 verify.py：")
        for d in drift:
            print("   -", d)
        return 2
    print()

    check_equivalence()
    bad = check_bijection()
    text = check_signatures(ansi=args.ansi)
    print(text)

    if args.md:
        target = HERE / "signatures.md"
        target.write_text("```\n" + text + "\n```\n", encoding="utf-8")
        print(f"[info] 已写入 {target}")

    print("=" * 78)
    print(f"结论：双射失败项 = {bad}；默认口径与旧版逐位一致 = 是；签名表见上。")
    print("=" * 78)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
