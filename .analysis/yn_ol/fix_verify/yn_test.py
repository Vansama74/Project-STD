#!/usr/bin/env python3
"""YN_OL TCP 现场问题复现/验证脚本（参数化：host/port 可指定）

只发只读命令 `'1'`（主机查询，期望应答 7B 31 01 00 7D）；不发任何写命令
（0x47 / 0x49 / '2' / '5' / 0x42… 一律不发）。
"""
import argparse
import datetime
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
LOG = HERE / "yn_test.log"

Q1 = bytes([0x7B, 0x31, 0x00, 0x7D])          # { '1' len=0 }  = 主机查询
R1 = bytes([0x7B, 0x31, 0x01, 0x00, 0x7D])    # 期望应答（状态正常）


def now() -> str:
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def log(tag: str, msg: str) -> None:
    line = f"{now()} [{tag}] {msg}"
    print(line, flush=True)
    with LOG.open("a", encoding="utf-8") as fh:
        fh.write(line + "\n")


def banner(title: str) -> None:
    log("====", "=" * 68)
    log("====", title)
    log("====", "=" * 68)


def hexs(b: bytes) -> str:
    return " ".join(f"{x:02X}" for x in b)


class Conn:
    def __init__(self, host: str, port: int, tag: str, bind_port=None, timeout=3.0):
        self.tag = tag
        self.addr = (host, port)
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if bind_port:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind(("", bind_port))
        t0 = time.time()
        s.settimeout(timeout)
        try:
            s.connect(self.addr)
        except OSError as e:
            log(tag, f"connect {host}:{port} FAIL errno={e.errno} ({e})")
            raise
        self.s = s
        self.connected = True
        log(tag, f"connect {host}:{port} OK local={s.getsockname()[1]} dt={(time.time()-t0)*1000:.1f}ms")

    def query(self, wait: float = 3.0) -> list[tuple[float, bytes]]:
        """发一条 '1' 查询，返回 [(dt_ms, 收到的块)] 列表（等待 wait 秒内的全部数据）。"""
        t0 = time.time()
        self.s.sendall(Q1)
        got = []
        deadline = t0 + wait
        while True:
            remain = deadline - time.time()
            if remain <= 0:
                break
            self.s.settimeout(remain)
            try:
                d = self.s.recv(256)
            except socket.timeout:
                break
            except OSError as e:
                log(self.tag, f"recv ERROR errno={e.errno} ({e})")
                self.connected = False
                break
            if not d:
                log(self.tag, f"recv EOF (对端关闭) dt={(time.time()-t0)*1000:.1f}ms")
                self.connected = False
                break
            got.append(((time.time() - t0) * 1000, d))
        return got

    def close(self, rst: bool = False):
        if not self.connected:
            return
        if rst:
            self.s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, b"\x01\x00\x00\x00\x00\x00\x00\x00")
        try:
            self.s.close()
        except OSError:
            pass
        self.connected = False
        log(self.tag, f"close{' (RST)' if rst else ' (FIN)'}")


def ping_once(host: str, timeout: float = 1.0) -> bool:
    r = subprocess.run(["ping", "-c", "1", "-W", str(max(1, int(timeout))), host],
                       capture_output=True, text=True)
    return r.returncode == 0


def step_baseline(a) -> int:
    banner(f"baseline: 单连接 '1' ×{a.n} + 重连再 ×{a.n}  ({a.host}:{a.port})")
    ok = 0
    total = 0
    for tag in ("A", "B"):
        c = Conn(a.host, a.port, tag)
        for i in range(a.n):
            total += 1
            got = c.query(3.0)
            hit = sum(1 for _, d in got if d == R1)
            if hit == 1 and len(got) == 1:
                ok += 1
                log(tag, f"q{i+1} OK {hexs(got[0][1])} dt={got[0][0]:.1f}ms")
            else:
                log(tag, f"q{i+1} **异常**: blocks={[(round(dt,1), hexs(d)) for dt, d in got]}")
            time.sleep(a.gap)
        c.close()
        time.sleep(0.5)
    log("====", f"baseline 结果: {ok}/{total} 正常")
    return 0 if ok == total else 1


def step_hold(a) -> int:
    """单连接长存活 + 周期查询：原死机复现窗。"""
    banner(f"hold: 单连接存活 {a.seconds}s，每 {a.interval}s 查询一次 '1'（原死机窗口）")
    c = Conn(a.host, a.port, "H")
    t0 = time.time()
    n_ok = n_no = 0
    gaps = []
    last_ok = t0
    while time.time() - t0 < a.seconds:
        got = c.query(3.0)
        hit = sum(1 for _, d in got if d == R1)
        if hit == 1 and len(got) == 1:
            n_ok += 1
            gaps.append((time.time() - last_ok))
            last_ok = time.time()
            log("H", f"q{n_ok} OK dt={got[0][0]:.1f}ms alive={time.time()-t0:.1f}s")
        else:
            n_no += 1
            log("H", f"q n={n_ok+n_no} **无应答/异常** alive={time.time()-t0:.1f}s "
                    f"blocks={[(round(dt,1), hexs(d)) for dt, d in got]} "
                    f"ping={'OK' if ping_once(a.host) else 'FAIL'}")
            if not c.connected:
                log("H", "连接已断，重建")
                c = Conn(a.host, a.port, "H2")
        time.sleep(a.interval)
    alive = time.time() - t0
    log("====", f"hold 结束: 存活 {alive:.1f}s, 正常 {n_ok}, 无应答 {n_no}, "
               f"最大查询间隔 {max(gaps) if gaps else 0:.1f}s")
    c.close()
    return 0 if n_no == 0 else 1


def step_liveness(a) -> int:
    banner(f"liveness: {a.seconds}s 无 TCP 流量，每 2s ping 一次（对照实验）")
    t0 = time.time()
    nok = nfail = 0
    while time.time() - t0 < a.seconds:
        if ping_once(a.host):
            nok += 1
        else:
            nfail += 1
            log("LIVE", f"ping FAIL at t={time.time()-t0:.1f}s")
        time.sleep(2)
    log("====", f"liveness: ping {nok} 通 / {nfail} 断")
    return 0 if nfail == 0 else 1


def step_watch(a) -> int:
    """连接 + 周期查询 + 并行 ping 探活（记录每次探测时间线）。"""
    banner(f"watch: 连接后每 {a.interval}s 查询，并行每 2s ping（{a.seconds}s）")
    c = Conn(a.host, a.port, "W")
    t0 = time.time()
    last_ping = 0.0
    n_ok = n_no = 0
    while time.time() - t0 < a.seconds:
        got = c.query(3.0)
        hit = sum(1 for _, d in got if d == R1)
        if hit == 1 and len(got) == 1:
            n_ok += 1
            log("W", f"q{n_ok} OK dt={got[0][0]:.1f}ms alive={time.time()-t0:.1f}s")
        else:
            n_no += 1
            log("W", f"q **无应答** alive={time.time()-t0:.1f}s "
                    f"blocks={[(round(dt,1), hexs(d)) for dt, d in got]}")
        if time.time() - last_ping >= 2.0:
            ok = ping_once(a.host)
            last_ping = time.time()
            log("W", f"ping {'OK' if ok else '**FAIL**'} alive={time.time()-t0:.1f}s")
        time.sleep(a.interval)
    c.close()
    log("====", f"watch 结束: 正常 {n_ok} / 无应答 {n_no}")
    return 0


def step_probe(a) -> int:
    """只读探活：连接 + 一次 '1'，用于快速判断板子当前是否可用。"""
    banner(f"probe: 连接 {a.host}:{a.port} + 一次 '1'")
    c = Conn(a.host, a.port, "P")
    got = c.query(3.0)
    hit = sum(1 for _, d in got if d == R1)
    log("P", f"应答 {hit} 帧: {[(round(dt,1), hexs(d)) for dt, d in got]}")
    c.close()
    return 0 if hit else 1


def step_halfopen(a) -> int:
    """半开连接模拟：丢弃 A 的 FIN ⇒ 板子侧 A 成孤儿连接。
    修复前：服务循环被永久占死，B 永不获服务（上一轮实测 30s 零自愈）。
    修复后（accepted conn 带 keepalive 10s/2s×3）：~16s 内探测失败 abort A → B 获服务。"""
    import subprocess
    DROP_OUT = ["-p", "tcp", "-s", a.host_self, "--sport", str(a.fixed_port),
                "-d", a.host, "--dport", str(a.port), "-j", "DROP"]
    DROP_IN = ["-p", "tcp", "-s", a.host, "--sport", str(a.port),
               "-d", a.host_self, "--dport", str(a.fixed_port), "-j", "DROP"]
    banner(f"halfopen: A(local {a.fixed_port}) 的 FIN 被 DROP → 观察 B 何时获服务 "
           f"（keepalive 自愈验证，最多等 {a.seconds:.0f}s）")
    rules_added = False
    try:
        ca = Conn(a.host, a.port, "A", bind_port=a.fixed_port)
        got = ca.query(3.0)
        log("A", f"预检应答 {len(got)} 帧 {'OK' if any(d == R1 for _, d in got) else '**异常**'}")
        subprocess.run(["sudo", "iptables", "-I", "OUTPUT", "1"] + DROP_OUT, check=True)
        subprocess.run(["sudo", "iptables", "-I", "INPUT", "1"] + DROP_IN, check=True)
        rules_added = True
        log("A", "iptables DROP 规则已装（FIN 将丢弃，板子侧成半开）")
        ca.close()  # FIN 被丢
        time.sleep(0.5)
        cb = Conn(a.host, a.port, "B", bind_port=a.fixed_port + 1)
        t0 = time.time()
        healed = None
        while time.time() - t0 < a.seconds:
            got = cb.query(3.0)
            dt = time.time() - t0
            if any(d == R1 for _, d in got):
                healed = dt
                log("B", f"**获得服务** t={dt:.1f}s（= keepalive 自愈时间）")
                break
            log("B", f"t={dt:.1f}s 无应答（服务循环仍被 A 占位）")
            time.sleep(4)
        if healed is None:
            log("====", f"halfopen 结论: {a.seconds:.0f}s 内 B 未获服务（**未自愈**）")
        else:
            log("====", f"halfopen 结论: B 在 {healed:.1f}s 后获服务（keepalive 自愈生效）")
        cb.close()
        return 0 if healed is not None else 1
    finally:
        if rules_added:
            for chain, rule in (("OUTPUT", DROP_OUT), ("INPUT", DROP_IN)):
                subprocess.run(["sudo", "iptables", "-D", chain] + rule, check=False)
            chk = subprocess.run(["sudo", "iptables", "-S"], capture_output=True, text=True)
            left = [l for l in chk.stdout.splitlines() if "DROP" in l and str(a.fixed_port) in l]
            log("====", f"iptables 清理完成，残留相关规则 {len(left)} 条")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("step", choices=["baseline", "hold", "liveness", "watch", "probe", "delay",
                                     "halfopen"])
    ap.add_argument("--host", default="192.168.114.200")
    ap.add_argument("--host-self", default="192.168.114.17")
    ap.add_argument("--fixed-port", type=int, default=41200)
    ap.add_argument("--port", type=int, default=9528)
    ap.add_argument("--seconds", type=float, default=180.0)
    ap.add_argument("--interval", type=float, default=10.0)
    ap.add_argument("--n", type=int, default=3)
    ap.add_argument("--gap", type=float, default=1.0)
    a = ap.parse_args()
    log("====", f"step={a.step} host={a.host} port={a.port} seconds={a.seconds} "
               f"interval={a.interval} pid={os.getpid()}")
    fn = {"baseline": step_baseline, "hold": step_hold, "liveness": step_liveness,
          "watch": step_watch, "probe": step_probe, "halfopen": step_halfopen,
          "delay": lambda x: 0}[a.step]
    return fn(a)


if __name__ == "__main__":
    sys.exit(main())
