#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""一次性：把现行 `dev_display_22_1665.c` 逆向还原为**第三十轮改动前**（= 第二十九轮
收口形态，468 行）源码——撤销本轮的两处编辑：① 文件头瘦身（49 行 → 16 行）；
② 四个几何宏由 `#ifndef` 守卫改普通 `#define`；另含 ③ 一条注释指针的措辞调整。

用途：零回归机器级 A/B 的「改动前」基准（`-O3` 同命令行编译后比 section md5 / 反汇编）。
产物 = `archive/round30/dev_display_22_1665_pre_round30.c`（md5 记在 round30 报告里）。

**本脚本是逆向工具，不是构建脚本**：一旦驱动再被改动，断言会失败——那时不要改本脚本，
直接把已归档的 `dev_display_22_1665_pre_round30.c` 当作基准（它才是冻结件）。
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CUR = ROOT / "Device/Display/dev_display_22_1665.c"
OUT = HERE / "archive/round30/dev_display_22_1665_pre_round30.c"

NEW_HEAD = """/**
 * @file    dev_display_22_1665.c
 * @brief   22-1665 模组派生类型 — 16x16 红绿双色 / 静态单扫（MBI5034B 多链）
 *
 * 料号 2200001665：列驱动 MBI5034B（16 通道恒流 + 16 位移位寄存器，8 片 / 链段 = 128 位），
 * 静态单扫（`scan_lines = 1`，不用行址线）。实现 dev_display_ops: prepare (pixel_map→
 * 帧状态数组) + scan (合并写 BSRR 查表) + set_row (行址恒 0)。
 *
 * 几何：单模块 16x16（4 条数据线 × 128 位 = 512 颗 LED）；接线 = 每列模块一组 4 根数据脚
 * （组 g = 兄弟驱动通道对 2g / 2g+1 的 R/G 脚，各组并行、每帧 128 × MODULE_ROWS 时钟）。
 * **改屏体尺寸只改下方「模组参数」一节的 MODULE_ROWS / MODULE_COLS 两个宏**（其余全派生）。
 *
 * 详细说明（几何 / 接线与技术史 / 数据流 / 落点规则 / CCM 组成 / 开关 / 选编 / 标定与历史轮次）
 * 见 `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md` §0（as-built）与附录 A（历史）；
 * 宿主脚本索引与「换口径」做法见 `.analysis/22_1665/README.md`。
 */"""

OLD_HEAD = """/**
 * @file    dev_display_22_1665.c
 * @brief   22-1665 模组派生类型 — 16x16 红绿双色 / 静态单扫（MBI5034B 多链）
 *
 * 料号 2200001665：列驱动 MBI5034B（16 通道恒流 + 16 位移位寄存器，8 片 / 链段 = 128 位），
 * 静态单扫（`scan_lines = 1`，不使用行址线）。实现 dev_display_ops：prepare（pixel_map →
 * 帧状态数组）+ scan（合并写 BSRR 查表输出）+ set_row（行址恒 0）。
 *
 * 几何（改屏体尺寸只改「模组参数」一节的两个模块数宏，其余全部派生）：
 *   单模块 16x16 像素 = 256 像素（256 红 die + 256 绿 die = 512 颗 LED）；
 *   每模块 4 条链段（上半 R/G + 下半 R/G），每条链段 = 8 片 × 16 位 = 128 位；
 *   屏面 = (MODULE_ROWS × MODULE_PIXEL_ROW) 宽 × (MODULE_COLS × MODULE_PIXEL_COL) 高。
 *
 * 接线（**唯一形态 = 每列一口独立数据线**；2026-09-17 现场判定 + 确认显示正常，第二十九轮固化）：
 *   每个模块列（= 一个 HUB 口）各占一组 4 根数据脚，组 g 取兄弟驱动（1_263 / 22_1703）的
 *   通道对 2g / 2g+1 —— 组 0 = R1/G1/R2/G2（PG9/PG10/PG15/PB6，现场逐脚定标）、
 *   组 1 = R3/G3/R4/G4（PB8/PB9/PE1/PE2）；各组并行（共享 CLK/LAT/OE），组内横向模块
 *   同线级联，每帧 128 × MODULE_ROWS 个时钟；MODULE_ROWS = 1 时每组一块 ⇒ HUB1 显逻辑
 *   上半、HUB2 显逻辑下半（合起来才是整屏）。详规见 doc/01 §0.3/§0.10。
 *   历史形态「同线级联（模型 A）」的代码路径与 A/B 开关 `-D_22_1665_HUB_WIRING` 已于
 *   第二十九轮删除——现场验证完毕、驱动正常，不再需要可选路径；删除前源码归档
 *   `.analysis/22_1665/archive/finalize/driver_pre_finalize_20260917.c`（恢复路径见
 *   同目录 README 与 `.analysis/22_1665/README.md`）。
 *
 * 数据流：prepare 逐组逐时钟位 p 合成状态字节 `state[组][p]`（**bit j = 组内第 j 条数据线的
 * 电平**；红线段取像素 R 位、绿线段取 G 位，故黄 = 两条都亮）→ scan 逐位按组查合并写表写
 * 各端口 BSRR（1×2 时每时钟 4 次：G/B + B/E）→ 数据建立裕量 → CLK 脉冲；帧末由基类在
 * OE 消隐窗口内打 LAT。
 *
 * 落点规则（单模块内，现场反解、逐位确认，唯一模型）：
 *   链位 p' → 片 c = p'/16、片内级序 i = p'%16；区域块栅格 blk_cols = w/4；
 *   bc = (blk_cols-1) - (c % blk_cols)、br = c / blk_cols；块内 dx = 3-(i%4)，
 *   dy = ((i/4)+2)%4；屏面 (X,Y) = (x0 + 4*bc + dx, y0 + 4*br + dy)。
 *
 * CCMRAM（通式 **1408·M + 256·MODULE_COLS**；1×1 = 1664B、1×2 = 3328B；上限 16KB 由
 * `_Static_assert` 守卫）：
 *   chain_dst 2×4M×128 + pixel_map 256M + 帧状态数组 128M + 合并写表 8×2×16×组数。
 *
 * 开关宏（`#ifndef` 守卫，可命令行 `-D` 覆盖）：仅 `_22_1665_CHAIN_HEAD_IS_MODULE0`
 * （组内横向链首口径；默认 0 ⇒ 链首装组内最后一块，MODULE_ROWS = 1 时无影响）。
 * 选编：显示模组三选一，由 Makefile `DISP=22_1665` / EIDE excludeList 表达（本文件不含选编开关）。
 *
 * 引脚定标与历史轮次见 `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`（§0 = as-built、
 * 附录 A = 历史）；标定用旧驱动与探针脚本已归档 `.analysis/22_1665/archive/prev_round/`，
 * 如需再标定（换屏 / 换接线）从那里取回。
 */"""

NEW_GEOM = """/* ---- 屏体尺寸（**唯一几何入口**，与兄弟驱动同款普通 `#define`，改值即改口径；
 *      换口径脚本用「源码替换」改这几行——普通 `#define` 下命令行 `-D` 会被文件值静默覆盖；
 *      非法值由下方「编译期防御」的 `_Static_assert` 拦下）---- */
#define _22_1665_MODULE_ROWS      (1U) /* 每行模块数（水平）→ 屏宽 = MODULE_ROWS × MODULE_PIXEL_ROW */
#define _22_1665_MODULE_COLS      (5U) /* 每列模块数（垂直）→ 屏高 = MODULE_COLS × MODULE_PIXEL_COL */
#define _22_1665_MODULE_PIXEL_ROW (16U) /* 单模块每行像素数（16×16 面板；块栅格 4×4 对齐） */
#define _22_1665_MODULE_PIXEL_COL (16U) /* 单模块每列像素数 */"""

OLD_GEOM = """/* ---- 屏体尺寸（**唯一几何入口**；可 `-D` 覆盖，改动后由「编译期防御」把关）---- */
#ifndef _22_1665_MODULE_ROWS
#define _22_1665_MODULE_ROWS (1U) /* 每行模块数（水平）→ 屏宽 = MODULE_ROWS × MODULE_PIXEL_ROW */
#endif
#ifndef _22_1665_MODULE_COLS
#define _22_1665_MODULE_COLS (5U) /* 每列模块数（垂直）→ 屏高 = MODULE_COLS × MODULE_PIXEL_COL */
#endif
#ifndef _22_1665_MODULE_PIXEL_ROW
#define _22_1665_MODULE_PIXEL_ROW (16U) /* 单模块每行像素数（16×16 面板；块栅格 4×4 对齐） */
#endif
#ifndef _22_1665_MODULE_PIXEL_COL
#define _22_1665_MODULE_PIXEL_COL (16U) /* 单模块每列像素数 */
#endif"""

SW_NEW = "/* ---- 行为开关（**保留 `#ifndef` 守卫**：现场可命令行 `-D` 覆盖；上方几何宏为普通 `#define`）---- */"
SW_OLD = "/* ---- 行为开关（可命令行 `-D` 覆盖）---- */"

CHK_NEW = "/** @brief 链位 p' 的屏面落点 → pixel_map 偏移（落点规则见 `doc/01` §0.3） */"
CHK_OLD = "/** @brief 链位 p' 的屏面落点 → pixel_map 偏移（默认落点规则，见文件头） */"

text = CUR.read_text(encoding="utf-8")
for new, old in ((NEW_HEAD, OLD_HEAD), (NEW_GEOM, OLD_GEOM), (SW_NEW, SW_OLD), (CHK_NEW, CHK_OLD)):
    assert text.count(new) == 1, new[:60]
    text = text.replace(new, old)
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(text, encoding="utf-8")
print(f"[ok] 复原改动前源码 → {OUT}（{len(text.splitlines())} 行）")
