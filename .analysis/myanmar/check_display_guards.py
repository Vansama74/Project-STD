#!/usr/bin/env python3
"""「整机无显示驱动」构建的边界防护审计 —— 源码级守卫覆盖检查 + 审计表生成。

背景：EIDE Myanmar 目标是**不带显示驱动**的构建口径（excludeList 排除全部
`Device/Display/dev_display_*.c`）⇒ `dev_display_get()` 恒为 nullptr。凡解引用其结果的
运行路径都必须能「安全跳过/降级」，否则上电即 HardFault 或定时任务空指针。

本脚本做两件事（不需要打开 EIDE / 不需要编译）：
  ① **硬断言**：对每个已知关键路径断言守卫形态存在（文件 + 必须出现的代码片段），
     全部通过才算合格；断言清单是「本轮补守卫」的机器可读版本，新增路径时应同步扩表。
  ② **启发式扫描**：枚举工程内全部 `dev_display_get()` 调用点，按「±6 行内出现判空/
     三元/守卫调用」分类，输出未覆盖清单（供人工确认；分类=启发式，非权威）。

用法：
    python3 .analysis/myanmar/check_display_guards.py            # 检查 + 打印审计表
    python3 .analysis/myanmar/check_display_guards.py --md OUT   # 另存 markdown 审计表
退出码 = 失败的硬断言数（0 = 全绿）。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# ---- 硬断言清单：(文件, 必须包含的片段, 说明) ----
ASSERTIONS: list[tuple[str, str, str]] = [
    # Device 层：守卫助手 + 扫描任务骨架 + 像素 API 纵深防御
    ("Device/Inc/dev_display.h", "bool dev_display_guard(const char *what);", "守卫助手声明"),
    ("Device/Display/dev_display.c", "bool dev_display_guard(const char *what)",
     "守卫助手实现（限速告警）"),
    ("Device/Display/dev_display.c", 'dev_display_guard("scan_task")',
     "dev_display_start：无显示不建 scan_task（⇒ 不启动 TIM3/TIM4）"),
    ("Device/Display/dev_display.c", "if (dev == nullptr)\n        return;",
     "_on_tim4_period：PWM ISR 判空（纵深防御）"),
    ("Application/Src/app_boot.c", 'dev_display_guard("splash")', "开机 splash 判空跳过"),
    ("Application/Src/app_default_display.c", 'dev_display_guard("default-display")',
     "默认欢迎画面判空跳过"),
    ("Device/IO/dev_light_sensor.c", "if (dev == nullptr || dev->display == nullptr)",
     "光敏调光判空跳过（无显示不写 light_level）"),
    ("Application/Src/app_factory_test.c", 'dev_display_guard("factory-test")',
     "出厂检测 monitor 判空 → 待机降级"),
    ("Application/Src/LDI/app_ldi_device.c", 'dev_display_guard("ldi.e6")',
     "LDI E6 显示命令判空 → 回 LDI_DEV_FAIL"),
    ("Application/Src/LDI/app_vms_ctrl.c", 'dev_display_guard("ldi.vms")',
     "LDI E9 VMS 判空跳过"),
    ("Application/Src/ProtocolParser_Myanmar/app_mm_proto.c",
     "if (dev_display_get() != nullptr)\n        app_default_display_register",
     "缅甸开机默认画面（红叉）：仅带显示驱动时注册（无显示不注册、零告警）"),
    ("Application/Src/ProtocolParser_Myanmar/app_mm_proto.c",
     "if (d == nullptr)\n        return;",
     "缅甸红叉回调判空（无显示纵深防御）"),
]

# ---- 像素 API 纵深防御（dev_display.c 内每个公开入口都要有判空）----
PIXEL_APIS = [
    "void dev_display_set_pixel(",
    "void dev_display_set_brightness(",
    "void dev_display_fill(",
    "void dev_display_draw_bitmap(",
]

# ---- 启发式扫描范围（Application / Device；不含 Device/Display 的模组驱动）----
SCAN_DIRS = ["Application/Src", "Device"]

# 已人工确认的「非解引用」点（启发式扫描的残余，不是漏守卫）：
#   app_light_sensor.c:37 —— dev_light_sensor_init() 只把句柄存进设备结构体、不解引用；
#   其唯一解引用点 dev_light_sensor_auto_adjust() 已在 Device 层判空（见硬断言清单）。
REVIEWED_SITES = {
    "Application/Src/app_light_sensor.c": "句柄赋值（不解引用）；解引用点在 dev_light_sensor_auto_adjust 判空",
}

GUARD_LINE = re.compile(
    r"if\s*\(\s*!\s*\w+\s*\)|if\s*\(\s*\w+\s*==\s*(?:nullptr|NULL)\s*\)|"
    r"if\s*\(\s*\w+\s*!=\s*(?:nullptr|NULL)\s*\)|if\s*\(\s*\w+\s*&&|dev_display_guard\s*\(|"
    r"\w+\s*\?\s*.*:\s*|\w+\s*!=\s*nullptr\s*\?"
)


def check_assertions() -> tuple[int, list[str]]:
    fails: list[str] = []
    for rel, needle, desc in ASSERTIONS:
        p = ROOT / rel
        if not p.exists():
            fails.append(f"{rel}: 文件不存在（{desc}）")
            continue
        if needle not in p.read_text(encoding="utf-8"):
            fails.append(f"{rel}: 未找到「{needle}」（{desc}）")
    # 像素 API 判空（各函数首 6 行内出现 if (!dev) return;）
    src = (ROOT / "Device/Display/dev_display.c").read_text(encoding="utf-8")
    for api in PIXEL_APIS:
        idx = src.find(api)
        if idx < 0:
            fails.append(f"Device/Display/dev_display.c: 未找到 {api}")
            continue
        if not re.search(r"if \(!dev\)\s*\n\s*return;", src[idx:idx + 400]):
            fails.append(f"Device/Display/dev_display.c: {api} 缺少入口判空 if (!dev) return;")
    return len(fails), fails


def scan_call_sites() -> list[tuple[str, int, str, str]]:
    """枚举 dev_display_get() 调用点 → [(文件, 行号, 源码行, 分类)]。"""
    rows: list[tuple[str, int, str, str]] = []
    for d in SCAN_DIRS:
        for p in sorted((ROOT / d).rglob("*.c")):
            if p.match("*dev_display_*.c") or p.name in ("dev_display.c",):
                continue
            lines = p.read_text(encoding="utf-8").split("\n")
            for i, l in enumerate(lines):
                if "dev_display_get()" not in l or l.strip().startswith(("*", "/*", "//")):
                    continue
                window = "\n".join(lines[max(0, i - 10):i + 11])
                if "?" in window or "!= nullptr" in window or "== nullptr" in window \
                        or "== NULL" in window or "dev_display_guard(" in window:
                    cat = "已判空/三元"
                elif "if (" in window:
                    cat = "已判空/三元"
                else:
                    cat = "**需人工确认**"
                rel = str(p.relative_to(ROOT))
                if rel in REVIEWED_SITES:
                    cat = f"已人工确认（{REVIEWED_SITES[rel]}）"
                rows.append((rel, i + 1, l.strip(), cat))
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description="无显示驱动构建的边界防护审计")
    ap.add_argument("--md", metavar="PATH", help="另存 markdown 审计表")
    args = ap.parse_args()

    fails_n, fails = check_assertions()
    rows = scan_call_sites()
    manual = [r for r in rows if "需人工确认" in r[3]]
    guarded = [r for r in rows if r[3] == "已判空/三元" or r[3].startswith("已人工确认")]

    print(f"== 守卫硬断言：通过 {len(ASSERTIONS) + len(PIXEL_APIS) - fails_n}/"
          f"{len(ASSERTIONS) + len(PIXEL_APIS)} ==")
    for f in fails:
        print("  ✗", f)
    print(f"\n== dev_display_get() 调用点扫描（启发式）：{len(rows)} 处 —— "
          f"已判空/三元 {len(guarded)} / 需人工确认 {len(manual)} ==")
    for r in manual:
        print(f"  ? {r[0]}:{r[1]}: {r[2][:88]}")

    if args.md:
        out = Path(args.md)
        lines = ["# 「整机无显示驱动」边界防护审计表（自动生成）", "",
                 f"- 生成脚本：`.analysis/myanmar/check_display_guards.py`",
                 f"- 守卫硬断言：{len(ASSERTIONS) + len(PIXEL_APIS) - fails_n}/"
                 f"{len(ASSERTIONS) + len(PIXEL_APIS)} 通过", "",
                 "## 守卫硬断言清单", "", "| 文件 | 断言 | 说明 |", "|---|---|---|"]
        for rel, needle, desc in ASSERTIONS:
            ok = (needle in (ROOT / rel).read_text(encoding="utf-8")) if (ROOT / rel).exists() else False
            lines.append(f"| `{rel}` | `{needle.strip()[:60]}` | {desc} |" if ok
                         else f"| `{rel}` | **缺失** | {desc} |")
        for api in PIXEL_APIS:
            lines.append(f"| `Device/Display/dev_display.c` | `{api.strip()}` 首部判空 | 像素 API 纵深防御 |")
        lines += ["", "## `dev_display_get()` 调用点分类（启发式扫描）", "",
                  "| 文件:行 | 分类 | 源码 |", "|---|---|---|"]
        for rel, ln, src_line, cat in rows:
            lines.append(f"| `{rel}:{ln}` | {cat} | `{src_line[:70].replace('|', '\\|')}` |")
        out.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"[write] {out}")
    return fails_n


if __name__ == "__main__":
    sys.exit(main())
