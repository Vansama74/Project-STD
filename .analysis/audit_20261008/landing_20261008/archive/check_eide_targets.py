#!/usr/bin/env python3
"""缅甸（MM）EIDE 目标静态校验 + 编译集 / CCMRAM 预算报告。

回答三个问题（不需要打开 EIDE 就能复核；报告随 doc/16 归档）：
  ① `.eide/eide.yml` 里缅甸模块与本目标的结构是否正确（Debug 行为是否被改动）；
  ② 目标**实际编入/排除**了哪些源文件（按 EIDE 的排除语义推导，并用 EIDE 上次构建
     留下的 `build/<target>/builder.params` 做**地面真相对拍**——该文件是 IDE 自己
     写的源文件清单，见 `build/Debug/Project_STD.objlist`）；
  ③ 该目标在 CCMRAM 上是否装得下（分显示模组口径给算术）。

**2026-09-24 第三阶段口径（用户裁决）**：
  · 协议编译集**保持继承 Debug 全集**（LDI / 四川 ETC/MTC/治超 不裁剪）；
  · **不带显示驱动** —— excludeList 排除 `Device/Display/` 下**全部** `dev_display_*.c`
    （含 Debug 口径编入的 `dev_display_1_969.c`）；基类 `dev_display.c` 必须保留
    （注册表 / 守卫 / 通用像素 API 的宿主）；
  · `defineList` **去掉 `PROTO_CHONGQING`**（缅甸不用网络；其余与 Debug 一致）。
  · `targets.Debug` / `files` 保持现状（第三阶段当轮一字未动）。

**2026-09-28 出厂测试二选一轮（本脚本随之扩断言）**：
  · `Application/Src/app_factory_test_mm.c`（缅甸版出厂测试，TEST 键切换 0x0A/0x0B 箭头
    落点）入 `files`；**标准版与缅甸版二选一**（同名对外符号 ⇒ 同时编入 = 链接期
    multiple definition，天然守卫）；
  · Debug 与 Myanmar 目标均**编缅甸版、排除标准版**（`excludeList` 新增
    `<virtual_root>/Application/app_factory_test.c`）；
  · 本脚本新增断言：两文件已入 files、每目标恰好编入一个（= 缅甸版）、排除标准版；
    地面真相对拍对「出厂测试文件」显式放行并只允许「空 / 整对」两种差异
    （EIDE 尚未按新 yml 重建时 = 整对，重建后自动归零）。

**2026-09-24 修复轮（EIDE Debug 构建报错后；本脚本随之改口径）**：
  · EIDE 的回写曾把 `targets.Debug.excludeList` 改坏 —— 丢了「缅甸」排除，
    且误把 `ProtocolParser_ldi` + 四川 ETC/MTC/治超 加入排除（阶段三基线里
    Debug 是**编入**它们的，见 objlist 206 条）。已恢复：Debug 排除缅甸、
    不排除 `ldi` / 四川三协议（= 编入，满足「协议集继承 Debug 全集」）。
  · **兜底**：Debug 与 Release 的 `incList` 也补上 `Application/Inc/ProtocolParser_Myanmar`
    —— 即使将来有人误把缅甸编进某目标，也不会缺 `-I`（本脚本据此断言）。

EIDE 排除语义（源码依据：`~/.cursor/extensions/cl.eide-3.27.2-universal/dist/extension.js`
的 `isExcluded()`）：
    excludeList 条目与其**虚拟路径**逐段比较：`virtual_path == entry || virtual_path.startswith(entry + '/')`
    其中虚拟路径 = `<virtual_root>/` + **虚拟文件夹链** + 文件名；
    虚拟文件夹链**不等于**磁盘路径 —— 例：磁盘 `Application/Src/ProtocolParser_YunNan/app_yn_proto.c`
    的虚拟路径是 `<virtual_root>/Application/protocol/ProtocolParser_YunNan/app_yn_proto.c`。
    这正是 excludeList 里 `Application/protocol/...` 条目能生效的原因（本脚本以 EIDE 上次
    构建产物对拍验证，见 §2）。

用法：
    python3 .analysis/myanmar/check_eide_targets.py            # 校验 + 报告
    python3 .analysis/myanmar/check_eide_targets.py -v         # 加打完整编入/排除清单
    python3 .analysis/myanmar/check_eide_targets.py --emit-make .analysis/myanmar/build_nodisp/Makefile.mm_nodisp
        # 生成「缅甸目标等价口径」的临时 Makefile；生成物里 BUILD_DIR 默认改写为
        #   `.analysis/myanmar/build_nodisp/$(CONFIG)`，产物**不再落 build/**。
        # 在仓库根复现链接：
        #   make -f .analysis/myanmar/build_nodisp/Makefile.mm_nodisp CONFIG=EIDEMyanmarNoDisp -B -j8
        # （BUILD_DIR 为 `?=`，需要换目录时用 `make BUILD_DIR=... ...` 覆盖；
        #   无显示驱动时 SRC_DISPLAY 置空，用于验证「整机无显示」是否装得下）
退出码 = 失败项数（0 = 全绿）。
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

MM_SRC = [
    "Application/Src/ProtocolParser_Myanmar/app_mm_proto.c",
    "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c",
]
MM_VFOLDER = "ProtocolParser_Myanmar"          # 虚拟文件夹名（protocol 下）
MM_INC = "Application/Inc/ProtocolParser_Myanmar"
MM_EXCLUDE_ENTRY = f"{VR}Application/protocol/{MM_VFOLDER}"  # Debug 用它排除缅甸模块
DEV_DISPLAY_BASE = "Device/Display/dev_display.c"            # 基类：任何目标都必须编入
DEV_DISPLAY_RE = re.compile(r"^Device/Display/dev_display_.+\.c$")

# ---- 出厂测试二选一（标准版 / 缅甸版，2026-09-28 新增）----
# 两文件提供同一套对外符号（app_factory_mode_interrupt / app_factory_test_active /
# g_factory_test）⇒ 同时编入必然链接报 multiple definition（天然互斥守卫）；
# 目标侧由 excludeList 恰好排除一个表达「二选一」语义：
#   · 缅甸相关目标（Debug 现行口径 / targets.Myanmar）= 编 MM 版、排标准版；
#   · 标准构建（Makefile 默认 FACTORY=STD）= 编标准版、排 MM 版。
FACTORY_STD = "Application/Src/app_factory_test.c"
FACTORY_MM = "Application/Src/app_factory_test_mm.c"
FACTORY_PAIR = {FACTORY_STD, FACTORY_MM}
FACTORY_EXCLUDE_ENTRY = f"{VR}Application/{Path(FACTORY_STD).name}"  # 排在 Application 虚拟文件夹下

# ---- 本轮基线快照（用于断言「Debug 与 files 保持现状」）----
DBG_DEFINE_LIST = ["USE_HAL_DRIVER", "STM32F407xx", "PROTO_CHONGQING"]
MY_DEFINE_LIST = ["USE_HAL_DRIVER", "STM32F407xx"]           # = Debug − PROTO_CHONGQING

# ---- CCMRAM 预算表（全部为**实测值**，可 `arm-none-eabi-size -A <obj>` 复测）----
CCM_OTHER = 13644   # 缅甸 EIDE 子集里「与显示模组无关」的固定消费者：静态任务栈+TCB
                    # （scan/factory_monitor/frame_dispatch/light_sensor/iap/ldi×2/etc/mtc/ol/
                    #  rs232/rs485 + 内核 idle/timer）、各协议队列/帧缓冲、dev_display 基类
                    #  自身的 scan 静态存储与对齐
                    # —— 由两次实测互证：54292(1_969) − 38208 = 62164(22_1665) − 46080 = 16084，
                    #    再减去缅甸模块自身 2440（队列 1316 + 任务栈/TCB 1124）⇒ 13644
CCM_MM = 2440       # 缅甸模块自身：队列体 84 + cb 80 + 任务帧缓冲 28 + 任务栈 1024 + TCB 100（+对齐）
CCM_DISPLAY = {     # 显示驱动单文件 `.ccmram`（实测；与几何宏联动）
    "dev_display_1_969.c": 38208,      # 192×96（3×3，工作区宏值）
    "dev_display_22_1665.c": 46080,    # 128×64（8×4 = 工作区当前宏值）
    "dev_display_1_263.c": 2496,       # 工作区当前 1×1 实验态（宏非 224×64）
}
CCM_TOTAL = 65536

FAILED: list[str] = []
CHECKS = 0


def check(name: str, got, want) -> None:
    global CHECKS
    CHECKS += 1
    if got != want:
        FAILED.append(f"{name}: got={got!r} want={want!r}")


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


def emit_make(path: Path, inc: list[str], defines: list[str], display: str | None) -> None:
    """按编译集生成临时 Makefile（改 SRC_APPLICATION / SRC_DISPLAY / DEFINES）。

    display=None（缅甸目标 = 无显示驱动）时把 `SRC_DISPLAY` **置空**——否则会沿用
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
                "# 无显示驱动口径（缅甸目标）：Device/Display 下无任何 dev_display_*.c\n"
                "SRC_DISPLAY =\n\n")
    src = src[:d_start] + head + src[d_end:]

    src = src.replace("DEFINES = -DUSE_HAL_DRIVER -DSTM32F407xx -DSTD_ALL_PROTO",
                      "DEFINES = " + " ".join("-D" + d for d in defines))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(src, encoding="utf-8")
    print(f"[emit] {path}（SRC_APPLICATION {len(app)} 个文件，显示驱动 {display or '无'}，"
          f"DEFINES {defines}）")
    print(f"[emit] 构建产物目录 = {nodisp_build}（不污染 build/）；复现命令：\n"
          f"       make -f {path} CONFIG=EIDEMyanmarNoDisp -B -j8")


def main() -> int:
    ap = argparse.ArgumentParser(description="缅甸 EIDE 目标校验 + 编译集/CCM 预算")
    ap.add_argument("-v", "--verbose", action="store_true", help="打印完整编入/排除清单")
    ap.add_argument("--emit-make", metavar="PATH", help="生成缅甸目标等价口径的临时 Makefile")
    args = ap.parse_args()

    try:
        import yaml
    except ImportError:  # pragma: no cover
        print("需要 PyYAML：pip install pyyaml")
        return 2

    doc = yaml.safe_load(YML.read_text(encoding="utf-8"))
    tgt = doc["targets"]
    check("YAML 顶层 targets 含 Debug", "Debug" in tgt, True)
    check("YAML 顶层 targets 含 Myanmar", "Myanmar" in tgt, True)
    check("YAML 顶层 targets 含 Release", "Release" in tgt, True)

    files = flatten(doc["virtualFolder"])
    reals = [r for _v, r in files]
    check("缅甸两个 .c 已入 files", all(p in reals for p in MM_SRC), True)
    check("出厂测试两文件均已入 files（标准版 + 缅甸版；二选一由目标排除表决定）",
          all(p in reals for p in (FACTORY_STD, FACTORY_MM)), True)
    check("Debug incList 含缅甸头文件目录（兜底：误编入也不缺 -I）",
          MM_INC in tgt["Debug"]["cppPreprocessAttrs"]["incList"], True)

    dbg, my = tgt["Debug"], tgt["Myanmar"]
    dbg_x, my_x = dbg["excludeList"], my["excludeList"]
    dbg_def = dbg["cppPreprocessAttrs"]["defineList"]
    my_def = my["cppPreprocessAttrs"]["defineList"]

    # ---- ① Debug 行为不变：缅甸模块被排除；defineList 保持本轮基线 ----
    check("Debug excludeList 含缅甸模块（Debug 编译集不变）", MM_EXCLUDE_ENTRY in dbg_x, True)
    check("Debug defineList 保持现状（本轮基线快照）", dbg_def, DBG_DEFINE_LIST)

    # ---- ② Myanmar 目标：三处差异 ----
    check("Myanmar excludeList 不含缅甸模块", MM_EXCLUDE_ENTRY in my_x, False)
    check("Myanmar excludeList ⊇ Debug 排除集 − 缅甸（其余逐条继承）",
          set(dbg_x) - {MM_EXCLUDE_ENTRY} <= set(my_x), True)
    check("Myanmar excludeList 含云南治超（排除无关协议）",
          any("YunNan_Overload" in e for e in my_x), True)
    check("Myanmar defineList 去掉了 PROTO_CHONGQING", "PROTO_CHONGQING" in my_def, False)
    check("Myanmar defineList = Debug defineList − PROTO_CHONGQING", my_def, MY_DEFINE_LIST)
    check("Myanmar incList 含缅甸头文件目录", MM_INC in my["cppPreprocessAttrs"]["incList"], True)
    check("Myanmar incList = Debug incList（其余逐条继承；Debug 亦已兜底补缅甸）",
          my["cppPreprocessAttrs"]["incList"], dbg["cppPreprocessAttrs"]["incList"])
    check("Myanmar toolchain 同 Debug",
          my["toolchain"], dbg["toolchain"])
    check("Myanmar 链接脚本同 Debug", my["toolchainConfigMap"].keys(), dbg["toolchainConfigMap"].keys())
    # 注：uploader 与 Debug 不同（Debug=Custom / Myanmar=JLink）属第二阶段的既有状态，
    # 本轮按「不回退既有内容」保持现状，只把它钉成基线快照以免被静默改动。
    check("Myanmar uploader 保持既有状态（JLink；与 Debug 的 Custom 不同）",
          my["uploader"], "JLink")

    # ---- ②b 无显示驱动口径：排除 Device/Display 下全部模组驱动，保留基类 ----
    disp_vfiles = [(vp, real) for vp, real in files if DEV_DISPLAY_RE.match(real)]
    check("Device/Display 下模组驱动齐备（files 收录 7 个）", len(disp_vfiles), 7)
    check("Myanmar 排除全部显示模组驱动",
          [real for vp, real in disp_vfiles if not is_excluded(vp, my_x)], [])
    check("Myanmar 未排除 dev_display.c（基类/守卫宿主必须编入）",
          is_excluded(f"{VR}Device/Display/dev_display.c", my_x), False)
    check("Debug 未排除 dev_display_1_969.c（Debug 显示口径保持现状）",
          is_excluded(f"{VR}Device/Display/dev_display_1_969.c", dbg_x), False)

    # ---- ③ 编入/排除清单（虚拟路径语义）----
    dbg_inc, dbg_exc = effective(files, dbg_x)
    my_inc, my_exc = effective(files, my_x)
    check("缅甸模块未被 Debug 编入", any("Myanmar" in p for p in dbg_inc), False)
    check("缅甸模块被 Myanmar 编入", all(p in my_inc for p in MM_SRC), True)
    check("Myanmar 未编入云南治超", any("YunNan_Overload" in p for p in my_inc), False)
    check("Myanmar 未编入 LDI（用户裁决：协议集继承 Debug 全集，含 LDI）",
          any(p.startswith("Application/Src/LDI/") for p in my_inc), True)
    check("Myanmar 未编入四川 ETC（用户裁决：协议集继承 Debug 全集）",
          any("SiChuang_ETC" in p for p in my_inc), True)
    check("Myanmar 未编入四川 MTC（用户裁决：协议集继承 Debug 全集）",
          any("SiChuang_MTC" in p for p in my_inc), True)
    check("Myanmar 未编入四川治超（用户裁决：协议集继承 Debug 全集）",
          any("SiChuang_Overload" in p for p in my_inc), True)
    check("Myanmar 编入集不含任何 dev_display_*.c（整机无显示驱动）",
          [p for p in my_inc if DEV_DISPLAY_RE.match(p)], [])
    check("Myanmar 编入集含 dev_display.c（基类）", DEV_DISPLAY_BASE in my_inc, True)
    check("Myanmar 编入集 = Debug 编入集 + 缅甸 − 全部显示模组驱动",
          sorted(my_inc),
          sorted([p for p in dbg_inc if not DEV_DISPLAY_RE.match(p)] + MM_SRC))
    check("Myanmar 排除集 = Debug 排除集 − 缅甸 + {1_969}",
          sorted(set(my_x)), sorted(set(dbg_x) - {MM_EXCLUDE_ENTRY} |
                                    {f"{VR}Device/Display/dev_display_1_969.c"}))

    # ---- ③b 出厂测试二选一（标准版 / 缅甸版；2026-09-28 新增）----
    for name, inc, exc, x in (("Debug", dbg_inc, dbg_exc, dbg_x),
                              ("Myanmar", my_inc, my_exc, my_x)):
        sel = [p for p in inc if p in FACTORY_PAIR]
        check(f"{name} 出厂测试恰好编入一个（互斥二选一）", len(sel), 1)
        check(f"{name} 出厂测试编入缅甸版（app_factory_test_mm.c）", sel, [FACTORY_MM])
        check(f"{name} 出厂测试排除标准版（app_factory_test.c）",
              FACTORY_STD in exc, True)
        check(f"{name} excludeList 含标准版出厂测试排除条目",
              FACTORY_EXCLUDE_ENTRY in x, True)

    # ---- ④ 与 EIDE 上次构建产物对拍（地面真相）----
    gt_note = "未找到 build/<target>/builder.params（跳过）"
    for name, eff in (("Debug", dbg_inc), ("Myanmar", my_inc)):
        bp = ROOT / f"build/{name}/builder.params"
        if not bp.exists():
            print(f"[地面真相] {name}: {gt_note}")
            continue
        src_list = set(json.loads(bp.read_text(encoding="utf-8"))["sourceList"])
        src_list = {s[2:] if s.startswith("./") else s for s in src_list}
        missing_all = sorted(set(eff) - src_list)
        extra_all = sorted(src_list - set(eff))
        # 出厂测试二选一：EIDE 上次构建（改动前的 yml）编的是标准版，新 yml 编缅甸版 ——
        # 该差异**只在 EIDE 按新 yml 重建前存在**（本次改动未触发 EIDE 重建时必然出现）。
        # 从两条硬断言里显式剔除并单独报告；重建后自动归零。其余文件仍逐条对拍。
        missing = [p for p in missing_all if p not in FACTORY_PAIR]
        extra = [p for p in extra_all if p not in FACTORY_PAIR]
        if missing_all or extra_all:
            print(f"[地面真相] {name}: 出厂测试对账差异（等 EIDE 按新 yml 重建后消失）："
                  f"缺失={missing_all} 多出={extra_all}")
        # EIDE 的 sourceList 只列 .c；本推导同样只含 .c ⇒ 差异应为空或仅余少量模板文件
        print(f"[地面真相] {name}: builder.params 源文件 {len(src_list)} 个；"
              f"推导集 {len(eff)} 个；差集 缺失={missing[:3]} 多出={extra[:3]}")
        check(f"{name} 推导集 ⊆ EIDE 实际编入集（出厂测试文件除外）", missing, [])
        check(f"{name} EIDE 实际编入集 ⊆ 推导集（出厂测试文件除外）", extra, [])
        # 出厂测试维度的差异只允许两种形态：空（已重建）= 整对（待重建）；
        # 半个对（只缺一个 / 只多一个）说明 files/排除表真不一致，本断言会打红。
        check(f"{name} 出厂测试对账差异 ∈ {{空, 整对}}（不再容忍半对）",
              sorted(set(missing_all) | set(extra_all)) in ([], sorted(FACTORY_PAIR)), True)

    # ---- ⑤ CCMRAM 预算（无显示驱动 / 分显示模组口径）----
    def disp_used(inc: list[str]) -> int:
        return sum(v for k, v in CCM_DISPLAY.items() if any(p.endswith(k) for p in inc))

    print("\n== CCMRAM 预算（缅甸 EIDE 目标）==")
    print(f"  固定消费者（静态任务栈/TCB + 内核缓冲 + dev_display 基类 + 对齐）: {CCM_OTHER} B")
    print(f"  缅甸模块自身（队列 1316 + 任务栈/TCB 1124）                : {CCM_MM} B")
    print(f"  显示驱动                                                   : {CCM_DISPLAY}")
    print(f"  整片 CCMRAM                                                : {CCM_TOTAL} B")
    scenarios = [
        ("现行（缅甸目标 = 无显示驱动）", CCM_OTHER + CCM_MM),
        ("若改用 1_969（历史继承口径）", CCM_OTHER + CCM_MM + CCM_DISPLAY["dev_display_1_969.c"]),
        ("若改用 22_1665 8×4", CCM_OTHER + CCM_MM + CCM_DISPLAY["dev_display_22_1665.c"]),
    ]
    for label, used in scenarios:
        tag = "链接通过（余 %d B）" % (CCM_TOTAL - used) if used <= CCM_TOTAL \
            else "**链接期溢出 %d B**" % (used - CCM_TOTAL)
        print(f"  · {label}: {used} B / {CCM_TOTAL} B → {tag}")
    check("缅甸目标（无显示）CCM 预算不溢出", CCM_OTHER + CCM_MM <= CCM_TOTAL, True)

    if args.verbose:
        print("\n== Myanmar 目标编入（%d）==" % len(my_inc))
        for p in my_inc:
            print("  +", p)
        print("== Myanmar 目标排除（%d）==" % len(my_exc))
        for p in my_exc:
            print("  -", p)

    if args.emit_make:
        display = None
        for k in CCM_DISPLAY:
            if any(p.endswith(k) for p in my_inc):
                display = k
        emit_make(Path(args.emit_make), my_inc,
                  my["cppPreprocessAttrs"]["defineList"], display)

    print(f"\n缅甸 EIDE 目标校验：通过 {CHECKS - len(FAILED)}/{CHECKS}，失败 {len(FAILED)}")
    for f in FAILED:
        print("  ✗", f)
    return len(FAILED)


if __name__ == "__main__":
    sys.exit(main())
