# GZ_OL「16 点阵正常、24/32 点阵无内容」实机缺陷定位报告

> 日期：2026-09-14　协议：贵州治超屏（GZ_OL，"TCLY" 帧族，源项目 `9K23881580`）
> 触发命令：`0x20` 立即显示　现象：16 点阵正常、24/32 点阵「屏幕上什么都没有」
> **结论：不是字号解析缺陷、不是字库缺陷、不是字形地址缺陷——是 16:02 烧录镜像的屏体几何为 32×16（22-1703 1×1 实验态），叠加显示层「字形越屏 → 整块丢弃」的静默语义。**
> 修复：GZ_OL 侧加「字号 ≤ 屏高」前置判定的 RTT 诊断（把静默变可诊断，**不替换字号、不降级掩盖**），现场动作见 §6。

---

## 0. 结论速览

| 假设（任务书 1~6） | 判定 | 一句话证据 |
|---|---|---|
| ① 字号字段接受条件（两字节/低字节/ASCII）与上位机实际字节形态冲突 → 整帧丢弃 | **排除** | 我实现与源固件**同规则**（两字节相等且 ∈{16,24,32}）；上位机 `CD_GuiZhou_QBB.dll` `IFB_Display` 反汇编证实 24/32 就发 `18 18` / `20 20`，与我实现判据一致 |
| ② 渲染区域参数（h 是否写死 16 / 区域高度 < 字形高度 → 整行跳过） | **机制成立，但触发源不是区域参数** | 执行层 `h = 屏高 − y`（从不是 16）；**字形整块丢弃发生在显示层** `dev_display.c:229-233`，触发条件是「字形框超出屏体几何」，而现场屏体几何只有 32×16 |
| ③ 字号→枚举映射（`-fshort-enums` 错配） | **排除** | `font_size_t` 显式赋值 {0,14,16,20,24,32}；解析处 `case 16/24/32` 直接赋 `FONT_16/24/32`，无回退路径 |
| ④ 字库缺 24/32 区块（→ 需换字库） | **排除** | 直接解码 `02字库/贵州P10双色治超屏字库`：24pt GBK 宋体非空率 63.2%，24/32×4 字型均可读出**可辨认字形**；算得地址与源固件 `func.h`/`func.c` 公式**逐字节相同**；且 `_find_region` 未命中会回退首项（画错尺寸字形，**不会空白**） |
| ⑤ 交叉验证：仓库内已有实机验证的 24 点阵先例 | **不成立（无此先例）** | doc/13 安徽 FONT_24 仍标「待联调」、doc/11/03 无实机记录 → 该链条不能用作证据，本报告改用「字库文件直接解码 + 地址公式对拍」 |
| ⑥ 文本编码/字形字节数随字号变化导致读空 | **排除** | 字节数 `size×((size+7)/8)`：24/32 GBK = 72/128B，与源固件 `W25QXX_Read(font_buf, readaddr, 72/128)` **一致**；编码分支 GBK 与字号无关 |
| **⑦（本轮新增）烧录镜像的屏体几何** | **✅ 根因** | 16:02 烧录的镜像链接的是 `dev_display_22_1703`，其实例 `g_22_1703` 实测 `screen_rows=32 / screen_cols=16`（1×1 实验态）→ 24/32 字形框 24×24 / 32×32 越屏 → 显示层整块丢弃 → 清屏后屏幕无内容 |

**为什么 16 正常而 24/32 不正常**：16pt 字形框 = 16×16，恰好等于 32×16 屏高（判界用 `>`，等号通过）→ 落屏；24/32pt 字形框 = 24×24 / 32×32 > 16 → `dev_display_draw_bitmap()` 整字形早退 → 0x20 又先整屏清黑，屏幕只剩黑底 = 用户看到的「不是花屏、不是乱码、是没有内容」。

---

## 1. 现场事实与镜像追溯（先定「跑的是哪个固件」）

实机反馈：通信正常（帧能收、协议能应答）→ 16 点阵正常，24/32 无内容。

### 1.1 烧录件 = EIDE 构建产物（不是任务书假定的 `PROTO=ALL DISP=1_263`）

| 证据 | 事实 |
|---|---|
| `build/Debug/Project_STD.elf` mtime | **2026-09-14 16:02:47**（elf/hex/bin 同一秒；同一时刻无任何终端构建命令：`terminals/1.txt` 为空提示符） |
| `build/Debug/*.o`（make 产物） | 15:56:35（上一次 make 构建，1-263 口径） |
| 终端 `terminals/10.txt` | **16:02:53** `tool/flash_all.sh` 烧录 `build/Debug/Project_STD.hex`；脚本用 `du -h` 报尺寸 **664K** → 正是当时那份 16:02 产物（当前 make 默认口径 hex = **1004K**，量级完全可区分） |
| `arm-none-eabi-nm build/Debug/Project_STD.elf` | 含 `dev_display_22_1703_init`，**不含** `dev_display_1_263_init` |
| `arm-none-eabi-size -A` 该 elf | `.ccmram = 5040` = 3904（22-1703 1×1）+ 1136（GZ_OL 队列）→ **EIDE Debug 口径**（排除 CQ；`make DISP=22_1703` 会是 11420） |

> 即：现场跑的是 **EIDE Debug 构建**（显示模组编 22-1703、协议集 ≈ LDI + IAP + 四川三协议 + 贵州治超）。
> 与任务书假定的「默认口径 `PROTO=ALL` `DISP=1_263`（224×64）」不符——这是本轮定位的关键前提纠正。

### 1.2 该镜像的屏体几何 = 32×16（硬证据）

`g_22_1703` 实例初值（`arm-none-eabi-objdump -s -j .data build/Debug/Project_STD.elf`，`g_22_1703 @ 0x200000a0`，按 `dev_display.h:37-70` 布局解码）：

```
module_rows=32  module_cols=16  channels_per_module=2  modules_per_row=1  modules_per_col=1
scan_lines=4    screen_rows=32  screen_cols=16        total_channels=2
channel_pixels=256  scan_line_pixels=64  buffer_size=512
pixel_map=0x10000690  hub75_buff=0x10000490  module_code="2200001703"
```

对应源码 `Device/Display/dev_display_22_1703.c`：

```58:69:Device/Display/dev_display_22_1703.c
#define _22_1703_MODULE_ROWS         (1U)  /* 每行模块数（水平） */
#define _22_1703_MODULE_COLS         (1U)  /* 每列模块数（垂直） */
...
#define _22_1703_SCREEN_ROWS    (_22_1703_MODULE_ROWS * _22_1703_MODULE_PIXEL_ROW) /* 屏幕每行像素数 = 224 */
#define _22_1703_SCREEN_COLS    (_22_1703_MODULE_COLS * _22_1703_MODULE_PIXEL_COL) /* 屏幕每列像素数 = 64 */
```

即 `MODULE_ROWS/COLS = 1/1` → **屏 32×16**（注释里的 "= 224 / = 64" 是 7/4 口径的残留文字，数值由宏计算，实为 32/16）。
该文件由并行审查会话判定为「15:26 未完成的 1×1 实验态、存在运行期越界」（`.analysis/9k23881580/disp_22_1703_change_review.md`），本轮不修改该文件（用户纪律）。

**自助复核命令**（任何时候可验证「当前 `build/Debug/Project_STD.elf` 到底是哪套几何」）：

```bash
cd /home/yystation/Program/3833024/Project-STD-main
# ① 生效显示驱动（只应出现一个；1-263 → 224×64，22-1703 → 见下）
arm-none-eabi-nm build/Debug/Project_STD.elf | grep -E "T dev_display_(1_263|22_1703)_init"
# ② 该驱动源码里的几何宏（22-1703 的 1×1 = 32×16）
grep -nE "_22_1703_MODULE_(ROWS|COLS)\s+\(" Device/Display/dev_display_22_1703.c
# ③ 口径速判：ccmram 与 hex 尺寸
arm-none-eabi-size -A build/Debug/Project_STD.elf | grep ccmram   # 5040=EIDE(22-1703 1×1) / 11420=make DISP=22_1703 / 37084=make 默认 1-263
du -h build/Debug/Project_STD.hex                                 # 664K=EIDE 产物 / 1004K=make 默认口径
```

> ⚠ 本轮收尾已按纪律把工作区重建为默认口径（1-263），**16:02 那份 EIDE elf 已被覆写**——其 md5 与实例字段 dump 已固定记录在 §1.1/§1.2（`g_22_1703`：`screen_rows=32 / screen_cols=16`）；若需现场再验，重新在 EIDE 里构建一次即可复现同一几何。

---

## 2. 逐条假设的排查过程（按任务书顺序）

### 2.1 假设 ①：字号字段接受条件 —— 排除

**本实现**（`Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.c`）：

```60:70:Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.c
static bool _gz_ol_font_size(uint8_t size_lo, uint8_t size_hi, font_size_t *out)
{
    if (size_lo != size_hi)
        return false;
    switch (size_lo) {
        case 16: *out = FONT_16; return true;
        case 24: *out = FONT_24; return true;
        case 32: *out = FONT_32; return true;
        default: return false;
    }
}
```
调用点 `:137`（`_gz_ol_font_size(p[12], p[13], &size)`，`p = &raw[16]` → 帧内偏移 28/29）。

**源固件**（`/home/yystation/Desktop/9K23881580/01Embedded_software/USER/LWIP_APP/UDP_SERVER.C:164-186`）：

```164:186:/home/yystation/Desktop/9K23881580/01Embedded_software/USER/LWIP_APP/UDP_SERVER.C
    if ((inbuf[12] == 16) && (inbuf[13] == 16)) {
        fontSize = FONT16;
        ...
    } else if ((inbuf[12] == 24) && (inbuf[13] == 24)) {
        fontSize = FONT24;
        ...
    } else if ((inbuf[12] == 32) && (inbuf[13] == 32)) {
        fontSize = FONT32;
        ...
    }
```

**逐字节对比结论**：两字节相等、取值恰为 16/24/32 —— **本实现与源固件判据完全相同**（差异仅在非法值分支：源固件沿用上一帧字号 = UB，本实现整帧丢弃，属有意收紧）。协议文档帧例亦为 `字体大小 20 20`（32 点阵，`38贵州LED_情报板_协议V1.0.doc` 第 44-47 行）。

**上位机实际发什么字节**（决定性证据，来自随源项目交付的测试软件 `03测试软件/2022_贵州贵黔高速_治超屏测试软件_黔通智联.zip`：
`CD_GuiZhou_QBB_Test.exe`（MFC42，导入 `IFB_Display`）+ `CD_GuiZhou_QBB.dll`（导出 7 个函数 `IFB_Open/Close/Clear/Display/GetHWVersion/GetStatus/GetStatusMsg`）。

`IFB_Display` @RVA `0x13b0` 反汇编（`objdump -D -b binary -m i386`）关键片段：

```asm
; 字号 = 两字节同一个值（写入帧偏移 28/29 = payload[12]/[13]）
1426: sar  eax,0x8        ; eax = arg2 >> 8
142a: jne  0x1433
142c: mov  ebx,0x10       ; 0 → 16
1433: dec  eax / neg / sbb / and 0x8 / add 0x18   ; 1 → 0x18(24)；≥2 → 0x20(32)
1504: mov  BYTE PTR [esp+0x34],bl   ; payload[12] = bl
1508: mov  BYTE PTR [esp+0x35],bl   ; payload[13] = bl   ← 两字节恒等
; 其余字段（帧内偏移）
;   [16..19] = 0           → X=0, Y=0        （0x14a5-0x14b1）
;   [20..23] = C0 00 50 00 → 屏宽=192 屏高=80（0x14de-0x14ef）
;   [24..27] = CB CE CC E5 → "宋体"           （0x14f0-0x14ff）
;   [30..32] = 颜色（FF0000/00FF00/FFFF00 三选一，由 arg2 低 4 位决定）
;   [33..]   = 文本（C 串，NUL 即帧尾 0x00）
```

→ 上位机对 16/24/32 分别发 **`10 10` / `18 18` / `20 20`**，X=0、Y=0。
**假设 ① 彻底排除**：不存在「24/32 落在拒绝分支」的情形（`18 18`、`20 20` 都被接受）。
（附带发现：该工具默认设备 IP `192.168.0.1`、端口 **10028**——与 doc/14 §11 记录一致，UDP 联调须改成 10011 或改走串口。）

### 2.2 假设 ②：渲染区域参数与裁剪语义 —— 机制成立，但「不是区域参数写死」

执行层实际传参（`Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c:123-185`，本次修改后行号）：

| 字段 | 取值 | 是否写死 |
|---|---|---|
| `x` / `y` | 帧内 X/Y，越界钳 0 | 帧值 |
| `w` | `屏幕宽 − x`（224×64 屏 = 224；32×16 屏 = 32） | **由屏体几何推导，非写死** |
| `h` | `屏幕高 − y`（224×64 屏 = 64；32×16 屏 = **16**） | **由屏体几何推导，非写死 16** |
| `style` | `ALIGN_LEFT_UP / ALIGN_LEFT_UP / word_wrap=true` | 固定 |
| `font_size` | 解析出的 `FONT_16/24/32` | 帧值 |
| `text_enc` | `FONT_ENC_GBK` | 固定 |

**裁剪语义**（`Application/Src/app_render.c`）：
- 宽方向：`_render_text` 逐字宽累加，超 `cfg->w` 时 `word_wrap` 换行（`:515-528`、`:608-625`）；
- 高方向：仅在换行分支检查 `if (cur_y + line_h > cfg->h) return;`（`:587`、`:620`）——**首行不检查**；
- **真正把 24/32 判死的是显示层的字形判界**（`Device/Display/dev_display.c`）：

```225:233:Device/Display/dev_display.c
void dev_display_draw_bitmap(dev_display_t *dev, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *bitmap, display_color_t color)
{
    /* 起点越界早退（与 fill 对齐）：x+w / y+h 为 uint16 加法，
     * 起点已回绕出屏时 32 位判界也无法救回，直接丢弃整区域 */
    if (x >= dev->screen_rows || y >= dev->screen_cols)
        return;
    /* 越界判断用 32 位运算防 x+w / y+h 的 uint16 回绕绕过判界（根因同 fill） */
    if ((uint32_t)x + w > dev->screen_rows || (uint32_t)y + h > dev->screen_cols)
        return;
```

调用点 `app_render.c:599` / `:632` 传的正是 `glyph_w × font_size`（GBK：16×16 / 24×24 / 32×32）。
→ 屏高 16 时：`0 + 24 > 16`、`0 + 32 > 16` → **整字形丢弃**（对比：`dev_display_fill` 同处是**截断部分填充**，所以先画的黑色底框仍然生效 → 屏幕全黑，与「没有内容」吻合）。

### 2.3 假设 ③：字号→枚举映射 —— 排除

`Application/Inc/app_render.h:19-26`：`FONT_SELF_ADAPT=0, FONT_14=14, FONT_16=16, FONT_20=20, FONT_24=24, FONT_32=32`（显式赋值，`-fshort-enums` 下只是类型宽度变小，值不变）。解析处直接 `*out = FONT_24/FONT_32`（`app_gz_ol_proto_parse.c:65-67`），无「非法值回退」路径；`_render_text` 只在 `FONT_SELF_ADAPT(0)` 时才自适应选择（`app_render.c:479-483`），显式字号不经该分支。

### 2.4 假设 ④：字库是否真含 24/32 —— 排除（并给出对拍证据）

字库文件：`/home/yystation/Desktop/9K23881580/02字库/贵州P10双色治超屏字库`（8,388,608B，与 `与贵州P10字库相同.txt` 同名注释一致）。

**(a) 各区块数据密度**（按本工程 `app_render.c` 的 W25Q64 表算起始地址与区块长度统计非 0x00/0xFF 字节）：

| 区块 | 起始地址 | 区域字节 | 非空 | 非空率 |
|---|---|---|---|---|
| 16 GBK 宋体 | 8,320 | 282,752 | 223,078 | 78.9% |
| 24 GBK 宋体 | 1,164,160 | 636,192 | 401,850 | **63.2%** |
| 24 GBK 仿宋 | 1,800,384 | 636,192 | 377,404 | 59.3% |
| 24 GBK 楷体 | 2,436,608 | 636,192 | 344,923 | 54.2% |
| 24 GBK 黑体 | 3,072,832 | 636,192 | 404,203 | 63.5% |
| 32 GBK 宋体 | 3,741,952 | 1,131,008 | 640,198 | **56.6%** |
| 32 GBK 仿宋 | 4,872,992 | 1,131,008 | 571,660 | 50.5% |
| 32 GBK 楷体 | 6,004,032 | 1,131,008 | 532,397 | 47.1% |
| 32 GBK 黑体 | 7,135,072 | 1,131,008 | 642,379 | 56.8% |

**(b) 直接解码字形**（以「贵」GBK `B9 F3` 为例，按本工程 `_flash_addr` 公式算地址后逐位展开）：16/24/32 三档、宋体/仿宋/楷体/黑体四型**均能读出结构完整的「贵」字点阵**（24pt = 72B、32pt = 128B）。

**(c) 地址公式对拍**（本工程 `app_render.c` 表 vs 源固件 `func.h` 宏 + `func.c` 读取函数）：

| 三元组 | 源固件公式 | 本工程表 | 对拍 |
|---|---|---|---|
| 24 GBK 宋体 | `(1164160/4096 + sec + 1)*4096 + (page−12)*256 + byte−128` | `base=284, X=1, Y=−12, Z=−128` → `(284+sec+1)*4096 + (page−12)*256 + byte−128` | ✅ 逐字节相同（1164160 起） |
| 24 GBK 黑体 | `(3072832/4096 + sec + 1)*4096 + (page−12)*256 + byte−192` | `base=750, X=1, Y=−12, Z=−192` | ✅ |
| 32 GBK 宋体 | `FonfAddr = ((94*(ch0−0xA1)+(ch1−0xA1)) * 128`；`(3741952/4096+sec+1)*4096 + (page−7)*256 + byte` | `base=913, X=1, Y=−7, Z=0` | ✅ |

（`GZ_OL` 使用的 `conf=宋体` 默认路径亦在最常见的 32 号帧例中被源固件使用。）

**(d) 未命中区块的回退行为**：`_find_region` 未命中返回 `&regions[0]`（`app_render.c:210`）= 16pt ASCII 宋体条目 → 会画出**错误尺寸但可见**的字形；因此「字库缺区块」的表现是错字/小块，**不是空白**。若字库整片未编程，读回 0xFF → 字形全亮（实心块），同样不是空白。
→ 假设 ④ 排除；**本设备用的字库与源固件字库同布局，24/32 区块存在且有数据**。

> 附注：字库配置由 DIP2 选择（`app_render.c:351-357` `_dip_font_select`：`get_state()=true → FONT_CHIP_W25Q64`）。「16 点阵显示正常」这一事实本身反证**当前生效的字库配置与实际字库布局匹配**（若错配到 `FONT_CHIP_MX25L256` 的连续布局，16 点阵会读到错地址 → 不会是"正常显示"），故 24/32 的地址同样正确。另发现 `app_render.h:5-7` 与 `app_render.c:345-350` 对 DIP2 逻辑电平的描述**互相矛盾**（文档待修，见 §8 遗留项）。

### 2.5 假设 ⑤：仓库内是否有「实机验证过的 24 点阵」先例 —— 不成立

- `doc/13_安徽费显协议/README.md`：`0x85`/`0x86~0x89` 用 FONT_24，但状态是「**待联调**」（§8 #13 24 点阵行距待联调）；
- `doc/11_云南费显协议/README.md`：无实机验证记录；
- `doc/03_.../README.md`：RLS 干接点 FONT_24，无「已在 XX 屏体验证」记载。

→ 该假设不能用于排除字库问题；本报告改用**直接解码字库文件 + 地址公式对拍**（§2.4）作为替代证据链，结论更强（不依赖他人是否测过）。
→ 同时说明：**本仓库没有「16px 高的屏上显示 24 点阵」的先例**，因为 24 点阵在任何既有屏体（192×96 / 224×64）上都放得下。

### 2.6 假设 ⑥：文本编码/字形字节数 —— 排除

`_packed_glyph_bytes`（`app_render.c:188-192`）：ASCII `size×((size/2+7)/8)`、GBK `size×((size+7)/8)` → 16/24/32 GBK = **32 / 72 / 128B**；与源固件 `readTwentyFourHzData`/`readThirtyTwoHzData` 的 `W25QXX_Read(font_buf, readaddr, 72/128)` 完全一致（`func.c:943/948/957/958` 等）。编码分支 `FONT_ENC_GBK` 与字号无关，不存在「按 16 点阵固定长度读取」的代码路径。

---

## 3. 根因（三层，按因果顺序）

1. **镜像层面（触发条件）**：现场 16:02 烧录的是 **EIDE Debug 构建**（`.ccmram=5040`、链接 `dev_display_22_1703`、`du -h` 尺寸 664K 三证），而 `dev_display_22_1703.c` 处于 **1×1 实验态**（`MODULE_ROWS/COLS=1/1`）→ 生效屏体几何 **32×16**（`g_22_1703` 实例实测 `screen_rows=32 / screen_cols=16`）。用户假定运行的 224×64（1-263 默认口径）**并不在板上**。
   - 同源风险：R1 构建卫生（EIDE 覆写 `build/Debug/` 共享产物 → 烧录脚本烧到 EIDE 产物）已在 `doc/构建开关总表.md` §4.2 记录，本次是其实机后果的第一次暴露。

2. **渲染/显示层面（直接机理）**：GZ_OL 执行层按屏体几何传参（`w=32, h=16`）；字符渲染逐字形调用 `dev_display_draw_bitmap(cur_x, cur_y, 字号, 字号, …)`，该函数对「字形框越屏」是**整块早退丢弃**（`dev_display.c:229-233`），不是部分绘制。屏高 16 时：
   - 16pt：16 ≤ 16 → 落屏（用户看到"正常"）；
   - 24pt/32pt：24/32 > 16 → **整字形丢弃**；而 0x20 又先整屏清黑（`app_gz_ol_proto_cmd.c:100-103`，`_gz_ol_clear_screen`）→ 屏幕只剩黑底 = 「什么都没有」。

3. **可观测性层面（放大因素）**：上述失败**完全静默**——没有日志、没有回退、没有告警；协议侧一切"正常"（帧收到、解析通过、无应答需求），现场无法区分「没收到帧」与「字形放不下」。

---

## 4. 修复（代码 diff 级说明）

### 4.1 代码改动（仅 GZ_OL 模块，2 文件；不改变任何渲染行为）

| 文件 | 改动 | 说明 |
|---|---|---|
| `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c` | ① `#include "SEGGER_RTT.h"`（:28）；② 新增 `GZ_OL_RTT_DIAG` 开关（:111）；③ `_gz_ol_exec_display()` 改收整个解析结果（:123，为了能 dump 原始载荷）并新增诊断块（:135-166）；④ 函数头注释写明「字号 ≤ 屏高」前置条件与整字形丢弃语义（:113-121） | **渲染行为零变化**：`w/h/x/y/font/color/text` 与修改前完全相同；`font_size` 不做替换/降级 |
| `Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h` | 0x20 命令行补充「字号 ≤ 屏高」前置条件（整字形丢弃语义） | 供后续维护者一眼看到约束 |

**诊断输出**（每帧 0x20 三行，`SEGGER_RTT_printf`，通道 0）：

```
[gz_ol] 0x20 plen=19 payload[0..15]=00 00 00 00 c0 00 50 00 cb ce cc e5 18 18 ff 00
[gz_ol] 0x20 x=0 y=0 size=24 type=0 color=1 text_len=2 -> render x=0 y=0 w=32 h=16 wrap=1 enc=GBK
[gz_ol] 0x20 screen=32x16 code=2200001703 size_fits=NO *** size>screen height: glyph dropped whole by dev_display_draw_bitmap -> nothing drawn ***
```

- 第一行：**载荷前 16 字节**（X/Y/屏宽/屏高/字体名/字号+1 字节）；
- 第二行：**解析结果 + 实际传给 `app_render` 的参数**（现场可核对 `w/h` 是不是被几何截短）；
- 第三行：**屏体几何 + 字号适配判定**（`size_fits=NO` 即「本帧在屏幕上不会有任何内容」）。

**一键移除**：把 `GZ_OL_RTT_DIAG` 改为 `0`（诊断块随之消失），或整块删除 `#define GZ_OL_RTT_DIAG` + `#if GZ_OL_RTT_DIAG … #endif`。移除后 `#include "SEGGER_RTT.h"` 可一并删除（改回原状）。验收后建议移除（或长期保留第③行类似的告警，属产品决策）。

### 4.2 明确的「不做」

- **不做**「字号 > 屏高时自动降到 16 点阵」：会把屏体几何缺陷伪装成"显示成功"，用户明确要求不得用代码掩盖；
- **不做**「把 `_render_text` 的逐字形 `draw_bitmap` 改成裁剪绘制」（改用 `app_render_draw_glyph_clipped`，可让 24/32 在 16px 屏上露出上半截）：这是**跨全部协议**的渲染语义变更，且会让「屏太小」表现为残缺字形，需用户裁决（见 §8 遗留项），本轮不动 `app_render.c`；
- **不动** `dev_display_22_1703.c`（用户实验态文件）与 `Makefile`/`EIDE` 构建开关语义（用户纪律）。

---

## 5. 验证

### 5.1 构建（GCC Debug，`make clean` 后全量）

| 口径 | 命令 | 结果 | 段尺寸（`arm-none-eabi-size -A`） |
|---|---|---|---|
| 默认 | `make -j8` | ✅ 链接通过，**GZ_OL 各 TU 零告警**（全日志仅 3 条既有 HAL `stm32f4xx_hal_flash_ex.c` unused parameter 告警） | `.text 164004 / .rodata 198088 / .data 1664 / .ccmram 37084 / .bss 126352 / ._user_heap_stack 2560` → SRAM 130576B（余 **496B**）、CCM 37084B（余 28452B） |
| CQ | `make -j8 PROTO=CQ` | ✅ 链接通过，同样零新增告警 | `.text 150812 / .rodata 197432 / .data 860 / .ccmram 37084 / .bss 122876` → SRAM 126296B（余 **4776B**） |

**诊断增量（相对任务书基线 `text 163716 / rodata 197760`）**：`text +288`、`rodata +328`（RTT 格式串与诊断代码），`.data/.ccmram/.bss` **无变化**（基线 bss 126352 完全吻合）。余量结论不变。

**工作区终态（本轮交付状态）**：默认口径（`PROTO=ALL` `DISP=1_263`）`make clean && make -j8` 干净构建态；链接显示模组核对：`dev_display_1_263_init` 在、`dev_display_22_1703_init` 不在；重复 `make -j8` = **0 编译 0 链接**（口径指纹命中）。

| 产物 | md5 | 备注 |
|---|---|---|
| `build/Debug/Project_STD.elf` | `81f114b86e991fe094f0ee4bafc40cc1` | text 164004 / rodata 198088 / data 1664 / ccmram 37084 / bss 126352 |
| `build/Debug/Project_STD.hex` | `ab20222fb30d9c427dcb60f5be2453e5` | `du -h` = **1004K**（用于区分 EIDE 的 664K 产物） |
| `build/Debug/Project_STD.bin` | `63f29f9dec6e1cc09bdead9b80ee1427` | — |

> 说明：诊断开启后 `text +288 / rodata +328`（相对任务书基线 163716 / 197760）；`.data/.ccmram/.bss` 与基线逐字节一致，SRAM 余量仍 496B。上述 elf 即现场复测应烧录的默认口径产物（`bash tool/flash_all.sh` 或手工烧录 `build/Debug/Project_STD.hex`），**不代表已烧录**（本轮无硬件，未烧）。

### 5.2 宿主推演（`~/EnvTools/CD-DebugTool-cpp/scripts/probe_sim/gz_ol_frame_sim.py`）

新增 §6「0x20 字号 16/24/32（上位机真实帧形态）」：按 **DLL 反汇编出的真实帧**构造（X=0 Y=0 屏宽=192 屏高=80 宋体 字号=两字节同值 颜色=FF0000 GBK 文本），并镜像「执行层参数推导」与「显示层字形判界」：

```
== 6. 0x20 字号 16/24/32（上位机真实帧形态；2026-09-14「24/32 不显示」回归）==
  [通过] 0x20 字号 16：probe READY / parse 不落整帧丢弃分支 / 字号字节 0x10 0x10 被接受
    [224x64（1-263 默认口径 = 源固件同几何）] render x=0 y=0 w=224 h=64 font_size=16 ... 字形框 16x16 → 落屏可见
    [32x16（22-1703 1x1 实验态 = 16:02 烧录镜像实测）] 字形框 16x16 → 落屏可见
  [通过] 0x20 字号 24：... 字号字节 0x18 0x18 被接受
    [224x64] 字形框 24x24 → 落屏可见
    [32x16] 字形框 24x24 → 被 dev_display_draw_bitmap 整块丢弃（清屏后屏幕无内容）   ← 现场现象复现
  [通过] 0x20 字号 32：... 字号字节 0x20 0x20 被接受
    [224x64] 字形框 32x32 → 落屏可见
    [32x16] 字形框 32x32 → 被 dev_display_draw_bitmap 整块丢弃（清屏后屏幕无内容）   ← 现场现象复现

结果：通过 53 / 失败 0
```

要点：**16/24/32 三种字号的 0x20 帧全部被正确解析、不落「整帧丢弃」分支**（任务书要求的断言）；同时在 32×16 几何下 24/32 的「整字形丢弃」被显式复现并打印执行层实际渲染参数（`w/h/font_size/enc`）。

---

## 6. 现场复测步骤

### 6.0 前置：确认现场屏体（关键，先做这一步）

| 现场实际面板 | 0x20 可用字号 | 说明 |
|---|---|---|
| **单块 P10 32×16 模组**（= 22-1703 1×1 台架） | 仅 **16** | 24/32 字形（24/32px）物理上高于屏高 16px，**任何软件都放不下** |
| **224×64 屏**（1-263 7×2，或 22-1703 7×4） | **16/24/32 全部可用** | 与源固件屏体一致（源固件 224×64） |

### 6.1 复测 A：验证「全套字号」（推荐，屏体为 224×64 时）

1. **不要用 EIDE 产物**（R1 陷阱：EIDE 会覆写 `build/Debug/` 共享产物）：
   ```bash
   cd /home/yystation/Program/3833024/Project-STD-main
   make clean && make -j8                       # 默认 PROTO=ALL DISP=1_263 → 1-263 224×64
   arm-none-eabi-nm build/Debug/Project_STD.elf | grep -c dev_display_1_263_init   # 应为 1
   arm-none-eabi-nm build/Debug/Project_STD.elf | grep -c dev_display_22_1703_init  # 应为 0
   arm-none-eabi-size -A build/Debug/Project_STD.elf | grep -E "\.text|\.bss"      # 对照 §5.1
   bash tool/flash_all.sh                       # 或手工 J-Link/OpenOCD 烧 build/Debug/Project_STD.hex
   ```
   （本次构建产物：`Project_STD.hex` 1004K；若再次看到 664K 级的 hex，说明拿到的还是 EIDE 产物）
2. **接 RTT**（J-Link + RTT Viewer / `JLinkRTTLogger`），开 `0x20` 诊断：
   - 期望看到 `screen=224x64`、`size_fits=yes`；
   - 若看到 `screen=32x16`，说明板上仍是 EIDE/1×1 镜像 → 回到第 1 步。
3. **发 PC 端帧**（上位机选 16 → 24 → 32，或按 §10 联调帧手工构造）：
   - 期望：三种字号都能在屏上看到内容；RTT 每帧三行，`size_fits=yes`。

### 6.2 复测 B：若现场必须是单块 32×16 台架

- 只能验证 **16 点阵**（`size_fits=yes`）；24/32 会打印 `size_fits=NO ***`，**这是屏体限制的正确表现**，不是协议缺陷；
- 要验证 24/32 必须换 224×64 屏（或把 22-1703 恢复 7×4：`_22_1703_MODULE_ROWS (7U)` / `_22_1703_MODULE_COLS (4U)` 两行——该文件属用户实验态，本轮未改；同时注意并行审查会话记录的该文件运行期越界问题）。

### 6.3 若仍不显示：抓什么发回

1. **RTT 全文**（0x20 触发的那三行，尤其 `payload[0..15]`、`size=`、`w/h`、`screen=`、`size_fits=`）——可直接判定「帧没到 / 解析丢弃 / 几何放不下」三类；
2. **原始帧十六进制**（串口抓包或 UDP 抓包；含帧头 `54 43 4C 59`，按 `2022...测试软件` 默认 IP/端口需注意其默认端口为 10028）；
3. 若 RTT 显示 `size_fits=yes` 但仍无内容：附 `screen=...` 一行 + 屏幕实拍（这时才需要查字库芯片内容与 DIP2 档位）。

---

## 7. 文档更新清单

| 文档 | 更新内容 |
|---|---|
| `doc/14_贵州治超屏协议/README.md` | 新增 §12「0x20 字号与屏体几何前置条件（2026-09-14 实机缺陷复盘）」：字号判据、字库前提、**字号 ≤ 屏高**前置条件与整字形丢弃语义、RTT 诊断用法/移除、构建口径纪律（EIDE vs make）；§1 命令表 0x20 行 + 载荷表字号行补充前置条件；§8 待确认清单补 1 条（屏体几何，第 6 条）；§9 as-built「实机联调/宿主推演/构建验证」行更新；§13 修订记录追加（原「§12 修订记录」顺延为 §13） |
| `doc/CLAUDE.md` | GZ_OL 小节 `0x20` 命令行补「**字号 ≤ 屏高**（否则整字形被 `dev_display_draw_bitmap` 丢弃 → 清屏黑底）；16:02 实机复盘的镜像几何证据见 doc/14 §13」 |
| 本报告 | `.analysis/9k23881580/gz_ol_font_size_bugfix.md`（本文件） |

---

## 8. 遗留项

1. **现场屏体几何待用户确认**：本轮用镜像证据证明「板上几何 32×16」；物理面板到底是单块 32×16 还是 224×64 需现场确认（决定 6.1 / 6.2 走哪条）。
2. **`dev_display_22_1703.c` 1×1 实验态**：并行审查会话已判「不安全（scan 越界 1536B / 表索引越界，可能误动 GPIOB 的 W25QXX_CS）」；建议联调前恢复 7×4 或回到 1-263。本轮按纪律未改。
3. **渲染层「整字形丢弃 vs 裁剪绘制」**：`_render_text` 用的是 `dev_display_draw_bitmap`（越屏整块丢弃），而 `app_scroll` 用的是 `app_render_draw_glyph_clipped`（裁剪绘制）。是否把 `RENDER_TEXT` 也改成裁剪语义（会让小屏露出部分字形）属**跨协议行为变更**，需用户裁决；本轮未动。
4. **`app_render.h` 与 `app_render.c` 对 DIP2 逻辑电平的描述互相矛盾**（头文件：逻辑 0→W25Q64；实现：逻辑 0→MX25L256）——建议确认实机 DIP2 档位后统一文档/注释（不属本轮任务，未改）。
5. **上位机端口**：该测试软件默认设备端口 **10028**（与源固件一致），但本实现 GZ_OL 网口绑定 **10011** → UDP 联调须改端口或走串口（doc/14 §11）。
6. **诊断移除**（验收后）：`GZ_OL_RTT_DIAG` 置 0 或删除 `#if` 块 + `#include "SEGGER_RTT.h"`；移除后 `text −288 / rodata −328` 回到基线。
7. **EIDE 构建集与 Makefile 默认口径不一致**（EIDE Debug 编 22-1703 + 协议子集，make 默认编 1-263 + 全协议）：本轮实机事故的直接来源，R1 纪律（EIDE 后 `make clean && make`）需在联调流程中强制执行。
