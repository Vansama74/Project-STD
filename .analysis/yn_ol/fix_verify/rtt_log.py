#!/usr/bin/env python3
"""RTT 抓取器（自研，绕过 JLinkRTTLogger 的「Control Block not found」故障）

原理：直接用 J-Link 读目标 RAM 里的 SEGGER RTT 控制块（_SEGGER_RTT），
自行解码 up-buffer 的 WrOff/RdOff 环形指针，读走数据后回写 RdOff。
不 halt、不 reset（除非显式传 --reset），因此可在目标全速运行时长时间抓取。

用法：
  python3 rtt_log.py --out log.txt --duration 60            # 只抓新数据
  python3 rtt_log.py --out log.txt --duration 60 --drain    # 先倒出积压数据
  python3 rtt_log.py --out log.txt --reset --duration 30    # 复位后再抓
"""
import argparse
import os
import sys
import time

import pylink

# ---- MCU 状态变量地址（arm-none-eabi-nm build/Debug/Project_STD.elf；换镜像必须更新）----
MCU_SYM = {
    "g_fault_r0": 0x2000E730,
    "g_fault_bfar": 0x2000E734,
    "g_fault_cfsr": 0x2000E738,
    "g_fault_pc": 0x2000E73C,
    "xTickCount": 0x2001567C,
    "pxCurrentTCB": 0x20015B54,
    "xMinimumEverFreeBytesRemaining": 0x20015C9C,
    "xFreeBytesRemaining": 0x20015CA0,
    "xLastFailedAllocSize": 0x20015CA4,
}
TASK_NAME_OFF = 52  # TCB 内 pcTaskName 偏移（FreeRTOS v10.3.1 + 本工程 config）

# SEGGER_RTT_BUFFER_UP 布局（SEGGER_RTT.h:320-328）：4B 对齐
#   +0  sName(ptr) +4 pBuffer(ptr) +8 SizeOfBuffer +12 WrOff +16 RdOff +20 Flags
UP0 = 24  # aUp[0] 相对控制块起始的偏移 = 16(acID) + 4 + 4


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--addr", default="auto", help="_SEGGER_RTT 控制块地址（auto = 扫描 SRAM/CCM）")
    ap.add_argument("--device", default="STM32F407ZG")
    ap.add_argument("--speed", type=int, default=4000)
    ap.add_argument("--channel", type=int, default=0)
    ap.add_argument("--out", required=True)
    ap.add_argument("--duration", type=float, default=60.0)
    ap.add_argument("--drain", action="store_true", help="启动时先倒出积压数据")
    ap.add_argument("--reset", action="store_true", help="抓取前复位目标")
    ap.add_argument("--sn", default=None, help="J-Link 序列号（多探针时指定）")
    ap.add_argument("--mcu", action="store_true", help="同时轮询 MCU 状态（故障寄存器/heap/tick/任务名）")
    ap.add_argument("--mcu-period", type=float, default=0.3)
    ap.add_argument("--mcu-out", default=None, help="MCU 状态日志（默认 <out>.mcu）")
    args = ap.parse_args()

    jl = pylink.JLink()
    if args.sn:
        jl.open(serial_no=args.sn)
    else:
        jl.open()
    jl.set_tif(pylink.enums.JLinkInterfaces.SWD)
    jl.connect(args.device, speed=args.speed, verbose=False)
    print(f"[rtt] connected: {jl.target_connected()} sn={jl.serial_number}", flush=True)

    if args.reset:
        jl.reset()
        print("[rtt] target reset issued", flush=True)

    # 定位控制块：显式地址，或在 SRAM/CCM 里扫描 "SEGGER RTT" 签名
    if args.addr == "auto":
        sig = b"SEGGER RTT"
        found = None
        for base, size in ((0x20000000, 0x20000), (0x10000000, 0x10000)):
            step = 0x1000
            for off in range(0, size, step):
                n = min(step, size - off)
                try:
                    chunk = bytes(jl.memory_read8(base + off, n))
                except Exception:
                    continue
                p = chunk.find(sig)
                if p >= 0:
                    found = base + off + p
                    break
            if found:
                break
        if not found:
            print("[rtt] FATAL: 未找到 'SEGGER RTT' 签名", flush=True)
            jl.close()
            return 2
        cb = found
        print(f"[rtt] auto-discovered control block @ 0x{cb:08x}", flush=True)
    else:
        cb = int(args.addr, 16)

    # 取出控制块
    raw = jl.memory_read8(cb, 16 + 8 + 3 * UP0)
    cid = bytes(raw[0:16]).split(b"\x00")[0].decode("ascii", "replace")
    if cid != "SEGGER RTT":
        print(f"[rtt] FATAL: control block ID={cid!r} (期望 'SEGGER RTT')", flush=True)
        jl.close()
        return 2
    base = cb + UP0 + args.channel * UP0
    b = jl.memory_read32(base, 5)  # sName,pBuffer,Size,WrOff,RdOff
    pbuffer, size, wroff, rdoff = b[1], b[2], b[3], b[4]
    print(f"[rtt] ch{args.channel} pBuffer=0x{pbuffer:08x} size={size} WrOff={wroff} RdOff={rdoff}", flush=True)
    if size == 0 or size > 0x10000:
        print("[rtt] FATAL: 非法 buffer size", flush=True)
        jl.close()
        return 2

    out = open(args.out, "ab", buffering=0)
    t0 = time.time()
    last_rd = rdoff
    # 首轮：非 --drain 时把 RdOff 追到 WrOff（丢弃历史）
    if not args.drain:
        jl.memory_write32(base + 16, [wroff])
        last_rd = wroff

    empty_polls = 0
    mcu_path = args.mcu_out or (args.out + ".mcu")
    mcu_fh = open(mcu_path, "a", encoding="utf-8") if args.mcu else None
    last_mcu = 0.0
    last_tick = None
    tick_stall = 0
    fault_seen = False
    alloc_seen = False
    mcu_n = 0

    def mcu_poll(t_elapsed):
        """非侵入（不 halt）读 MCU 关键状态；只在异常/每 10s 落一行，异常全落。"""
        nonlocal last_mcu, last_tick, tick_stall, fault_seen, alloc_seen, mcu_n
        now_t = time.time()
        if now_t - last_mcu < args.mcu_period:
            return
        last_mcu = now_t
        try:
            tick = jl.memory_read32(MCU_SYM["xTickCount"], 1)[0]
            fpc = jl.memory_read32(MCU_SYM["g_fault_pc"], 1)[0]
            cfsr = jl.memory_read32(MCU_SYM["g_fault_cfsr"], 1)[0]
            bfar = jl.memory_read32(MCU_SYM["g_fault_bfar"], 1)[0]
            fr0 = jl.memory_read32(MCU_SYM["g_fault_r0"], 1)[0]
            alloc = jl.memory_read32(MCU_SYM["xLastFailedAllocSize"], 1)[0]
            free = jl.memory_read32(MCU_SYM["xFreeBytesRemaining"], 1)[0]
            minfree = jl.memory_read32(MCU_SYM["xMinimumEverFreeBytesRemaining"], 1)[0]
            tcb = jl.memory_read32(MCU_SYM["pxCurrentTCB"], 1)[0]
            name = "?"
            try:
                raw = bytes(jl.memory_read8(tcb + TASK_NAME_OFF, 16))
                name = raw.split(b"\x00")[0].decode("ascii", "replace")
            except Exception:
                pass
        except Exception as exc:  # 读失败（目标复位瞬间等）不致命
            print(f"[mcu] read error: {exc}", flush=True)
            return

        mcu_n += 1
        notes = []
        if last_tick is not None:
            if tick == last_tick:
                tick_stall += 1
            elif tick_stall:
                notes.append(f"tick STALLED {tick_stall} polls (~{tick_stall * args.mcu_period:.1f}s) -> recovered")
                tick_stall = 0
        last_tick = tick
        if fpc and not fault_seen:
            fault_seen = True
            notes.append(f"**HardFault** pc=0x{fpc:08x} cfsr=0x{cfsr:08x} bfar=0x{bfar:08x} r0=0x{fr0:08x}")
        if alloc and not alloc_seen:
            alloc_seen = True
            notes.append(f"**malloc FAILED** req={alloc} free={free} min={minfree}")
        if free < 2048:
            notes.append(f"heap LOW free={free}")
        if notes or tick_stall or mcu_n % max(1, int(10.0 / args.mcu_period)) == 0:
            line = (f"{time.strftime('%H:%M:%S')}.{int((now_t % 1) * 1000):03d} [mcu] t={t_elapsed:7.1f}s "
                    f"tick={tick} task={name:<20} heap free={free} min={minfree} "
                    + ("; ".join(notes) if notes else "ok"))
            mcu_fh.write(line + "\n")
            mcu_fh.flush()
            print(line, flush=True)

    try:
        while time.time() - t0 < args.duration:
            if args.mcu:
                mcu_poll(time.time() - t0)
            b = jl.memory_read32(base, 5)
            size, wroff, rdoff = b[2], b[3], b[4]
            avail = (wroff - rdoff) % size
            if rdoff >= size or wroff >= size:  # 控制块自愈（异常值）
                jl.memory_write32(base + 16, [0])
                avail = 0
            if avail:
                empty_polls = 0
                # 连续读取（处理回绕）
                n = avail
                data = []
                p = (rdoff % size)
                first = min(n, size - p)
                data.extend(jl.memory_read8(pbuffer + p, first))
                if n > first:
                    data.extend(jl.memory_read8(pbuffer, n - first))
                out.write(bytes(data))
                jl.memory_write32(base + 16, [(rdoff + n) % size])
                last_rd = (rdoff + n) % size
            else:
                empty_polls += 1
                if empty_polls % 200 == 0:
                    print(f"[rtt] idle {empty_polls * 0.05:.1f}s (tick 仍在?)", flush=True)
            time.sleep(0.05)
    except KeyboardInterrupt:
        pass
    finally:
        out.close()
        jl.close()
    print(f"[rtt] done, elapsed={time.time() - t0:.1f}s -> {args.out}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
