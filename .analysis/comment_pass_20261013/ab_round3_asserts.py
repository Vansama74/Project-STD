#!/usr/bin/env python3
"""第三轮（2026-10-13 裁决执行）断言删除 A/B：**隔离证明「删断言 = 零 codegen」**。

做法：对两个「断言被删」的源文件，用**同一份编译参数**（逐字取
`build/cmt3_after/Debug/.build_stamp` 的 `CFLAGS=` 行）各编两份对象：
  · 现行源码（断言已删，含契约注释）；
  · 把**原 `_Static_assert` 语句逐字插回**的临时副本（仅此一处差异）。
判据（全绿才通过）：
  ① `objcopy --strip-debug` 后 md5 相同；
  ② 全部非调试段逐段 md5 相同（`objdump -s` 十六进制比对）；
  ③ `objdump -d` 反汇编逐行相同。

`app_dispatch.c` 的 A/B 只针对「断言」这一处差异——它与 API 清理的差异另由
`ab_round3_objects.py`（逐对象 diff + 期望清单）覆盖。
用法：python3 .analysis/comment_pass_20261013/ab_round3_asserts.py
退出码 = 失败项数。
"""
from __future__ import annotations

import hashlib
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
STAMP = ROOT / "build/cmt3_after/Debug/.build_stamp"

# app_dispatch.c 的 4 条断言（逐字 = 上一轮删除前原文）
APPDISPATCH_ASSERTS = """_Static_assert(sizeof(g_rb_provide) / sizeof(g_rb_provide[0]) == RB_CNT_MAX, "g_rb_provide length");
_Static_assert(sizeof(names) / sizeof(names[0]) == RB_CNT_MAX, "names length");
_Static_assert(RB_SLOT_RJ45 == 0 && RB_SLOT_RS485 == 1 && RB_SLOT_RS232 == 2 &&
                   RB_SLOT_COUNT == 3,
               "rb_slot_t must stay contiguous");
_Static_assert(RB_SIZE_RJ45 == 1536U && RB_SIZE_RS485 == 768U && RB_SIZE_RS232 == 768U,
               "RB sizes must match product plan");
"""

SKIP = (".debug", ".zdebug", ".comment", ".ARM.attributes", ".symtab", ".strtab", ".shstrtab")


def cflags() -> list[str]:
    for line in STAMP.read_text(encoding="utf-8").splitlines():
        if line.startswith("CFLAGS="):
            return shlex.split(line[len("CFLAGS="):])
    raise SystemExit("未在 .build_stamp 找到 CFLAGS=")


def sections_md5(obj: pathlib.Path) -> dict[str, str]:
    out = subprocess.run(["arm-none-eabi-readelf", "-S", "-W", str(obj)],
                         capture_output=True, text=True).stdout
    sizes = {}
    for line in out.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)", line)
        if m:
            sizes[m.group(1)] = int(m.group(3), 16)
    res = {}
    for name, size in sizes.items():
        if name.startswith(SKIP) or name.startswith(".rel") or name in (".group", ".note.GNU-stack"):
            continue
        if size == 0:
            res[name] = "empty"
            continue
        dump = subprocess.run(["arm-none-eabi-objdump", "-s", "-j", name, str(obj)],
                              capture_output=True, text=True).stdout
        text = "\n".join(l for l in dump.splitlines() if "file format" not in l)
        res[name] = hashlib.md5(text.encode()).hexdigest()
    return res


def variant(src_text: str, anchor: str, inserted: str) -> str:
    assert src_text.count(anchor) == 1, f"锚点命中 {src_text.count(anchor)} 次"
    return src_text.replace(anchor, inserted)


def main() -> int:
    cc = "arm-none-eabi-gcc"
    base = cflags()
    tmp = pathlib.Path(tempfile.mkdtemp())
    fails = 0
    n_case = 0

    def run_case(rel: str, cur_text: str, var_text: str) -> None:
        nonlocal fails, n_case
        n_case += 1
        cur, var = tmp / "cur.c", tmp / "var.c"
        cur.write_text(cur_text, encoding="utf-8")
        var.write_text(var_text, encoding="utf-8")
        oc, ov = tmp / "cur.o", tmp / "var.o"
        for src, outp in ((cur, oc), (var, ov)):
            r = subprocess.run([cc, *base, "-c", "-o", str(outp), str(src)],
                               capture_output=True, text=True)
            if r.returncode != 0:
                print(f"  [失败] {rel}: 编译失败\n{r.stderr[:800]}")
                fails += 1
                return
        # ① strip-debug md5
        sc, sv = tmp / "cur_s.o", tmp / "var_s.o"
        subprocess.run(["arm-none-eabi-objcopy", "--strip-debug", str(oc), str(sc)], check=True)
        subprocess.run(["arm-none-eabi-objcopy", "--strip-debug", str(ov), str(sv)], check=True)
        m1 = hashlib.md5(sc.read_bytes()).hexdigest()
        m2 = hashlib.md5(sv.read_bytes()).hexdigest()
        # ② 段级
        s1, s2 = sections_md5(oc), sections_md5(ov)
        sec_ok = s1 == s2
        # ③ 反汇编
        d1 = subprocess.run(["arm-none-eabi-objdump", "-d", str(oc)], capture_output=True, text=True).stdout
        d2 = subprocess.run(["arm-none-eabi-objdump", "-d", str(ov)], capture_output=True, text=True).stdout
        asm_ok = "\n".join(l for l in d1.splitlines() if "file format" not in l) == \
                 "\n".join(l for l in d2.splitlines() if "file format" not in l)
        if m1 == m2 and sec_ok and asm_ok:
            print(f"  [通过] {rel}：断言插回前后 strip-debug md5 相同 + 段级 {len(s1)}/{len(s1)} 一致 + "
                  f"反汇编逐行一致")
        else:
            fails += 1
            print(f"  [失败] {rel}：md5 {'同' if m1 == m2 else '异'} / 段级 {'一致' if sec_ok else '差异'} / "
                  f"反汇编 {'一致' if asm_ok else '差异'}")
            if not sec_ok:
                for k in sorted(set(s1) | set(s2)):
                    if s1.get(k) != s2.get(k):
                        print(f"        ✘ 段 {k}")

    # ---- dev_display_22_1665.c：插回守卫 ① 与守卫 ②（含 _22_1665_CCM_BYTES 宏）----
    rel = "Device/Display/dev_display_22_1665.c"
    cur_text = (ROOT / rel).read_text(encoding="utf-8")
    var_text = cur_text
    # 守卫 ①：在契约 ① 块之后插回断言
    a1 = (" * 见 doc/01_显示系统/22-1665模组驱动分析与迁移记录.md §0.3。 */\n"
          "\n"
          "typedef struct {\n"
          '    const char *name;        /* 线名（文档 / 烧录物身份用，不参与逻辑） */')
    i1 = (" * 见 doc/01_显示系统/22-1665模组驱动分析与迁移记录.md §0.3。 */\n"
          "_Static_assert(2U * _22_1665_MODULE_COLS <= HUB75_CHANNEL_MAX,\n"
          '               "22_1665: per-column wiring needs 2 channels per module column"\n'
          '               " (2 x MODULE_COLS <= HUB75_CHANNEL_MAX = 10)");\n'
          "\n"
          "typedef struct {\n"
          '    const char *name;        /* 线名（文档 / 烧录物身份用，不参与逻辑） */')
    var_text = variant(var_text, a1, i1)
    # 守卫 ②：契约块之后插回 _22_1665_CCM_BYTES 定义 + 断言
    a2 = (" * 是否装得下同由链接期兜底。见 doc/01_显示系统/22-1665模组驱动分析与迁移记录.md §0.3。 */\n"
          "\n"
          "static dev_display_22_1665_t g_22_1665 = {")
    i2 = (" * 是否装得下同由链接期兜底。见 doc/01_显示系统/22-1665模组驱动分析与迁移记录.md §0.3。 */\n"
          "\n"
          "/** 本模组 CCMRAM 占用 = 4 个 `.ccmram` 数组之和（通式 1408·M + 256·MODULE_COLS；\n"
          " *  1×1 = 1664B、1×2 = 3328B、8×4 = 46080B） */\n"
          "#define _22_1665_CCM_BYTES                                                                     \\\n"
          "    (sizeof(_22_1665_chain_dst) + sizeof(_22_1665_pixel_map) +                                 \\\n"
          "     sizeof(_22_1665_frame_state) + sizeof(_22_1665_bsrr_tab))\n"
          "\n"
          "_Static_assert(_22_1665_CCM_BYTES <= 64U * 1024U,\n"
          '               "22_1665: arrays exceed CCMRAM region (64KB; reduce MODULE_ROWS/MODULE_COLS)");\n'
          "\n"
          "static dev_display_22_1665_t g_22_1665 = {")
    var_text = variant(var_text, a2, i2)
    run_case(rel, cur_text, var_text)

    # ---- app_dispatch.c：在契约注释后插回 4 条断言 ----
    rel = "Application/Src/app_dispatch.c"
    cur_text = (ROOT / rel).read_text(encoding="utf-8")
    anchor = (" * doc/05_协议模块多协议兼容优化/01_architecture.md §2.1（一物理通道一 RB）。 */\n"
              "\n"
              "/* ================================================================\n"
              " *  调度上下文")
    assert cur_text.count(anchor) == 1, cur_text.count(anchor)
    var_text = cur_text.replace(
        anchor,
        " * doc/05_协议模块多协议兼容优化/01_architecture.md §2.1（一物理通道一 RB）。 */\n"
        + APPDISPATCH_ASSERTS
        + "\n/* ================================================================\n"
          " *  调度上下文")
    run_case(rel, cur_text, var_text)

    print(f"\n断言删除 A/B（隔离）：{n_case - fails}/{n_case} 通过，失败 {fails}")
    return fails


if __name__ == "__main__":
    sys.exit(main())
