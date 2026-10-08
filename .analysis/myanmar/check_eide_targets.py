#!/usr/bin/env python3
"""EIDE 目标**结构不变量**校验 + 编译集 / CCMRAM 预算报告。

口径（2026-10-08 用户裁决，取代旧的「Debug 必须 = 某固定集合」断言）：
    协议解析器（Application/Src/ProtocolParser_xx）与显示模组驱动（Device/Display/dev_display_xx.c）
    **编不编、编哪个属于项目可随时调整的选择，不是固定契约**。本脚本因此只校验**结构不变量**，
    并把每个目标的**当前集合**作为信息原样打印；任何一套合法集合都应通过。

结构不变量（违反 = 失败；依据全部来自代码/构建事实）：
  I1  显示模组驱动编入数 ≤ 1          —— >1 = 双实例注册 + CCMRAM 超（doc/01 三模组硬约束）
  I2  编入 0 个模组驱动 = 「无显示」口径（合法；运行时由 dev_display_guard 兜底，doc/16 §10）
  I3  基类 Device/Display/dev_display.c 必须编入（显示 API / 守卫宿主）
  I4  出厂测试恰好编入 1 个           —— 标准版 / 缅甸版对外符号同名，同编即链接 multiple definition
  I5  `{` 帧族协议编入数 ≤ 1          —— 源码事实：定义 g_brace_proto_guard 的模块；除非 defineList
      含 STD_ALL_PROTO（全协议开发构建口径才允许共存）
  I6  定义 PROTO_CHONGQING ⇒ CQ 模块必须编入 —— 否则 = C3「混编」（CQ 语义开关生效但无消费者，
      默认网络写 CQ 网段 192.168.1.5，而 CQ 协议与 20103 口均不在镜像里）
  I7  编入 Application/Src/<X>/**（若 Application/Inc/<X> 存在）⇒ 该目标 incList 必须含
      Application/Inc/<X> —— 防 2026-09-24 那类 fatal error: xxx.h: No such file or directory
  I8  每个目标 defineList 必须含 USE_HAL_DRIVER / STM32F407xx（STM32 工程前提）

提示（打印但不失败）：
  W1  定义 PROTO_CHONGQING 且同时编入 LDI —— 2026-10-08 C3-C 起 LDI 上报已与 Sector1 同源，
      无功能矛盾；仅提示「默认网络为 CQ 网段」这一事实
  W2  编入 CQ 模块但未定义 PROTO_CHONGQING —— dev 共存口径，合法
  W3  地面真相对拍：`build/<target>/builder.params`（EIDE 自己写的上次构建源清单）与本脚本推导集
      的差异一律**只报信息**（yml 可能刚被 GUI 改过而 EIDE 尚未重建）
  W4  非可用目标（incList 仍含已删除的 Drivers/BSP 前缀，如 Release）= 历史残留：跳过 I1~I8 硬断言，
      只打印推导集与原因

EIDE 排除语义（源码依据：`~/.cursor/extensions/cl.eide-3.27.2-universal/dist/extension.js`
的 `isExcluded()`）：excludeList 条目与其**虚拟路径**逐段比较
（`virtual_path == entry || virtual_path.startswith(entry + '/')`），虚拟路径 =
`<virtual_root>/` + **虚拟文件夹链** + 文件名 —— 虚拟链 ≠ 磁盘路径（例：磁盘
`Application/Src/ProtocolParser_YunNan/app_yn_proto.c` 的虚拟路径是
`<virtual_root>/Application/protocol/ProtocolParser_YunNan/app_yn_proto.c`）。

用法：
    python3 .analysis/myanmar/check_eide_targets.py                    # 校验 + 报告
    python3 .analysis/myanmar/check_eide_targets.py -v                 # 加打完整编入/排除清单
    python3 .analysis/myanmar/check_eide_targets.py --target Debug --emit-make /tmp/mk/Debug.mk
        # 生成「某目标等价口径」的临时 Makefile（默认目标 = Myanmar）；产物目录默认改写为
        # .analysis/myanmar/build_nodisp/$(CONFIG)（可用 BUILD_DIR=… 覆盖），不污染 build/
退出码 = 结构不变量失败项数（0 = 全绿）。
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
YML = ROOT / ".eide/eide.yml"
VR = "<virtual_root>/"

# ---- 候选池（files 应收录的条目；缺失只输出信息，不失败）----
MM_SRC = [
    "Application/Src/ProtocolParser_Myanmar/app_mm_proto.c",
    "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c",
]
MM_VFOLDER = "ProtocolParser_Myanmar"
MM_INC = "Application/Inc/ProtocolParser_Myanmar"
DEV_DISPLAY_BASE = "Device/Display/dev_display.c"
DEV_DISPLAY_RE = re.compile(r"^Device/Display/dev_display_.+\.c$")

# 出厂测试二选一（同名对外符号 ⇒ 同编 = multiple definition，天然互斥守卫）
FACTORY_STD = "Application/Src/app_factory_test.c"
FACTORY_MM = "Application/Src/app_factory_test_mm.c"
FACTORY_PAIR = {FACTORY_STD, FACTORY_MM}

# ---- `{` 帧族：**从源码事实推导**（哪个模块定义 g_brace_proto_guard），不硬编码目录名 ----
GUARD_SYM = "g_brace_proto_guard"
STD_ALL_PROTO = "STD_ALL_PROTO"
PROTO_CQ_MACRO = "PROTO_CHONGQING"
CQ_DIR = "ProtocolParser_ChongQing"
LDI_DIR = "LDI"

# ---- CCMRAM 预算表（全部为**实测值**，可 `arm-none-eabi-size -A <obj>` 复测）----
CCM_OTHER = 13644   # 与显示模组无关的固定消费者：静态任务栈/TCB + 各协议队列/帧缓冲 +
                    # dev_display 基类自身存储与对齐（两次实测互证，见 doc/16 §10）
CCM_MM = 2440       # 缅甸模块自身：队列体 84 + cb 80 + 任务帧缓冲 28 + 任务栈 1024 + TCB 100（+对齐）
CCM_DISPLAY = {     # 显示驱动单文件 `.ccmram`（实测；与几何宏联动）
    "dev_display_1_969.c": 38208,      # 192×96（3×3，工作区宏值）
    "dev_display_22_1665.c": 46080,    # 128×64（8×4 = 2026-10-08 用户确认几何）
    "dev_display_1_263.c": 2496,       # 工作区当前 1×1（宏非 224×64）
}
CCM_TOTAL = 65536

FAILED: list[str] = []
WARNS: list[str] = []
CHECKS = 0


def check(name: str, got, want) -> None:
    global CHECKS
    CHECKS += 1
    if got != want:
        FAILED.append(f"{name}: got={got!r} want={want!r}")


def warn(msg: str) -> None:
    WARNS.append(msg)


def flatten(vf: dict, prefix: str = "") -> list[tuple[str, str]]:
    """虚拟文件夹树 → [(虚拟路径, 磁盘路径)]（顺序 = yml 收录序）。"""
    out: list[tuple[str, str]] = []
    for f in vf.get("files") or []:
        real = f["path"]
        out.append((f"{VR}{prefix}{Path(real).name}", real))
    for sub in vf.get("folders") or []:
        out += flatten(sub, prefix + sub["name"] + "/")
    return out


def is_excluded(virtual_path: str, exclude: list[str]) -> bool:
    """镜像 EIDE isExcluded()：条目按**虚拟路径**逐段前缀匹配。"""
    vp = virtual_path[len(VR):] if virtual_path.startswith(VR) else virtual_path
    for e in exclude:
        ev = e[len(VR):] if e.startswith(VR) else e
        if vp == ev or vp.startswith(ev + "/"):
            return True
    return False


def effective(files: list[tuple[str, str]], exclude: list[str]) -> tuple[list[str], list[str]]:
    inc, exc = [], []
    for vp, real in files:
        (exc if is_excluded(vp, exclude) else inc).append(real)
    return inc, exc


def brace_family_dirs() -> set[str]:
    """源码事实：定义 g_brace_proto_guard 的 Application/Src 子目录名（`{` 帧族）。"""
    dirs: set[str] = set()
    for p in sorted((ROOT / "Application/Src").glob("**/*.c")):
        try:
            if GUARD_SYM in p.read_text(encoding="utf-8", errors="ignore"):
                dirs.add(p.parent.name)
        except OSError:
            pass
    return dirs


def emit_make(path: Path, inc: list[str], defines: list[str], display: str | None,
              target: str) -> None:
    """按编译集生成临时 Makefile（改 SRC_APPLICATION / SRC_DISPLAY / DEFINES）。

    display=None（如缅甸目标 = 无显示驱动）时把 `SRC_DISPLAY` **置空**——否则会沿用
    Makefile 默认的 `dev_display_1_263.c`，验证「无显示构建」就失去意义。
    输出目录一并改写为 `.analysis/myanmar/build_nodisp/$(CONFIG)`：等价口径的产物
    属于分析证据，默认不许再落 `build/`（BUILD_DIR 是 `?=`，命令行仍可覆盖）。
    """
    app = [p for p in inc if p.startswith("Application/")]
    src = (ROOT / "Makefile").read_text(encoding="utf-8")
    nodisp_build = ".analysis/myanmar/build_nodisp/$(CONFIG)"
    assert "BUILD_DIR ?= build/$(CONFIG)" in src, "Makefile 里 BUILD_DIR 已不是 `?=` 形式，需同步 emit_make"
    src = src.replace("BUILD_DIR ?= build/$(CONFIG)", f"BUILD_DIR ?= {nodisp_build}")
    start = src.index("SRC_APPLICATION = \\")
    end = src.index("\n\n", start)
    block = "SRC_APPLICATION = \\\n" + " \\\n".join("\t" + p for p in app) + "\n"
    src = src[:start] + block + src[end:]

    # DISP 三选一分支 → 直接钉死 SRC_DISPLAY（含「置空 = 无显示驱动」口径）
    d_start = src.index("DISP ?= 1_263")
    d_end = src.index("INC_DIRS =")
    if display:
        head = f"DISP ?= 1_263\nSRC_DISPLAY = Device/Display/{display}\n\n"
    else:
        head = ("DISP ?= 1_263\n"
                "# 无显示驱动口径：Device/Display 下无任何 dev_display_*.c\n"
                "SRC_DISPLAY =\n\n")
    src = src[:d_start] + head + src[d_end:]

    src = src.replace("DEFINES = -DUSE_HAL_DRIVER -DSTM32F407xx -DSTD_ALL_PROTO",
                      "DEFINES = " + " ".join("-D" + d for d in defines))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(src, encoding="utf-8")
    n_ldi = sum(1 for p in app if f"/{LDI_DIR}/" in p)
    print(f"[emit] {path}（目标 {target}：SRC_APPLICATION {len(app)} 个文件，其中 LDI {n_ldi} 个；"
          f"显示驱动 {display or '无'}；DEFINES {defines}）")
    print(f"[emit] 构建产物目录 = {nodisp_build}（不污染 build/）；复现命令：\n"
          f"       make -f {path} CONFIG=EIDE{target}NoDisp -B -j8   （可用 BUILD_DIR=… 覆盖）")


def main() -> int:
    ap = argparse.ArgumentParser(description="EIDE 目标结构不变量校验 + 编译集/CCM 预算")
    ap.add_argument("-v", "--verbose", action="store_true", help="打印完整编入/排除清单")
    ap.add_argument("--target", default="Myanmar", help="--emit-make 的目标名（默认 Myanmar）")
    ap.add_argument("--emit-make", metavar="PATH", help="生成「某目标等价口径」的临时 Makefile")
    args = ap.parse_args()

    try:
        import yaml
    except ImportError:  # pragma: no cover
        print("需要 PyYAML：pip install pyyaml")
        return 2

    doc = yaml.safe_load(YML.read_text(encoding="utf-8"))
    tgts = doc["targets"]
    files = flatten(doc["virtualFolder"])
    reals = [r for _v, r in files]
    braces = brace_family_dirs()

    print("== EIDE 目标结构不变量 ==")
    print(f"  候选池 files：{len(files)} 个源文件；`{{` 帧族（源码事实）= {sorted(braces)}")
    for want in MM_SRC + [DEV_DISPLAY_BASE]:
        if want not in reals:
            print(f"  ⚠ 候选池缺 {want}（该变体将无法被任何目标选中；仅信息）")

    for name, t in tgts.items():
        x = t.get("excludeList") or []
        inc_list = t["cppPreprocessAttrs"].get("incList") or []
        defs = t["cppPreprocessAttrs"].get("defineList") or []
        inc, exc = effective(files, x)
        # 显示相关
        drv = [p for p in inc if DEV_DISPLAY_RE.match(p)]
        factory = [p for p in inc if p in FACTORY_PAIR]
        brace_inc = sorted({p.split("/")[2] for p in inc
                            if p.startswith("Application/Src/") and p.split("/")[2] in braces})
        legacy = any(i.startswith("Drivers/BSP") for i in inc_list)

        print(f"\n== 目标 {name}{'（历史残留目标：跳过结构断言）' if legacy else ''} ==")
        print(f"  defines : {defs}")
        print(f"  编入 {len(inc)} / 排除 {len(exc)}")
        print(f"  显示模组驱动（{len(drv)}）: {[Path(p).name for p in drv] or '无（无显示驱动口径）'}")
        print(f"  出厂测试（{len(factory)}）: {[Path(p).name for p in factory] or '无'}")
        print(f"  `{{` 帧族（{len(brace_inc)}）: {brace_inc or '无'}")
        if legacy:
            print("  原因：incList 仍含已删除的 Drivers/BSP/* 前缀 ⇒ 该目标不是可用构建入口"
                  "（doc/CLAUDE.md「EIDE Release」条）；只报告，不参与结构断言。")

        if legacy:
            continue

        # ---- I1/I2/I3 显示模组 ----
        check(f"[{name}] I1 显示模组驱动编入数 ≤ 1", len(drv) <= 1, True)
        if not drv:
            print("  · 口径记录：无显示驱动（合法；运行时靠 dev_display_guard 兜底）")
        check(f"[{name}] I3 基类 dev_display.c 编入", DEV_DISPLAY_BASE in inc, True)

        # ---- I4 出厂测试二选一 ----
        check(f"[{name}] I4 出厂测试恰好编入 1 个", len(factory), 1)

        # ---- I5 `{` 帧族互斥（无 STD_ALL_PROTO 时）----
        if STD_ALL_PROTO not in defs:
            check(f"[{name}] I5 `{{` 帧族编入数 ≤ 1（无 {STD_ALL_PROTO}）", len(brace_inc) <= 1, True)
        else:
            print(f"  · {STD_ALL_PROTO} 已定义 ⇒ 允许 `{{` 帧族共存（g_brace_proto_guard 守卫豁免）")

        # ---- I6 PROTO_CHONGQING ⇒ CQ 模块编入 ----
        cq_inc = any(CQ_DIR in p for p in inc)
        if PROTO_CQ_MACRO in defs:
            check(f"[{name}] I6 定义 {PROTO_CQ_MACRO} ⇒ CQ 模块编入", cq_inc, True)
            if any(f"/{LDI_DIR}/" in p for p in inc):
                warn(f"[{name}] W1 定义 {PROTO_CQ_MACRO} 且同编 LDI：默认网络 = CQ 网段 192.168.1.5"
                     f"（C3-C 起 LDI/IAP 上报已同源，无功能矛盾；知悉即可）")
        elif cq_inc:
            warn(f"[{name}] W2 编入 CQ 模块但未定义 {PROTO_CQ_MACRO}：dev 共存口径（合法）")

        # ---- I7 私有 -I 目录齐备 ----
        need = {}
        for p in inc:
            if not p.startswith("Application/Src/"):
                continue
            sub = p.split("/")[2]
            if (ROOT / f"Application/Inc/{sub}").is_dir():
                need.setdefault(sub, True)
        missing_inc = sorted(s for s in need if f"Application/Inc/{s}" not in inc_list)
        check(f"[{name}] I7 编入模块的私有 -I 均在 incList", missing_inc, [])

        # ---- I8 工程宏 ----
        check(f"[{name}] I8 defineList 含 USE_HAL_DRIVER", "USE_HAL_DRIVER" in defs, True)
        check(f"[{name}] I8 defineList 含 STM32F407xx", "STM32F407xx" in defs, True)

        # ---- W3 地面真相（只报信息）----
        bp = ROOT / f"build/{name}/builder.params"
        if bp.exists():
            try:
                src_list = json.loads(bp.read_text(encoding="utf-8"))["sourceList"]
                src_list = {s[2:] if s.startswith("./") else s for s in src_list
                            if s.endswith(".c")}
                miss, extra = sorted(set(inc) - src_list), sorted(src_list - set(inc))
                if miss or extra:
                    print(f"  · 地面真相：与 build/{name}/builder.params 有差异"
                          f"（缺失 {len(miss)} / 多出 {len(extra)}）——EIDE 未按当前 yml 重建时属预期："
                          f"缺失 {miss[:3]} 多出 {extra[:3]}")
                else:
                    print(f"  · 地面真相：与 build/{name}/builder.params 逐条一致 ✓"
                          f"（{len(src_list)} 个 .c）")
            except (OSError, KeyError, json.JSONDecodeError) as e:
                print(f"  · 地面真相：{bp} 解析失败（{e}）——仅信息")
        else:
            print(f"  · 地面真相：未找到 {bp}（仅信息）")

        if args.verbose:
            print(f"  ---- {name} 编入（{len(inc)}）----")
            for p in inc:
                print("   +", p)
            print(f"  ---- {name} 排除（{len(exc)}）----")
            for p in exc:
                print("   -", p)

    # ---- CCMRAM 预算（信息性）----
    print("\n== CCMRAM 预算（信息性）==")
    print(f"  固定消费者（静态任务栈/TCB + 内核缓冲 + dev_display 基类 + 对齐）: {CCM_OTHER} B")
    print(f"  缅甸模块自身（队列 1316 + 任务栈/TCB 1124）                : {CCM_MM} B")
    print(f"  显示驱动                                                   : {CCM_DISPLAY}")
    print(f"  整片 CCMRAM                                                : {CCM_TOTAL} B")
    mm_nodisp = CCM_OTHER + CCM_MM
    for label, used in [
        ("缅甸目标（当前 = 无显示驱动；实测 16084）", mm_nodisp),
        ("缅甸目标 + 1_969（192×96）", mm_nodisp + CCM_DISPLAY["dev_display_1_969.c"]),
        ("缅甸目标 + 22_1665 8×4（128×64）", mm_nodisp + CCM_DISPLAY["dev_display_22_1665.c"]),
        ("Debug 目标换 22_1665 8×4〔推算：实测 52972 − 38208 + 46080〕",
         52972 - CCM_DISPLAY["dev_display_1_969.c"] + CCM_DISPLAY["dev_display_22_1665.c"]),
    ]:
        tag = f"装得下（余 {CCM_TOTAL - used} B）" if used <= CCM_TOTAL \
            else f"**溢出 {used - CCM_TOTAL} B**"
        print(f"  · {label}: {used} B / {CCM_TOTAL} B → {tag}")
    print("  注：EIDE 子集（协议裁剪）与 make 全协议口径的 CCM 占用不同——"
          "make PROTO=ALL/CQ 下 22_1665 8×4 超整片（实测溢出 17700 / 15444 B）。")

    # ---- --emit-make ----
    if args.emit_make:
        name = args.target
        if name not in tgts:
            print(f"[emit] 目标 {name} 不在 yml 中")
            return len(FAILED) + 1
        t = tgts[name]
        inc, _exc = effective(files, t.get("excludeList") or [])
        display = None
        for k in CCM_DISPLAY:
            if any(p.endswith(k) for p in inc):
                display = k
        emit_make(Path(args.emit_make), inc,
                  t["cppPreprocessAttrs"].get("defineList") or [], display, name)

    print(f"\nEIDE 目标结构不变量：通过 {CHECKS - len(FAILED)}/{CHECKS}，失败 {len(FAILED)}")
    for f in FAILED:
        print("  ✗", f)
    for w in WARNS:
        print("  ⚠", w)
    return len(FAILED)


if __name__ == "__main__":
    sys.exit(main())
