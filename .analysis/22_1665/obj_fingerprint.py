#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""驱动对象「不改动证明」指纹工具（第三十四轮新增）。

对给定的 `.o` 打印可逐行比对的指纹（同一源码路径两次编译 ⇒ 输出应完全一致）：

  · `objdump -h` 各 section 的序号 / 名 / 大小 / 对齐；
  · 每个 section 的**内容 md5**（`objcopy -O binary --only-section`，与第三十/三十二轮
    A/B 同一口径；`.debug*` 与符号表段按工具链记账排除）；
  · `objdump -d` 反汇编（滤掉 `file format` 行）**整体 md5 + 行数**；
  · 整个 `.o` 文件的 md5 与字节数（仅存档参考 —— 含 .debug 与源文件名，跨路径不可比）。

用法：
  python3 .analysis/22_1665/obj_fingerprint.py <对象路径> [--label 标签]
退出码：0 = 成功（与内容是否相同无关；比对用 diff）。
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import subprocess
import sys
import tempfile

OBJDUMP = "arm-none-eabi-objdump"
OBJCOPY = "arm-none-eabi-objcopy"
# 工具链记账段（内容随「传给 gcc 的源文件名 / 调试行号表」变化，非代码差异）
EXCLUDE = (".debug", ".comment", ".symtab", ".strtab", ".shstrtab", ".ARM.attributes",
           ".note", ".stab")


def run(*cmd: str) -> str:
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("obj")
    ap.add_argument("--label", default="")
    args = ap.parse_args()

    obj = pathlib.Path(args.obj).resolve()
    if not obj.is_file():
        print(f"缺对象文件：{obj}", file=sys.stderr)
        return 2

    print(f"# 对象：{obj}")
    if args.label:
        print(f"# 标签：{args.label}")
    print(f"# 文件 md5：{hashlib.md5(obj.read_bytes()).hexdigest()}  字节数：{obj.stat().st_size}")

    # ---- ① section 表（名 / 大小 / 对齐）+ 逐 section 内容 md5 ----
    secs = []
    for line in run(OBJDUMP, "-h", str(obj)).splitlines():
        parts = line.split()
        if len(parts) >= 7 and parts[1].startswith(".") and parts[1] != ".comment":
            idx, name, size, _, _, align = parts[0], parts[1], parts[2], parts[3], parts[4], parts[5]
            secs.append((idx, name, int(size, 16), align))

    print("\n== sections（序号 名称 大小(B) 对齐 | 内容 md5）==")
    md5s: dict[str, str] = {}
    with tempfile.TemporaryDirectory() as td:
        bin_path = pathlib.Path(td) / "sec.bin"
        for idx, name, size, align in secs:
            digest = "-"
            if not name.startswith(EXCLUDE):
                bin_path.unlink(missing_ok=True)
                subprocess.run([OBJCOPY, "-O", "binary", f"--only-section={name}",
                                str(obj), str(bin_path)], check=False,
                               capture_output=True)
                digest = (hashlib.md5(bin_path.read_bytes()).hexdigest()
                          if bin_path.is_file() else "(空)")
                md5s[name] = digest
            print(f"  {idx:>4} {name:<28} {size:>8} {align:>4} | {digest}")

    # ---- ② 反汇编（滤掉 file format 行）----
    lines = [l for l in run(OBJDUMP, "-d", str(obj)).splitlines() if "file format" not in l]
    asm = "\n".join(lines) + "\n"
    print(f"\n== 反汇编 ==\n  行数 {len(lines)}  整体 md5 {hashlib.md5(asm.encode()).hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
