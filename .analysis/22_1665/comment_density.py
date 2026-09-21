#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""C 源码「注释密度」粗分工具（第三十四轮新增，报告用，不参与固件构建）。

对文件做三件事（不依赖编译器，纯词法扫描：识别 // 与 /* */、跳过字符串/字符字面量）：
  · 整文件行数 / 空行 / 纯注释行 / 带尾注释的代码行 / 纯代码行；
  · 注释字符数 ÷（代码字符数）—— 去掉注释与空白后的有效代码字符数；
  · 「注释密度」= 注释字符数 ÷ 源码总字符数。

用法：python3 .analysis/22_1665/comment_density.py <file.c> [more.c ...]
"""

from __future__ import annotations

import pathlib
import sys


def scan(text: str) -> tuple[int, int, int, int, int]:
    """返回 (总行数, 空行, 纯注释行, 带尾注释的代码行, 纯代码行)"""
    lines = text.splitlines()
    total = len(lines)
    blank = cmt_only = code_only = code_cmt = 0
    in_block = False
    for raw in lines:
        i, n = 0, len(raw)
        has_code = has_cmt = False
        while i < n:
            ch = raw[i]
            if in_block:
                has_cmt = True
                if raw.startswith("*/", i):
                    in_block = False
                    i += 2
                    continue
                i += 1
                continue
            if raw.startswith("//", i):
                has_cmt = True
                break
            if raw.startswith("/*", i):
                has_cmt = True
                in_block = True
                i += 2
                continue
            if ch in "\"'":
                quote, has_code = ch, True
                i += 1
                while i < n:
                    if raw[i] == "\\":
                        i += 2
                        continue
                    if raw[i] == quote:
                        i += 1
                        break
                    i += 1
                continue
            if not ch.isspace():
                has_code = True
            i += 1
        if has_code and has_cmt:
            code_cmt += 1
        elif has_code:
            code_only += 1
        elif has_cmt:
            cmt_only += 1
        else:
            blank += 1
    return total, blank, cmt_only, code_cmt, code_only


def stats(text: str) -> tuple[int, int]:
    """返回 (注释字符数, 有效代码字符数)"""
    cmt = code = 0
    i, n = 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            cmt += j - i
            i = j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            cmt += j - i
            i = j
            continue
        ch = text[i]
        if ch in "\"'":
            quote = ch
            i += 1
            while i < n:
                if text[i] == "\\":
                    code += 2
                    i += 2
                    continue
                if text[i] == quote:
                    code += 1
                    i += 1
                    break
                code += 1
                i += 1
            continue
        if not ch.isspace():
            code += 1
        i += 1
    return cmt, code


for path in sys.argv[1:]:
    p = pathlib.Path(path)
    text = p.read_text(encoding="utf-8")
    total, blank, cmt_only, code_cmt, code_only = scan(text)
    cmt_chars, code_chars = stats(text)
    print(f"== {path} ==")
    print(f"  行：总 {total} = 空行 {blank} + 纯注释行 {cmt_only} + 尾注释代码行 {code_cmt} + 纯代码行 {code_only}")
    print(f"  字符：注释 {cmt_chars} / 有效代码 {code_chars} → 注释字符占比 {cmt_chars / (cmt_chars + code_chars):.1%}"
          f"，注释/代码 = {cmt_chars / code_chars:.2f}")
