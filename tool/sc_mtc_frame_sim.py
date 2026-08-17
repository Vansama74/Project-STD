#!/usr/bin/env python3
"""宿主推演：四川 MTC（1E 方案二）'{' 帧族 probe/parse 仿真。

背景（乱码 bug）：上位机发 `{3 1 1234 } 3 }`（10B，'}' 定界变长），
旧 probe 按「无 BCC 20B / 带 BCC 21B」固定双长度匹配 → 8B 帧不够 20B →
WAIT 累积 → 多帧拼凑后按 20B 认领跨帧垃圾 → 渲染乱码。

本脚本镜像固件 app_sc_mtc_proto.c / app_sc_mtc_proto_parse.c 的
旧逻辑（--old）与修复后逻辑（默认），并镜像 QH probe 验证链式互斥。

用法：
  python3 tool/sc_mtc_frame_sim.py        # 修复后 probe/parse 推演
  python3 tool/sc_mtc_frame_sim.py --old  # 修复前旧逻辑复现乱码
"""
import sys

FAKE, WAIT, READY = 0, 1, 2
OLD = '--old' in sys.argv
SC_MTC_PAYLOAD_MAX = 74


def bcc_calc(b, ln):
    x = 0
    for i in range(1, ln - 2):
        x ^= b[i]
    return x & 0xFF


# ---------------- 修复前：旧 probe（固定双长度 + '78' BCC 变长） ----------------
def mtc_probe_old(buf):
    avail = len(buf)
    if avail == 0:
        return (FAKE, 0)
    b0 = buf[0]
    if b0 == 0x0A:
        if avail < 3:
            return (WAIT, 0)
        if buf[1] == 0x46 and buf[2] in (0x0A, 0x0D):
            return (READY, 3)
        return (FAKE, 0)
    if b0 != 0x7B:
        return (FAKE, 0)
    if avail < 2:
        return (WAIT, 0)
    c = buf[1]
    if 0x40 <= c <= 0x45 and c != 0x41:
        fl = 6 if c == 0x40 else (3 if c == 0x45 else 4)
        if avail < fl:
            return (WAIT, 0)
        return (READY, fl) if buf[fl - 1] == 0x7D else (FAKE, 0)
    if not (0x31 <= c <= 0x39 or c == 0x41):
        return (FAKE, 0)
    if c == 0x41:
        if avail < 4:
            return (WAIT, 0)
        if buf[3] == 0x7D and buf[2] <= 2:
            return (READY, 4)
        if avail >= 5:
            return (READY, 5) if buf[4] == 0x7D else (FAKE, 0)
        return (WAIT, 0)
    varlen = False
    if c == 0x36:
        if avail < 3:
            return (WAIT, 0)
        t = buf[2]
        if t == 0x30:
            lo, hi = 15, 16
        elif t == 0x31:
            lo, hi = 24, 25
        else:
            return (FAKE, 0)
    elif c == 0x37:
        if avail < 3:
            return (WAIT, 0)
        v = buf[2]
        if 0x30 <= v <= 0x37:
            lo, hi = 4, 5
        elif v == 0x38:
            varlen = True
            lo = hi = 0
        else:
            return (FAKE, 0)
    else:
        lo, hi = {0x31: (3, 4), 0x32: (3, 4), 0x35: (3, 4), 0x33: (20, 21),
                  0x34: (67, 68), 0x38: (4, 5), 0x39: (4, 5)}[c]
    if varlen:
        max_scan = min(avail, SC_MTC_PAYLOAD_MAX)
        for e in range(5, max_scan):
            if buf[e] != 0x7D:
                continue
            ln = e + 1
            if bcc_calc(buf[:ln], ln) == buf[ln - 2]:
                return (READY, ln)
        if avail >= SC_MTC_PAYLOAD_MAX:
            return (FAKE, 0)
        return (WAIT, 0)
    if avail >= lo and buf[lo - 1] == 0x7D:
        return (READY, lo)
    if avail >= hi:
        return (READY, hi) if buf[hi - 1] == 0x7D else (FAKE, 0)
    return (WAIT, 0)


# ---------------- 修复后：新 probe（'}' 定界变长扫描） ----------------
def mtc_probe_new(buf):
    avail = len(buf)
    if avail == 0:
        return (FAKE, 0)
    b0 = buf[0]
    if b0 == 0x0A:                      # 0A 46 0A / 0A 46 0D 不变
        if avail < 3:
            return (WAIT, 0)
        if buf[1] == 0x46 and buf[2] in (0x0A, 0x0D):
            return (READY, 3)
        return (FAKE, 0)
    if b0 != 0x7B:                      # 首字节快拒
        return (FAKE, 0)
    if avail < 2:
        return (WAIT, 0)
    c = buf[1]
    # '1'~'9','A' + 7B 40~45 原始帧族：全部 '}' 定界
    if not (0x31 <= c <= 0x39 or c == 0x41 or 0x40 <= c <= 0x45):
        return (FAKE, 0)
    lim = min(avail, SC_MTC_PAYLOAD_MAX)      # 上限 74B（队列 payload 约束）
    for e in range(2, lim):
        if buf[e] == 0x7D:
            return (READY, e + 1)
    if avail >= SC_MTC_PAYLOAD_MAX:
        return (FAKE, 0)                      # 超限无 '}'：畸形帧，逐字节重同步
    return (WAIT, 0)                          # 帧跨 DMA 块：停等更多字节


# ---------------- 修复后：parse 镜像（app_sc_mtc_proto_parse.c） ----------------
def parse_brace(buf, ln):
    """返回 (cmd, sta, 字段摘要)。镜像修复后 parse 逻辑。"""
    if ln < 3 or buf[0] != 0x7B or buf[ln - 1] != 0x7D:
        return ('INVALID', 'ERR_FRAME', None)
    c = buf[1]
    if c == 0x41:                       # 0x41 双义：4B=点阵大小 / 5B=颜色
        if ln == 4:
            return ('RAW_DOT_SIZE', 'OK' if buf[2] <= 2 else 'ERR_PARAM', buf[2])
        if ln == 5:
            v = buf[2] - 0x30
            return ('COLOR', 'OK' if 0x31 <= buf[2] <= 0x33 else 'ERR_PARAM', v)
        return ('INVALID', 'ERR_FRAME', None)
    if 0x40 <= c <= 0x45:               # 原始帧族长度校验不变
        lens = {0x40: 6, 0x42: 4, 0x43: 4, 0x44: 4, 0x45: 3}
        if ln != lens[c]:
            return ('RAW', 'ERR_FRAME', None)
        return ('RAW', 'OK', None)
    if c == 0x31 or c == 0x32 or c == 0x35:        # '1','2','5'
        return ('INIT/SELF/CLEAR', 'OK' if ln in (3, 4) else 'ERR_PARAM', None)
    if c == 0x33:                       # '{3' 单行：行号[2] + 变长文本[3..]
        if ln < 4:
            return ('ONE_LINE', 'ERR_PARAM', None)
        row = buf[2]
        if not (0x31 <= row <= 0x36):
            return ('ONE_LINE', 'ERR_PARAM', None)
        return ('ONE_LINE', 'OK', ('row=%d' % (row - 0x30), buf[3:ln - 1]))
    if c == 0x34:                       # '{4' 全屏：变长文本[2..]
        return ('FULL_SCREEN', 'OK', buf[2:ln - 1])
    if c == 0x36:                       # '{6' 固定格式：type[2] + 变长数字串
        if ln < 4:
            return ('FIXED', 'ERR_PARAM', None)
        t = buf[2]
        dlen = ln - 4
        if t == 0x30:
            return ('FIXED', 'OK' if dlen >= 11 else 'ERR_PARAM', ('客车', dlen))
        if t == 0x31:
            return ('FIXED', 'OK' if dlen >= 20 else 'ERR_PARAM', ('货车', dlen))
        return ('FIXED', 'ERR_PARAM', None)
    if c == 0x37:                       # '{7'：'0'~'7' 固定 / '8' 自定义文本
        if ln < 4:
            return ('VOICE', 'ERR_PARAM', None)
        v = buf[2]
        if 0x30 <= v <= 0x37:
            return ('VOICE', 'OK', 'idx=%d' % (v - 0x30))
        if v == 0x38:
            return ('VOICE', 'OK' if ln > 4 else 'ERR_PARAM', ('custom', buf[3:ln - 1]))
        return ('VOICE', 'ERR_PARAM', None)
    if c == 0x38:                       # '{8' 亮度：二进制 0~8 或 ASCII '0'~'8'
        if ln < 4:
            return ('BRIGHTNESS', 'ERR_PARAM', None)
        v = buf[2]
        if v <= 8 or 0x30 <= v <= 0x38:
            return ('BRIGHTNESS', 'OK', v if v <= 8 else v - 0x30)
        return ('BRIGHTNESS', 'ERR_PARAM', None)
    if c == 0x39:                       # '{9' 音量 '1'~'5'
        if ln < 4:
            return ('VOLUME', 'ERR_PARAM', None)
        return ('VOLUME', 'OK' if 0x31 <= buf[2] <= 0x35 else 'ERR_PARAM', buf[2] - 0x30)
    return ('INVALID', 'ERR_CMD', None)


def qh_probe(buf):
    avail = len(buf)
    if avail == 0:
        return (FAKE, 0)
    if buf[0] != 0x7B:
        return (FAKE, 0)
    if avail < 3:
        return (WAIT, 0)
    c = buf[1]
    if not (0x31 <= c <= 0x39 or c in (0x41, 0x42)):
        return (FAKE, 0)
    dl = buf[2]
    fl = dl + 4
    if avail < fl:
        return (WAIT, 0)
    return (READY, fl) if buf[fl - 1] == 0x7D else (FAKE, 0)


def run_chain(stream, mtc_probe):
    """镜像 frame_dispatch_task 内层循环：QH 先于 MTC（qh_proto_init < sc_mtc_proto_init）。"""
    buf = []
    consumed = []
    idx = 0
    while True:
        if not buf and idx < len(stream):
            buf.append(stream[idx]); idx += 1
            continue
        if not buf:
            break
        any_parsed = any_wait = any_fake = False
        for name, p in (('QH', qh_probe), ('MTC', mtc_probe)):
            st, ln = p(buf)
            if st == READY:
                consumed.append((name, ln, bytes(buf[:ln])))
                buf = buf[ln:]
                any_parsed = True
                break
            elif st == WAIT:
                any_wait = True
            else:
                any_fake = True
        if any_parsed:
            continue
        if any_wait:
            if idx >= len(stream):
                break
            buf.append(stream[idx]); idx += 1
            continue
        if any_fake:
            buf = buf[1:]
            continue
        break
    return consumed, bytes(buf)


def show(name, data, mtc_probe):
    print('--- %s' % name)
    print('    输入: %s (%dB)' % (data.hex(' '), len(data)))
    consumed, rest = run_chain(list(data), mtc_probe)
    if not consumed:
        print('    probe: 无帧被认领（WAIT 停等，剩余 %dB 滞留）' % len(rest))
        return
    for who, ln, fr in consumed:
        print('    %s 认领 %dB: %s' % (who, ln, fr.hex(' ')))
        if who == 'MTC' and fr[0] == 0x7B:
            cmd, sta, info = parse_brace(list(fr), ln)
            print('      parse: cmd=%s sta=%s %s' % (cmd, sta, info))
    if rest:
        print('    残余 %dB: %s' % (len(rest), rest.hex(' ')))


def b(*xs):
    return bytes(xs)


print('=== MTC \'{\' 帧族推演（%s）===\n' % ('旧：固定双长度' if OLD else '新：\'}\' 定界变长'))
mtc_probe = mtc_probe_old if OLD else mtc_probe_new

# 1. 用户实测帧（单条 10B）
show('用户帧单条 {3 1 1234 } 3 }', b(0x7B, 0x33, 0x31, 0x31, 0x32, 0x33, 0x34, 0x7D, 0x33, 0x7D), mtc_probe)
# 2. 用户帧连发 ×2（模拟工具连续发送）
show('用户帧连发 ×2（粘包 20B）',
     b(0x7B, 0x33, 0x31, 0x31, 0x32, 0x33, 0x34, 0x7D, 0x33, 0x7D) * 2, mtc_probe)
# 3. 标准 20B '{3}' 帧（行1 + 16B 文本）
show('标准 20B {3 1 <16B> }', b'{31' + b'ABCDEFGHIJKLMNOP' + b'}', mtc_probe)
# 4. '{4}' 全屏变长（20B 文本）
show("{4 全屏变长 20B 文本}", b'{4' + b'GHIJKLMNOPQRSTUVWXYZAB' + b'}', mtc_probe)
# 5. 短帧 '{5}' / '{1}' / '{2}'
show('短帧 {5}{1}{2} 连发', b'{5}{1}{2}', mtc_probe)
# 6. '{3}' 带 BCC 变体（BCC=0x70，参考语义：BCC 字节视为文本尾部）
show('{3 1 1234 BCC(0x70) }', b'{31' + b'1234' + b'\x70}', mtc_probe)
# 7. 跨帧连续两帧
show('跨帧 {3 1 12 } + {3 2 ABCD }', b'{31' + b'12' + b'}' + b'{32' + b'ABCD' + b'}', mtc_probe)
# 8. '{6}' 客车 15B / 货车 24B
show('{6 0 客车 11 位数字 }', b'{60' + b'01234567890' + b'}', mtc_probe)
show('{6 1 货车 20 位数字 }', b'{61' + b'01234567890123456789' + b'}', mtc_probe)
# 9. '{7}' 固定语音 / '{78}' 自定义文本（无 BCC）
show("{7 3 }", b'{7' + b'3}', mtc_probe)
show('{78 AB }', b'{78' + b'AB' + b'}', mtc_probe)
# 10. '{8}' 亮度二进制 / '{9}' 音量
show('{8 0x05 }', b'{8' + b'\x05}', mtc_probe)
show('{9 3 }', b'{9' + b'3}', mtc_probe)
# 11. '{A 0x01 }' 点阵大小 / '{A 1 0x70 }' 颜色
show('{A 0x01 }', b'{' + b'\x41\x01' + b'}', mtc_probe)
show('{A 1 0x70 }', b'{' + b'\x41\x31\x70' + b'}', mtc_probe)
# 12. 青海 '{3 0x10 <16B> }' 不应被 MTC 吞（QH 先认领）
show('青海 {3 0x10 <16B> }（QH 优先）', b'{3' + b'\x10' + b'AB' * 8 + b'}', mtc_probe)
# 13. 未闭合 '{' 帧：停等不丢数据
show("未闭合 {3 1 1234（截断）", b(0x7B, 0x33, 0x31, 0x31, 0x32, 0x33, 0x34), mtc_probe)