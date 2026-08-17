#!/usr/bin/env python3
"""宿主模拟：四川三协议 + RLS + 青海 在同 RB 链式 probe 的互斥与卡死验证。
复刻 frame_dispatch_task 内层循环语义：
  链序（注册位序）：RLS -> QH -> ETC -> MTC -> OL
  逐字节喂流；对每个 probe 状态：
    READY  -> 按 total_len 读走（any_parsed）
    SKIP   -> 按 frame_len 跳过
    WAIT   -> any_wait=True
    FAKE   -> any_fake=True
  一轮结束：any_parsed 则继续下一轮；否则 any_wait -> 停等更多数据；
  !any_wait && any_fake -> 丢弃 1 字节重同步。
"""
import sys

FAKE, WAIT, READY, SKIP = 0, 1, 2, 3

QH_FIX = '--fix-qh' in sys.argv   # 模拟修复后 QH probe（'A'/'B' data_len==0 拒）

SC_MTC_PAYLOAD_MAX = 74
SC_ETC_PAYLOAD_MAX = 149
SC_OL_FRAME_LEN_MIN = 7
SC_OL_PAYLOAD_MAX = 255 if '--fix-ol' in sys.argv else 249

def etc_probe(buf):
    avail = len(buf)
    if avail == 0: return (FAKE, 0)
    if buf[0] != 0x0A: return (FAKE, 0)
    if avail < 2: return (WAIT, 0)
    c = buf[1]
    if c in (0x36,0x37,0x38,0x39,0x50):
        if avail < 3: return (WAIT, 0)
        if buf[2] != 0x0D: return (FAKE, 0)
        return (READY, 3)
    if c == 0x40:
        if avail < 5: return (WAIT, 0)
        if buf[4] != 0x0D: return (FAKE, 0)
        return (READY, 5)
    if c in (0x00, 0x01):
        if avail < 3: return (WAIT, 0)
        if buf[2] > 6: return (FAKE, 0)
        for off in range(3, min(avail, SC_ETC_PAYLOAD_MAX)):
            if buf[off] == 0x0D: return (READY, off+1)
        if avail >= SC_ETC_PAYLOAD_MAX: return (FAKE, 0)
        return (WAIT, 0)
    return (FAKE, 0)

def mtc_bcc_calc(b, ln):
    x = 0
    for i in range(1, ln-2): x ^= b[i]
    return x & 0xFF

def mtc_probe(buf):
    avail = len(buf)
    if avail == 0: return (FAKE, 0)
    b0 = buf[0]
    if b0 == 0x0A:
        if avail < 3: return (WAIT, 0)
        if buf[1] == 0x46 and buf[2] in (0x0A, 0x0D): return (READY, 3)
        return (FAKE, 0)
    if b0 != 0x7B: return (FAKE, 0)
    if avail < 2: return (WAIT, 0)
    c = buf[1]
    if 0x40 <= c <= 0x45 and c != 0x41:
        fl = 6 if c == 0x40 else (3 if c == 0x45 else 4)
        if avail < fl: return (WAIT, 0)
        return (READY, fl) if buf[fl-1] == 0x7D else (FAKE, 0)
    if not (0x31 <= c <= 0x39 or c == 0x41): return (FAKE, 0)
    if c == 0x41:
        if avail < 4: return (WAIT, 0)
        if buf[3] == 0x7D: return (READY, 4)
        if avail >= 5:
            return (READY, 5) if buf[4] == 0x7D else (FAKE, 0)
        return (WAIT, 0)
    varlen = False
    if c == 0x36:
        if avail < 3: return (WAIT, 0)
        t = buf[2]
        if t == 0x30: lo, hi = 15, 16
        elif t == 0x31: lo, hi = 24, 25
        else: return (FAKE, 0)
    elif c == 0x37:
        if avail < 3: return (WAIT, 0)
        v = buf[2]
        if 0x30 <= v <= 0x37: lo, hi = 4, 5
        elif v == 0x38: varlen = True; lo = hi = 0
        else: return (FAKE, 0)
    else:
        lo, hi = {0x31:(3,4), 0x32:(3,4), 0x35:(3,4), 0x33:(20,21),
                  0x34:(67,68), 0x38:(4,5), 0x39:(4,5)}[c]
    if varlen:
        max_scan = min(avail, SC_MTC_PAYLOAD_MAX)
        for e in range(5, max_scan):
            if buf[e] != 0x7D: continue
            ln = e + 1
            if mtc_bcc_calc(buf[:ln], ln) == buf[ln-2]:
                return (READY, ln)
        if avail >= SC_MTC_PAYLOAD_MAX: return (FAKE, 0)
        return (WAIT, 0)
    if avail >= lo and buf[lo-1] == 0x7D: return (READY, lo)
    if avail >= hi:
        return (READY, hi) if buf[hi-1] == 0x7D else (FAKE, 0)
    return (WAIT, 0)

def qh_probe(buf):
    avail = len(buf)
    if avail == 0: return (FAKE, 0)
    if buf[0] != 0x7B: return (FAKE, 0)
    if avail < 3: return (WAIT, 0)
    c = buf[1]
    if not (0x31 <= c <= 0x39 or c in (0x41, 0x42)): return (FAKE, 0)
    dl = buf[2]
    if QH_FIX and c in (0x41, 0x42) and dl == 0:
        return (FAKE, 0)  # 修复：QH 'A'/'B' 必须带数据
    fl = dl + 4
    if avail < fl: return (WAIT, 0)
    return (READY, fl) if buf[fl-1] == 0x7D else (FAKE, 0)

def rls_probe(buf):
    avail = len(buf)
    if avail == 0: return (FAKE, 0)
    if buf[0] != 0xFF: return (FAKE, 0)
    if avail < 2: return (WAIT, 0)
    if buf[1] != 0xFE: return (FAKE, 0)
    if avail < 19: return (WAIT, 0)     # sizeof(rls_frame_t)+4
    dl = (buf[4] << 8) | buf[5]
    if dl < 19 or dl > 530: return (FAKE, 0)
    if avail < dl: return (WAIT, 0)
    return (READY, dl) if buf[dl-2] == 0x0D and buf[dl-1] == 0x0C else (FAKE, 0)

def ol_probe(buf):
    avail = len(buf)
    if avail == 0: return (FAKE, 0)
    if buf[0] != 0xFF: return (FAKE, 0)
    if avail < 2: return (WAIT, 0)
    ln = buf[1]
    if ln < SC_OL_FRAME_LEN_MIN or ln == 0xFE: return (FAKE, 0)
    if '--fix-ol' in sys.argv and ln > SC_OL_PAYLOAD_MAX: return (FAKE, 0)
    if avail < ln: return (WAIT, 0)
    return (READY, ln) if buf[ln-1] == 0xFF else (FAKE, 0)

CHAIN = [('RLS', rls_probe), ('QH', qh_probe), ('ETC', etc_probe),
         ('MTC', mtc_probe), ('OL', ol_probe)]

def run(stream, feed_limit=10000):
    """模拟 dispatch：stream 逐字节到达（每个事件一次完整探测轮）。"""
    buf = []
    consumed = []          # (proto, len) 记录认领
    idx = 0
    while idx < feed_limit:
        if not buf and idx < len(stream):
            buf.append(stream[idx]); idx += 1
            continue
        if not buf: break
        # 当前字节可探测
        any_parsed = False; any_wait = False; any_fake = False
        for name, p in CHAIN:
            st, ln = p(buf)
            if st == READY:
                consumed.append((name, ln, bytes(buf[:ln])))
                buf = buf[ln:]
                any_parsed = True
                break
            elif st == SKIP:
                buf = buf[ln:]; any_parsed = True; break
            elif st == WAIT: any_wait = True
            elif st == FAKE: any_fake = True
        if any_parsed: continue
        if any_wait:
            if idx >= len(stream): break   # 没更多数据 -> 停等（真实场景等下一包）
            buf.append(stream[idx]); idx += 1
            continue
        if any_fake:
            if not buf: break              # 空缓冲重同步即耗尽，退出
            buf = buf[1:]                  # 重同步 1 字节
            continue
        break                               # 空缓冲
    return consumed, buf

def b(*xs): return bytes(xs)

def mk_ol(cmd, bright, data):
    """治超帧：FF len cmd bright data BCC FF（len=6+len(data)）"""
    body = b'\xFF\x00' + b(cmd, bright) + data
    ln = len(body) + 2
    bcc = 0
    for x in body: bcc ^= x
    return b'\xFF' + b(ln) + b(cmd, bright) + data + b(bcc, 0xFF)

cases = []
# --- 各协议合法帧 ---
cases.append(("ETC 心跳 0A 50 0D", b(0x0A,0x50,0x0D)))
cases.append(("ETC 显示全屏 0A 00 00 <GBK> 0D", b(0x0A,0x00,0x00,0xB3,0xB5,0x0D)))
cases.append(("ETC 亮度 0A 40 03 00 0D", b(0x0A,0x40,0x03,0x00,0x0D)))
cases.append(("MTC 初始化 {1}", b'{1}'))
cases.append(("MTC 点阵 7B 41 00 7D (16点阵)", b'\x7B\x41\x00\x7D'))
cases.append(("MTC 字体 7B 42 00 7D (宋体)", b'\x7B\x42\x00\x7D'))
cases.append(("MTC 颜色 {A1 } + BCC", b'\x7B\x41\x31\x7D\x7D'))
cases.append(("MTC '4' 全屏 67B", b'{4' + b'A'*64 + b'}'))
cases.append(("MTC '6' 客车 15B", b'{60' + b'0123456789A' + b'}'))  # 3+11
# 手工构造 '78' 帧：BCC = '7'^'8'^text
def mk_78(text):
    bcc = ord('7')^ord('8')
    for x in text: bcc ^= x
    return b'{78' + text + b(bcc) + b'}'
cases.append(("MTC '78' 自定义语音(含GBK)", mk_78(b'\xB3\xB5\xC6\xBD\xB0\xB2')))
cases.append(("MTC 0A 46 0A 主机查询", b(0x0A,0x46,0x0A)))
cases.append(("MTC 0A 46 0D 清屏", b(0x0A,0x46,0x0D)))
cases.append(("治超 清屏 FF 07 94 00 00 BCC FF", mk_ol(0x94, 0, b'\x00')))
cases.append(("治超 行1 变长", mk_ol(0x81, 0x03, b'\xB3\xB5\xB3\xB5')))
cases.append(("治超 80 全屏 20B", mk_ol(0x80, 0x05, b'\xC4\xFA\xBA\xC3'*5)))
cases.append(("治超 最大帧 255B(数据249)", mk_ol(0x80, 0, b'A'*249)))
cases.append(("RLS 帧头 FF FE (空)", b(0xFF,0xFE)))
cases.append(("青海 主机查询 {1 00 }", b'{1' + b(0x00) + b'}'))
cases.append(("青海 单行 {3 <16B> }", b'{3' + b(0x10) + b'AB'*8 + b'}'))

print(f"=== 场景：单帧独立判定（QH_FIX={QH_FIX}, OL_CAP={'--fix-ol' in sys.argv}）===")
for name, f in cases:
    res = [(n,l) for n,p in CHAIN if (s:=p(list(f)))[0]==READY for l in [s[1]]]
    who = res[0][0] if res else None
    print(f"  {name:40s} -> 认领={who} {res}")

print("\n=== 场景：粘包流（连续两帧）===")
streams = [
    ("ETC 心跳 x2 粘包", b(0x0A,0x50,0x0D)*2),
    ("ETC 显示 + MTC 查询 粘包", b(0x0A,0x00,0x00,0xB3,0xB5,0x0D,0x0A,0x46,0x0A)),
    ("治超 2 帧粘包", mk_ol(0x81,0,b'AB') + mk_ol(0x94,0,b'')),
    ("MTC {1} + {5} 粘包", b'{1}{5}'),
    ("MTC '{A 00 }' + '{1}' 粘包", b'{'+b'\x41\x00'+b'}' + b'{1}'),
    ("青海 '{1 00 }' + ETC 心跳", b'{1\x00}' + b(0x0A,0x50,0x0D)),
    ("治超 255B + ETC 心跳", mk_ol(0x80,0,b'A'*249) + b(0x0A,0x50,0x0D)),
]
for name, s in streams:
    consumed, rest = run(list(s))
    print(f"  {name:30s} 剩余={len(rest)}B 认领={[(n,l) for n,l,_ in consumed]}")

print("\n=== 场景：异协议帧不应被误吞（吞=被非本协议 probe READY 认领）===")
mis = [
    ("ETC 帧被 MTC/OL/RLS/QH?", b'\x0A\x00\x00\xB3\xB5\x0D', "ETC"),
    ("MTC '{A 00 }' 被 QH?", b'\x7B\x41\x00\x7D', "MTC"),
    ("MTC '{B 00 }' 被 QH?", b'\x7B\x42\x00\x7D', "MTC"),
    ("MTC '{1}' 被 QH?", b'{1}', "MTC"),
    ("MTC 0A 46 0A 被 ETC?", b(0x0A,0x46,0x0A), "MTC"),
    ("治超 帧被 RLS?", mk_ol(0x94,0,b'\x00'), "OL"),
    ("RLS FF FE 被治超?", b(0xFF,0x00,0x00,0x00,0x00,0xFE), "RLS"),
    ("青海 '{1 00 }' 被 MTC?", b'{1\x00}', "QH"),
    ("青海 '{3 16B }' 被 MTC?", b'{3'+b(0x10)+b'AB'*8+b'}', "QH"),
]
for name, f, owner in mis:
    res = [(n,l) for n,p in CHAIN if (s:=p(list(f)))[0]==READY for l in [s[1]]]
    bad = [n for n,_ in res if n != owner and owner != 'RLS']
    flag = "OK" if not bad else ("!!吞帧" if res and res[0][0]!=owner else f"??{bad}")
    print(f"  {name:28s} -> {res}  {flag}")

print("\n=== 场景：卡死检测（任何 probe WAIT 且流中断时缓冲区是否有残余可推进）===")
stall = [
    ("0xFF 0x07 垃圾(仅2B, 治超 WAIT)", b(0xFF,0x07)),
    ("0x0A 0x00 0x00 无0x0D (ETC WAIT 短流)", b(0x0A,0x00,0x00,0xB3)),
    ("'{' 'A' 0x01 残帧 (MTC/QH WAIT)", b'{',0x41,0x01),
    ("0xFF 0x10 数据不足 (治超 WAIT)", b(0xFF,0x10,0x94,0x00)),
    ("'78' 未闭合无BCC (MTC WAIT)", b'{78'+b'AB'),
]
for name, s in stall:
    consumed, rest = run(list(s))
    print(f"  {name:38s} 流结束残余={len(rest)}B（预期停等，不丢数据、不死循环）")