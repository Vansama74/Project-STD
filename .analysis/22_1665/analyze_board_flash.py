#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 第二十一轮：**板上 flash 实读分析**（判定「板上跑的是哪份镜像」）

数据来源：J-Link 只读 dump（`savebin /tmp/board_flash_r21.bin 0x08000000 0x100000`，
1MB 全片，含 Bootloader / Sector1 / Recovery / 主固件）。

分析项：
  ① Sector1（0x08004000）是否为空（出厂态）；
  ② 主固件里的产品码（PROGRAM_CODE）/ 横幅格式串 / 树哈希（tree=）；
  ③ 链表形态：重构后的「链名 blob（R1/G1/R2/G2 相邻）」vs 重构前的 8 字节行签名；
  ④ 驱动内 RTT / 探针判别串（**重构后应全部 0 命中**）→ 板上是不是重构前的旧驱动；
  ⑤ 协议集判别串（YN_OL「祝您一路平安」/ GZ_OL "TCLY" / 云南 / 山东 …）
     → 板上是 `make PROTO=ALL` 全协议构建还是 EIDE 编译集；
  ⑥ 默认画面判别串（「口」UTF-8 vs 旧欢迎语「欢迎行驶\n高速公路」）。

用法：python3 .analysis/22_1665/analyze_board_flash.py [dump路径]
"""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DUMP = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/board_flash_r21.bin")
FLASH_BASE = 0x08000000

if not DUMP.exists():
    raise SystemExit(f"dump 不存在：{DUMP}\n先跑 J-Link：savebin {DUMP} 0x08000000 0x100000")

data = DUMP.read_bytes()
print(f"=== 板上 flash 实读：{DUMP}（{len(data)} 字节 @ 0x{FLASH_BASE:08X}）===")


def at(addr: int, n: int) -> bytes:
    off = addr - FLASH_BASE
    return data[off:off + n]


def find_all(needle: bytes, lo: int = 0, hi: int | None = None) -> list[int]:
    out = []
    start = 0
    hi = len(data) if hi is None else hi
    while True:
        i = data.find(needle, start, hi)
        if i < 0:
            return out
        if i >= lo:
            out.append(i)
        start = i + 1


def cstr_at(addr: int, n: int = 96) -> str:
    b = at(addr, n)
    z = b.find(b"\x00")
    return b[:z if z >= 0 else n].decode("utf-8", "replace")


print()
print("== ① Sector1（0x08004000，板级配置）==")
s1 = at(0x08004000, 0x4000)
blank = all(b == 0xFF for b in s1)
print(f"  Sector1 前 32B：{s1[:32].hex(' ')}")
print(f"  全 0xFF（出厂/已擦除态）？ {'是' if blank else '否 —— 含配置记录'}")
if not blank:
    magic = int.from_bytes(s1[0:4], "little")
    print(f"  magic=0x{magic:08X}（0xA5A5A5A5 之类才有效）")

print()
print("== ② 主固件身份（产品码 / 横幅 / 树哈希）==")
main = at(0x08040000, len(data) - 0x40000)
for pat in ("9K13A127E0", "9K10212482"):
    n = main.count(pat.encode())
    print(f"  产品码 {pat:<12} 出现 {n} 次")
m = re.search(rb"\[diag\] fw=%s built=%s %s tree=%s", main)
print(f"  [diag] 横幅格式串：{'存在' if m else '不存在'}")
for h in ("cf772faa",):
    print(f"  已知树哈希 {h}：出现 {main.count(h.encode())} 次")
trees = sorted({x.decode() for x in re.findall(rb"\b[0-9a-f]{8}\b", main)
                if x.decode() in ("cf772faa",)})
# 全量列出 ≥ 6 次出现的 8 位十六进制串（排除常见噪声后人工看）
print("  镜像里 8 位十六进制串候选（前 20）：")
cnt: dict[str, int] = {}
for x in re.findall(rb"\b[0-9a-f]{8}\b", main):
    s = x.decode()
    cnt[s] = cnt.get(s, 0) + 1
for s, c in sorted(cnt.items(), key=lambda kv: -kv[1])[:20]:
    print(f"     {s}  ×{c}")

print()
print("== ③ 链表形态判别（重构后 vs 重构前）==")
# 重构后（第二十三轮起）：链表 = {const char *name; const hub75_pin_t *line; uint8_t role;}[4]
#   rodata 里四个链名相邻（"R1\0 G1\0 R2\0 G2\0"）——对齐填充不敏感：窗口内按序出现即可
i, blob = data.find(b"R1\x00"), False
while i >= 0:
    win = data[i:i + 20]
    if b"G1\x00" in win and b"R2\x00" in win and b"G2\x00" in win:
        blob = True
        print(f"  有  重构后链名 blob @ 0x{FLASH_BASE + i:08X}（R1/G1/R2/G2 相邻）")
        break
    i = data.find(b"R1\x00", i + 1)
if not blob:
    print("  无  重构后链名 blob（→ 板上是重构前的旧驱动或别的模组）")

# 重构前的旧行签名（8 字节：role/区域字段 + 0 填充），用于区分「重构前多链表」
SIGS = {
    "旧表 chain1 (G1/GREEN/上半)": bytes.fromhex("0200010000100800"),
    "旧表 chain2 (R2/RED/下半)  ": bytes.fromhex("0800000008100800"),
    "旧表 chain3 (G2/GREEN/下半)": bytes.fromhex("1000010008100800"),
    "更旧表 chain1 (A/GREEN)    ": bytes.fromhex("4000010000100800"),
}
for name, sig in SIGS.items():
    hits = find_all(sig)
    locs = " ".join(f"0x{FLASH_BASE + i:08X}" for i in hits[:4])
    print(f"  {name}：{len(hits)} 处  {locs}")

print()
print("== ④ 驱动内 RTT / 探针判别串（重构后应**全部为 0**）==")
for s in ("[22_1665] multichain=%u chains=%u screen=%ux%u frame_bits=%u",
          "[22_1665] chain%u %-3s lines=0x%03X role=%s region=(%u,%u,%u,%u) xf=0x%X dst=%s",
          "[22_1665] pin self-test: out_lines=0x%03X lines=%u",
          "[22_1665] probe8 dwell: pin=%s (P%c%u) k=%u",
          "[22_1665] probe9: chain=%u %-3s role=%s pin=%s (P%c%u) k=%u",
          "[22_1665] out_lines=0x%03X ports=%u states=%u"):
    n = main.count(s.encode())
    print(f"  {'有（旧驱动/探针口径！）' if n else '无  ✔'}  ×{n}  {s[:60]}")

print("== ⑤ 协议集判别串（make PROTO=ALL 全协议 vs EIDE 编译集）==")
PROBES = {
    "YN_OL 上电画面「祝您一路平安」": "祝您一路平安".encode(),
    "YN_OL 帧解析标记 `[yn_ol]`": b"[yn_ol]",
    "GZ_OL 帧头 \"TCLY\"": b"TCLY",
    "GZ_OL 诊断 `[gz_ol]`": b"[gz_ol]",
    "CQ 协议串 `[cq]`": b"[cq]",
    "山东 `[sd]`": b"[sd]",
    "云南常规 `[yn]`": b"[yn]",
    "青海 `[qh]`": b"[qh]",
}
for name, needle in PROBES.items():
    n = main.count(needle)
    print(f"  {'有' if n else '无'}  ×{n}  {name}")

print()
print("== ⑥ 默认画面判别串 ==")
for name, needle in {"「口」UTF-8": "口".encode(),
                     "旧欢迎语「欢迎行驶」": "欢迎行驶".encode(),
                     "「高速公路」": "高速公路".encode()}.items():
    n = main.count(needle)
    print(f"  {'有' if n else '无'}  ×{n}  {name}")

print()
print("== ⑦ 当前源码树的树哈希（Makefile 规则复算，供与板上 tree= 对照）==")
cmd = ("{ find Application Device Kernel Platform Core Compiler -type f \\( -name '*.c' -o -name '*.h' "
       "-o -name '*.ld' \\) -print0 | LC_ALL=C sort -z | xargs -0 cat; cat Makefile; } 2>/dev/null "
       "| md5sum | cut -c1-8")
out = subprocess.run(["bash", "-c", cmd], cwd=ROOT, capture_output=True, text=True)
print(f"  当前树哈希 = {out.stdout.strip()}   （现行重构后 make 产物树哈希见 doc/01 §0.7）")
