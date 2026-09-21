#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 第二十一轮宿主自检：**「新链表烧录后行为未变」诊断**

  A. **源码口径**：从 `dev_display_22_1665.c` 实读链表四行 + 关键宏（表 = 唯一标定块）。
  B. **产物口径（决定性证据）**：用 nm 取 `_22_1665_chains` 符号地址，从各 ELF / Intel HEX
     里**逐字段解码 64 字节链表**（名字指针、掩码、角色、区域、flags、dst），与两张已知表
     （新表 R1/G1/R2/G2、旧表 R1/A/B/C）对照 → 直接回答「这份可烧录产物装的是哪张表」。
  C. **端到端流水线复算**：新表下模拟 prepare（角色取分量）→ 合并写 BSRR 表 → scan 逐时钟位，
     统计**每根数据脚被拉高的时钟位数** + 逐位 BSRR 序列 + 屏上可见点集（现场导通性过滤）。
  D. **旧表对照**：用第十七~十九轮旧表（R1/A/B/C）复算，验证它**恰好逐字复现**现场两条观测
     （红「口」= 上半屏 23 点 / 绿「口」= 全黑）——即「旧表 + 现场导通性」的唯一签名。

用法：python3 .analysis/22_1665/check_round21.py
"""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
# 归档说明：本脚本断言的是**重构前**的驱动形态，故固定读归档的驱动备份
SRC = (ROOT / ".analysis/22_1665/archive/prev_round/"
       "dev_display_22_1665_round23_pre_refactor.c")

PASS = 0
FAIL = 0


def check(desc: str, cond: bool, extra: str = "") -> None:
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [通过] {desc} {extra}".rstrip())
    else:
        FAIL += 1
        print(f"  [失败] {desc} {extra}")


src = SRC.read_text(encoding="utf-8")

# ---------------------------------------------------------------- 常量与模型
LINE_ORDER = ["R1", "G1", "B1", "R2", "G2", "B2", "A", "B", "C", "D"]
LINE_MASK = {n: (1 << k) for k, n in enumerate(LINE_ORDER)}
ROLE_RED, ROLE_GREEN, ROLE_MONO = 0, 1, 2
XF_X, XF_Y, XF_BLK_X, XF_BLK_Y = 0x1, 0x2, 0x4, 0x8
XF_ROT180 = XF_X | XF_Y | XF_BLK_X | XF_BLK_Y

# 脚 → (端口, 位号)：R1 PG9 / G1 PG10 / B1 PG12 / R2 PG15 / G2 PB6 / B2 PB7 / A..D PD4..7
PIN_PORT_BIT = {
    "R1": ("G", 9), "G1": ("G", 10), "B1": ("G", 12), "R2": ("G", 15),
    "G2": ("B", 6), "B2": ("B", 7), "A": ("D", 4), "B": ("D", 5), "C": ("D", 6), "D": ("D", 7),
}

SCREEN_W, SCREEN_H = 16, 16
CHIPS, CHIP_BITS = 8, 16
FRAME_BITS = CHIPS * CHIP_BITS
BLK_W = BLK_H = 4
# 现场探针 8 逐脚定标（第二十轮）：真导通的数据脚（10 位掩码）
HW_WORKING = LINE_MASK["R1"] | LINE_MASK["G1"] | LINE_MASK["R2"] | LINE_MASK["G2"]

# 颜色索引：黑0 红1 绿2 黄3 蓝4 紫5 青6 白7；BLUE_AS_LIT=1 口径
ROLE_BIT_LIT = {
    ROLE_RED:   [0, 1, 0, 1, 1, 1, 1, 1],
    ROLE_GREEN: [0, 0, 1, 1, 1, 1, 1, 1],
}
COLOR_OF = {"BLACK": 0, "RED": 1, "GREEN": 2, "YELLOW": 3, "BLUE": 4,
            "PURPLE": 5, "CYAN": 6, "WHITE": 7}

# 「口」FONT_16 黑体字模（第十二轮 dump 的锚：52 点，上半 23 / 下半 29）
GLYPH = """
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
GLYPH_PX = {(x, y) for y, row in enumerate(GLYPH) for x, ch in enumerate(row) if ch == "#"}


# ---------------------------------------------------------------- 源码链表解析
def macro(name: str) -> int:
    m = re.search(rf"^#define\s+{re.escape(name)}\s+\(?\s*(0[xX][0-9a-fA-F]+|\d+)U?\)?", src, re.M)
    if not m:
        raise SystemExit(f"宏 {name} 未找到")
    return int(m.group(1), 0)


MULTI = macro("_22_1665_MULTI_CHAIN")
PROBE = macro("_22_1665_CHAIN_PROBE")
CHAIN_COUNT_SRC = macro("_22_1665_CHAIN_COUNT")

SYMS = {"_22_1665_LINE_ALL": 0x3FF, "_22_1665_ADDR_LINES": 0x3C0, "_22_1665_RGB_LINES": 0x03F,
        "_22_1665_SCREEN_W": SCREEN_W, "_22_1665_SCREEN_H": SCREEN_H, "_22_1665_HALF_H": 8}
SYMS.update({f"_22_1665_LINE_{n}": LINE_MASK[n] for n in LINE_ORDER})


def parse_chains() -> list[dict]:
    """解析 `_22_1665_chains[]` 的多链分支（`#if _22_1665_MULTI_CHAIN` … `#else` 之间）。

    `_22_1665_ROW(名, 掩码, 角色, x, y, w, h)` = 7 参；`_22_1665_ROW_XF(..., 旗标)` = 8 参。
    """
    start = src.index("static const _22_1665_chain_t _22_1665_chains")
    body = src[start:]
    body = body[:body.index("#else")]   # 兼容模式分支起点
    out = []
    for m in re.finditer(r"_22_1665_ROW(_XF|_DST)?\s*\((.*?)\)\s*,", body, re.S):
        kind = m.group(1)
        parts = [p.strip() for p in m.group(2).split(",")]
        if len(parts) not in (7, 8):
            continue
        nm, lmask, role, x0, y0, w, h = parts[:7]
        xf = parts[7] if len(parts) == 8 else "_22_1665_XF_NONE"
        flags = 0
        for tok, v in (("_22_1665_XF_ROT180", XF_ROT180), ("_22_1665_XF_BLK_X", XF_BLK_X),
                       ("_22_1665_XF_BLK_Y", XF_BLK_Y), ("_22_1665_XF_X", XF_X),
                       ("_22_1665_XF_Y", XF_Y)):
            if tok in xf:
                flags |= v
        role_v = ROLE_RED if "ROLE_RED" in role else (ROLE_GREEN if "ROLE_GREEN" in role
                                                      else (ROLE_MONO if "ROLE_MONO" in role else -1))

        def val(tok: str, default: int) -> int:
            tok = tok.strip()
            if tok in SYMS:
                return SYMS[tok]
            m = re.match(r"^(0[xX][0-9a-fA-F]+|\d+)U?$", tok)
            return int(m.group(1), 0) if m else default

        out.append(dict(name=nm.strip('"'), line_mask=val(lmask, -1), role=role_v,
                        x0=val(x0, 0), y0=val(y0, 0), w=val(w, 16), h=val(h, 8), flags=flags))
    return out


SRC_CHAINS = parse_chains()

print("== A. 源码口径（唯一标定块）==")
print(f"  MULTI_CHAIN={MULTI} CHAIN_PROBE={PROBE} CHAIN_COUNT={CHAIN_COUNT_SRC}")
for c in SRC_CHAINS:
    print(f"  {c['name']}: mask=0x{c['line_mask']:03X} role={'RED' if c['role'] == 0 else 'GREEN'}"
          f" region=({c['x0']},{c['y0']},{c['w']},{c['h']}) flags=0x{c['flags']:X}")
check("源码链表 = 4 行", len(SRC_CHAINS) == 4, f"{len(SRC_CHAINS)} 行")
check("源码链表 = R1/G1/R2/G2（第二十轮定标值）",
      [c["line_mask"] for c in SRC_CHAINS] == [0x001, 0x002, 0x008, 0x010],
      str([hex(c["line_mask"]) for c in SRC_CHAINS]))
check("源码链表角色 = RED/GREEN/RED/GREEN", [c["role"] for c in SRC_CHAINS] == [0, 1, 0, 1])
check("源码链表区域 = 上半/上半/下半/下半",
      [(c["x0"], c["y0"], c["w"], c["h"]) for c in SRC_CHAINS]
      == [(0, 0, 16, 8), (0, 0, 16, 8), (0, 8, 16, 8), (0, 8, 16, 8)])
check("源码 flags 全恒等（0）", all(c["flags"] == 0 for c in SRC_CHAINS))


# ---------------------------------------------------------------- ELF / HEX 读取
def run(cmd: list[str]) -> str:
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def elf_sections(path: str) -> list[tuple[str, int, int, int]]:
    out = run(["arm-none-eabi-objdump", "-h", path])
    secs = []
    for line in out.splitlines():
        m = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)",
                     line)
        if m:
            secs.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16), int(m.group(5), 16)))
    return secs


def elf_read(path: str, secs, addr: int, n: int) -> bytes | None:
    for _name, size, vaddr, off in secs:
        if vaddr and size and vaddr <= addr and addr + n <= vaddr + size:
            with open(path, "rb") as f:
                f.seek(off + (addr - vaddr))
                return f.read(n)
    return None


def elf_symbol(path: str, sym: str) -> tuple[int, int] | None:
    out = run(["arm-none-eabi-nm", "-S", "--defined-only", path])
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[3] == sym:
            return int(parts[0], 16), int(parts[1], 16)
    return None


def hex_mem(path: str) -> dict[int, int]:
    mem: dict[int, int] = {}
    base = 0
    with open(path, "r", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line.startswith(":"):
                continue
            raw = bytes.fromhex(line[1:])
            if (sum(raw) & 0xFF) != 0:
                continue
            ln, off, typ = raw[0], (raw[1] << 8) | raw[2], raw[3]
            data = raw[4:4 + ln]
            if typ == 0x00:
                for i, b in enumerate(data):
                    mem[base + off + i] = b
            elif typ == 0x02:
                base = ((data[0] << 8) | data[1]) << 4
            elif typ == 0x04:
                base = ((data[0] << 8) | data[1]) << 16
    return mem


def decode_table_blob(blob: bytes, peeker) -> list[dict]:
    """64 字节链表逐字段解码（16B/行：name(4) mask(2) role(1) x0 y0 w h flags(1) pad(0) dst(4)）。"""
    rows = []
    for i in range(4):
        b = blob[i * 16:(i + 1) * 16]
        if len(b) < 16:            # 兼容口径只有 1 行（16B）
            break
        ptr = int.from_bytes(b[0:4], "little")
        nm = peeker(ptr, 4) if peeker else None
        rows.append(dict(
            name=(nm.split(b"\x00")[0].decode("ascii", "replace") if nm else f"C{i}"),
            line_mask=int.from_bytes(b[4:6], "little"), role=b[6], x0=b[7], y0=b[8],
            w=b[9], h=b[10], flags=b[11], dst=int.from_bytes(b[12:16], "little")))
    return rows


def table_signature(rows: list[dict]) -> str:
    return " | ".join(f"{r['name']}:0x{r['line_mask']:03X}/{r['role']}/"
                      f"({r['x0']},{r['y0']},{r['w']},{r['h']})/xf=0x{r['flags']:X}" for r in rows)


NEW_TABLE_SIG = ("C0:0x001/0/(0,0,16,8)/xf=0x0 | C1:0x002/1/(0,0,16,8)/xf=0x0 | "
                 "C2:0x008/0/(0,8,16,8)/xf=0x0 | C3:0x010/1/(0,8,16,8)/xf=0x0")
OLD_TABLE_SIG = ("C0:0x001/0/(0,0,16,8)/xf=0x0 | C1:0x040/1/(0,0,16,8)/xf=0x0 | "
                 "C2:0x080/0/(0,8,16,8)/xf=0x0 | C3:0x100/1/(0,8,16,8)/xf=0x0")
OLD2_TABLE_SIG = ("C0:0x001/0/(0,0,16,8)/xf=0x0 | C1:0x040/1/(0,0,16,8)/xf=0x0 | "
                  "C2:0x008/0/(0,8,16,8)/xf=0x0 | C3:0x010/1/(0,8,16,8)/xf=0x0")

ARTIFACTS = [
    ("build/Debug/Project_STD.elf", "EIDE 现行产物（15:46 全量重建；EIDE 点烧录写它）"),
    ("build/Debug/Project_STD.hex", "EIDE 现行 hex（commands.jlink 的 loadfile 目标）"),
    ("/tmp/r20.elf", "第二十轮 make 产物留档（md5 f9ef09c4…，应为新表）"),
    ("/tmp/22_1665_probe8.elf", "探针 8 口径产物（留档）"),
    ("/tmp/22_1665_probe9.elf", "探针 9 口径产物（留档）"),
    ("/tmp/22_1665_compat.elf", "兼容口径产物（留档）"),
    (".analysis/9k23881580/artifacts_1754_eide/Project_STD.elf", "更早轮次的 EIDE 留档产物"),
]

print()
print("== B. 可烧录产物里装的是哪张表（决定性证据）==")
results: dict[str, str] = {}
for rel, note in ARTIFACTS:
    p = pathlib.Path(rel) if rel.startswith("/") else ROOT / rel
    if not p.exists():
        print(f"  -- {rel}：不存在，跳过")
        continue
    try:
        if p.suffix == ".hex":
            sib = p.with_suffix(".elf")
            sym = elf_symbol(str(sib), "_22_1665_chains") if sib.exists() else None
            addr = sym[0] if sym else 0x0806CFE8
            mem = hex_mem(str(p))
            if not all((addr + i) in mem for i in range(64)):
                print(f"  -- {rel}：hex 内 0x{addr:08X} 处无数据，跳过")
                continue
            blob = bytes(mem[addr + i] for i in range(64))
            rows = decode_table_blob(blob, None)
        else:
            sym = elf_symbol(str(p), "_22_1665_chains")
            if sym is None:
                print(f"  -- {rel}：无 `_22_1665_chains` 符号（该产物未编入本驱动），跳过")
                continue
            addr, size = sym
            secs = elf_sections(str(p))
            blob = elf_read(str(p), secs, addr, size)
            if blob is None:
                print(f"  -- {rel}：0x{addr:08X} 处读取失败，跳过")
                continue

            def peeker(a: int, n: int, _p=str(p), _s=secs):
                return elf_read(_p, _s, a, n)

            rows = decode_table_blob(blob, peeker)
        sig = table_signature(rows)
        results[rel] = sig
        verdict = ("✔ 新表（第二十轮 R1/G1/R2/G2）" if sig == NEW_TABLE_SIG
                   else "✘ 旧表（第十七~十九轮 R1/A/B/C）" if sig == OLD_TABLE_SIG
                   else "△ 第三张表（混合）" if sig == OLD2_TABLE_SIG
                   else "? 其它/未识别")
        print(f"  {rel}  [{note}]")
        print(f"      {sig}")
        print(f"      → {verdict}")
    except Exception as e:  # noqa: BLE001
        print(f"  -- {rel}：读取失败 {e}")


# ---------------------------------------------------------------- 端到端流水线复算
def region_offset(x0, y0, blk_cols, blk_rows, flags, p) -> int:
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


def chain_dst(c: dict) -> list[int]:
    return [region_offset(c["x0"], c["y0"], c["w"] // 4, c["h"] // 4, c["flags"], p)
            for p in range(FRAME_BITS)]


def lit_bits(c: dict, content: str) -> list[int]:
    """该链 128 位里哪些位为 1（按角色取像素分量；非字模像素 = 黑）。"""
    out = []
    for o in chain_dst(c):
        x, y = o % SCREEN_W, o // SCREEN_W
        col = COLOR_OF[content] if (x, y) in GLYPH_PX else COLOR_OF["BLACK"]
        out.append(ROLE_BIT_LIT[c["role"]][col])
    return out


def pipeline(chains: list[dict], content: str, buggy: bool = False):
    """prepare → 合并写 BSRR 表 → scan；返回 (每脚被拉高的位数, 表, 逐位轨迹, 端口, 帧状态)。

    `buggy=True` = 复算第二十一轮修复前的错误语义（`role_bit & (1<<i)`）——用于把现场
    两条症状「算出来」作为反证；`buggy=False` = 修复后的正确语义（`role_bit ? 1<<i : 0`）。
    """
    frame = []
    for p in range(FRAME_BITS):
        st = 0
        for i, c in enumerate(chains):
            bit = lit_bits(c, content)[p]
            if buggy:
                st |= (bit & (1 << i))          # ← 原错误写法：1 & 2/4/8 = 0，链 1..3 永远被掩掉
            else:
                st |= (bit << i) if bit else 0  # ← 修复后：该链该位点亮 → 置状态字节第 i 位
        frame.append(st)

    out_lines = 0
    for c in chains:
        out_lines |= c["line_mask"]
    ports: list[str] = []
    for k, nm in enumerate(LINE_ORDER):
        if (out_lines >> k) & 1 and PIN_PORT_BIT[nm][0] not in ports:
            ports.append(PIN_PORT_BIT[nm][0])

    table = {}
    for st in range(1 << len(chains)):
        st_bits = 0
        for i, c in enumerate(chains):
            if (st >> i) & 1:
                st_bits |= c["line_mask"]
        vals: dict[str, list[int]] = {p: [0, 0] for p in ports}
        for k, nm in enumerate(LINE_ORDER):
            if not (out_lines >> k) & 1:
                continue
            port, bit = PIN_PORT_BIT[nm]
            if (st_bits >> k) & 1:
                vals[port][0] |= 1 << bit
            else:
                vals[port][1] |= 1 << bit
        table[st] = {p: (v[0], v[1]) for p, v in vals.items()}

    high = {k: 0 for k in range(10)}
    trace = []
    for p in range(FRAME_BITS - 1, -1, -1):
        st = frame[p]
        for port, (s, _clr) in table[st].items():
            for k, nm in enumerate(LINE_ORDER):
                if (out_lines >> k) & 1 and PIN_PORT_BIT[nm][0] == port \
                        and (s >> PIN_PORT_BIT[nm][1]) & 1:
                    high[k] += 1
        if len(trace) < 8:
            trace.append((p, st, table[st]))
    return high, table, trace, ports, frame


def visible_pixels(chains: list[dict], working: int, content: str) -> set[tuple[int, int]]:
    """按「现场哪些脚真导通」算出屏上可见点集（单脚链；多脚 OR 场景不涉及）。"""
    vis: set[tuple[int, int]] = set()
    for c in chains:
        if (c["line_mask"] & working) == 0:
            continue
        for o, b in zip(chain_dst(c), lit_bits(c, content)):
            if b:
                vis.add((o % SCREEN_W, o // SCREEN_W))
    return vis


NEW_CHAINS = [dict(name=n, line_mask=m, role=r, x0=0, y0=y0, w=16, h=8, flags=0) for
              n, m, r, y0 in (("C0", 0x001, ROLE_RED, 0), ("C1", 0x002, ROLE_GREEN, 0),
                              ("C2", 0x008, ROLE_RED, 8), ("C3", 0x010, ROLE_GREEN, 8))]
OLD_CHAINS = [dict(name=n, line_mask=m, role=r, x0=0, y0=y0, w=16, h=8, flags=0) for
              n, m, r, y0 in (("C0", 0x001, ROLE_RED, 0), ("C1", 0x040, ROLE_GREEN, 0),
                              ("C2", 0x080, ROLE_RED, 8), ("C3", 0x100, ROLE_GREEN, 8))]

print()
print("== C. 新表（R1/G1/R2/G2）端到端复算：prepare → 合并 BSRR → scan ==")
for content in ("RED", "GREEN"):
    high, table, trace, ports, frame = pipeline(NEW_CHAINS, content)
    driven = {LINE_ORDER[k]: v for k, v in high.items() if v}
    vis = visible_pixels(NEW_CHAINS, HW_WORKING, content)
    print(f"  ── 内容 = {content}「口」（52 点：上半 23 / 下半 29）；端口槽 = {ports}")
    print(f"     每根脚被拉高的时钟位数：" + " ".join(f"{n}={v}" for n, v in driven.items()))
    print(f"     屏上可见点集（现场导通性过滤后）= {len(vis)} 点"
          f"（上半 {sum(1 for _, y in vis if y < 8)} / 下半 {sum(1 for _, y in vis if y >= 8)}）")
    print("     逐位 BSRR 轨迹（p=127 起前 8 个时钟位；val 拆成 BSRR 的 set / clear 两段）：")
    for p, st, vals in trace:
        segs = " ".join(f"P{port}:(set=0x{s:04X},clr=0x{c:04X})" for port, (s, c) in vals.items())
        print(f"       p={p:3d} st={st:2d}(0b{st:04b}) {segs}")
    if content == "RED":
        check("新表 · 红「口」→ R1 被拉高 23 位（上半 23 点）", high[0] == 23, f"{high[0]}")
        check("新表 · 红「口」→ R2 被拉高 29 位（下半 29 点）★关键：下半屏必须亮★",
              high[3] == 29, f"{high[3]}")
        check("新表 · 红「口」→ G1/G2 一位都不拉高（分量隔离）", high[1] == 0 and high[4] == 0)
        check("新表 · 红「口」→ 可见 = 整幅 52 点（上半 23 + 下半 29）", len(vis) == 52, f"{len(vis)}")
    else:
        check("新表 · 绿「口」→ G1 23 位 / G2 29 位★关键：绿「口」必须整幅可见★",
              high[1] == 23 and high[4] == 29, f"G1={high[1]} G2={high[4]}")
        check("新表 · 绿「口」→ R1/R2 一位都不拉高", high[0] == 0 and high[3] == 0)
        check("新表 · 绿「口」→ 可见 = 整幅 52 点", len(vis) == 52, f"{len(vis)}")

_, _, _, ports_new, _ = pipeline(NEW_CHAINS, "RED")
check("新表：参与脚 R1/G1/R2（GPIOG）+ G2（GPIOB）→ 2 个端口槽 → 每时钟位 2 次 BSRR 写",
      ports_new == ["G", "B"], str(ports_new))

print()
print("== C2. 修复前后语义差分（现场两条症状的反证 / 修复回归）==")
check("源码已修复：`prepare` 不再出现原错误表达式 "
      "`(_22_1665_role_bit[s_chain_role[i]][c] & s_chain_sel[i])`（按位与写法）",
      "(_22_1665_role_bit[s_chain_role[i]][c] & s_chain_sel[i])" not in src,
      "（源码级回归断言：宿主模型按意图语义建模，只有这条能拦住此缺陷）")
check("源码已修复：改为「点亮 → 置第 i 位」（`if (... != 0U) s |= s_chain_sel[i];`）",
      "if (_22_1665_role_bit[s_chain_role[i]][c] != 0U)" in src
      and "s = (uint8_t)(s | s_chain_sel[i]);" in src)
for content in ("RED", "GREEN"):
    hb, _, _, _, fb = pipeline(NEW_CHAINS, content, buggy=True)
    hf, _, _, _, ff = pipeline(NEW_CHAINS, content, buggy=False)
    visb = visible_pixels(NEW_CHAINS, HW_WORKING, content) if False else None
    print(f"  ── 内容 = {content}「口」")
    print(f"     修复前（& 掩码）：帧状态值域={sorted(set(fb))} "
          f"每脚位数 " + " ".join(f"{LINE_ORDER[k]}={v}" for k, v in hb.items() if v))
    print(f"     修复后（置位  ）：帧状态值域={sorted(set(ff))} "
          f"每脚位数 " + " ".join(f"{LINE_ORDER[k]}={v}" for k, v in hf.items() if v))
    if content == "RED":
        check("修复前 · 红「口」：只有 R1 23 位、R2 = 0 位 ⇒ **现场症状①（只亮上半屏）逐字复现**",
              hb[0] == 23 and hb[3] == 0, f"R1={hb[0]} R2={hb[3]}")
        check("修复后 · 红「口」：R1 23 位 + R2 29 位 ⇒ 整幅 52 点（本次修复的验收判据）",
              hf[0] == 23 and hf[3] == 29, f"R1={hf[0]} R2={hf[3]}")
    else:
        check("修复前 · 绿「口」：全 0 位（链 0 = RED 取绿分量 = 0，绿链被掩掉）⇒ "
              "**现场症状②（绿口全黑）逐字复现**",
              all(v == 0 for v in hb.values()), str({LINE_ORDER[k]: v for k, v in hb.items() if v}))
        check("修复后 · 绿「口」：G1 23 位 + G2 29 位 ⇒ 整幅 52 点", hf[1] == 23 and hf[4] == 29,
              f"G1={hf[1]} G2={hf[4]}")

print()
print("== D. 旧表（R1/A/B/C，第十七~十九轮口径）对照复算（现场导通性 = 只有 R1/G1/R2/G2）==")
for content in ("RED", "GREEN"):
    high, table, trace, ports, frame = pipeline(OLD_CHAINS, content)
    driven = {LINE_ORDER[k]: v for k, v in high.items() if v}
    vis = visible_pixels(OLD_CHAINS, HW_WORKING, content)
    print(f"  ── 内容 = {content}「口」：逻辑上被拉高的脚 = "
          + " ".join(f"{n}={v}位" for n, v in driven.items() if v))
    print(f"     现场导通性过滤后可见 = {len(vis)} 点"
          f"（上半 {sum(1 for _, y in vis if y < 8)} / 下半 {sum(1 for _, y in vis if y >= 8)}）")
    if content == "RED":
        check("旧表 · 红「口」：逻辑上 R1=23 位、B=29 位（B 现场不导通）",
              high[0] == 23 and high[7] == 29, f"R1={high[0]} B={high[7]}")
        check("旧表 · 红「口」现场可见 = **上半屏 23 点、y≥8 全黑** = 观测①逐字",
              vis == {(x, y) for (x, y) in GLYPH_PX if y < 8} and len(vis) == 23, f"{len(vis)} 点")
    else:
        check("旧表 · 绿「口」：逻辑上 A（k=6）=23 位、C（k=8）=29 位（现场均不导通）",
              high[6] == 23 and high[8] == 29, f"A={high[6]} C={high[8]}")
        check("旧表 · 绿「口」现场可见 = **0 点（全黑）** = 观测②逐字", len(vis) == 0, f"{len(vis)} 点")

print()
print("== E. 结论 ==")
print("  · 源码（修复后）链路表 = R1/G1/R2/G2；`prepare` 逐链置位语义正确。")
print("  · 现场两条症状（红口只上半屏 23 点 / 绿口全黑）与「修复前错误语义」的复算**逐字一致**；")
print("    该错误语义把链 1..3 全部掩掉（1&2 = 1&4 = 1&8 = 0）⇒ 只有链 0（R1）能亮，")
print("    **与链表怎么标定无关** —— 这正是「改链表行为不变」的机理。")
print("  · 修复验收判据：红「口」R1 23 位 + R2 29 位；绿「口」G1 23 位 + G2 29 位。")
print("  · 修复后构建产物请以 RTT 横幅 `tree=` + 段尺寸核对（本轮 tree=b8f6e59a）。")

print()
print(f"== 结果：{PASS} 通过 / {FAIL} 失败 ==")
sys.exit(1 if FAIL else 0)
