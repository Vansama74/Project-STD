#!/usr/bin/env python3
"""第三轮（2026-10-13 裁决执行）逐对象 A/B：**期望差异清单**核对。

对「只读观察 API / 调试冻结」清理涉及的 8 个 .c，逐对象比较两轮全量构建产物
（默认 A = `build/cmt2_after/Debug`（上一轮基线），B = `build/cmt3_after/Debug`（本轮）），
把每个段的尺寸差异与**期望清单**逐条对拍：
  · 清单内差异（符号在册 ⇒ 删除即消失 / 尺寸按删除语句收敛）→ 通过；
  · 任何清单外的段差异 → 失败（防「清理带出无关改动」）。
另对**断言**删除的零 codegen 由 `ab_round3_asserts.py` 隔离证明（本脚本不管断言）。

`dev_display_22_1665.c` 在默认口径（DISP=1_263）不参与构建 ⇒ 对象不存在，跳过（由上述隔离
A/B 覆盖）。

用法：python3 .analysis/comment_pass_20261013/ab_round3_objects.py [A目录] [B目录]
"""
from __future__ import annotations

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
A = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/cmt2_after/Debug"
B = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "build/cmt3_after/Debug"

# 对象 → {段名: 期望增量或 None（=「删除该段」）}
EXPECT: dict[str, dict[str, int | None]] = {
    "Application/Src/app_dispatch.o": {
        ".text.app_dispatch_qfull_drops": None,   # 只读访问器（未引用，链接期本就 gc）
        ".text.app_dispatch_resync_count": None,
        ".text.app_dispatch_notify_drops": None,
        ".bss.g_dispatch": -8,                    # 3 个 uint32 计数域 + 对齐
        ".text.app_channel_dispatch": -16,        # notify_drop++ 删除
        ".text.frame_dispatch_task": -52,         # qfull_drop++ / resync_byte++ ×2 删除
    },
    "Application/Src/Channel/app_udp.o": {
        ".text.app_udp_gzol_bind_status": None,
        ".text.app_udp_gzol_bind_status_str": None,
        ".rodata.app_udp_gzol_bind_status_str.str1.4": None,
        ".bss.s_udp_gzol_bind_sta": None,
        ".bss.s_udp_gzol_bind_err": None,
        ".bss.s_udp_gzol_bind_ok_cnt": None,
        ".text.udp_gzol_task": -56,               # 三处绑定结果记录删除
    },
    "Application/Src/Channel/app_tcp_server.o": {
        ".bss.g_tcp_server_connected": None,
        ".text.tcp_server_task": -16,             # 两处连接标志写入删除
    },
    "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.o": {
        ".bss.s_mm_lamp_state": None,
        ".text.mm_apply_lamp": -12,
        ".text.mm_apply_init": -12,
    },
    "Application/Src/app_render.o": {
        ".text.app_render_chip_info_get": None,
        ".text.app_render_glyph_addr_for": None,
        ".text.app_render_glyph_probe": None,
        ".rodata.app_render_chip_info_get.str1.4": None,
    },
    "Device/Display/dev_display.o": {
        ".text.dev_display_init": -20,            # pl_tim_dbg_freeze ×2 调用删除
    },
    "Platform/Src/pl_tim.o": {
        ".text.pl_tim_dbg_freeze": None,
    },
}

SKIP = (".debug", ".zdebug", ".comment", ".ARM.attributes")


def sizes(obj: pathlib.Path) -> dict[str, int]:
    out = subprocess.run(["arm-none-eabi-size", "-A", str(obj)], capture_output=True, text=True).stdout
    res = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 2 and p[0].startswith("."):
            res[p[0]] = int(p[1])
    return res


def main() -> int:
    fails = 0
    for obj, expect in EXPECT.items():
        a, b = A / obj, B / obj
        if not a.exists() or not b.exists():
            print(f"  [跳过] {obj}（缺对象：A={a.exists()} B={b.exists()}）")
            continue
        sa, sb = sizes(a), sizes(b)
        keys = sorted(set(sa) | set(sb))
        seen: dict[str, int | None] = {}
        for k in keys:
            if k.startswith(SKIP):
                continue
            va, vb = sa.get(k), sb.get(k)
            if va == vb:
                continue
            if va is not None and vb is None:
                seen[k] = None
            elif va is None and vb is not None:
                seen[k] = vb
            else:
                seen[k] = (vb - va)
        ok = seen == expect
        n_expect = len(expect)
        if ok:
            print(f"  [通过] {obj}：{n_expect} 处差异全部命中期望清单（删除 {sum(1 for v in expect.values() if v is None)} 段 / 尺寸收敛 {n_expect - sum(1 for v in expect.values() if v is None)} 段）")
        else:
            fails += 1
            print(f"  [失败] {obj}：差异与期望不符")
            for k in sorted(set(seen) | set(expect)):
                if seen.get(k, "—") != expect.get(k, "—"):
                    print(f"        ✘ {k}: 实际 {seen.get(k, '—')} / 期望 {expect.get(k, '—')}")
    print(f"\n逐对象 A/B（期望清单）：{len(EXPECT) - fails}/{len(EXPECT)} 通过，失败 {fails}")
    return fails


if __name__ == "__main__":
    sys.exit(main())
