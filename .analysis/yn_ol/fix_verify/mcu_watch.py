#!/usr/bin/env python3
"""MCU 非侵入式状态监视（不 halt、不 reset）——死机/复位取证

用途：在被测固件跑 TCP 复现实验时，用一条常驻 J-Link 会话按周期读 RAM 里的
关键变量，把「死机瞬间」的固件内部状态落盘（IWDG 复位会清 .bss，事后读不到）：

  g_fault_pc        非 0 = 发生过 HardFault（值就是出错指令地址，用 addr2line 定位）
  g_fault_cfsr      故障状态寄存器（BFSR/MMFSR/UFSR）
  g_fault_bfar      总线错误地址
  xLastFailedAllocSize  非 0 = pvPortMalloc 失败过（值 = heap_4 实耗口径请求字节数）
  xFreeBytesRemaining / xMinimumEverFreeBytesRemaining  ucHeap 当前/历史最小余量
  xTickCount        不前进 = 调度器已死（中断关闭 / 卡在临界区 / 卡在故障处理）
  pxCurrentTCB      当前运行任务（读 TCB->pcTaskName，偏移 52）

用法：
  python3 mcu_watch.py --duration 300 --period 0.3 --out mcu_watch.log
"""
import argparse
import datetime
import struct
import sys
import time

import pylink

# 由 arm-none-eabi-nm build/Debug/Project_STD.elf 取得（换镜像必须更新！）
SYM = {
    "g_fault_r0": 0x2000E708,
    "g_fault_bfar": 0x2000E70C,
    "g_fault_cfsr": 0x2000E710,
    "g_fault_pc": 0x2000E714,
    "xTickCount": 0x20015654,
    "pxCurrentTCB": 0x20015B2C,
    "xMinimumEverFreeBytesRemaining": 0x20015C74,
    "xFreeBytesRemaining": 0x20015C78,
    "xLastFailedAllocSize": 0x20015C7C,
}
TASK_NAME_OFF = 52  # TCB 内 pcTaskName 偏移（v10.3.1 + 本工程 config）


def now() -> str:
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--device", default="STM32F407ZG")
    ap.add_argument("--speed", type=int, default=4000)
    ap.add_argument("--duration", type=float, default=300.0)
    ap.add_argument("--period", type=float, default=0.3)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    jl = pylink.JLink()
    jl.open()
    jl.set_tif(pylink.enums.JLinkInterfaces.SWD)
    jl.connect(args.device, speed=args.speed, verbose=False)

    def rd(addr, n=1):
        return jl.memory_read32(addr, n)

    out = open(args.out, "a", encoding="utf-8")

    def emit(tag, msg):
        line = f"{now()} [{tag}] {msg}"
        print(line, flush=True)
        out.write(line + "\n")
        out.flush()

    emit("===", f"mcu_watch start duration={args.duration} period={args.period}")
    t0 = time.time()
    last_tick = None
    tick_stall = 0
    fault_seen = False
    alloc_fail_seen = False
    n = 0
    try:
        while time.time() - t0 < args.duration:
            v = rd(SYM["xTickCount"])[0]
            fpc = rd(SYM["g_fault_pc"])[0]
            cfsr = rd(SYM["g_fault_cfsr"])[0]
            bfar = rd(SYM["g_fault_bfar"])[0]
            fr0 = rd(SYM["g_fault_r0"])[0]
            alloc = rd(SYM["xLastFailedAllocSize"])[0]
            free = rd(SYM["xFreeBytesRemaining"])[0]
            minfree = rd(SYM["xMinimumEverFreeBytesRemaining"])[0]
            tcb = rd(SYM["pxCurrentTCB"])[0]
            name = "?"
            try:
                raw = bytes(jl.memory_read8(tcb + TASK_NAME_OFF, 16))
                name = raw.split(b"\x00")[0].decode("ascii", "replace")
            except Exception:
                pass

            n += 1
            # 只在「异常」或每 5s 打一行（避免日志爆掉）
            anomaly = []
            if last_tick is not None:
                d = (v - last_tick) & 0xFFFFFFFF
                if d == 0:
                    tick_stall += 1
                else:
                    if tick_stall > 0:
                        anomaly.append(f"tick STALLED {tick_stall} polls (~{tick_stall*args.period:.1f}s)")
                    tick_stall = 0
            last_tick = v
            if fpc and not fault_seen:
                fault_seen = True
                anomaly.append(f"**HardFault** pc=0x{fpc:08x} cfsr=0x{cfsr:08x} bfar=0x{bfar:08x} r0=0x{fr0:08x}")
            if alloc and not alloc_fail_seen:
                alloc_fail_seen = True
                anomaly.append(f"**malloc FAILED** xLastFailedAllocSize={alloc} free={free} min={minfree}")
            if free < 2048:
                anomaly.append(f"heap LOW free={free}")
            if anomaly or n % int(5.0 / args.period) == 0:
                emit("mcu", f"t={time.time()-t0:7.1f}s tick={v} task={name:<22} "
                            f"heap free={free} min={minfree} " +
                            ("; ".join(anomaly) if anomaly else "ok"))
    finally:
        out.close()
        jl.close()
    emit("===", f"mcu_watch done ({n} polls)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
