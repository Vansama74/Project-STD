# -*- coding: utf-8 -*-
"""ETH 看门狗事件环解码器（配合 eth_wedge_dump.jlink 导出的 bin）。
事件环条目 16B：{tick(4) dmasr(4) reason(1) alloc_status(1) pool_free(1) reserved(1) recoveries(4)}
reason 位：0=RX挂起未读帧 1=RBUS 2=RPSS 3=TUS 4=TBUS有在途 5=池锁存ERROR 6=致命FBES
用法：python eth_wedge_decode.py [ring.bin] [wr.bin]
"""
import struct
import sys
import os

RING_PATH = sys.argv[1] if len(sys.argv) > 1 else "eth_wedge_ring.bin"
WR_PATH = sys.argv[2] if len(sys.argv) > 2 else "eth_wedge_wr.bin"
EVT_SIZE = 16
RING_SIZE = 64

REASON_NAMES = {
    1 << 0: "RX挂起+未读帧(RPS=3)",
    1 << 1: "RBUS接收缓冲不可用",
    1 << 2: "RPSS接收进程停止",
    1 << 3: "TUS发送欠载",
    1 << 4: "TBUS+真在途发送(OWN=1)",
    1 << 5: "池状态锁存ERROR",
    1 << 6: "致命总线错误(FBES)",
    1 << 7: "RPS=4关闭描述符卡死(无帧完成>2s)",
    1 << 8: "重症受控整机重启(启动回读)",
}

def dmasr_bits(v):
    """DMASR 位解释"""
    out = []
    rps = (v >> 17) & 7
    tps = (v >> 20) & 7
    rps_names = {0: "停止", 1: "运行-取描述符", 2: "运行-等包", 3: "挂起(Suspended)",
                 4: "运行-关闭", 5: "时间戳", 6: "运行-传输", 7: "运行-排队"}
    tps_names = {0: "停止", 1: "运行-取描述符", 2: "运行-等状态", 3: "运行-读数据",
                 4: "时间戳", 5: "运行-关闭", 6: "挂起(Suspended)", 7: "运行-传输"}
    out.append("RPS=%d(%s)" % (rps, rps_names.get(rps, "?")))
    out.append("TPS=%d(%s)" % (tps, tps_names.get(tps, "?")))
    for bit, name in [(0x4, "TBUS"), (0x20, "TUS"), (0x40, "RS"), (0x80, "RBUS"),
                      (0x100, "RPSS"), (0x2000, "FBES"), (0x400, "ETS"),
                      (0x8000, "AIS"), (0x10000, "NIS")]:
        if v & bit:
            out.append(name)
    return " ".join(out)

def main():
    if not os.path.exists(RING_PATH):
        print(f"找不到 {RING_PATH}（先跑 eth_wedge_dump.jlink）")
        sys.exit(1)
    ring = open(RING_PATH, "rb").read()
    wr = struct.unpack("<I", open(WR_PATH, "rb").read())[0] if os.path.exists(WR_PATH) else 0
    n = min(wr, RING_SIZE)
    print(f"事件总数 {wr}，环内 {n} 条（最新在前）：")
    print("-" * 78)
    if n == 0:
        print("（空 —— 期间无真实看门狗事件）")
        return
    for k in range(n):
        # 最新一条在 (wr-1) % 64，往前数
        idx = (wr - 1 - k) % RING_SIZE
        off = idx * EVT_SIZE
        tick, dmasr, reason, alloc, pool_free, rec = struct.unpack(
            "<IIHBB I".replace(" ", ""), ring[off:off + EVT_SIZE])
        rnames = "|".join(v for b, v in REASON_NAMES.items() if reason & b) or "?"
        print(f"#{wr - k:3d} t={tick / 1000.0:8.1f}s DMASR=0x{dmasr:08X}"
              f" [{dmasr_bits(dmasr)}]")
        print(f"     原因={rnames}  池状态={alloc}({'OK' if alloc == 0 else 'ERROR'})"
              f"  池空闲={pool_free}/12  recoveries={rec}")

if __name__ == "__main__":
    main()
