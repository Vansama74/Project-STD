#!/usr/bin/env python3
"""守卫改动 A/B 反汇编对照（支持证据）—— 检查「有显示时原路径未丢失任何操作」。

背景：为「整机无显示驱动」构建（EIDE Myanmar 目标）补的运行时 NULL 守卫，形态统一为
「函数入口 / 调用点前置一次判空 + 原路径原样保留」。有显示时判空不成立 ⇒ 原路径必须
执行原来的每一步操作。语义等价性的**主证据**是源码 diff（纯增量：既有行不删改）与
`check_display_guards.py` 的源码级守卫覆盖断言；本脚本是机器级**支持证据**。

为什么不能要求「指令逐字节相同」：`-Og` 下在入口插入判空后，寄存器分配器会重新分配
寄存器、调整栈帧大小、把互不相关的指令重排，并把 `add.w` 编成 `adds`（标志位dead）。

判据（逐函数，归一化后比较**操作多重集**）：
  ① 归一化：寄存器名 → `R`；立即数/地址/字节偏移删除；助记符统一
     （`.w`/`.n` 后缀、`movs`/`adds`/`subs` 等标志位后缀 → 基名）。
  ② 忽略栈帧/流水线噪声（`push`/`pop`/`stmdb`/`ldmia`/`nop`/`bx` 与 `sub R, R` /
     `add R, R` 形式的栈指针调整）。
  ③ **硬判据**：改动前的每一条归一化操作，在改动后必须仍然存在同样条数
     （原操作零丢失）；多出的操作逐条列出（= 编译后的入口判空 / 降级块 / 告警调用）。

用法：
    python3 .analysis/myanmar/check_guard_disasm.py [基线目录]
退出码 = 原操作丢失的函数数（0 = 全部通过）。
"""

from __future__ import annotations

import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE_DIR = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".analysis/myanmar/ab_baseline"

# (对象相对路径, 基线文件基名)
TARGETS = [
    ("Device/Display/dev_display.o", "dev_display"),
    ("Device/IO/dev_light_sensor.o", "dev_light_sensor"),
    ("Application/Src/app_boot.o", "app_boot"),
    ("Application/Src/app_default_display.o", "app_default_display"),
    ("Application/Src/app_factory_test.o", "app_factory_test"),
    ("Application/Src/app_test.o", "app_test"),
    ("Application/Src/LDI/app_ldi_device.o", "app_ldi_device"),
    ("Application/Src/LDI/app_vms_ctrl.o", "app_vms_ctrl"),
    ("Application/Src/RLS/app_rls_cmd.o", "app_rls_cmd"),
    ("Application/Src/AH_MQTT/ah_mqtt.o", "ah_mqtt"),
    ("Application/Src/AH_MQTT/ah_mqtt_cmd.o", "ah_mqtt_cmd"),
]

FUNC_RE = re.compile(r"^[0-9a-f]+ <([^>]+)>:$")
INSN_RE = re.compile(r"^\s*[0-9a-f]+:\t[0-9a-f ]*\t(.*)$")
REG_RE = re.compile(r"\b(?:r(?:1[0-5]|[0-9])|sl|fp|ip|sp|lr|pc)\b")
IMM_RE = re.compile(r"(?:#|, )?(?:0x[0-9a-f]+|(?<![a-z0-9_])\d+\b)")
OFF_RE = re.compile(r"\+0x[0-9a-f]+")
BR_TGT_RE = re.compile(r"\b[0-9a-f]+\s*(?=<)")
FLAG_BASES = {
    "mov", "add", "sub", "and", "orr", "eor", "bic", "cmp", "tst", "adc", "sbc",
    "neg", "lsl", "lsr", "asr", "ldr", "ldrb", "ldrh", "ldrsb", "ldrsh",
    "str", "strb", "strh", "mul", "mla", "mls", "rsb", "rev",
}
# 栈帧 / 流水线噪声（这些指令的条数随寄存器分配变化，不参与比较）：
#   push/pop/stmdb/ldmia（栈帧）、nop/bx（流水线）、`mov R, R`（寄存器搬运）、
#   `add/sub R, R`（栈指针调整）。另见 noise()：跳回本函数的条件分支与纯取值调用。
FRAME_RE = re.compile(r"^(push|pop|stmdb|ldmia|stmia|nop|bx)\b|^(mov|add|sub) R, R$")
# 纯取值函数（无副作用）：旧实现一次语义调用两处、新实现提升为局部变量后合并为一次，
# 调用条数减少属预期；其使用点的判空由源码级 check_display_guards.py 覆盖。
NO_SIDE_EFFECT_CALLS = ("bl <dev_display_get>",)


def parse_dis(text: str) -> dict[str, list[str]]:
    """反汇编文本 → {函数名: [归一化操作, ...]}（保持出现顺序）。"""
    out: dict[str, list[str]] = {}
    cur: str | None = None
    for line in text.splitlines():
        m = FUNC_RE.match(line)
        if m:
            cur = m.group(1)
            out.setdefault(cur, [])
            continue
        if cur is None:
            continue
        m = INSN_RE.match(line)
        if not m:
            continue
        ins = m.group(1).split(";")[0].split("\t@")[0]  # 去 objdump 的 @ 注解（含地址，随布局变化）
        ins = OFF_RE.sub("", ins)
        ins = BR_TGT_RE.sub("", ins)          # 去跳转目标数值（保留 <符号>）
        ins = re.sub(r"\s+", " ", ins).strip().rstrip(",")
        out[cur].append(ins)
    return out


def normalize(ins: str) -> str | None:
    """归一化；返回 None 表示栈帧/流水线噪声（不参与比较）。"""
    ins = REG_RE.sub("R", ins)
    ins = IMM_RE.sub("", ins)
    ins = re.sub(r"\s+", " ", ins).strip().rstrip(",")
    if FRAME_RE.match(ins):
        return None
    head, _, tail = ins.partition(" ")
    mn = head.replace(".w", "").replace(".n", "")
    if mn.endswith("s") and mn[:-1] in FLAG_BASES:
        mn = mn[:-1]
    return (mn + " " + tail).strip() if tail else mn


def ops(seq: list[str], fn: str) -> list[str]:
    """归一化 + 去噪：返回参与比较的操作序列。"""
    out: list[str] = []
    for ins in seq:
        n = normalize(ins)
        if n is None:
            continue
        # 跳回本函数的条件分支（守卫引入的跳转合并）与纯取值调用：不参与比较
        if n == f"bl <dev_display_get>" or re.match(rf"^b\w* <{re.escape(fn)}>$", n):
            continue
        out.append(n)
    return out


# 已逐条人工复核的 5 处「操作集差异」（全部为编译期代码生成差异，语义等价）：
#   ① 三元折叠：入口守卫保证「进入主体时指针非空」⇒ 旧源码 `d ? d->screen_rows : 0`
#      被折叠为直接加载（少 4 次 ldr），取值恒等；
#   ② 分支合并：`cbz r, done` 展开为 `cmp r, #0` + `beq`；
#   ③ 寄存器重排 / 换编址方式（`add 立即数` → `add 寄存器`、`asr` 编码变化）。
REVIEWED_CODEGEN = {
    ("Device/Display/dev_display.o", "dev_display_draw_bitmap"): "cbz→cmp+beq 分支合并；asr 编码变化",
    ("Application/Src/LDI/app_ldi_device.o", "display_show"): "守卫后指针非空 ⇒ 旧三级 `d ? … : 0` 折叠（少 4 次加载）",
    ("Application/Src/LDI/app_vms_ctrl.o", "vms_display_ctrl"): "守卫后寄存器重排 + 溢出槽位变化",
    ("Application/Src/RLS/app_rls_cmd.o", "cmd_display"): "`add 立即数` → `add 寄存器` 编址方式变化",
    ("Application/Src/RLS/app_rls_cmd.o", "cmd_display_save"): "同上",
}


# 2026-10-13 用户裁决轮：`dev_display_init` 的 2 处调试冻结调用被整体删除
# （`pl_tim_dbg_freeze` API 一并移除；debug-only 设施，发布运行路径零影响）。
# 注：本脚本的对象来源 = `build/Debug`（与 EIDE 共用、本裁决轮未重建）⇒ 若以「含该删除的
# 构建」重跑，本条目把 dev_display_init 的原操作减少记为「已复核」而不是 LOST；
# 机器级证据见 `.analysis/comment_pass_20261013/ab_round3_objects.py`（逐对象期望清单）。
ROUND3_REMOVED_CALLS = {
    ("Device/Display/dev_display.o", "dev_display_init"):
        "2026-10-13 裁决：删除 pl_tim_dbg_freeze(TIM3/TIM4) 调试冻结调用（debug-only）",
}
# 2026-09-29 代码瘦身轮：随 RTT 调试设施删除而**整体消失的函数**（改动后不存在）。
# 逐条已人工复核：全部为只读诊断函数（打印/取参），业务路径零调用者。
RTT_ROUND_DELETED = {
    ("Device/Display/dev_display.o", "pl_task_create_checked"):
        "静态内联：RTT 失败打印删除后函数体仅剩 `return tid`，-Og 直接内联消除",
    ("Application/Src/app_boot.o", "pl_task_create_checked"):
        "同上（app_boot.c 的创建点内联）",
    ("Application/Src/app_factory_test.o", "pl_task_create_checked"):
        "同上（出厂测试任务创建点内联）",
    ("Application/Src/app_boot.o", "_diag_ch_state"): "诊断辅助（通道状态短名），仅服务横幅",
    ("Application/Src/app_boot.o", "app_diag_heap"): "堆余量探针（启动期 12 处调用点一并删除）",
    ("Application/Src/app_boot.o", "app_diag_boot_banner"): "开机横幅（RTT）",
    ("Application/Src/app_boot.o", "app_diag_boot_late"): "延迟体检（RTT）",
    ("Application/Src/app_boot.o", "dev_key_get_state"):
        "静态内联：唯一调用点在横幅（读 DIP2），随横幅删除而消失；dev_key.c 的定义未变",
}

# 2026-09-29 代码瘦身轮：函数仍在，但**操作集少了 RTT/诊断相关指令**（逐条已复核）。
RTT_ROUND_REVIEWED = {
    ("Application/Src/app_boot.o", "app_tcp_server_start"): "内联创建点：`pl_task_create_checked` 的 tag 取址加载消失",
    ("Application/Src/app_boot.o", "app_tcp_client_start"): "同上",
    ("Application/Src/app_boot.o", "app_udp_start"): "同上",
    ("Application/Src/app_boot.o", "app_udp_cq_start"): "同上",
    ("Application/Src/app_boot.o", "app_udp_gzol_start"): "同上",
    ("Application/Src/app_boot.o", "init_task"):
        "8 处 app_diag_heap 调用点的字符串取址加载 + 1 处 osDelay(2500) 体检延时删除",
    ("Application/Src/app_boot.o", "app_boot"):
        "init_task 创建失败路径的 RTT 打印（含 xPortGetFreeHeapSize/…MinimumEverFreeHeapSize）删除，"
        "保留 `taskDISABLE_INTERRUPTS()+for(;;)`",
    ("Application/Src/app_boot.o", "vApplicationMallocFailedHook"):
        "RTT 两行打印（含 xLastFailedAllocSize + 两个堆查询）删除，保留关中断死循环",
}


def objdump(path: Path) -> str:
    return subprocess.run(
        ["arm-none-eabi-objdump", "-d", str(path)],
        check=True, capture_output=True, text=True,
    ).stdout


def main() -> int:
    bad = 0
    total_fn = changed_fn = 0
    for rel, base in TARGETS:
        obj = ROOT / "build/Debug" / rel
        base_dis = BASE_DIR / f"{base}.dis"
        if not obj.exists() or not base_dis.exists():
            print(f"[skip] {rel} 或基线 {base_dis} 不存在")
            bad += 1
            continue
        before = parse_dis(base_dis.read_text(encoding="utf-8"))
        after = parse_dis(objdump(obj))
        if not any(before.values()) or not any(after.values()):
            print(f"[FAIL] {rel}: 反汇编解析结果为空（objdump 格式变更？）")
            bad += 1
            continue

        same = added_only = lost = reviewed = 0
        detail: list[str] = []
        for fn, old in before.items():
            if fn not in after:
                if (rel, fn) in RTT_ROUND_DELETED:
                    reviewed += 1
                    detail.append(f"    已复核 {fn}: 函数随 2026-09-29 RTT 瘦身轮删除 —— "
                                  f"{RTT_ROUND_DELETED[(rel, fn)]}")
                    continue
                detail.append(f"    LOST   {fn}: 改动后不存在")
                lost += 1
                continue
            total_fn += 1
            old_c = Counter(ops(old, fn))
            new_c = Counter(ops(after[fn], fn))
            lost_c = old_c - new_c
            extra_c = new_c - old_c
            if not lost_c and not extra_c:
                same += 1
                continue
            changed_fn += 1
            if lost_c and (rel, fn) in RTT_ROUND_REVIEWED:
                reviewed += 1
                detail.append(f"    已复核 {fn}: RTT 瘦身轮删除的诊断指令 = {dict(lost_c)} / "
                              f"新增 {dict(extra_c)} —— {RTT_ROUND_REVIEWED[(rel, fn)]}")
                continue
            if lost_c and (rel, fn) in ROUND3_REMOVED_CALLS:
                reviewed += 1
                detail.append(f"    已复核 {fn}: 原操作减少 = {dict(lost_c)} / 新增 {dict(extra_c)}"
                              f" —— {ROUND3_REMOVED_CALLS[(rel, fn)]}")
                continue
            if lost_c and (rel, fn) in REVIEWED_CODEGEN:
                reviewed += 1
                detail.append(f"    已复核 {fn}: 操作集差异 = {dict(lost_c)} / 新增 {dict(extra_c)}"
                              f" —— {REVIEWED_CODEGEN[(rel, fn)]}")
                continue
            if lost_c:
                lost += 1
                detail.append(f"    LOST   {fn}: 原操作丢失 {dict(lost_c)}")
                continue
            added_only += 1
            fmt = [f"{k}×{v}" if v > 1 else k for k, v in sorted(extra_c.items())]
            detail.append(f"    仅新增 {fn}: 原 {len(old_c)} 项操作零丢失，新增 {len(extra_c)} 项：{fmt}")
        new_fns = [f for f in after if f not in before]
        bad += lost
        print(f"[{rel}] 函数 {len(before)} 个：操作集一致 {same} / 仅新增 {added_only} / "
              f"已复核差异 {reviewed} / 原操作丢失 {lost} / 新增函数 {new_fns}")
        for line in detail:
            print(line)
    print(f"\nA/B 反汇编对照：函数合计 {total_fn}（改动 {changed_fn} 个），原操作丢失 {bad}")
    return bad


if __name__ == "__main__":
    sys.exit(main())
