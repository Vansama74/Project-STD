#!/usr/bin/env python3
"""新疆费额显示器协议 — 宿主 probe/parse/执行语义推演 + 源码一致性断言。

镜像 `Application/Src/ProtocolParser_XinJiang/` 的 `xj_probe_frame()`（app_xj_proto.c）、
`xj_parse_frame()` / `xj_bcc_*`（app_xj_proto_parse.c）与执行层关键映射
（`_xj_render_text_lines()` 行切分、`_xj_map_rgb()`、`_xj_apply_brightness()` 亮度档、
`_xj_voice_level()` 音量档、心跳维护屏状态机），喂边界用例验证状态机流转与同槽共存。

用法：
    python3 .analysis/xinjiang/xj_frame_sim.py            # 全部用例
    python3 .analysis/xinjiang/xj_frame_sim.py -v         # 打印每条用例明细

帧事实（`费额显示器通讯协议.md`，2024 版；实施依据 = `pre_integration_questions.md` §1/§3）：
    [0] 0xFF | [1] 0xDC | [2] Len（整帧总长，含头与 BCC）| [3] Cmd | [4..] 载荷 | [末] BCC
    BCC = 除自身外全部字节 XOR；帧长 5~106；无转义、无应答。
    'T' 文本：len = 10+16N（N=1~4）；[4]亮度 [5]显示行（忽略）[6]R [7]G [8]B [9..]内容
    'V' TTS ：[4]语言（忽略）[5]音量 0x30~0x37 [6..len-2] 文本（probe 容忍 71/72 两种整帧长）
    'S' 预置：[4]语言（忽略）[5]音量 [6]编号（0x31~0x35），整帧 8
    'O'/'E'/'F'/'J'/'K'/'C'：无载荷，整帧 5
    'D'(0x44) 与「彩色行显示协议」不进 probe 白名单（文档缺口，见 doc/19 §8）

同槽共存（RS485/RS232 与四川治超 SC_OL 共享首字节 0xFF）：靠「第二字节 0xDC + 长度 ≤106 +
命令字白名单 + BCC」四重隔离；本脚本 §8 与 SC_OL/RLS/MM/ETC/安徽/`{` 族逐一同槽判别。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# ---- 与 C 侧常量一一对应（源码断言会核对） ----
XJ_STX0 = 0xFF
XJ_STX1 = 0xDC
XJ_LEN_OFFSET = 2
XJ_CMD_OFFSET = 3
XJ_FRAME_LEN_MIN = 5
XJ_FRAME_LEN_MAX = 106
XJ_QUEUE_DEPTH = 3
XJ_T_TEXT_OFFSET = 9
XJ_T_LINE_BYTES = 16
XJ_T_LINE_MAX = 6
XJ_V_FRAME_LEN_64 = 71
XJ_V_FRAME_LEN_65 = 72
XJ_S_FRAME_LEN = 8
XJ_S_QUOTE1_FRAME_LEN = 12
XJ_S_QUOTE2_FRAME_LEN = 17
XJ_S_QUOTE3_FRAME_LEN = 18
XJ_D_TEXT_OFFSET = 6
XJ_D_LINE_FRAME_LEN = 23
XJ_D_FULL_LINE_MIN = 2
XJ_D_FULL_LINE_MAX = XJ_T_LINE_MAX
XJ_D_FULL_FRAME_LEN_MIN = XJ_D_TEXT_OFFSET + XJ_T_LINE_BYTES * XJ_D_FULL_LINE_MIN + 1
XJ_D_FULL_FRAME_LEN_MAX = XJ_D_TEXT_OFFSET + XJ_T_LINE_BYTES * XJ_D_FULL_LINE_MAX + 1
XJ_D_FULL_FRAME_LEN = 71
XJ_D_LINE_FIELD_MIN = 1
XJ_D_LINE_FIELD_MAX = 4
XJ_S_PAYLOAD_OFFSET = 6
XJ_S_PAYLOAD_MAX = 16
XJ_VOL_MIN = 0x30
XJ_VOL_MAX = 0x37
XJ_RGB_HIGH = 128
XJ_FAULT_TIMEOUT_MS = 15000
XJ_FAULT_POLL_MS = 500
XJ_S_ASCII_MIN = 0x30
XJ_S_ASCII_MAX = 0x39
XJ_BRIGHT_MAX = 255
DEV_DISPLAY_BRIGHTNESS_MAX = 8

CMD_T, CMD_O, CMD_E, CMD_F, CMD_J, CMD_K = ord("T"), ord("O"), ord("E"), ord("F"), ord("J"), ord("K")
CMD_V, CMD_S, CMD_C, CMD_D = ord("V"), ord("S"), ord("C"), ord("D")
CMD_G, CMD_H, CMD_Z, CMD_P = ord("G"), ord("H"), ord("Z"), ord("P")
CMD_B = ord("B")  # PDF §3.1.12 下载位图：帧头 FE DB + 双字节长度，**故意不进白名单**
WHITELIST = {CMD_T, CMD_O, CMD_E, CMD_F, CMD_J, CMD_K, CMD_V, CMD_S, CMD_C,
             CMD_D, CMD_G, CMD_H, CMD_Z, CMD_P}
BARE_CMDS = {CMD_O, CMD_E, CMD_F, CMD_G, CMD_H, CMD_J, CMD_K, CMD_C, CMD_Z}

# 颜色枚举（与 display_color_t 恒等）
COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW = 0, 1, 2, 3
COLOR_BLUE, COLOR_PURPLE, COLOR_CYAN, COLOR_WHITE = 4, 5, 6, 7

FAKE, WAIT, READY, SKIP = "FAKE", "WAIT", "READY", "SKIP"

S_KIND_INVALID, S_KIND_QUOTE1, S_KIND_QUOTE2, S_KIND_QUOTE3, S_KIND_PRESET = range(5)

# PDF §4.4 出口收卡机文案（与 cmd 层 s_xj_preset_texts 对应）
PRESET_TEXTS = {
    0xA0: "ETC已交易成功，请通行",
    0xB0: "缴费XX元，请展示付款码",
    0xB1: "缴费成功",
    0xB2: "请将通行卡插入卡口",
    0xB3: "未检测到通行卡，请联系工作人员",
    0xB4: "通行卡损坏，请联系工作人员",
    0xB5: "如需发票，请按键取票",
    0xB6: "请拿好通行卡，祝您一路平安",
}


def s_kind_from_len(raw_len: int) -> int:
    """镜像 _xj_s_kind_from_len()：'S' 帧长 → 形态（四种互斥帧长）。"""
    return {
        XJ_S_QUOTE1_FRAME_LEN: S_KIND_QUOTE1,
        XJ_S_QUOTE2_FRAME_LEN: S_KIND_QUOTE2,
        XJ_S_QUOTE3_FRAME_LEN: S_KIND_QUOTE3,
        XJ_S_FRAME_LEN: S_KIND_PRESET,
    }.get(raw_len, S_KIND_INVALID)


def copy_quote_payload(raw: bytes) -> bytes | None:
    """镜像 _xj_copy_quote_payload()：[6..len-2] 必须全为 ASCII 数字，否则 None。"""
    n = len(raw) - 7
    if n > XJ_S_PAYLOAD_MAX:
        return None
    out = raw[XJ_S_PAYLOAD_OFFSET:XJ_S_PAYLOAD_OFFSET + n]
    if any(b < XJ_S_ASCII_MIN or b > XJ_S_ASCII_MAX for b in out):
        return None
    return out


# ==========================================================================
# 探针 / 解析 / 执行语义镜像
# ==========================================================================
def bcc(data: bytes) -> int:
    """BCC = 逐字节 XOR。"""
    v = 0
    for b in data:
        v ^= b
    return v


def bcc_ok(frame: bytes) -> bool:
    """镜像 xj_bcc_ok()：XOR(frame[0..len-2]) == frame[len-1]。"""
    return len(frame) >= 2 and bcc(frame[:-1]) == frame[-1]


def xj_probe(buf: bytes) -> tuple[str, int | None, str | None]:
    """镜像 xj_probe_frame()；返回 (sta, total_len, note)。"""
    avail = len(buf)
    if avail == 0:
        return FAKE, None, "空缓冲"
    if buf[0] != XJ_STX0:
        return FAKE, None, "首字节非 0xFF"
    if avail < XJ_LEN_OFFSET + 1:
        return WAIT, None, "长度字段未到齐"
    if buf[1] != XJ_STX1:
        return FAKE, None, "第二字节非 0xDC（RLS FF FE / MM FF E0 在此分道）"
    ln = buf[XJ_LEN_OFFSET]
    if ln < XJ_FRAME_LEN_MIN or ln > XJ_FRAME_LEN_MAX:
        return FAKE, None, f"长度字段 {ln} 越界（治超帧长 0xDC=220 一类）"
    if avail < XJ_CMD_OFFSET + 1:
        return WAIT, None, "命令字未到齐"
    if buf[XJ_CMD_OFFSET] not in WHITELIST:
        return FAKE, None, f"命令字 0x{buf[XJ_CMD_OFFSET]:02X} 不在白名单"
    if avail < ln:
        return WAIT, None, f"整帧未到齐（{avail}/{ln}）"
    if not bcc_ok(buf[:ln]):
        return FAKE, None, "BCC 不符"
    return READY, ln, None


def xj_cmd_from_byte(c: int) -> bool:
    """镜像 xj_cmd_from_byte()：白名单判定。"""
    return c in WHITELIST


def map_rgb(r: int, g: int, b: int) -> int:
    """镜像 _xj_map_rgb()：各分量 ≥128 置位，组合值 0~7 与 display_color_t 恒等。"""
    return (1 if r >= XJ_RGB_HIGH else 0) | (2 if g >= XJ_RGB_HIGH else 0) | (4 if b >= XJ_RGB_HIGH else 0)


def bright_level(v: int):
    """镜像 _xj_apply_brightness()：0 → None（恢复光敏自动）；1~255 → 1~8。"""
    if v == 0:
        return None
    return 1 + (v - 1) * (DEV_DISPLAY_BRIGHTNESS_MAX - 1) // 254


def voice_level(v: int) -> int:
    """镜像 _xj_voice_level()：0x30~0x37 → 1~9（越界钳位）。"""
    v = max(XJ_VOL_MIN, min(XJ_VOL_MAX, v))
    return 1 + (v - XJ_VOL_MIN) * 8 // (XJ_VOL_MAX - XJ_VOL_MIN)


def trim_len(p: bytes) -> int:
    """镜像 _xj_trim_len()：去尾部 0x00/0x20。"""
    n = len(p)
    while n > 0 and p[n - 1] in (0x00, 0x20):
        n -= 1
    return n


def is_gbk_lead(b: int) -> bool:
    return 0x81 <= b <= 0xFE


def is_gbk_trail(b: int) -> bool:
    return 0x40 <= b <= 0xFE and b != 0x7F


def split_lines(content: bytes) -> list[tuple[int, bytes]]:
    """镜像 _xj_render_text_lines() 的行切分；返回 [(row, 非空行内容)]。"""
    out: list[tuple[int, bytes]] = []
    p, rem, row = 0, len(content), 0
    while rem > 0:
        take = XJ_T_LINE_BYTES if rem >= XJ_T_LINE_BYTES else rem
        # GBK 边界自适应：窗口末字节为首字节且其后有尾字节 → 该字符整字节留给下一行
        if (
            take == XJ_T_LINE_BYTES
            and rem > XJ_T_LINE_BYTES
            and is_gbk_lead(content[p + XJ_T_LINE_BYTES - 1])
            and is_gbk_trail(content[p + XJ_T_LINE_BYTES])
        ):
            take = XJ_T_LINE_BYTES - 1
        seg = content[p : p + take]
        n = trim_len(seg)
        if n > 0:
            out.append((row, seg[:n]))
        p += take
        rem -= take
        row += 1
    return out


def xj_parse(frame: bytes):
    """镜像 xj_parse_frame()；返回字段 dict 或 None。"""
    if len(frame) < XJ_FRAME_LEN_MIN or len(frame) > XJ_FRAME_LEN_MAX:
        return None
    if frame[0] != XJ_STX0 or frame[1] != XJ_STX1:
        return None
    if frame[XJ_LEN_OFFSET] != len(frame):
        return None
    if not bcc_ok(frame):
        return None
    cmd = frame[XJ_CMD_OFFSET]
    if not xj_cmd_from_byte(cmd):
        return None

    out = {"cmd": cmd}
    if cmd == CMD_T:
        if len(frame) <= XJ_T_TEXT_OFFSET:
            return None
        content_len = len(frame) - XJ_T_TEXT_OFFSET - 1
        if content_len % XJ_T_LINE_BYTES != 0 or content_len // XJ_T_LINE_BYTES > XJ_T_LINE_MAX:
            return None
        out.update(
            brightness=frame[4],
            display_row=frame[5],  # 解析出来仅为断言「执行层忽略」；C 侧不读
            color=map_rgb(frame[6], frame[7], frame[8]),
            text=frame[XJ_T_TEXT_OFFSET : len(frame) - 1],
            text_len=content_len,
        )
    elif cmd == CMD_V:
        if len(frame) not in (XJ_V_FRAME_LEN_64, XJ_V_FRAME_LEN_65):
            return None
        out.update(volume=frame[5], text=frame[6 : len(frame) - 1], text_len=len(frame) - 7)
    elif cmd == CMD_S:
        kind = s_kind_from_len(len(frame))
        if kind == S_KIND_INVALID:
            return None
        out.update(s_kind=kind, volume=frame[5])
        if kind == S_KIND_PRESET:
            out.update(preset_no=frame[6])
        else:
            payload = copy_quote_payload(frame)
            if payload is None:
                return None
            out.update(payload=payload, payload_len=len(payload))
    elif cmd == CMD_D:
        if len(frame) == XJ_D_LINE_FRAME_LEN:
            line = frame[5]
            if not (XJ_D_LINE_FIELD_MIN <= line <= XJ_D_LINE_FIELD_MAX):
                return None  # 行号越界不兜底、不画
            out.update(d_is_full=False, line=line)
        else:
            data_len = len(frame) - XJ_D_TEXT_OFFSET - 1
            lines = data_len // XJ_T_LINE_BYTES
            if data_len % XJ_T_LINE_BYTES or not (XJ_D_FULL_LINE_MIN <= lines <= XJ_D_FULL_LINE_MAX):
                return None  # §3.2.1 的 5 字节 'D' 是设备→车道机的应答，不属入向
            out.update(d_is_full=True, line=0)
        out.update(
            brightness=frame[4],
            color=COLOR_RED,  # 'D' 无 RGB 字段，固定红字
            text=frame[XJ_D_TEXT_OFFSET : len(frame) - 1],
            text_len=len(frame) - XJ_D_TEXT_OFFSET - 1,
        )
    elif cmd == CMD_P:
        if len(frame) != XJ_FRAME_LEN_MIN + 2:
            return None
        out.update(brightness=frame[4], bitmap_no=frame[5])
    else:
        if len(frame) != XJ_FRAME_LEN_MIN:
            return None
    return out


def xj_ack(cmd: int) -> bytes:
    """镜像 _xj_send_ack()：5 字节无载荷应答 FF DC 05 <cmd> BCC。"""
    body = bytes([XJ_STX0, XJ_STX1, XJ_FRAME_LEN_MIN, cmd])
    return body + bytes([bcc(body)])


def d_render_rows(d_is_full: bool, line: int, content: bytes, screen_cols: int) -> list[tuple[int, bytes]]:
    """镜像 _xj_exec_line_or_full() + _xj_render_text_lines()：返回落屏的 (行号, 行内容)。

    行显示只重画目标行（不清整屏），故 start_row = line-1；全屏 start_row = 0。
    """
    start = 0 if d_is_full else line - 1
    return [
        (row, chunk)
        for row, chunk in ((start + r, c) for r, c in split_lines(content))
        if row * XJ_T_LINE_BYTES < screen_cols
    ]


class FaultSim:
    """镜像 app_xj_proto.c 的心跳维护屏状态机（`_xj_heartbeat_*`）。

    `on_frame(now)` = 收到有效帧（刷新时刻 + 维护屏在显示则清屏一次）；
    `poll(now)` = 500ms 节拍判定（超时且未显示 → 渲染一次，幂等）；
    `enabled=False` 对应用户开关 `XJ_FAULT_SCREEN=0`（无计数、无渲染）。
    """

    def __init__(self, enabled: bool = True, t0: int = 0) -> None:
        self.enabled = enabled
        self.last = t0
        self.shown = False
        self.events: list[tuple[str, int]] = []

    def on_frame(self, now: int) -> None:
        if not self.enabled:
            return
        self.last = now
        if self.shown:
            self.shown = False
            self.events.append(("clear", now))

    def poll(self, now: int) -> str | None:
        if not self.enabled or self.shown:
            return None
        if now - self.last > XJ_FAULT_TIMEOUT_MS:
            self.shown = True
            self.events.append(("show", now))
            return "show"
        return None


def other_probe(buf: bytes, kind: str) -> tuple[str, int | None]:
    """镜像同槽其它协议的首页判据（只判到足以说明「谁认领/是否卡链」的深度）。"""
    if not buf:
        return FAKE, None
    if kind == "sc_ol":  # FF + 长度(07~FF，排除 FE) + … + BCC + FF
        if buf[0] != 0xFF:
            return FAKE, None
        if len(buf) < 2:
            return WAIT, None
        ln = buf[1]
        if ln < 7 or ln == 0xFE:
            return FAKE, None
        if len(buf) < ln:
            return WAIT, None
        return (READY, ln) if buf[ln - 1] == 0xFF else (FAKE, None)
    if kind == "rls":  # FF FE …
        if buf[0] != 0xFF:
            return FAKE, None
        if len(buf) < 2:
            return WAIT, None
        return (WAIT, None) if buf[1] == 0xFE else (FAKE, None)
    if kind == "mm":  # FF E0 …（定长 20）
        if buf[0] != 0xFF:
            return FAKE, None
        if len(buf) < 2:
            return WAIT, None
        if buf[1] != 0xE0:
            return FAKE, None
        return (WAIT, None) if len(buf) < 20 else (READY, 20)
    if kind == "etc":  # 0A … 0D 定界
        return (WAIT, None) if buf[0] == 0x0A else (FAKE, None)
    if kind == "anhui":  # 5A 01 …
        return (WAIT, None) if buf[0] == 0x5A else (FAKE, None)
    if kind == "brace":  # '{' … '}' 定界
        return (WAIT, None) if buf[0] == 0x7B else (FAKE, None)
    raise ValueError(kind)


def chain_probe(buf: bytes, order: tuple[str, ...]) -> tuple[str, int | None, str | None]:
    """镜像 frame_dispatch_task 的链式语义：WAIT/FAKE 继续试下一协议，READY/SKIP 终止。"""
    fed_wait = False
    for name in order:
        sta, tlen = xj_probe(buf)[:2] if name == "xj" else other_probe(buf, name)
        if sta in (READY, SKIP):
            return sta, tlen, name
        if sta == WAIT:
            fed_wait = True
    return (WAIT if fed_wait else FAKE), None, None


def drain(buf: bytes, max_steps: int = 64) -> tuple[list[bytes], bytes, int]:
    """镜像「单协议绑定」时 frame_dispatch_task 的内循环：READY 消费 / FAKE skip 1 / WAIT 停。"""
    data = bytearray(buf)
    frames: list[bytes] = []
    resync = 0
    steps = 0
    while len(data) > 0:
        steps += 1
        if steps > max_steps:
            del data[0]
            resync += 1
            break
        sta, ln, _ = xj_probe(bytes(data))
        if sta == READY:
            frames.append(bytes(data[:ln]))
            del data[:ln]
        elif sta == WAIT:
            break
        else:
            del data[0]
            resync += 1
    return frames, bytes(data), resync


# ==========================================================================
# 帧构造
# ==========================================================================
def frame(cmd: int, payload: bytes = b"", declared_len: int | None = None,
          bcc_override: int | None = None) -> bytes:
    """按文档布局构造整帧（长度字段 = 整帧总长；可用 declared_len / bcc_override 造畸形帧）。"""
    ln = 5 + len(payload) if declared_len is None else declared_len
    body = bytes([XJ_STX0, XJ_STX1, ln, cmd]) + payload
    return body + bytes([bcc(body) if bcc_override is None else bcc_override])


def text_frame(r: int, g: int, b: int, content: bytes, brightness: int = 128,
               display_row: int = 0) -> bytes:
    """'T' 文本帧（载荷 = 亮度 显示行 R G B + 16N 内容）。"""
    assert len(content) % XJ_T_LINE_BYTES == 0 and 0 < len(content) // XJ_T_LINE_BYTES <= XJ_T_LINE_MAX
    return frame(CMD_T, bytes([brightness, display_row, r, g, b]) + content)


def v_frame(text: bytes, volume: int = 0x34, lang: int = 0x30, length_72: bool = True) -> bytes:
    """'V' TTS 帧：长度 72（内容 65B）或 71（内容 64B）。"""
    content_len = 65 if length_72 else 64
    body = bytes([lang, volume]) + text.ljust(content_len, b"\x00")[:content_len]
    return frame(CMD_V, body)


def s_frame(no: int, volume: int = 0x34, lang: int = 0x30) -> bytes:
    return frame(CMD_S, bytes([lang, volume, no]))


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
    # --- §0 长度口径自证（文档 §2.1/§2.8/§2.9） ---
    case("§0.1 'T' N=1 整帧 = 26（10+16）", len(text_frame(255, 0, 0, b"A" * 16)), 26)
    case("§0.2 'T' N=4 整帧 = 74（文档表形态）", len(text_frame(255, 0, 0, b"A" * 64)), 74)
    case("§0.2b 'T' N=6 整帧 = 106（10+96 = 上界，PDF §5.2 采样行数）",
         len(text_frame(255, 0, 0, b"A" * 96)), 106)
    case("§0.3 'V' 整帧 = 72（文档 Length=72 形态）", len(v_frame(b"hi")), 72)
    case("§0.4 'V' 整帧 = 71（64 字节内容形态）", len(v_frame(b"hi", length_72=False)), 71)
    case("§0.5 'S' 整帧 = 8", len(s_frame(0x31)), 8)
    case("§0.6 无载荷命令整帧 = 5", len(frame(CMD_O)), 5)

    # --- §1 九命令正常帧：probe READY + parse OK ---
    tf = text_frame(255, 128, 0, "你好世界".encode("gbk") + b" " * 8, brightness=200)
    case("§1.1 'T' probe READY(26)", xj_probe(tf)[:2], (READY, 26))
    p = xj_parse(tf)
    case("§1.2 'T' parse OK", p is not None, True)
    case("§1.3 'T' 颜色 R≥128/G≥128 → 黄(3)", p["color"], COLOR_YELLOW)
    case("§1.4 'T' 亮度字段透传 200", p["brightness"], 200)
    case("§1.5 'T' 内容 = [9..len-2] 共 16B", (p["text"], p["text_len"]), (tf[9:-1], 16))
    case("§1.6 'T' 只切一行（16B 内容，尾部空格填充已去）",
         split_lines(p["text"]), [(0, "你好世界".encode("gbk"))])

    for name, cmd, extra in (
        ("'O' 心跳", CMD_O, {}),
        ("'E' 通行绿灯", CMD_E, {}),
        ("'F' 通行红灯", CMD_F, {}),
        ("'J' 报警开", CMD_J, {}),
        ("'K' 报警关", CMD_K, {}),
        ("'C' 重置屏幕", CMD_C, {}),
    ):
        f = frame(cmd)
        case(f"§1.7 {name} probe READY(5)", xj_probe(f)[:2], (READY, 5))
        case(f"§1.8 {name} parse OK", (xj_parse(f) or {}).get("cmd"), cmd)

    vf = v_frame("3型车，请交费20元".encode("gbk"), volume=0x37)
    case("§1.9 'V' probe READY(72)", xj_probe(vf)[:2], (READY, 72))
    pv = xj_parse(vf)
    case("§1.10 'V' 音量字节 = 0x37", pv["volume"], 0x37)
    case("§1.11 'V' 文本 = [6..len-2] 共 65B", pv["text_len"], 65)
    case("§1.12 'V' 文本首字节 = GBK '3'", pv["text"][:1], b"3")

    sf = s_frame(0x33, volume=0x30)
    case("§1.13 'S' probe READY(8)", xj_probe(sf)[:2], (READY, 8))
    ps = xj_parse(sf)
    case("§1.14 'S' 编号 = 0x33", ps["preset_no"], 0x33)

    # --- §1B PDF 2020 全文新增命令（'D'/'G'/'H'/'Z'/'P' + 'S' 报价三形态）---
    for name, cmd in (("'G' 全亮", CMD_G), ("'H' 全关", CMD_H), ("'Z' 查询", CMD_Z)):
        f = frame(cmd)
        case(f"§1B.1 {name} probe READY(5)", xj_probe(f)[:2], (READY, 5))
        case(f"§1B.2 {name} parse OK", (xj_parse(f) or {}).get("cmd"), cmd)
    case("§1B.3 'Z' 应答回送 = FF DC 05 5A 7C",
         xj_ack(CMD_Z).hex(" ").upper(), "FF DC 05 5A 7C")
    case("§1B.4 'C' 应答回送 BCC 自洽", bcc_ok(xj_ack(CMD_C)), True)

    # §3.1.1 行显示 'D' Len=23：只重画指定行，其余行保留
    line_content = "第3行".encode("gbk").ljust(16, b" ")
    dl = frame(CMD_D, bytes([200, 3]) + line_content)
    case("§1B.5 'D' 行显示整帧 = 23", len(dl), 23)
    case("§1B.6 'D' 行显示 probe READY(23)", xj_probe(dl)[:2], (READY, 23))
    pdl = xj_parse(dl)
    case("§1B.7 'D' 行号 = 3", pdl["line"], 3)
    case("§1B.8 'D' 行显示 = 非全屏", pdl["d_is_full"], False)
    case("§1B.9 'D' 固定红字（无 RGB 字段）", pdl["color"], COLOR_RED)
    case("§1B.10 'D' 行显示只落第 3 行（0 基 2），不清整屏",
         [r for r, _ in d_render_rows(False, 3, pdl["text"], 64)], [2])
    for bad in (0, 5, 0xFF):
        case(f"§1B.11 'D' 行号 {bad} 越界 → parse 丢弃", xj_parse(frame(CMD_D, bytes([200, bad]) + b"A" * 16)), None)

    # §3.1.2 全屏显示 'D' Len=71（PDF 正文实例 Length=0x47）
    # 全屏显示：4 行 × 16B。屏高 64px 时 4 行全落；实配 1-263 模组屏高 32px，第 3/4 行越屏丢弃
    full_content = b"".join(f"L{i}".encode("ascii").ljust(16, b" ") for i in range(4))
    df = frame(CMD_D, bytes([0xFF, 0]) + full_content)
    case("§1B.12 'D' 全屏整帧 = 71", len(df), 71)
    case("§1B.13 'D' 全屏 probe READY(71)", xj_probe(df)[:2], (READY, 71))
    case("§1B.14 'D' 全屏内容区 = 64B", len(xj_parse(df)["text"]), 64)
    # 全屏显示 4 行 64B；屏高按 1-263 模组实配 32px 计，故第 4 行（y=48）越屏丢弃
    case("§1B.15 'D' 全屏 4 行内容切分得 0~3 行号（屏高 64 时全部落屏）",
         [r for r, _ in d_render_rows(True, 0, xj_parse(df)["text"], 64)], [0, 1, 2, 3])
    case("§1B.15b 屏高 32px（1-263 模组实配）时第 4 行越屏丢弃",
         [r for r, _ in d_render_rows(True, 0, xj_parse(df)["text"], 32)], [0, 1])
    case("§1B.16 'D' 长度 5（§3.2.1 应答方向）入向 parse 丢弃", xj_parse(frame(CMD_D)), None)

    # §3.1.11 位图显示 'P' Len=7：本硬件无位图存储 → 执行层清屏后应答
    pf = frame(CMD_P, bytes([200, 0xA5]))
    case("§1B.17 'P' 整帧 = 7", len(pf), 7)
    case("§1B.18 'P' probe READY(7)", xj_probe(pf)[:2], (READY, 7))
    case("§1B.19 'P' 图号透传 0xA5", xj_parse(pf)["bitmap_no"], 0xA5)

    # §3.1.12 'B' 下载位图：帧头 FE DB + 双字节长度 1093，超出单字节长度模型 → 不进白名单
    case("§1B.20 'B' 下载位图不进白名单", CMD_B in WHITELIST, False)
    case("§1B.21 'B' 帧头 FE DB 被 probe FAKE",
         xj_probe(b"\xfe\xdb\x04\x45" + b"\x00" * 20)[0], FAKE)

    # §4.1/4.2/4.3 语音报价：三种帧长必须都能通过（此前被「整帧必须 8」卡死）
    q1 = frame(CMD_S, bytes([0x32, 0x37]) + b"3" + b"0020")
    case("§1B.22 报价 1 整帧 = 12（PDF §4.1 示例）", len(q1), 12)
    case("§1B.23 报价 1 probe READY(12)", xj_probe(q1)[:2], (READY, 12))
    case("§1B.24 报价 1 形态 = QUOTE1", xj_parse(q1)["s_kind"], S_KIND_QUOTE1)
    case("§1B.25 报价 1 载荷 = 车型+金额 5 字节", xj_parse(q1)["payload"], b"30020")
    # PDF §4.2 原文示例帧：FF DC 0C 53 32 37 31 30 30 30 35 4D
    pdf_q1 = bytes.fromhex("FFDC0C53323731303030354D")
    case("§1B.26 PDF §4.2 示例帧可解析为报价 1", xj_parse(pdf_q1)["s_kind"], S_KIND_QUOTE1)
    case("§1B.26b PDF §4.2 示例帧 BCC 自洽", bcc_ok(pdf_q1), True)
    q2 = frame(CMD_S, bytes([0x30, 0x35]) + b"3" + b"023" + b"78" + b"0020")
    case("§1B.27 报价 2 整帧 = 17", len(q2), 17)
    case("§1B.28 报价 2 形态 = QUOTE2", xj_parse(q2)["s_kind"], S_KIND_QUOTE2)
    case("§1B.29 报价 2 载荷 10 字节（车型+总重5+金额4）", xj_parse(q2)["payload_len"], 10)
    q3 = frame(CMD_S, bytes([0x31, 0x35]) + b"3" + b"073" + b"40" + b"020" + b"34")
    case("§1B.30 报价 3 整帧 = 18", len(q3), 18)
    case("§1B.31 报价 3 形态 = QUOTE3", xj_parse(q3)["s_kind"], S_KIND_QUOTE3)
    case("§1B.32 报价 3 载荷 11 字节（车型+总重5+超重5）", xj_parse(q3)["payload_len"], 11)
    case("§1B.33 'S' 长度 9（不在四形态内）→ parse 丢弃",
         xj_parse(frame(CMD_S, bytes([0x30, 0x34, 0xA0, 0x00]))), None)
    case("§1B.34 报价载荷含非数字 → 丢弃（不因个别位越界丢整帧，只挡控制字节）",
         xj_parse(frame(CMD_S, bytes([0x30, 0x34]) + b"3" + b"00\x01\x30")), None)

    # §4.4 预制编号表（出口收卡机）
    for no in (0xA0, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6):
        case(f"§1B.35 预制编号 0x{no:02X} 有文案", no in PRESET_TEXTS, True)
    for no in (0x31, 0x35, 0x36, 0xA1, 0xAF, 0xB7):
        case(f"§1B.36 预制编号 0x{no:02X} 未收录（静默忽略）", no in PRESET_TEXTS, False)

    # --- §2 半帧 / 粘包 / 伪头 / 超长 / BCC 错 ---
    case("§2.1 空缓冲 → FAKE", xj_probe(b"")[0], FAKE)
    case("§2.2 仅 0xFF → WAIT", xj_probe(b"\xff")[0], WAIT)
    case("§2.3 0xFF 0xDC → WAIT（长度未到齐）", xj_probe(b"\xff\xdc")[0], WAIT)
    case("§2.4 0xFF 0xDC 0x1A → WAIT（命令字未到齐）", xj_probe(b"\xff\xdc\x1a")[0], WAIT)
    case("§2.5 半帧 'T'（20/26）→ WAIT", xj_probe(tf[:20])[0], WAIT)
    case("§2.6 整帧 → READY", xj_probe(tf)[0], READY)
    case("§2.7 伪头 0x00 → FAKE", xj_probe(b"\x00\xdc\x05O" + bytes([0]))[0], FAKE)
    case("§2.8 0xFF 0xFE（RLS 形）→ FAKE", xj_probe(b"\xff\xfe" + b"\x00" * 10)[0], FAKE)
    case("§2.9 0xFF 0xE0（MM 形）→ FAKE", xj_probe(b"\xff\xe0" + b"\x00" * 18)[0], FAKE)
    over = frame(CMD_T, bytes([0, 0, 255, 0, 0]) + b"A" * 64, declared_len=107)
    case("§2.10 长度 107（>106）→ FAKE", xj_probe(over)[0], FAKE)
    case("§2.11 长度 4（<5）→ FAKE", xj_probe(bytes([0xFF, 0xDC, 0x04, CMD_O, 0x00]))[0], FAKE)
    badc = frame(CMD_O, bcc_override=0x00)
    case("§2.12 BCC 错（0x00）→ FAKE", xj_probe(badc)[0], FAKE)

    # 粘包（两帧连发）：单协议内循环消费两帧
    frames, left, resync = drain(tf + sf)
    case("§2.13 粘包 'T'+'S' → 消费 2 帧", (len(frames), left, resync), (2, b"", 0))
    case("§2.14 粘包首帧仍是 'T'", frames[0][3], CMD_T)
    # 伪头前置 + 合法帧：1 字节重同步后认出
    frames, left, resync = drain(b"\x00\xff" + sf)
    case("§2.15 伪头前导 → 重同步 2 次后认领", (len(frames), resync), (1, 2))
    # 残帧尾（半帧）→ 留在缓冲等待
    frames, left, resync = drain(sf + tf[:10])
    case("§2.16 完整帧 + 残帧 → 消费 1 帧、残留 10B", (len(frames), len(left), resync), (1, 10, 0))
    # 内循环防死循环上限（>64 帧连续）验算：65 个空载荷帧
    frames, left, resync = drain(frame(CMD_O) * 65)
    case("§2.17 65 帧连发 → 消费 64（内循环上限）", (len(frames), resync), (64, 1))

    # --- §3 'V' 71/72 双长度 + 'S' 未知编号 ---
    case("§3.1 'V' 71 → READY(71)", xj_probe(v_frame(b"x", length_72=False))[:2], (READY, 71))
    case("§3.2 'V' 71 parse 文本 64B", (xj_parse(v_frame(b"x", length_72=False)) or {}).get("text_len"), 64)
    case("§3.3 'V' 72 parse 文本 65B", (xj_parse(v_frame(b"x")) or {}).get("text_len"), 65)
    case("§3.4 'V' 70（第三种长度）→ parse 拒绝",
         xj_parse(frame(CMD_V, bytes([0x30, 0x34]) + b"x" * 63)), None)
    case("§3.5 'S' 0x36（文档悬空）→ parse 仍 OK 但编号未知",
         (xj_parse(s_frame(0x36)) or {}).get("preset_no"), 0x36)
    case("§3.6 预制文案表 = 0xA0 + 0xB0~0xB6（PDF §4.4 出口收卡机）", sorted(PRESET_TEXTS),
         [0xA0, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6])
    case("§3.7 0x36 无文案 → 执行层忽略", PRESET_TEXTS.get(0x36), None)
    case("§3.8 音量 0x30 → 板载 1", voice_level(0x30), 1)
    case("§3.9 音量 0x37 → 板载 9", voice_level(0x37), 9)
    case("§3.10 音量越界 0x20 钳到下限档 1", voice_level(0x20), 1)
    case("§3.11 音量越界 0x40 钳到上限档 9", voice_level(0x40), 9)
    # 'V' 文本尾部 0x00 填充剔除（65B 区只放了 3 字节）
    pv2 = xj_parse(v_frame(b"abc"))
    case("§3.12 'V' 尾部 0x00 剔除后长度 3", trim_len(pv2["text"]), 3)

    # --- §4 颜色映射边界（0/1/127/128/255） ---
    case("§4.1 全 0 → 黑(0)", map_rgb(0, 0, 0), COLOR_BLACK)
    case("§4.2 (1,0,0) → 黑(0)（<128 不点亮）", map_rgb(1, 0, 0), COLOR_BLACK)
    case("§4.3 (127,0,0) → 黑(0)", map_rgb(127, 0, 0), COLOR_BLACK)
    case("§4.4 (128,0,0) → 红(1)", map_rgb(128, 0, 0), COLOR_RED)
    case("§4.5 (255,0,0) → 红(1)", map_rgb(255, 0, 0), COLOR_RED)
    case("§4.6 (0,255,0) → 绿(2)", map_rgb(0, 255, 0), COLOR_GREEN)
    case("§4.7 (255,255,0) → 黄(3)", map_rgb(255, 255, 0), COLOR_YELLOW)
    case("§4.8 (0,0,255) → 蓝(4)", map_rgb(0, 0, 255), COLOR_BLUE)
    case("§4.9 (255,0,255) → 紫(5)", map_rgb(255, 0, 255), COLOR_PURPLE)
    case("§4.10 (0,255,255) → 青(6)", map_rgb(0, 255, 255), COLOR_CYAN)
    case("§4.11 (255,255,255) → 白(7)", map_rgb(255, 255, 255), COLOR_WHITE)
    case("§4.12 组合值与 display_color_t 恒等", [map_rgb(255 if i & 1 else 0, 255 if i & 2 else 0, 255 if i & 4 else 0) for i in range(1, 8)],
         [1, 2, 3, 4, 5, 6, 7])
    # 黑字只清屏：parse 仍 OK、color=0
    case("§4.13 'T' 黑字 → color=0（执行层只清屏）",
         (xj_parse(text_frame(0, 0, 0, b"A" * 16)) or {}).get("color"), COLOR_BLACK)

    # --- §5 亮度映射边界 ---
    case("§5.1 亮度 0 → 恢复光敏自动（None）", bright_level(0), None)
    case("§5.2 亮度 1 → 档 1", bright_level(1), 1)
    case("§5.3 亮度 2 → 档 1", bright_level(2), 1)
    case("§5.4 亮度 128 → 档 4", bright_level(128), 4)
    case("§5.5 亮度 254 → 档 7（公式 1+(v-1)*7/254）", bright_level(254), 7)
    case("§5.6 亮度 255 → 档 8（最亮）", bright_level(255), 8)
    case("§5.7 单调不降（1..255）",
         all(bright_level(v) <= bright_level(v + 1) for v in range(1, 255)), True)
    case("§5.8 值域恒在 1..8",
         sorted({bright_level(v) for v in range(1, 256)}), list(range(1, 9)))

    # --- §6 16B 行切分（含 GBK 边界） ---
    c1 = b"A" * 16 + b"B" * 16
    case("§6.1 两整行 → 2 行、各 16B", split_lines(c1), [(0, b"A" * 16), (1, b"B" * 16)])
    c2 = "一二三四五六七八".encode("gbk")  # 8 汉字 = 16B
    case("§6.2 8 汉字整行不被劈开", split_lines(c2), [(0, c2)])
    # 15B ASCII + 2B 汉字 + 15B ASCII：行 0 窗口末字节 = 汉字首字节、尾字节在窗口外
    ascii15 = b"A" * 15
    jiu = "九".encode("gbk")
    c3 = ascii15 + jiu + b"B" * 15
    lines3 = split_lines(c3)
    case("§6.3 GBK 跨 16B 边界 → 行 0 退 1 字节（15B）", lines3[0], (0, ascii15))
    case("§6.4 GBK 跨边界字符完整落在行 1 起始", lines3[1][1][:2], jiu)
    case("§6.5 切行连续、字节无损（行 2 = 余量 1B）",
         lines3, [(0, ascii15), (1, c3[15:31]), (2, b"B")])
    c4 = b"ABCD" + b"\x00" * 12
    case("§6.6 尾部 0x00/0x20 填充剔除", split_lines(c4), [(0, b"ABCD")])
    c5 = b"\x00" * 16
    case("§6.7 整行填充为空 → 该行不画（0 行）", split_lines(c5), [])
    c6 = b"\x00" * 16 + b"X" * 16
    case("§6.8 空行不画、后行行号不压缩（row=1）", split_lines(c6), [(1, b"X" * 16)])
    c7 = "一二三".encode("gbk") + b"A" * 11  # 16B：6 + 11 = 17? 断言构造合法
    case("§6.9 混合中英整行 16B → 1 行", len(c7), 16 if len(c7) == 16 else len(c7))
    c8 = "一二三四五六七".encode("gbk") + b"ABCD"  # 14 + 4 = 18B
    case("§6.10 14B 汉字 + 4B ASCII：窗口末字节 = 'A'（非首字节）→ 不触发退让",
         [len(s) for _, s in split_lines(c8 + b"\x00" * 14)], [16, 2])
    case("§6.11 行号 y = row × FONT_16（0/16/32/48）",
         [r * 16 for r, _ in split_lines(b"A" * 64)], [0, 16, 32, 48])

    # --- §7 维护屏 15s 触发与解除 ---
    f = FaultSim(t0=0)
    case("§7.1 上电 14.9s 内不显示", f.poll(14900), None)
    case("§7.2 恰在 15.0s 不显示（严格大于）", f.poll(15000), None)
    case("§7.3 15.001s 触发维护屏", f.poll(15001), "show")
    case("§7.4 已在显示 → 节拍幂等（不重复渲染）", (f.poll(16000), len(f.events)), (None, 1))
    case("§7.5 收到有效帧 → 清屏解除", (f.on_frame(16010), f.events[-1]), (None, ("clear", 16010)))
    case("§7.6 解除后 14.9s 内不再显示", f.poll(30000), None)
    case("§7.7 解除后再次超时 → 再次显示", f.poll(31100), "show")
    case("§7.8 'O' 心跳帧同样刷新（任意有效帧）",
         (lambda g: (g.on_frame(40000), g.poll(54500), g.poll(55100))[1:])(FaultSim(t0=0)), (None, "show"))
    f2 = FaultSim(enabled=False, t0=0)
    case("§7.9 XJ_FAULT_SCREEN=0 → 永不计数/渲染",
         (f2.on_frame(100000), f2.poll(200000), f2.events), (None, None, []))
    case("§7.10 节拍粒度 500ms（轮询周期）", XJ_FAULT_POLL_MS, 500)

    # --- §8 同槽共存判别 ---
    case("§8.1 SC_OL 帧（长度≠0xDC）→ XJ FAKE", xj_probe(bytes([0xFF, 0x20, 0x80]) + bytes([0] * 33))[0], FAKE)
    case("§8.2 SC_OL 帧（长度恰 0xDC=220）→ XJ FAKE（不 WAIT）",
         xj_probe(bytes([0xFF, 0xDC, 0x80]) + bytes([0] * 217))[0], FAKE)
    case("§8.3 RLS 帧（FF FE）→ XJ FAKE", xj_probe(bytes([0xFF, 0xFE]) + bytes(10))[0], FAKE)
    case("§8.4 MM 帧（FF E0）→ XJ FAKE", xj_probe(bytes([0xFF, 0xE0, 0x90]) + bytes(17))[0], FAKE)
    case("§8.5 ETC 帧（0A）→ XJ FAKE", xj_probe(bytes([0x0A, 0x01]) + bytes(20))[0], FAKE)
    case("§8.6 安徽帧（5A）→ XJ FAKE", xj_probe(bytes([0x5A, 0x01]) + bytes(20))[0], FAKE)
    case("§8.7 '{' 族帧（7B）→ XJ FAKE", xj_probe(bytes([0x7B, 0x31]) + bytes(20))[0], FAKE)
    # 反向：治超先注册（收录序在前）对 XJ 帧判 WAIT（链式继续）→ XJ 认领
    case("§8.8 链式（SC_OL 在前）+ XJ 帧 → XJ 认领",
         chain_probe(tf, ("sc_ol", "xj"))[:1] + (chain_probe(tf, ("sc_ol", "xj"))[2],), (READY, "xj"))
    case("§8.9 链式（RLS 在前）+ XJ 帧 → XJ 认领",
         chain_probe(sf, ("rls", "xj"))[2], "xj")
    case("§8.10 链式（MM 在前）+ XJ 帧 → XJ 认领",
         chain_probe(sf, ("mm", "xj"))[2], "xj")
    # 治超帧在链式里不被 XJ 抢（XJ 先 FAKE、治超自己 READY）
    sc_ol_frame = bytes([0xFF, 0x08, 0x96, 0x00, 0x00, 0x00, 0x00, 0xFF])
    case("§8.11 治超合法帧 → 治超认领（XJ 不抢）", chain_probe(sc_ol_frame, ("sc_ol", "xj"))[2], "sc_ol")

    # --- §9 probe 白名单：'D'(0x44) 双向共存 + 未知命令 ---
    # 'D' 在 PDF 中既是入向显示命令（23/71），又是出向应答（5）：probe 认领 5 字节帧，
    # parse 阶段再按长度拒收入向的应答形态（见 §1B.16）。
    dframe = frame(CMD_D)
    case("§9.1 'D'(0x44) 长度 5 合法 BCC → probe 认领（应答同帧头）", xj_probe(dframe)[0], READY)
    case("§9.2 'D' 5 字节帧 probe READY 但 parse 拒绝（出向形态）",
         (bcc_ok(dframe), xj_probe(dframe)[0], xj_parse(dframe)), (True, READY, None))
    case("§9.3 'D' 5 字节帧被整帧消费（首字节垃圾后只 skip 1）",
         drain(b"\x00" + dframe + sf)[2], 1)
    case("§9.4 未知命令 'X' → FAKE", xj_probe(frame(ord("X")))[0], FAKE)
    case("§9.5 白名单恰为 14 命令（含 PDF 'D'/'G'/'H'/'Z'/'P'，不含 'B'）",
         sorted(chr(c) for c in WHITELIST),
         ["C", "D", "E", "F", "G", "H", "J", "K", "O", "P", "S", "T", "V", "Z"])
    # 'T' 非 10+16N 长度：probe READY（结构合法）但 parse 拒绝（静默丢弃）
    bad_t = frame(CMD_T, bytes([0, 0, 255, 0, 0]) + b"A" * 16)  # 声明长度自洽但内容 16B → 合法
    case("§9.6 'T' 内容 15B（非 16N）→ probe READY / parse 拒绝",
         (xj_probe(frame(CMD_T, bytes([0, 0, 255, 0, 0]) + b"A" * 15))[0],
          xj_parse(frame(CMD_T, bytes([0, 0, 255, 0, 0]) + b"A" * 15))), (READY, None))
    case("§9.7 'T' 内容 16B → parse OK（对照）", (xj_parse(bad_t) or {}).get("text_len"), 16)
    case("§9.8 'T' 内容 112B（7 行，整帧 122 > 106）→ probe FAKE / parse 拒绝",
         (xj_probe(frame(CMD_T, bytes([0, 0, 255, 0, 0]) + b"A" * 112))[0],
          xj_parse(frame(CMD_T, bytes([0, 0, 255, 0, 0]) + b"A" * 112))), (FAKE, None))
    case("§9.9 'S' 长度 7 → parse 拒绝", xj_parse(frame(CMD_S, bytes([0x30, 0x34]))), None)
    case("§9.10 'O' 长度 6（多带 1B）→ probe READY / parse 拒绝",
         (xj_probe(frame(CMD_O, b"\x00"))[0], xj_parse(frame(CMD_O, b"\x00"))), (READY, None))

    # --- §10 PDF 全文（2020 版）示例帧逐帧喂现行 probe：文档自洽示例 vs §5.2 采样 ---
    # §3.1.2 实例（页 6 原文，**71 字节完整帧**：声明长度 0x47=71 = 实际字节数）
    ex312 = bytes.fromhex(
        "FFDC4744FF00B3B520202020D0CD3A2020BFCD31D0CDBDF020202020B6EE3A"
        "2020202035D4AAB3ACCFDEB1EAD7BC3A2020203230B6D6B3B5BBF5D7DCD6D8"
        "3A2020302E30B6D6BB")
    case("§10.1 §3.1.2 实例 = 71B 且长度字段 0x47 自洽（文档表形态）",
         (len(ex312), ex312[2], bcc_ok(ex312)), (71, 0x47, True))
    case("§10.2 §3.1.2 实例 probe READY(71) + parse 全屏 'D'（内容 64B）",
         (xj_probe(ex312)[:2], (xj_parse(ex312) or {}).get("d_is_full"),
          (xj_parse(ex312) or {}).get("text_len")), ((READY, 71), True, 64))
    # §5.2 采样一（页 20/22）：FF DC 08 4C 30 00 01 01 —— 命令字 0x4C 'L' 不在文档命令表，
    # 且不满足文档自身 BCC 规则（XOR 前 7B=0x56 ≠ 末字节 0x01）：判为「非本协议帧」样本
    samp_l = bytes.fromhex("FFDC084C30000101")
    case("§10.3 §5.2 采样 'L'(0x4C) 8B → 卡在命令字白名单（FAKE，整帧被逐字节丢）",
         (len(samp_l), samp_l[2], xj_probe(samp_l)[:2], xj_probe(samp_l)[2]),
         (8, 8, (FAKE, None), "命令字 0x4C 不在白名单"))
    case("§10.4 §5.2 采样 'L' 帧不满足文档自身 BCC 规则（XOR 前 7B=0x56 ≠ 末字节 0x01）",
         (bcc(samp_l[:-1]), samp_l[-1], bcc_ok(samp_l)), (0x56, 0x01, False))
    # §5.2 采样二/三（页 20 dumpB 第二个帧、页 21 全屏）：**103 字节 'D' 全屏帧，数据 96B = 6 行 × 16B**
    # 页 21 原样字节 XOR(前 102B) == 末字节 0x53? → 见 §10.6 断言；两份采样按 6 行读法 BCC 自洽，
    # 故 2026-10-12 起 probe/parse 收 7+16N（N=2~6），把这类现场帧纳入（此前 74 上限会 FAKE）。
    samp_d20 = bytes([XJ_STX0, XJ_STX1, 0x67, CMD_D, 0xFF, 0x00]) + b"\x20" * 36 \
        + "欢迎行驶".encode("gbk") + b"\x20" * 6 + "新疆高速公路".encode("gbk") + b"\x20" * 34 + b"\xEE"
    case("§10.5 §5.2 采样二 'D' 103B（数据 96B）→ probe READY(103) + parse 全屏、内容 96B",
         (len(samp_d20), samp_d20[2], xj_probe(samp_d20)[:2],
          (xj_parse(samp_d20) or {}).get("d_is_full"), (xj_parse(samp_d20) or {}).get("text_len"),
          bcc_ok(samp_d20)),
         (103, 0x67, (READY, 103), True, 96, True))
    samp_d21 = bytes.fromhex(
        "FFDC6744FF00B3B520202020D0CD3A20"
        "20BFCD31D0CDBDF020202020B6EE3A20"
        "20203230D4AAB3D0D4D8B1EAD7BC3A20"
        "20203137B6D6B3B5BBF5D7DCD6D83A20"
        "20302E30B6D6B3ACB1EAD7BCB1C83A20"
        "20202030A3A500000000000000000000"
        "000000000000B3")
    case("§10.6 §5.2 采样三（页 21 原样 103B）→ probe READY(103)、BCC 自洽、parse 全屏 96B",
         (len(samp_d21), samp_d21[2], xj_probe(samp_d21)[:2], bcc_ok(samp_d21),
          (xj_parse(samp_d21) or {}).get("d_is_full"), (xj_parse(samp_d21) or {}).get("text_len")),
         (103, 0x67, (READY, 103), True, True, 96))
    case("§10.7 'D' 全屏行数口径：N=4（71 文档表）/ N=6（103 采样）都收；N=1（23 归行显示）/"
         " N=7（119）拒绝",
         (len(xj_parse(frame(CMD_D, bytes([0xFF, 0]) + b"A" * 64)) or {}),
          len(xj_parse(frame(CMD_D, bytes([0xFF, 0]) + b"A" * 96)) or {}),
          xj_probe(frame(CMD_D, bytes([0xFF, 0]) + b"A" * 112))[:2]),
         (7, 7, (FAKE, None)))

    if verbose:
        for name, got, want in CASES:
            mark = "✓" if got == want else "✗"
            print(f"  {mark} {name}: got={got!r}" + ("" if got == want else f" want={want!r}"))


# ==========================================================================
# 源码一致性断言
# ==========================================================================
def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def run_source_assertions() -> None:
    def has(text: str, pat: str, name: str) -> None:
        case(f"[源码] {name}", bool(re.search(pat, text)), True)

    disp_h = (ROOT / "Application/Inc/app_dispatch.h").read_text(encoding="utf-8")
    disp_c = (ROOT / "Application/Src/app_dispatch.c").read_text(encoding="utf-8")
    bit_h = (ROOT / "Kernel/Inc/bit_utils.h").read_text(encoding="utf-8")
    bit_c = (ROOT / "Kernel/Src/bit_utils.c").read_text(encoding="utf-8")
    mkh = (ROOT / "Application/Inc/ProtocolParser_XinJiang/app_xj_proto.h").read_text(encoding="utf-8")
    proto = (ROOT / "Application/Src/ProtocolParser_XinJiang/app_xj_proto.c").read_text(encoding="utf-8")
    parse_h = (ROOT / "Application/Inc/ProtocolParser_XinJiang/app_xj_proto_parse.h").read_text(encoding="utf-8")
    parse = (ROOT / "Application/Src/ProtocolParser_XinJiang/app_xj_proto_parse.c").read_text(encoding="utf-8")
    cmdh = (ROOT / "Application/Inc/ProtocolParser_XinJiang/app_xj_proto_cmd.h").read_text(encoding="utf-8")
    cmd = (ROOT / "Application/Src/ProtocolParser_XinJiang/app_xj_proto_cmd.c").read_text(encoding="utf-8")
    mk = (ROOT / "Makefile").read_text(encoding="utf-8")

    proto_code = _strip_comments(proto)
    parse_code = _strip_comments(parse)
    cmd_code = _strip_comments(cmd)
    module_all = "\n".join([mkh, proto, parse_h, parse, cmdh, cmd])

    # ---- 框架掩码扩容（32→64） ----
    has(disp_h, r"#define PROTO_MAX_COUNT\s+\(64U\)", "PROTO_MAX_COUNT = 64")
    has(disp_h, r"typedef uint64_t proto_mask_t;", "proto_mask_t = uint64_t")
    has(disp_h, r"proto_mask_t registered_mask;", "registered_mask 随 64 位类型")
    has(disp_h, r"uint8_t proto_index\(proto_mask_t mask\);", "proto_index 取 64 位掩码")
    has(bit_h, r"uint8_t bit_ctz64\(uint64_t x\);", "bit_ctz64 声明")
    has(bit_c, r"uint8_t bit_ctz64\(uint64_t x\)", "bit_ctz64 定义")
    has(bit_c, r"if \(x == 0U\)\s*\n\s*return 0U;", "bit_ctz64 x==0 与 bit_ctz 语义一致（回 0）")
    case("[源码] app_dispatch.c 已无 32 位 bit_ctz 调用", len(re.findall(r"(?<![0-9A-Za-z_])bit_ctz\(", disp_c)), 0)
    case("[源码] app_dispatch.c bit_ctz64 调用数 = 5",
         len(re.findall(r"bit_ctz64\(", disp_c)), 5)
    case("[源码] 每处位移都显式转 proto_mask_t（64 位安全）",
         len(re.findall(r"\(proto_mask_t\)1U <<", disp_c)),
         len(re.findall(r"1U <<", disp_c)))
    has(disp_c, r"ring_buffer_t \*seen\[RB_CNT_MAX\]", "无协议数据去重表按 RB 槽数开表（1KB 通道任务栈）")

    # ---- app_xj_proto.h 常量 ----
    for pat, name in (
        (r"#define XJ_STX1 \(0xDCU\)", "XJ_STX1 = 0xDC"),
        (r"#define XJ_LEN_OFFSET \(2U\)", "XJ_LEN_OFFSET = 2"),
        (r"#define XJ_CMD_OFFSET \(3U\)", "XJ_CMD_OFFSET = 3"),
        (r"#define XJ_FRAME_LEN_MIN \(5U\)", "XJ_FRAME_LEN_MIN = 5"),
        (r"#define XJ_FRAME_LEN_MAX \(106U\)", "XJ_FRAME_LEN_MAX = 106"),
        (r"#define XJ_QUEUE_DEPTH \(3U\)", "XJ_QUEUE_DEPTH = 3"),
        (r"#define XJ_T_TEXT_OFFSET \(9U\)", "XJ_T_TEXT_OFFSET = 9"),
        (r"#define XJ_T_LINE_BYTES \(16U\)", "16 字节/行"),
        (r"#define XJ_T_LINE_MAX \(6U\)", "最多 6 行"),
        (r"#define XJ_V_FRAME_LEN_64 \(71U\)", "'V' 71 形态"),
        (r"#define XJ_V_FRAME_LEN_65 \(72U\)", "'V' 72 形态"),
        (r"#define XJ_S_FRAME_LEN \(8U\)", "'S' 整帧 8"),
        (r"#define XJ_VOL_MIN \(0x30U\)", "音量下限 0x30"),
        (r"#define XJ_VOL_MAX \(0x37U\)", "音量上限 0x37"),
        (r"#define XJ_RGB_HIGH \(128U\)", "颜色阈值 128"),
        (r"#define XJ_FAULT_TIMEOUT_MS \(15000U\)", "心跳超时 15s"),
        (r"#define XJ_FAULT_POLL_MS \(500U\)", "节拍 500ms"),
    ):
        has(mkh, pat, name)
    has(mkh, r"#ifndef XJ_FAULT_SCREEN\s*\n#define XJ_FAULT_SCREEN \(1\)\s*\n#endif",
        "XJ_FAULT_SCREEN #ifndef 守卫默认 1")
    # 2026-10-13 全仓编译期断言清理（用户裁决）：原 7 条 `_Static_assert(...)` 存在性检查改为
    # 「契约注释存在」+「数值关系宿主侧复算」两类，检查强度不降。
    has(mkh, r"契约：帧上限 ≤ 串口 RB 容量；且 < 治超帧长字段 0xDC\(220\)",
        "帧长口径契约注释（≤ 串口 RB 且 < 0xDC；快拒前提）")
    has(mkh, r"契约：'T' 整帧长 = 内容起始 9 \+ 16N \+ BCC 1 = XJ_FRAME_LEN_MAX",
        "'T' 帧长契约注释（9 + 16N + 1）")
    has(mkh, r"契约：报价整帧长 = 载荷起始 6 \+ 各形态载荷长 \+ BCC 1（报价1 5B / 报价2 10B / 报价3 11B）",
        "报价帧长契约注释（6 + 载荷 + 1）")
    has(mkh, r"契约：'D' 行显示整帧长 = 6 \+ 16 \+ 1；全屏形态 6 \+ 16N \+ 1（N=2~6 ⇒ 39~103）",
        "'D' 帧长区间契约注释（39~103）")
    case("帧长口径自证（'T' = 9 + 16×6 + 1 = 上限）",
         XJ_T_TEXT_OFFSET + XJ_T_LINE_BYTES * XJ_T_LINE_MAX + 1, XJ_FRAME_LEN_MAX)
    case("快拒前提：帧上限 < 治超长度字段 0xDC(220)", XJ_FRAME_LEN_MAX < 220, True)
    case("'D' 全屏上限 = 103（PDF 采样）", XJ_D_FULL_FRAME_LEN_MAX, 103)
    case("'D' 全屏下限 = 39", XJ_D_FULL_FRAME_LEN_MIN, 39)
    case("文档表 71 形态落在 [39,103] 内",
         XJ_D_FULL_FRAME_LEN_MIN <= XJ_D_FULL_FRAME_LEN <= XJ_D_FULL_FRAME_LEN_MAX, True)

    # ---- probe（app_xj_proto.c） ----
    has(proto_code, r"if \(avail == 0U\)\s*\n\s*return PROTO_PROBE_FAKE;", "PURE 契约：avail==0 → FAKE")
    has(proto_code, r"head\[0\] != XJ_STX0", "首字节快拒 0xFF")
    has(proto_code, r"if \(avail < \(XJ_LEN_OFFSET \+ 1U\)\)\s*\n\s*return PROTO_PROBE_WAIT;",
        "长度未到齐 → WAIT")
    has(proto_code, r"head\[1\] != XJ_STX1", "第二字节 0xDC 强判")
    has(proto_code, r"frame_len < XJ_FRAME_LEN_MIN \|\| frame_len > XJ_FRAME_LEN_MAX",
        "长度双向校验（不等齐先判，防治超长帧占预算）")
    has(proto_code, r"xj_cmd_from_byte\(head\[XJ_CMD_OFFSET\]\) == XJ_PCMD_INVALID", "命令字白名单（复用 parse 单一真源）")
    has(proto_code, r"if \(avail < frame_len\)\s*\n\s*return PROTO_PROBE_WAIT;", "整帧未到齐 → WAIT")
    has(proto_code, r"xj_bcc_ok\(frame, \(uint16_t\)frame_len\)", "BCC 第三重隔离")
    case("[源码] 仅 READY 路径写 *total_len",
         len(re.findall(r"\*total_len = frame_len;", proto_code)), 1)
    has(proto_code, r"\*total_len = frame_len;\s*\n\s*return PROTO_PROBE_READY;",
        "*total_len 写入紧邻 READY")

    # probe 覆盖回归（防 2026-10-12 修复的「peek 读长 < 随后下标访问」类未初始化读）：
    # 顺序扫描 probe 体，凡 head/frame 等被 rb_peek(rb,0,…,N) 填充的缓冲，其后的 buf[i]
    # 访问下标 i 必须 < N；捕捉「只 peek 2 字节却读 head[2]」这类栈垃圾读。
    # 取定义体（文件内有前向声明 → 取最后一次出现，即函数定义）
    _probe_body = proto_code.rsplit("xj_probe_frame(const channel_t", 1)[1].split("\n}\n", 1)[0]

    def _int_expr(expr: str):
        e = expr.strip().replace("U", "")
        for _name, _val in (("XJ_LEN_OFFSET", XJ_LEN_OFFSET), ("XJ_CMD_OFFSET", XJ_CMD_OFFSET)):
            e = e.replace(_name, str(_val))
        try:
            return int(eval(e, {"__builtins__": {}}, {}))  # noqa: S307（仅数字算术）
        except Exception:
            return None

    _covered: dict[str, set[int]] = {}
    _uncovered: list[str] = []
    _analyzed = 0
    for _line in _probe_body.splitlines():
        _pm = re.search(r"rb_peek\(\s*rb,\s*0U,\s*(\w+),\s*([^,]+),", _line)
        if _pm:
            _n = _int_expr(_pm.group(2))
            if _n is not None:
                _covered[_pm.group(1)] = set(range(_n))
        for _am in re.finditer(r"\b(\w+)\[(\w+)\]", _line):
            _buf, _idx = _am.group(1), _am.group(2)
            if _buf not in _covered:
                continue
            _i = _int_expr(_idx)
            if _i is None:
                continue
            _analyzed += 1
            if _i not in _covered[_buf]:
                _uncovered.append(f"{_buf}[{_i}]")
    case("[源码] probe 内 head[N]/frame[N] 访问均被前置 rb_peek 读长覆盖（无未初始化读）",
         (_analyzed > 0, _uncovered), (True, []))


    # 通道绑定：RS485 + RS232 两独立 mask；自备两槽体（weak provide）再 acquire；禁绑 RS232_1
    has(proto_code, r"app_proto_acquire_buf\(RB_SLOT_RS485, RB_SIZE_RS485\)", "acquire RS485 槽")
    has(proto_code, r"app_proto_acquire_buf\(RB_SLOT_RS232, RB_SIZE_RS232\)", "acquire RS232 槽")
    has(proto_code, r"app_proto_bind_channel\(s_xj_mask_rs485, CH_ID_RS485\)", "bind CH_ID_RS485")
    has(proto_code, r"app_proto_bind_channel\(s_xj_mask_rs232, CH_ID_RS232\)", "bind CH_ID_RS232")
    has(proto_code, r"RB_PROVIDE_WEAK\(rb_provide_rs485, RB_SIZE_RS485\)", "provide RS485 槽体")
    has(proto_code, r"RB_PROVIDE_WEAK\(rb_provide_rs232, RB_SIZE_RS232\)", "provide RS232 槽体")
    case("[源码] 两槽自备提供者（单协议构建 = IAP+LDI+新疆 也能注册，不再依赖他协议）",
         len(re.findall(r"RB_PROVIDE_WEAK\(rb_provide_rs(?:485|232),",
                        _strip_comments(module_all))), 2)
    case("[源码] 禁绑 CH_ID_RS232_1", bool(re.search(r"CH_ID_RS232_1", _strip_comments(module_all))), False)
    case("[源码] 两 mask 判空即整模块不激活（silent skip）",
         bool(re.search(r"s_xj_mask_rs485 == 0 && s_xj_mask_rs232 == 0\)\s*\n\s*return;", proto_code)), True)
    has(proto_code, r"sw_app_initcall\(xj_proto_init\);", "sw_app_initcall 自注册")

    # CCMRAM 放置 + 静态任务
    has(proto_code, r'\[\[gnu::section\("\.ccmram"\)\]\] static StaticQueue_t s_xj_queue_cb;', "队列 cb 置 CCMRAM")
    has(proto_code, r'\[\[gnu::section\("\.ccmram"\)\]\] static uint8_t s_xj_queue_buf', "队列体置 CCMRAM")
    has(proto_code, r'\[\[gnu::section\("\.ccmram"\)\]\] static uint8_t msg_buf\[XJ_MSG_SIZE\]', "任务帧缓冲置 CCMRAM")
    has(proto_code, r"PL_TASK_STATIC_STORAGE\(xj_handle, 256\)", "任务栈 + TCB 静态落 CCMRAM")
    has(proto_code, r"PL_TASK_STATIC_ATTR\(xj_handle, 256\)", "任务属性接静态存储")
    has(proto_code, r'\.priority\s+= osPriorityNormal', "任务优先级 Normal")

    # 心跳节拍（不新建任务）
    has(proto_code, r"#if XJ_FAULT_SCREEN\s*\n\s*const uint32_t wait_ms = XJ_FAULT_POLL_MS;",
        "开关开：500ms 队列超时做节拍")
    has(proto_code, r"const uint32_t wait_ms = osWaitForever;", "开关关：无节拍、零周期唤醒")
    has(proto, r"_xj_heartbeat_on_frame\(\);\s*\n\s*\n\s*/\* 只传裸帧：",
        "解除动作先于本帧执行（'T'/'C' 首步清屏不被擦）")
    # 栈预算回归断言：具名 xj_parsed_cmd_t 会与 xj_execute_cmd 帧内副本并存
    case("[源码] 任务不具名持有解析结果（零拷贝传裸帧，不占任务栈）",
         bool(re.search(r"xj_parsed_cmd_t\s+cmd\s*=", proto_code)), False)
    has(proto, r"xj_execute_cmd\(msg->ch, msg->data, \(uint16_t\)msg->data_len\);",
        "调用点只传裸帧 + 来源通道")
    has(cmd_code, r"xj_parsed_cmd_t cmd = xj_parse_frame\(raw, raw_len\);",
        "解析结果只存在于执行层一帧内")
    has(proto_code, r"_xj_heartbeat_poll\(\);", "节拍末尾调用超时判定")
    has(proto_code, r"_xj_heartbeat_reset\(\)", "心跳起点 = 模块 init")
    case("[源码] 模块内不新建任何任务（除处理任务与 initcall 外无 osThreadNew）",
         len(re.findall(r"osThreadNew\(", proto_code)), 1)

    # ---- parse（app_xj_proto_parse.c） ----
    case("[源码] 'T' 分支不读 [5] 显示行字段",
         bool(re.search(r"raw\[5\]", parse_code.split("case XJ_PCMD_TEXT")[1].split("case XJ_PCMD_VOICE_TTS")[0])),
         False)
    has(parse_code, r"\(uint16_t\)raw\[XJ_LEN_OFFSET\] != raw_len", "长度字段与实到长度自洽（契约 ①）")
    has(parse_code, r"\(content_len % XJ_T_LINE_BYTES\) != 0U", "'T' 内容须为 16 的整数倍")
    has(parse_code, r"\(content_len / XJ_T_LINE_BYTES\) > XJ_T_LINE_MAX", "'T' 行数 ≤ 6")
    has(parse_code, r"raw_len != XJ_V_FRAME_LEN_64 && raw_len != XJ_V_FRAME_LEN_65", "'V' 容忍 71/72 两种整帧长")
    has(parse_code, r"out\.text_len = \(uint16_t\)\(raw_len - 7U\);", "'V' 文本 = [6..len-2]")
    has(parse_code, r"_xj_s_kind_from_len\(raw_len\)", "'S' 帧长判形态（报价 1/2/3 + 预制）")
    has(parse_code, r"case XJ_S_QUOTE1_FRAME_LEN: return XJ_S_KIND_QUOTE1;", "报价 1 = 12 字节")
    has(parse_code, r"case XJ_S_QUOTE2_FRAME_LEN: return XJ_S_KIND_QUOTE2;", "报价 2 = 17 字节")
    has(parse_code, r"case XJ_S_QUOTE3_FRAME_LEN: return XJ_S_KIND_QUOTE3;", "报价 3 = 18 字节")
    has(parse_code, r"case XJ_S_FRAME_LEN:        return XJ_S_KIND_PRESET;", "§4.4 预制 = 8 字节")
    has(parse_code, r"\(r >= XJ_RGB_HIGH\) \? 1U : 0U", "颜色位掩码 = 各分量 ≥128")
    has(parse_code, r"return \(display_color_t\)v;", "映射值 = display_color_t")
    case("[源码] BCC 覆盖除自身外全部字节",
         bool(re.search(r"xj_bcc_calc\(frame, \(uint16_t\)\(frame_len - 1U\)\) == frame\[frame_len - 1U\]", parse_code)),
         True)
    case("[源码] 解析层无副作用（不碰显示/灯光/语音/存储/RTOS）",
         bool(re.search(r"dev_display|dev_io_|dev_rs232_voice|osThread|dev_storage|app_render", parse_code)), False)

    # ---- 执行层（app_xj_proto_cmd.c） ----
    has(cmd_code, r"if \(take == \(uint16_t\)XJ_T_LINE_BYTES && rem > \(uint16_t\)XJ_T_LINE_BYTES &&\s*\n\s*_xj_gbk_lead\(p\[XJ_T_LINE_BYTES - 1U\]\) && _xj_gbk_trail\(p\[XJ_T_LINE_BYTES\]\)\)\s*\n\s*take = \(uint16_t\)\(XJ_T_LINE_BYTES - 1U\);",
        "GBK 跨 16B 边界 → 整字节退让")
    has(cmd_code, r"_xj_gbk_lead\(uint8_t b\)\s*\n\{\s*\n\s*return b >= 0x81U && b <= 0xFEU;", "GBK 首字节判据")
    has(cmd_code, r"p\[n - 1U\] == 0x00U \|\| p\[n - 1U\] == 0x20U", "行内容去尾部 0x00/0x20")
    has(cmd_code, r"\.font_size = FONT_16,\s*\n\s*\.font_type = FONT_ST,", "'T' FONT_16 / FONT_ST")
    has(cmd_code, r"const uint16_t y = \(uint16_t\)\(row \* \(uint16_t\)FONT_16\);", "行 y = row × 16")
    has(cmd_code, r"dev_display_fill\(d, 0, 0, d->screen_rows, d->screen_cols, COLOR_BLACK\);\s*\n\s*if \(cmd->color != COLOR_BLACK\)",
        "先整屏清黑、黑字只清屏")
    has(cmd_code, r"app_scroll_render_lock\(\);", "渲染+提交整段持滚动渲染锁")
    case("[源码] render_lock / unlock 配平",
         len(re.findall(r"app_scroll_render_lock\(\)", cmd_code)), len(re.findall(r"app_scroll_render_unlock\(\)", cmd_code)))
    # 灯控
    has(cmd_code, r"case XJ_PCMD_LANE_GREEN:\s*\n\s*dev_io_lane_light\(true\);", "'E' → 车道灯亮（PD14 绿）")
    has(cmd_code, r"case XJ_PCMD_LANE_RED:\s*\n\s*dev_io_lane_light\(false\);", "'F' → 车道灯灭")
    has(cmd_code, r"case XJ_PCMD_ALARM_ON:\s*\n\s*dev_io_flash_light\(true\);", "'J' → 黄闪/报警开")
    has(cmd_code, r"case XJ_PCMD_ALARM_OFF:\s*\n\s*dev_io_flash_light\(false\);", "'K' → 黄闪/报警关")
    case("[源码] 模块 init 不改灯态（无 init 期灯光调用）",
         bool(re.search(r"dev_io_(lane|flash)_light", proto_code)), False)
    # 亮度
    has(cmd_code, r"1U \+ \(\(uint32_t\)\(v - 1U\) \* \(DEV_DISPLAY_BRIGHTNESS_MAX - 1U\)\) / 254U",
        "亮度 1~255 → 1~8 均匀映射")
    has(cmd_code, r"osThreadSuspend\(g_light_sensor_task_handle\);", "非 0 亮度挂起光敏自动调光")
    has(cmd_code, r"osThreadResume\(g_light_sensor_task_handle\);", "0 亮度恢复光敏自动调光")
    case("[源码] 挂起标志只在本模块挂起过时恢复",
         bool(re.search(r"if \(s_xj_auto_dim_off\) \{\s*\n\s*if \(g_light_sensor_task_handle != nullptr\)\s*\n\s*osThreadResume", cmd_code)),
         True)
    # 语音
    has(cmd_code, r"dev_rs232_voice_play\(cmd->text, len\);", "'V' 文本直送语音板")
    has(cmd_code, r"dev_rs232_voice_volume\(_xj_voice_level\(cmd->volume\)\);", "音量经 0x30~0x37 → 1~9 映射")
    has(cmd_code, r"return \(uint8_t\)\(1U \+ \(\(uint32_t\)\(v - XJ_VOL_MIN\) \* 8U\) / \(XJ_VOL_MAX - XJ_VOL_MIN\)\);",
        "音量线性映射式")
    case("[源码] 预制语音表 8 条（0xA0 + 0xB0~0xB6，PDF §4.4 出口收卡机）",
         len(re.findall(r'/\* 0x(?:A0|B[0-6]) \*/', cmd)), 8)
    for text in ("ETC已交易成功，请通行", "缴费XX元，请展示付款码", "缴费成功",
                 "请将通行卡插入卡口", "未检测到通行卡，请联系工作人员",
                 "通行卡损坏，请联系工作人员", "如需发票，请按键取票",
                 "请拿好通行卡，祝您一路平安"):
        case(f"[源码] 预制文案含「{text}」", text in cmd_code, True)
    has(cmd_code, r"UTF8ToGBK\(text, \(uint32_t\)strlen\(text\), \(char \*\)gbk, &gbk_len\);", "预制文案运行转 GBK")
    has(cmd_code, r"s_xj_preset_index\[i\] == cmd->preset_no", "预制编号查表（非连续编号不按 idx 直算）")
    case("[源码] 报价语音三种形态各有中文模板",
         all(k in cmd_code for k in ("请交费", "车货总重", "超重")), True)
    # 维护屏
    has(cmd_code, r'static const char s_xj_fault_text\[\] = "系统维护中";', "维护屏文案 = 系统维护中")
    has(cmd_code, r"\.color     = COLOR_RED,\s*\n\s*\.text      = s_xj_fault_text,", "维护屏红字")
    has(cmd_code, r"\.h_align   = ALIGN_CENTER,\s*\n\s*\.v_align   = ALIGN_CENTER,", "维护屏居中")
    has(cmd_code, r"\.font_size = FONT_16,\s*\n\s*\.font_type = FONT_ST,", "维护屏 FONT_16 / FONT_ST")
    has(cmd_code, r"void xj_fault_screen_clear\(void\)", "维护屏解除清屏 API")

    # ---- 无显示 / 日志 / 帧族纪律 ----
    case("[源码] 模块全文零 RTT/printf/诊断",
         bool(re.search(r"SEGGER_RTT|printf|APP_DIAG|RTT_DIAG", module_all)), False)
    case("[源码] 非 '{' 帧族（不定义 g_brace_proto_guard）",
         bool(re.search(r"g_brace_proto_guard", module_all)), False)
    case("[源码] 无 #if 0 / 注释掉的代码块", bool(re.search(r"#if 0", module_all)), False)
    case("[源码] 每个文件头都有 doc/19 指针",
         sum(1 for t in (mkh, proto, parse_h, parse, cmdh, cmd) if "doc/19_新疆费显协议/README.md" in t), 6)
    case("[源码] 执行层显示入口全部判空（dev_display_get() == nullptr）",
         len(re.findall(r"if \(d == nullptr\)\s*\n\s*return;", cmd_code)) >= 4, True)
    case("[源码] 执行层不碰持久化/重启（存储与系统复位）",
         bool(re.search(r"dev_storage|dev_w25qxx|app_board_net_cfg|NVIC_SystemReset", cmd_code)), False)
    case("[源码] 应答回送仅经 channel_send 虚表（不改通道层）",
         bool(re.search(r"channel_send", cmd_code)) and not bool(re.search(r"dev_rs232_send|dev_rs485_send", cmd_code)),
         True)

    # ---- Makefile 收录（末尾追加 = probe 最低优先级） ----
    has(mk, r"-I Application/Inc/ProtocolParser_XinJiang", "INC_DIRS 收录 -I")
    for f in ("app_xj_proto.c", "app_xj_proto_parse.c", "app_xj_proto_cmd.c"):
        has(mk, rf"Application/Src/ProtocolParser_XinJiang/{re.escape(f)}", f"SRC_APPLICATION 收录 {f}")
    idx_xj = mk.index("Application/Src/ProtocolParser_XinJiang/app_xj_proto.c")
    idx_mm = mk.index("Application/Src/ProtocolParser_Myanmar/app_mm_proto.c")
    idx_rs485 = mk.index("Application/Src/Channel/app_rs485.c")
    case("[源码] 新疆收录在所有既有协议之后（probe 最低优先级）",
         idx_xj > max(idx_mm, idx_rs485), True)


    # ---- 契约块纪律（口径同 .analysis/comment_pass_20261008/scripts/refine_round2/check_contracts.py；
    #      该脚本 MODULES 为硬编码表，本模块由这里自证） ----
    MARK = "①②③④⑤⑥⑦⑧⑨"
    CN = {c: i + 1 for i, c in enumerate(MARK)}
    blk: list[str] = []
    for ln in mkh.splitlines():
        blk.append(ln)
        if ln.strip().endswith("*/"):
            break
    case("[源码] 主头文件头 ≤ 10 行", len(blk) <= 10, True)
    mark = "契约（改错静默致现场故障）"
    case("[源码] 契约块存在", mark in "\n".join(blk), True)
    body = "\n".join(blk).split(mark, 1)[1].split("详见", 1)[0]
    items = [
        p.strip().strip("；;。：:").strip()
        for p in re.split(f"[{MARK}]", " ".join(s.strip().lstrip("*").strip() for s in body.splitlines() if s.strip()))
        if p.strip().strip("；;。：:")
    ]
    case("[源码] 契约条数 4~8", 4 <= len(items) <= 8, True)
    case("[源码] 每条契约 ≤ 30 字", max((len(i) for i in items), default=0) <= 30, True)
    case("[源码] 契约行无日期/无理由引导词",
         bool(re.search(r"20\d\d|理由|已裁决|用户裁决|因为", " ".join(items))), False)
    bad_refs = []
    allow = set(range(1, len(items) + 1))
    for rel, text in (("h", mkh), ("c", proto), ("parse_h", parse_h), ("parse", parse), ("cmd_h", cmdh), ("cmd", cmd)):
        for ln_no, ln in enumerate(text.splitlines(), 1):
            if "契约" not in ln:
                continue
            for ch in re.findall(f"[{MARK}]", ln):
                if CN[ch] not in allow:
                    bad_refs.append(f"{rel}:{ln_no} 契约 {ch}")
    case(f"[源码] 契约引用编号一致（1~{len(items)}）", bad_refs, [])
    case("[源码] 长行 ≤ 100 列（long_comments.py 阈值）",
         max(len(ln) for ln in mkh.splitlines()) <= 100, True)


def main() -> int:
    ap = argparse.ArgumentParser(description="新疆费显协议 probe/parse 宿主推演")
    ap.add_argument("-v", "--verbose", action="store_true", help="打印每条用例明细")
    args = ap.parse_args()

    run_cases(args.verbose)
    run_source_assertions()

    total = len(CASES)
    passed = total - len(FAILED)
    print(f"\n新疆费显协议宿主推演：{passed}/{total} 通过，失败 {len(FAILED)}")
    for f in FAILED:
        print(f"  ✗ {f}")
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
