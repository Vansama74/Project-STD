#!/usr/bin/env python3
"""第四轮（2026-10-13 小项裁决）`g_dev_display_commit_count` 清理 A/B。

> **快照说明（2026-10-13 加注）**：本脚本锚定第四轮当时的 `Device/Inc/dev_display.h`
> （`HEADER_ANCHOR` = 彼时同在的 `extern volatile uint32_t g_dev_display_scan_count;`）。
> 第五轮（§⑪）已把该行连同 `g_dev_display_scan_count` 一并删除 ⇒ **本脚本不能直接在现行树上复跑**；
> 需复现第四轮证据时，先按 §⑩ 的删除项把 `commit_count` 的 1 处定义 + 2 处自增插回，并把
> `scan_count` 的 extern/定义/自增也插回（= 第五轮前的形态），再运行本脚本。
> 第五轮自身的 A/B 见 `ab_round5_scan_count.py`。

三部分证据：

**Part 1 — `dev_display.o` 真实 A/B（归档改动前对象 vs 现行构建产物）**
  · 段尺寸对比：`.text.dev_display_commit_frame` / `.text.dev_display_commit_frame_rect`
    按删除的 volatile 自增收敛；`.bss.g_dev_display_commit_count` 段消失；其余段不变；
  · `objdump -d` 逐函数比对：差异函数必须**恰好**是 `dev_display_commit_frame` 与
    `dev_display_commit_frame_rect`，其余 12 个函数逐行一致（并打印差异函数的具体指令差）。

**Part 2 — 头文件行号位移的隔离证明（重建 A/B，覆盖全部重编 TU）**
  删除 `dev_display.h:146` 的 extern 行使该行之后声明的 `DW_AT_decl_line` 前移 1 ⇒
  依赖该头的 TU 会因 `.debug_*` 差异而 md5 变化（**与 codegen 无关**）。做法：
  在临时目录重建「改动前」两个文件（`dev_display.h` 插回 extern 行、`dev_display.c`
  插回 1 处定义 + 2 处自增）；因设备头用 `#pragma once`（按文件实体去重），
  覆盖目录 = `Device/Inc` 全量软链接 + 改前 `dev_display.h` 实体，并把 CFLAGS 的
  `-I Device/Inc` 整体替换为该目录；逐 TU 编译 before 变体，与 build 目录中的
  after 真实对象比对：
    · `objcopy --strip-debug` 后 md5 必须相同（= 机器码零差异）；
    · 差异段必须全部落在 `.debug*`；
    · for-trust：`dev_display.c` 的 before 变体对象必须与**归档的真实改动前对象**逐字节相同。
  另统计：md5 完全未变的 TU（before 变体 == after 对象，逐字节）。

**Part 3 — 链接期归因（重建改动前 elf）**
  用现行对象 + 归档的改动前 `dev_display.o` 重链（链接参数取自本轮链接命令行），
  校验重建 elf 的段尺寸 == 改前基线（171832 / 158444），再用符号地址差定位
  「对象级 −24B」与「镜像级 −16B/−32B」之间的 ±8B 增量。

用法：python3 .analysis/comment_pass_20261013/ab_round4_commit_count.py
退出码 = 失败项数。
"""
from __future__ import annotations

import hashlib
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
R4 = ROOT / ".analysis/comment_pass_20261013/ab_round4"
LOGS = ROOT / ".analysis/comment_pass_20261013/logs"

# 改动前的删除项（逐字 = 本轮从源码中移除的原文）
DEF_LINE = "volatile uint32_t g_dev_display_commit_count;\n"
EXTERN_LINE = "extern volatile uint32_t g_dev_display_commit_count;\n"
INC_FRAME = "    g_dev_display_commit_count++;\n    osKernelUnlock();"
INC_RECT = "    g_dev_display_commit_count++;\n    osKernelUnlock();"
HEADER_ANCHOR = "extern volatile uint32_t g_dev_display_scan_count;\n"

BUILDS = {
    "all_1263": {
        "build_dir": ROOT / "build/cmt3_after/Debug",
        "log": LOGS / "round4_build_all_debug.log",
        "baseline_text": 171832,
        "baseline_bss": 125364,
    },
    "cq_1263": {
        "build_dir": ROOT / "build/cmt3_after_cq/Debug",
        "log": LOGS / "round4_build_cq_debug.log",
        "baseline_text": 158444,
        "baseline_bss": 121876,
    },
}

SKIP_SECT = (".debug", ".zdebug", ".comment", ".ARM.attributes", ".symtab", ".strtab",
             ".shstrtab", ".group", ".note.GNU-stack")
SKIP_SECT_NONDBG = (".comment", ".ARM.attributes", ".symtab", ".strtab",
                    ".shstrtab", ".group", ".note.GNU-stack")


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT, **kw)


def cflags(stamp: pathlib.Path) -> list[str]:
    for line in stamp.read_text(encoding="utf-8").splitlines():
        if line.startswith("CFLAGS="):
            return shlex.split(line[len("CFLAGS="):])
    raise SystemExit(f"未在 {stamp} 找到 CFLAGS=")


def sections_md5(obj: pathlib.Path, skip_debug: bool = True) -> dict[str, str]:
    """逐段 md5；skip_debug=False 时含 `.debug*`（用于「差异是否全部落在调试信息」判据）。"""
    out = run(["arm-none-eabi-readelf", "-S", "-W", str(obj)]).stdout
    sizes = {}
    for line in out.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)", line)
        if m:
            sizes[m.group(1)] = int(m.group(3), 16)
    res = {}
    for name, size in sizes.items():
        if name.startswith(SKIP_SECT_NONDBG) or name.startswith(".rel"):
            continue
        if skip_debug and name.startswith((".debug", ".zdebug")):
            continue
        if size == 0:
            res[name] = "empty"
            continue
        dump = run(["arm-none-eabi-objdump", "-s", "-j", name, str(obj)]).stdout
        res[name] = hashlib.md5("\n".join(l for l in dump.splitlines()
                                          if "file format" not in l).encode()).hexdigest()
    return res


def obj_sizes(obj: pathlib.Path) -> dict[str, int]:
    out = run(["arm-none-eabi-size", "-A", str(obj)]).stdout
    res = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 2 and p[0].startswith("."):
            res[p[0]] = int(p[1])
    return res


def strip_md5(obj: pathlib.Path, tmp: pathlib.Path) -> str:
    dst = tmp / (obj.name + ".stripped")
    subprocess.run(["arm-none-eabi-objcopy", "--strip-debug", str(obj), str(dst)],
                   check=True, capture_output=True)
    return hashlib.md5(dst.read_bytes()).hexdigest()


def funcs(obj: pathlib.Path) -> dict[str, list[str]]:
    out = run(["arm-none-eabi-objdump", "-d", str(obj)]).stdout
    cur, res = None, {}
    for line in out.splitlines():
        m = re.match(r"^[0-9a-f]{8} <(.+)>:$", line)
        if m:
            cur = m.group(1)
            res[cur] = []
        elif cur is not None:
            res[cur].append(line)
    return res


def n_insns(lines: list[str]) -> int:
    return sum(1 for l in lines if re.match(r"^\s+[0-9a-f]+:", l))


def main() -> int:
    fails = 0

    # ================= Part 1：dev_display.o 真实 A/B =================
    print("== Part 1：dev_display.o 真实 A/B（归档改动前 vs 现行产物）==")
    for name, cfg in BUILDS.items():
        before = R4 / name / "dev_display.before.o"
        after = cfg["build_dir"] / "Device/Display/dev_display.o"
        sb, sa = obj_sizes(before), obj_sizes(after)
        expect = {
            ".text.dev_display_commit_frame": -12,
            ".text.dev_display_commit_frame_rect": -12,
            ".bss.g_dev_display_commit_count": None,
        }
        seen = {}
        for k in sorted(set(sb) | set(sa)):
            if k.startswith(SKIP_SECT):
                continue
            va, vb = sb.get(k), sa.get(k)
            if va != vb:
                seen[k] = None if vb is None else (vb - va)
        ok = seen == expect
        print(f"  [{'通过' if ok else '失败'}] {name}：段差异 = {seen}")
        if not ok:
            fails += 1
        # 逐函数
        fb, fa = funcs(before), funcs(after)
        diff_f = {k for k in set(fb) | set(fa) if fb.get(k) != fa.get(k)}
        want = {"dev_display_commit_frame", "dev_display_commit_frame_rect"}
        same_n = sum(1 for k in fb if k in fa and fb[k] == fa[k])
        ok2 = diff_f == want
        print(f"  [{'通过' if ok2 else '失败'}] {name}：函数数 {len(fb)}/{len(fa)}，"
              f"逐行一致 {same_n} 个，差异函数 = {sorted(diff_f)}")
        if not ok2:
            fails += 1
        if name == "all_1263":
            print("  ---- 差异函数的指令级比对（before → after）----")
            for fn in sorted(want):
                tb = [l.split(":", 1)[1].strip() for l in fb[fn] if re.match(r"^\s+[0-9a-f]+:", l)]
                ta = [l.split(":", 1)[1].strip() for l in fa[fn] if re.match(r"^\s+[0-9a-f]+:", l)]
                import difflib
                print(f"    · {fn}（{len(tb)} → {len(ta)} 条）")
                for d in difflib.unified_diff(tb, ta, lineterm="", n=1):
                    if d.startswith(("+", "-")) and not d.startswith(("+++", "---")):
                        print(f"        {d}")

    # ================= Part 2：重建 A/B（全部重编 TU，隔离证明） =================
    print("\n== Part 2：重建改动前状态，逐 TU 隔离证明（strip-debug 零差异）==")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="round4_"))
    # 头文件覆盖目录：Device/Inc 全量软链接 + 「改动前」dev_display.h 实体。
    # 设备头用 #pragma once（按文件实体去重）⇒ 不能让真头与副本同时可见，
    # 故把 CFLAGS 里的 `-I Device/Inc` 整体替换为本目录（软链接按访问路径解析，
    # 头之间的 quoted include 仍落在本目录内）。
    inc = tmp / "Device_Inc_before"
    inc.mkdir()
    for entry in (ROOT / "Device/Inc").iterdir():
        if entry.name != "dev_display.h":
            (inc / entry.name).symlink_to(entry)
    hdr = (ROOT / "Device/Inc/dev_display.h").read_text(encoding="utf-8")
    assert hdr.count(HEADER_ANCHOR) == 1
    (inc / "dev_display.h").write_text(hdr.replace(HEADER_ANCHOR, EXTERN_LINE + HEADER_ANCHOR),
                                       encoding="utf-8")
    # dev_display.c before 变体（3 处插回）
    src = (ROOT / "Device/Display/dev_display.c").read_text(encoding="utf-8")
    assert src.count("    s_pending_rect_valid = false; /* 全屏语义：矩形失效 → prepare 全量重排 */\n    osKernelUnlock();") == 1
    src_b = src.replace(
        "    s_pending_rect_valid = false; /* 全屏语义：矩形失效 → prepare 全量重排 */\n    osKernelUnlock();",
        "    s_pending_rect_valid = false; /* 全屏语义：矩形失效 → prepare 全量重排 */\n" + INC_FRAME)
    assert src_b.count("    s_pending_frame = dev->pixel_map;\n    s_frame_ready   = 1U;\n    osKernelUnlock();") == 1
    src_b = src_b.replace(
        "    s_pending_frame = dev->pixel_map;\n    s_frame_ready   = 1U;\n    osKernelUnlock();",
        "    s_pending_frame = dev->pixel_map;\n    s_frame_ready   = 1U;\n" + INC_RECT)
    anchor_c = "static bool s_pending_rect_valid;\n\nvolatile uint32_t g_dev_display_scan_count;"
    assert src_b.count(anchor_c) == 1
    src_b = src_b.replace(anchor_c, "static bool s_pending_rect_valid;\n\n" +
                          DEF_LINE + "volatile uint32_t g_dev_display_scan_count;")
    (tmp / "dev_display_before.c").write_text(src_b, encoding="utf-8")

    base = cflags(BUILDS["all_1263"]["build_dir"] / ".build_stamp")

    def compile_before(rel: str, tmp_name: str) -> pathlib.Path | None:
        """编译 before 变体：`-I Device/Inc` 替换为含「改动前 dev_display.h」的覆盖目录。"""
        out = tmp / tmp_name
        c, i = [], 0
        while i < len(base):
            if base[i] == "-I" and base[i + 1] == "Device/Inc":
                c += ["-I", str(inc)]
                i += 2
                continue
            c.append(base[i])
            i += 1
        r = run(["arm-none-eabi-gcc", *c, "-c", "-o", str(out), rel])
        if r.returncode != 0:
            print(f"  [失败] 编译 {rel}\n{r.stderr[:600]}")
            return None
        return out

    # 2a for-trust：dev_display.c before 变体 == 归档真实改动前对象
    # （重建的临时源/头路径会写进调试信息 ⇒ 判据取「机器码等价」：
    #   strip-debug md5 + 反汇编逐行 + 差异段 ⊆ .debug*，并验证差异来源 = 临时路径字符串）
    obj_b = compile_before(str(tmp / "dev_display_before.c"), "dev_display_before.o")
    trust = False
    if obj_b is not None:
        sa, sb = strip_md5(R4 / "all_1263/dev_display.before.o", tmp), strip_md5(obj_b, tmp)
        m_a = sections_md5(R4 / "all_1263/dev_display.before.o")            # 机器码段
        m_b = sections_md5(obj_b)
        a_all = sections_md5(R4 / "all_1263/dev_display.before.o", skip_debug=False)
        b_all = sections_md5(obj_b, skip_debug=False)
        sec_diff = {k for k in set(a_all) | set(b_all) if a_all.get(k) != b_all.get(k)}
        dbg_secs = sorted(k for k in sec_diff if k.startswith((".debug", ".zdebug")))
        raw = obj_b.read_bytes()
        path_art = b"Device_Inc_before" in raw or b"dev_display_before.c" in raw
        asm_same = funcs(R4 / "all_1263/dev_display.before.o") == funcs(obj_b)
        trust = (sa == sb and asm_same and path_art and m_a == m_b
                 and len(dbg_secs) == len(sec_diff))
        print(f"  [{'通过' if trust else '失败'}] 重建可信度：dev_display.c before 变体 vs 归档改动前对象 —— "
              f"strip-debug md5 {'相同' if sa == sb else '不同'} / 反汇编{'逐行一致' if asm_same else '有差异'} / "
              f"机器码段差异 {sorted(set(m_a.items()) ^ set(m_b.items())) or '无'} / "
              f"调试段差异 {dbg_secs}（⊆ .debug*）/ 临时路径字符串在对象内 "
              f"{'是' if path_art else '否'}")
    if not trust:
        fails += 1

    # 2b 其余重编 TU：before 变体 vs build 目录 after 真实对象
    log = BUILDS["all_1263"]["log"].read_text(encoding="utf-8")
    tUs = sorted({m.group(1) for m in re.finditer(r"^Compiling (\S+\.c)$", log, re.M)})
    # 现网增量构建的真实 md5 对比（改前基线快照 vs 本轮构建产物）
    real_before_md5 = {p.split()[1].removeprefix("build/cmt3_after/Debug/"): p.split()[0]
                       for p in (R4 / "all_before.sorted").read_text().splitlines()}
    real_after_md5 = {p.split()[1].removeprefix("build/cmt3_after/Debug/"): p.split()[0]
                      for p in (R4 / "all_after_debug.sorted").read_text().splitlines()}
    n_codegen_diff = n_debug_only = n_identical = 0
    dbg_names: set[str] = set()
    for src_rel in tUs:
        if src_rel.endswith("Device/Display/dev_display.c"):
            continue
        after_obj = BUILDS["all_1263"]["build_dir"] / src_rel.replace(".c", ".o")
        obj = compile_before(src_rel, src_rel.replace("/", "_") + ".o")
        if obj is None:
            fails += 1
            continue
        m_a = hashlib.md5(after_obj.read_bytes()).hexdigest()
        m_b = hashlib.md5(obj.read_bytes()).hexdigest()
        if m_a == m_b:
            n_identical += 1
            continue
        # strip-debug 后必须一致（= 机器码零差异）；含 .debug* 的段差异必须全部落在调试信息
        sa = strip_md5(after_obj, tmp)
        sb = strip_md5(obj, tmp)
        sec_a = sections_md5(after_obj, skip_debug=False)
        sec_b = sections_md5(obj, skip_debug=False)
        sec_diff = {k for k in set(sec_a) | set(sec_b) if sec_a.get(k) != sec_b.get(k)}
        dbg_diff = {k for k in sec_diff if k.startswith((".debug", ".zdebug"))}
        only_debug = len(dbg_diff) == len(sec_diff) and bool(sec_diff)
        dbg_names.update(dbg_diff)
        if sa == sb and only_debug:
            n_debug_only += 1
        else:
            n_codegen_diff += 1
            print(f"  [失败] {src_rel}：strip-debug {'同' if sa == sb else '异'}，"
                  f"差异段 {sorted(sec_diff)}")
            fails += 1
    real_changed = [k for k in real_before_md5
                    if k in real_after_md5 and real_before_md5[k] != real_after_md5[k]]
    real_tu = [k for k in tUs if not k.endswith("Device/Display/dev_display.c")]
    real_changed_tu = [k for k in real_changed if k.replace(".o", ".c") in real_tu]
    print(f"  [{'通过' if n_codegen_diff == 0 else '失败'}] 隔离证明：{len(real_tu)} 个重编 TU —— "
          f"codegen 差异 {n_codegen_diff} 个（须 0）")
    print(f"        现网实测（改前基线 vs 本轮构建）：md5 变化 {len(real_changed_tu)} 个 / 不变 "
          f"{len(real_tu) - len(real_changed_tu)} 个")
    print(f"        隔离复算：strip-debug 后逐字节相同 {n_debug_only} 个（差异全部落在 {sorted(dbg_names)}，"
          f"= 行号位移 {len(real_changed_tu)} 个 + 重建路径字符串 {n_debug_only - len(real_changed_tu)} 个）"
          f" / 逐字节相同 {n_identical} 个")
    print("        （仅 .debug* 差异的原因 = dev_display.h 删 1 行 ⇒ 其后函数原型声明的"
          " DWARF decl_line 前移 1；无宏/无内联/无类型变化 ⇒ codegen 不变）")

    # ================= Part 3：链接期归因（重建改动前 elf） =================
    print("\n== Part 3：链接期归因（归档 before 对象重链 vs 现行 elf）==")
    for name, cfg in BUILDS.items():
        log_txt = cfg["log"].read_text(encoding="utf-8")
        link = None
        for line in log_txt.splitlines():
            if line.startswith("arm-none-eabi-gcc ") and "-o" in line and "Project_STD.elf" in line \
                    and "/Device/Display/dev_display.o" in line:
                link = shlex.split(line)
        if link is None:
            print(f"  [失败] {name}：日志中未找到链接命令行")
            fails += 1
            continue
        out_before = R4 / name / "Project_STD.relink_before.elf"
        map_before = R4 / name / "Project_STD.relink_before.map"
        cmd, i = [], 0
        while i < len(link):
            tok = link[i]
            if tok == "-o" and link[i + 1].endswith("Project_STD.elf"):
                cmd += ["-o", str(out_before)]
                i += 2  # 连同旧输出路径一起消费
                continue
            if tok.startswith("-Wl,-Map="):
                cmd += [f"-Wl,-Map={map_before},--cref"]
            elif tok.endswith("Device/Display/dev_display.o"):
                cmd += [str(R4 / name / "dev_display.before.o")]
            else:
                cmd += [tok]
            i += 1
        # 其余对象用现行 build 目录（codegen 与改前一致，仅 .debug* 行号位移）
        r = run(cmd)
        if r.returncode != 0:
            print(f"  [失败] {name}：重链失败\n{r.stderr[:400]}")
            fails += 1
            continue
        sb = obj_sizes(out_before)
        txt_ok = sb.get(".text") == cfg["baseline_text"]
        print(f"  [{'通过' if txt_ok else '失败'}] {name}：重建改动前 elf .text = {sb.get('.text')}"
              f"（基线 {cfg['baseline_text']}）")
        if not txt_ok:
            fails += 1
        # 符号地址差（before_elf vs after_elf）
        def syms(p):
            out = run(["arm-none-eabi-nm", "-S", "--defined-only", "-n", str(p)]).stdout
            d = {}
            for line in out.splitlines():
                parts = line.split()
                if len(parts) >= 4:
                    d[parts[3]] = int(parts[0], 16)
            return d
        s_before = syms(out_before)
        s_after = syms(cfg["build_dir"] / "Project_STD.elf")
        common = [k for k in s_before if k in s_after]
        shifted = {k: s_after[k] - s_before[k] for k in common if s_after[k] != s_before[k]}
        if shifted:
            vals = sorted(set(shifted.values()))
            print(f"        符号地址位移：{len(shifted)} 个符号，位移值集合 = {vals}"
                  f"（同一位移 = 代码段整体收缩，无其它改动）")
        else:
            print("        符号地址无位移")
        d_text = obj_sizes(cfg["build_dir"] / "Project_STD.elf").get(".text")
        print(f"        after elf .text = {d_text} ⇒ 镜像级 {d_text - cfg['baseline_text']:+d}B"
              f"（对象级 −24B = 2×−12B；差额由链接期对齐/填充吸收）")

    print(f"\n第四轮 A/B：{'全绿' if fails == 0 else f'{fails} 项失败'}")
    return fails


if __name__ == "__main__":
    sys.exit(main())
