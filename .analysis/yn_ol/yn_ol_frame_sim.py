#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
云南治超屏协议（YN_1.3.0，`{` 帧族）帧推演自检 —— 纯 Python 镜像 C 实现的 probe/parse。

用途（与 ~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/gz_ol_frame_sim.py 同风格）：
  在没有硬件的情况下，用边界用例验证固件 probe 状态机与 parse 分支的行为——
  半帧/粘包/伪头/超长/非法参数/命令字快拒/互斥判定/字节序，全部离线可复现。

覆盖章节（2026-09-17 裁决轮扩展）：
  §1~§7  probe 状态机 / 粘包重同步 / parse 分支 / 0x47 字节序 / 0x49 参数 / 帧结构边界 / 帧族互斥
  §8     联调帧样例
  §9     **TCP 双通道绑定与 RJ45 槽 probe 竞争**（通道绑定表由 Application/Src 静态扫描得出；
         双向快拒、与 CQ JSON 互不误认、半帧/粘包/分片；反事实：绑 UDP 会被 CQ 抢先认领）
  §10    **0x49 持久化契约**（W25Qxx 12B 记录 写→读→装载一致、CRC 坏/魔数错/版本错/字段越界
         回退默认；扇区选址证明：cap-12288 独立且与字库/LDI/app_render 无重叠）
  §11    **四通道应答回源**（'1' / 0x48 / 0x47 在四个通道上均单播回同一通道、共享同一队列）

被测源码（单一真源对照）：
  Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c   （probe / 注册绑定 / 任务）
  Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_parse.c（parse）
  Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c  （执行 + 0x49 持久化）
  Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h   （常量 / 记录布局）
  Application/Src/ProtocolParser_ChongQing/app_cq_proto.c            （RJ45 槽竞争者，只读扫描）
  Application/Src/app_render.c                                       （字库区块表 / persist 扇区）

运行：python3 .analysis/yn_ol/yn_ol_frame_sim.py
"""

import re
import sys
from pathlib import Path

# ---- 帧常量（镜像 app_yn_ol_proto.h）----
STX = 0x7B  # '{'
ETX = 0x7D  # '}'
HEAD_LEN = 3
FRAME_OVERHEAD = 4
FRAME_LEN_MIN = 4
FRAME_LEN_MAX = 259
NET_PAYLOAD_LEN = 14

# ---- 命令字白名单（镜像 yn_ol_cmd_from_byte：仅本模块实现的命令）----
SUPPORTED_CMDS = {
    0x31,  # '1' 主机查询
    0x32,  # '2' 自检
    0x33,  # '3' 单行
    0x34,  # '4' 全屏
    0x35,  # '5' 清屏
    0x38,  # '8' 亮度
    0x41,  # 'A' 外设
    0x42, 0x43, 0x44, 0x45, 0x46,  # 行 1~5 清除
    0x47,  # 修改 IP
    0x48,  # 查询 IP
    0x49,  # 屏体参数
    0x50,  # 行 6 清除
    0x51,  # 查询 IP 应答（仅出站）
    0x01,  # 全屏点亮
    0x02,  # 版本号
}
# 治超屏文档明示不开发 / 其它 `{` 帧族专有 → 必须快拒
REJECTED_CMDS = {
    0x36,  # '6' 固定格式（不开发）
    0x37,  # '7' 礼貌用语语音（不开发）
    0x39,  # '9' 音量（不开发）
    0x42 + 0x20,  # 0x62 'b' 未知
    0x42,  # 占位（下方用显式集合覆盖）
}
REJECTED_CMDS = {0x36, 0x37, 0x39, 0x42 + 0x20, 0x00, 0x03, 0x19}


def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    """按 `{` + cmd + len + payload + `}` 组帧（len = 参数区字节数，二进制）。"""
    assert len(payload) <= 255
    return bytes([STX, cmd, len(payload)]) + payload + bytes([ETX])


def parse_color(b: int):
    """镜像 C 侧 `_yn_ol_parse_color`（2026-09-17 容错轮）：
    ASCII '0'~'2'（0x30~0x32）或二进制 0x00~0x02；越界 None。"""
    if 0x30 <= b <= 0x32:
        return b - 0x30
    if b <= 0x02:
        return b
    return None


def parse_row(b: int):
    """镜像 C 侧 `_yn_ol_parse_row`：ASCII '1'~'5'（0x31~0x35）或二进制 0x01~0x05；越界 None。"""
    if 0x31 <= b <= 0x35:
        return b - 0x31
    if 0x01 <= b <= 0x05:
        return b - 0x01
    return None


def probe(buf: bytes):
    """镜像 yn_ol_probe_frame：返回 (状态, frame_len)；状态 ∈ {FAKE, WAIT, READY}。"""
    if len(buf) == 0:
        return ("FAKE", None)  # ① 无数据不得盲 WAIT
    if buf[0] != STX:
        return ("FAKE", None)  # ② 首字节快拒
    if len(buf) < HEAD_LEN:
        return ("WAIT", None)  # ③ 头未到齐
    if buf[1] not in SUPPORTED_CMDS:
        return ("FAKE", None)  # ④ 命令字白名单（'6'/'7'/'9'/'B' 及未知 → FAKE）
    frame_len = buf[2] + FRAME_OVERHEAD
    if len(buf) < frame_len:
        return ("WAIT", None)  # ⑤ 整帧未到齐
    if buf[frame_len - 1] != ETX:
        return ("FAKE", None)  # ⑥ 尾字节定界
    return ("READY", frame_len)  # ⑦


def parse(frame: bytes):
    """镜像 yn_ol_parse_frame：返回 (cmd, sta, fields)。sta ∈ OK/ERR_FRAME/ERR_CMD/ERR_PARAM。"""
    if len(frame) < FRAME_LEN_MIN or len(frame) > FRAME_LEN_MAX:
        return (None, "ERR_FRAME", {})
    if frame[0] != STX or frame[-1] != ETX:
        return (None, "ERR_FRAME", {})
    declared = frame[2]
    if len(frame) - FRAME_OVERHEAD != declared:
        return (None, "ERR_FRAME", {})
    cmd = frame[1]
    data = frame[3:-1]
    if cmd not in SUPPORTED_CMDS:
        return (cmd, "ERR_CMD", {})
    f = {}

    if cmd in (0x31, 0x32, 0x48):  # '1' / '2' / 0x48：无参
        return (cmd, "OK" if declared == 0 else "ERR_PARAM", f)
    if cmd == 0x35:  # '5' 清屏：无参
        return (cmd, "OK" if declared == 0 else "ERR_PARAM", f)

    if cmd == 0x33:  # '3' 单行：颜色 + 行号 + 文本（≥3）
        if declared < 3:
            return (cmd, "ERR_PARAM", f)
        color, row = parse_color(data[0]), parse_row(data[1])
        if color is None or row is None:
            return (cmd, "ERR_PARAM", f)
        f = {"color": color, "row": row, "text": data[2:]}
        return (cmd, "OK", f)

    if cmd == 0x34:  # '4' 全屏：颜色 + X + Y + 文本（≥4）
        if declared < 4:
            return (cmd, "ERR_PARAM", f)
        color = parse_color(data[0])
        if color is None:
            return (cmd, "ERR_PARAM", f)
        f = {"color": color, "x": data[1], "y": data[2], "text": data[3:]}
        return (cmd, "OK", f)

    if cmd == 0x38:  # '8' 亮度：长度 1；0x00/'0' 自动；'1'~'8' 手动
        if declared != 1:
            return (cmd, "ERR_PARAM", f)
        if data[0] == 0x00 or data[0] == 0x30:
            return (cmd, "OK", {"brightness": 0})
        if 0x31 <= data[0] <= 0x38:
            return (cmd, "OK", {"brightness": data[0] - 0x30})
        return (cmd, "ERR_PARAM", f)

    if cmd == 0x41:  # 'A' 外设：长度 1
        if declared != 1:
            return (cmd, "ERR_PARAM", f)
        return (cmd, "OK", {"peripheral": data[0]})

    if cmd in (0x42, 0x43, 0x44, 0x45, 0x46, 0x50):  # 行清除：无参
        if declared != 0:
            return (cmd, "ERR_PARAM", f)
        row = 5 if cmd == 0x50 else cmd - 0x42
        return (cmd, "OK", {"clear_row": row})

    if cmd == 0x47:  # 修改 IP：≥14（ip4 mask4 gw4 port2 **BE16 已裁决**）
        if declared < NET_PAYLOAD_LEN:
            return (cmd, "ERR_PARAM", f)
        port = (data[12] << 8) | data[13]  # 高字节在前
        f = {"ip": data[0:4], "mask": data[4:8], "gw": data[8:12], "port": port}
        return (cmd, "OK", f)

    if cmd == 0x49:  # 屏体参数：长度 2；字体 ≤3、字宽 ≤2
        if declared != 2:
            return (cmd, "ERR_PARAM", f)
        if data[0] > 3 or data[1] > 2:
            return (cmd, "ERR_PARAM", f)
        return (cmd, "OK", {"font": data[0], "width": data[1]})

    if cmd == 0x51:  # 仅出站应答：入站丢弃
        return (cmd, "ERR_CMD", f)

    if cmd == 0x01:  # 全屏点亮：长度 1，值 1~7
        if declared != 1 or not (1 <= data[0] <= 7):
            return (cmd, "ERR_PARAM", f)
        return (cmd, "OK", {"fill_color": data[0]})

    if cmd == 0x02:  # 版本号：长度 1
        return (cmd, "OK" if declared == 1 else "ERR_PARAM", f)

    return (cmd, "ERR_CMD", f)


# ============================== 用例 ==============================

PASS = 0
FAIL = 0


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
    else:
        FAIL += 1
        print(f"  [FAIL] {name} {detail}")


def eq(name, got, want):
    check(name, got == want, f"got={got!r} want={want!r}")


def sec(title):
    print(f"\n== {title}")


# ---- §1 probe 状态机 ----
sec("§1 probe 状态机（FAKE/WAIT/READY）")
eq("1.1 空缓冲 → FAKE", probe(b"")[0], "FAKE")
eq("1.2 首字节错（0x7A）→ FAKE", probe(bytes([0x7A, 0x31, 0x00, 0x7D]))[0], "FAKE")
eq("1.3 首字节对但仅 1 字节 → WAIT", probe(b"{")[0], "WAIT")
eq("1.4 头（3B）齐、帧未到 → WAIT", probe(build_frame(0x31)[:3])[0], "WAIT")
eq("1.5 完整帧 → READY", probe(build_frame(0x31))[0], "READY")
eq("1.6 READY 帧长正确（0x31 空参）", probe(build_frame(0x31))[1], 4)
eq("1.7 READY 帧长正确（'3' 9 字节）", probe(build_frame(0x33, b"01A"))[1], 7)
eq("1.8 尾字节错（'}' 改 '|'）→ FAKE", probe(build_frame(0x31)[:-1] + b"|")[0], "FAKE")
eq("1.9 命令字 '6'（不开发）→ FAKE", probe(build_frame(0x36, b"0"))[0], "FAKE")
eq("1.10 命令字 '7'（语音，不开发）→ FAKE", probe(build_frame(0x37, b"0"))[0], "FAKE")
eq("1.11 命令字 '9'（音量，不开发）→ FAKE", probe(build_frame(0x39, b"1"))[0], "FAKE")
eq("1.12 0x42 第一行清除（**与云南常规 'B' 同字节**）→ READY", probe(build_frame(0x42))[0], "READY")
eq("1.13 未知命令字 0x00 → FAKE", probe(build_frame(0x00))[0], "FAKE")
eq("1.14 未知命令字 0x19 → FAKE", probe(build_frame(0x19))[0], "FAKE")
check("1.15 WAIT 路径不写 total_len（回归口径：frame_len 未定）", probe(build_frame(0x31)[:3])[1] is None)

# ---- §2 粘包 / 重同步 ----
sec("§2 粘包与链式重同步")
two = build_frame(0x31) + build_frame(0x35)
eq("2.1 粘包第一帧 READY 且长度 4", probe(two), ("READY", 4))
eq("2.2 粘包第二帧 READY 且长度 4", probe(two[4:]), ("READY", 4))
# 前导垃圾字节：非 '{' 首字节 → FAKE（框架 1 字节重同步）
eq("2.3 垃圾前导 → FAKE（交框架逐字节重同步）", probe(b"\xaa\xbb" + build_frame(0x31))[0], "FAKE")
# 伪头：'{' + 合法命令但尾字节不对（长度足够）→ FAKE
eq("2.4 伪头（'}' 位置是 'X'）→ FAKE", probe(b"{1\x05abcdeX")[0], "FAKE")
eq("2.4b 伪头但数据不足 → WAIT（待更多字节再判）", probe(b"{1\x05abcXX")[0], "WAIT")
# 半帧 + 补齐
half = build_frame(0x34, bytes([0x30, 0x00, 0x2C]) + "ETC车道已关闭".encode("gbk"))
eq("2.5 半帧 → WAIT", probe(half[:2])[0], "WAIT")
eq("2.6 半帧（缺尾字节）→ WAIT", probe(half[:-1])[0], "WAIT")
eq("2.7 补齐 → READY", probe(half)[0], "READY")
eq("2.8 READY 长度 = len+4", probe(half)[1], len(half))

# ---- §3 parse 分支 ----
sec("§3 parse 分支（合法/非法参数）")
c, s, f = parse(build_frame(0x31))
eq("3.1 '1' 入站查询帧（7B 31 00 7D）→ OK", (c, s), (0x31, "OK"))
eq("3.2 '1' 带参数（如应答帧形态 01 00）→ ERR_PARAM", parse(build_frame(0x31, b"\x00"))[1], "ERR_PARAM")
eq("3.3 '1' 无参数（7B 31 00 7D）→ OK", parse(build_frame(0x31))[1], "OK")
eq("3.4 '2' 无参 → OK", parse(build_frame(0x32))[1], "OK")
eq("3.5 '3' 颜色/行号/文本解析", parse(build_frame(0x33, b"0" + b"1" + "A".encode()))[2],
   {"color": 0, "row": 0, "text": b"A"})
eq("3.6 '3' 行号 '5' → row=4 合法", parse(build_frame(0x33, b"01A"))[2]["row"], 0)
eq("3.7 '3' 行号 '6' → ERR_PARAM", parse(build_frame(0x33, b"01A".replace(b"1", b"6")))[1], "ERR_PARAM")
eq("3.8 '3' 颜色 '3' → ERR_PARAM", parse(build_frame(0x33, b"31A"))[1], "ERR_PARAM")
eq("3.9 '3' 参数仅 2B（无文本）→ ERR_PARAM", parse(build_frame(0x33, b"01"))[1], "ERR_PARAM")
eq("3.10 '4' 全屏解析（X/Y 直用像素）", parse(build_frame(0x34, b"0\x00\x2c" + "ETC".encode("gbk")))[2]["y"], 0x2C)
eq("3.11 '4' 参数 3B（无文本）→ ERR_PARAM", parse(build_frame(0x34, b"0\x00\x00"))[1], "ERR_PARAM")
eq("3.12 '5' 无参 → OK", parse(build_frame(0x35))[1], "OK")
eq("3.13 '5' 带参 → ERR_PARAM", parse(build_frame(0x35, b"\x00"))[1], "ERR_PARAM")
eq("3.14 '8' NUL 自动亮度", parse(build_frame(0x38, b"\x00"))[2]["brightness"], 0)
eq("3.15 '8' ASCII '0' 自动亮度", parse(build_frame(0x38, b"0"))[2]["brightness"], 0)
eq("3.16 '8' '5' → 档 5（文档口径）", parse(build_frame(0x38, b"5"))[2]["brightness"], 5)
eq("3.17 '8' '8' → 档 8（超集口径）", parse(build_frame(0x38, b"8"))[2]["brightness"], 8)
eq("3.18 '8' '9' → ERR_PARAM", parse(build_frame(0x38, b"9"))[1], "ERR_PARAM")
eq("3.19 'A' 外设 0x06（红+黄闪）", parse(build_frame(0x41, b"\x06"))[2]["peripheral"], 0x06)
eq("3.20 0x42 行清除 → row=0", parse(build_frame(0x42))[2]["clear_row"], 0)
eq("3.21 0x46 行清除 → row=4", parse(build_frame(0x46))[2]["clear_row"], 4)
eq("3.22 0x50 行清除 → row=5（第六行）", parse(build_frame(0x50))[2]["clear_row"], 5)
eq("3.23 行清除带参 → ERR_PARAM", parse(build_frame(0x42, b"\x00"))[1], "ERR_PARAM")
# 3.24~3.29：颜色/行号**编码容错**（2026-09-17 联调轮，只放宽不放宽语义）
eq("3.24 '3' 二进制颜色 0x00/行号 0x01 → OK（bin/bin）",
   parse(build_frame(0x33, bytes([0x00, 0x01]) + b"A"))[2], {"color": 0, "row": 0, "text": b"A"})
eq("3.25 '3' 二进制颜色 0x02/行号 0x05 → OK（色 2 行 4）",
   parse(build_frame(0x33, bytes([0x02, 0x05]) + b"A"))[2], {"color": 2, "row": 4, "text": b"A"})
eq("3.26 '3' ASCII 颜色 + 二进制行号（混用）→ OK",
   parse(build_frame(0x33, bytes([0x31, 0x03]) + b"A"))[2], {"color": 1, "row": 2, "text": b"A"})
eq("3.27 '3' 二进制行号 0x06 → ERR_PARAM（不放宽到越界）",
   parse(build_frame(0x33, bytes([0x30, 0x06]) + b"A"))[1], "ERR_PARAM")
eq("3.28 '3' 二进制颜色 0x03 → ERR_PARAM",
   parse(build_frame(0x33, bytes([0x03, 0x01]) + b"A"))[1], "ERR_PARAM")
eq("3.29 '4' 二进制颜色 0x01 → OK / 颜色 0x03 → ERR_PARAM",
   (parse(build_frame(0x34, bytes([0x01, 0x00, 0x00]) + b"A"))[1],
    parse(build_frame(0x34, bytes([0x03, 0x00, 0x00]) + b"A"))[1]), ("OK", "ERR_PARAM"))

# ---- §4 网络命令与字节序 ----
sec("§4 0x47/0x48/0x51 与端口字节序（**已裁决：高字节在前 BE16**，2026-09-17）")
net = bytes([192, 168, 1, 5]) + bytes([255, 255, 255, 0]) + bytes([192, 168, 1, 1]) + bytes([0x25, 0x38])
_, s, f = parse(build_frame(0x47, net))
eq("4.1 0x47 解析 OK", s, "OK")
eq("4.2 端口 BE16：`25 38` → 9528", f["port"], 9528)
eq("4.3 IP 字段", tuple(f["ip"]), (192, 168, 1, 5))
eq("4.4 掩码/网关", (tuple(f["mask"]), tuple(f["gw"])), ((255, 255, 255, 0), (192, 168, 1, 1)))
eq("4.5 0x47 载荷 13B（<14）→ ERR_PARAM", parse(build_frame(0x47, net[:-1]))[1], "ERR_PARAM")
eq("4.6 0x47 载荷 15B（>14，多余忽略）→ OK", parse(build_frame(0x47, net + b"\x00"))[1], "OK")
eq("4.7 端口 LE 反序 `38 25` → 14373（交换值，区别于 9528）",
   parse(build_frame(0x47, net[:-2] + bytes([0x38, 0x25])))[2]["port"], 14373)
eq("4.8 0x48 查询 IP 无参 → OK", parse(build_frame(0x48))[1], "OK")
eq("4.9 0x48 带参 → ERR_PARAM", parse(build_frame(0x48, b"\x00"))[1], "ERR_PARAM")
eq("4.10 0x51 入站 → 丢弃（ERR_CMD）", parse(build_frame(0x51, net))[1], "ERR_CMD")
eq("4.11 0x51 应答帧可构造（14B 载荷，总长 18）", len(build_frame(0x51, net)), 18)
eq("4.12 0x51 帧 probe READY", probe(build_frame(0x51, net))[0], "READY")

# ---- §5 屏体参数 / 全屏点亮 / 版本号 ----
sec("§5 0x49 / 0x01 / 0x02")
eq("5.1 0x49 宋体 16 点阵", parse(build_frame(0x49, b"\x00\x00"))[2], {"font": 0, "width": 0})
eq("5.2 0x49 黑体 32 点阵", parse(build_frame(0x49, b"\x03\x02"))[2], {"font": 3, "width": 2})
eq("5.3 0x49 字体 4 越界 → ERR_PARAM", parse(build_frame(0x49, b"\x04\x00"))[1], "ERR_PARAM")
eq("5.4 0x49 字宽 3 越界 → ERR_PARAM", parse(build_frame(0x49, b"\x00\x03"))[1], "ERR_PARAM")
eq("5.5 0x49 长度 1 → ERR_PARAM", parse(build_frame(0x49, b"\x00"))[1], "ERR_PARAM")
eq("5.6 0x01 全屏红（01）", parse(build_frame(0x01, b"\x01"))[2]["fill_color"], 1)
eq("5.7 0x01 黄（03）", parse(build_frame(0x01, b"\x03"))[2]["fill_color"], 3)
eq("5.8 0x01 白（07，扩展）", parse(build_frame(0x01, b"\x07"))[2]["fill_color"], 7)
eq("5.9 0x01 值 0 → ERR_PARAM", parse(build_frame(0x01, b"\x00"))[1], "ERR_PARAM")
eq("5.10 0x01 值 8 → ERR_PARAM", parse(build_frame(0x01, b"\x08"))[1], "ERR_PARAM")
eq("5.11 0x02 版本号（1B 参数 00）", parse(build_frame(0x02, b"\x00"))[1], "OK")
eq("5.12 0x02 长度 0 → ERR_PARAM", parse(build_frame(0x02))[1], "ERR_PARAM")

# ---- §6 帧结构边界 ----
sec("§6 帧结构边界")
eq("6.1 长度字段与实际不符 → ERR_FRAME", parse(bytes([STX, 0x31, 0x05, 0x7D]))[1], "ERR_FRAME")
eq("6.2 3 字节非法帧（<4）→ ERR_FRAME", parse(b"{1}")[1], "ERR_FRAME")
eq("6.3 最大帧（255B 参数）probe READY", probe(build_frame(0x34, b"A" * 255))[0], "READY")
eq("6.4 最大帧总长 259", probe(build_frame(0x34, b"A" * 255))[1], FRAME_LEN_MAX)
eq("6.5 255B 参数帧 parse OK（'4' 无文本上限）", parse(build_frame(0x34, b"0\x00\x00" + b"A" * 252))[1], "OK")

# ---- §7 与云南常规的互斥/共存判定（帧头纪律的离线证据）----
sec("§7 与云南常规/四川 MTC 的共槽判定（文档纪律离线证据）")
YN_REGULAR_CMDS = set(b"123456789AB") | {0x01, 0x02}
MTC_CMDS = set(b"123456789") | {0x41} | set(range(0x40, 0x46))  # '1'~'9' / 'A' / 0x40~0x45
yn_ol_ascii = {c for c in SUPPORTED_CMDS if 0x20 <= c <= 0x7E}
overlap_yn = yn_ol_ascii & YN_REGULAR_CMDS
check(f"7.1 与云南常规重叠集 = 1/2/3/4/5/8/A + 0x42(='B')（{len(overlap_yn)} 个）",
      overlap_yn == {0x31, 0x32, 0x33, 0x34, 0x35, 0x38, 0x41, 0x42})
check("7.2 **0x42 同字节歧义**：本协议=第一行清除 / 云南常规='B' 费额语音（probe 层不可分，"
      "必须 EIDE 互斥）", 0x42 in SUPPORTED_CMDS and 0x42 in YN_REGULAR_CMDS)
only_yn_ol = {c for c in SUPPORTED_CMDS if c not in YN_REGULAR_CMDS}
check(f"7.3 本协议相对云南常规的独有命令字 = 0x43~0x49 + 0x50/0x51（{sorted(hex(c) for c in only_yn_ol)}）",
      only_yn_ol == (set(range(0x43, 0x4A)) | {0x50, 0x51}))
overlap_mtc = yn_ol_ascii & MTC_CMDS | (set(range(0x42, 0x47)) & MTC_CMDS)
check(f"7.4 与四川 MTC 重叠 ⊇ 0x42~0x45（MTC '}}' 定界扫描先注册先认领，实际重叠 {len(overlap_mtc)} 个）",
      {0x42, 0x43, 0x44, 0x45} <= overlap_mtc)
check("7.5 '6'/'7'/'9' 互不认领（本协议 FAKE、常规认领）",
      all(c not in SUPPORTED_CMDS for c in (0x36, 0x37, 0x39)) and all(c in YN_REGULAR_CMDS for c in (0x36, 0x37, 0x39)))
check("7.6 首字节同 '{' → 无法用 probe 层区分，需编译期 g_brace_proto_guard 互斥", STX == 0x7B)

# ---- §8 联调帧样例（可复制进上位机）----
sec("§8 联调帧样例（hex，可直接发送）")
SAMPLES = [
    ("'1' 主机查询（期望 7B 31 01 00 7D）", build_frame(0x31)),
    ("'5' 全屏清除", build_frame(0x35)),
    ("'3' 第 1 行红色 'A'（第 1 行 = 行号 '1'）", build_frame(0x33, b"01" + b"A")),
    ("'3' 第 2 行绿色 'ETC车道'（GBK）", build_frame(0x33, b"12" + "ETC车道".encode("gbk"))),
    ("'4' 全屏红字 (0,44) 'ETC车道已关闭'（协议文档示例）",
     build_frame(0x34, bytes([0x30, 0x00, 0x2C]) + "ETC车道已关闭".encode("gbk"))),
    ("'8' 亮度自动（NUL）", build_frame(0x38, b"\x00")),
    ("'A' 绿灯开/红灯关/黄闪关", build_frame(0x41, b"\x01")),
    ("'A' 红灯开/黄闪开（云南 P6 治超屏 7B 41 01 04 7D 黄闪开口径）", build_frame(0x41, b"\x04")),
    ("0x42 第一行清除", build_frame(0x42)),
    ("0x50 第六行清除", build_frame(0x50)),
    ("0x48 查询 IP（期望 0x51 应答）", build_frame(0x48)),
    ("0x49 宋体 16 点阵", build_frame(0x49, b"\x00\x00")),
    ("0x01 全屏点亮红色", build_frame(0x01, b"\x01")),
    ("0x02 版本号（期望裸 ASCII PROGRAM_CODE + 屏显）", build_frame(0x02, b"\x00")),
    ("0x47 修改 IP 192.168.1.5/24 gw .1 port 9528（25 38 BE16）",
     build_frame(0x47, bytes([192, 168, 1, 5, 255, 255, 255, 0, 192, 168, 1, 1, 0x25, 0x38]))),
]
for name, fr in SAMPLES:
    print(f"  {name}\n    {fr.hex(' ').upper()}  ({len(fr)}B)")
    check(f"8.x 样例 probe READY：{name}", probe(fr)[0] == "READY")

# ================================================================
#  §9 TCP 双通道绑定与 RJ45 槽 probe 竞争（2026-09-17 裁决 Q1）
#
#  事实基础（全部由 Application/Src 静态扫描得出，不靠人记）：
#    · frame_dispatch_task 取 proto = ch_proto_map[ch->ch_id]，
#      仅掩码命中的协议进入该通道的 probe 链（app_dispatch.c:252 / :303）。
#    · 本模块绑 CH_ID_RS485 / RS232 / TCP_SERVER / TCP_CLIENT（四 mask 共一队列）。
#    · CQ 只绑 CH_ID_UDP / CH_ID_UDP_CQ → **TCP 链上 CQ probe 不会被调用**。
#    · 本模块**不绑** CH_ID_UDP → CQ 的 10011 行为逐字节不变。
# ================================================================

SRC_DIR = Path(__file__).resolve().parents[2] / "Application" / "Src"


def _src(p: Path) -> str:
    return p.read_text(encoding="utf-8", errors="replace")


def _code(text: str) -> str:
    """去掉 /* */ 与 // 注释后的代码（静态检查只看代码，不看注释）。"""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def _module_key(fname: str) -> str:
    known = {
        "app_yn_ol_proto.c": "yn_ol",
        "app_yn_proto.c": "yn",
        "app_ldi.c": "ldi",
        "app_cq_proto.c": "cq",
        "app_iap.c": "iap",
        "app_gz_ol_proto.c": "gz_ol",
        "app_gz_proto_default.c": "gz_default",
        "app_gz_ol_proto_default.c": "gz_ol_default",
    }
    return known.get(fname, fname)


_BIND_PAT = re.compile(r"app_proto_bind_channel\s*\(\s*[A-Za-z0-9_]+\s*,\s*(CH_ID_[A-Z0-9_]+)\s*\)")


def scan_channel_bindings():
    """静态扫描 Application/Src/**/*.c 的 app_proto_bind_channel 调用 → {ch: {module}}。"""
    table = {}
    for f in sorted(SRC_DIR.rglob("*.c")):
        txt = _src(f)
        hits = _BIND_PAT.findall(txt)
        if not hits:
            continue
        for ch in hits:
            table.setdefault(ch, set()).add(_module_key(f.name))
    return table


BINDS = scan_channel_bindings()
TCP_CHAIN = BINDS.get("CH_ID_TCP_SERVER", set()) | BINDS.get("CH_ID_TCP_CLIENT", set())
UDP_CHAIN = BINDS.get("CH_ID_UDP", set())
YNOL_CHANNELS = {ch for ch, mods in BINDS.items() if "yn_ol" in mods}

# ---- 其它槽位协议的 probe 镜像（仅判定用；只镜像 FAKE/WAIT/READY 判别，不镜像业务语义）----
CQ_BIN_RESTART = bytes([0xFF, 0xFF, 0x52, 0x53, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01])
CQ_BIN_SEARCH = bytes([0xFF, 0xFF, 0x53, 0x45, 0x41, 0x52, 0x43, 0x48, 0x00, 0x00, 0x00, 0x02])


def ldi_probe(buf: bytes):
    """镜像 ldi_probe_frame 的判别部分（STX FF FF + ver 0x00 + 4B **大端** LEN + DATA + CRC16）。

    关键镜像点：`data_len == 2` → FAKE（放行 CQ 的 12B 二进制帧）；len<1 或 >502 → FAKE。
    """
    if len(buf) == 0 or buf[0] != 0xFF:
        return ("FAKE", None)
    if len(buf) < 8:  # sizeof(ldi_frame_t) = stx2 + ver + seq + len4
        return ("WAIT", None)
    if buf[1] != 0xFF or buf[2] != 0x00:
        return ("FAKE", None)
    dlen = int.from_bytes(buf[4:8], "big")
    if dlen == 2 or dlen < 1 or dlen > 502:
        return ("FAKE", None)
    need = 8 + dlen + 2
    return ("WAIT", None) if len(buf) < need else ("READY", need)


def iap_probe(buf: bytes):
    """镜像 iap_probe_frame 的判别部分（5A5A5A5A + … 4B len + 载荷 + CRC32）。"""
    if len(buf) == 0 or buf[0] != 0x5A:
        return ("FAKE", None)
    if len(buf) < 16:
        return ("WAIT", None)
    if buf[:4] != b"\x5A\x5A\x5A\x5A":
        return ("FAKE", None)
    total = 16 + int.from_bytes(buf[12:16], "little") + 4
    return ("WAIT", None) if len(buf) < total else ("READY", total)


def cq_probe(buf: bytes):
    """镜像 cq_probe_frame：'{' 花括号深度扫描 / 两个 12B 二进制帧精确比对 / 其它 FAKE。"""
    if len(buf) == 0:
        return ("FAKE", None)
    if buf[0] == STX:
        depth, in_str, i = 0, False, 0
        while i < len(buf):
            c = buf[i]
            if in_str:
                if c == 0x22:
                    in_str = False
            elif c == 0x22:
                in_str = True
            elif c == STX:
                depth += 1
                if depth > 16:
                    return ("FAKE", None)
            elif c == ETX:
                depth -= 1
                if depth == 0:
                    return ("READY", i + 1)
            i += 1
        return ("WAIT", None)
    if buf[0] == 0xFF:
        if len(buf) < 12:
            return ("WAIT", None)
        if buf[:12] in (CQ_BIN_RESTART, CQ_BIN_SEARCH):
            return ("READY", 12)
        return ("FAKE", None)
    return ("FAKE", None)


def gz_ol_probe(buf: bytes):
    """镜像 gz_ol_probe_frame 的判别部分（"TCLY" + 2B LE 整帧长）。"""
    if len(buf) == 0 or buf[0] != 0x54:
        return ("FAKE", None)
    if len(buf) < 8:
        return ("WAIT", None)
    if buf[:4] != b"TCLY":
        return ("FAKE", None)
    total = buf[6] | (buf[7] << 8)
    if len(buf) < total:
        return ("WAIT", None)
    return ("READY", total)


PROBES = {"yn_ol": probe, "cq": cq_probe, "ldi": ldi_probe, "iap": iap_probe, "gz_ol": gz_ol_probe}


def chain_round(chain, buf: bytes):
    """镜像 frame_dispatch_task 单轮（同一 rb 上的链式探测）。

    返回 (owner, frame_len) / ("WAIT", None) / ("RESYNC", None)。
    语义：READY 立即认领消费；WAIT 记为「待更多字节」但**继续探测下一协议**；
    FAKE 放行下一协议；全链无消费且有人 WAIT → 等更多字节（不消费）；
    全链 FAKE → 框架逐字节重同步（消费 1 字节）。
    """
    any_wait = False
    for name in chain:
        st, ln = PROBES[name](buf)
        if st == "READY":
            return (name, ln)
        if st == "WAIT":
            any_wait = True
    return ("WAIT", None) if any_wait else ("RESYNC", None)


def build_ldi(payload: bytes) -> bytes:
    """构造 LDI 帧：FF FF + ver 0x00 + seq 0x00 + 4B BE LEN + DATA + CRC16（2B）。"""
    return bytes([0xFF, 0xFF, 0x00, 0x00]) + len(payload).to_bytes(4, "big") + payload + b"\x00\x00"


def build_iap(payload: bytes) -> bytes:
    return b"\x5A\x5A\x5A\x5A" + b"\x00" * 8 + len(payload).to_bytes(4, "little") + payload + b"\x00" * 4


def build_gz_ol(payload: bytes = b"") -> bytes:
    total = 4 + 4 + 2 + 2 + 4 + len(payload) + 1
    return b"TCLY" + b"\x00" * 4 + total.to_bytes(2, "little") + b"\x00\x00" + b"\x10\x00\x00\x00" + payload + b"\x00"


sec("§9 TCP 双通道绑定与 RJ45 槽 probe 竞争（裁决 Q1）")

check(f"9.1 本模块绑定通道 = RS485 + RS232 + TCP Server + TCP Client"
      f"（实扫：{[c for c in sorted(YNOL_CHANNELS)]}）",
      YNOL_CHANNELS == {"CH_ID_RS485", "CH_ID_RS232", "CH_ID_TCP_SERVER", "CH_ID_TCP_CLIENT"})
check(f"9.2 **本模块未绑 CH_ID_UDP**（实扫 UDP 链 = {sorted(UDP_CHAIN)}）→ CQ 10011 行为零影响",
      "yn_ol" not in UDP_CHAIN and "cq" in UDP_CHAIN)
check(f"9.3 **TCP 链上只有 LDI 与本模块**（实扫 = {sorted(TCP_CHAIN)}）→ CQ probe 在 TCP 上根本不被调用",
      TCP_CHAIN == {"ldi", "yn_ol"} and "cq" not in TCP_CHAIN)

ynol1 = build_frame(0x31)
for ch in ("CH_ID_RS485", "CH_ID_RS232", "CH_ID_TCP_SERVER", "CH_ID_TCP_CLIENT"):
    conn = {0x31: build_frame(0x31), 0x35: build_frame(0x35), 0x48: build_frame(0x48)}
    st = probe(conn[0x31])
    check(f"9.4 YN_OL `7B 31 00 7D` 在 {ch} 上由本模块 READY（状态 {st}）", st == ("READY", 4))

# 双向快拒：CQ 的帧喂给本 probe 必须 FAKE（不被误认、不卡住）
cq_json = b'{"cmd":"display","text":"hello"}' + b"}"
eq("9.5 CQ JSON 帧喂给本 probe → FAKE（第二字节 '\"' 不在命令字白名单）", probe(cq_json)[0], "FAKE")
eq("9.6 CQ JSON 半帧（仅 '{'）喂给本 probe → WAIT（不吞、可等后续）", probe(b"{")[0], "WAIT")
eq("9.7 CQ 二进制 12B 帧喂给本 probe → FAKE（首字节 0xFF 快拒）", probe(CQ_BIN_RESTART)[0], "FAKE")
eq("9.8 LDI 帧（FF FF）喂给本 probe → FAKE（首字节快拒，同链不互抢）", probe(build_ldi(b"\x01\x02"))[0], "FAKE")
eq("9.9 IAP 帧（5A5A5A5A）喂给本 probe → FAKE", probe(build_iap(b"\x00"))[0], "FAKE")
eq("9.10 GZ_OL 帧（TCLY）喂给本 probe → FAKE", probe(build_gz_ol())[0], "FAKE")

# 链上协作（TCP：LDI + 本模块，两种注册序都必须正确归属）
tcp_chain = ["ldi", "yn_ol"]
for order in (["ldi", "yn_ol"], ["yn_ol", "ldi"]):
    eq(f"9.11 TCP 链序 {order}：YN_OL 帧归属 yn_ol", chain_round(order, ynol1), ("yn_ol", 4))
    eq(f"9.12 TCP 链序 {order}：LDI 帧归属 ldi", chain_round(order, build_ldi(b"\x01")), ("ldi", 11))
    eq(f"9.13 TCP 链序 {order}：CQ 二进制帧全链 FAKE → 框架重同步（不吞帧）",
       chain_round(order, CQ_BIN_RESTART)[0], "RESYNC")

# 半帧 / 粘包 / 分片
half_ynol = build_frame(0x33, b"01" + b"A")
eq("9.14 TCP 上半帧 YN_OL（缺尾字节）→ 链 WAIT（本 probe 认领前不放行）", chain_round(tcp_chain, half_ynol[:-1])[0], "WAIT")
eq("9.15 补齐后 → 链 READY 且归属 yn_ol", chain_round(tcp_chain, half_ynol), ("yn_ol", len(half_ynol)))
eq("9.16 TCP 粘包（YN_OL + YN_OL）：第一帧 READY 4B", chain_round(tcp_chain, ynol1 + build_frame(0x35)), ("yn_ol", 4))
eq("9.17 TCP 粘包：第二帧 READY 4B", chain_round(tcp_chain, (ynol1 + build_frame(0x35))[4:]), ("yn_ol", 4))
eq("9.18 TCP 混合粘包（LDI 帧 + YN_OL 帧）：LDI 先走", chain_round(tcp_chain, build_ldi(b"\x01") + ynol1)[0], "ldi")
eq("9.19 TCP 混合粘包：剩余 YN_OL 帧归本模块",
   chain_round(tcp_chain, (build_ldi(b"\x01") + ynol1)[11:]), ("yn_ol", 4))
eq("9.20 CQ 二进制帧分片（<12B）→ CQ 链 WAIT（不误判为完整帧）", chain_round(["cq"], CQ_BIN_RESTART[:7])[0], "WAIT")
eq("9.21 CQ 二进制帧补齐 → CQ 链 READY 12B", chain_round(["cq"], CQ_BIN_RESTART), ("cq", 12))
eq("9.22 CQ JSON 分片（'{' 未闭合）→ CQ 链 WAIT", chain_round(["cq"], b'{"a":1'), ("WAIT", None))
eq("9.23 CQ JSON 收全 → CQ 链 READY（深度扫描正确闭合）", chain_round(["cq"], b'{"a":1}'), ("cq", 7))

# 反事实：若把本模块绑到 UDP（10011），CQ 收录序更早 → 本协议帧会被 CQ 抢先认领丢弃
check("9.24 **反事实取证**：本模块帧喂给 CQ probe 会被 READY 认领（4B 当完整 JSON）→ "
      "绑 10011 不可用，故裁决只走 TCP",
      cq_probe(build_frame(0x31)) == ("READY", 4))
udp_chain = ["iap", "ldi", "cq", "gz_ol"]
eq("9.25 反事实链（含 CQ）：本模块帧在 UDP 链上被 cq 抢走", chain_round(udp_chain, build_frame(0x31))[0], "cq")
eq("9.26 CQ JSON 帧在 UDP 链上仍归 cq（本模块不参与 → 零削弱）", chain_round(udp_chain, b'{"a":1}')[0], "cq")
eq("9.27 LDI 帧在 UDP 链上归 ldi（既有行为不变）", chain_round(udp_chain, build_ldi(b"\x01"))[0], "ldi")
eq("9.28 IAP 帧在 UDP 链上归 iap（既有行为不变）", chain_round(udp_chain, build_iap(b"\x11"))[0], "iap")

# 全部命令字在 TCP 上均可被本 probe 认领
_for_cmds = [(0x31, b""), (0x32, b""), (0x33, b"01A"), (0x34, b"0\x00\x00A"), (0x35, b""),
             (0x38, b"1"), (0x41, b"\x01"), (0x42, b""), (0x45, b""), (0x46, b""), (0x47, net),
             (0x48, b""), (0x49, b"\x00\x00"), (0x50, b""), (0x01, b"\x01"), (0x02, b"\x00")]
_bad = [hex(c) for c, p in _for_cmds if chain_round(tcp_chain, build_frame(c, p))[0] != "yn_ol"]
check(f"9.29 全部 {len(_for_cmds)} 个入站命令字在 TCP 链上均归本模块（异常 {_bad}）", not _bad)
eq("9.30 '6' 帧（不开发）在 TCP 链上无协议认领 → 框架重同步", chain_round(tcp_chain, build_frame(0x36, b"0"))[0], "RESYNC")
eq("9.31 '7' 帧（不开发）在 TCP 链上无协议认领 → 框架重同步", chain_round(tcp_chain, build_frame(0x37, b"0"))[0], "RESYNC")

# TCP 绑定不新增 LwIP 资源（复用既有 TCP Server/Client 通道）
_ynol_src = _src(SRC_DIR / "ProtocolParser_YunNan_Overload" / "app_yn_ol_proto.c")
_banned = [k for k in ("netconn", "netbuf", "udp_new", "udp_bind", "MEMP_NUM", "tcp_new",
                       "app_tcp_server_start", "app_udp_start", "app_udp_cq_start")
           if k in _ynol_src]
check(f"9.32 本模块源码不含任何 LwIP/通道创建调用（扫描命中 {_banned}）→ TCP 绑定零新增 netconn/PCB",
      not _banned)

# ================================================================
#  §10 0x49 持久化契约（2026-09-17 裁决 Q5）
#
#  记录：magic(4) + version(2) + font_type(1) + font_size(1) + crc32(4) = 12B
#  地址：dev_storage_capacity(w25) - 12288（倒数第三个 4KB 扇区）
#  CRC ：STM32 硬件 CRC 单元（poly 0x04C11DB7 / init 0xFFFFFFFF / 无反转 / 无末异或）
#        = CRC-32/MPEG-2，覆盖前 8B（HAL_CRC_Calculate 按 32 位字喂入，len=8 → 2 字）
# ================================================================

W25Q64_CAP = 8 * 1024 * 1024
MX25L256_CAP = 32 * 1024 * 1024
YN_OL_CFG_MAGIC = 0x594E4F4C
YN_OL_CFG_VERSION = 1
YN_OL_CFG_SECTOR_OFFSET = 12288
CFG_RECORD_SIZE = 12


def crc32_mpeg2(data: bytes) -> int:
    """STM32 硬件 CRC32 镜像：poly 0x04C11DB7 / init 0xFFFFFFFF / 不反转 / 无末异或。"""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if (crc & 0x80000000) else (crc << 1) & 0xFFFFFFFF
    return crc


def cfg_record(font_type: int, font_size: int, version: int = YN_OL_CFG_VERSION,
               magic: int = YN_OL_CFG_MAGIC, break_crc: bool = False) -> bytes:
    head = magic.to_bytes(4, "little") + version.to_bytes(2, "little") + bytes([font_type, font_size])
    crc = crc32_mpeg2(head)
    if break_crc:
        crc ^= 0xDEADBEEF
    return head + crc.to_bytes(4, "little")


def cfg_load(sector: bytes):
    """镜像 yn_ol_screen_cfg_load：返回 (ok, font_type, font_size, 是否打告警)。"""
    if sector[:4] == b"\xFF" * 4 and sector[8:12] == b"\xFF" * 4:
        return (False, 0, 0, False)  # 空扇区：静默回默认
    magic = int.from_bytes(sector[0:4], "little")
    version = int.from_bytes(sector[4:6], "little")
    font_type, font_size = sector[6], sector[7]
    crc = int.from_bytes(sector[8:12], "little")
    ok = (magic == YN_OL_CFG_MAGIC and version == YN_OL_CFG_VERSION
          and font_type <= 3 and font_size in (16, 24, 32)
          and crc == crc32_mpeg2(sector[:8]))
    return (ok, font_type, font_size, not ok)


# 字库区块表镜像（app_render.c g_font_regions / g_font_regions_orig；base + sec + X）
W25Q64_REGIONS = [
    (16, "ascii", 0, 0, 0, 0, 16), (16, "ascii", 0, 0, 0, 0, 16),
    (16, "ascii", 1, 0, 0, 64, 16), (16, "ascii", 1, 0, 0, 64, 16),
    (16, "gbk", 2, 0, 0, 128, 32), (16, "gbk", 71, 0, 0, 288, 32),
    (16, "gbk", 140, 0, 0, 448, 32), (16, "gbk", 209, 0, 2, 96, 32),
    (24, "ascii", 278, 0, 3, 0, 48), (24, "ascii", 279, 0, 12, -224, 48),
    (24, "ascii", 281, 0, 3, 64, 48), (24, "ascii", 282, 0, 12, -160, 48),
    (24, "gbk", 284, 1, -12, -128, 72), (24, "gbk", 439, 1, -7, -64, 72),
    (24, "gbk", 594, 1, -2, 0, 72), (24, "gbk", 750, 1, -12, -192, 72),
    (32, "ascii", 905, 1, -7, -128, 64), (32, "ascii", 907, 1, -7, -96, 64),
    (32, "ascii", 909, 1, -7, -64, 64), (32, "ascii", 911, 1, -7, -32, 64),
    (32, "gbk", 913, 1, -7, 0, 128), (32, "gbk", 1189, 1, -5, 32, 128),
    (32, "gbk", 1465, 1, -3, 64, 128), (32, "gbk", 1741, 1, -1, 96, 128),
]
MX25L256_REGIONS = [
    (14, "ascii", 0, 0, 0, 0, 14), (14, "gbk", 1, 0, 5, 0, 28),
    (16, "ascii", 655, 0, 14, 192, 16), (16, "gbk", 657, 0, 6, 192, 32),
    (20, "gbk", 1409, 0, 4, 192, 60), (24, "gbk", 2816, 0, 8, 128, 72),
    (32, "gbk", 4505, 0, 13, 0, 128), (32, "gbk", 6750, 0, 3, 0, 128),
]


def max_font_addr(regions, cols_190: bool, n_chars: int) -> int:
    """区块表最大可读地址（最坏字符索引 → FonfAddr → readaddr），字模长度计入尾部。"""
    worst = 0
    for size, enc, base, x, y, z, bpc in regions:
        idx = n_chars - 1
        fonf = idx * bpc
        sec, rem = divmod(fonf, 4096)
        page, byte = divmod(rem, 256)
        addr = (base + sec + x) * 4096 + (page + y) * 256 + byte + z
        worst = max(worst, addr + bpc - 1)
    return worst


# GB2312 94 列序（W25Q64）：(0xFE-0xA1)*94 + (0xFE-0xA1) = 8835；GBK 190 列序：23875
N_GBK_94 = 94 * 94
N_GBK_190 = 190 * 126

sec("§10 0x49 屏体参数持久化契约（裁决 Q5）")

eq("10.1 CRC 镜像自洽（CRC-32/MPEG-2('123456789') = 0x0376E6E7）", crc32_mpeg2(b"123456789"), 0x0376E6E7)
eq("10.2 记录长度 12B", len(cfg_record(0, 16)), CFG_RECORD_SIZE)

# 写→读→装载一致
for ft, fs in ((0, 16), (3, 32), (2, 24), (1, 16)):
    sec_buf = bytearray(b"\xFF" * 4096)
    sec_buf[0:CFG_RECORD_SIZE] = cfg_record(ft, fs)
    ok, got_ft, got_fs, warned = cfg_load(bytes(sec_buf))
    check(f"10.3 写→读一致：font_type={ft} font_size={fs}（装载 {ok}/{got_ft}/{got_fs}）",
          ok and got_ft == ft and got_fs == fs and not warned)

# 空扇区 → 静默回默认（不打告警）
ok, _, _, warned = cfg_load(b"\xFF" * 4096)
check("10.4 空扇区（全 0xFF）→ 保持默认且**不打告警**（首次上电正常路径）", ok is False and warned is False)
# 损坏/越界 → 回默认 + 告警
ok, _, _, warned = cfg_load(cfg_record(0, 16, magic=0x12345678))
check("10.5 魔数错 → 回默认 + 告警", ok is False and warned is True)
ok, _, _, warned = cfg_load(cfg_record(0, 16, version=9))
check("10.6 版本不匹配 → 回默认 + 告警", ok is False and warned is True)
ok, _, _, warned = cfg_load(cfg_record(7, 16))
check("10.7 字体字段越界（7）→ 回默认 + 告警", ok is False and warned is True)
ok, _, _, warned = cfg_load(cfg_record(0, 20))
check("10.8 字宽字段非法（20 点阵不支持）→ 回默认 + 告警", ok is False and warned is True)
ok, _, _, warned = cfg_load(cfg_record(0, 16, break_crc=True))
check("10.9 CRC 损坏 → 回默认 + 告警", ok is False and warned is True)

# 扇区选址证明
eq("10.10 W25Q64 本记录扇区 = 2045（cap-12288）", (W25Q64_CAP - YN_OL_CFG_SECTOR_OFFSET) // 4096, 2045)
eq("10.11 app_render persist 扇区 = 2046（cap-8192）", (W25Q64_CAP - 8192) // 4096, 2046)
eq("10.12 LDI 记录扇区 = 2047（cap-4096）", (W25Q64_CAP - 4096) // 4096, 2047)
_w25_max = max_font_addr(W25Q64_REGIONS, False, N_GBK_94)
_mx_max = max_font_addr(MX25L256_REGIONS, True, N_GBK_190)
check(f"10.13 W25Q64 字库最大绝对地址 {_w25_max} (扇区 {_w25_max // 4096}) < 本记录扇区起点 "
      f"{W25Q64_CAP - YN_OL_CFG_SECTOR_OFFSET} → 无重叠",
      _w25_max < W25Q64_CAP - YN_OL_CFG_SECTOR_OFFSET)
check(f"10.14 MX25L256 字库最大绝对地址 {_mx_max} (扇区 {_mx_max // 4096}) < 本记录扇区起点 "
      f"{MX25L256_CAP - YN_OL_CFG_SECTOR_OFFSET} → 无重叠",
      _mx_max < MX25L256_CAP - YN_OL_CFG_SECTOR_OFFSET)

# 全仓 W25 写入者清单（静态扫描；app_board_net_cfg 绑的是**内部 Flash**（dev_flash_int），
# 不计入 W25 写入者）：证明 [cap-12288, cap-8192) 区间只有本模块
_board = _src(SRC_DIR / "Config" / "app_board_net_cfg.c")
check("10.15a 板级配置写的是内部 Flash（含 dev_flash_int / flash_int_ops）→ 不计入 W25 写入者",
      "dev_flash_int" in _board and "flash_int_ops" in _board)
_writers = []
for f in sorted(SRC_DIR.rglob("*.c")):
    if f.name == "app_board_net_cfg.c":
        continue
    txt = _code(_src(f))
    for m in re.finditer(r"dev_storage_(?:write|erase)\s*\(", txt):
        _writers.append(f.name)
check(f"10.15b W25 写入者全仓静态清单 = {sorted(set(_writers))}（恰为 app_render persist / LDI / 本模块 → "
      "本扇区独占，无整扇区保全负担）",
      set(_writers) == {"app_render.c", "app_ldi_cfg.c", "app_yn_ol_proto_cmd.c"})

_cfg_src = _src(SRC_DIR / "ProtocolParser_YunNan_Overload" / "app_yn_ol_proto_cmd.c")
_cfg_code = _code(_cfg_src)
check("10.16 落盘/装载地址取自 dev_storage_capacity（不硬编码绝对地址）",
      "dev_storage_capacity" in _cfg_code and "YN_OL_CFG_SECTOR_OFFSET" in _cfg_code)
check("10.17 **不持有整扇区镜像**（代码无 [4096] 声明；dev_w25qxx._write 内部整扇区读-改-写）",
      re.search(r"\[\s*4096\s*\]", _cfg_code) is None)
check("10.18 记录缓冲为 12B 静态 SRAM（s_yn_ol_cfg_rec，仿 LDI 记录范式）",
      "static yn_ol_screen_cfg_record_t s_yn_ol_cfg_rec" in _cfg_code)
_nvic_calls = len(re.findall(r"NVIC_SystemReset\s*\(\s*\)", _cfg_code))
check(f"10.19 唯一的重启调用是 0x47 改 IP 语义（代码内 {_nvic_calls} 处）；装载/保存路径不阻塞、不重启",
      _nvic_calls == 1
      and not re.search(r"(yn_ol_screen_cfg_(?:load|save))\s*\([^)]*\)\s*\{[^}]*while", _cfg_code, re.S))

# 默认口径 = STD 工程既有默认（FONT_16 / FONT_ST）
_h_default = "static font_size_t s_yn_ol_font_size = FONT_16;" in _cfg_code
_t_default = "static font_type_t s_yn_ol_font_type = FONT_ST;" in _cfg_code
_qh = _src(SRC_DIR / "ProtocolParser_QingHai" / "app_qh_proto_cmd.c")
_gz = _src(SRC_DIR / "ProtocolParser_GuiZhou" / "app_gz_proto_cmd.c")
_yn = _src(SRC_DIR / "ProtocolParser_YunNan" / "app_yn_proto_cmd.c")
check("10.20 默认字号/字型 = FONT_16 / FONT_ST，与青海/贵州（同为 `{` 族、协议未限定字号）同口径；"
      "云南常规因其协议明示 24 点阵而用 FONT_24（协议要求，非本模块默认口径冲突）",
      _h_default and _t_default
      and ".font_size = FONT_16" in _qh and ".font_size = FONT_16" in _gz
      and "#define YN_ROW_PX (FONT_24)" in _yn)

# ================================================================
#  §11 四通道应答回源（'1' / 0x48 / 0x47 在四个通道上均回原通道）
# ================================================================

FOUR_CH = ["CH_ID_RS485", "CH_ID_RS232", "CH_ID_TCP_SERVER", "CH_ID_TCP_CLIENT"]


def reply_of(cmd_byte: int, payload: bytes = b"") -> bytes:
    """镜像 _yn_ol_send_frame：`{` + cmd + len + payload + `}`。"""
    return bytes([STX, cmd_byte, len(payload)]) + payload + bytes([ETX])


sec("§11 四通道应答回源（单播 + 共享队列）")

check(f"11.1 四个通道在绑定表中均含本模块（实扫 = {[c for c in FOUR_CH if 'yn_ol' in BINDS.get(c, set())]}）",
      all("yn_ol" in BINDS.get(c, set()) for c in FOUR_CH))
_r1 = reply_of(0x31, b"\x00")
eq("11.2 '1' 主机查询应答 = `7B 31 01 00 7D`（4B，同帧族）", _r1.hex(" ").upper(), "7B 31 01 00 7D")
_ack = reply_of(0x51, bytes([192, 168, 114, 200, 255, 255, 255, 0, 192, 168, 114, 1, 0x25, 0x38]))
eq("11.3 0x48 → 0x51 应答 18B，端口 BE16 `25 38` = 9528",
   (_ack[1], len(_ack), (_ack[15] << 8) | _ack[16]), (0x51, 18, 9528))
eq("11.4 0x47 → 0x51 回显载荷 14B（回显请求值，非读回记录）",
   (reply_of(0x51, bytes(range(14)))[1], reply_of(0x51, bytes(range(14)))[2]), (0x51, 14))
_q_link = _src(SRC_DIR / "ProtocolParser_YunNan_Overload" / "app_yn_ol_proto.c")
check("11.5 四 mask 共用同一静态队列（4 次 app_proto_set_frame_queue 均传 s_yn_ol_queue）",
      _q_link.count("app_proto_set_frame_queue(") == 4
      and all("s_yn_ol_queue)" in ln for ln in _q_link.splitlines()
              if "app_proto_set_frame_queue(" in ln))
check("11.6 应答恒走 channel_send(ch, …)（ch = 帧来源通道 → 单播回源；无广播）",
      "channel_send(ch," in _cfg_src and "_send_frame(ch" in _cfg_src)
check("11.7 应答组帧缓冲上界 18B（头 3 + 载荷 14 + 尾 1），无溢出风险",
      "YN_OL_HEAD_LEN + YN_OL_NET_PAYLOAD_LEN + 1U" in _cfg_code)

# ================================================================
#  §12 裁决轮 as-built 源码断言（Q6 上电画面删除 / Q7 帧族守卫 / 编译期口径）
# ================================================================

YNOL_DIR = SRC_DIR / "ProtocolParser_YunNan_Overload"

sec("§12 裁决轮 as-built 源码断言（Q6 / Q7）")

_files = sorted(p.name for p in YNOL_DIR.glob("*.c"))
check(f"12.1 模块源文件 = {_files}（**上电画面 app_yn_ol_proto_default.c 已删除**）",
      _files == ["app_yn_ol_proto.c", "app_yn_ol_proto_cmd.c", "app_yn_ol_proto_parse.c"])
_all_ynol = " ".join(_src(p) for p in YNOL_DIR.glob("*.[ch]"))
check("12.2 模块内无 app_default_display_register 调用（Q6：上电画面不实现）",
      "app_default_display_register" not in _code(_all_ynol))
check("12.3 模块内无 5s 熄灭/blank 惰性任务残留（无 yn_ol_blank / osTimerNew）",
      "yn_ol_blank" not in _all_ynol and "osTimerNew" not in _code(_all_ynol))
_guard = _src(YNOL_DIR / "app_yn_ol_proto.c")
check("12.4 `{` 帧族守卫：`#ifndef STD_ALL_PROTO` 包裹的 g_brace_proto_guard 存在（Q7）",
      "#ifndef STD_ALL_PROTO" in _guard and "g_brace_proto_guard" in _guard
      and "__attribute__((used))" in _guard)
_mk = _src(SRC_DIR.parents[1] / "Makefile")
check("12.5 Makefile 收录本模块 3 个 .c 且**不收录** default 文件（Q6 同步）",
      _mk.count("ProtocolParser_YunNan_Overload/app_yn_ol_proto") == 3
      and "app_yn_ol_proto_default" not in _mk)
_eide = _src(SRC_DIR.parents[1] / ".eide" / "eide.yml")
check("12.6 `.eide/eide.yml` 收录本模块 3 个 .c（无 default 条目，Q6 同步；改后需 EIDE: Reload Project）",
      _eide.count("ProtocolParser_YunNan_Overload/app_yn_ol_proto") == 3
      and "app_yn_ol_proto_default" not in _eide)

# ---- §13 联调轮（2026-09-17 晚）：编码容错 / 逐帧诊断 / 执行状态码 as-built 断言 ----
sec("§13 联调轮 as-built 源码断言（'3' 编码容错 / [yn_ol] 逐帧诊断 / exec ret）")

_psrc = _src(YNOL_DIR / "app_yn_ol_proto_parse.c")
check("13.1 C 侧容错助手 `_yn_ol_parse_color` / `_yn_ol_parse_row` 存在（与 §3.24~3.29 镜像同源）",
      "_yn_ol_parse_color" in _psrc and "_yn_ol_parse_row" in _psrc)
check("13.2 '3' 与 '4' 均经容错助手解析颜色（'3' 行号亦经助手）",
      _psrc.count("_yn_ol_parse_color(cmd.data[0], &color)") == 2
      and "_yn_ol_parse_row(cmd.data[1], &row)" in _psrc)
_csrc = _src(YNOL_DIR / "app_yn_ol_proto_cmd.c")
check("13.3 执行函数全部返回 yn_ol_exec_ret_t（`return _yn_ol_exec_` ≥ 12 处）",
      _csrc.count("return _yn_ol_exec_") >= 12)
check("13.4 `yn_ol_execute_cmd` 返回 int（头文件签名，供任务层打 exec ret）",
      "int yn_ol_execute_cmd(channel_t *ch, const yn_ol_parsed_cmd_t *cmd);"
      in _src(YNOL_DIR.parents[1] / "Inc" / "ProtocolParser_YunNan_Overload" / "app_yn_ol_proto.h"))
check("13.5 逐帧诊断：`[yn_ol] rx` 前缀 + YN_OL_RTT_DIAG 门控 + 单帧两次打印纪律（rx/exec/drop 三函数）",
      "[yn_ol] rx" in _guard and "YN_OL_RTT_DIAG" in _guard
      and all(fn in _guard for fn in ("_yn_ol_diag_frame", "_yn_ol_diag_exec", "_yn_ol_diag_drop")))
check("13.6 '3' 诊断含屏体几何与落屏判读（screen_w/screen_h/OFF-SCREEN）",
      "screen_w=%u screen_h=%u rowh=%u" in _guard and "OFF-SCREEN(executed but invisible)" in _guard)

# ================================================================
#  汇总
# ================================================================

print(f"\n{'=' * 62}")
print(f"用例通过 {PASS} / 失败 {FAIL}（共 {PASS + FAIL}）")
print("=" * 62)
sys.exit(1 if FAIL else 0)
