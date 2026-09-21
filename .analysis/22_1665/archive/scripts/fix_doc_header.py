#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 22-1665 记录文档的标题/状态行改为「第十二轮 = 反解模型」，并指向 §15。"""
import pathlib

DOC = pathlib.Path("/home/yystation/Program/3833024/Project-STD-main/doc/01_显示系统/22-1665模组驱动分析与迁移记录.md")
text = DOC.read_text(encoding="utf-8")

pairs = [
    (
        "# 22-1665 模组驱动（16×16 红绿双色 / 静态扫描 / MBI5034B 单链）分析与迁移记录",
        "# 22-1665 模组驱动分析与迁移记录\n\n"
        "> **现行模型（2026-09-17 第十二轮起）**：**16 列 × 8 行单色 LED 栅格（128 点、1 位/像素）/ 静态扫描 / 单链 128 位 = 8 片 MBI5034B**，\n"
        "> 由现场两条实测观测**反解出的唯一解**（§15）；旧的「16×16 红绿双色 / 512 位 / 8 个校准开关」推定模型已删除\n"
        "> （全文备份 `.analysis/22_1665/prev_round/`）。§1~§14 保留为**历史推导记录**，凡与 §15 冲突处以 §15 为准。",
    ),
    (
        "**状态**：`[已落地]`",
        "**状态**：`[**第十二轮（2026-09-17 下午）：现场两条观测反解出真实接线 → 模型整体替换（16×8 / 128 位 = 8 片 / 颜色＝非黑即亮 / 探针 0..6）→ 见 §15**]` `[已落地]`",
    ),
]

for old, new in pairs:
    if old in text:
        text = text.replace(old, new, 1)
        print(f"[OK]   {old[:40]}…")
    else:
        print(f"[MISS] {old[:40]}…")

DOC.write_text(text, encoding="utf-8")
print("done")
