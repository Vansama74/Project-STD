#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""第二十二轮：把 doc/CLAUDE.md 里 22_1665 的超长条目压缩为「现行状态 + 指向 doc/01 的指针」。

按行号定点替换（长行内容不便于 StrReplace）；处理前先断言目标行含预期关键词，避免错位误伤。
用法：python3 .analysis/22_1665/_round22_compress_claude_md.py
"""

from __future__ import annotations

import pathlib
import sys

DOC = pathlib.Path(__file__).resolve().parents[3] / "doc/CLAUDE.md"

NEW_MUTEX_ROW = """| 2-dev | dev | `dev_display_22_1665_init` | `dev_display_22_1665.c` | **22-1665 模组实例注册** —— **16×16 红绿双色 / 静态单扫 / 多链独立位流**（4 链 × 128 位 = 每链 8 片 MBI5034B，共 512 LED）；链表 `_22_1665_chains[]` 现行 = **R1/G1（上半红/绿）+ R2/G2（下半红/绿）**（第二十轮现场探针 8 逐脚定标；B1/B2/A/B/C/D 六根实测无链）；每时钟位**并行喂**（状态字节 + 按端口合并 BSRR）；`_22_1665_MULTI_CHAIN = 0` 一键回退 16×8 单链兼容口径；本模组 CCM **1792B**（兼容 560B、探针 8/9 态 1888B）；**2026-09-17 现场确认显示正常**（`prepare` 逐链掩码缺陷已于第二十一轮修复）。**as-built / 开关 / RTT 判读表 / 探针 7/8/9 用法 / 历史轮次见 `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` §0 与附录 A**；宿主脚本索引 `.analysis/22_1665/README.md`） |"""

NEW_MODULE_ROW = """| **`dev_display_22_1665.c`**（`2200001665`） | **16×16 双色**（4 链 × 128 位 = 512 LED = 256 像素 × 2 die；**链表**每链一行 = 脚 / 角色 / 区域 / 镜像旗标，现行 = **R1/G1（上半红/绿）+ R2/G2（下半红/绿）**、`flags` 恒等；兼容口径回退 **16×8 单链**，其 `_22_1665_BLK_COLS` 1/2/4/8 调屏形 4×32 / 8×16 / 16×8 / 32×4） | **静态 1 扫**，**每条链 128 位 = 8 片 MBI5034B**；换屏标定用探针 7（逐脚标识）/ 8（逐脚闪烁+纹理）/ 9（逐链×逐脚内容扫描），步骤见 doc/01 §0.3 | **1792B**（`_22_1665_chain_dst[4][128]` 1024 + pixel_map 256 + 帧状态数组 128 + `g_bsrr_tab[3][16]` 384；兼容 560B；探针 8/9 态 1888B） | **EIDE Debug 现行编入本文件**（1_263 / 22_1703 均在其 excludeList 内）；`make DISP=22_1665` 选编；**2026-09-17 现场确认显示正常**（as-built 见 doc/01 §0） |"""

NEW_MAKEFILE_BULLET = """- **Makefile**：`DISP ?= 1_263`（默认，保持改动前行为）/ `DISP=22_1703`（P10 订单）/ **`DISP=22_1665`（16×16 红绿双色多链，2026-09-17 现场调通）**——三模组 CCM 硬约束不可同编、非法值 `$(error)`；`DISP` 值进口径指纹 `.build_stamp`，切口径必然全量重编 + 重链接。**22_1665 现行基线**（`PROTO=ALL` `DISP=22_1665`，2026-09-17 第二十二轮整理后）：text **172460** / rodata **202896** / data 1672 / ccmram **10460**（本模组 **1792B**）/ bss 126452 / SRAM 合计 **130688**（余 **384B**）/ 内嵌 tree **`c0b61e2f`**；兼容口径（`-D_22_1665_MULTI_CHAIN=0`）ccmram 9228、探针 8/9 态 10556 —— 四口径均编译 + 链接通过、本文件零告警（全项目仅 HAL 3 条预存）；scan 热路径 **33 指令/时钟位** ≈ 25~35µs/帧（TIM3 500µs 周期）。**as-built 见 `doc/01` §0，历史轮次见同文档附录 A**。"""

NEW_SIZE_BULLET = """- **22_1665 历史段尺寸（追溯用）**：第一~二十一轮逐轮的 text/rodata/ccmram/md5/tree 与各宿主脚本通过数，已随轮次细节移交 `doc/01` 附录 A 与 `.analysis/22_1665/archive/`（各轮报告 + `build_round*.log`）；**现行基线见上一条**。"""

NEW_EIDE_BULLET = """- **EIDE Debug（当前 `.eide/eide.yml` 实态，2026-09-17 复核）**：**显示模组编 22_1665**（`dev_display_1_263.c` / `dev_display_22_1703.c` / `p20` / `1_969` / `1_260` / `1_577` 均在 `targets.Debug.excludeList` 内；`dev_display_22_1665.c` 在 files 内且未排除 —— 几何固定 16×16，`_22_1665_MULTI_CHAIN=1`）；`defineList` 含 `PROTO_CHONGQING`（CQ 口径 netcfg 默认 192.168.1.5 / 9528 / 20103）；**LDI 目录未排除（编入）**；协议侧按 excludeList 排除 `ProtocolParser_{ShanDong,YunNan,YunNan_Overload,ChongQing,QingHai,GuiZhou,GuiZhou_Overload}` + `rls` + `ah`（**excludeList 条目是虚拟路径，与 virtualFolder 树对应即生效**）；**云南治超（YN_OL）已收录进 virtualFolder 与 incList，但同列 excludeList（默认不编入）**——如需在 EIDE 编入，须从 excludeList 移除本目录**并同时排除任一其它 `{` 帧族目录（`ProtocolParser_SiChuang_MTC` / `ProtocolParser_YunNan`）**（`{` 帧族 `g_brace_proto_guard` 互斥，链接期 `multiple definition` 兜底；「云南常规 × 云南治超」已 `ld -r` 实测报 multiple definition → 量产必二选一）；故 EIDE Debug 编译集 ≈ IAP + LDI + 四川三协议 + 安徽 + **22_1665** + `APP_DIAG_BANNER` 横幅（EIDE 无 Makefile `DIAG_DEFS` → 横幅的 `tree=`/口径字段打占位值，几何与端口行照常输出；横幅 `driver linked?` 行只探 1_263/22_1703 弱符号，22_1665 下打 0——**用户裁决 2026-09-16 不补 22_1665 弱符号**）。"""

NEW_DISP_BULLET = """  - **`DISP ?= 1_263`**（2026-09-14 新增，2026-09-16 扩第三值，2026-09-17 第二十二轮复核）：显示模组三选一——
    `1_263`（默认，P6 32×32）、`22_1703`（P10 32×16 / 料号 2200001703，1/4 扫描）、
    **`22_1665`（16×16 红绿双色 / 4 链 × 8 片 MBI5034B / 静态单扫）**。
    **`22_1665` 已于 2026-09-17 现场调通（用户确认显示正常）**：四条链 = **R1(上半红) / G1(上半绿) / R2(下半红) / G2(下半绿)**
    （第二十轮探针 8 逐脚定标；B1/B2/A/B/C/D 六根实测无链）；「红口只上半屏 / 绿口全黑」的真根因 =
    `prepare` 逐链掩码表达式写错（第二十一轮一行修复）。本模组默认 CCM **1792B**
    （`chain_dst[4][128]` 1024 + pixel_map 256 + frame_state 128 + `g_bsrr_tab[3][16]` 384；兼容口径 `-D_22_1665_MULTI_CHAIN=0` = 560B）、
    scan 热路径 33 指令/时钟位 ≈ 25~35µs/帧（TIM3 500µs）。
    **as-built（几何 / 链表 / 开关 / RTT 判读 / 构建基线 / 遗留项）见 `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` §0**；
    第一~二十一轮演进、探针 7/8/9 用法与作废结论见同文档附录 A；宿主脚本（`verify_all.sh` 一键入口 + 索引）
    见 `.analysis/22_1665/README.md`。
    三模组 CCMRAM 显存 + CQ 6377B + 贵州治超 1136B + 云南治超 1152B **不可同编**（同编 = .ccmram 溢出或双实例注册、
    活动屏由链接序决定）；非法值 make 直接 `$(error)` 报错。EIDE 侧等价语义由 `targets.Debug.excludeList` 表达
    （见「选编口径」；**22_1665 已收录进 EIDE 并选为 Debug 编译口径**）。"""


def main() -> int:
    lines = DOC.read_text(encoding="utf-8").splitlines(keepends=True)

    # (行号 1-based, 断言关键词, 新内容)
    plan = [
        (867, "EIDE Debug（当前", NEW_EIDE_BULLET),
        (866, "22_1665 实测段尺寸", NEW_SIZE_BULLET),
        (864, "**Makefile**：`DISP ?= 1_263`", NEW_MAKEFILE_BULLET),
        (578, "`dev_display_22_1665.c`", NEW_MODULE_ROW),
        (320, "`dev_display_22_1665_init`", NEW_MUTEX_ROW),
        (10, "`DISP ?= 1_263`", NEW_DISP_BULLET),
    ]

    ok = True
    for lineno, needle, _ in plan:
        if needle not in lines[lineno - 1]:
            print(f"[FAIL] 第 {lineno} 行不含预期关键词：{needle!r}")
            print("       实际开头：" + lines[lineno - 1][:120])
            ok = False
    if not ok:
        return 1

    for lineno, _, new_text in plan:
        lines[lineno - 1] = new_text + "\n"

    # 内存布局节的 22_1665 实测数字补齐（子串替换）
    old_sub = "`PROTO=ALL` `DISP=22_1665` SRAM 130688B（余 384B）/ CCM **10460B**"
    new_sub = ("`PROTO=ALL` `DISP=22_1665` text **172460** / rodata **202896** / data 1672 / "
               "SRAM 130688B（余 384B）/ CCM **10460B**（本模组 1792B；第二十二轮整理后基线，tree `c0b61e2f`）")
    text = "".join(lines)
    if old_sub in text:
        text = text.replace(old_sub, new_sub)
        print("[ok] 内存布局节的 22_1665 段尺寸已补齐")
    else:
        print("[WARN] 未找到内存布局节的 22_1665 段尺寸子串（可能已被改过，跳过）")

    DOC.write_text(text, encoding="utf-8")
    print(f"[ok] doc/CLAUDE.md 压缩完成：{len(lines)} 行")
    return 0


if __name__ == "__main__":
    sys.exit(main())
