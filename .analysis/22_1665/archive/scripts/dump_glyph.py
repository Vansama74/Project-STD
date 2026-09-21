#!/usr/bin/env python3
"""从字库 bin 中按项目公式 dump「口」(GBK 0xBFDA) 的 16 点阵字形。

公式来源：Application/Src/app_render.c 的 _flash_addr_cfg()
  W25Q64  : char_idx = (h-0xA1)*94 + (l-0xA1)          （94 列序）
  MX25L256: char_idx = (h-0x81)*190 + (l-0x41/l-0x40)  （190 列序）
  readaddr = (base + sec + X)*4096 + (page + Y)*256 + byte + Z
"""
import sys, pathlib

GBK_HT = bytes([0xBF, 0xDA])  # 口

# (name, base, X, Y, Z) —— 取自 app_render.c 的 16pt GBK HT 行
FORMULAS = {
    "W25Q64":    dict(base=209,  X=0, Y=2, Z=96,  idx=lambda h, l: (h - 0xA1) * 94 + (l - 0xA1)),
    "MX25L256":  dict(base=1218, X=0, Y=8, Z=64,  idx=lambda h, l: (h - 0x81) * 190 + (l - 0x41)),
}
BINS = [
    ("信路威-16-20-24", "/home/yystation/文档/创迪科技生产订单与技术资料/19 标准产品资料/收费站多功能一体机-标准资料整理v2.0/5.软件（含平台软件、测试软件与嵌入式程序）/1.嵌入式程序/9J6F1C5110(3-1248_3-1412_3-1613)/字库芯片程序/信路威-16-20-24-HZK-NEW-bin.bin"),
    ("W25Q256_14_16_20_24_32", "/home/yystation/文档/创迪科技生产订单与技术资料/19 标准产品资料/升降栏杆机/5.软件（含平台软件、测试软件与嵌入式程序）/1.嵌入式程序/9I124D6521/字库芯片程序/W25Q256_FONT_14_16_20_24_32.bin"),
    ("贵州P10双色治超屏字库", "/home/yystation/文档/创迪科技生产订单与技术资料/19 标准产品资料/收费站多功能一体机-标准资料整理v2.0/5.软件（含平台软件、测试软件与嵌入式程序）/1.嵌入式程序/9J9F2E4230(23-543)/字库芯片程序/贵州P10双色治超屏字库"),
]


def addr_of(f, size=16, w=16):
    """GBK 字形：每字 size * ((size+7)//8) 字节"""
    bpc = size * ((w + 7) // 8)
    h, l = GBK_HT
    idx = f["idx"](h, l)
    fonf = idx * bpc
    sec, page, byte = fonf // 4096, (fonf % 4096) // 256, (fonf % 4096) % 256
    return (f["base"] + sec + f["X"]) * 4096 + (page + f["Y"]) * 256 + byte + f["Z"], idx


def show(buf, size=16, w=16):
    rb = (w + 7) // 8
    out = []
    for row in range(size):
        line = ""
        for col in range(w):
            bit = (buf[row * rb + col // 8] >> (7 - col % 8)) & 1
            line += "#" if bit else "."
        out.append(line)
    return "\n".join(out)


def main():
    for name, path in BINS:
        p = pathlib.Path(path)
        if not p.exists():
            print(f"!! 缺失 {name}: {path}")
            continue
        data = p.read_bytes()
        print("=" * 72)
        print(f"# {name}  ({len(data)} bytes)")
        for fname, f in FORMULAS.items():
            a, idx = addr_of(f)
            in_range = a + 32 <= len(data)
            print(f"-- {fname}: idx={idx} addr={a} (0x{a:X}) in_range={in_range}")
            if not in_range:
                continue
            print(show(data[a:a + 32]))
            print()


if __name__ == "__main__":
    main()
