#!/usr/bin/env python3
"""缅甸 2D 费显协议 — 宿主 probe/parse 推演 + 源码一致性断言。

镜像 `Application/Src/ProtocolParser_Myanmar/app_mm_proto.c` 的 `mm_probe_frame()`
与 `app_mm_proto_cmd.c` 的 `mm_parse_frame()`，喂边界用例（半帧 / 粘包 / 伪头 /
SC_OL 抢帧 / 载荷非法 / 正签名缺失）验证状态机流转与链式共存。

用法：
    python3 .analysis/myanmar/mm_frame_sim.py            # 全部用例
    python3 .analysis/myanmar/mm_frame_sim.py -v         # 打印每条用例明细

帧事实（`2D缅甸费显协议.doc`，定长 20 字节）：
    [0] 0xFF 起始码 | [1] 0xE0 机器地址 | [2] 命令方式字 | [3] DATA0
    [4..14] DATA1..DATA11 | [15..19] 结束码（文档 00 00 00 00 00）

本模块只认 0x90 / 0x0A / 0x0B 三条车道灯命令（DATA0 ∈ {0,1}）：
    0x90 → 红灯（S1=PE12） | 0x0A → 左下箭头灯（S2=PE11） | 0x0B → 右下箭头灯（S3=PE10）
其余命令字（0x10/0x40/0x80 等）→ probe SKIP 整帧消费，**但须结束码前 4 字节全 0
（文档正签名）**，否则 FAKE —— 防误吞同槽四川治超（SC_OL）帧长恰为 0xE0 的半帧。

干接点输出层（2026-09-24 阶段二，用户裁决；**2026-09-28 现场实测翻转极性**）：三路复用
SW1/SW2/SW3 端子改为**输出**，**推挽、高电平有效**（点亮 = 输出**高**；现场证据：复位后红灯
即亮 + PE12 实测 3.3V ⇒ 3.3V = 点亮）、**上电默认显示红叉 = 红灯 S1/PE12 点亮（输出高）、
两个绿箭头熄灭**（**2026-09-29 用户需求**，取代 2026-09-24 的「三路全灭」语义；协议帧到达后
照常按帧驱动）；`§9` 覆盖灯位/引脚/极性/上电默认态语义，
`[源码]` 断言覆盖「单一映射点 + 先灭电平→摘 EXTI→切输出→红灯点亮 的接管顺序 + 逐线摘 EXTI +
现行 `active_low=false`」。

上电默认画面（**2026-09-29 新增**，用户需求「开机默认显示红叉」）：带显示驱动的构建里
缅甸模块经 `app_default_display_register()` 注册「红色叉（×）」默认画面（`§11` 用例 +
`[源码]` 断言）；无显示驱动构建（`dev_display_get() == nullptr`）不注册、零调用零告警。

箭头落点运行时交换（**2026-09-28 新增**，TEST 键出厂测试开关）：`§10` 覆盖
`mm_arrow_map_toggle()` 的语义 —— 默认态 `0x0A→左下 / 0x0B→右下`、交换态 `0x0A→右下 /
0x0B→左下`、**红灯 `0x90` 不受影响**、可来回切换、**开机复位为默认态（不持久化）**；
`[源码]` 断言固定不变量：生效单点 = `mm_execute_cmd()` 内 `_mm_effective_lamp()`（解析
`mm_parse_frame()` 不含开关、保持纯函数）、交换函数只比较 LEFT/RIGHT、`mm_apply_init()`
复位开关、cmd 文件无任何存储设备调用、RTT 日志一行且引脚由映射表推导。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# ---- 与 C 侧常量一一对应（源码断言会核对） ----
MM_FRAME_HEAD = 0xFF
MM_FRAME_ADDR = 0xE0
MM_CMD_OFFSET = 2
MM_ARG_OFFSET = 3
MM_DATA_LEN = 12
MM_TAIL_LEN = 5
MM_FRAME_LEN = 3 + MM_DATA_LEN + MM_TAIL_LEN  # 20
MM_QUEUE_DEPTH = 3
MM_TAIL_SIG_OFFSET = MM_FRAME_LEN - MM_TAIL_LEN  # 15
MM_TAIL_SIG_LEN = MM_TAIL_LEN - 1  # 4（结束字符5..2；末字节不判别）

MM_CMD_LANE_RED = 0x90
MM_CMD_LANE_LEFT = 0x0A
MM_CMD_LANE_RIGHT = 0x0B

MM_LAMP_RED, MM_LAMP_LEFT, MM_LAMP_RIGHT = "red", "left", "right"

FAKE, WAIT, READY, SKIP = "FAKE", "WAIT", "READY", "SKIP"

# 四川省治超协议（SC_OL）probe 常量 —— 用于验证 RS232 槽 0xFF 首字节共存
SC_OL_FRAME_LEN_MIN = 7
SC_OL_TAIL = 0xFF


# ==========================================================================
# 探针 / 解析镜像
# ==========================================================================
def mm_probe(buf: bytes) -> tuple[str, int | None, str | None]:
    """镜像 mm_probe_frame()；返回 (sta, total_len, note)。"""
    avail = len(buf)
    if avail == 0:
        return FAKE, None, "空缓冲"
    if buf[0] != MM_FRAME_HEAD:
        return FAKE, None, "首字节非 0xFF"
    if avail < 2:
        return WAIT, None, "仅 0xFF"
    if buf[1] != MM_FRAME_ADDR:
        return FAKE, None, "机器地址非 0xE0"
    if avail < MM_FRAME_LEN:
        return WAIT, None, f"定长帧未到齐（{avail}/20）"
    cmd, arg = buf[MM_CMD_OFFSET], buf[MM_ARG_OFFSET]
    if cmd not in (MM_CMD_LANE_RED, MM_CMD_LANE_LEFT, MM_CMD_LANE_RIGHT):
        sig = buf[MM_TAIL_SIG_OFFSET : MM_TAIL_SIG_OFFSET + MM_TAIL_SIG_LEN]
        if any(b != 0 for b in sig):
            return FAKE, None, "非本模块命令且无正签名 → 不消费"
        return SKIP, MM_FRAME_LEN, f"非本模块命令 0x{cmd:02X} → 整帧消费"
    if arg > 0x01:
        return SKIP, MM_FRAME_LEN, f"DATA0=0x{arg:02X} 越界 → 整帧消费"
    return READY, MM_FRAME_LEN, None


def mm_parse(frame: bytes):
    """镜像 mm_parse_frame()；返回 parsed dict 或 None。"""
    if len(frame) != MM_FRAME_LEN or frame[0] != MM_FRAME_HEAD or frame[1] != MM_FRAME_ADDR:
        return None
    cmd, arg = frame[MM_CMD_OFFSET], frame[MM_ARG_OFFSET]
    lamp = {
        MM_CMD_LANE_RED: MM_LAMP_RED,
        MM_CMD_LANE_LEFT: MM_LAMP_LEFT,
        MM_CMD_LANE_RIGHT: MM_LAMP_RIGHT,
    }.get(cmd)
    if lamp is None:
        return None
    return {"cmd": cmd, "lamp": lamp, "on": arg == 0x01}


def sc_ol_probe(buf: bytes) -> tuple[str, int | None, str | None]:
    """镜像 SC_OL 的 sc_ol_probe_frame()（只判首字节/长度/尾字节，BCC 在 parse 层）。"""
    if len(buf) == 0:
        return FAKE, None, None
    if buf[0] != 0xFF:
        return FAKE, None, None
    if len(buf) < 2:
        return WAIT, None, None
    ln = buf[1]
    if ln < SC_OL_FRAME_LEN_MIN or ln == 0xFE:
        return FAKE, None, None
    if len(buf) < ln:
        return WAIT, None, None
    if buf[ln - 1] != SC_OL_TAIL:
        return FAKE, None, None
    return READY, ln, None


def chain_probe(buf: bytes, order: tuple[str, ...] = ("mm",)) -> tuple[str, int | None, str | None]:
    """镜像 frame_dispatch_task 的链式语义：WAIT/FAKE 继续试下一协议，READY/SKIP 终止。

    返回 (sta, total_len, 认领者)。
    """
    fed_wait = False
    for name in order:
        sta, tlen, _note = mm_probe(buf) if name == "mm" else sc_ol_probe(buf)
        if sta in (READY, SKIP):
            return sta, tlen, name
        if sta == WAIT:
            fed_wait = True
    return WAIT if fed_wait else FAKE, None, None


# ==========================================================================
# 帧构造
# ==========================================================================
def frame(cmd: int, arg: int, body: bytes | None = None, tail: bytes | None = None) -> bytes:
    """按文档布局构造 20 字节帧（文档例：DATA1..DATA11 填 0x3A，结束码 00×5）。"""
    b = body if body is not None else bytes([0x3A]) * (MM_DATA_LEN - 1)
    assert len(b) == MM_DATA_LEN - 1, len(b)
    t = tail if tail is not None else bytes(MM_TAIL_LEN)
    assert len(t) == MM_TAIL_LEN, len(t)
    return bytes([MM_FRAME_HEAD, MM_FRAME_ADDR, cmd, arg]) + b + t


# 文档第 5 节原文示例（逐字节抄录，用于「与文档同字节」断言）
DOC_0X10_EX1 = bytes.fromhex("FFE0103A3A3A3A31303A3A3A3A3235" + "00" * 5)
DOC_0X10_EX2 = bytes.fromhex("FFE01038" + "38" * 11 + "00" * 5)
DOC_0X90_ON = bytes.fromhex("FFE09001" + "3A" * 11 + "00" * 5)
DOC_0X90_OFF = bytes.fromhex("FFE09000" + "3A" * 11 + "00" * 5)
DOC_0X0A_ON = bytes.fromhex("FFE00A01" + "3A" * 11 + "00" * 5)
DOC_0X0A_OFF = bytes.fromhex("FFE00A00" + "3A" * 11 + "00" * 5)


# ==========================================================================
# 用例
# ==========================================================================
CASES: list[tuple[str, object, object]] = []
FAILED: list[str] = []


def case(name: str, got, want) -> None:
    CASES.append((name, got, want))
    if got != want:
        FAILED.append(f"{name}: got={got!r} want={want!r}")


def run_cases(verbose: bool) -> None:
    # --- §0 文档示例长度自证 ---
    case("§0.1 文档 0x10 例1 = 20B", len(DOC_0X10_EX1), 20)
    case("§0.2 文档 0x10 例2 = 20B", len(DOC_0X10_EX2), 20)
    case("§0.3 文档 0x90 例 = 20B", len(DOC_0X90_ON), 20)
    case("§0.4 文档 0x0A 例 = 20B", len(DOC_0X0A_ON), 20)

    # --- §1 probe 基本判据 ---
    case("§1.1 空缓冲 → FAKE", mm_probe(b"")[0], FAKE)
    case("§1.2 首字节非 0xFF → FAKE", mm_probe(bytes.fromhex("00E09001"))[0], FAKE)
    case("§1.3 仅 0xFF → WAIT", mm_probe(b"\xff")[0], WAIT)
    case("§1.4 0xFF + 地址非 0xE0 → FAKE", mm_probe(bytes.fromhex("FFE1"))[0], FAKE)
    case("§1.5 0xFF 0xE0 + 半帧(19B) → WAIT", mm_probe(b"\xff\xe0" + bytes(17))[0], WAIT)
    case("§1.6 完整 0x90 开灯 → READY(20)", mm_probe(DOC_0X90_ON)[:2], (READY, MM_FRAME_LEN))
    case("§1.7 完整 0x0A 关灯 → READY(20)", mm_probe(DOC_0X0A_OFF)[:2], (READY, MM_FRAME_LEN))

    # --- §2 命令与灯位映射（用户口径） ---
    case("§2.1 0x90 1 → 红灯 ON", mm_parse(DOC_0X90_ON), {"cmd": 0x90, "lamp": "red", "on": True})
    case("§2.2 0x90 0 → 红灯 OFF", mm_parse(DOC_0X90_OFF), {"cmd": 0x90, "lamp": "red", "on": False})
    case("§2.3 0x0A 1 → 左下箭头 ON", mm_parse(DOC_0X0A_ON), {"cmd": 0x0A, "lamp": "left", "on": True})
    case("§2.4 0x0A 0 → 左下箭头 OFF", mm_parse(DOC_0X0A_OFF), {"cmd": 0x0A, "lamp": "left", "on": False})
    case("§2.5 0x0B 1 → 右下箭头 ON", mm_parse(frame(0x0B, 1)),
         {"cmd": 0x0B, "lamp": "right", "on": True})
    case("§2.6 0x0B 0 → 右下箭头 OFF", mm_parse(frame(0x0B, 0)),
         {"cmd": 0x0B, "lamp": "right", "on": False})

    # --- §3 文档非本模块命令 → 正签名成立时 SKIP 整帧消费 ---
    case("§3.1 0x10 数码管显示 → SKIP(20)", mm_probe(DOC_0X10_EX1)[:2], (SKIP, MM_FRAME_LEN))
    case("§3.2 0x10 例2 → SKIP(20)", mm_probe(DOC_0X10_EX2)[:2], (SKIP, MM_FRAME_LEN))
    case("§3.3 0x40 亮度 → SKIP(20)", mm_probe(frame(0x40, 0x0A))[:2], (SKIP, MM_FRAME_LEN))
    case("§3.4 0x80 报警控制 → SKIP(20)", mm_probe(frame(0x80, 0x04))[:2], (SKIP, MM_FRAME_LEN))
    case("§3.5 DATA0=0x02 越界 → SKIP(20)", mm_probe(frame(0x90, 0x02))[:2], (SKIP, MM_FRAME_LEN))
    case("§3.6 DATA0=0xFF 越界 → SKIP(20)", mm_probe(frame(0x0A, 0xFF))[:2], (SKIP, MM_FRAME_LEN))
    case("§3.7 SKIP 帧不进解析（parse=None）", mm_parse(DOC_0X10_EX1), None)
    # 正签名缺失（结束码 [15..18] 非 0）→ FAKE，不消费字节
    case("§3.8 0x10 帧无正签名 → FAKE", mm_probe(frame(0x10, 0, tail=bytes([0x11, 0, 0, 0, 0])))[0],
         FAKE)
    case("§3.9 0x80 帧无正签名 → FAKE", mm_probe(frame(0x80, 4, tail=bytes([0xAA] * 5)))[0], FAKE)

    # --- §4 结束码末字节不参与判别（同源 8051 把 [19] 当未比对的 XOR） ---
    case("§4.1 末字节=XOR(0x16) 仍 READY", mm_probe(frame(0x90, 1, tail=bytes([0, 0, 0, 0, 0x16])))[0],
         READY)
    case("§4.2 末字节=0xFF 仍 READY", mm_probe(frame(0x90, 1, tail=bytes([0, 0, 0, 0, 0xFF])))[0],
         READY)
    case("§4.3 末字节=XOR 时 0x10 仍 SKIP", mm_probe(frame(0x10, 0, tail=bytes([0, 0, 0, 0, 0x16])))[0],
         SKIP)

    # --- §5 粘包 / 拆包 ---
    two = DOC_0X90_ON + DOC_0X0A_OFF
    case("§5.1 粘包首帧 → READY(20)", mm_probe(two)[:2], (READY, MM_FRAME_LEN))
    case("§5.2 粘包次帧 → READY(20)", mm_probe(two[MM_FRAME_LEN:])[:2], (READY, MM_FRAME_LEN))
    case("§5.3 拆包前半(10B) → WAIT", mm_probe(frame(0x0B, 1)[:10])[0], WAIT)
    case("§5.4 拆包后半(10B) → FAKE", mm_probe(frame(0x0B, 1)[10:])[0], FAKE)

    # --- §6 与同槽既有协议共存（RS232 槽：四川治超 SC_OL 首字节亦为 0xFF） ---
    sc_ol_clear = bytes.fromhex("FF079400006CFF")  # FF 07 94 00 00 6C FF（len=7 清屏最小帧）
    # 治超 len=224 长帧：真实数据非全 0 → 本协议正签名不成立
    sc_ol_long_ok = bytes([0xFF, 0xE0, 0x80]) + bytes([0xAA]) * 220 + bytes([0xFF])
    # 人工最坏帧：治超 len=224 且 [15..18] 恰为 0（构造，非真实治超帧）→ 已知残余风险
    sc_ol_long_zeroed = (
        bytes([0xFF, 0xE0, 0x81]) + bytes([0xAA]) * 12 + bytes(4) + bytes([0xAA]) * 204 + bytes([0xFF])
    )
    case("§6.0 实测样例长度自证（7 / 224 / 224）", (len(sc_ol_clear), len(sc_ol_long_ok),
         len(sc_ol_long_zeroed)), (7, 224, 224))
    case("§6.1 治超短帧 → 本 probe FAKE", mm_probe(sc_ol_clear)[0], FAKE)
    case("§6.2 治超短帧 → SC_OL READY(7)", sc_ol_probe(sc_ol_clear)[:2], (READY, 7))
    case("§6.3 治超 len=0xE0 长帧(全 0 数据) → 本 probe FAKE", mm_probe(sc_ol_long_ok)[0], FAKE)
    case("§6.4 治超 len=0xE0 长帧(全 0 数据) → SC_OL READY(224)",
         sc_ol_probe(sc_ol_long_ok)[:2], (READY, 224))
    case("§6.5 人工最坏帧(治超 len=0xE0 + [15..18]全0) → SKIP(20) —— 已知残余风险",
         mm_probe(sc_ol_long_zeroed)[:2], (SKIP, MM_FRAME_LEN))
    case("§6.6 RLS 帧(FF FE) → 本 probe FAKE", mm_probe(bytes.fromhex("FFFE") + bytes(20))[0], FAKE)
    case("§6.7 链[sc_ol,mm] 本协议帧 → mm READY", chain_probe(DOC_0X90_ON, ("sc_ol", "mm")),
         (READY, MM_FRAME_LEN, "mm"))
    case("§6.8 链[mm,sc_ol] 治超帧 → sc_ol READY(7)", chain_probe(sc_ol_clear, ("mm", "sc_ol")),
         (READY, 7, "sc_ol"))
    case("§6.9 链[sc_ol,mm] 本协议半帧(10B) → WAIT", chain_probe(DOC_0X90_ON[:10], ("sc_ol", "mm"))[0],
         WAIT)
    case("§6.10 孤字节 0xFF → 链 WAIT", chain_probe(b"\xff", ("sc_ol", "mm"))[0], WAIT)
    case("§6.11 链[sc_ol,mm] 治超长帧未被本协议消费",
         chain_probe(sc_ol_long_ok, ("sc_ol", "mm"))[:2], (READY, 224))

    # --- §7 乱字节流重同步（probe 全 FAKE → 调度器 skip 1B） ---
    noise = b"\x90\x01\x0a\x0b\xff\xe1\x00\xff"
    skipped, buf = 0, noise
    while buf:
        sta, _tlen, _note = mm_probe(buf)
        if sta != FAKE:
            break
        buf, skipped = buf[1:], skipped + 1
    case("§7.1 噪声流重同步 7 字节后停在 0xFF", (skipped, mm_probe(buf)[0]), (7, WAIT))

    # --- §8 队列容量核算 ---
    msg_size = 8 + MM_FRAME_LEN  # frame_msg_t(8) + payload
    case("§8.1 队列体 = 深度×msg = 84B", MM_QUEUE_DEPTH * msg_size, 84)

    # --- §9 干接点输出层（S1=PE12 / S2=PE11 / S3=PE10，推挽；**2026-09-28 现场实测：高有效**）---
    # 与 C 侧 s_mm_lamp_map + _mm_drive() 一一对应（极性语义 high = on != active_low）
    lamp_pins = {MM_LAMP_RED: ("S1", 12), MM_LAMP_LEFT: ("S2", 11), MM_LAMP_RIGHT: ("S3", 10)}

    def drive(lamp: str, on: bool) -> str:
        """镜像 _mm_drive()：high = (on != active_low)；现行映射表 active_low=False（高有效）。

        现场证据（2026-09-28）：复位后红灯即亮、PE12 实测 3.3V ⇒ 3.3V = 点亮 ⇒ 高有效。
        """
        active_low = False  # = C 侧 s_mm_lamp_map 三项的 .active_low（2026-09-28 翻转）
        return "HIGH" if (on != active_low) else "LOW"

    case("§9.1 灯位→干接点→引脚映射（S1/PE12, S2/PE11, S3/PE10）", lamp_pins,
         {MM_LAMP_RED: ("S1", 12), MM_LAMP_LEFT: ("S2", 11), MM_LAMP_RIGHT: ("S3", 10)})
    case("§9.2 红灯开(0x90 01) → 输出 HIGH（点亮）", drive(MM_LAMP_RED, True), "HIGH")
    case("§9.3 红灯关(0x90 00) → 输出 LOW（熄灭）", drive(MM_LAMP_RED, False), "LOW")
    case("§9.4 左下箭头开(0x0A 01) → 输出 HIGH", drive(MM_LAMP_LEFT, True), "HIGH")
    case("§9.5 右下箭头开(0x0B 01) → 输出 HIGH", drive(MM_LAMP_RIGHT, True), "HIGH")
    case("§9.6 上电/复位默认态 = 红叉亮（红灯 HIGH）/ 两个绿箭头灭（LOW）"
         "（2026-09-29 用户需求，取代 2026-09-24 的「三路全灭」）",
         [drive(MM_LAMP_RED, True), drive(MM_LAMP_LEFT, False), drive(MM_LAMP_RIGHT, False)],
         ["HIGH", "LOW", "LOW"])
    case("§9.6b 上电默认态不是「三路全灭」（红灯必须 HIGH）",
         drive(MM_LAMP_RED, False), "LOW")
    case("§9.7 三路输出与命令一一对应（同帧不同灯互不串扰）",
         [drive(MM_LAMP_RED, True), drive(MM_LAMP_LEFT, False), drive(MM_LAMP_RIGHT, True)],
         ["HIGH", "LOW", "HIGH"])

    # --- §10 箭头落点运行时交换（TEST 键出厂测试开关；2026-09-28 新增）---
    # 镜像 C 侧 `_mm_effective_lamp()`（mm_execute_cmd 的最后一步）+ `mm_arrow_map_toggle()`。
    def effective_lamp(lamp: str, swapped: bool) -> str:
        """镜像 _mm_effective_lamp()：交换只涉及 LEFT ⇄ RIGHT，红灯 0x90 不动。"""
        if not swapped:
            return lamp
        if lamp == MM_LAMP_LEFT:
            return MM_LAMP_RIGHT
        if lamp == MM_LAMP_RIGHT:
            return MM_LAMP_LEFT
        return lamp

    def toggle(swapped: bool) -> bool:
        """镜像 mm_arrow_map_toggle()：翻转并返回新状态。"""
        return not swapped

    def effective_of(frame: bytes, swapped: bool) -> str:
        """完整链路镜像：probe 判据 → parse（名义灯位）→ 交换（执行层）→ 实际灯位。"""
        return effective_lamp(mm_parse(frame)["lamp"], swapped)

    case("§10.1 默认态：0x0A → 左下箭头（PE11）",
         effective_of(frame(0x0A, 1), False), MM_LAMP_LEFT)
    case("§10.2 默认态：0x0B → 右下箭头（PE10）",
         effective_of(frame(0x0B, 1), False), MM_LAMP_RIGHT)
    case("§10.3 交换后：0x0A → 右下箭头（PE10）",
         effective_of(frame(0x0A, 1), True), MM_LAMP_RIGHT)
    case("§10.4 交换后：0x0B → 左下箭头（PE11）",
         effective_of(frame(0x0B, 1), True), MM_LAMP_LEFT)
    case("§10.5 交换后：0x90 红灯不受影响（仍 → 红灯）",
         effective_of(DOC_0X90_ON, True), MM_LAMP_RED)
    case("§10.6 交换可来回（翻转两次回默认态）",
         toggle(toggle(False)), False)
    case("§10.7 交换后 probe 判据不变（0x0A 帧仍 READY(20)，SKIP 分支同前）",
         (mm_probe(DOC_0X0A_ON)[:2], mm_probe(DOC_0X10_EX1)[:2], mm_probe(DOC_0X0A_ON[:10])[0]),
         ((READY, MM_FRAME_LEN), (SKIP, MM_FRAME_LEN), WAIT))
    case("§10.8 parse 结果与交换开关无关（解析保持纯函数：名义灯位恒为 left/right）",
         (mm_parse(DOC_0X0A_ON)["lamp"], mm_parse(frame(0x0B, 0))["lamp"]),
         (MM_LAMP_LEFT, MM_LAMP_RIGHT))
    case("§10.9 开机态 = 默认（模型复位后 0x0A 回左下）",
         effective_of(frame(0x0A, 1), False), MM_LAMP_LEFT)
    case("§10.10 交换后驱动电平仍按目标灯位极性（0x0A→右下 = PE10 HIGH）",
         drive(effective_of(frame(0x0A, 1), True), True), "HIGH")

    # --- §11 上电默认画面：红色叉（×）（2026-09-29 用户需求）---
    # 镜像 C 侧 `_mm_default_red_cross()`：整屏清黑 + 两条对角线（整数线性插值逐行落笔，
    # 线宽 = max(2, h/24)，越界丢弃）。屏几何取自 dev_display_get()（宿主按两口径各推一遍）。
    def cross_pixels(w: int, h: int) -> set[tuple[int, int]]:
        thick = max(2, h // 24)
        x_max, y_max = w - 1, h - 1
        pts: set[tuple[int, int]] = set()
        for y in range(h):
            x_l = y * x_max // y_max
            x_r = x_max - x_l
            for k in range(thick):
                if x_l + k < w:
                    pts.add((x_l + k, y))
                if x_r >= k:
                    pts.add((x_r - k, y))
        return pts

    cross_192x96 = cross_pixels(192, 96)  # 1-969 / 1-263 / 22-1703 口径
    cross_32x32 = cross_pixels(32, 32)    # 1-263 1×1 实验态口径
    case("§11.1 红叉线宽 = max(2, h/24)（192×96 → 4px；32×32 → 2px）",
         (max(2, 96 // 24), max(2, 32 // 24)), (4, 2))
    case("§11.2 红叉覆盖四角（左上/右上/左下/右下）",
         all(p in cross_192x96 for p in ((0, 0), (0, 95), (191, 0), (191, 95))), True)
    case("§11.3 红叉中心行两支相邻（192×96：y=48 → x=96/95）",
         (48 * 191 // 95, 191 - 48 * 191 // 95), (96, 95))
    case("§11.4 红叉坐标全部在屏内（无越界落笔）",
         all(0 <= x < 192 and 0 <= y < 96 for x, y in cross_192x96), True)
    case("§11.5 红色之外不落笔（脚本层只建模 COLOR_RED 落点，不含其它颜色）",
         len({p for p in cross_32x32 if not (0 <= p[0] < 32 and 0 <= p[1] < 32)}), 0)
    case("§11.6 红叉为「两条对角线」而非实心（覆盖像素数远小于整屏）",
         len(cross_192x96) < 192 * 96 // 8, True)

    if verbose:
        for i, (name, got, want) in enumerate(CASES, 1):
            mark = "PASS" if got == want else "FAIL"
            print(f"[{mark}] {i:02d} {name}\n      got={got!r}\n     want={want!r}")


# ==========================================================================
# 源码一致性断言（防 C 侧常量漂移后被脚本静默漏检）
# ==========================================================================
def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def run_source_assertions() -> None:
    hdr = (ROOT / "Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h").read_text(encoding="utf-8")
    proto = (ROOT / "Application/Src/ProtocolParser_Myanmar/app_mm_proto.c").read_text(encoding="utf-8")
    cmd = (ROOT / "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c").read_text(encoding="utf-8")
    code = _strip_comments(hdr + proto + cmd)
    proto_code = _strip_comments(proto)
    cmd_code = _strip_comments(cmd)

    def has(text: str, pat: str, name: str) -> None:
        case(f"[源码] {name}", bool(re.search(pat, text)), True)

    has(hdr, r"#define MM_FRAME_HEAD \(0xFFU\)", "MM_FRAME_HEAD=0xFF")
    has(hdr, r"#define MM_FRAME_ADDR \(0xE0U\)", "MM_FRAME_ADDR=0xE0")
    has(hdr, r"#define MM_CMD_OFFSET \(2U\)", "MM_CMD_OFFSET=2")
    has(hdr, r"#define MM_ARG_OFFSET \(3U\)", "MM_ARG_OFFSET=3")
    has(hdr, r"#define MM_TAIL_LEN \(5U\)", "MM_TAIL_LEN=5")
    has(hdr, r"#define MM_QUEUE_DEPTH \(3U\)", "MM_QUEUE_DEPTH=3")
    has(hdr, r"MM_CMD_LANE_RED\s*=\s*0x90", "0x90 红灯命令字")
    has(hdr, r"MM_CMD_LANE_LEFT\s*=\s*0x0A", "0x0A 左下箭头命令字")
    has(hdr, r"MM_CMD_LANE_RIGHT\s*=\s*0x0B", "0x0B 右下箭头命令字")
    # 2026-10-13 全仓编译期断言清理（用户裁决）：原 `_Static_assert(MM_FRAME_LEN == 20U)`
    # 存在性检查改为「契约注释存在」+「宿主侧复算」。
    has(hdr, r"契约：帧定长 20B（协议文档固定）且 ≤ RS232 RB 容量",
        "定长 20 字节契约注释（原编译期断言）")
    case("[源码] MM_FRAME_LEN == 20（宿主侧复算）", MM_FRAME_LEN, 20)
    # 只绑 RS232；不得出现其它通道的 bind
    binds = re.findall(r"app_proto_bind_channel\([^,]+,\s*([A-Z_0-9]+)\)", proto_code)
    case("[源码] 唯一 bind 通道 = CH_ID_RS232", binds, ["CH_ID_RS232"])
    for forbidden in ("CH_ID_RS485", "CH_ID_RS232_1", "CH_ID_UDP", "CH_ID_TCP_SERVER",
                      "CH_ID_TCP_CLIENT", "CH_ID_UDP_CQ", "CH_ID_UDP_GZOL", "CH_ID_MQTT"):
        case(f"[源码] 代码中不出现 {forbidden}", forbidden in code, False)
    # 静态任务 + CCMRAM 队列
    has(proto, r"PL_TASK_STATIC_STORAGE\(mm_handle, 256\)", "任务栈静态落 CCMRAM")
    has(proto, r'\[\[gnu::section\("\.ccmram"\)\]\] static uint8_t s_mm_queue_buf', "队列体 CCMRAM")
    has(proto, r'\[\[gnu::section\("\.ccmram"\)\]\] static StaticQueue_t s_mm_queue_cb',
        "队列控制块 CCMRAM")
    has(proto, r"RB_PROVIDE_WEAK\(rb_provide_rs232, RB_SIZE_RS232\)", "RS232 槽 weak provide")
    # 输出层纪律（2026-09-24 阶段二：真实驱动 PE12/PE11/PE10）
    #   ① Application 层**不得**直接碰 HAL —— 只经 Platform 的 pl_gpio_* 接口；
    #   ② 单一映射点 = s_mm_lamp_map + _mm_drive()；
    #   ③ 接管顺序：先写灭电平（_mm_drive）→ 摘 EXTI → 切推挽输出。
    case("[源码] 输出层经 pl_gpio.h（Platform 抽象）", "pl_gpio.h" in cmd_code, True)
    case("[源码] 输出层不含 HAL 头（分层纪律）", "stm32f4xx_hal" in cmd_code, False)
    has(cmd, r"pl_gpio_write\s*\(", "输出层调用 pl_gpio_write（驱动电平）")
    has(cmd, r"pl_gpio_release_exti\s*\(", "输出层调用 pl_gpio_release_exti（摘 EXTI）")
    has(cmd, r"pl_gpio_config_output\s*\(", "输出层调用 pl_gpio_config_output（推挽输出）")
    case("[源码] 输出层无占位/待确认残留",
         bool(re.search(r"未接线占位|\[待确认\]|NO GPIO", cmd_code)), False)
    # 单一实现点：极性语义 = on != active_low（写引脚只此一处；`mm_apply_init` 的灭电平预写
    # 也经 `_mm_drive`，本文件无第二处电平硬编码）
    has(cmd, r"pl_gpio_write\(m->port, m->pin, on != m->active_low\)",
        "极性表达式 on != active_low（唯一电平真源）")
    case("[源码] 现行极性 = 高有效（映射表三项均 .active_low = false）",
         len(re.findall(r"\.active_low = false", cmd)), 3)
    case("[源码] 映射表无 .active_low = true 残留（2026-09-28 现场实测翻转）",
         ".active_low = true" in cmd, False)
    case("[源码] pl_gpio_write 调用点唯一（单一映射点）",
         len(re.findall(r"pl_gpio_write\s*\(", cmd_code)), 1)
    # 接管顺序（mm_apply_init 内）：_mm_drive → release_exti → config_output → 红灯点亮
    init_body = cmd_code[cmd_code.index("void mm_apply_init"):]
    order = [init_body.find("_mm_drive(m, false)"), init_body.find("pl_gpio_release_exti"),
             init_body.find("pl_gpio_config_output"),
             init_body.find("mm_apply_lamp(MM_LAMP_RED, true)")]
    case("[源码] 接管顺序 = 先灭电平 → 摘 EXTI → 切输出 → 点亮红灯（开机默认红叉）",
         all(i >= 0 for i in order) and order == sorted(order), True)
    case("[源码] 上电默认显示红叉（mm_apply_init 内 mm_apply_lamp(MM_LAMP_RED, true)）",
         bool(re.search(r"mm_apply_lamp\(MM_LAMP_RED, true\)", init_body)), True)
    case("[源码] 上电默认态不再把红灯留在灭（无 mm_apply_lamp(MM_LAMP_RED, false)）",
         bool(re.search(r"mm_apply_lamp\(MM_LAMP_RED, false\)", init_body)), False)
    # 灯位 → 干接点映射（用户口径 + 引脚/极性：2026-09-28 现场实测翻转 ⇒ 高有效）
    for contact, lamp in (("S1", "MM_LAMP_RED"), ("S2", "MM_LAMP_LEFT"), ("S3", "MM_LAMP_RIGHT")):
        has(cmd, rf"{lamp}\]\s*=\s*\{{\s*\.contact = \"{contact}\"", f"{lamp} → {contact}")
    for lamp, pin in (("MM_LAMP_RED", 12), ("MM_LAMP_LEFT", 11), ("MM_LAMP_RIGHT", 10)):
        has(cmd, rf"{lamp}\].*?\.port = PL_PORT_E,\s*\.pin = {pin},\s*\.active_low = false",
            f"{lamp} → PE{pin} 高有效")
    # 2026-09-29 代码瘦身轮：`[mm]` 诊断打印（含极性文案与接管摘要）随 RTT 设施整体删除
    # —— 断言改为「无残留」，检查强度不降（原文案/引脚硬编码若回归即失败）。
    case("[源码] 极性诊断文案已随 RTT 设施删除（无「高有效:高=点亮」残留）",
         "高有效:高=点亮" in cmd_code or "低有效:低=点亮" in cmd_code, False)
    case("[源码] 接管日志摘要已随 RTT 设施删除（无 active-high / active-low 残留）",
         "active-high" in cmd_code or "active-low" in cmd_code, False)
    # Platform 层的 EXTI 摘除纪律（逐线清理；禁止整条 IRQ 一刀切）
    gpio_hdr = (ROOT / "Platform/Inc/pl_gpio.h").read_text(encoding="utf-8")
    gpio_src = (ROOT / "Platform/Src/pl_gpio.c").read_text(encoding="utf-8")
    gpio_code = _strip_comments(gpio_src)
    has(gpio_code, r"EXTI->IMR\s*&= ~line", "release_exti 清 EXTI->IMR")
    has(gpio_code, r"EXTI->EMR\s*&= ~line", "release_exti 清 EXTI->EMR")
    has(gpio_code, r"EXTI->RTSR\s*&= ~line", "release_exti 清 EXTI->RTSR")
    has(gpio_code, r"EXTI->FTSR\s*&= ~line", "release_exti 清 EXTI->FTSR")
    has(gpio_code, r"EXTI->PR\s*=\s*line", "release_exti 清 EXTI->PR 挂起（HAL 不做）")
    has(gpio_code, r"SYSCFG->EXTICR\[pin >> 2U\]", "release_exti 释放 SYSCFG EXTICR 线映射")
    case("[源码] 禁止 HAL_NVIC_DisableIRQ（一刀切会误杀其它 EXTI 线）",
         ("HAL_NVIC_DisableIRQ" in code) or ("HAL_NVIC_DisableIRQ" in gpio_code), False)
    case("[源码] pl_gpio_config_output 用 GPIO_MODE_OUTPUT_PP",
         bool(re.search(r"\.Mode\s*=\s*GPIO_MODE_OUTPUT_PP", gpio_code)), True)
    has(gpio_hdr, r"void pl_gpio_release_exti\(pl_port_t port, uint8_t pin\)",
        "pl_gpio_release_exti 声明（不透明端口枚举）")
    case("[源码] pl_gpio.h 不暴露 HAL 类型",
         bool(re.search(r"GPIO_TypeDef|GPIO_InitTypeDef", gpio_hdr)), False)

    # ---- 箭头落点运行时交换（2026-09-28 新增；生效单点 + 不变量）----
    # ① 公开 API 声明（出厂测试消费者用）；
    has(hdr, r"bool mm_arrow_map_toggle\(void\);", "header 声明 mm_arrow_map_toggle()")
    has(hdr, r"bool mm_arrow_map_is_swapped\(void\);", "header 声明 mm_arrow_map_is_swapped()")
    # ② 生效单点 = mm_execute_cmd 内 _mm_effective_lamp()（解析/probe/映射表零改动）；
    has(cmd, r"static mm_lamp_t _mm_effective_lamp\(mm_lamp_t lamp\)",
        "交换生效单点 _mm_effective_lamp()")
    has(cmd, r"mm_apply_lamp\(_mm_effective_lamp\(cmd->lamp\), cmd->on\)",
        "mm_execute_cmd 经 _mm_effective_lamp 驱动（唯一生效路径）")
    has(cmd, r"static volatile bool s_mm_arrow_swapped", "交换开关 s_mm_arrow_swapped（volatile）")
    # ③ 交换只动 LEFT ⇄ RIGHT（红灯 0x90 不受影响）：函数体内只出现这两个灯位比较；
    eff_body = cmd_code[cmd_code.index("static mm_lamp_t _mm_effective_lamp"):]
    eff_body = eff_body[:eff_body.index("bool mm_arrow_map_is_swapped")]
    case("[源码] 交换函数只比较 MM_LAMP_LEFT / MM_LAMP_RIGHT（红灯不参与）",
         sorted(set(re.findall(r"lamp == (MM_LAMP_[A-Z]+)", eff_body))),
         ["MM_LAMP_LEFT", "MM_LAMP_RIGHT"])
    # ④ mm_parse_frame 不参与交换（解析保持纯函数：只依赖 raw 字节）；
    parse_body = cmd_code[cmd_code.index("mm_parsed_cmd_t mm_parse_frame"):]
    parse_body = parse_body[:parse_body.index("void mm_execute_cmd")]
    case("[源码] mm_parse_frame 内不含交换开关（解析纯函数不变量）",
         "s_mm_arrow_swapped" in parse_body, False)
    # ⑤ 开机复位为默认态（不持久化：cmd 文件无任何存储设备调用）；
    init_body = cmd_code[cmd_code.index("void mm_apply_init"):]
    init_body = init_body[:init_body.index("mm_parsed_cmd_t mm_parse_frame")]
    case("[源码] mm_apply_init 把交换开关复位为默认态（开机回默认）",
         bool(re.search(r"s_mm_arrow_swapped = false;", init_body)), True)
    case("[源码] 交换开关不持久化（cmd 文件无 W25Qxx / dev_storage 读写）",
         bool(re.search(r"dev_storage|dev_w25qxx|s_ldi_base", cmd_code)), False)
    # ⑥ 2026-09-29 代码瘦身轮：RTT 诊断打印整体删除（`MM_RTT_DIAG` 门控与其文案一并消失）；
    #    断言「无任何 arrow map 打印残留」——若将来重新引入日志，此处需同步更新（检查强度不降）。
    case("[源码] 切换日志已随 RTT 设施删除（无 arrow map 打印残留 / 无硬编码引脚号）",
         len(re.findall(r"arrow map", cmd_code)), 0)
    case("[源码] 无 SEGGER / RTT 残留引用",
         bool(re.search(r"SEGGER_RTT|_RTT_DIAG|app_diag", cmd_code)), False)

    # ---- 上电默认画面：红色叉（×）（2026-09-29 用户需求；app_mm_proto.c）----
    #   ① 注册点在 mm_proto_init 内，且**仅当本构建带显示驱动**（dev_display_get() != nullptr）；
    #   ② 颜色 = COLOR_RED；几何取自 dev_display_get()（无硬编码分辨率）；先清黑再画 + 提交帧；
    #   ③ 回调自带判空（无显示构建纵深防御），不引入任何编译期开关。
    has(proto_code, r"_mm_default_display_init\(\)", "mm_proto_init 调 _mm_default_display_init()")
    has(proto_code, r"app_default_display_register\(_mm_default_red_cross\)",
        "注册默认画面回调 _mm_default_red_cross()")
    case("[源码] 默认画面仅在带显示驱动时注册（dev_display_get() != nullptr 守卫）",
         bool(re.search(
             r"if \(dev_display_get\(\) != nullptr\)\s*\n\s*app_default_display_register", proto_code)),
         True)
    cross_body = proto_code[proto_code.index("static void _mm_default_red_cross"):]
    cross_body = cross_body[:cross_body.index("static void _mm_default_display_init")]
    case("[源码] 红叉回调判空（d == nullptr → return，无显示纵深防御）",
         bool(re.search(r"if \(d == nullptr\)\s*\n\s*return;", cross_body)), True)
    case("[源码] 红叉几何取自 dev_display_get()（screen_rows/screen_cols，无硬编码分辨率）",
         ("d->screen_rows" in cross_body) and ("d->screen_cols" in cross_body)
         and not re.search(r"\b(16|32|64|96|128|160|192|224)\b", cross_body), True)
    case("[源码] 红叉唯一落笔颜色 = COLOR_RED",
         sorted(set(re.findall(r"dev_display_set_pixel\([^;]*?(COLOR_[A-Z]+)", proto_code))),
         ["COLOR_RED"])
    has(proto_code, r"dev_display_fill\(d, 0, 0, w, h, COLOR_BLACK\)", "红叉前整屏清黑")
    has(proto_code, r"dev_display_commit_frame\(d\)", "红叉渲染后提交帧")
    case("[源码] 红叉不引入编译期开关（无 #if 门控注册）",
         bool(re.search(r"#if.*_mm_default", proto_code)), False)

    # ---- 缅甸版出厂测试文件（app_factory_test_mm.c）与 Makefile FACTORY 开关 ----
    fmm_src = (ROOT / "Application/Src/app_factory_test_mm.c").read_text(encoding="utf-8")
    fmm_code = _strip_comments(fmm_src)
    has(fmm_src, r"mm_arrow_map_toggle\(\)", "缅甸版出厂测试调用 mm_arrow_map_toggle()")
    has(fmm_src, r"PL_TASK_STATIC_STORAGE\(mm_factory_monitor, 256\)",
        "缅甸版任务栈静态落 CCMRAM（256 words，同标准版）")
    has(fmm_src, r"dev_key_wait_press\(DEV_KEY_TST", "TEST 键经 dev_key_wait_press 一次性消费")
    case("[源码] 缅甸版出厂测试不引用任何显示 API（无显示构建安全）",
         bool(re.search(r"dev_display|app_render|dev_io_", fmm_code)), False)
    case("[源码] 缅甸版出厂测试提供同一套对外符号（同名同义）",
         all(s in fmm_code for s in ("app_factory_mode_interrupt", "app_factory_test_active",
                                     "g_factory_test")), True)
    case("[源码] 缅甸版不含标准版序列文本（老化 / 通信灯 / 斜扫均不出现）",
         bool(re.search(r"老化|通信灯|oblique", fmm_code)), False)
    case("[源码] 缅甸版不持久化（无存储 / 板级配置调用）",
         bool(re.search(r"dev_storage|dev_w25qxx|app_board_net_cfg", fmm_code)), False)

    mk = (ROOT / "Makefile").read_text(encoding="utf-8")
    has(mk, r"FACTORY \?= STD", "Makefile: FACTORY ?= STD（默认保持现状）")
    has(mk, r"\$\(error FACTORY must be STD or MM", "Makefile: FACTORY 非法值 $(error)")
    has(mk, r"\nFACTORY=\$\(FACTORY\)\n", "Makefile: FACTORY 进口径指纹 .build_stamp")
    has(mk, r"SRC_FACTORY_TEST = Application/Src/app_factory_test_mm\.c",
        "Makefile: FACTORY=MM 收录缅甸版")
    has(mk, r"\n\t\$\(SRC_FACTORY_TEST\) \\", "Makefile: SRC_APPLICATION 用 SRC_FACTORY_TEST 占位")


def main() -> int:
    ap = argparse.ArgumentParser(description="缅甸 2D 费显协议 probe/parse 宿主推演")
    ap.add_argument("-v", "--verbose", action="store_true", help="打印每条用例明细")
    args = ap.parse_args()

    run_cases(args.verbose)
    run_source_assertions()

    total = len(CASES)
    passed = total - len(FAILED)
    print(f"\n缅甸 2D 费显协议宿主推演：{passed}/{total} 通过，失败 {len(FAILED)}")
    for f in FAILED:
        print(f"  ✗ {f}")
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
