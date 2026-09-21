#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
YN_OL（云南治超屏协议）TCP 直连实测 —— 只读命令 '1'（主机查询）

现场问题：TCP 连接建立成功后，上位机发数据板子无反应，必须重启板子再重连。

只发只读命令：
  '1' 主机查询：请求 `7B 31 00 7D`（4B，`{` + '1' + len=0 + `}`），
                应答应为 `7B 31 01 00 7D`（5B，payload 00 = 状态正常）。
  字节来源：app_yn_ol_proto_cmd.c:261 `_yn_ol_exec_host_query`（sta[1]={0x00}）
            与 .analysis/yn_ol/yn_ol_frame_sim.py §8/§11.2 一致。
严禁发任何写命令（0x47 / '2' / 0x49 / '5' / 0x42…）。

用法（工作目录任意；日志自动追加到脚本同目录 board_tcp_test.log）：
    python3 .analysis/yn_ol/board_tcp_test.py baseline    # 步骤 0/1
    python3 .analysis/yn_ol/board_tcp_test.py repro       # 步骤 2/3（串行占位 + FIN/RST 恢复条件）
    python3 .analysis/yn_ol/board_tcp_test.py halfopen    # 步骤 4（半开连接，需 sudo iptables）
    python3 .analysis/yn_ol/board_tcp_test.py cleanup     # 兜底：清掉 yn_ol_test 防火墙规则
    python3 .analysis/yn_ol/board_tcp_test.py final       # 步骤 5（收尾基线）
"""

import datetime
import re
import select
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

BOARD = "192.168.114.200"
HOST_IP = "192.168.114.17"
IFACE = "enp193s0"
PORT_MAIN = 9528
PORT_ALT = 10028

Q1 = bytes([0x7B, 0x31, 0x00, 0x7D])           # { 1 \0 }
R1 = bytes([0x7B, 0x31, 0x01, 0x00, 0x7D])     # { 1 \x01 \x00 }
R1_TXT = " ".join(f"{b:02X}" for b in R1)
FW_COMMENT = "yn_ol_test"
HALFOPEN_LOCAL_PORT = 41200

LOG_PATH = Path(__file__).resolve().parent / "board_tcp_test.log"


def now() -> str:
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def log(tag: str, msg: str) -> None:
    line = f"{now()} [{tag}] {msg}"
    print(line, flush=True)
    try:
        with LOG_PATH.open("a", encoding="utf-8") as fh:
            fh.write(line + "\n")
    except OSError:
        pass


def banner(title: str) -> None:
    log("====", "=" * 70)
    log("====", f"{title}")
    log("====", "=" * 70)


# ---------------------------------------------------------------- 连接 / 收发

def open_conn(tag: str, port: int = PORT_MAIN, bind_port: int | None = None,
              linger: tuple | None = None, timeout: float = 4.0):
    """建一条连接；port 连接失败时自动回退 10028（最多两个候选口）。"""
    ports = [port] + ([] if port == PORT_ALT else [PORT_ALT])
    for p in ports:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        if linger is not None:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", *linger))
        if bind_port is not None:
            try:
                s.bind((HOST_IP, bind_port))
            except OSError as e:
                log(tag, f"bind {HOST_IP}:{bind_port} FAIL errno={e.errno} ({e})")
                s.close()
                return None
        t0 = time.perf_counter()
        s.settimeout(timeout)
        try:
            s.connect((BOARD, p))
        except OSError as e:
            dt = (time.perf_counter() - t0) * 1000
            log(tag, f"connect {BOARD}:{p} FAIL errno={e.errno} ({e}) dt={dt:.1f}ms")
            s.close()
            continue
        dt = (time.perf_counter() - t0) * 1000
        lip, lport = s.getsockname()
        log(tag, f"connect {BOARD}:{p} OK local={lip}:{lport} dt={dt:.1f}ms linger={linger}")
        return s
    return None


def query(s, tag: str, wait: float = 2.0, note: str = "") -> list:
    """发一次 '1' 查询，等 wait 秒收数据；返回收到的分片列表 [(ms, bytes)]。"""
    if s is None:
        log(tag, "query skipped (no socket)")
        return []
    t0 = time.perf_counter()
    try:
        s.sendall(Q1)
    except OSError as e:
        log(tag, f"send FAIL errno={e.errno} ({e}) {note}")
        return []
    log(tag, f"send {Q1.hex(' ').upper()} (4B) {note}".rstrip())
    chunks = []
    deadline = t0 + wait
    while True:
        remain = deadline - time.perf_counter()
        if remain <= 0:
            break
        try:
            r, _, _ = select.select([s], [], [], remain)
        except OSError as e:
            log(tag, f"select FAIL errno={e.errno} ({e})")
            break
        if not r:
            break
        try:
            data = s.recv(4096)
        except OSError as e:
            log(tag, f"recv FAIL errno={e.errno} ({e})")
            break
        dt = (time.perf_counter() - t0) * 1000
        if not data:
            log(tag, f"recv=0 (peer FIN) +{dt:.1f}ms")
            break
        log(tag, f"recv +{dt:.1f}ms {data.hex(' ').upper()} ({len(data)}B)")
        chunks.append((dt, data))
    if not chunks:
        log(tag, f"NO RESPONSE within {wait:.0f}s")
    return chunks


def close_conn(s, tag: str, linger: tuple | None = None) -> None:
    if s is None:
        return
    if linger is not None:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", *linger))
    try:
        lip, lport = s.getsockname()
    except OSError:
        lip, lport = "?", "?"
    s.close()
    log(tag, f"closed socket (local {lip}:{lport}) linger={linger}")


def r1_hits(chunks) -> int:
    return b"".join(d for _, d in chunks).count(R1)


def sh(cmd: list) -> str:
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
        return (p.stdout + p.stderr).strip()
    except Exception as e:  # noqa: BLE001
        return f"<cmd failed: {e}>"


# ---------------------------------------------------------------- 防火墙（步骤 4）

def ipt(*args) -> bool:
    cmd = ["sudo", "-n", "iptables", "-w"] + [str(a) for a in args]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
    log("fw", f"iptables {' '.join(str(a) for a in args)} -> rc={p.returncode}"
              f" {p.stdout.strip()} {p.stderr.strip()}".rstrip())
    return p.returncode == 0


def fw_spec_out() -> list:
    return ["-p", "tcp", "-s", HOST_IP, "--sport", HALFOPEN_LOCAL_PORT,
            "-d", BOARD, "--dport", PORT_MAIN,
            "-m", "comment", "--comment", FW_COMMENT, "-j", "DROP"]


def fw_spec_in() -> list:
    return ["-p", "tcp", "-s", BOARD, "--sport", PORT_MAIN,
            "-d", HOST_IP, "--dport", HALFOPEN_LOCAL_PORT,
            "-m", "comment", "--comment", FW_COMMENT, "-j", "DROP"]


def fw_install() -> None:
    ipt("-I", "OUTPUT", "1", *fw_spec_out())
    ipt("-I", "INPUT", "1", *fw_spec_in())
    log("fw", "installed rule summary:")
    log("fw", sh(["sudo", "-n", "iptables", "-w", "-S"]).replace("\n", " | "))


def fw_remove() -> None:
    ipt("-D", "OUTPUT", *fw_spec_out())
    ipt("-D", "INPUT", *fw_spec_in())
    rest = sh(["sudo", "-n", "iptables", "-w", "-S"])
    leftovers = [l for l in rest.splitlines() if FW_COMMENT in l]
    log("fw", f"remaining {FW_COMMENT} rules: {len(leftovers)}")


def fw_cleanup_by_number() -> None:
    """兜底：按行号删除带 comment 的规则（spec 匹配失败时用）。"""
    for chain in ("OUTPUT", "INPUT"):
        out = sh(["sudo", "-n", "iptables", "-w", "-L", chain, "--line-numbers", "-n"])
        nums = []
        for line in out.splitlines():
            if FW_COMMENT in line:
                parts = line.split()
                if parts and parts[0].isdigit():
                    nums.append(int(parts[0]))
        for n in sorted(nums, reverse=True):
            ipt("-D", chain, str(n))
    rest = sh(["sudo", "-n", "iptables", "-w", "-S"])
    log("fw", f"after number-cleanup, remaining {FW_COMMENT} rules:"
              f" {len([l for l in rest.splitlines() if FW_COMMENT in l])}")


# ---------------------------------------------------------------- 步骤 0/1

def step_baseline() -> int:
    banner("步骤 0：本机网络确认 + TCP 可达性；步骤 1：基线（单连接，'1' 查询 ×3 + 重连 ×3）")
    log("step0", f"host local addrs: {sh(['ip', '-4', 'addr', 'show', 'dev', IFACE])}".replace("\n", " | "))
    log("step0", f"route to {BOARD}: {sh(['ip', 'route', 'get', BOARD]).replace(chr(10), ' | ')}")

    s = open_conn("step1.1")
    if s is None:
        log("step1.1", "RESULT: 9528/10028 均连不上（板子不可达或端口未监听）")
        return 1
    ok = 0
    for i in range(3):
        c = query(s, f"step1.1-q{i + 1}", wait=2.0, note="(同连接第 %d 次)" % (i + 1))
        n = r1_hits(c)
        ok += 1 if n >= 1 else 0
        log("step1.1", f"q{i + 1} verdict: R1_hits={n} raw_ok={b''.join(d for _, d in c).hex(' ').upper() or '-'}")
    close_conn(s, "step1.1")
    log("step1.1", f"RESULT: 3 次查询中 {ok}/3 收到期望应答 {R1_TXT}")

    time.sleep(1.0)
    s = open_conn("step1.2")
    if s is None:
        log("step1.2", "RESULT: 重连失败")
        return 1
    ok = 0
    for i in range(3):
        c = query(s, f"step1.2-q{i + 1}", wait=2.0, note="(重连后第 %d 次)" % (i + 1))
        n = r1_hits(c)
        ok += 1 if n >= 1 else 0
    close_conn(s, "step1.2")
    log("step1.2", f"RESULT: 重连后 3 次查询中 {ok}/3 收到期望应答")
    return 0 if ok == 3 else 1


# ---------------------------------------------------------------- 步骤 2/3

def step_repro() -> int:
    banner("步骤 2：串行占位复现（A 保持打开，B/C 新连接）；步骤 3：FIN / RST 恢复条件")

    # ---- 步骤 2 ----
    a = open_conn("A")
    if a is None:
        log("A", "RESULT: 连不上，无法继续")
        return 1
    ca = query(a, "A-q1", 2.0, "(基线确认 A 被服务)")
    log("A", f"baseline R1_hits={r1_hits(ca)}")

    b = open_conn("B")
    cb1 = query(b, "B-q1", 2.0, "(A 仍打开；预期无应答)")
    log("B", f"B-q1 verdict: R1_hits={r1_hits(cb1)} (0 = 复现「连接成功但无响应」)")

    c = open_conn("C")
    cc1 = query(c, "C-q1", 2.0, "(第二条新连接；预期无应答)")
    log("C", f"C-q1 verdict: R1_hits={r1_hits(cc1)}")

    ca2 = query(a, "A-q2", 2.0, "(B/C 已被占位后，A 应仍正常)")
    log("A", f"A-q2 verdict: R1_hits={r1_hits(ca2)} (1 = A 仍被服务)")

    # ---- 步骤 3（FIN 变体）----
    t_close = time.perf_counter()
    close_conn(a, "A", linger=None)  # 默认 close = 发 FIN
    time.sleep(1.5)
    cb2 = query(b, "B-q2", 2.0, f"(A 已 FIN 关闭 +{(time.perf_counter() - t_close):.1f}s；预期恢复)")
    log("B", f"B-q2 verdict: R1_hits={r1_hits(cb2)} (>=1 = 不需要重启即恢复)")
    time.sleep(0.5)
    cc2 = query(c, "C-q2", 2.0, "(C 是否也要等 B 关闭才被服务)")
    log("C", f"C-q2 verdict: R1_hits={r1_hits(cc2)}")
    close_conn(b, "B")
    time.sleep(1.0)
    cc3 = query(c, "C-q3", 2.0, "(B 关闭后 C 应被服务)")
    log("C", f"C-q3 verdict: R1_hits={r1_hits(cc3)}")
    close_conn(c, "C")
    time.sleep(1.5)

    # ---- 步骤 3b（RST 变体：被服务的连接用 SO_LINGER(1,0) 关闭）----
    a2 = open_conn("A2")
    ca2b = query(a2, "A2-q1", 2.0, "(RST 变体：确认 A2 被服务)")
    log("A2", f"baseline R1_hits={r1_hits(ca2b)}")
    b2 = open_conn("B2")
    cb2b = query(b2, "B2-q1", 2.0, "(A2 仍打开；预期无应答)")
    log("B2", f"B2-q1 verdict: R1_hits={r1_hits(cb2b)}")
    t_close = time.perf_counter()
    close_conn(a2, "A2", linger=(1, 0))  # RST
    time.sleep(1.5)
    cb2c = query(b2, "B2-q2", 2.0, f"(A2 已 RST 关闭 +{(time.perf_counter() - t_close):.1f}s；预期恢复)")
    log("B2", f"B2-q2 verdict: R1_hits={r1_hits(cb2c)} (>=1 = RST 也能释放服务循环)")
    close_conn(b2, "B2")
    time.sleep(1.0)

    # ---- 收尾自检：新连接是否正常 ----
    z = open_conn("Z", timeout=3.0)
    cz = query(z, "Z-q1", 2.0, "(步骤 2/3 结束后的收尾基线)")
    log("Z", f"final verdict: R1_hits={r1_hits(cz)}")
    close_conn(z, "Z")
    return 0


# ---------------------------------------------------------------- 步骤 4

def step_halfopen() -> int:
    banner("步骤 4：半开连接模拟（本机与板子间该流临时 DROP，A 的 FIN 被丢）")
    fw_remove()  # 先清可能残留的本轮规则
    fw_cleanup_by_number()

    probe = subprocess.run(["sudo", "-n", "true"], capture_output=True, text=True)
    if probe.returncode != 0:
        log("step4", f"SKIP: sudo 不可用 rc={probe.returncode} {probe.stderr.strip()}")
        return 2

    a = open_conn("A", bind_port=HALFOPEN_LOCAL_PORT)
    if a is None:
        log("A", "RESULT: 连不上（端口被占或板子不可达），跳过步骤 4")
        return 1
    ca = query(a, "A-q1", 2.0, "(安装 DROP 规则前确认 A 被服务)")
    log("A", f"baseline R1_hits={r1_hits(ca)}")
    if r1_hits(ca) == 0:
        log("A", "RESULT: A 未被服务，步骤 4 前提不成立，跳过")
        close_conn(a, "A")
        return 1

    # tcpdump 抓包（记录 FIN 重传与板子最终响应，佐证恢复机制）
    cap_path = Path(__file__).resolve().parent / "halfopen_capture.txt"
    cap_fh = cap_path.open("w", encoding="utf-8")
    cap = subprocess.Popen(
        ["sudo", "-n", "tcpdump", "-i", IFACE, "-nn", "-tt", "-l",
         "tcp and host %s and port %d" % (BOARD, HALFOPEN_LOCAL_PORT)],
        stdout=cap_fh, stderr=subprocess.STDOUT, text=True)
    log("step4", f"tcpdump started (pid={cap.pid}) -> {cap_path}")

    fw_install()
    t_close = time.perf_counter()
    close_conn(a, "A", linger=None)  # FIN 将被 OUTPUT DROP 丢弃
    log("step4", "A 的 FIN 已被防火墙丢弃 → 板子侧 A 进入半开/孤儿态")

    b = open_conn("B", bind_port=HALFOPEN_LOCAL_PORT + 1)
    if b is None:
        log("B", "RESULT: B 连不上")
        fw_remove()
        return 1

    t0 = time.perf_counter()
    drop_hits = 0
    it = 0
    while time.perf_counter() - t0 < 30.0:
        it += 1
        cb = query(b, f"B-drop{it}", 2.0,
                   f"(半开窗口 +{time.perf_counter() - t0:.0f}s / A 关闭 +{time.perf_counter() - t_close:.0f}s；预期无应答)")
        drop_hits += r1_hits(cb)
        time.sleep(3.0)
    log("B", f"半开窗口结论：30s 内 {it} 次查询共收到 R1_hits={drop_hits}（0 = 半开期间完全无反应）")

    # ---- 拆除防火墙，观察恢复（靠 FIN 重传送达）----
    fw_remove()
    t_rm = time.perf_counter()
    rec = None
    it = 0
    while time.perf_counter() - t_rm < 150.0:
        it += 1
        cb = query(b, f"B-rec{it}", 2.0,
                   f"(规则已拆除 +{time.perf_counter() - t_rm:.0f}s；预期由 FIN 重传送达后自愈)")
        if r1_hits(cb) > 0:
            rec = time.perf_counter() - t_rm
            log("B", f"RECOVERED: 规则拆除后 {rec:.1f}s 收到应答（无需重启板子）")
            break
        time.sleep(1.0)
    if rec is None:
        log("B", "NOT RECOVERED within 150s (FIN 重传已耗尽) —— 需人工介入（重启板子）")

    close_conn(b, "B")
    time.sleep(1.0)
    z = open_conn("Z", timeout=3.0)
    cz = query(z, "Z-q1", 2.0, "(步骤 4 收尾基线)")
    log("Z", f"final verdict: R1_hits={r1_hits(cz)} (>=1 = 板子恢复服务)")
    close_conn(z, "Z")

    try:
        cap.terminate()
        cap.wait(timeout=5)
    except Exception:  # noqa: BLE001
        pass
    cap_fh.close()
    text = cap_path.read_text(encoding="utf-8", errors="replace")
    fins = [l for l in text.splitlines() if "Flags [F" in l or "Flags [FP" in l]
    rsts = [l for l in text.splitlines() if "Flags [R" in l]
    acks = [l for l in text.splitlines() if BOARD in l and "ack" in l]
    log("cap", f"packets={len(text.splitlines())} FIN={len(fins)} RST={len(rsts)}")
    for l in fins:
        log("cap", f"  FIN: {l}")
    for l in rsts[:10]:
        log("cap", f"  RST: {l}")
    for l in acks[-5:]:
        log("cap", f"  last-ack: {l}")
    return 0


def step_burst() -> int:
    """受控复现：A 被服务时在 B 上积压 6 条查询，A 关闭后集中爆发处理；随后监控板子健康度。

    目的：刻画「半开/占位恢复瞬间的集中帧爆发」是否会让板子短暂/长期停止服务新连接。
    """
    banner("附加实验：B 积压 6 条查询 → A 关闭后集中处理 → 60s 健康监控（TCP + ICMP）")

    a = open_conn("A")
    if a is None:
        return 1
    ca = query(a, "A-q1", 2.0, "(基线确认 A 被服务)")
    log("A", f"baseline R1_hits={r1_hits(ca)}")

    b = open_conn("B")
    log("B", "积压 6 条 '1' 查询（每条等 0.3s，预期均无应答）")
    queued = 0
    for i in range(6):
        cb = query(b, f"B-queue{i + 1}", 0.3, "(积压)")
        queued += r1_hits(cb)
        time.sleep(0.3)
    log("B", f"积压阶段共收到应答 {queued}（预期 0）")

    log("burst", "保持 A/B 打开 30s（模拟半开占位窗口）")
    t_hold = time.perf_counter()
    while time.perf_counter() - t_hold < 30.0:
        time.sleep(5.0)
        log("burst", f"hold +{time.perf_counter() - t_hold:.0f}s")

    t_close = time.perf_counter()
    close_conn(a, "A", linger=None)
    burst = []
    deadline = time.perf_counter() + 30.0
    first_at = None
    while time.perf_counter() < deadline:
        cb = query(b, "B-after", 2.0, f"(A 关闭 +{time.perf_counter() - t_close:.1f}s；等集中应答)")
        n = r1_hits(cb)
        if n:
            if first_at is None:
                first_at = time.perf_counter() - t_close
            burst.append(n)
        if n:
            time.sleep(0.5)
            if len(burst) >= 3:
                break
    log("B", f"集中应答：首个 +{('%.1f' % first_at) if first_at else '-'}s，轮次 {burst}，"
             f"合计 {sum(burst)} 帧（积压 6 条）")
    close_conn(b, "B")
    time.sleep(1.0)

    # ---- 健康监控：新连接 × N（连接 + 查询 + 关闭），同时 ICMP 探活 ----
    log("mon", "健康监控开始：每 2s 一轮 {connect → '1' 查询 → close} + ping")
    t0 = time.perf_counter()
    bad = []
    for i in range(20):
        t = time.perf_counter() - t0
        s = open_conn("mon", timeout=2.5)
        hit = 0
        if s is not None:
            cm = query(s, "mon", 1.5)
            hit = r1_hits(cm)
            close_conn(s, "mon")
        prc = subprocess.run(["ping", "-c", "1", "-W", "1", BOARD],
                             capture_output=True, text=True).returncode
        log("mon", f"+{t:.1f}s iter{i + 1}: tcp_hit={hit} ping={'OK' if prc == 0 else 'FAIL'}")
        if s is None or hit == 0 or prc != 0:
            bad.append((round(t, 1), s is not None, hit, prc == 0))
        time.sleep(2.0)
    log("mon", f"异常轮次（t, tcp_connect_ok, hits, ping_ok）= {bad if bad else '无'}")
    return 0 if not bad else 1


def step_churn() -> int:
    """连接抖动实验：连续 20 轮 {connect → '1' 查询 → close}，全程抓包。

    目的：区分「集中帧爆发」与「连接建立/关闭本身的抖动」哪一个是板子停止服务的触发条件，
    并用抓包（ARP/ICMP/TCP 全量）确认停止服务期间板子是否发生了重启（IWDG ≈33s）。
    """
    banner("附加实验：连接抖动 20 轮（无积压/无并发占位），全程抓包")
    cap_path = Path(__file__).resolve().parent / "churn_capture.txt"
    cap_fh = cap_path.open("w", encoding="utf-8")
    cap = subprocess.Popen(
        ["sudo", "-n", "tcpdump", "-i", IFACE, "-nn", "-tt", "-l", "host", BOARD],
        stdout=cap_fh, stderr=subprocess.STDOUT, text=True)
    log("churn", f"tcpdump started (pid={cap.pid}) -> {cap_path}")

    t0 = time.perf_counter()
    bad = []
    try:
        for i in range(20):
            t = time.perf_counter() - t0
            s = open_conn("churn", timeout=2.5)
            hit = 0
            if s is not None:
                hit = r1_hits(query(s, "churn", 1.5))
                close_conn(s, "churn")
            prc = subprocess.run(["ping", "-c", "1", "-W", "1", BOARD],
                                 capture_output=True, text=True).returncode
            log("churn", f"+{t:.1f}s cycle{i + 1}: tcp_connect={'OK' if s is not None else 'FAIL'}"
                         f" tcp_hit={hit} ping={'OK' if prc == 0 else 'FAIL'}")
            if s is None or hit == 0 or prc != 0:
                bad.append((round(t, 1), s is not None, hit, prc == 0))
            time.sleep(1.5)
    finally:
        try:
            cap.terminate()
            cap.wait(timeout=5)
        except Exception:  # noqa: BLE001
            pass
        cap_fh.close()

    log("churn", f"异常轮次（t, tcp_connect_ok, hits, ping_ok）= {bad if bad else '无'}")
    text = cap_path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    arp = [l for l in lines if "ARP" in l or "arp" in l]
    icmp = [l for l in lines if "ICMP" in l]
    rst = [l for l in lines if "Flags [R" in l]
    syn_no_answer = [l for l in lines if "Flags [S]" in l]
    log("cap", f"capture lines={len(lines)} ARP={len(arp)} ICMP={len(icmp)} RST={len(rst)} SYN={len(syn_no_answer)}")
    for l in arp[:20]:
        log("cap", f"  ARP: {l}")
    for l in lines:
        if "Flags [S]" in l and BOARD in l.split(">")[0]:
            log("cap", f"  SYN(host->board): {l}")
    for l in rst[:20]:
        log("cap", f"  RST: {l}")
    return 0 if not bad else 1


def step_watch() -> int:
    """被动观察 240s：保持单连接不断开（每 10s 查询一次）+ 每 2s ping，全程抓包。

    目的：判断板子的「完全停止服务（TCP+ICMP 双死）→ ~33s 后自愈」是否会在**没有连接抖动**时
    自发出现（即周期性缺陷），并抓取故障前后与重启瞬间的报文（ARP/ICMP/TCP）。
    """
    banner("附加实验：240s 被动观察（保持 1 条连接 + ping + 抓包）")
    s = open_conn("watch", timeout=3.0)
    if s is None:
        log("watch", "RESULT: 起始连接失败")
        return 1

    cap_path = Path(__file__).resolve().parent / "watch_capture.txt"
    cap_fh = cap_path.open("w", encoding="utf-8")
    cap = subprocess.Popen(
        ["sudo", "-n", "tcpdump", "-i", IFACE, "-nn", "-tt", "-l", "host", BOARD],
        stdout=cap_fh, stderr=subprocess.STDOUT, text=True)
    log("watch", f"tcpdump started (pid={cap.pid}) -> {cap_path}")

    t0 = time.perf_counter()
    t_query = 0.0
    t_ping = 0.0
    dead_since = None
    events = []
    try:
        while time.perf_counter() - t0 < 240.0:
            now = time.perf_counter()
            if now - t_ping >= 2.0:
                t_ping = now
                prc = subprocess.run(["ping", "-c", "1", "-W", "1", BOARD],
                                     capture_output=True, text=True).returncode
                if prc != 0 and dead_since is None:
                    dead_since = now
                    events.append((round(now - t0, 1), "PING-FAIL-START"))
                    log("watch", f"+{now - t0:.1f}s PING FAIL（板子 ICMP 无响应）")
                elif prc == 0 and dead_since is not None:
                    events.append((round(now - t0, 1), f"PING-OK-AFTER-{now - dead_since:.1f}s"))
                    log("watch", f"+{now - t0:.1f}s PING OK（中断 {now - dead_since:.1f}s）")
                    dead_since = None
            if now - t_query >= 10.0:
                t_query = now
                hit = r1_hits(query(s, "watch", 2.0, f"(+{now - t0:.0f}s)"))
                if hit == 0:
                    log("watch", f"+{now - t0:.1f}s TCP 查询无应答")
            time.sleep(0.2)
    finally:
        close_conn(s, "watch")
        try:
            cap.terminate()
            cap.wait(timeout=5)
        except Exception:  # noqa: BLE001
            pass
        cap_fh.close()

    log("watch", f"事件表 = {events if events else '无异常'}")
    text = cap_path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    arp_req = [l for l in lines if "ARP, Request" in l]
    icmp = [l for l in lines if "ICMP" in l]
    tcp = [l for l in lines if "Flags [" in l]
    log("cap", f"lines={len(lines)} ARP_request={len(arp_req)} ICMP={len(icmp)} TCP={len(tcp)}")
    if arp_req:
        log("cap", f"ARP 目标统计（前 3 个不同目标）：")
        seen = {}
        for l in arp_req:
            parts = l.split("who-has")
            tgt = parts[1].split()[0] if len(parts) > 1 else "?"
            seen[tgt] = seen.get(tgt, 0) + 1
        for k, v in list(seen.items())[:3]:
            log("cap", f"  who-has {k}: {v} 次")
    # 打印非 ARP 的报文（TCP/ICMP/其他），便于看故障边界
    for l in lines:
        if "ARP," not in l:
            log("cap", f"  {l}")
    return 0 if not events else 1


def step_liveness() -> int:
    """纯 liveness 观察 300s：**不发任何 TCP 报文**，只 ping + 抓板子自身的 ARP 心跳。

    板子有持续 1Hz「who-has 网关」的 ARP 请求，用它当心跳可精确判定板子自身死/活窗口，
    不依赖本机 ARP 缓存；再与 ping 结果对照，区分「板子挂了」与「本机网络问题」。
    """
    banner("附加实验：300s 纯 liveness（无 TCP 流量；板子 1Hz ARP 心跳 + ping）")
    mac = sh(["ip", "neigh", "show", BOARD]).split()
    board_mac = mac[mac.index("lladdr") + 1] if "lladdr" in mac else None
    log("live", f"board mac from neigh = {board_mac}")

    cap_path = Path(__file__).resolve().parent / "liveness_capture.txt"
    cap_fh = cap_path.open("w", encoding="utf-8")
    flt = ["ether", "src", board_mac, "and", "arp"] if board_mac else ["arp"]
    cap = subprocess.Popen(["sudo", "-n", "tcpdump", "-i", IFACE, "-nn", "-tt", "-l"] + flt,
                           stdout=cap_fh, stderr=subprocess.STDOUT, text=True)
    log("live", f"tcpdump started (pid={cap.pid}) filter={' '.join(flt)} -> {cap_path}")

    t0 = time.perf_counter()
    fails = []
    try:
        while time.perf_counter() - t0 < 300.0:
            prc = subprocess.run(["ping", "-c", "1", "-W", "1", BOARD],
                                 capture_output=True, text=True).returncode
            if prc != 0:
                fails.append(round(time.perf_counter() - t0, 1))
                log("live", f"+{time.perf_counter() - t0:.1f}s PING FAIL")
            time.sleep(1.0)
    finally:
        try:
            cap.terminate()
            cap.wait(timeout=5)
        except Exception:  # noqa: BLE001
            pass
        cap_fh.close()

    # 合并连续的 ping 失败为中断窗口
    wins = []
    for t in fails:
        if wins and t - wins[-1][1] <= 2.5:
            wins[-1][1] = t
        else:
            wins.append([t, t])
    log("live", f"ping 失败 {len(fails)} 次；中断窗口 = {[(a, b, round(b - a, 1)) for a, b in wins]}")

    lines = [l for l in cap_path.read_text(errors="replace").splitlines() if re.match(r"^\d+\.\d+ ", l)]
    ts = [float(l.split()[0]) for l in lines]
    gaps = []
    for a, b in zip(ts, ts[1:]):
        if b - a > 3.0:
            gaps.append((a, b, b - a))
    log("live", f"板子 ARP 心跳：{len(ts)} 包，{ts[0]:.1f}~{ts[-1]:.1f}，"
                f"间隔>3s 的空窗 = {[(round(a, 1), round(b, 1), round(g, 1)) for a, b, g in gaps]}")
    return 0 if not gaps else 1


def step_hold() -> int:
    """假设验证：**保持一条已建立连接 150s**（尽量少发数据，只每 10s 一次 '1' + 每 2s ping），
    观察板子是否在「连接建立后 ~55~60s」整体死亡（TCP+ICMP 双死）→ ~30s 后（IWDG）自愈。

    三次历史故障（09:34:56 / 09:38:30 / 09:43:08）均发生在某条连接建立后 53~61s。
    """
    banner("附加实验：长连接假设验证（保持 1 条连接 150s，观察是否 ~60s 后板子整体死亡）")
    t0 = time.perf_counter()
    s = open_conn("hold", timeout=3.0)
    if s is None:
        log("hold", "RESULT: 起始连接失败")
        return 1
    log("hold", "连接已建立，开始计时（每 2s ping；每 10s '1' 查询）")

    t_ping = 0.0
    t_query = 0.0
    first_fail = None
    back_at = None
    queries_ok = 0
    dead = False
    while time.perf_counter() - t0 < 150.0:
        now = time.perf_counter()
        if now - t_ping >= 2.0:
            t_ping = now
            prc = subprocess.run(["ping", "-c", "1", "-W", "1", BOARD],
                                 capture_output=True, text=True).returncode
            if prc != 0 and first_fail is None:
                first_fail = now - t0
                dead = True
                log("hold", f"+{now - t0:.1f}s 板子 ICMP 首次无响应（连接建立后 {first_fail:.1f}s）")
            if prc == 0 and dead and back_at is None:
                back_at = now - t0
                log("hold", f"+{now - t0:.1f}s 板子 ICMP 恢复（中断 {back_at - first_fail:.1f}s）")
        if now - t_query >= 10.0:
            t_query = now
            hit = r1_hits(query(s, "hold", 2.0, f"(+{now - t0:.0f}s)"))
            if hit:
                queries_ok += 1
            if hit == 0 and first_fail is None:
                log("hold", f"+{now - t0:.1f}s TCP 查询无应答（但 ping 尚存活？）")
        time.sleep(0.2)
    log("hold", f"结果：queries_ok={queries_ok} 首次 ping 失败={first_fail} 恢复={back_at}")
    close_conn(s, "hold")
    return 0


def step_hold2() -> int:
    """对照实验：保持一条已建立连接但**一个字节都不发**（只用外部 ping 探活）150s。

    与 step_hold（每 10s 查询一次，崩溃于 +42s）对照：
      · 若本实验也崩 → 触发条件 = 「连接被 accept 并保持」本身；
      · 若本实验不崩 → 触发条件 = 连接上的查询/应答流。
    """
    banner("附加实验：静默长连接对照（不发送任何 TCP 数据，仅外部 ping，150s）")
    t0 = time.perf_counter()
    s = open_conn("hold2", timeout=3.0)
    if s is None:
        log("hold2", "RESULT: 起始连接失败")
        return 1
    log("hold2", "连接已建立；本实验不发送任何 TCP 数据（板子侧 recv 阻塞等待）")

    t_ping = 0.0
    first_fail = None
    back_at = None
    while time.perf_counter() - t0 < 150.0:
        now = time.perf_counter()
        if now - t_ping >= 2.0:
            t_ping = now
            prc = subprocess.run(["ping", "-c", "1", "-W", "1", BOARD],
                                 capture_output=True, text=True).returncode
            if prc != 0 and first_fail is None:
                first_fail = now - t0
                log("hold2", f"+{now - t0:.1f}s 板子 ICMP 首次无响应（连接静默保持 {first_fail:.1f}s）")
            if prc == 0 and first_fail is not None and back_at is None:
                back_at = now - t0
                log("hold2", f"+{now - t0:.1f}s 板子 ICMP 恢复（中断 {back_at - first_fail:.1f}s）")
        time.sleep(0.2)
    log("hold2", f"结果：首次 ping 失败={first_fail} 恢复={back_at}（150s 内{'未' if first_fail is None else ''}出现中断）")
    close_conn(s, "hold2")
    return 0


def step_quick5() -> int:
    """对照实验：同一条连接上**快速连发 5 次 '1'**（≈5 个应答，连接总寿命 <3s）→ 观察 90s。

    与 step_hold（连接保持 42s、5 个应答 → 崩）对照：区分「应答个数」与「连接寿命」。
    """
    banner("附加实验：单连接快速 5 连发（短寿命）对照")
    t0 = time.perf_counter()
    s = open_conn("quick5", timeout=3.0)
    if s is None:
        return 1
    hits = 0
    for i in range(5):
        hits += r1_hits(query(s, f"quick5-q{i + 1}", 0.8, "(快速连发)"))
    close_conn(s, "quick5")
    log("quick5", f"5 次查询共收到 {hits} 个应答；连接寿命 {(time.perf_counter() - t0):.1f}s")

    t_start = time.perf_counter()
    fail = None
    back = None
    while time.perf_counter() - t_start < 90.0:
        prc = subprocess.run(["ping", "-c", "1", "-W", "1", BOARD],
                             capture_output=True, text=True).returncode
        if prc != 0 and fail is None:
            fail = time.perf_counter() - t_start
            log("quick5", f"+{fail:.1f}s（连接关闭后）板子 ICMP 无响应")
        if prc == 0 and fail is not None and back is None:
            back = time.perf_counter() - t_start
            log("quick5", f"+{back:.1f}s 恢复（中断 {back - fail:.1f}s）")
        time.sleep(1.0)
    log("quick5", f"结果：90s 内{'崩溃于 +%.1fs' % fail if fail else '未出现中断'}")
    return 0


def step_cleanup() -> int:
    banner("兜底清理：移除 yn_ol_test 防火墙规则")
    fw_remove()
    fw_cleanup_by_number()
    return 0


def step_final() -> int:
    banner("步骤 5：收尾基线（清空连接后重新验证单连接 '1' 查询）")
    s = open_conn("final")
    if s is None:
        return 1
    ok = 0
    for i in range(3):
        c = query(s, f"final-q{i + 1}", 2.0)
        n = r1_hits(c)
        ok += 1 if n >= 1 else 0
    close_conn(s, "final")
    log("final", f"RESULT: {ok}/3 收到 {R1_TXT}；raw=<见上>")
    st = sh(["sudo", "-n", "iptables", "-w", "-S"])
    log("final", f"leftover {FW_COMMENT} rules: {len([l for l in st.splitlines() if FW_COMMENT in l])}")
    return 0 if ok == 3 else 1


def main() -> int:
    step = sys.argv[1] if len(sys.argv) > 1 else "baseline"
    log("====", f"run step={step} (pid={__import__('os').getpid()})")
    table = {
        "baseline": step_baseline,
        "repro": step_repro,
        "halfopen": step_halfopen,
        "burst": step_burst,
        "churn": step_churn,
        "watch": step_watch,
        "liveness": step_liveness,
        "hold": step_hold,
        "hold2": step_hold2,
        "quick5": step_quick5,
        "cleanup": step_cleanup,
        "final": step_final,
    }
    if step not in table:
        log("====", f"unknown step {step}; use one of {list(table)}")
        return 2
    try:
        return table[step]()
    except KeyboardInterrupt:
        log("====", "interrupted")
        return 130
    finally:
        if step == "halfopen":
            st = sh(["sudo", "-n", "iptables", "-w", "-S"])
            if any(FW_COMMENT in l for l in st.splitlines()):
                log("====", "WARNING: 残留规则，执行兜底清理")
                fw_cleanup_by_number()


if __name__ == "__main__":
    sys.exit(main())
