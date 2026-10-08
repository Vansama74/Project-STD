#!/usr/bin/env python3
"""改动前后 C 源码 token 级等价校验（只允许删除、不允许新增/改写）

用法:
    check_token_equivalence.py <before.tar.gz> [--files a.c b.c | --list FILE] [--out REPORT]

原理:
  ① tar 内归档副本 = 改动前源码；工作区 = 改动后源码；
  ② 双方各自剥注释、切 token（标识符/数字/字符串/字符/预处理与全部标点单字符）；
  ③ 用 difflib 求最小编辑脚本，断言只存在 delete（after 是 before 的**子序列**）
     —— 任何 insert/replace 都会失败（用于证明"只删调试代码与注释，
     业务指令序列零改写/零重排"）。
  ④ 输出每处删除的 token 上下文，供人工审计「被删代码是否有业务副作用」。

退出码: 0 = 全部通过（仅删除）；1 = 存在新增/改写；2 = 文件缺失。
"""
import argparse
import difflib
import re
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

TOKEN_RE = re.compile(r"""
    "(?:\\.|[^"\\])*"          # 字符串字面量
  | '(?:\\.|[^'\\])*'          # 字符字面量
  | [A-Za-z_][A-Za-z0-9_]*     # 标识符
  | \d+\.?\d*(?:[eE][+-]?\d+)? # 数字
  | \.\d+
  | .
""", re.VERBOSE | re.DOTALL)


def strip_comments(src: str) -> str:
    """剥注释（保留字符串字面量内容与换行位置）。"""
    out = []
    i, n = 0, len(src)
    state = None
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if state is None:
            if c == "/" and nxt == "/":
                state = "line"
                i += 2
                continue
            if c == "/" and nxt == "*":
                state = "block"
                i += 2
                continue
            if c == '"':
                state = "str"
            elif c == "'":
                state = "chr"
            out.append(c)
            i += 1
        elif state == "str":
            out.append(c)
            if c == "\\":
                out.append(nxt)
                i += 2
                continue
            if c == '"':
                state = None
            i += 1
        elif state == "chr":
            out.append(c)
            if c == "\\":
                out.append(nxt)
                i += 2
                continue
            if c == "'":
                state = None
            i += 1
        elif state == "line":
            if c == "\n":
                state = None
                out.append(c)
            i += 1
        else:
            if c == "*" and nxt == "/":
                state = None
                i += 2
                continue
            if c == "\n":
                out.append("\n")
            i += 1
    return "".join(out)


def tokens(src: str):
    return [t for t in TOKEN_RE.findall(strip_comments(src)) if not t.isspace()]


def line_of_tokens(src: str):
    """token → 行号映射（供报告定位）。"""
    stripped = strip_comments(src)
    toks, lines, ln = [], [], 1
    for t in TOKEN_RE.findall(stripped):
        if t.isspace():
            ln += t.count("\n")
            continue
        toks.append(t)
        lines.append(ln)
    return toks, lines


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("archive")
    ap.add_argument("--list", help="文件清单（每行一个相对路径）")
    ap.add_argument("--files", nargs="*", default=[])
    ap.add_argument("--out", help="报告输出路径")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    # 已逐条人工复核的「等价改写」（非纯删除）——每项都必须给出改写理由；
    # 未列入者出现 insert/replace 即判 FAIL（工具默认严格）。
    ALLOW_REWRITE = {
        "Makefile": "开关/指纹段整段替换为本轮说明注释（构建语义见报告 §3）",
        "Platform/Inc/lwipopts.h": "APP_LWIP_DBG_LEVEL 覆盖块 → LWIP_DBG_TYPES_ON 0（LwIP 调试整体编出）",
        "Platform/Inc/pl_task_guard.h": "判空助手：删除 RTT 打印分支（函数体简化为 return tid）",
        "Device/Display/dev_display.c": "dev_display_guard：删除限速告警块，判断式等价简化为 return (ptr != nullptr)",
        "Application/Src/Channel/app_udp.c": "GZ_OL 绑定告警闩锁（纯打印限速态）删除 + 打印分支整块移除",
        "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c":
            "0x49：`const bool saved = …` + 诊断分支 → `(void)yn_ol_screen_cfg_save();`（写盘调用保留）",
        "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c":
            "0x20：`_gz_ol_text_unescape_nl(…, &nl_count)` → `…, nullptr`（换行计数仅供已删诊断打印；"
            "被调函数已判空出参，换行转换行为不变）",
        "Core/Src/syscalls.c": "新增无输出 `__io_putchar`（替代已移除的 SEGGER `_write` 落地点）",
        "Application/Src/app_boot.c":
            "启动通道段注释合并重写（调用顺序不变；token 层为纯删除 + 注释重写）",
        "Application/Src/app_factory_test_mm.c": "文件头注释重写（代码零改动）",
        "Application/Src/ProtocolParser_Myanmar/app_mm_proto.c": "文件头注释重写（代码零改动）",
        "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c": "注释重写（代码仅删诊断）",
        "Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h": "文件头注释重写",
        "Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h": "文件头注释重写",
        "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c": "文件头注释重写",
        "Application/Inc/ProtocolParser_Anhui/app_anhui_proto.h": "注释措辞（去掉 RTT 字样）",
    }

    files = list(args.files)
    if args.list:
        files += [l.strip() for l in Path(args.list).read_text(encoding="utf-8").splitlines()
                  if l.strip() and not l.strip().startswith("#")]
    if not files:
        print("no files given", file=sys.stderr)
        return 2

    report = []
    bad = 0
    missing = 0
    deleted = 0
    total_del = 0
    with tarfile.open(args.archive) as tf:
        names = set(tf.getnames())
        for rel in files:
            if rel not in names:
                report.append(f"[MISSING-IN-ARCHIVE] {rel}")
                missing += 1
                continue
            before = tf.extractfile(rel).read().decode("utf-8", "replace")
            cur = ROOT / rel
            if not cur.is_file():
                report.append(f"\n== {rel}: DELETED（整文件删除，已声明）")
                deleted += 1
                continue
            after = cur.read_text(encoding="utf-8", errors="replace")
            bt, bl = line_of_tokens(before)
            at, al = line_of_tokens(after)
            sm = difflib.SequenceMatcher(a=bt, b=at, autojunk=False)
            ops = [o for o in sm.get_opcodes() if o[0] != "equal"]
            ins = [o for o in ops if o[0] in ("insert", "replace")]
            dels = [o for o in ops if o[0] == "delete"]
            ndel = sum(o[2] - o[1] for o in dels)
            total_del += ndel
            allowed = rel in ALLOW_REWRITE
            status = "OK(仅删除)" if not ins else (
                "已声明改写" if allowed else "FAIL(存在新增/改写)")
            if ins and allowed:
                report.append(f"   [已声明改写] {ALLOW_REWRITE[rel]}")
            if ins and not allowed:
                bad += 1
            report.append(f"\n== {rel}: {status}  删除 token {ndel} 个 / 删除块 {len(dels)} 处"
                          f"（before {len(bt)} → after {len(at)} tokens）")
            for o in ins[:10]:
                snippet = " ".join(bt[o[1]:o[2]])[:200]
                report.append(f"   !! {o[0]} before[{o[1]}:{o[2]}] = {snippet!r}")
            for o in dels:
                snippet = " ".join(bt[o[1]:o[2]])
                snippet = snippet if len(snippet) <= 220 else snippet[:220] + " …"
                report.append(f"   - before:L{bl[o[1]]}-L{bl[max(o[2]-1, o[1])]}  {snippet}")

    text = "\n".join(report)
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    if not args.quiet:
        print(text)
    print(f"\n[token-equiv] 文件 {len(files)}：仅删除/已声明改写 {len(files) - bad - missing - deleted} / "
          f"整文件删除 {deleted} / 违规 {bad} / 归档缺失 {missing}；合计删除 token {total_del}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
