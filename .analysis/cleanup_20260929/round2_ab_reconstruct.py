#!/usr/bin/env python3
"""第二轮注释精简的**真 A/B 基线重建**（避免依赖可能是别的口径的旧对象）。

原理：本轮改动全部是「注释块替换」，脚本把本轮**已做的 5 处替换逐条反转**，在
`round2_ab/reconstruct/` 下重建出「改动前」源码副本（代码 token 一字不动），随后用
**与 make 完全相同的编译命令**（从 round2_build 的构建日志里抓取）分别编译
「重建版 / 现行版」，比较对象 section（排除 .debug_*）与反汇编。

用法：python3 round2_ab_reconstruct.py
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
AB = ROOT / ".analysis/cleanup_20260929/round2_ab"
REC = AB / "reconstruct"
LOG_DIR = ROOT / ".analysis/cleanup_20260929/round2_build"

# (文件, [(现行文本, 改动前文本), ...])  —— 与执行时的 StrReplace 逐条对应
EDITS: list[tuple[str, list[tuple[str, str]]]] = [
    ("Application/Src/app_boot.c", [
        (
            """  /* app_test_run() 恒注释（内部 for(;;) 独占屏幕，normal 画面永不绘制；出厂/现场口径
   * 一律不改本文件）。判读表与替代入口（app_test_pixel_scan / app_test_led_mapping、
   * TEST 键出厂检测）见 doc/01_显示系统/22-1665模组驱动分析与迁移记录.md §5 与
   * .analysis/22_1665/README.md。 */""",
            """  /* app_test_run() = 逐像素走位自检（app_test.c，**内部 for(;;) 不会返回**）：
   * 开启即独占屏幕——面板上只显示一个移动的绿点，normal 画面（app_default_display）
   * 永远不会被绘制。**出厂/现场口径必须保持注释**（app_test.h 原文：「正式发布时
   * 移除 app_test_run() 调用即可」）。
   * bring-up / 现场「显示乱」排查不需要改本文件：
   *   ① 出厂检测（TEST 键，app_factory_test.c）——逐次按键走到老化（单个 16x16 字形）
   *      与斜扫（移动对角线），可直接判「内容溢出」还是「映射/时序错」；
   *   ② 需要逐点走位时，把 app_test.c 的 app_test_run() 换成 app_test_pixel_scan()
   *      （全链路）或 app_test_led_mapping()（直接走 hub75_buff = 链序探针），
   *      再取消本行注释。判读表见 doc/01_显示系统/22-1665模组驱动分析与迁移记录.md §5
   *      （现场排障判读）与 `.analysis/22_1665/README.md`（现场判读速查）。 */""",
        ),
        (
            """/* ---- FreeRTOS 内核静态任务缓冲：空闲任务 / 定时器服务任务 → CCMRAM ----
 *
 * 强定义覆盖 cmsis_os2.c 的 `__WEAK` 实现（弱版把 Idle/Timer 的 TCB+栈共 1736B
 * 放在自己的 .bss = SRAM）。账本、地址实证与「CCM 不经 DMA 故可放」的安全性论证见
 * doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md §8.2；
 * 仍未下沉的 timers.c 定时器队列（240B）与 `configUSE_TIMERS=0` 评估见同节。
 *
 * 调用时机：`vTaskStartScheduler()` 内（先空闲任务、后定时器任务），早于 init_task。 */""",
            """/* ---- FreeRTOS 内核静态任务缓冲：空闲任务 / 定时器服务任务 → CCMRAM ----
 *
 * `configSUPPORT_STATIC_ALLOCATION=1`（FreeRTOSConfig.h）⇒ 内核不用 ucHeap 装
 * 「空闲任务」与「定时器服务任务」，改为回调本函数索取静态 TCB + 栈。
 * cmsis_os2.c 里的默认实现是 `__WEAK`，把缓冲定义在**它自己的 .bss（SRAM）**：
 *   Idle_TCB 100 + Idle_Stack 512 + Timer_TCB 100 + Timer_Stack 1024 = 1736B
 * （`nm` 实测落在 0x20007e30 / 0x200079cc 等 SRAM 地址）——SRAM 余量仅数百 B，
 * 这块必须让位。此处以**强定义覆盖**弱符号，把两个任务的 TCB+栈整体改置 CCMRAM。
 *
 * 安全性：CCM 不可被 DMA/ETH 访问，但任务栈/TCB 只被 CPU（上下文切换、函数栈帧）
 * 读写，不经 DMA——与 `pl_task_static.h` 把 23 个应用任务下沉 CCM 同源同理；
 * `.ccmram` 由 `startup.c` 整体清零，满足「静态 TCB 缓冲须已清零」。
 *
 * ⚠ 仍未下沉的部分：`timers.c` 内的定时器队列（`xStaticTimerQueue` +
 * `ucStaticTimerQueueStorage`，实测 0x50+0xA0 = 240B）是 timers.c 的 static 局部，
 * 无法从外部改置；若将来需要再挤 SRAM，可评估 `configUSE_TIMERS=0`
 * （全项目无 `osTimerNew`/`xTimerCreate` 调用点，见 CLAUDE.md「待用户裁决」）。
 *
 * 调用时机：`vTaskStartScheduler()` 内（先空闲任务、后定时器任务），早于 init_task。 */""",
        ),
    ]),
    ("Application/Src/LDI/app_ldi.c", [
        (
            """/**
 * @brief  LDI 上下文装载：Sector1/W25 自愈 + LDI 设备配置装载。
 *
 * 职责：只做配置真源裁定与自愈——网络字段以 Sector1.net_cfg 为唯一真源、W25 为恢复
 * 镜像（三分支判定）；非网络字段（host/lane/cert/modules）与 TCP Client 远端装载，
 * Sector1 空/损坏时从 W25 回写自愈。
 * **不应用网络**：netif / TCP Server 口移交中立模块 app_net_boot（app_boot.c init_task
 * 在本函数之后调用 app_net_boot_apply）。三分支与中间态语义见 doc/07 §10/§11。
 */""",
            """/**
 * @brief  LDI 上下文装载：Sector1/W25 自愈 + LDI 设备配置装载。
 *
 * 职责（2026-08-21 解耦修订）：只做配置真源裁定与自愈——
 * 网络字段以 Sector1.net_cfg 为唯一真源、W25 为恢复镜像（三分支判定），
 * 非网络字段（host/lane/cert/modules）与 TCP Client 远端装载，Sector1 空/损坏
 * 时从 W25 回写自愈。**不再应用网络**：pl_net_set_ip /
 * app_tcp_server_set_port 移交中立模块 app_net_boot（app_boot.c init_task
 * 在本函数之后调用 app_net_boot_apply 统一应用 netif 与 TCP Server 口）。
 */""",
        ),
    ]),
    ("Application/Src/ProtocolParser_ChongQing/app_cq_proto.c", [
        (
            """ * 通道：业务口 CH_ID_UDP_CQ（PROTO_CHONGQING 读 Sector1 net_cfg.udp_port，默认""",
            """ * 通道：业务口 CH_ID_UDP_CQ（PROTO_CHONGQING 读 Sector1 net_cfg.port，默认""",
        ),
        (
            """ * probe 顺序：10011 上 LDI probe 先于本模块（源码收录序），对 CQ 12B 帧
 * `data_len==2` 显式 FAKE 放行、由本模块认领（「LDI 先探测、FAKE 放行、CQ 收单」）
 * ——来历与实态修正见 doc/03 PartB Q23。
 */""",
            """ * probe 注册序：Makefile 收录序 CQ 位于 LDI 之后（initcall 同层按链接序执行），
 * LDI probe 先于 CQ probe。全协议 dev 构建下 10011 口 12B 二进制帧先过 LDI
 * probe——CQ 12B 帧 len=00 00 00 02 且 CRC16-XMODEM 恰好通过 LDI 校验，
 * 2026-08-21 起 LDI probe 对 len==2 显式 FAKE 放行，帧仍由 CQ probe 认领
 * （「LDI 先探测、FAKE 放行、CQ 收单」，无功能受限；修复前 LDI 曾沿身份
 * 校验路径 SKIP 吞掉 CQ 搜索/重启帧，见 doc/03 PartB Q23）。
 */""",
        ),
    ]),
    ("Device/Display/dev_display_22_1703.c", [
        (
            """/**
 * @file    dev_display_22_1703.c
 * @brief   22-1703 模组派生类型 — P10 32x16 双色/全彩（1/4 扫描）
 *
 * 料号 2200001703（列驱动 MBI5124GM 16 通道恒流，行驱动译码器型 ABCD）。
 * 实现 dev_display_ops: prepare (pixel_map→hub75_buff) + scan (BSRR 查表输出)。
 *
 * 几何：单模块 32x16、2 通道/模块（R1G1B1 + R2G2B2）、1/4 扫描；7 模块/行 × 4 模块/列
 * → 224x64、8 通道、显存 14336B ×2（CCMRAM）。
 * 位流（= 源固件 fixBuf 顺序，逐字节等价）：通道 = y/8、扫描行 = y%4、半行 = (y%8>=4)；
 * 每通道 1792 像素、每扫描行 448 = 28 组 × 16B（组 = 上半行 8 个 x + 下半行 8 个 x，顺序），
 * 扫描逐像素步进 448 次、每次取 8 通道各 1 字节（步长 CHANNEL_PIXELS=1792）。
 *
 * 通道序 = 自然序 {R1..R8}、下半行位序 = 顺序：**2026-09-14 任务 Q3 裁决**——以 wyh 分支实机
 * 驱动 `Project-STD-wyh/.../dev_P10_32x16_2200001703.c` 为准（= 源固件口径，宿主穷举
 * 14336/14336 全像素命中）；与已废弃旧 STD `convert_pixelmap_p10`（下半行逆序 + 通道相邻互换）
 * 的差异、源固件逐位对照与差异清单见 `doc/01_显示系统/22-1703模组驱动分析与迁移记录.md` §2/§4。
 *
 * scan 档位 D+：8 通道 24 个数据脚按「同一 GPIO 端口」分 5 组（本 PCB：G/B/E/C/F，由
 * pl_hub75 引脚表推导），每时钟只写 5 次 BSRR（合并同端口多通道的置位/复位位），
 * 替代档位 A 的 24 次写引脚；表在 init 期生成并校验同组同端口，校验失败自动回退通用
 * 逐通道路径（pl_hub75_set_rgb），不静默出错。
 *
 * 拼装宏可改性（代价不同）：`MODULE_ROWS` **可直接改**（GROUPS_PER_LINE / row_dst / 扫描
 * 均自动跟随）；`MODULE_COLS` **改后须同步重做端口分组表与 scan 的通道集合**——它决定
 * 通道数与每通道行覆盖，下方 5 组信号表与 scan 的 c0..c7 按 8 通道写死。
 */""",
            """/**
 * @file    dev_display_22_1703.c
 * @brief   22-1703 模组派生类型 — P10 32x16 双色/全彩（1/4 扫描）
 *
 * 料号 2200001703（P10 32x16，列驱动 MBI5124GM 16 通道恒流，行驱动译码器型 ABCD）。
 * 实现 dev_display_ops: prepare (pixel_map→hub75_buff) + scan (BSRR 查表输出)。
 *
 * 几何（与源固件 9K23881580 `USER/TIMER/timer.h:5-19` 同构）：
 *   单模块 32x16，2 通道/模块（R1G1B1 + R2G2B2），1/4 扫描；
 *   7 模块/行 x 4 模块/列 → 224x64，8 通道，显存 14336B x2（CCMRAM）。
 *
 * 移位链推导（每扫描线每通道 448 bit = 448 时钟）：
 *   1/4 扫 → 每扫描线点亮 4 物理行（y、y+4、y+8、...、y+60 共 16 行？否——
 *   本模组单通道覆盖 8 物理行：上半 4 行 + 下半 4 行，见下）；
 *   通道 = y/8（0~7），扫描行 = y%4，半行 = (y%8 >= 4)；
 *   每通道像素 1792 = 32*16*7/2，每扫描行 1792/4 = 448。
 *
 * hub75_buff 布局（= 源固件 fixBuf 位流顺序，逐字节等价）：
 *   组（GROUP_SIZE=16 字节）= 同一扫描线的 8 个 x（上半行）+ 同 8 个 x（下半行）；
 *   组内偏移 0..7 = 上半行 x%8，偏移 8..15 = 下半行 x%8（顺序，非逆序）；
 *   组号 = (y/8*4 + y%4) * (448/16) + x/8，每扫描行 28 组。
 *   scan 逐像素步进 448 次，每次取 8 通道各 1 字节（步长 CHANNEL_PIXELS=1792）。
 *
 * 映射裁决（2026-09-14，任务 Q3）：以 wyh 分支实机驱动
 * `Project-STD-wyh/Device/Display/dev_P10_32x16_2200001703.c` 为准 ——
 * 通道序为自然序 {R1..R8}（ch = y/8），下半行位序为顺序（偏移 +8），
 * 与源固件 9K23881580 的推导逐位一致（wyh 版为源公式的代数等价改写，
 * 已用宿主穷举比对验证 14336/14336 全像素命中；差异仅在屏体拼装宏
 * MODULE_ROWS：wyh 版 10（320x64）/ 本文件 7（224x64，本订单口径））。
 * 与已删除的旧 STD `convert_pixelmap_p10` 不同：后者下半行逆序 + 通道相邻互换，
 * 本屏体不采用（该实现所在工程/DOM 已废弃）。
 *
 * scan 实现档位 D+（合并写 BSRR，2026-09-14）：把 8 通道 24 个数据脚按
 * 「同一 GPIO 端口」分组（本 PCB 为 5 组：G/B/E/C/F，由 pl_hub75 引脚表推导），
 * 每时钟只写 5 次 BSRR（合并同端口多通道的置位/复位位），替代档位 A 的
 * 24 次写引脚；key 位序按组内信号排列，表在 init 期由 g_hub75_pin_* 生成，
 * 并校验同组信号确在同一端口——校验失败自动回退通用逐通道路径
 * （pl_hub75_set_rgb），不静默出错。
 *
 * 仅改拼装宏即可适配不同屏体，但两者代价不同：
 *   - `_22_1703_MODULE_ROWS`（每行模块数）**可直接改**：`GROUPS_PER_LINE`、`row_dst`
 *     与扫描均自动跟随（组公式按 MODULE_ROWS*4 自适应）；
 *   - `_22_1703_MODULE_COLS`（每列模块数）**改后须同步重做端口分组表**：它决定通道数
 *     （TOTAL_CHANNELS = MODULE_COLS*2）与每通道覆盖行数，下方的 5 组信号表与 scan 的
 *     c0..c7 均按 8 通道写死；通道数变化时需按新引脚分布重排分组与 key 表达式。
 */""",
        ),
    ]),
]


def compile_cmd(rel_c: str) -> list[str]:
    """从 round2_build 任一构建日志里抓取该文件的真实编译命令（-o 换成临时输出）。"""
    pat = re.compile(r"^arm-none-eabi-gcc .* -c -o \S+ " + re.escape(rel_c) + r"$")
    for log in ("all_1263_std.log", "final_all_1263_std.log", "cq_1263.log"):
        p = LOG_DIR / log
        if not p.exists():
            continue
        for line in p.read_text(errors="replace").splitlines():
            if pat.match(line.strip()):
                return line.strip().split()
    raise SystemExit(f"未在构建日志中找到 {rel_c} 的编译命令")


def build_one(rel_c: str, pre_c: Path, tag: str) -> None:
    cmd = compile_cmd(rel_c)
    obj = cmd[cmd.index("-c") + 2]  # -c -o <obj>
    out = REC / f"{tag}.o"
    cmd = [*cmd[:cmd.index("-o") + 1], str(out), *cmd[cmd.index("-o") + 2:]]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        raise SystemExit(f"编译失败：{tag}")


def main() -> int:
    REC.mkdir(parents=True, exist_ok=True)
    print(f"[reconstruct] 重建改动前源码到 {REC}")
    for rel, pairs in EDITS:
        src = (ROOT / rel).read_text(encoding="utf-8")
        pre = src
        for cur, old in pairs:
            if cur not in pre:
                raise SystemExit(f"现行文本未找到（{rel}）：{cur[:60]}…")
            pre = pre.replace(cur, old, 1)
        pre_path = REC / (Path(rel).name + ".pre")
        pre_path.write_text(pre, encoding="utf-8")
        build_one(rel, pre_path, Path(rel).name + ".pre")
        # 现行版同 flags 重编（与 make 产物同口径，避免依赖 build/Debug 里可能是别的口径的对象）
        build_one(rel, ROOT / rel, Path(rel).name + ".cur")
        print(f"  ✔ {rel}：pre/cur 两个对象已生成")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
