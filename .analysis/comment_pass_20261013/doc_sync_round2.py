#!/usr/bin/env python3
"""续接轮（2026-10-13 第二轮）文档同步：编译期断言清理相关的文档指针/表述更新。

只改「当前口径」表述与计数；历史修订记录行不动（doc/18 §2 第 5 条）。
每个替换要求恰好命中一次。
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

EDITS: list[tuple[str, str, str]] = [
    # ---- doc/08 协议模块接入规则（payload ≤ RB 规则与骨架/清单）----
    ("doc/08_协议模块接入规则/README.md",
     "6. **帧队列静态分配**（`StaticQueue_t`），深度惯例 2~4；payload 上限必须 ≤ 对应 RB 容量并用 `_Static_assert` 锁死。",
     "6. **帧队列静态分配**（`StaticQueue_t`），深度惯例 2~4；payload 上限必须 ≤ 对应 RB 容量，并在 payload 宏处留一行契约注释（含越限后果；业务代码不用编译期断言，见 `doc/18_注释规范.md` §2）。"),
    ("doc/08_协议模块接入规则/01_通用规则与模块骨架.md",
     '_Static_assert(X_PAYLOAD_MAX <= RB_SIZE_RS485, "X frame must fit RS485 RB");\n'
     '_Static_assert(X_PAYLOAD_MAX <= RB_SIZE_RS232, "X frame must fit RS232 RB");',
     "/* 契约：payload 上限须 ≤ 串口 RB 容量（越限被 RB 截断，静默收错帧） */"),
    ("doc/08_协议模块接入规则/01_通用规则与模块骨架.md",
     "- **payload 上限 ≤ RB 容量且 static_assert**：QH（259 ≤ 768）、SC_ETC（149）、SC_MTC（74）、SC_OL（255）均有 `_Static_assert`；RLS（530）只 assert RS485。网络侧 IAP（1044）/LDI（512）≤ RB_SIZE_RJ45（1536），依赖 `RB_SIZE_RJ45` 注释契约（`app_dispatch.h:37`）。**新模块一律补 static_assert**。",
     "- **payload 上限 ≤ RB 容量**：QH（259 ≤ 768）、SC_ETC（149）、SC_MTC（74）、SC_OL（255）、RLS（530，仅 RS485）在 payload 宏处留一行契约注释；网络侧 IAP（1044）/LDI（512）≤ RB_SIZE_RJ45（1536），依赖 `RB_SIZE_RJ45` 注释契约（`app_dispatch.h:37`）。**新模块一律留该契约注释；业务代码不用编译期断言**（`doc/18_注释规范.md` §2）。"),
    ("doc/08_协议模块接入规则/01_通用规则与模块骨架.md",
     "- payload 上限 > RB 容量且无 static_assert → 队列越界风险。",
     "- payload 上限 > RB 容量且无契约注释 → 队列越界风险。"),
    ("doc/08_协议模块接入规则/04_接入检查清单.md",
     "1. 五步注册（`01 §2`），队列静态分配 + `_Static_assert(payload ≤ RB 容量)`（`01 §4`）。",
     "1. 五步注册（`01 §2`），队列静态分配 + payload 宏一行契约注释（≤ RB 容量，`01 §4`；不用编译期断言，见 `doc/18_注释规范.md` §2）。"),
    ("doc/08_协议模块接入规则/04_接入检查清单.md",
     "- [ ] 静态队列 + static_assert(payload ≤ RB)",
     "- [ ] 静态队列 + payload ≤ RB 契约注释"),
    # ---- 各协议文档 ----
    ("doc/03_重庆高速二代费显协议/PartB_ProtocolParser_ChongQing接入.md",
     "| 28 | CCM 队列越界 | `app_cq_proto.c` | OK：`_Static_assert(CQ_PAYLOAD_MAX <= RB_SIZE_RJ45)`；",
     "| 28 | CCM 队列越界 | `app_cq_proto.c` | OK：`CQ_PAYLOAD_MAX ≤ RB_SIZE_RJ45`（源码契约注释）；"),
    ("doc/11_云南费显协议/README.md",
     "（静态 SRAM 801B + cb 80B）；`_Static_assert` ≤ RB 768。",
     "（静态 SRAM 801B + cb 80B）；payload ≤ RB 768（源码契约注释）。"),
    ("doc/12_缅甸费显项目适配/03_软件实现方案.md",
     "（静态 SRAM 队列，同云南规模，`_Static_assert` ≤ RB 768）。",
     "（静态 SRAM 队列，同云南规模，payload ≤ RB 768 契约注释）。"),
    ("doc/13_安徽费显协议/README.md",
     "`ANHUI_QUEUE_DEPTH 3`；`_Static_assert` ≤ RB 768。",
     "`ANHUI_QUEUE_DEPTH 3`；payload ≤ RB 768（源码契约注释）。"),
    # ---- doc/16 缅甸（计数 + 断言表述）----
    ("doc/16_缅甸2D费显协议/README.md",
     "> 宿主推演：`.analysis/myanmar/mm_frame_sim.py`（**157 用例通过 / 失败 0**——2026-09-29 代码瘦身轮复跑计数；",
     "> 宿主推演：`.analysis/myanmar/mm_frame_sim.py`（**158 用例通过 / 失败 0**——2026-10-13 全仓编译期断言清理后，原「定长 20 字节编译期断言」检查改为「契约注释存在 + 宿主侧复算」，净 +1；2026-09-29 代码瘦身轮复跑计数为 157；"),
    ("doc/16_缅甸2D费显协议/README.md", "`[宿主推演 157/157]`", "`[宿主推演 158/158]`"),
    ("doc/16_缅甸2D费显协议/README.md",
     "帧常量（含 `_Static_assert(MM_FRAME_LEN == 20U)`）",
     "帧常量（定长 20B 契约注释）"),
    ("doc/16_缅甸2D费显协议/README.md",
     "（**当前 157/157**——瘦身轮删除 RTT 断言后由 159 收敛为 157，见 `.analysis/cleanup_20260929/report.md`；",
     "（**当前 158/158**——2026-10-13 编译期断言清理后原断言存在性检查改「契约注释 + 宿主复算」，净 +1；此前瘦身轮删除 RTT 断言后由 159 收敛为 157，见 `.analysis/cleanup_20260929/report.md`；"),
    ("doc/16_缅甸2D费显协议/README.md",
     "- [x] 静态队列 + `_Static_assert(payload ≤ RB_SIZE_RS232)`",
     "- [x] 静态队列 + payload ≤ RB_SIZE_RS232（契约注释）"),
    ("doc/16_缅甸2D费显协议/README.md",
     "- 脚本：`mm_frame_sim.py` **157/157**（**2026-09-29 瘦身轮由 159 收敛为 157**，见本文件顶部横幅）、",
     "- 脚本：`mm_frame_sim.py` **158/158**（**2026-10-13 编译期断言清理后净 +1；此前瘦身轮由 159 收敛为 157**，见本文件顶部横幅）、"),
    # ---- doc/19 新疆（计数 + §11 扩展）----
    ("doc/19_新疆费显协议/README.md",
     "- **宿主推演**：`python3 .analysis/xinjiang/xj_frame_sim.py`（**329/329 通过**；§10 = PDF 示例帧逐帧用例）",
     "- **宿主推演**：`python3 .analysis/xinjiang/xj_frame_sim.py`（**331/331 通过**；§10 = PDF 示例帧逐帧用例；\n  2026-10-13 编译期断言清理后原 7 条断言存在性检查改「契约注释 + 宿主复算」两类，净 +2）"),
    ("doc/19_新疆费显协议/README.md",
     "| `.analysis/xinjiang/xj_frame_sim.py` | **329/329 通过**：",
     "| `.analysis/xinjiang/xj_frame_sim.py` | **331/331 通过**（2026-10-13 编译期断言清理后，原 7 条断言存在性检查改「契约注释 + 宿主复算」）："),
    ("doc/19_新疆费显协议/README.md",
     "`.analysis/comment_pass_20261013/report.md`）。全项目其它同类检查清单（供决定是否扩展）见同报告。",
     "`.analysis/comment_pass_20261013/report.md`）。全项目扩展见本文件 §11.1。"),
    # ---- doc/07 布局锁定表述（当前设计口吻的两处；历史修订记录行不动）----
    ("doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md",
     "5. **布局锁定**：在 `app_board_net_cfg.h`（唯一归属）用 `static_assert` 锁死 `sizeof` / `offsetof` 与 magic 值，",
     "5. **布局锁定**：在 `app_board_net_cfg.h`（唯一归属）以 §15 布局表 + 头文件契约注释锁死 `sizeof` / `offsetof` 与 magic 值，"),
    ("doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md",
     "**记录布局（三固件二进制兼容，各自 static_assert 锁定）**：",
     "**记录布局（三固件二进制兼容；本表为唯一口径，源码留契约注释）**："),
    # ---- doc/CLAUDE.md ----
    ("doc/CLAUDE.md",
     "布局由头文件 `static_assert` 锁定（与 Bootloader/Recovery 二进制兼容）。",
     "布局以 `app_board_net_cfg.h` 注释 + `doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md` §15 为唯一口径（与 Bootloader/Recovery 二进制兼容）。"),
    ("doc/CLAUDE.md",
     "不 WAIT；`_Static_assert(XJ_FRAME_LEN_MAX < 220)` 锁此前提）**、",
     "不 WAIT；帧上限 < 220（源码契约注释）锁此前提）**、"),
    ("doc/CLAUDE.md",
     "宿主推演 `.analysis/myanmar/mm_frame_sim.py` **159 用例通过 / 失败 0**",
     "宿主推演 `.analysis/myanmar/mm_frame_sim.py` **158 用例通过 / 失败 0**"),
    # ---- .analysis 索引 ----
    ("doc/CLAUDE.md",
     "- **宿主推演**：`.analysis/xinjiang/xj_frame_sim.py` **329/329**",
     "- **宿主推演**：`.analysis/xinjiang/xj_frame_sim.py` **331/331**"),
    (".analysis/myanmar/README.md",
     "python3 .analysis/myanmar/mm_frame_sim.py                    # 协议推演，当前 157/157",
     "python3 .analysis/myanmar/mm_frame_sim.py                    # 协议推演，当前 158/158"),
    (".analysis/myanmar/README.md",
     "）。**当前 157/157**（阶段一 77 → 阶段二 103 → 极性轮 108 → 厂测轮 142 → 红叉轮 159 → **2026-09-29 瘦身轮收敛为 157**〔删 4 条 RTT 日志断言、换 2 条更强断言〕；",
     "）。**当前 158/158**（阶段一 77 → 阶段二 103 → 极性轮 108 → 厂测轮 142 → 红叉轮 159 → **2026-09-29 瘦身轮收敛为 157**〔删 4 条 RTT 日志断言、换 2 条更强断言〕→ **2026-10-13 编译期断言清理后 158**〔原「定长 20 字节编译期断言」改「契约注释存在 + 宿主复算」〕；"),
]


def main() -> int:
    fail = 0
    for rel, old, new in EDITS:
        path = ROOT / rel
        text = path.read_text(encoding="utf-8")
        n = text.count(old)
        if n != 1:
            print(f"✘ {rel}: 命中 {n} 次（期望 1）——{old[:50]!r}")
            fail = 1
            continue
        path.write_text(text.replace(old, new, 1), encoding="utf-8")
        print(f"✔ {rel}: {old[:40]!r}…")
    print(f"文档替换 {len(EDITS)} 处；失败 {fail}")
    return fail


if __name__ == "__main__":
    sys.exit(main())
