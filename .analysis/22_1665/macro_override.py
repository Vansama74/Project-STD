#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""22-1665 几何宏「换口径」源码替换工具（第三十轮新增）。

**为什么需要它**：第三十轮起几何宏改成**兄弟驱动同款普通 `#define`**（不再用 `#ifndef`
守卫）——命令行 `-D_22_1665_MODULE_ROWS=2` 只会得到「重定义告警 + **文件值胜出**」，
静默测错口径。凡换口径一律改为**源码替换**：把源码里那行 `#define NAME (V)` 换成目标值，
生成临时副本再编译。`-D` 只对仍带 `#ifndef` 守卫的宏（本文件仅
`_22_1665_CHAIN_HEAD_IS_MODULE0`）有意义。

**做法**：定位行首 `#define NAME <值> <其余>`（值 = 括号表达式或单个非空白 token），
只替换值、保留尾随注释与缩进；原值带 `U` 后缀时新值也补 `U`（保持无符号语义，
避免 `-Wsign-compare` 之类的新告警）。

用法（CLI；两种写法都收）：
    python3 macro_override.py <源.c> <目标.c> NAME=VAL [NAME=VAL ...]
    python3 macro_override.py <源.c> <目标.c> -DNAME=VAL ...

给宿主脚本用的接口（`import macro_override`）：
    `materialize(src, over, tag)` —— `over` 里的**几何宏**走源码替换、其余宏原样返回
    （调用方按 `-D` 传给编译器，例：`_22_1665_CHAIN_HEAD_IS_MODULE0` 仍带 `#ifndef` 守卫）。

失败即非零退出（宏不在源码里 / 参数格式错 / 读写失败），不静默降级。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


def _define_re(name: str) -> re.Pattern[str]:
    """匹配「行首 `#define NAME <值> <其余>`」；`<值>` = 括号表达式或单个非空白 token。"""
    return re.compile(
        rf"^(?P<pre>#[ \t]*define[ \t]+{re.escape(name)}[ \t]+)"
        rf"(?P<val>\([^)\n]*\)|[^\s/]+)(?P<post>.*)$",
        re.M,
    )


def apply_overrides(text: str, overrides: dict[str, int]) -> str:
    """把 `overrides`（NAME → 十进制/十六进制整数值）逐个替换进源码文本。"""
    for name, value in overrides.items():
        m = _define_re(name).search(text)
        if m is None:
            raise SystemExit(f"macro_override：源码里找不到 `#define {name}`，无法源码替换")
        keep_u = m.group("val").endswith(("U", "u"))
        new_val = f"({value}{'U' if keep_u else ''})"
        text = text[: m.start()] + m.group("pre") + new_val + m.group("post") + text[m.end():]
    return text


def variant(src: str | Path, dest: str | Path, overrides: dict[str, int]) -> Path:
    """读 `src`、按 `overrides` 替换宏值、写到 `dest`（自动建父目录）。"""
    text = Path(src).read_text(encoding="utf-8")
    out = Path(dest)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(apply_overrides(text, overrides), encoding="utf-8")
    return out


def parse_kv(items: list[str]) -> dict[str, int]:
    """`NAME=VAL` / `-DNAME=VAL` → {NAME: int(VAL)}（`VAL` 支持 0x 前缀）。"""
    out: dict[str, int] = {}
    for item in items:
        key, sep, val = item.partition("=")
        if not sep:
            raise SystemExit(f"macro_override：参数 `{item}` 不是 NAME=VAL 形式")
        if key.startswith("-D"):
            key = key[2:]
        out[key.lstrip("-")] = int(val, 0)
    return out


# 第三十轮起几何宏是**普通 `#define`**（兄弟驱动同款）⇒ 命令行 `-D` 只会得到
# 「重定义告警 + 文件值胜出」；这四个宏一律走源码替换。
GEOMETRY_MACROS = (
    "_22_1665_MODULE_ROWS",
    "_22_1665_MODULE_COLS",
    "_22_1665_MODULE_PIXEL_ROW",
    "_22_1665_MODULE_PIXEL_COL",
)

_TMP_ROOT = Path("/tmp/22_1665_macro_override")


def split_macros(over: dict[str, int]) -> tuple[dict[str, int], dict[str, int]]:
    """`over` →（几何宏覆盖，其余宏覆盖）。前者必须源码替换，后者按 `-D` 传。"""
    src_over = {k: v for k, v in over.items() if k in GEOMETRY_MACROS}
    defines = {k: v for k, v in over.items() if k not in GEOMETRY_MACROS}
    return src_over, defines


def materialize(src: str | Path, over: dict[str, int], tag: str) -> tuple[Path, dict[str, int]]:
    """返回（可编译的源码路径，该按 `-D` 传的宏）。几何宏无覆盖时原样返回 `src`。"""
    src_over, defines = split_macros(over)
    if not src_over:
        return Path(src), defines
    dest = _TMP_ROOT / f"{tag}_{Path(src).name}"
    return variant(src, dest, src_over), defines


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__.split("用法（CLI", 1)[-1].strip(), file=sys.stderr)
        return 2
    variant(argv[0], argv[1], parse_kv(argv[2:]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
