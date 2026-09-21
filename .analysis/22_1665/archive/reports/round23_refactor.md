# 第二十三轮（2026-09-17）：22-1665 驱动重构（宏控几何 / 去 RTT / 去探针）——行为零回归

> 输入：用户六点要求 —— ① 驱动格式与其它模组对齐；② 显示已完全正常（无下半屏问题，朝向定案）；
> ③ 去掉驱动内 RTT；④ 用 `_22_1665_MODULE_ROWS` / `_22_1665_MODULE_COLS` 宏控整屏尺寸（语义与
> 兄弟驱动一致）；⑤ 注释瘦身（推理/验证过程归文档）；⑥ 模组选编仍走 EIDE 添加/排除（不在驱动里加开关）。
> 硬约束：**绝不允许行为回归**。

## 1. 结果一览

| 项 | 重构前 | 重构后 |
|---|---|---|
| 文件行数 | 1566 行（文件头 61 行） | **376 行（文件头 34 行）** |
| 几何入口 | `_22_1665_SCREEN_W/H`（+ 兼容模式 `BLK_COLS`） | **`_22_1665_MODULE_ROWS` / `_22_1665_MODULE_COLS` + 单模块像素宏**（可 `-D` 覆盖） |
| 链定义 | `_22_1665_chains[]`（name/mask/role/x0,y0,w,h/flags/dst + 3 个 `_22_1665_ROW*` 宏） | `_22_1665_chains[]`（name/`const hub75_pin_t*`/role）+ **区域由链号推导** |
| RTT | `_22_1665_RTT_DIAG` + 链表/自检/探针 6 类打印 | **全删（含 `SEGGER_RTT.h` / `pl_gpio.h` 包含）** |
| 探针 | 7 / 8 / 9 + 上电引脚回读自检 + 0..6 图案（约 700 行） | **全删**（旧驱动已归档，可随时取回标定） |
| 兼容口径 | `_22_1665_MULTI_CHAIN=0`（16×8 单链同流） | **删除**（16×16 四链为唯一形态） |
| 落点镜像旗标 | `_22_1665_XF_*`（16 组合）+ 显式落点表逃生口 | **删除**（恒等 = 已确认模型；朝向已定案） |
| 开关 | 12+（探针/掩码/极性/蓝/自检/RTT/兼容…） | **2 个**：`_22_1665_BLUE_AS_LIT`、`_22_1665_DATA_ACTIVE_HIGH` |
| CCMRAM | 1792B（本模组） | **1792B（逐字节同）** |
| `.text` / `.rodata` | 172460 / 202896 | **170692 / 201744**（↓ 1768 / 1152 = 删除的 RTT 串与探针/自检代码） |
| 选编 | Makefile `DISP=22_1665` / EIDE excludeList | **未变**（按用户要求不动 EIDE） |

## 2. 几何宏（与兄弟驱动同语义）

```c
_22_1665_MODULE_ROWS      (1U)   /* 每行模块数（水平） */
_22_1665_MODULE_COLS      (1U)   /* 每列模块数（垂直） */
_22_1665_MODULE_PIXEL_ROW (16U)  /* 单模块每行像素数 */
_22_1665_MODULE_PIXEL_COL (16U)  /* 单模块每列像素数 */
→ _22_1665_SCREEN_ROWS = MODULE_ROWS × MODULE_PIXEL_ROW   （16）
→ _22_1665_SCREEN_COLS = MODULE_COLS × MODULE_PIXEL_COL   （16）
→ _22_1665_CHAIN_COUNT = 4 × MODULE_ROWS × MODULE_COLS    （4）
→ 链 i 区域（推导）：module = i/4 → (m % ROWS, m / ROWS) 模块栅格位；
                     half = (i%4)/2 → 上半/下半；区域 = (mx·PIXEL_ROW, my·PIXEL_COL + half·HALF_COLS, PIXEL_ROW, HALF_COLS)
```

`_Static_assert` 共 10 条把关：模块数 ≥1；单模块宽为 4 的倍数、高为偶数、半屏高 ≥4 且为 4 的倍数；
**每条链区域恰 8 块 = 128 位（单模块须 256 像素）**；链数 1..8（状态字节 ≤256）；像素数/帧位数
不超 65535；端口槽 ≥2；**CCM 预算 ≤16KB**；链表行数 = 4 × 模块数。

扩屏路径（`check_compile_matrix.sh` 实测）：`-D_22_1665_MODULE_ROWS=2` + 链表现在序补 4 行
（模块 1 的 四链）→ 编译通过（8 链 / 32×16 / states=256 / CCM 8832B）；未补行 → 断言拦下并提示补行。

## 3. 行为零回归证明（`check_equivalence.py`，46/46）

对照旧模型 = `archive/prev_round/dev_display_22_1665_round23_pre_refactor.c`（默认口径 MULTI_CHAIN=1、
探针关）；新模型 = 现行驱动。8 组测试图案：红「口」/绿「口」/全白/单点/上半屏满红/下半屏满绿/八色棋盘/伪随机。

| 对照项 | 结果 |
|---|---|
| 落点表 `chain_dst[4][128]`（512 项） | **逐字节一致** |
| `prepare` 产物 `frame_state[128]`（8 图案） | **逐字节一致**（红「口」52 点、全白 512 点、单点 1 点…） |
| 合并写表 `g_bsrr_tab[端口][状态]`（2 端口 × 16 状态） | **逐字节一致**（端口槽顺序 G→B 亦同） |
| 每帧逐时钟位 (端口, BSRR) 写序列（8 图案 × 256 次/帧） | **逐字节一致** |
| `set_row(0)` 的 A/B/C/D 引脚电平 | **一致（全低）** |
| `_22_1665_scan` 机器码（同编译命令、`-Og`） | **51 条指令逐条一致** |
| 链 数据脚/颜色角色/区域（4 条） | **逐条一致**（mask 0x001/0x002/0x008/0x010 ↔ R1/G1/R2/G2） |

其余函数（`prepare` / `build_bsrr_table` / `set_row` / `region_offset`）实现策略不同（去掉了掩码与
旗标参数），机器码不同，但**输出语义由上面各项逐字节覆盖**。

## 4. 构建与产物

- `make clean && make -j8 DISP=22_1665`：**零新增告警**（全项目仅 HAL `stm32f4xx_hal_flash_ex.c` 3 条预存）。
- 段尺寸：`.text 170692 / .rodata 201744 / .data 1672 / .ccmram 10460（本模组 1792）/ .bss 126436 /
  _user_heap_stack 2564`，SRAM 合计 **130672B（余 400B）**。
- 本模组 CCM 符号：`chain_dst 0x400` + `pixel_map 0x100` + `frame_state 0x80` + `g_bsrr_tab 0x180` = **1792B**。
- tree **`f0fa31df`**；elf `1b89bccf9a0b8f02a4f997f0394952e2`、hex `5740f3cd22ee843a622a872a67f4a937`、
  bin `d8c25a8d29122bac2efad746aff9306c`（横幅内嵌 `__DATE__/__TIME__`，重编必变）。
- 宿主全绿：`verify_all.sh` 7/7（equivalence 46/46、driver 40/40、compile-matrix 11/11、
  flash-artifact 6/6、link_variant 蓝策略/反相/32×8 各 1/1）。
- 归档脚本复跑（对重构前备份）：`check_round20.py` 58/58、`check_round21.py` 23/23、
  `check_multichain.py` 31/31、`check_model_matrix.sh` 96/96。

## 5. 脚本与文档

- **新增**：`check_equivalence.py`（本轮零回归对照）、`check_driver.py`（结构自检）、
  `check_compile_matrix.sh`（几何/开关编译矩阵）。
- **更新**：`verify_all.sh`（新 7 项）、`check_flash_artifact.sh`（链名 blob + 去 RTT 证据 + tree）、
  `link_variant.sh`（改用新开关做换口径重链）、`analyze_board_flash.py`（链名 blob / 旧行签名 /
  去 RTT 判别）、`README.md`（现行状态与现场判读）。
- **归档**：`check_round20/21.py`、`check_multichain.py`、`check_model_matrix.sh`（其 `SRC` 固定指向
  `archive/prev_round/…round23_pre_refactor.c`，复跑全通过）；`round20_screens.md`、`round21_diagnosis.md`、
  `round22_cleanup.md`、`multichain_screens.md` 移入 `archive/{reports,scripts}`。
- **文档**：`doc/01` §0（几何宏表 / 去 RTT 后的现场核对方式 / 构建基线 / 遗留项清空）+ 附录 A.11；
  `doc/CLAUDE.md` 22_1665 条目；`doc/构建开关总表.md`（DISP 条目 + 修订记录）。

## 6. 现场复验建议（一次烧录）

1. `make clean && make -j8 DISP=22_1665` → `bash .analysis/22_1665/check_flash_artifact.sh` → 烧录。
2. 看 `[diag]` 横幅：`display screen=16x16 code=2200001665 scan_lines=1`；`tree=` 与本地一致即镜像正确
   （**没有 `[diag]` 行 = 板上仍是旧镜像**）。
3. 验收：红「口」整幅 52 点、绿「口」整幅 52 点（上半 23 + 下半 29）。
4. 以后改整屏尺寸：只改 `_22_1665_MODULE_ROWS` / `_22_1665_MODULE_COLS`（+ 单模块像素宏），
   并按模块数在 `_22_1665_chains[]` 补 4 行/模块。
