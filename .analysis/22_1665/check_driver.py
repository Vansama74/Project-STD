#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 驱动结构自检 + **几何口径自检**（第二十四轮：几何全派生后重写）。

检查对象：`Device/Display/dev_display_22_1665.c`。

  A. 几何宏与派生式（与兄弟驱动同构：四个**普通 `#define`** 宏 → SCREEN/FRAME/CHAIN 全派生；
     第三十轮起不再用 `#ifndef` 守卫 —— 改口径 = 改文件宏，换口径脚本走源码替换）
  B. 落点模型（**按指定口径复算**）：链段区域由「模块序号 + 线序号」推导、每链段 128 位双射、
     每模块红/绿链段各覆盖该模块 256 像素、全屏红/绿各覆盖全部像素、级联时钟映射无缝无重
  C. CCMRAM 占用复算（模型 B：1408·M + 256·MODULE_COLS）+ 基类字段一致性
     （尺寸 ⇄ 数组 ⇄ 实例绑定）；**第三十四轮起：无编译期防御**——0 条 `_Static_assert`、
     无 `#error`，「MODULE_COLS ≤ 5 / 像素与帧位 ≤ 65535 / 整片 CCMRAM 由链接期兜底」
     降级为**注释契约 + doc/01 §0.3 文档**（越限后果 = 静默越界 / 链接期溢出）
  D. 源码卫生：RTT / 探针 / 兼容口径 / 手工补行**零残留**；行为开关仅存 1 个（链首口径）；
     prepare 逐链掩码表达式正确（round21 教训）；历史轮次叙述零残留
  E. 文件规模：文件头 ≤ 60 行、整文件 ≤ 400 行（round34 精简后 372 行，防历史叙述回流）
  F. 「口」52 点（现场验收口径；字库 bin 不在本机时跳过）

用法：
  python3 .analysis/22_1665/check_driver.py                     # 默认口径（源码宏值）
  python3 .analysis/22_1665/check_driver.py --geom 2x1          # 多模块口径自检
  python3 .analysis/22_1665/check_driver.py --geom 2x2 --pix 16x16
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "Device/Display/dev_display_22_1665.c"

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


src = SRC.read_text(encoding="utf-8")


def macro(name: str, default: int | None = None) -> int:
    m = re.search(rf"^#define\s+{re.escape(name)}\s+\(?\s*(0[xX][0-9a-fA-F]+|\d+)U?\s*\)?", src, re.M)
    if not m:
        if default is not None:
            return default
        raise SystemExit(f"宏 {name} 未找到")
    return int(m.group(1), 0)


ap = argparse.ArgumentParser()
ap.add_argument("--geom", default=None, help="模块数 RxC（覆盖源码默认值，用于多口径自检）")
ap.add_argument("--pix", default=None, help="单模块像素 ROWxCOL（覆盖源码默认值）")
args = ap.parse_args()

MODULE_ROWS = macro("_22_1665_MODULE_ROWS")
MODULE_COLS = macro("_22_1665_MODULE_COLS")
PIX_ROW = macro("_22_1665_MODULE_PIXEL_ROW")
PIX_COL = macro("_22_1665_MODULE_PIXEL_COL")
if args.geom:
    MODULE_ROWS, MODULE_COLS = (int(x) for x in args.geom.lower().split("x"))
if args.pix:
    PIX_ROW, PIX_COL = (int(x) for x in args.pix.lower().split("x"))

SCREEN_ROWS = MODULE_ROWS * PIX_ROW
SCREEN_COLS = MODULE_COLS * PIX_COL
MODULES = MODULE_ROWS * MODULE_COLS
CHIPS = macro("_22_1665_CHIPS")
CHIP_BITS = macro("_22_1665_CHIP_BITS")
BLK_W = macro("_22_1665_BLK_W")
BLK_H = macro("_22_1665_BLK_H")
LINE_COUNT = macro("_22_1665_LINE_COUNT")
SEGMENT_BITS = CHIPS * CHIP_BITS
FRAME_BITS = SEGMENT_BITS * MODULES
CHAIN_COUNT = LINE_COUNT * MODULES
HALF = PIX_COL // 2
# 接线模型：**唯一 = 每列一口独立数据线**（第二十九轮固化，`_22_1665_HUB_WIRING` 开关已删除）
CHAIN_HEAD = macro("_22_1665_CHAIN_HEAD_IS_MODULE0", 0)
GROUP_PORT_MAX = 2  # = `_22_1665_GROUP_PORT_MAX`（本板每组 4 线跨 2 个端口：G+B / B+E）
# 每帧时钟数 = 128 × 每列模块数（各组并行）
CLOCKS = SEGMENT_BITS * MODULE_ROWS
CCM = (2 * CHAIN_COUNT * SEGMENT_BITS + SCREEN_ROWS * SCREEN_COLS + FRAME_BITS +
       8 * GROUP_PORT_MAX * MODULE_COLS * (1 << LINE_COUNT))

TAG = f"{MODULE_ROWS}×{MODULE_COLS} 模块 / {PIX_ROW}×{PIX_COL} 单模块 → 屏面 {SCREEN_ROWS}×{SCREEN_COLS}"
print(f"== 口径：{TAG}（每列一口独立数据线 / "
      f"{MODULES} 模块 / {CHAIN_COUNT} 链段 / 每帧 {CLOCKS} 时钟 / CCM {CCM}B）==")

# ================================================================
#  A. 几何宏与派生式
# ================================================================
print("== A. 几何宏与派生式 ==")
check("四个几何宏为**普通 `#define`**（兄弟驱动同款；第三十轮起不再有 `#ifndef` 守卫 ⇒ `-D` 不生效）",
      all(re.search(rf"^#define\s+{n}\b", src, re.M) for n in
          ("_22_1665_MODULE_ROWS", "_22_1665_MODULE_COLS",
           "_22_1665_MODULE_PIXEL_ROW", "_22_1665_MODULE_PIXEL_COL"))
      and not any(re.search(rf"^#ifndef\s+{n}\b", src, re.M) for n in
                  ("_22_1665_MODULE_ROWS", "_22_1665_MODULE_COLS",
                   "_22_1665_MODULE_PIXEL_ROW", "_22_1665_MODULE_PIXEL_COL")))
check("链首口径开关 `_22_1665_CHAIN_HEAD_IS_MODULE0` 仍带 `#ifndef` 守卫（现场 A/B 可 -D）",
      re.search(r"^#ifndef\s+_22_1665_CHAIN_HEAD_IS_MODULE0\b", src, re.M) is not None)
check("派生式与兄弟驱动同式：SCREEN_ROWS/COLS = 模块数 × 单模块像素",
      re.search(r"#define\s+_22_1665_SCREEN_ROWS\s+\(_22_1665_MODULE_ROWS \* _22_1665_MODULE_PIXEL_ROW\)", src)
      and re.search(r"#define\s+_22_1665_SCREEN_COLS\s+\(_22_1665_MODULE_COLS \* _22_1665_MODULE_PIXEL_COL\)", src))
check("链段/帧长派生式：SEGMENT_BITS = CHIPS×CHIP_BITS、FRAME_BITS = SEGMENT_BITS×模块总数、"
      "CHAIN_COUNT = LINE_COUNT×模块总数",
      re.search(r"#define\s+_22_1665_SEGMENT_BITS\s+\(_22_1665_CHIPS \* _22_1665_CHIP_BITS\)", src)
      and re.search(r"#define\s+_22_1665_FRAME_BITS\s+\(_22_1665_SEGMENT_BITS \* _22_1665_MODULE_TOTAL\)", src)
      and re.search(r"#define\s+_22_1665_CHAIN_COUNT\s+\(_22_1665_LINE_COUNT \* _22_1665_MODULE_TOTAL\)", src))
GROUP_MAX = 5  # = `_22_1665_GROUP_MAX` = HUB75_CHANNEL_MAX / 2（通道表 10 项 → 5 组）
LINE_ROWS = re.findall(r'\{"(\w+)", &g_hub75_pin_([rgb])\[(\d+)\], _22_1665_ROLE_(\w+), (\d)U\}', src)
EXPECT_ROWS = []
for _g in range(GROUP_MAX):
    EXPECT_ROWS += [(f"R{2*_g+1}", "r", str(2*_g), "RED", "0"), (f"G{2*_g+1}", "g", str(2*_g), "GREEN", "0"),
                    (f"R{2*_g+2}", "r", str(2*_g+1), "RED", "1"), (f"G{2*_g+2}", "g", str(2*_g+1), "GREEN", "1")]
check(f"数据线表 = 通道对 (2g, 2g+1) × {GROUP_MAX} 组（组 g 的 4 行 = 上半 R/G + 下半 R/G，"
      "前 MODULE_COLS 组在用；换模块数不改表）",
      LINE_ROWS == EXPECT_ROWS and MODULE_COLS <= GROUP_MAX, f"{len(LINE_ROWS)} 行")
check("组内 4 行的 role/half 模式恒 {RED,0}/{GREEN,0}/{RED,1}/{GREEN,1}（与组号无关）",
      all(line_rows[0][3:] == ("RED", "0") and line_rows[1][3:] == ("GREEN", "0")
          and line_rows[2][3:] == ("RED", "1") and line_rows[3][3:] == ("GREEN", "1")
          for line_rows in [LINE_ROWS[i:i + 4] for i in range(0, len(LINE_ROWS), 4)]))
check("每链段 128 位 = 8 片 × 16 位；链段数 = 4 × 模块数；每帧 = 128 × 模块数",
      (CHIPS, CHIP_BITS, SEGMENT_BITS) == (8, 16, 128) and CHAIN_COUNT == 4 * MODULES
      and FRAME_BITS == 128 * MODULES and LINE_COUNT == 4)
check("实例绑定：module_rows=PIXEL_ROW / modules_per_row=MODULE_ROWS / screen_rows=SCREEN_ROWS",
      all(p in src for p in (".module_rows         = _22_1665_MODULE_PIXEL_ROW",
                             ".module_cols         = _22_1665_MODULE_PIXEL_COL",
                             ".modules_per_row     = _22_1665_MODULE_ROWS",
                             ".modules_per_col     = _22_1665_MODULE_COLS",
                             ".screen_rows         = _22_1665_SCREEN_ROWS",
                             ".screen_cols         = _22_1665_SCREEN_COLS")))
check("注册/取用/ops 与兄弟驱动同构（hw_dev_initcall + _get + ops 表）",
      all(p in src for p in ("hw_dev_initcall(dev_display_22_1665_init)",
                             "dev_display_t *dev_display_22_1665_get(void)",
                             "static const dev_display_ops_t _22_1665_ops = {",
                             ".prepare = _22_1665_prepare", ".scan    = _22_1665_scan",
                             ".set_row = _22_1665_set_row")))
check("落点表/帧状态数组按派生尺寸声明（chain_dst[CHAIN_COUNT][SEGMENT_BITS]、"
      "frame_state[FRAME_BITS]、pixel_map[BUFFER_SIZE]）",
      "_22_1665_chain_dst[_22_1665_CHAIN_COUNT][_22_1665_SEGMENT_BITS]" in src
      and "_22_1665_frame_state[_22_1665_FRAME_BITS]" in src
      and "_22_1665_pixel_map[_22_1665_BUFFER_SIZE]" in src)
check("落点表建表按「模块 × 线」两重循环（不再要求手工补链表行）",
      "for (uint16_t m = 0; m < _22_1665_MODULE_TOTAL; m++)" in src
      and "_22_1665_chain_dst[m * _22_1665_LINE_COUNT + j]" in src)

# ================================================================
#  B. 落点模型（按目标口径复算）
# ================================================================
print("== B. 落点模型（双射 / 覆盖 / 级联时钟）==")
LINE_ROLE = ["RED", "GREEN", "RED", "GREEN"]
LINE_HALF = [0, 0, 1, 1]


def region_of(m: int, j: int) -> tuple[int, int, int, int]:
    return ((m % MODULE_ROWS) * PIX_ROW,
            (m // MODULE_ROWS) * PIX_COL + LINE_HALF[j] * HALF, PIX_ROW, HALF)


def offset(x0: int, y0: int, blk_cols: int, p: int) -> int:
    c, i = divmod(p, CHIP_BITS)
    bc = (blk_cols - 1) - (c % blk_cols)
    br = c // blk_cols
    dx = 3 - (i % 4)
    dy = ((i // 4) + 2) % 4
    return (y0 + BLK_H * br + dy) * SCREEN_ROWS + (x0 + BLK_W * bc + dx)


dst = {}


def chain_dst(m: int, j: int) -> list[int]:
    if (m, j) not in dst:
        x0, y0, w, _ = region_of(m, j)
        dst[(m, j)] = [offset(x0, y0, w // BLK_W, p) for p in range(SEGMENT_BITS)]
    return dst[(m, j)]


check(f"每链段 {SEGMENT_BITS} 链位 → {SEGMENT_BITS} 像素双射（不重复、不越屏）",
      all(len(set(chain_dst(m, j))) == SEGMENT_BITS
          and all(0 <= o < SCREEN_ROWS * SCREEN_COLS for o in chain_dst(m, j))
          for m in range(MODULES) for j in range(LINE_COUNT)))
def in_module(m: int, o: int) -> bool:
    x0, y0 = region_of(m, 0)[:2]
    return x0 <= o % SCREEN_ROWS < x0 + PIX_ROW and y0 <= o // SCREEN_ROWS < y0 + PIX_COL


check(f"每模块红链段（线 0/2）与绿链段（线 1/3）各覆盖该模块全部 {PIX_ROW*PIX_COL} 像素"
      "（不重不漏、不越出本模块区域）",
      all(len(set(chain_dst(m, 0)) | set(chain_dst(m, 2))) == PIX_ROW * PIX_COL
          and len(set(chain_dst(m, 1)) | set(chain_dst(m, 3))) == PIX_ROW * PIX_COL
          and all(in_module(m, o) for j in range(LINE_COUNT) for o in chain_dst(m, j))
          for m in range(MODULES)))
red = set().union(*[set(chain_dst(m, j)) for m in range(MODULES) for j in (0, 2)])
grn = set().union(*[set(chain_dst(m, j)) for m in range(MODULES) for j in (1, 3)])
check(f"全屏红链段覆盖全部 {SCREEN_ROWS*SCREEN_COLS} 像素、绿链段同样（= 双色全屏，像素不重不漏）",
      len(red) == len(grn) == SCREEN_ROWS * SCREEN_COLS, f"红 {len(red)} / 绿 {len(grn)}")
check("chain_dst 偏移 uint16 可容纳（像素数 ≤ 65535）", SCREEN_ROWS * SCREEN_COLS <= 65535)
check("模块区域按行主序排布且互不重叠（前 MODULE_ROWS 个模块同一行）",
      len({region_of(m, 0)[:2] for m in range(MODULES)}) == MODULES
      and {region_of(m, 0)[:2] for m in range(MODULES)}
      == {((m % MODULE_ROWS) * PIX_ROW, (m // MODULE_ROWS) * PIX_COL) for m in range(MODULES)})

# 模型 B（唯一形态）：每列一口——组 g（= 模块列 g）内，链上第 b 块（块 0 = 末 128 位 = 链首）
# 承载模块 m = g·MODULE_ROWS + local（CHAIN_HEAD=0 时 local = MODULE_ROWS-1-b）
clock_owner = []   # 状态数组逐字节（= 逐时钟位）的归属模块；长度 = FRAME_BITS
for _g in range(MODULE_COLS):
    for _b in range(MODULE_ROWS):
        _local = _b if CHAIN_HEAD else MODULE_ROWS - 1 - _b
        clock_owner += [_g * MODULE_ROWS + _local] * SEGMENT_BITS
_ok = len(clock_owner) == FRAME_BITS == CLOCKS * MODULE_COLS
for _g in range(MODULE_COLS):
    _seg = clock_owner[_g * CLOCKS:(_g + 1) * CLOCKS]
    _ok = _ok and sorted(_seg) == sorted([_g * MODULE_ROWS + _local
                                          for _local in range(MODULE_ROWS)
                                          for _ in range(SEGMENT_BITS)])
    _head = _g * MODULE_ROWS + (MODULE_ROWS - 1 if not CHAIN_HEAD else 0)
    _ok = _ok and all(x == _head for x in _seg[:SEGMENT_BITS])
check(f"时钟归属：{MODULE_COLS} 组 × 每帧 {CLOCKS} 个时钟，组内每模块恰占 "
      "128 位、每模块只出现一次、块 0（索引 0..127）= 链首 = 末 128 位",
      _ok)
check(f"每帧时钟数算式（128 × MODULE_ROWS）= {CLOCKS}，scan 循环上界 = _22_1665_CLOCKS",
      re.search(rf"#define\s+_22_1665_CLOCKS\s+\(_22_1665_SEGMENT_BITS \* _22_1665_MODULE_ROWS\)", src)
      and "for (uint16_t p = _22_1665_CLOCKS; p-- > 0;)" in src
      and "state[(uint32_t)g * _22_1665_CLOCKS + p]" in src
      and "每帧 128 × MODULE_ROWS 时钟" in src)

# ================================================================
#  C. CCMRAM 预算 + 基类字段一致性
# ================================================================
print("== C. CCMRAM 预算与基类字段 ==")
check(f"CCM 占用 = chain_dst {2*CHAIN_COUNT*SEGMENT_BITS} + pixel_map {SCREEN_ROWS*SCREEN_COLS}"
      f" + frame_state {FRAME_BITS} + bsrr "
      f"{8*GROUP_PORT_MAX*MODULE_COLS*(1<<LINE_COUNT)} = {CCM}B"
      f"（通式 1408·M + 256·MODULE_COLS）",
      CCM == 1408 * MODULES + 256 * MODULE_COLS,
      str(CCM))
check("**无编译期防御**（第三十四轮用户裁决）：0 条 `_Static_assert`、无 `#error`、无 error 属性",
      re.search(r"_Static_assert\s*\(", src) is None and "#error" not in src
      and "__attribute__((error" not in src)
check("仅断言使用的派生宏已清理（`_22_1665_CCM_BYTES` / `_22_1665_BSRR_SLOTS` 零残留）",
      "_22_1665_CCM_BYTES" not in src and "_22_1665_BSRR_SLOTS" not in src)
check("自设 16KB 预算与 64KB 物理守卫断言均已删除（无 `16384` / `budget exceeded` / "
      "`arrays exceed CCMRAM region` 旧消息残留）",
      "16384" not in src and "budget exceeded" not in src
      and "arrays exceed CCMRAM region" not in src)
check("物理契约降级为**注释**（文件头「物理约束（契约，非编译期守卫）」+ `MODULE_COLS ≤ 5` "
      "+ 像素/帧位 `≤ 65535` + 链接期兜底）",
      "物理约束（契约，非编译期守卫）" in src and "MODULE_COLS ≤ 5" in src
      and "65535" in src and "链接期" in src)
check("CCM 通式与实例值写在缓冲段注释里（`1408·M + 256·COLS`；1×1 = 1664B、8×4 = 46080B）",
      "1408·M + 256·COLS" in src and "1×1 = 1664B" in src and "8×4 = 46080B" in src)
check("buffer_size / scan_line_pixels / channel_pixels 与数组尺寸自洽"
      "（buffer_size = 像素数、scan_line_pixels = 每帧位数）",
      ".buffer_size         = _22_1665_BUFFER_SIZE," in src
      and ".scan_line_pixels    = _22_1665_FRAME_BITS," in src
      and ".hub75_buff          = _22_1665_frame_state," in src
      and ".pixel_map           = _22_1665_pixel_map," in src)

# ================================================================
#  D. 源码卫生
# ================================================================
print("== D. 源码卫生 ==")
LEFTOVERS = ["SEGGER_RTT", "[22_1665]", "_22_1665_RTT_DIAG", "_22_1665_PIN_SELFTEST",
             "_22_1665_CHAIN_PROBE", "_22_1665_MULTI_CHAIN", "_22_1665_DATA_LINES",
             "_22_1665_BLK_COLS", "_22_1665_SCREEN_W", "_22_1665_SCREEN_H",
             "_22_1665_XF_", "_22_1665_ROW", "PROBE8", "PROBE9", "probe8", "probe9",
             "pl_gpio.h", "selftest", "self-test",
             # 上一形态的残留（本轮删除的「手工补链表」）
             "_22_1665_chains", "_22_1665_chain_t", "_22_1665_CHAINS_PER_MODULE"]
for tok in LEFTOVERS:
    check(f"无残留：{tok}", tok not in src)
check("prepare 逐链掩码表达式正确（round21 缺陷已修复的源码级回归）",
      "s = (uint8_t)(s | (uint8_t)(1U << j));" in src
      and "& s_chain_sel" not in src and "role_bit[s_chain_role" not in src)
check("蓝分量策略已固化（单一角色表 = 蓝分量并入红绿，现场口径）",
      re.search(r"static const uint8_t _22_1665_role_bit\[2\]\[8\] = \{\s*"
                r"/\* RED   \*/ \{0U, 1U, 0U, 1U, 1U, 1U, 1U, 1U\},", src) is not None
      and src.count("_22_1665_role_bit[2][8]") == 1)
check("数据极性已固化（建表：状态位 1 → 置位；`on` 无开关异或）",
      "const bool on       = ((st >> j) & 1U) != 0U;" in src
      and "_22_1665_DATA_ACTIVE_HIGH" not in src)
check("接线模型开关已退休（`_22_1665_HUB_WIRING` 无 `#define`；无 `#if` 接线分支、无模型 A 函数）",
      re.search(r"#define\s+_22_1665_HUB_WIRING", src) is None
      and "_22_1665_flush_state" not in src and "_22_1665_PORT_MAX" not in src
      and src.count("_22_1665_flush_group") == 2 and src.count("_22_1665_scan(dev_display_t") == 1
      and src.count("_22_1665_build_bsrr_table(void)") == 1)
check("每列一口通道上限的**契约**（`MODULE_COLS ≤ 5`）与越限后果（越界读 `_22_1665_lines`）"
      "写在注释里（不再由编译期拦下）",
      "MODULE_COLS ≤ 5" in src and "越界读" in src and "_22_1665_lines`" in src)
check("历史轮次叙述零残留（注释里不再出现「第N轮」编号；历史移交 doc/01 附录 A）",
      re.search(r"第[一二三四五六七八九十百]+轮", src) is None)
check("状态数组按组分段（scan 读 state[组 × CLOCKS + 时钟位]）",
      "state[(uint32_t)g * _22_1665_CLOCKS + p]" in src
      and "_22_1665_MODULE_OF_GROUP" in src and "_22_1665_BLOCK_OF" in src)
check("只 include 基类与 initcall（无 RTT / 无 pl_gpio）",
      set(re.findall(r'#include "([^"]+)"', src)) == {"dev_display.h", "initcall.h"})
DOC01 = ROOT / "doc/01_显示系统/22-1665模组驱动分析与迁移记录.md"
_header = src[:src.index("*/") + 2]
check("文件头条数 = 简短说明 + 指针（doc/01 §0 as-built + `.analysis/22_1665/README.md`）",
      "doc/01_显示系统/22-1665模组驱动分析与迁移记录.md" in _header
      and ".analysis/22_1665/README.md" in _header)
check("详细说明与归档恢复路径在文档里（doc/01 含 finalize 归档 + prev_round 标定工具路径）",
      DOC01.exists()
      and "archive/finalize/driver_pre_finalize_20260917.c" in DOC01.read_text(encoding="utf-8")
      and "archive/prev_round/" in DOC01.read_text(encoding="utf-8"))
check("断言已清零（第三十四轮用户裁决）⇒ 不再有「ASCII 断言消息」约束；"
      "契约改由注释 + `doc/01` §0.3 承担",
      re.search(r"_Static_assert\s*\(", src) is None and "22_1665:" not in src)

# ================================================================
#  E. 文件规模
# ================================================================
print("== E. 文件规模 ==")
lines = src.splitlines()
head_end = next(i for i, l in enumerate(lines) if l.startswith("#include"))
check(f"文件头注释 ≤ 60 行（实际 {head_end} 行）", head_end <= 60)
check(f"整文件 ≤ 400 行（实际 {len(lines)} 行；round34 精简后 372 行 = 防线，防历史叙述回流）",
      len(lines) <= 400)

# ================================================================
#  F. 「口」字模（现场验收口径）
# ================================================================
print("== F. 现场验收口径 ==")
FONT_BIN = ("/home/yystation/文档/创迪科技生产订单与技术资料/19 标准产品资料/升降栏杆机/"
            "5.软件（含平台软件、测试软件与嵌入式程序/1.嵌入式程序/9I124D6521/"
            "字库芯片程序/W25Q256_FONT_14_16_20_24_32.bin")
p = pathlib.Path(FONT_BIN)
if MODULES == 1 and PIX_ROW == 16 and PIX_COL == 16 and p.exists():
    try:
        data = p.read_bytes()
        bpc = 16 * 2
        idx = (0xBF - 0x81) * 190 + (0xDA - 0x41)
        fonf = idx * bpc
        sec, page, byte = fonf // 4096, (fonf % 4096) // 256, (fonf % 4096) % 256
        a = (1218 + sec) * 4096 + (page + 8) * 256 + byte + 64
        buf = data[a:a + bpc]
        glyph = [[(buf[y * 2 + x // 8] >> (7 - x % 8)) & 1 for x in range(16)] for y in range(16)]
        total = sum(sum(r) for r in glyph)
        upper = sum(sum(r) for r in glyph[:8])
        check("「口」字模 52 点 = 上半 23 + 下半 29（2026-09-17 现场验收口径）",
              (total, upper, total - upper) == (52, 23, 29), f"{total} = {upper}+{total-upper}")
    except Exception as e:  # noqa: BLE001
        check(f"「口」字模读取（{e}）", False)
else:
    print("  [跳过] 「口」52 点口径（仅 1×1 / 16×16 口径适用，或字库 bin 不在本机）")

print()
if FAILURES:
    print("失败项：")
    for f in FAILURES:
        print(f"  - {f}")
print(f"结果：通过 {PASS} / 失败 {FAIL}")
sys.exit(1 if FAIL else 0)
