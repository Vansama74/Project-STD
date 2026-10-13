#!/usr/bin/env python3
"""续接轮（2026-10-13 第二轮）逐对象 A/B：改动前后同 flags 编译的 .o 对比。

对每个被改源文件：
  ① `objcopy --strip-debug` 后 md5（去掉行号信息噪声）；
  ② 段级逐段 md5（排除 .debug* / .comment / .ARM.attributes）；
  ③ `objdump -d` 反汇编逐行比对。
用法：python3 ab_round2_objects.py <before_dir> <after_dir>
"""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

CHANGED = [
    "Application/Src/IAP/app_iap.c",
    "Application/Src/IAP/app_iap_cmd.c",
    "Application/Src/LDI/app_ldi.c",
    "Application/Src/LDI/app_ldi_cmd.c",
    "Application/Src/ProtocolParser_Anhui/app_anhui_proto.c",
    "Application/Src/ProtocolParser_ChongQing/app_cq_proto.c",
    "Application/Src/ProtocolParser_GuiZhou/app_gz_proto.c",
    "Application/Src/ProtocolParser_QingHai/app_qh_proto.c",
    "Application/Src/ProtocolParser_ShanDong/app_sd_proto.c",
    "Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto.c",
    "Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto.c",
    "Application/Src/ProtocolParser_SiChuang_Overload/app_sc_ol_proto.c",
    "Application/Src/ProtocolParser_YunNan/app_yn_proto.c",
    "Application/Src/RLS/app_rls.c",
]
# 说明：6 个头文件（app_board_net_cfg.h / app_ldi_cfg.h / app_ldi_cmd.h / app_gz_ol_proto.h /
# app_mm_proto.h / app_yn_ol_proto.h）无独立 .o——其影响由「全 elf 段级 15/15 一致」（本目录
# report §⑧）与被其包含的各 .o（含下述反汇编）覆盖；objcopy 段级 dump 对 .rel* 段不可靠，
# 故逐对象判据 = strip-debug md5 + 反汇编逐行（两者覆盖全部非调试段、重定位表与符号表）。

def clean(txt: str) -> str:
    """去掉 objdump 首部「<路径>: file format elf32-littlearm」行（两构建目录路径不同）。"""
    return "\n".join(l for l in txt.splitlines() if "file format" not in l)


SKIP = (".debug", ".zdebug", ".comment", ".ARM.attributes", ".symtab", ".strtab", ".shstrtab")


def sections(obj: str) -> dict[str, int]:
    out = subprocess.run(["arm-none-eabi-readelf", "-S", "-W", obj], capture_output=True, text=True).stdout
    import re
    res = {}
    for line in out.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)", line)
        if m:
            res[m.group(1)] = int(m.group(5), 16)
    return res


def sec_md5(obj: str, name: str, size: int, tmp: str) -> str:
    if size == 0:
        return "empty"
    p = f"{tmp}/{name.replace('.', '_')}.bin"
    r = subprocess.run(["arm-none-eabi-objcopy", f"--dump-section={name}={p}", obj], capture_output=True, text=True)
    if r.returncode != 0:
        return "ERR"
    return hashlib.md5(Path(p).read_bytes()).hexdigest()


def main() -> int:
    bdir, adir = Path(sys.argv[1]), Path(sys.argv[2])
    bad = 0
    total_sec = diff_sec = 0
    for rel in CHANGED:
        obj = rel[:-2] + ".o"
        a, b = bdir / obj, adir / obj
        if not a.exists() or not b.exists():
            print(f"✘ 缺对象 {obj}")
            bad += 1
            continue
        ta, tb = tempfile.mkdtemp(), tempfile.mkdtemp()
        # strip-debug md5
        sa, sb = f"{ta}/x.o", f"{tb}/x.o"
        subprocess.run(["arm-none-eabi-objcopy", "--strip-debug", str(a), sa], check=True)
        subprocess.run(["arm-none-eabi-objcopy", "--strip-debug", str(b), sb], check=True)
        ma = hashlib.md5(Path(sa).read_bytes()).hexdigest()
        mb = hashlib.md5(Path(sb).read_bytes()).hexdigest()
        # 段级：用 readelf 段名集合 + 各段 objdump -s 十六进制比对（跳过 .rel* 等非内容段）
        secs_a, secs_b = sections(str(a)), sections(str(b))
        n = d = 0
        for name in secs_a:
            if name.startswith(SKIP) or name.startswith(".rel") or name in (".group", ".note.GNU-stack"):
                continue
            if name not in secs_b:
                print(f"  ONLY-IN-A {rel}:{name}")
                d += 1
                continue
            if secs_a[name] != secs_b[name]:
                print(f"  SIZE-DIFF {rel}:{name} {secs_a[name]} vs {secs_b[name]}")
                d += 1
                continue
            if secs_a[name] == 0:
                n += 1
                continue
            da = subprocess.run(["arm-none-eabi-objdump", "-s", "-j", name, str(a)], capture_output=True, text=True).stdout
            db = subprocess.run(["arm-none-eabi-objdump", "-s", "-j", name, str(b)], capture_output=True, text=True).stdout
            if clean(da) != clean(db):
                print(f"  DIFF {rel}:{name}")
                d += 1
            else:
                n += 1
        # 反汇编
        da = subprocess.run(["arm-none-eabi-objdump", "-d", str(a)], capture_output=True, text=True).stdout
        db = subprocess.run(["arm-none-eabi-objdump", "-d", str(b)], capture_output=True, text=True).stdout
        asm = "一致" if clean(da) == clean(db) else "差异"
        flag = "" if (ma == mb and d == 0 and clean(da) == clean(db)) else "  ← 不一致"
        print(f"{rel:64s} strip-md5={'同' if ma == mb else '异'} 段 {n}/{n + d} 反汇编 {asm}{flag}")
        total_sec += n + d
        diff_sec += d
        if ma != mb or d or clean(da) != clean(db):
            bad += 1
    print(f"== 逐对象 A/B：{len(CHANGED)} 个源文件，段级 {total_sec - diff_sec}/{total_sec} 一致，"
          f"不一致对象 {bad} 个 ==")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
