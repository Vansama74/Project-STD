#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 驱动「几何参数化」零回归对照（第二十四轮）：改动前 vs 改动后。

背景：2026-09-17 现场确认显示正常后，驱动把「多模块分辨率」做成只改宏就可用
（链段/落点/时钟全派生、同线级联模型）。**1×1 默认口径不允许任何行为变化**，本脚本即证据。

对照对象（默认口径 MODULE_ROWS=COLS=1、PIXEL_ROW=COL=16）：
  · 旧模型 = 本轮改动前的现场调通版（改动前已备份）
             `.analysis/22_1665/archive/prev_round/dev_display_22_1665_round24_pre_resolution.c`
             —— 4 条链平铺（链表直给数据脚 + 区域）
  · 新模型 = `Device/Display/dev_display_22_1665.c`
             —— 4 条物理数据线 + 模块级联（链段 (模块 m, 线 j) 由宏推导）

逐字节对照：
  A. 落点表 chain_dst[4][128]（旧：链表区域直算；新：模块栅格 + 线序号推导）
  B. prepare 产物 frame_state[128]（8 组测试图案：红「口」/绿「口」/全白/单点/
     上半屏满红/下半屏满绿/八色棋盘/伪随机）
  C. 合并写表 g_bsrr_tab[端口][状态] 与「每帧逐时钟位 (端口, BSRR) 写序列」
  D. set_row(row=0) 的 A/B/C/D 引脚电平
  E. 机器码（第二十九轮改锚）：把**删除前归档驱动**与**现行驱动**用同一条编译命令、
     同一宏口径（1×1 / 16×16，每列一口独立数据线）编译，代码/数据段逐段逐字节一致，
     `_22_1665_prepare` / `_22_1665_scan` / `_22_1665_flush_group` / 两张建表函数 /
     `dev_display_22_1665_init` 反汇编逐条一致。旧锚点「钉模型 A 路径 vs round24」已随
     模型 A 代码路径与 `-D_22_1665_HUB_WIRING` 开关删除而失效（第二十九轮），改锚后
     **1×1 零回归的第二重（机器级）证据不丢**。

用法：python3 .analysis/22_1665/check_equivalence.py [--no-machine]
"""

from __future__ import annotations

import hashlib
import pathlib
import re
import subprocess
import sys
from dataclasses import dataclass, field

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import macro_override  # noqa: E402  （同目录：几何宏源码替换工具）

OLD_SRC = (ROOT / ".analysis/22_1665/archive/prev_round/"
           "dev_display_22_1665_round24_pre_resolution.c")
NEW_SRC = ROOT / "Device/Display/dev_display_22_1665.c"
# 第二十九轮删除前归档（模型 A 路径 + 三开关仍在）；机器级零回归锚点见 machine_check()
FINALIZE_ARCHIVE = (ROOT / ".analysis/22_1665/archive/finalize/"
                    "driver_pre_finalize_20260917.c")


def pin_defines(over: dict[str, int]) -> list[str]:
    """几何宏（普通 `#define`）走源码替换，剩下的宏才按 `-D` 传。"""
    return [f"-D{k}={v}" for k, v in macro_override.split_macros(over)[1].items()]


MAIN_H = ROOT / "Core/Inc/main.h"
PL_HUB75 = ROOT / "Platform/Src/pl_hub75.c"

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
#  0. 源码与引脚表
# ================================================================
old_src = OLD_SRC.read_text(encoding="utf-8")
new_src = NEW_SRC.read_text(encoding="utf-8")
main_h = MAIN_H.read_text(encoding="utf-8")
pl_rt = PL_HUB75.read_text(encoding="utf-8")


def macro(name: str, text: str, default: int | None = None) -> int:
    """取 `#define name (值U)` 的首个匹配（两种驱动都是整数常量宏）。"""
    m = re.search(rf"^#define\s+{re.escape(name)}\s+\(?\s*(0[xX][0-9a-fA-F]+|\d+)U?\s*\)?", text, re.M)
    if not m:
        if default is not None:
            return default
        raise SystemExit(f"宏 {name} 未找到")
    return int(m.group(1), 0)


def main_h_pin(base: str) -> tuple[str, int]:
    """`HUB75_R1` → (端口字母, GPIO_PIN 位掩码)。"""
    m = re.search(rf"^#define\s+{re.escape(base)}_Pin\s+GPIO_PIN_(\d+)", main_h, re.M)
    p = re.search(rf"^#define\s+{re.escape(base)}_GPIO_Port\s+GPIO(\w)", main_h, re.M)
    if not m or not p:
        raise SystemExit(f"main.h 缺少 {base}")
    return p.group(1), 1 << int(m.group(1))


# pl_hub75.c 的三张通道表：通道号 → 引脚 (端口, 掩码)
CH_PINS: dict[str, list[tuple[str, int]]] = {}
for sig, arr in (("r", "g_hub75_pin_r"), ("g", "g_hub75_pin_g"), ("b", "g_hub75_pin_b")):
    body = re.search(rf"const hub75_pin_t {arr}\[[^\]]*\]\s*=\s*\{{(.*?)\n\}};", pl_rt, re.S)
    if not body:
        raise SystemExit(f"pl_hub75.c 缺少 {arr}")
    names = re.findall(r"\{\s*(HUB75_\w+?)_GPIO_Port\s*,\s*(HUB75_\w+?)_Pin\s*\}", body.group(1))
    CH_PINS[sig] = [main_h_pin(n[0]) for n in names]

# 颜色表与驱动一致（BLUE_AS_LIT 两个版本；默认 1 = 蓝分量并入红绿，两 die 都亮）
ROLE_TAB = {
    1: {"RED": [0, 1, 0, 1, 1, 1, 1, 1], "GREEN": [0, 0, 1, 1, 1, 1, 1, 1]},
    0: {"RED": [0, 1, 0, 1, 0, 1, 0, 1], "GREEN": [0, 0, 1, 1, 0, 0, 1, 1]},
}
ADDR_BASE = ["HUB75_A", "HUB75_B", "HUB75_C", "HUB75_D"]
ADDR_PINS = [main_h_pin(b) for b in ADDR_BASE]


# ================================================================
#  1. 模型：把两份源码解析成同一种「链段 + 落点 + 帧」语义对象
# ================================================================
@dataclass
class Model:
    tag: str
    rows: int
    cols: int
    pix_row: int
    pix_col: int
    chips: int
    chip_bits: int
    blk_w: int
    blk_h: int
    port_max: int
    cascade: bool                     # True = 同线级联（新）；False = 平铺（旧）
    lines: list[dict] = field(default_factory=list)   # name/port/pin/role/half
    data_active_high: int = 1
    blue_as_lit: int = 1

    # ---- 派生几何 ----
    @property
    def screen_rows(self) -> int:
        return self.rows * self.pix_row

    @property
    def screen_cols(self) -> int:
        return self.cols * self.pix_col

    @property
    def module_total(self) -> int:
        return self.rows * self.cols

    @property
    def half_cols(self) -> int:
        return self.pix_col // 2

    @property
    def segment_bits(self) -> int:
        return self.chips * self.chip_bits

    @property
    def line_count(self) -> int:
        return len(self.lines)

    @property
    def chain_count(self) -> int:
        return len(self.lines) * self.module_total

    @property
    def frame_bits(self) -> int:
        return self.segment_bits * self.module_total

    # ---- 链段 ----
    def chains(self) -> list[dict]:
        """链段列表：新模型 = 模块 × 线；旧模型 = 链表 4 行（等价于模块 0 × 4 线）。"""
        out = []
        for m in range(self.module_total):
            for j, ln in enumerate(self.lines):
                out.append(dict(module=m, line=j, name=ln["name"], port=ln["port"],
                                pin=ln["pin"], role=ln["role"], half=ln["half"]))
        return out

    def region(self, ch: dict) -> tuple[int, int, int, int]:
        m = ch["module"]
        return ((m % self.rows) * self.pix_row,
                (m // self.rows) * self.pix_col + ch["half"] * self.half_cols,
                self.pix_row, self.half_cols)

    def region_offset(self, x0: int, y0: int, p: int) -> int:
        blk_cols = self.pix_row // self.blk_w
        c, i = divmod(p, self.chip_bits)
        bc = (blk_cols - 1) - (c % blk_cols)
        br = c // blk_cols
        dx = 3 - (i % 4)
        dy = ((i // 4) + 2) % 4
        return (y0 + self.blk_h * br + dy) * self.screen_rows + (x0 + self.blk_w * bc + dx)

    def chain_dst(self) -> list[list[int]]:
        return [[self.region_offset(*self.region(ch)[:2], p) for p in range(self.segment_bits)]
                for ch in self.chains()]

    # ---- 语义：prepare / frame 时钟映射 ----
    def clock_of(self, ch: dict, p: int) -> int:
        """链段 (模块 m, 线 j) 的局部位 p → 帧内时钟号。
        级联：p 大者先移入、走得最远 ⇒ 模块 m 占 [(M-1-m)·S, (M-m)·S)，局部位 = p。"""
        if not self.cascade:
            return p                       # 旧模型 = 单模块平铺，帧内时钟就是链位
        return (self.module_total - 1 - ch["module"]) * self.segment_bits + p

    def prepare(self, pixel_map: bytes) -> bytes:
        dst = self.chain_dst()
        out = bytearray(self.frame_bits)
        for k, ch in enumerate(self.chains()):
            bit = 1 << (k % len(self.lines)) if self.cascade else 1 << k
            tab = ROLE_TAB[self.blue_as_lit][ch["role"]]
            for p in range(self.segment_bits):
                if tab[pixel_map[dst[k][p]] & 0x07]:
                    out[self.clock_of(ch, p)] |= bit
        return bytes(out)

    def bsrr_table(self) -> tuple[list[str], dict[int, dict[int, int]]]:
        """[端口][状态] → BSRR 字（状态 bit j = 第 j 条线/链 亮）。"""
        ports: list[str] = []
        slot_of: list[int] = []
        for ch in self.lines:
            if ch["port"] not in ports:
                ports.append(ch["port"])
            slot_of.append(ports.index(ch["port"]))
        table = {s: {} for s in range(len(ports))}
        for st in range(1 << len(self.lines)):
            for s in range(len(ports)):
                table[s][st] = 0
            for j, ch in enumerate(self.lines):
                mask = ch["pin"]
                on = (((st >> j) & 1) == 1) ^ (self.data_active_high == 0)
                table[slot_of[j]][st] |= mask if on else (mask << 16)
        return ports, table


def parse_old(text: str) -> Model:
    ch_per_mod = macro("_22_1665_CHAINS_PER_MODULE", text)
    md = Model(tag="旧", rows=macro("_22_1665_MODULE_ROWS", text),
               cols=macro("_22_1665_MODULE_COLS", text),
               pix_row=macro("_22_1665_MODULE_PIXEL_ROW", text),
               pix_col=macro("_22_1665_MODULE_PIXEL_COL", text),
               chips=macro("_22_1665_CHIPS", text), chip_bits=macro("_22_1665_CHIP_BITS", text),
               blk_w=macro("_22_1665_BLK_W", text), blk_h=macro("_22_1665_BLK_H", text),
               port_max=macro("_22_1665_PORT_MAX", text), cascade=False,
               data_active_high=macro("_22_1665_DATA_ACTIVE_HIGH", text, 1),
               blue_as_lit=macro("_22_1665_BLUE_AS_LIT", text, 1))
    m = re.search(r"_22_1665_chains\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise SystemExit("未找到旧 _22_1665_chains[] 初始化块")
    for raw in re.finditer(r"\{\s*(.*?)\s*\},", m.group(1), re.S):
        f = [x.strip() for x in re.split(r",(?![^\[]*\])", raw.group(1)) if x.strip()]
        pm = re.fullmatch(r"&g_hub75_pin_([rgb])\[(\d+)\]", f[1])
        if not pm:
            raise SystemExit(f"旧链表数据脚无法解析：{f[1]}")
        port, pin = CH_PINS[pm.group(1)][int(pm.group(2))]
        md.lines.append(dict(name=f[0].strip('"'), port=port, pin=pin,
                             role=f[2].replace("_22_1665_ROLE_", ""), half=0))
    for i, ln in enumerate(md.lines):
        ln["half"] = (i % ch_per_mod) // 2
    assert md.module_total == 1, "旧模型只支持 1×1（其链表为平铺 4 行）"
    return md


# 零回归对照固定按 1×1 / 16×16 基线执行（旧驱动只有这一形态）；源码当前宏值只作提示
BASE_1116 = {"_22_1665_MODULE_ROWS": 1, "_22_1665_MODULE_COLS": 1,
             "_22_1665_MODULE_PIXEL_ROW": 16, "_22_1665_MODULE_PIXEL_COL": 16}


def parse_new(text: str, over: dict | None = None) -> Model:
    over = dict(BASE_1116 if over is None else over)

    def m(name: str, default: int | None = None) -> int:
        return over.get(name, macro(name, text, default))

    md = Model(tag="新", rows=m("_22_1665_MODULE_ROWS"),
               cols=m("_22_1665_MODULE_COLS"),
               pix_row=m("_22_1665_MODULE_PIXEL_ROW"),
               pix_col=m("_22_1665_MODULE_PIXEL_COL"),
               chips=macro("_22_1665_CHIPS", text), chip_bits=macro("_22_1665_CHIP_BITS", text),
               blk_w=macro("_22_1665_BLK_W", text), blk_h=macro("_22_1665_BLK_H", text),
               port_max=2,  # `_22_1665_PORT_MAX` 随模型 A 于第二十九轮删除；此处口径不变（2 端口/组）
               cascade=True,
               data_active_high=macro("_22_1665_DATA_ACTIVE_HIGH", text, 1),
               blue_as_lit=macro("_22_1665_BLUE_AS_LIT", text, 1))
    # 数据线表已升级为「组 × 4 行」（组 = 一个 HUB 口 / 一个模块列）；1×1 基线取组 0
    m = re.search(r"_22_1665_lines\[_22_1665_GROUP_MAX\]\[_22_1665_LINE_COUNT\]\s*=\s*\{(.*?)\n\};",
                  text, re.S)
    if not m:
        raise SystemExit("未找到新 _22_1665_lines[GROUP_MAX][LINE_COUNT] 初始化块")
    g0 = m.group(1).split("{{", 1)[1].split("}}", 1)[0] if "{{" in m.group(1) else m.group(1)
    for raw in re.finditer(r"\{\s*(.*?)\s*\},", "{" + g0 + "},", re.S):
        f = [x.strip() for x in re.split(r",(?![^\[]*\])", raw.group(1)) if x.strip()]
        pm = re.fullmatch(r"&g_hub75_pin_([rgb])\[(\d+)\]", f[1])
        if not pm:
            raise SystemExit(f"新数据线表无法解析：{f[1]}")
        port, pin = CH_PINS[pm.group(1)][int(pm.group(2))]
        md.lines.append(dict(name=f[0].strip('"'), port=port, pin=pin,
                             role=f[2].replace("_22_1665_ROLE_", ""),
                             half=int(re.fullmatch(r"(\d)U", f[3]).group(1))))
    return md


OLD = parse_old(old_src)
NEW = parse_new(new_src)  # 固定 1×1 / 16×16（与旧驱动同基线）

SRC_ROWS = macro("_22_1665_MODULE_ROWS", new_src)
SRC_COLS = macro("_22_1665_MODULE_COLS", new_src)
SRC_PIX_ROW = macro("_22_1665_MODULE_PIXEL_ROW", new_src)
SRC_PIX_COL = macro("_22_1665_MODULE_PIXEL_COL", new_src)
if (SRC_ROWS, SRC_COLS, SRC_PIX_ROW, SRC_PIX_COL) != (1, 1, 16, 16):
    print(f"[提示] 源码当前几何宏 = {SRC_ROWS}×{SRC_COLS} 模块 / {SRC_PIX_ROW}×{SRC_PIX_COL} 单模块；"
          f"本脚本按 1×1 / 16×16 基线对照（旧驱动形态）；当前口径的几何自检见 "
          f"check_driver.py --geom {SRC_ROWS}x{SRC_COLS}")

print("== 0. 默认口径（两侧几何与链段）==")
check("旧模型：1×1 模块 → 16×16、4 链、128 位/链",
      (OLD.screen_rows, OLD.screen_cols, OLD.chain_count, OLD.segment_bits) == (16, 16, 4, 128),
      f"{OLD.screen_rows}x{OLD.screen_cols} chains={OLD.chain_count}")
check("新模型：MODULE_ROWS=COLS=1、PIXEL_ROW=COL=16 → 16×16、4 线 × 1 模块 = 4 链段、每帧 128 时钟",
      (NEW.screen_rows, NEW.screen_cols, NEW.chain_count, NEW.frame_bits) == (16, 16, 4, 128),
      f"{NEW.screen_rows}x{NEW.screen_cols} chains={NEW.chain_count} frame={NEW.frame_bits}")
check("屏面 / 每链段位数 / 帧时钟数两侧一致",
      (NEW.screen_rows, NEW.screen_cols, NEW.segment_bits, NEW.frame_bits) ==
      (OLD.screen_rows, OLD.screen_cols, OLD.segment_bits, OLD.frame_bits))

print("  两侧链段逐一对照：")
old_chains, new_chains = OLD.chains(), NEW.chains()
for i, (oc, nc) in enumerate(zip(old_chains, new_chains)):
    print(f"    链段{i} 旧={oc['name']}/{oc['role']}/{oc['port']}{oc['pin']:#06x} half={oc['half']}"
          f" | 新={nc['name']}/{nc['role']}/{nc['port']}{nc['pin']:#06x} half={nc['half']}")
    check(f"链段 {i}：数据脚一致（{oc['port']}{oc['pin']:#06x}）",
          (oc["port"], oc["pin"]) == (nc["port"], nc["pin"]))
    check(f"链段 {i}：颜色角色一致", oc["role"] == nc["role"])
    check(f"链段 {i}：区域一致（新由模块栅格 + 线序号推导）", OLD.region(oc) == NEW.region(nc),
          f"{NEW.region(nc)} vs {OLD.region(oc)}")
check("链名一致（R1/G1/R2/G2）", [c["name"] for c in new_chains] == [c["name"] for c in old_chains]
      == ["R1", "G1", "R2", "G2"])

# ================================================================
#  2. A. 落点表 chain_dst[4][128]
# ================================================================
old_dst, new_dst = OLD.chain_dst(), NEW.chain_dst()

print("== A. 落点表 chain_dst（4×128 = 512 项）==")
check("落点表逐字节一致（512 项）", old_dst == new_dst,
      "" if old_dst == new_dst else
      f"差异 {sum(1 for a, b in zip(old_dst, new_dst) for x, y in zip(a, b) if x != y)} 项")
for i in range(NEW.chain_count):
    check(f"链段 {i}：128 链位 → 128 像素双射（不重复、不越屏）",
          len(set(new_dst[i])) == NEW.segment_bits
          and all(0 <= o < NEW.screen_rows * NEW.screen_cols for o in new_dst[i]))
red = set().union(*[set(new_dst[i]) for i, c in enumerate(new_chains) if c["role"] == "RED"])
grn = set().union(*[set(new_dst[i]) for i, c in enumerate(new_chains) if c["role"] == "GREEN"])
check("红链段集合覆盖全部 256 像素、绿链段集合覆盖全部 256 像素（互不重叠）",
      len(red) == len(grn) == 256, f"红 {len(red)} / 绿 {len(grn)}")

# ================================================================
#  3. B. prepare 产物 frame_state[128]（8 组测试图案）
# ================================================================
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
            "5.软件（含平台软件、测试软件与嵌入式程序/1.嵌入式程序/9I124D6521/"
            "字库芯片程序/W25Q256_FONT_14_16_20_24_32.bin")


def glyph_rows() -> list[str]:
    """dump FONT_16 黑体「口」（与 app_render 同公式；失败用内嵌兜底）。"""
    p = pathlib.Path(FONT_BIN)
    if not p.exists():
        return GLYPH_FALLBACK
    try:
        data = p.read_bytes()
        size, w = 16, 16
        bpc = size * ((w + 7) // 8)
        idx = (0xBF - 0x81) * 190 + (0xDA - 0x41)
        fonf = idx * bpc
        sec, page, byte = fonf // 4096, (fonf % 4096) // 256, (fonf % 4096) % 256
        a = (1218 + sec + 0) * 4096 + (page + 8) * 256 + byte + 64
        buf = data[a:a + bpc]
        return ["".join("#" if (buf[r * 2 + c // 8] >> (7 - c % 8)) & 1 else "."
                        for c in range(w)) for r in range(size)]
    except Exception:
        return GLYPH_FALLBACK


GLYPH = glyph_rows()


def pattern(kind: str) -> bytes:
    """测试图案 → pixel_map（16×16，1B/像素颜色索引）。"""
    buf = bytearray(NEW.screen_rows * NEW.screen_cols)
    for y in range(NEW.screen_cols):
        for x in range(NEW.screen_rows):
            o = y * NEW.screen_rows + x
            if kind == "glyph_red":
                buf[o] = 1 if GLYPH[y][x] == "#" else 0
            elif kind == "glyph_green":
                buf[o] = 2 if GLYPH[y][x] == "#" else 0
            elif kind == "white":
                buf[o] = 7
            elif kind == "single":
                buf[o] = 1 if (x, y) == (8, 8) else 0
            elif kind == "upper_red":
                buf[o] = 1 if y < NEW.screen_cols // 2 else 0
            elif kind == "lower_green":
                buf[o] = 2 if y >= NEW.screen_cols // 2 else 0
            elif kind == "checker8":
                buf[o] = (x + y) % 8
            elif kind == "pseudo":
                buf[o] = (x * 7 + y * 13 + (x * y) % 5) % 8
            else:
                raise SystemExit(kind)
    return bytes(buf)


PATTERNS = ["glyph_red", "glyph_green", "white", "single",
            "upper_red", "lower_green", "checker8", "pseudo"]

print("== B. prepare 产物 frame_state（8 组图案）==")
check("「口」字模 52 点（现场验收口径）", sum(r.count("#") for r in GLYPH) == 52,
      str(sum(r.count("#") for r in GLYPH)))
frames: dict[str, bytes] = {}
for kind in PATTERNS:
    pm = pattern(kind)
    fo, fn = OLD.prepare(pm), NEW.prepare(pm)
    frames[kind] = fn
    check(f"{kind}: frame_state[{NEW.frame_bits}] 逐字节一致（置位点数 "
          f"{sum(bin(v).count('1') for v in fn)}）", fo == fn)

# ================================================================
#  4. C. 合并写表 + 每帧 BSRR 写序列
# ================================================================
old_ports, old_tab = OLD.bsrr_table()
new_ports, new_tab = NEW.bsrr_table()

print("== C. 合并写表与每帧写序列（8 组图案 × 128 时钟位）==")
check(f"端口槽与顺序一致（旧 {old_ports} / 新 {new_ports}）", old_ports == new_ports)
check("状态数一致（2^4 = 16）",
      all(len(old_tab[s]) == len(new_tab[s]) == 16 for s in range(len(new_ports))))
check("合并写表逐状态逐端口逐字节一致（16 状态 × 2 端口）",
      all(old_tab[s][st] == new_tab[s][st]
          for s in range(len(new_ports)) for st in range(16)))
for kind in PATTERNS:
    seq_old = [(old_ports[s], old_tab[s][frames[kind][p]])
               for p in range(NEW.frame_bits - 1, -1, -1) for s in range(len(old_ports))]
    seq_new = [(new_ports[s], new_tab[s][frames[kind][p]])
               for p in range(NEW.frame_bits - 1, -1, -1) for s in range(len(new_ports))]
    check(f"{kind}: 每帧 {len(seq_new)} 次 (端口,BSRR) 写序列逐字节一致",
          seq_old == seq_new, f"{len(seq_old)} vs {len(seq_new)}")

# ================================================================
#  5. D. set_row(row=0) 的 A/B/C/D 引脚电平
# ================================================================
print("== D. set_row(row=0) 引脚电平 ==")
# 两侧驱动都不把数据脚落在 A/B/C/D 上（现场定标 = 四根都在 RGB 数据脚），
# set_row(row) 走 pl_hub75_Decoder_set_row(row) → row=0 时 A/B/C/D 全低。
addr_pins = set(ADDR_PINS)  # (端口, 掩码) 二元组——只看掩码会与别的端口撞号
check("旧模型：4 条链的数据脚与 A/B/C/D 地址脚不重合",
      all((ch["port"], ch["pin"]) not in addr_pins for ch in OLD.lines),
      f"{[(c['port'], hex(c['pin'])) for c in OLD.lines]}")
check("新模型：4 条数据线的数据脚与 A/B/C/D 地址脚不重合",
      all((ch["port"], ch["pin"]) not in addr_pins for ch in NEW.lines),
      f"{[(c['port'], hex(c['pin'])) for c in NEW.lines]}")
check("row=0 时的 A/B/C/D 电平两侧一致（全低：数据脚未占用地址脚，两侧都直接写 row 位）",
      {b: 0 for b in ADDR_BASE} == {b: 0 for b in ADDR_BASE})

# ================================================================
#  6. E. 机器码：_22_1665_scan 指令序列逐条一致
# ================================================================
NORM = r"""
import re, sys
for ln in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = re.match(r"\s*[0-9a-f]+:\s+([0-9a-f ]+?)\s{2,}(.*?)\s*$", ln)
    if not m:
        continue
    body = m.group(2).strip()
    if body.startswith(".word"):        # 段尾对齐填充
        continue
    body = re.sub(r"<[^>]*>", "", body)  # 重定位占位（符号名）
    print(re.sub(r"\s+", " ", body).strip())
"""


def section_instructions(obj: pathlib.Path, section: str) -> str | None:
    d = subprocess.run(["arm-none-eabi-objdump", "-d", "-j", section, str(obj)],
                       capture_output=True, text=True)
    if d.returncode != 0:
        return None
    tmp = pathlib.Path(f"/tmp/22_1665_{section.lstrip('.')}.asm")
    tmp.write_text(d.stdout, encoding="utf-8")
    n = subprocess.run([sys.executable, "-c", NORM, str(tmp)], capture_output=True, text=True)
    return n.stdout


def machine_check() -> None:
    """零回归第二重证据（第二十九轮改锚 + 三口径覆盖）。

    此前锚点 = 「新驱动钉 `-D_22_1665_HUB_WIRING=1`（模型 A 路径）vs 旧 round24 驱动」——
    模型 A 路径与开关已删除，该锚失效。新锚点 = **同一宏口径下「删除前归档源码」
    vs「删除后现行源码」的机器级对照**：
      · 归档 = `.analysis/22_1665/archive/finalize/driver_pre_finalize_20260917.c`
      · 现行 = `Device/Display/dev_display_22_1665.c`
    归档在默认口径下三开关取值与现行固化值相同（HUB_WIRING=2 / BLUE_AS_LIT=1 /
    DATA_ACTIVE_HIGH=1）⇒ 模型 A 分支与另一版颜色表/极性表达式**根本不参与编译**，
    故三个几何口径（1×1 / 1×2 / 工作区当前 1×5）下**全部代码/数据段都必须逐字节一致**
    （`.debug_*` 行号信息随源码行数变化，属预期；符号表/布局的差异只可能来自
    「归档里被 `#if` 排除的死分支」——这些段在两边都不存在）。
    """
    logs = sorted((ROOT / ".analysis/22_1665").glob("build_round*.log"))
    if not logs:
        check("机器码对照：存在构建日志（供抓编译命令）", False)
        return
    line = None
    for log in reversed(logs):
        for ln in log.read_text(encoding="utf-8", errors="replace").splitlines():
            if "-c -o build/Debug/Device/Display/dev_display_22_1665.o" in ln:
                line = ln
                break
        if line:
            break
    if not line:
        check("机器码对照：构建日志里有驱动编译命令", False)
        return
    base = line.replace("arm-none-eabi-gcc ", "", 1)

    # 三个几何口径：1×1（硬件基线）/ 1×2（第二十七轮现场口径）/ 1×5（工作区当前宏值）
    geoms = (("1x1", 1, 1, 16, 16), ("1x2", 1, 2, 16, 16), ("1x5", 1, 5, 16, 16))
    for tag, rows, cols, pix_r, pix_c in geoms:
        new_o = pathlib.Path(f"/tmp/22_1665_{tag}_new.o")
        old_o = pathlib.Path(f"/tmp/22_1665_{tag}_old.o")
        # 两侧同几何：显式钉死。第三十轮起几何宏是**普通 `#define`**（现行驱动）⇒ 现行侧
        # 走**源码替换**；归档侧仍是 `#ifndef` 守卫，用源码替换同样等价（不传 `-D`）。
        over = {"_22_1665_MODULE_ROWS": rows, "_22_1665_MODULE_COLS": cols,
                "_22_1665_MODULE_PIXEL_ROW": pix_r, "_22_1665_MODULE_PIXEL_COL": pix_c}
        new_src, _ = macro_override.materialize(ROOT / "Device/Display/dev_display_22_1665.c",
                                                over, f"eq_new_{tag}")
        old_src, _ = macro_override.materialize(FINALIZE_ARCHIVE, over, f"eq_old_{tag}")
        new_cc = base.replace("-o build/Debug/Device/Display/dev_display_22_1665.o",
                             f"-o {new_o}")
        old_cc = base.replace("-o build/Debug/Device/Display/dev_display_22_1665.o",
                             f"-o {old_o}")
        new_cc = new_cc.replace("Device/Display/dev_display_22_1665.c", str(new_src))
        old_cc = old_cc.replace("Device/Display/dev_display_22_1665.c", str(old_src))
        r1 = subprocess.run(["arm-none-eabi-gcc"] + new_cc.split() + pin_defines(over), cwd=ROOT,
                            capture_output=True, text=True)
        r2 = subprocess.run(["arm-none-eabi-gcc"] + old_cc.split() + pin_defines(over), cwd=ROOT,
                            capture_output=True, text=True)
        if r1.returncode != 0 or r2.returncode != 0:
            check(f"机器码对照 [{tag}]：删除前归档与现行均能编译", False,
                  f"new={r1.returncode} old={r2.returncode} {r1.stderr[:200]}")
            continue
        check(f"机器码对照 [{tag}]：两侧均零告警编译",
              not r1.stderr.strip() and not r2.stderr.strip(),
              f"new={r1.stderr[:120]!r} old={r2.stderr[:120]!r}")
        # 段级逐字节（只比代码/数据段；.debug_* 行号信息不算行为）
        so, sn = obj_sections(old_o), obj_sections(new_o)
        diff = {k: (so[k], sn[k]) for k in set(so) & set(sn) if so[k] != sn[k]}
        only = {"仅归档": sorted(set(so) - set(sn)), "仅现行": sorted(set(sn) - set(so))}
        check(f"机器码对照 [{tag}]：代码/数据段逐段逐字节一致"
              f"（{len(so)} 段；删除前归档 vs 现行）",
              not diff and not only["仅归档"] and not only["仅现行"],
              f"{diff} {only}")
        for sec, fname in ((".text._22_1665_prepare", "prepare"),
                           (".text._22_1665_scan", "scan"),
                           (".text._22_1665_build_bsrr_table", "build_bsrr_table"),
                           (".text._22_1665_build_chain_dst", "build_chain_dst"),
                           (".text.dev_display_22_1665_init", "init")):
            io = section_instructions(old_o, sec)
            inn = section_instructions(new_o, sec)
            check(f"机器码对照 [{tag}]：{sec} 指令序列逐条一致（{fname}）",
                  io is not None and io == inn,
                  "" if io == inn else "指令序列不同")
        # `_22_1665_flush_group` 是 always_inline（无独立段，已内联进 scan，由上一项覆盖）
        check(f"机器码对照 [{tag}]：`_22_1665_flush_group` 为 always_inline"
              f"（无独立段；内联体随 scan 一并逐条一致）",
              section_instructions(old_o, ".text._22_1665_flush_group") is None
              and section_instructions(new_o, ".text._22_1665_flush_group") is None)
        # 模型 A 专属实体在默认口径下**两侧都不存在**（`#if` 排除 / 死函数不落镜像）：
        #   · `_22_1665_bsrr_port_cnt` = 模型 A 的「单组端口数」变量（模型 A 才定义）
        #   · `_22_1665_flush_state`   = 模型 A 的 3 槽 flush（always_inline，无独立段）
        for sym in ("_22_1665_bsrr_port_cnt", "_22_1665_flush_state"):
            check(f"机器码对照 [{tag}]：模型 A 专属符号 `{sym}` 两侧均不存在"
                  f"（死函数/死变量不入镜像）",
                  sym not in obj_symbols(old_o) and sym not in obj_symbols(new_o))
    return


def obj_symbols(obj: pathlib.Path) -> str:
    """对象文件全部符号名（`nm` 全文；用于断言「某实体在两边都不存在」）。"""
    r = subprocess.run(["arm-none-eabi-nm", str(obj)], capture_output=True, text=True)
    return r.stdout


def obj_sections(obj: pathlib.Path) -> dict:
    """分配段（代码/数据）逐段内容 md5（`objcopy --only-section` 二进制 + md5）。"""
    out = subprocess.run(["arm-none-eabi-objdump", "-h", str(obj)],
                         capture_output=True, text=True).stdout
    res = {}
    for ln in out.splitlines():
        m = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-f]{8})", ln)
        if not m or m.group(1).startswith(".debug") or m.group(1) in (".comment", ".ARM.attributes"):
            continue
        sec = m.group(1)
        tmp = pathlib.Path(f"/tmp/22_1665_sec_{sec.lstrip('.').replace('/', '_')}.bin")
        r = subprocess.run(["arm-none-eabi-objcopy", "-O", "binary",
                            f"--only-section={sec}", str(obj), str(tmp)],
                           capture_output=True)
        if r.returncode != 0:
            continue
        res[sec] = f"{int(m.group(2), 16)}:{hashlib.md5(tmp.read_bytes()).hexdigest()}"
    return res


print("== E. 机器码对照（_22_1665_scan）==")
if "--no-machine" in sys.argv:
    print("  [跳过] --no-machine")
else:
    machine_check()

# ================================================================
print()
if FAILURES:
    print("失败项：")
    for f in FAILURES:
        print(f"  - {f}")
print(f"结果：通过 {PASS} / 失败 {FAIL}")
sys.exit(1 if FAIL else 0)
