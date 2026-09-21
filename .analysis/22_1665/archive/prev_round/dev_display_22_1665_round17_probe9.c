/**
 * @file    dev_display_22_1665.c
 * @brief   22-1665 模组驱动 — 16×16 红绿双色 / 静态扫描 / **多链独立位流**（4 链 × 8 片 MBI5034B）
 *
 * 料号 2200001665，列驱动 MBI5034B（16 通道恒流下沉 + 16 位移位寄存器 + 输出栓锁，
 * SDO→SDI 级联）。实现 dev_display_ops: prepare（pixel_map → 每链位流 / 帧状态数组）+
 * scan（逐时钟位查表 + 合并写数据脚 + CLK 脉冲）；链位→屏面位置映射表与「状态→BSRR」表
 * 放 CCMRAM，init 期生成，扫描热路径只做「查表 + 按端口写 BSRR + 时钟」。
 *
 * =============================================================================
 *  第十六轮（2026-09-17 下午）：多链独立位流框架（本轮改动主体）
 * =============================================================================
 *  现场事实（本轮全部设计的起点）：
 *    ① **绿色灯珠亮了** —— 其余链确实被驱动、绿 dies 真实存在且可达；
 *    ② 默认画面「口」**被切成两半/分块** —— 上一版把**同一份 128 位帧**同时打到全部
 *       候选脚，四条链各自显示同一份内容（同一内容在各自区域各出现一份）；
 *    ③ 面板口径：**16×16 像素、双色（红 + 绿）、512 颗 LED = 32 片 MBI5034B
 *       = 4 条链 × 8 片**，排线 16 针全接。
 *
 *  本轮把「四条链共用一帧」升级为**每条链喂自己那片的内容**：
 *
 *   · **链表**（§二，本文件唯一的标定块）：每条链一行 = 数据脚掩码（`line_mask`）/
 *     颜色角色（`role` 红或绿）/ 覆盖区域（`x0,y0,w,h`）/ 可选显式落点表（`dst`）。
 *     默认 4 行按「R/G 成对覆盖上/下半屏」**占位**（★待现场标定，一行一改★）。
 *   · **帧发射**：一帧仍是 128 个时钟位；每个时钟位把 N 条链各自的第 p 位**同时**
 *     写到各自的数据脚（共享 CLK/LAT）——**必须并行喂，不能分时**（分时会让其余链的
 *     移位寄存器吃进垃圾）。N 条链的电平合并成一个「状态字节」→ 每时钟位仍是
 *     按 GPIO 端口合并的 ≤3 次 BSRR 写，与单链版同量级（见 §十 指令数估算）。
 *   · **prepare**：从 16×16 的 `pixel_map` 为每条链生成自己的 128 位——红链取像素 R 位、
 *     绿链取 G 位（黄 = 两位都亮）；蓝按「非黑即亮」策略让红绿两块都亮（`_22_1665_BLUE_AS_LIT`，
 *     说明见 §二）。结果直接写成「帧状态数组」`_22_1665_frame_state[128]`（1B/时钟位），
 *     scan 逐位取值即可，热路径不解析颜色。
 *   · **屏面几何**：16×16（256 像素 = 256 红 die + 256 绿 die = 512 LED = 4 链 × 128 位）。
 *   · **一键回退**：`_22_1665_MULTI_CHAIN = 0` → 退回第十二~十五轮行为（16×8 单链、
 *     `_22_1665_DATA_LINES` 指定的脚同流、颜色「非黑即亮」、prepare 恒等拷贝），
 *     用于现场对照与故障回退（见 §二 开关说明）。
 *
 * =============================================================================
 *  第十七轮（2026-09-17 傍晚）：现场两条新观测 → 根因判定 + 默认表修正 + 探针 9
 * =============================================================================
 *  现场新观测（用户烧第十六轮多链版后的实测）：
 *    ① 默认画面改成 **绿色**「口」→ **全屏一点不亮**；
 *    ② 默认画面（**红色**「口」）→ **只有单块模组的前 8 行**（= 上半屏）显示「口」的上半部
 *       （y=3 一整条 x=3..13；y=4..7 只有 x=3 与 x=12,13 —— x=12/13 双双点亮是**字形本身**
 *       的左 1 px / 右 2 px 笔画，不是驱动问题），**y≥8 全黑**。
 *
 *  反解（两条观测 + 历史事实一起推）：
 *    · 观测② ⇒ 链表里的 **链 2（B1，下半屏红）不导通**：只有链 0（R1，上半屏红）真在工作；
 *    · 观测① ⇒ 链表里的 **链 1（G1）/ 链 3（R2）也不导通**：绿色内容只喂给这两条链
 *      （红链取 R 位 = 0 ⇒ 上半屏也随之正确地变黑），两条都不亮 ⇒ 全屏黑；
 *    · 观测① 同时**验证了取色/映射逻辑本身正确**（绿内容下红链取 0 → 上半屏「正确地变黑」，
 *      若取色或落点有 bug，上半屏不会这样表现）；
 *    · 结合历史：第十三轮六根 RGB 脚（0x3F）同流显示图案时「**无任何新亮区域**」、
 *      全 1 十脚同流时「**整屏 16×16 全亮**」（512 颗都亮 ⇒ 四条链确实都挂在这 10 根候选脚内）
 *      ⇒ **其余三条链的数据入口不在六根 RGB 脚上，而在未被链表占用的地址脚 A/B/C/D**
 *      （本模组静态扫描、行址脚空闲，板卡把它们当数据线用 —— 第十四轮的假设由此获得现场证据）。
 *
 *  本轮改动（仍只改本文件）：
 *    · **链表默认值修正**：链 0 = R1 不变（现场两次确认）；链 1/2/3 由 G1/B1/R2（已证不导通）
 *      改挂 **A / B / C**（角色/区域维持「上绿 / 下红 / 下绿」）——**这是排除法得出的推定值，
 *      不是结论**：真正的标定用下面的探针 9 一次烧录读全（§十-3）。
 *    · **每链落点镜像旗标**（`_22_1665_ROW_XF` 的 `flags`；恒等 = 0 = 已确认模型）：某条链的
 *      片内位序/块栅格若呈镜像/旋转（下半屏 PCB 常见 180° 旋转），一处改旗标即可，无需手写表。
 *    · **新增探针 9「逐链 × 逐脚内容扫描」**：一次烧录（约 24s 一轮、循环），每次只驱动**一根**
 *      候选脚（其余写 0）并送**某条链自己的内容**，同时在链 0 区域显示两位数字
 *      （左 = 链号 0..3、右 = 驱动内部脚号 k：0..5 = R1/G1/B1/R2/G2/B2、6..9 = A/B/C/D）
 *      → 用户看到「哪块屏出现正确
 *      内容/什么颜色」即读出该链的脚 + 区域 + 颜色 + 落点朝向。
 * =============================================================================
 *
 *  （历史模型 = 第十二轮由现场两条观测反解出的唯一解，现在是**默认落点规则**：
 *    链位 p → 片 c = p/16、片内级序 i = p%16；块栅格 blk_cols × blk_rows（每块 4×4 LED）；
 *    `bc = (blk_cols-1) - (c % blk_cols)`、`br = c / blk_cols`（块列反向）；
 *    块内 `dx = 3 - (i%4)`、`dy = ((i/4)+2) % 4`；屏面 (X,Y) = (x0+4bc+dx, y0+4br+dy)。
 *    区域 16×8 → 4 列 × 2 块栅格，与反解模型逐位一致；多链模式下**每条链按自己的区域**
 *    用同一规则生成落点表。）
 *
 * =============================================================================
 *  一、开关与参数总览（全部 `#ifndef` 守卫，可命令行 -D 覆盖）
 * =============================================================================
 *    _22_1665_MULTI_CHAIN   1 = 多链独立位流（默认）；0 = 单链兼容（第十二~十五轮行为）
 *    _22_1665_CHAIN_COUNT   链数，默认 4（1..8；链表超出/不足由运行时校验告警）
 *    _22_1665_CHAIN_PROBE   链序探针 0..9（默认 0 = 关；开启后 scan 忽略显示内容）
 *                            1 走点 / 2 区间全亮 / 3 前半链 / 4 片锚点 / 5 全链点亮 /
 *                            6 等价 5（历史编号） / 7 逐脚标识 / 8 逐脚闪烁 + 纹理 /
 *                            9 **逐链 × 逐脚内容扫描**（第十七轮新增，标定首选：一次烧录读全
 *                              「哪根脚 ↔ 哪块屏 / 哪种颜色 / 落点朝向」，见 §十-3）
 *    _22_1665_DATA_LINES    **探针候选脚掩码**（10 位；默认 0x3FF = 全部候选脚）。
 *                            多链模式下正常显示**不用**它（各链用自己的 line_mask）；
 *                            **兼容模式**下它就是「全部同流脚」。
 *    _22_1665_DATA_ACTIVE_HIGH  数据极性（默认 1；补色图案时改 0）
 *    _22_1665_BLUE_AS_LIT   双色屏无蓝 die：1 = 蓝分量让红绿都亮（默认）；0 = 丢弃蓝分量
 *    _22_1665_BLK_COLS      **仅兼容模式**的块栅格列数（1/2/4/8 → 屏面 4×32 / 8×16 / 16×8 / 32×4）
 *    _22_1665_PIN_SELFTEST / _22_1665_RTT_DIAG  上电引脚回读自检 / RTT 输出（默认均 1）
 *    _22_1665_PROBE8_*      探针 8 的停留/闪烁/帧率参数
 *
 * =============================================================================
 *  二、链表（★唯一标定块★）与颜色语义
 * =============================================================================
 *  每条链一行（宏 `_22_1665_ROW(名字, 数据脚掩码, 颜色角色, x0, y0, w, h)`）：
 *
 *    名字        诊断短名（RTT / 对照表用）
 *    数据脚掩码  10 位候选脚（bit0..5 = R1/G1/B1/R2/G2/B2，bit6..9 = A/B/C/D）；可多根
 *    颜色角色    _22_1665_ROLE_RED（取像素 R 位）/ _22_1665_ROLE_GREEN（取像素 G 位）
 *    x0,y0,w,h   该链覆盖的屏面区域（像素，行主序坐标；**w/h 须为 4 的倍数**且
 *                (w/4)×(h/4) = 8 块，否则该链被 init 期校验禁用并打 RTT 告警）
 *
 *  可选逃生口：把 `_22_1665_ROW` 换成 `_22_1665_ROW_DST(..., 落点表指针)`，用一张
 *  显式 128 项「链位 → pixel_map 偏移」表覆盖默认落点规则（非矩形/异形排布用）。
 *
 *  颜色语义（多链模式，**真实颜色**）：
 *    · 红链：像素颜色位 bit0（R）= 1 → 该链对应位为 1；绿链：bit1（G）。
 *    · 黄（R+G）→ 两条链都亮；纯红只亮红链、纯绿只亮绿链。
 *    · 蓝（bit2）：双色屏**没有蓝 die**，按既有「非黑即亮」策略处理——默认让红绿
 *      两块都亮（`_22_1665_BLUE_AS_LIT = 1`，于是蓝/紫/青/白都显示为「红+绿」）；
 *      置 0 则丢弃蓝分量（紫 → 只红、青 → 只绿、蓝/白 → 视白为红+绿）。
 *  颜色语义（兼容模式）：维持「非黑即亮」单色口径（scan 直接判 `!= COLOR_BLACK`）。
 *
 * =============================================================================
 *  三、探针（默认全关；开启后 scan 忽略显示内容，只发探针图案）
 * =============================================================================
 *  探针是**标定设施**，与多链/兼容模式无关（两种模式下都可编可用；探针图案走
 *  `_22_1665_DATA_LINES` 掩码，而不是链表）：
 *
 *    模式 4「片锚点」：每片第 POS 级点亮 → 8 个点，读出块排布（兼容模式看 `BLK_COLS`）
 *    模式 5「全链点亮」/ 6（等价）：128 位全 1 → 不亮的位置 = 未接/坏灯
 *    模式 7「逐脚标识」（★标定首选）：每根候选脚一个**每块点数互不相同**的重复图案
 *                     （周期 = 一片 = 16 链位，点数 = k+1，块内行主序填充）：
 *                     k=0..9 → 1,2,3,4,5,6,7,8,12,16 点（R1/G1/B1/R2/G2/B2/A/B/C/D）
 *                     **点数判据对链内位序置换不变** ⇒ 即使其余链的片内位序与已知链不同，
 *                     也能按「一片 16 位亮几个」认出脚；补色图案读 16 − 点数。
 *    模式 8「逐脚闪烁 + 纹理」（判「真被驱动」还是「悬空/弱耦合」）：每根脚 3s 停留，
 *                     前 1.5s 2 Hz「全 1 帧 ↔ 全 0 帧」闪烁、后 1.5s 静态 1010… 纹理；
 *                     已知链（R1）全程显示被测脚序号（5×7 数字点阵，居中于**链 0 区域**）。
 *
 *  上电**数据脚回读自检**（`_22_1665_PIN_SELFTEST` 默认开）：init 期对**参与输出的脚**
 *  （`s_out_lines`：多链模式 = 有效链的数据脚并集；探针/兼容 = 候选掩码）逐脚
 *  「写 1 → 读回 → 写 0 → 读回」，RTT 逐脚一行，用于排除「某些脚根本没被驱动」的软件病因。
 *
 * =============================================================================
 *  设计约束
 * =============================================================================
 *  - 只改本文件即可（基类 dev_display.c / pl_hub75 / app_test / app_boot 均零改动）；
 *  - 多链模式下**未参与链表的脚一个位都不写**（保持静默电平，互不干扰）；
 *    被选作数据的地址脚由 scan 数据路径独占，`_22_1665_set_row()` 不再碰它们；
 *  - 同一根脚被两条链声明 = 位流「或」叠加（物理上不可能同时喂两份数据）→ init 期 RTT 告警。
 */

#include "dev_display.h"

#include <string.h>
#include "initcall.h"
#include "pl_gpio.h"     /* 上电数据脚回读自检（pl_gpio_write / pl_gpio_read） */
#include "SEGGER_RTT.h"  /* 自检行 / 链表白检行 / 探针 8 停留提示（未编入时仅头文件，零成本） */

/* ================================================================
 *  一、模组基本参数（片 / 位 / 块）
 * ================================================================ */
#define _22_1665_MODULE_CODE "2200001665"

#define _22_1665_CHIP_BITS (16U) /* 一片的位宽（MBI5034B 16 位移位寄存器） */
#define _22_1665_CHIPS     (8U)  /* 每条链上的片数（反解结论①：8 片 = 128 位） */
#define _22_1665_BLK_W     (4U)  /* 每块的宽（像素）：一片的 16 路输出恰好铺满一个 4×4 块 */
#define _22_1665_BLK_H     (4U)  /* 每块的高（像素） */

/* ---- 块栅格列数（**仅单链兼容模式**使用）----
 *   兼容模式（`_22_1665_MULTI_CHAIN = 0`）下屏面 = 8 块的块栅格：
 *     BLK_COLS = 1 / 2 / 4 / 8 → 屏面 4×32 / 8×16 / 16×8 / 32×4（默认 4 = 16×8）。
 *   多链模式下每条链的块栅格由**该链区域尺寸**导出（w/4 × h/4），本宏不参与。 */
#ifndef _22_1665_BLK_COLS
#define _22_1665_BLK_COLS (4U)
#endif
#define _22_1665_BLK_ROWS ((_22_1665_CHIPS) / (_22_1665_BLK_COLS))

/* ================================================================
 *  ★★★ 现场切换开关（只改这一段即可）★★★
 * ================================================================ */

/* ---- 多链独立位流总开关（第十六轮新增）----
 *  1（默认）= 多链模式：屏面 16×16、每条链按链表喂自己的 128 位、真实颜色（红链取 R / 绿链取 G）；
 *  0        = 单链兼容模式：退回第十二~十五轮行为（屏面 16×8、`_22_1665_DATA_LINES`
 *             指定的脚同流同一份 128 位、颜色「非黑即亮」、prepare 恒等拷贝）——
 *             现场对照与回退用（链表在兼容模式下只允许 1 行且 role = MONO）。 */
#ifndef _22_1665_MULTI_CHAIN
#define _22_1665_MULTI_CHAIN (1U)
#endif

/* ---- 链数（= 模组链数；32 片 / 每条链 8 片 = 4）----
 *  只有 1..8 合法（状态数 1<<N ≤ 256）；链表行数可多于链数（多余行被忽略）
 *  但**少于链数时**缺的行 = 全 0（line_mask=0 → init 期校验禁用 + 告警）。 */
#ifndef _22_1665_CHAIN_COUNT
#define _22_1665_CHAIN_COUNT (4U)
#endif

#if !_22_1665_MULTI_CHAIN
/* 兼容模式只有「唯一一条链」（整屏 + 候选脚同流，见链表 `#else` 分支）：
 * 链数收敛为 1 —— 省下 3×128×2B = 768B CCM，且使 CCM 占用与第十五轮**逐字节一致**。 */
#undef _22_1665_CHAIN_COUNT
#define _22_1665_CHAIN_COUNT (1U)
#endif

/* ---- 颜色角色 ---- */
#define _22_1665_ROLE_RED   (0U) /* 取像素 R 位（bit0） */
#define _22_1665_ROLE_GREEN (1U) /* 取像素 G 位（bit1） */
#define _22_1665_ROLE_MONO  (2U) /* 兼容模式专用：不取分量，scan 走「非黑即亮」 */

/* ---- 双色屏无蓝 die：蓝分量按「非黑即亮」处理 ----
 *  1（默认）= 蓝 bit 置位时红、绿两条链都点亮（蓝/紫/青/白 → 红+绿）；
 *  0        = 丢弃蓝分量（紫 → 只红、青 → 只绿；白仍为红+绿，因 R/G 位本就置位）。
 *  仅在多链模式生效（兼容模式那条路径本就是「非黑即亮」）。 */
#ifndef _22_1665_BLUE_AS_LIT
#define _22_1665_BLUE_AS_LIT (1U)
#endif

/* ================================================================
 *  二、屏面几何（模式相关）
 * ================================================================ */
#if _22_1665_MULTI_CHAIN
/* 多链模式：屏面 = 面板口径 16×16（= 256 像素 = 256 红 die + 256 绿 die = 512 LED）*/
#ifndef _22_1665_SCREEN_W
#define _22_1665_SCREEN_W (16U)
#endif
#ifndef _22_1665_SCREEN_H
#define _22_1665_SCREEN_H (16U)
#endif
#else
/* 单链兼容模式：屏面 = 8 块的块栅格（由 BLK_COLS 决定，默认 16×8）*/
#define _22_1665_SCREEN_W ((_22_1665_BLK_COLS) * (_22_1665_BLK_W))
#define _22_1665_SCREEN_H ((_22_1665_BLK_ROWS) * (_22_1665_BLK_H))
#endif

#define _22_1665_BUFFER_SIZE ((_22_1665_SCREEN_W) * (_22_1665_SCREEN_H)) /* 像素数（1B/像素） */
#define _22_1665_FRAME_BITS  ((_22_1665_CHIPS) * (_22_1665_CHIP_BITS))   /* 每链每帧位数 = 128 */
#define _22_1665_SEGMENTS    (1U)                                        /* 每像素 1 位/链 */
#define _22_1665_SCAN_LINE_PX (_22_1665_BUFFER_SIZE)                     /* 静态单扫：整屏一次 */

/* ---- 数据脚掩码（10 位候选脚；**探针候选 + 兼容模式同流脚**）----
 *
 *   bit0 = R1 (PG9)   bit1 = G1 (PG10)  bit2 = B1 (PG12)
 *   bit3 = R2 (PG15)  bit4 = G2 (PB6)   bit5 = B2 (PB7)
 *   bit6 = A  (PD4)   bit7 = B  (PD5)   bit8 = C  (PD6)   bit9 = D  (PD7)
 *
 *   多链模式：正常显示**不用本掩码**（每条链用自己的 `line_mask`）；本掩码是**探针 1..8
 *   的候选脚集合**（探针 7 = 逐脚标识一次读全 10 根；探针 8 = 逐脚闪烁）。
 *   兼容模式：本掩码 = 「同一份 128 位同流到哪几根脚」（默认 0x3FF 十脚全开）。
 *   硬件依据：10 根脚落在 3 个 GPIO 端口（PG / PB / PD）→ 每时钟位 ≤3 次合并 BSRR 写。
 *   A/B/C/D 同时是行址脚：本模组静态（scan_lines = 1）不用行址 → 可安全当数据线用。 */
#define _22_1665_LINE_R1 (0x001U) /* PG9  */
#define _22_1665_LINE_G1 (0x002U) /* PG10 */
#define _22_1665_LINE_B1 (0x004U) /* PG12 */
#define _22_1665_LINE_R2 (0x008U) /* PG15 */
#define _22_1665_LINE_G2 (0x010U) /* PB6  */
#define _22_1665_LINE_B2 (0x020U) /* PB7  */
#define _22_1665_LINE_A  (0x040U) /* PD4（行址脚，第十四轮纳入数据脚候选） */
#define _22_1665_LINE_B  (0x080U) /* PD5（行址脚） */
#define _22_1665_LINE_C  (0x100U) /* PD6（行址脚） */
#define _22_1665_LINE_D  (0x200U) /* PD7（行址脚） */

#ifndef _22_1665_DATA_LINES
#define _22_1665_DATA_LINES   (_22_1665_LINE_ALL) /* 默认 = 10 根全开（探针候选 / 兼容同流） */
#endif
#define _22_1665_LINE_COUNT   (10U)    /* 候选脚根数（掩码位宽） */
#define _22_1665_LINE_ALL     (0x3FFU) /* 10 根全开 */
#define _22_1665_RGB_COUNT    (6U)     /* 六根 RGB 数据脚 */
#define _22_1665_RGB_LINES    (0x03FU) /* 六根 RGB 数据脚掩码（bit0..5） */
#define _22_1665_ADDR_COUNT   (4U)     /* 四根行址脚 A/B/C/D */
#define _22_1665_ADDR_LINES   (_22_1665_LINE_A | _22_1665_LINE_B | _22_1665_LINE_C | _22_1665_LINE_D)
#define _22_1665_LINE_TO_CH(k)  ((uint8_t)((k) / 3U)) /* 排线组号：0=第1组(R1/G1/B1) 1=第2组 */
#define _22_1665_LINE_TO_SIG(k) ((uint8_t)((k) % 3U)) /* 组内信号：0=R / 1=G / 2=B */

/** 数据极性：1（默认）= 链上 1 → 数据脚高（MBI 系列恒流下沉口径：数据 1 点亮）；
 *  0 = 反相（该亮的灭、该灭的亮）。全部数据脚共用同一极性（出现补色图案时改这里）。 */
#ifndef _22_1665_DATA_ACTIVE_HIGH
#define _22_1665_DATA_ACTIVE_HIGH (1U)
#endif

/* ================================================================
 *  ★★★ 链表 —— 本文件唯一的现场标定块（每条链一行，「一行一改」）★★★
 *
 *  现场标定（两条判据 + 一张表，详见 doc/01 22-1665 记录 §19.4）：
 *    ① 用探针 7（逐脚标识）读出「哪根脚 → 哪块屏」：亮起来的区域里，一块 4×4 里亮
 *       几个灯 = k+1 → k 即候选脚序号 → 填进该链的 `line_mask`（1 << k）；
 *    ② 用 TEST 键全屏纯色测试读出「哪块屏是哪种 die」：全屏红时某区域显示**绿**
 *       ⇒ 该链的 `role` 填反了（应为 GREEN）；显示红 ⇒ RED；
 *    ③ 区域 `x0,y0,w,h` = 该链实际覆盖的屏面范围（坐标口径 = 已知链（R1）在 §15 反解
 *       时确立的屏面坐标；典型值：上半屏 = (0,0,16,8)、下半屏 = (0,8,16,8)、
 *       左半屏 = (0,0,8,16)、右半屏 = (8,0,8,16)）。
 *
 *  ★★ 下面 4 行：链 0 = R1（**现场两次观测确认**）；链 1/2/3 = A/B/C 是**排除法推定值**
 *     （G1/B1/R2 已被现场观测排除，见 §十七轮 反解），**不是结论** —— 权威标定见探针 9。★★
 * ================================================================ */
typedef struct {
    const char *name;        /* 诊断短名（RTT / 对照表用） */
    uint16_t line_mask;      /* 该链的数据脚（10 位候选掩码；bit k ↔ R1/G1/B1/R2/G2/B2/A/B/C/D） */
    uint8_t role;            /* _22_1665_ROLE_RED / _22_1665_ROLE_GREEN（兼容模式 = MONO） */
    uint8_t x0, y0;          /* 覆盖区域左上角（像素） */
    uint8_t w, h;            /* 覆盖区域尺寸（像素；须为 4 的倍数、块数 = 8） */
    uint8_t flags;           /* 落点镜像旗标（`_22_1665_XF_*`；0 = 已确认的默认模型，见 §八） */
    const uint16_t *dst;     /* 可选显式落点表（128 项，链位 → pixel_map 偏移）；NULL = 默认规则 */
} _22_1665_chain_t;

/* ---- 每链落点镜像旗标（`flags` 字段）----
 *  **恒等（0）= 第十二轮现场反解确认的默认落点模型**；只在本条链的片内位序或块栅格与已确认
 *  模型呈镜像/旋转时才用（例如下半屏 PCB 相对上半屏 180° 旋转 —— 现象：该区域内容上下颠倒
 *  且左右相反）。四个旗标可任意组合（16 种），对落点公式的作用见 §八：
 *    X      = 块内 dx → 3 - dx          Y      = 块内 dy → 3 - dy
 *    BLK_X  = 块列序镜像（bc → 列数-1-bc） BLK_Y = 块行序镜像（br → 行数-1-br）
 *  常用组合：
 *    · 180° 旋转（下半屏最常见的板卡旋屏情形）：`X | Y | BLK_X | BLK_Y`
 *    · 水平镜像：`X | BLK_X`；垂直镜像：`Y | BLK_Y`
 *    · 「链位顺序整体反了」（芯片级联次序与假定相反）：等价于 `BLK_X | BLK_Y`（见 §八 注释） */
#define _22_1665_XF_NONE   (0x0U) /* 恒等 = 已确认模型（默认） */
#define _22_1665_XF_X      (0x1U) /* 块内 x 镜像 */
#define _22_1665_XF_Y      (0x2U) /* 块内 y 镜像 */
#define _22_1665_XF_BLK_X  (0x4U) /* 块列序镜像 */
#define _22_1665_XF_BLK_Y  (0x8U) /* 块行序镜像 */
#define _22_1665_XF_ROT180 (_22_1665_XF_X | _22_1665_XF_Y | _22_1665_XF_BLK_X | _22_1665_XF_BLK_Y)

/** 链表行（声明式，一行一条链）：`_22_1665_ROW(名字, 数据脚掩码, 颜色角色, x0, y0, w, h)` */
#define _22_1665_ROW(nm, lmask, rl, x, y, ww, hh)                                         \
    {                                                                                     \
        .name = nm, .line_mask = lmask, .role = rl, .x0 = x, .y0 = y, .w = ww, .h = hh,   \
        .flags = _22_1665_XF_NONE, .dst = nullptr                                        \
    }

/** 链表行（带落点镜像旗标版）：`_22_1665_ROW_XF(名字, 掩码, 角色, x0, y0, w, h, 旗标)` */
#define _22_1665_ROW_XF(nm, lmask, rl, x, y, ww, hh, xf)                                  \
    {                                                                                     \
        .name = nm, .line_mask = lmask, .role = rl, .x0 = x, .y0 = y, .w = ww, .h = hh,   \
        .flags = xf, .dst = nullptr                                                       \
    }

/** 链表行（显式落点表版）：`_22_1665_ROW_DST(名字, 掩码, 角色, x0, y0, w, h, 落点表)` */
#define _22_1665_ROW_DST(nm, lmask, rl, x, y, ww, hh, tbl)                                \
    {                                                                                     \
        .name = nm, .line_mask = lmask, .role = rl, .x0 = x, .y0 = y, .w = ww, .h = hh,   \
        .flags = _22_1665_XF_NONE, .dst = tbl                                            \
    }

#define _22_1665_HALF_H ((_22_1665_SCREEN_H) / 2U) /* 占位区域用的半屏高 */

static const _22_1665_chain_t _22_1665_chains[_22_1665_CHAIN_COUNT] = {
#if _22_1665_MULTI_CHAIN
    /* 链 0 ── ★现场已确认（第十二/十七轮两次观测）★ ── 数据脚 R1、红 die、上半屏（16×8） */
    _22_1665_ROW("C0", _22_1665_LINE_R1, _22_1665_ROLE_RED, 0U, 0U, _22_1665_SCREEN_W,
                 _22_1665_HALF_H),
#if _22_1665_CHAIN_COUNT > 1
    /* 链 1 ── ★待现场标定（推定挂在地址脚 A；0x3F 六 RGB 脚同流已证不导通）★ ── 上半屏 绿 */
    _22_1665_ROW("C1", _22_1665_LINE_A, _22_1665_ROLE_GREEN, 0U, 0U, _22_1665_SCREEN_W,
                 _22_1665_HALF_H),
#endif
#if _22_1665_CHAIN_COUNT > 2
    /* 链 2 ── ★待现场标定（推定挂在地址脚 B）★ ── 下半屏 红 */
    _22_1665_ROW("C2", _22_1665_LINE_B, _22_1665_ROLE_RED, 0U, _22_1665_HALF_H,
                 _22_1665_SCREEN_W, _22_1665_HALF_H),
#endif
#if _22_1665_CHAIN_COUNT > 3
    /* 链 3 ── ★待现场标定（推定挂在地址脚 C）★ ── 下半屏 绿
     * 注：第 4 个地址脚 D 刻意留空（由 `set_row` 保持 0）——「三选一」的推定不值得再占用一根脚；
     *     若探针 9 读出某条链在 D 上，把该链的掩码改成 `_22_1665_LINE_D`（或与既有脚按位或）即可。 */
    _22_1665_ROW("C3", _22_1665_LINE_C, _22_1665_ROLE_GREEN, 0U, _22_1665_HALF_H,
                 _22_1665_SCREEN_W, _22_1665_HALF_H),
#endif
    /* 链 4..7：`_22_1665_CHAIN_COUNT` 调到 >4 时缺行 = 全 0（init 期校验禁用 + RTT 告警）——
     * 需要更多链时在下面照抄一行 `_22_1665_ROW(...)` 并改 `_22_1665_CHAIN_COUNT`。 */
#else
    /* 兼容模式：唯一「链」= 整屏；数据脚 = `_22_1665_DATA_LINES`（多脚同流）；
     * 颜色 = 非黑即亮（scan 不走角色表） */
    _22_1665_ROW("ALL", _22_1665_DATA_LINES, _22_1665_ROLE_MONO, 0U, 0U, _22_1665_SCREEN_W,
                 _22_1665_SCREEN_H),
#endif
};

/* ---- 链序探针（默认关；开启后 scan 忽略显示内容，只发探针图案）---- */
#ifndef _22_1665_CHAIN_PROBE
#define _22_1665_CHAIN_PROBE (0U) /* 0=关（默认） / 1=走点 / 2=区间全亮 / 3=前半链 /
                                   * 4=片锚点 / 5=全链点亮 / 6=全链（同上，历史编号）/
                                   * 7=逐脚标识（多数据脚，一次读出「哪根脚管哪块屏」）/
                                   * 8=逐脚闪烁+纹理（判「真被驱动」还是「悬空/弱耦合」）/
                                   * 9=逐链×逐脚内容扫描（第十七轮，标定首选，见 §十-3） */
#endif
#ifndef _22_1665_CHAIN_PROBE_POS
#define _22_1665_CHAIN_PROBE_POS (3U) /* 模式 2 区间起点、模式 4 片内级序（0..15） */
#endif
#ifndef _22_1665_CHAIN_PROBE_SPAN
#define _22_1665_CHAIN_PROBE_SPAN (16U) /* 模式 2 区间链位数（默认 = 一片 = 16 位） */
#endif
#ifndef _22_1665_CHAIN_PROBE_HOLD
#define _22_1665_CHAIN_PROBE_HOLD (200U) /* 模式 1：每位保持的 scan 次数（×500µs） */
#endif

/* ---- 探针 8 参数（仅 `_22_1665_CHAIN_PROBE == 8` 编入）----
 *  每根候选脚一个「停留」，停留内两段（半周期 = 500ms = 1000 帧 @ TIM3 500µs）：
 *    前 BLINK_HALVES 个半周期 = 2 Hz 全 1 / 全 0 交替；其余 = 静态 1010… 纹理。 */
#ifndef _22_1665_PROBE8_HALF_MS
#define _22_1665_PROBE8_HALF_MS (500U) /* 半周期毫秒（2 Hz 闪烁 = 500ms 亮/灭） */
#endif
#ifndef _22_1665_PROBE8_DWELL_HALVES
#define _22_1665_PROBE8_DWELL_HALVES (6U) /* 每根脚的停留 = 6 半周期 = 3s */
#endif
#ifndef _22_1665_PROBE8_BLINK_HALVES
#define _22_1665_PROBE8_BLINK_HALVES (3U) /* 前 3 半周期 = 闪烁段（1.5s），其余 = 纹理段 */
#endif
#ifndef _22_1665_PROBE8_FRAME_HZ
#define _22_1665_PROBE8_FRAME_HZ (2000U) /* 帧率 = 1 / TIM3 周期 500µs（与 pl_tim 一致） */
#endif

/* ---- 探针 9 参数（仅 `_22_1665_CHAIN_PROBE == 9` 编入，第十七轮新增）----
 *  「逐链 × 逐脚内容扫描」：外层遍历链（链表行号 0..3），内层遍历候选脚（默认 4 个地址脚
 *  A/B/C/D），每个组合停留 PROBE9_HALF_MS 毫秒 —— 期间**只驱动该脚**（其余写 0）并把它
 *  **自己那条链的内容**（非黑即亮）送上去；链 0 所在的 R1 脚不参与扫描（它负责显示序号）。
 *  一轮 = 链数 × 脚数 × 停留（默认 4 × 4 × 1.5s = 24s），循环播放，随时可重看。 */
#ifndef _22_1665_PROBE9_PINS
#define _22_1665_PROBE9_PINS (_22_1665_ADDR_LINES) /* 扫描的候选脚掩码：默认四个地址脚 A/B/C/D */
#endif
#ifndef _22_1665_PROBE9_CHAINS
/* 参与扫描的链掩码（bit i = 链表第 i 行）；默认 = 全部链（随链数自动收敛） */
#define _22_1665_PROBE9_CHAINS ((uint8_t)((1U << _22_1665_CHAIN_COUNT) - 1U))
#endif
#ifndef _22_1665_PROBE9_HALF_MS
#define _22_1665_PROBE9_HALF_MS (1500U) /* 每个「链 × 脚」组合的停留毫秒 */
#endif
#ifndef _22_1665_PROBE9_FRAME_HZ
#define _22_1665_PROBE9_FRAME_HZ (2000U) /* 帧率 = 1 / TIM3 周期 500µs（与 pl_tim 一致） */
#endif

/* ---- 上电数据脚回读自检（默认开）：1 = 编入，0 = 关（-D 或改这里）----
 *  与 RTT 输出开关**同时**为 1 才真正编入（否则只翻引脚却无从回报，没有意义）。 */
#ifndef _22_1665_PIN_SELFTEST
#define _22_1665_PIN_SELFTEST (1U)
#endif

/* ---- 驱动内 RTT 输出总开关（链表自检行 + 回读自检 + 探针 8 停留提示；0 = 全部不编入）---- */
#ifndef _22_1665_RTT_DIAG
#define _22_1665_RTT_DIAG (1U)
#endif

#if _22_1665_PIN_SELFTEST && _22_1665_RTT_DIAG
#define _22_1665_PIN_SELFTEST_ON (1) /* 回读自检真正编入 */
#else
#define _22_1665_PIN_SELFTEST_ON (0)
#endif

/* 需要「脚名 / 端口字母 / 位号」标签的设施（自检打印或探针 8/9 打印） */
#if _22_1665_RTT_DIAG && (_22_1665_PIN_SELFTEST_ON || (_22_1665_CHAIN_PROBE == 8) || (_22_1665_CHAIN_PROBE == 9))
#define _22_1665_PIN_LABEL_ON (1)
#else
#define _22_1665_PIN_LABEL_ON (0)
#endif

/* ---- 位时序裕量（数据脚与 CLK 不在同一 GPIO 端口，数据先于时钟建立的关键余量）----
 * 现场判为「随机散点 / 随时间变化」时，把 NOP 增到 8 / 16（帧时长 128 时钟
 * ≈ 25~35µs → 60~80µs，TIM3 500µs 周期余量仍 6 倍以上）。*/
#define _22_1665_BIT_MARGIN() \
    do {                      \
        __NOP();              \
        __NOP();              \
        __NOP();              \
        __NOP();              \
    } while (0)

/* ================================================================
 *  三、编译期防御（口径自相矛盾即编译报错，不留到实机）
 * ================================================================ */
_Static_assert(_22_1665_BLK_COLS == 1U || _22_1665_BLK_COLS == 2U || _22_1665_BLK_COLS == 4U ||
                   _22_1665_BLK_COLS == 8U,
               "BLK_COLS 只能取 1 / 2 / 4 / 8（须整除片数 8）");
_Static_assert(_22_1665_CHIPS % _22_1665_BLK_COLS == 0U, "块列数须整除片数");
_Static_assert(_22_1665_BLK_ROWS >= 1U, "块行数须 >= 1");
_Static_assert(_22_1665_CHIP_BITS == _22_1665_BLK_W * _22_1665_BLK_H,
               "一片的 16 路输出须恰好铺满一个 BLK_W × BLK_H 块");
_Static_assert(_22_1665_BUFFER_SIZE <= 65535U, "链位映射表的偏移为 uint16_t（像素数不得超 65535）");
_Static_assert(_22_1665_CHAIN_COUNT >= 1U && _22_1665_CHAIN_COUNT <= 8U,
               "链数只能取 1..8（状态数 1<<N ≤ 256）");
_Static_assert((1U << _22_1665_CHAIN_COUNT) <= 256U, "状态数超出 uint8_t 状态索引");
_Static_assert(_22_1665_DATA_LINES != 0U, "探针候选脚掩码不能为空（至少选一根）");
_Static_assert((_22_1665_DATA_LINES & ~_22_1665_LINE_ALL) == 0U,
               "候选脚掩码只有低 10 位有效：R1/G1/B1/R2/G2/B2/A/B/C/D");
_Static_assert(_22_1665_LINE_ALL == (_22_1665_RGB_LINES | _22_1665_ADDR_LINES),
               "10 根候选脚 = 6 根 RGB 数据脚（bit0..5）+ 4 根行址脚（bit6..9）");
_Static_assert(_22_1665_CHAIN_PROBE <= 9U, "探针模式只能取 0..9");
_Static_assert(_22_1665_CHAIN_PROBE != 2U || _22_1665_CHAIN_PROBE_SPAN >= 1U, "探针区间须至少 1 位");
_Static_assert(_22_1665_CHAIN_PROBE != 4U || _22_1665_CHAIN_PROBE_POS < _22_1665_CHIP_BITS,
               "片锚点级序须 < 16");
_Static_assert(_22_1665_CHAIN_PROBE != 8U ||
                   (_22_1665_PROBE8_DWELL_HALVES >= 2U && _22_1665_PROBE8_BLINK_HALVES >= 1U &&
                    _22_1665_PROBE8_BLINK_HALVES < _22_1665_PROBE8_DWELL_HALVES),
               "探针 8：停留须 ≥2 个半周期，闪烁段须 ≥1 且短于停留（否则没有纹理段）");
_Static_assert(_22_1665_CHAIN_PROBE != 8U || (_22_1665_PROBE8_FRAME_HZ * _22_1665_PROBE8_HALF_MS) >= 1000U,
               "探针 8：半周期至少 1000 帧（帧率 × 毫秒 / 1000 ≥ 1）");
_Static_assert(_22_1665_CHAIN_PROBE != 9U || (_22_1665_PROBE9_PINS != 0U),
               "探针 9：扫描脚掩码不能为空（至少选一根候选脚）");
_Static_assert(_22_1665_CHAIN_PROBE != 9U || (_22_1665_PROBE9_PINS & (uint16_t)~_22_1665_LINE_ALL) == 0U,
               "探针 9：扫描脚掩码只有低 10 位有效");
_Static_assert(_22_1665_CHAIN_PROBE != 9U || ((_22_1665_PROBE9_PINS & _22_1665_LINE_R1) == 0U),
               "探针 9：扫描脚掩码不得含 R1（R1 是序号显示器，由本文件独占）");
_Static_assert(_22_1665_CHAIN_PROBE != 9U || (_22_1665_PROBE9_CHAINS != 0U),
               "探针 9：参与扫描的链不能为空");
_Static_assert(_22_1665_CHAIN_PROBE != 9U ||
                   (_22_1665_PROBE9_CHAINS & (uint8_t)~((1U << _22_1665_CHAIN_COUNT) - 1U)) == 0U,
               "探针 9：参与扫描的链掩码超出链数");
_Static_assert(_22_1665_CHAIN_PROBE != 9U || _22_1665_PROBE9_HALF_MS >= 200U,
               "探针 9：组合停留至少 200ms（太短看不清）");
_Static_assert(_22_1665_CHAIN_PROBE != 9U || (_22_1665_PROBE9_FRAME_HZ * _22_1665_PROBE9_HALF_MS) >= 200000U,
               "探针 9：停留至少 200 帧（帧率 × 毫秒 / 1000 ≥ 200）");
#if !_22_1665_MULTI_CHAIN
_Static_assert(_22_1665_SCREEN_W * _22_1665_SCREEN_H == _22_1665_FRAME_BITS,
               "单链兼容模式：显存像素数须 = 每链位数（1 位/像素）");
#endif

/* ================================================================
 *  四、查表与映射表（CCMRAM；init 期生成，scan 热路径只查表）
 * ================================================================ */

/** 每时钟位的「数据脚状态」个数（= `g_bsrr_tab` 第二维）：
 *   · 模式 0 多链：1 << N（每个状态位 = 一条链的电平）——默认 4 链 = 16 项
 *   · 模式 0 兼容：2（全部参与脚一起灭 / 一起亮）
 *   · 模式 1..6：2（同上）
 *   · 模式 7   ：16（片内级序 i：逐脚图案以一片 = 16 链位为周期，按 i 取表即可）
 *   · 模式 8/9 ：4（(被测脚电平/内容位 << 1) | 链 0 数字位） */
#define _22_1665_STATES                                                                       \
    (((_22_1665_CHAIN_PROBE) == 7U)                                                           \
         ? _22_1665_CHIP_BITS                                                                 \
         : (((_22_1665_CHAIN_PROBE) == 8U || (_22_1665_CHAIN_PROBE) == 9U)                    \
                ? 4U                                                                          \
                : (((_22_1665_CHAIN_PROBE) != 0U)                                             \
                       ? 2U                                                                   \
                       : ((_22_1665_MULTI_CHAIN != 0U) ? (1U << _22_1665_CHAIN_COUNT) : 2U))))

/** 数据脚分布在几个 GPIO 端口：10 根候选脚落 PG（R1/G1/B1/R2）/ PB（G2/B2）/
 *  PD（A/B/C/D）三个端口（main.h 实态）→ 表按端口分组，每时钟位只写「端口数」次 BSRR（≤3）。
 *  换板导致端口数 >3 时加大本值（超容量的脚会在 init 期被静默丢弃——见 §九 防御注释）。 */
#define _22_1665_PORT_MAX (3U)

_Static_assert(_22_1665_PORT_MAX >= 3U, "10 根候选脚跨 PG / PB / PD 三个端口，表容量须 >= 3");

/** 合并写表：[端口槽][数据脚状态] → 该端口 32 位 BSRR 字
 *  （表项由 init 期把该端口上**全部参与输出脚**的置位/复位位合并而成）。 */
[[gnu::section(".ccmram")]] static pl_hub75_bsrr_t g_bsrr_tab[_22_1665_PORT_MAX][_22_1665_STATES];
static uint8_t g_bsrr_port_cnt; /* 实际参与输出的端口槽数（≤ PORT_MAX） */

/** 逐链位映射表：链 i 的链位 p → 该位驱动的像素在 pixel_map（行主序）中的字节偏移。
 *  init 期由「该链区域 + 默认落点规则」生成（或由链表里的显式落点表覆盖）。
 *  多链 = N 行（默认 4 × 128 × 2B = 1024B）；兼容模式 = 1 行（256B，等价旧 `_22_1665_dst[]`）。 */
[[gnu::section(".ccmram")]] static uint16_t _22_1665_chain_dst[_22_1665_CHAIN_COUNT][_22_1665_FRAME_BITS];

/** 四根行址脚（A/B/C/D，同为 GPIOD）的引脚描述 —— 与 CubeMX 宏绑定（改板只改 main.h）。
 *  §七 `_22_1665_set_row()`（只写未被选作数据的地址脚）与 §九 建表都要用，故放这里。 */
static const hub75_pin_t _22_1665_addr_pins[_22_1665_ADDR_COUNT] = {
    {HUB75_A_GPIO_Port, HUB75_A_Pin}, /* bit6 → PD4 */
    {HUB75_B_GPIO_Port, HUB75_B_Pin}, /* bit7 → PD5 */
    {HUB75_C_GPIO_Port, HUB75_C_Pin}, /* bit8 → PD6 */
    {HUB75_D_GPIO_Port, HUB75_D_Pin}, /* bit9 → PD7 */
};

/* ================================================================
 *  五、引脚标识与查询（建表 / 探针 8 / 上电自检共用；改板只改 main.h）
 * ================================================================ */

/** @brief 取信号引脚描述（sig: 0=R / 1=G / 2=B） */
static inline const hub75_pin_t *_22_1665_pin_of(uint8_t ch, uint8_t sig)
{
    switch (sig) {
        case 0: return &g_hub75_pin_r[ch];
        case 1: return &g_hub75_pin_g[ch];
        default: return &g_hub75_pin_b[ch];
    }
}

/** @brief 取第 k 根候选脚的引脚描述（k: 0..5 = R1/G1/B1/R2/G2/B2；6..9 = A/B/C/D） */
static inline const hub75_pin_t *_22_1665_line_pin(uint8_t k)
{
    if (k >= _22_1665_RGB_COUNT)
        return &_22_1665_addr_pins[k - _22_1665_RGB_COUNT];
    return _22_1665_pin_of(_22_1665_LINE_TO_CH(k), _22_1665_LINE_TO_SIG(k));
}

#if _22_1665_PIN_LABEL_ON
/** @brief 十根候选脚的短名（bit k ↔ 名字；RTT 与屏上数字共用同一序号口径） */
static const char *const _22_1665_line_names[_22_1665_LINE_COUNT] = {
    "R1", "G1", "B1", "R2", "G2", "B2", "A", "B", "C", "D",
};

/** @brief GPIO_TypeDef* → 端口字母（'A'..'H'；未知 '?'，仅诊断打印用） */
static char _22_1665_port_char(const GPIO_TypeDef *port)
{
    if (port == GPIOA) return 'A';
    if (port == GPIOB) return 'B';
    if (port == GPIOC) return 'C';
    if (port == GPIOD) return 'D';
    if (port == GPIOE) return 'E';
    if (port == GPIOF) return 'F';
    if (port == GPIOG) return 'G';
    if (port == GPIOH) return 'H';
    return '?';
}

/** @brief 引脚掩码（GPIO_PIN_x）→ 位号（0..15） */
static uint8_t _22_1665_pin_index(uint16_t mask)
{
    uint8_t idx = 0U;
    while (idx < 15U && (mask & 1U) == 0U) {
        mask = (uint16_t)(mask >> 1U);
        idx++;
    }
    return idx;
}
#endif /* _22_1665_PIN_LABEL_ON */

#if _22_1665_PIN_SELFTEST_ON
/** @brief GPIO_TypeDef* → pl_gpio 端口号（未知 → PL_PORT_MAX） */
static pl_port_t _22_1665_pl_port(const GPIO_TypeDef *port)
{
    if (port == GPIOA) return PL_PORT_A;
    if (port == GPIOB) return PL_PORT_B;
    if (port == GPIOC) return PL_PORT_C;
    if (port == GPIOD) return PL_PORT_D;
    if (port == GPIOE) return PL_PORT_E;
    if (port == GPIOF) return PL_PORT_F;
    if (port == GPIOG) return PL_PORT_G;
    if (port == GPIOH) return PL_PORT_H;
    return PL_PORT_MAX;
}
#endif /* _22_1665_PIN_SELFTEST_ON */

/* ================================================================
 *  六、22-1665 实例与缓冲区
 * ================================================================ */
typedef struct {
    dev_display_t me;
} dev_display_22_1665_t;

[[gnu::section(".ccmram")]] static uint8_t _22_1665_pixel_map[_22_1665_BUFFER_SIZE];

#if _22_1665_MULTI_CHAIN
/* 多链模式：**发送缓存 = 帧状态数组**（1B/时钟位；bit i = 第 i 条链该位的电平）。
 * 它就是本模式的「prepare 产物 / scan 输入」，故基类 hub75_buff 字段直接指向它
 * （基类不解释该缓冲的内容，仅作 prepare 目标 + 指针暂存，见 dev_display.c scan_task）。
 * 注意：数组只有 FRAME_BITS = 128B，而 buffer_size = 像素数 256 —— scan 只按
 * FRAME_BITS 访问，勿把本缓冲当「像素缓冲」用。 */
[[gnu::section(".ccmram")]] static uint8_t _22_1665_frame_state[_22_1665_FRAME_BITS];
#define _22_1665_SEND_BUFFER _22_1665_frame_state
#else
/* 兼容模式：发送缓存 = pixel_map 的恒等拷贝（1B/像素颜色索引），与旧版逐字节同布局 */
[[gnu::section(".ccmram")]] static uint8_t _22_1665_hub75_buff[_22_1665_BUFFER_SIZE];
#define _22_1665_SEND_BUFFER _22_1665_hub75_buff
#endif

static dev_display_22_1665_t g_22_1665 = {
    .me = {
        .ops                 = nullptr, /* 由 dev_display_22_1665_init 设置 */
        .module_rows         = _22_1665_SCREEN_W, /* 基类约定：module_rows = 每行像素数（宽） */
        .module_cols         = _22_1665_SCREEN_H, /* module_cols = 每列像素数（高） */
        .channels_per_module = 1,
        .modules_per_row     = 1,
        .modules_per_col     = 1,
        .scan_lines          = 1, /* 静态扫描（无行址线） */
        .screen_rows         = _22_1665_SCREEN_W,
        .screen_cols         = _22_1665_SCREEN_H,
        .total_channels      = 1,
        .channel_pixels      = _22_1665_BUFFER_SIZE,
        .scan_line_pixels    = _22_1665_FRAME_BITS,
        .buffer_size         = _22_1665_BUFFER_SIZE,
        .pixel_map           = _22_1665_pixel_map,
        .hub75_buff          = _22_1665_SEND_BUFFER,
        .module_code         = _22_1665_MODULE_CODE,
        .light_level         = DEV_DISPLAY_BRIGHTNESS_MAX,
    },
};

dev_display_t *dev_display_22_1665_get(void)
{
    return &g_22_1665.me;
}

/* ================================================================
 *  七、链表运行时状态（init 期校验后填写；prepare/scan/set_row 只读）
 * ================================================================ */
static uint8_t s_chain_ok[_22_1665_CHAIN_COUNT];   /* 1 = 该链通过校验、参与输出 */
static uint8_t s_chain_sel[_22_1665_CHAIN_COUNT];  /* 该链的状态位掩码：ok ? (1<<i) : 0 */
static uint8_t s_chain_role[_22_1665_CHAIN_COUNT]; /* 钳位后的角色（0/1；非法/禁用 → 0） */
static uint16_t s_out_lines;                       /* 本帧真正参与输出的脚（建表/set_row 用） */

/* ================================================================
 *  八、屏幕位置公式与链表建表（init 期；scan 热路径只查表）
 *
 *    链位 p：片 c = p / 16、片内级序 i = p % 16
 *    块：   bc = (blk_cols - 1) - (c % blk_cols)、br = c / blk_cols      （块列反向）
 *    块内： dx = 3 - (i % 4)、dy = ((i / 4) + 2) % 4
 *    屏面： (X, Y) = (x0 + 4*bc + dx, y0 + 4*br + dy)
 *
 *  区域 16×8（4 列 × 2 块栅格）= 第十二轮反解出的已知链模型，逐位一致。
 *
 *  第十七轮新增**每链落点镜像旗标**（`flags`，见 §二 `_22_1665_XF_*`）：在上述公式上逐项取反，
 *  flags = 0 时与本轮改动前**逐位相同**（宿主 check_multichain.py 已逐链复算）。
 *  另注：整条链的芯片级联次序若与假定相反（c → 7-c），等价于 `BLK_X | BLK_Y`
 *        （因 bc/br 由 c 导出：c'=7-c 时 bc' = 3-bc、br' = 1-br）；片内级序反向（i → 15-i）
 *        等价于 `X | Y`（dx' = 3-dx、dy' = 3-dy）——两者都不需要单独的旗标。
 * ================================================================ */
static uint16_t _22_1665_region_offset(uint8_t x0, uint8_t y0, uint8_t blk_cols, uint8_t blk_rows,
                                       uint8_t flags, uint16_t p)
{
    const uint16_t c  = (uint16_t)(p / _22_1665_CHIP_BITS);
    const uint16_t i  = (uint16_t)(p % _22_1665_CHIP_BITS);
    uint16_t bc       = (uint16_t)((blk_cols - 1U) - (c % blk_cols));
    uint16_t br       = (uint16_t)(c / blk_cols);
    uint16_t dx       = (uint16_t)(3U - (i % 4U));
    uint16_t dy       = (uint16_t)(((i / 4U) + 2U) % 4U);
    if ((flags & _22_1665_XF_BLK_X) != 0U)
        bc = (uint16_t)((blk_cols - 1U) - bc);
    if ((flags & _22_1665_XF_BLK_Y) != 0U)
        br = (uint16_t)((blk_rows - 1U) - br);
    if ((flags & _22_1665_XF_X) != 0U)
        dx = (uint16_t)(3U - dx);
    if ((flags & _22_1665_XF_Y) != 0U)
        dy = (uint16_t)(3U - dy);
    const uint16_t X = (uint16_t)(x0 + 4U * bc + dx);
    const uint16_t Y = (uint16_t)(y0 + 4U * br + dy);
    return (uint16_t)(Y * _22_1665_SCREEN_W + X);
}

/** @brief 链 i 的表项校验：区域/掩码/角色合法才参与输出（否则禁用 + RTT 告警）。
 *  @param why  出参：失败原因（静态字符串；供 RTT 打印） */
static bool _22_1665_chain_valid(uint8_t i, const char **why)
{
    const _22_1665_chain_t *c = &_22_1665_chains[i];

    if (c->name == nullptr) {
        *why = "name=null";
        return false;
    }
    if (c->line_mask == 0U) {
        *why = "line_mask=0";
        return false;
    }
    if ((c->line_mask & (uint16_t)~_22_1665_LINE_ALL) != 0U) {
        *why = "line_mask 越界（只有低 10 位合法）";
        return false;
    }
    if (c->w == 0U || c->h == 0U) {
        *why = "区域为空";
        return false;
    }
    if ((c->w % 4U) != 0U || (c->h % 4U) != 0U) {
        *why = "w/h 非 4 的倍数（块尺寸 4）";
        return false;
    }
    if ((uint16_t)((c->w / 4U) * (c->h / 4U)) != _22_1665_CHIPS) {
        *why = "块数 ≠ 8（区域须容纳 8 片 × 16 级 = 128 位）";
        return false;
    }
    if (((uint16_t)c->x0 + c->w) > _22_1665_SCREEN_W ||
        ((uint16_t)c->y0 + c->h) > _22_1665_SCREEN_H) {
        *why = "区域越屏";
        return false;
    }
#if _22_1665_MULTI_CHAIN
    if (c->role > _22_1665_ROLE_GREEN) {
        *why = "role 非法（多链模式只允许 RED / GREEN）";
        return false;
    }
#else
    if (c->role != _22_1665_ROLE_MONO) {
        *why = "兼容模式 role 须为 MONO";
        return false;
    }
#endif
    *why = "ok";
    return true;
}

/** @brief 生成一条链的落点表（显式表优先；否则用区域 + 默认规则逐位生成） */
static void _22_1665_build_chain_dst(uint8_t i)
{
    const _22_1665_chain_t *c = &_22_1665_chains[i];
    uint16_t *dst             = _22_1665_chain_dst[i];

    if (!s_chain_ok[i]) {
        memset(dst, 0, sizeof(_22_1665_chain_dst[i])); /* 禁用链：全部指向 0（不参与输出） */
        return;
    }
    if (c->dst != nullptr) {
        /* 显式落点表（逃生口）：逐项越界钳位（越界项钳到 0，屏上表现为该位静默） */
        for (uint16_t p = 0; p < _22_1665_FRAME_BITS; p++) {
            const uint16_t off = c->dst[p];
            dst[p] = (off < _22_1665_BUFFER_SIZE) ? off : 0U;
        }
        return;
    }
    const uint8_t blk_cols = (uint8_t)(c->w / 4U);
    const uint8_t blk_rows = (uint8_t)(c->h / 4U);
    for (uint16_t p = 0; p < _22_1665_FRAME_BITS; p++)
        dst[p] = _22_1665_region_offset(c->x0, c->y0, blk_cols, blk_rows, c->flags, p);
}

/** @brief 计算「本帧真正参与输出的脚集合」：
 *   · 探针 8：候选掩码 ∪ R1（R1 是序号显示器，恒参与）；其余探针：候选掩码；
 *   · 兼容模式：候选掩码（= 同流脚）；
 *   · 多链模式：全部**通过校验**的链的 line_mask 并集（未参与链表的脚一个位都不写）。 */
static uint16_t _22_1665_calc_out_lines(void)
{
#if _22_1665_CHAIN_PROBE == 8
    return (uint16_t)(_22_1665_DATA_LINES | _22_1665_LINE_R1);
#elif _22_1665_CHAIN_PROBE == 9
    /* 探针 9：扫描脚掩码 ∪ R1（序号显示器恒定参与）——未被扫到的脚也写 0（静默）*/
    return (uint16_t)(_22_1665_PROBE9_PINS | _22_1665_LINE_R1);
#elif _22_1665_CHAIN_PROBE != 0
    return (uint16_t)_22_1665_DATA_LINES;
#elif _22_1665_MULTI_CHAIN
    uint16_t m = 0U;
    for (uint8_t i = 0; i < _22_1665_CHAIN_COUNT; i++)
        if (s_chain_ok[i])
            m |= _22_1665_chains[i].line_mask;
    return m;
#else
    return (uint16_t)_22_1665_DATA_LINES;
#endif
}

/** @brief 链表初始化：逐行校验 → 落点表 → 参与脚集合 → RTT 自检行 */
static void _22_1665_chain_init(void)
{
    const char *why = "ok";

#if _22_1665_RTT_DIAG
    SEGGER_RTT_printf(0, "[22_1665] multichain=%u chains=%u screen=%ux%u frame_bits=%u\n",
                      (unsigned)_22_1665_MULTI_CHAIN, (unsigned)_22_1665_CHAIN_COUNT,
                      (unsigned)_22_1665_SCREEN_W, (unsigned)_22_1665_SCREEN_H,
                      (unsigned)_22_1665_FRAME_BITS);
#endif

    for (uint8_t i = 0; i < _22_1665_CHAIN_COUNT; i++) {
        s_chain_ok[i]   = _22_1665_chain_valid(i, &why) ? 1U : 0U;
        s_chain_sel[i]  = s_chain_ok[i] ? (uint8_t)(1U << i) : 0U;
        s_chain_role[i] = s_chain_ok[i] ? _22_1665_chains[i].role : (uint8_t)_22_1665_ROLE_RED;
        _22_1665_build_chain_dst(i);
#if _22_1665_RTT_DIAG
        {
            const _22_1665_chain_t *c = &_22_1665_chains[i];
            if (s_chain_ok[i]) {
                SEGGER_RTT_printf(
                    0, "[22_1665] chain%u %-3s lines=0x%03X role=%s region=(%u,%u,%u,%u) xf=0x%X dst=%s\n",
                    (unsigned)i, c->name, (unsigned)c->line_mask,
                    (c->role == _22_1665_ROLE_RED)     ? "RED"
                    : (c->role == _22_1665_ROLE_GREEN) ? "GREEN"
                                                       : "MONO",
                    (unsigned)c->x0, (unsigned)c->y0, (unsigned)c->w, (unsigned)c->h,
                    (unsigned)c->flags, (c->dst != nullptr) ? "explicit" : "default");
            } else {
                SEGGER_RTT_printf(0, "[22_1665] chain%u %-3s DISABLED: %s\n", (unsigned)i,
                                  (c->name != nullptr) ? c->name : "?", why);
            }
        }
#endif
    }

    s_out_lines = _22_1665_calc_out_lines();

#if _22_1665_RTT_DIAG
    /* 同一根脚被多条链声明 = 位流「或」叠加（物理上喂不了两份数据）→ 现场必须避免 */
    for (uint8_t i = 0; i < _22_1665_CHAIN_COUNT; i++) {
        if (!s_chain_ok[i])
            continue;
        for (uint8_t j = (uint8_t)(i + 1U); j < _22_1665_CHAIN_COUNT; j++) {
            if (!s_chain_ok[j])
                continue;
            const uint16_t dup = (uint16_t)(_22_1665_chains[i].line_mask & _22_1665_chains[j].line_mask);
            if (dup != 0U)
                SEGGER_RTT_printf(0,
                                  "[22_1665] WARN pin conflict 0x%03X between chain%u(%s) and "
                                  "chain%u(%s): bit streams will be OR-ed\n",
                                  (unsigned)dup, (unsigned)i, _22_1665_chains[i].name, (unsigned)j,
                                  _22_1665_chains[j].name);
        }
    }
    SEGGER_RTT_printf(0, "[22_1665] out_lines=0x%03X ports=%u states=%u\n", (unsigned)s_out_lines,
                      (unsigned)g_bsrr_port_cnt, (unsigned)_22_1665_STATES);
    if (s_out_lines == 0U)
        SEGGER_RTT_printf(0, "[22_1665] WARN out_lines=0 (all chains disabled?) -> screen dark\n");
#else
    (void)why;
#endif
}

/* ================================================================
 *  九、prepare：pixel_map → 发送缓存
 *
 *  · 多链模式：逐时钟位把 N 条链的电平拼成一个状态字节
 *      st[p] = Σ_i ( 该链像素分量位 << i )        （bit i = 第 i 条链）
 *    颜色分量：红链取 bit0(R)、绿链取 bit1(G)；蓝按 `_22_1665_BLUE_AS_LIT` 处理。
 *    **脏矩形只用来判「有无变化」**：任何一次提交都整帧重算 128 位（≈128×N 次查表，
 *    数 µs，远小于 500µs 帧周期），避免「链位散落在全屏」时的反查复杂度。
 *  · 兼容模式：恒等拷贝（像素颜色索引钳位 0..7），可只重排脏矩形覆盖的行。
 * ================================================================ */
#if _22_1665_MULTI_CHAIN
/** 颜色 → 该链是否点亮（[角色][颜色索引]；蓝分量按开关并入红绿） */
#if _22_1665_BLUE_AS_LIT
static const uint8_t _22_1665_role_bit[2][8] = {
    /* RED   */ {0U, 1U, 0U, 1U, 1U, 1U, 1U, 1U}, /* 黑/红/绿/黄/蓝/紫/青/白 */
    /* GREEN */ {0U, 0U, 1U, 1U, 1U, 1U, 1U, 1U},
};
#else
static const uint8_t _22_1665_role_bit[2][8] = {
    /* RED   */ {0U, 1U, 0U, 1U, 0U, 1U, 0U, 1U},
    /* GREEN */ {0U, 0U, 1U, 1U, 0U, 0U, 1U, 1U},
};
#endif

static void _22_1665_prepare(dev_display_t *dev)
{
    if (dev->dirty_rect_valid && dev->dirty_rect_h == 0U)
        return; /* 空矩形：内容无变化，不重算 */

    const uint8_t *pixel_map = dev->pixel_map;
    uint8_t *state           = dev->hub75_buff; /* = _22_1665_frame_state */

    for (uint16_t p = 0; p < _22_1665_FRAME_BITS; p++) {
        uint8_t s = 0U;
        for (uint8_t i = 0; i < _22_1665_CHAIN_COUNT; i++) {
            const uint8_t c = (uint8_t)(pixel_map[_22_1665_chain_dst[i][p]] & 0x07U);
            s = (uint8_t)(s | (_22_1665_role_bit[s_chain_role[i]][c] & s_chain_sel[i]));
        }
        state[p] = s;
    }
}
#else
static void _22_1665_prepare(dev_display_t *dev)
{
    const uint16_t width      = dev->screen_rows; /* 行主序步长（= 行宽） */
    const uint8_t *pixel_map  = dev->pixel_map;
    uint8_t *hub75_buff       = dev->hub75_buff;

    uint16_t row_begin = 0;
    uint16_t row_end   = dev->screen_cols;
    if (dev->dirty_rect_valid) {
        if (dev->dirty_rect_h == 0)
            return; /* 空矩形：无行变化，无需重排 */
        row_begin = dev->dirty_rect_y;
        if (row_begin >= dev->screen_cols)
            return; /* 矩形整体在屏外（防御，提交侧已钳位） */
        uint32_t end = (uint32_t)row_begin + dev->dirty_rect_h;
        row_end      = (end > dev->screen_cols) ? dev->screen_cols : (uint16_t)end;
    }

    for (uint16_t row = row_begin; row < row_end; row++) {
        const uint8_t *src = pixel_map + (uint32_t)row * width;
        uint8_t *dst       = hub75_buff + (uint32_t)row * width;
        for (uint16_t col = 0; col < width; col++)
            dst[col] = (uint8_t)(src[col] & 0x07U);
    }
}
#endif /* _22_1665_MULTI_CHAIN */

/* ================================================================
 *  十、链序探针图案（仅探针开启时编译进来）
 * ================================================================ */

#if _22_1665_CHAIN_PROBE != 0
/* 注意：步进必须按 **scan（帧）** 计数，不能按链位计数 —— 本函数每帧被调用
 * FRAME_BITS 次，若在函数内自增，模式 1 的走点会快 128 倍。 */

/** @brief 本次 scan 的当前步进值（模式 1 = 逐帧推进的链位号；其余模式不使用） */
static inline uint16_t _22_1665_probe_step(void)
{
#if _22_1665_CHAIN_PROBE == 1
    static uint32_t s_frames;
    return (uint16_t)((s_frames++ / _22_1665_CHAIN_PROBE_HOLD) % _22_1665_FRAME_BITS);
#else
    return 0U;
#endif
}

/** @brief 模式 1/2/3/4 下，本链位是否点亮（返回「数据脚状态」的 0/1） */
static inline uint8_t _22_1665_probe_on(uint16_t p, uint16_t step)
{
    (void)step; /* 仅模式 1 使用；其余模式显式抑制告警 */
#if _22_1665_CHAIN_PROBE == 1
    return (step == p) ? 1U : 0U;
#elif _22_1665_CHAIN_PROBE == 2
    const uint16_t first = (uint16_t)(_22_1665_CHAIN_PROBE_POS % _22_1665_FRAME_BITS);
    const uint32_t last  = (uint32_t)first + _22_1665_CHAIN_PROBE_SPAN;
    return ((p >= first) && (p < last) && (p < _22_1665_FRAME_BITS)) ? 1U : 0U;
#elif _22_1665_CHAIN_PROBE == 3
    return (p < (_22_1665_FRAME_BITS / 2U)) ? 1U : 0U;
#elif _22_1665_CHAIN_PROBE == 4
    /* 片锚点：每片的第 POS 级 → 屏上 8 个点（每片 1 点），直接读出 8 片的块排布 */
    return ((p % _22_1665_CHIP_BITS) == _22_1665_CHAIN_PROBE_POS) ? 1U : 0U;
#else
    (void)p;
    return 1U; /* 5 / 6：全链点亮 */
#endif
}
#endif /* _22_1665_CHAIN_PROBE */

#if _22_1665_CHAIN_PROBE == 7
/**
 * @brief  片内级序 i → 该级在 4×4 块内的**行主序格号**（bit 位号，0 = 左上角）。
 *
 *  与 §八 的落点公式同源（dx = 3 − i%4、dy = (i/4 + 2) % 4）；块的屏面位置不影响块内格号，
 *  所以逐脚图案只需在这 16 个格号上定义一次（换区域/兼容模式不变）。
 */
static inline uint8_t _22_1665_cell_of_i(uint8_t i)
{
    const uint8_t dx = (uint8_t)(3U - (i % 4U));
    const uint8_t dy = (uint8_t)(((i / 4U) + 2U) % 4U);
    return (uint8_t)(dy * 4U + dx);
}

/**
 * @brief  探针 7「逐脚标识」的逐脚图案（4×4 块的行主序格掩码：bit = dy*4+dx，1 = 亮）。
 *
 *  点数 = k + 1（k = 脚序号 0..9），逐格行主序填充：
 *      k=0..3 → 1..4 点（第 0 行渐满，4 = 整行）
 *      k=4..6 → 5..7 点（第 0 行满 + 第 1 行渐满）
 *      k=7    → 8 点（前两行整满）      k=8 → 12 点（前三行）      k=9 → 16 点（满块）
 *  点数互不相同 ⇒ 即使其余链的片内位序与已知链不同，按「一片 16 位里亮了几个灯」仍可认脚；
 *  形状（在已知链模型下）为「逐渐填满的方块」，见 `.analysis/22_1665/probe7_screens.md`。 */
static const uint16_t _22_1665_probe7_cells[_22_1665_LINE_COUNT] = {
    0x0001U, /* k=0 R1(PG9) ：1 点（块左上角） */
    0x0003U, /* k=1 G1(PG10)：2 点 */
    0x0007U, /* k=2 B1(PG12)：3 点 */
    0x000FU, /* k=3 R2(PG15)：4 点 = 第 0 行整行 */
    0x001FU, /* k=4 G2(PB6) ：5 点 */
    0x003FU, /* k=5 B2(PB7) ：6 点 */
    0x007FU, /* k=6 A (PD4) ：7 点 */
    0x00FFU, /* k=7 B (PD5) ：8 点 = 前两行整满 */
    0x0FFFU, /* k=8 C (PD6) ：12 点 = 前三行 */
    0xFFFFU, /* k=9 D (PD7) ：16 点 = 整块实心 */
};

/**
 * @brief  第 k 根候选脚在片内级序 i 处是否点亮（探针 7）。
 *
 *  「点数」是**对链内位序置换不变**的判据（一片 16 位里亮几个灯），所以即使某条链的
 *  片内位序与已知链不同，也能按点数认出是哪根脚。若某链极性相反（屏上出补色图案），
 *  则把观察到的点数 n 反查为 16 − n。
 */
static inline bool _22_1665_probe7_line_on(uint8_t k, uint8_t i)
{
    return ((_22_1665_probe7_cells[k] >> _22_1665_cell_of_i(i)) & 1U) != 0U;
}
#endif /* _22_1665_CHAIN_PROBE == 7 */

/* ================================================================
 *  十-2、探针 8「逐脚闪烁 + 纹理」（判「真被驱动」还是「悬空/弱耦合」）
 *
 *  每根候选脚一个 3s 停留：前 1.5s 2 Hz 全1/全0 闪烁、后 1.5s 静态 1010… 纹理；
 *  已知链（R1）全程显示被测脚序号（5×7 数字点阵，居中于链 0 区域 = 已知链屏面）。
 *  状态表（`_22_1665_STATES = 4`）：st = (被测脚电平 bb << 1) | 已知链数字位 dg。
 *
 *  与探针 9（§十-3）共用：数字点阵 `_22_1665_probe8_font`、像素缓冲
 *  `_22_1665_probe8_text`、链位缓冲 `_22_1665_probe8_bits`、链 0 区域取法
 *  `_22_1665_probe8_region()`、数字绘制 `_22_1665_probe8_draw_digit()`。
 * ================================================================ */
#if (_22_1665_CHAIN_PROBE == 8) || (_22_1665_CHAIN_PROBE == 9)

static void _22_1665_build_bsrr_table(void); /* §十一；表随 (被测脚, 相位) 重建 */

/** 半周期帧数（TIM3 = 500µs/帧 → 500ms = 1000 帧；仅探针 8 使用） */
#define _22_1665_PROBE8_HALF_FRAMES ((_22_1665_PROBE8_FRAME_HZ * _22_1665_PROBE8_HALF_MS) / 1000U)

/** 5×7 数字点阵（行主序；每行 bit4 = 最左列，bit0 = 最右列）。
 *  字形居中于**链 0 的区域**（多链 = 已知链的 16×8 半屏；兼容 = 整屏 16×8）。 */
static const uint8_t _22_1665_probe8_font[10][7] = {
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* 0 */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, /* 2 */
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, /* 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, /* 4 */
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, /* 5 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, /* 6 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, /* 8 */
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, /* 9 */
};

[[gnu::section(".ccmram")]] static uint8_t _22_1665_probe8_text[_22_1665_BUFFER_SIZE]; /* 像素 0/1 */
[[gnu::section(".ccmram")]] static uint8_t _22_1665_probe8_bits[_22_1665_FRAME_BITS];   /* 链位 → 数字位 */

/** @brief 序号/指示器所用「已知链」区域 = 链 0 的区域（多链 = 已知链半屏；兼容 = 整屏）。
 *  数字与链位图案都按链 0 的落点表映射（= 已知链的物理排布）。 */
static void _22_1665_probe8_region(uint8_t *x0, uint8_t *y0, uint8_t *w, uint8_t *h)
{
    *x0 = _22_1665_chains[0].x0;
    *y0 = _22_1665_chains[0].y0;
    *w  = _22_1665_chains[0].w;
    *h  = _22_1665_chains[0].h;
}

/** @brief 把一个 5×7 数字 k 画到 `_22_1665_probe8_text` 的 (px, py) 处（供单/双数字共用） */
static void _22_1665_probe8_blit_digit(uint8_t k, int px, int py)
{
    const int w = 5;
    const int h = 7;
    for (int r = 0; r < h; r++) {
        const int y = py + r;
        if (y < 0 || y >= (int)_22_1665_SCREEN_H)
            break;
        for (int c = 0; c < w; c++) {
            const int x = px + c;
            if (x < 0 || x >= (int)_22_1665_SCREEN_W)
                break;
            if ((_22_1665_probe8_font[k][r] & (uint8_t)(1U << (w - 1 - c))) != 0U)
                _22_1665_probe8_text[(uint32_t)y * _22_1665_SCREEN_W + (uint16_t)x] = 1U;
        }
    }
}

/** @brief 画当前被测脚序号到 `_22_1665_probe8_text`，再由链 0 落点表逆映射成链位序列 */
#if _22_1665_CHAIN_PROBE == 8
static void _22_1665_probe8_draw_digit(uint8_t k)
{
    memset(_22_1665_probe8_text, 0, sizeof(_22_1665_probe8_text));

    uint8_t rx = 0U, ry = 0U, rw = 0U, rh = 0U;
    _22_1665_probe8_region(&rx, &ry, &rw, &rh);

    int x0 = (int)rx + (((int)rw - 5) / 2);
    int y0 = (int)ry + (((int)rh - 7) / 2);
    if (x0 < (int)rx) x0 = (int)rx;
    if (y0 < (int)ry) y0 = (int)ry;

    _22_1665_probe8_blit_digit(k, x0, y0);
    for (uint16_t p = 0; p < _22_1665_FRAME_BITS; p++)
        _22_1665_probe8_bits[p] = (_22_1665_probe8_text[_22_1665_chain_dst[0][p]] != 0U) ? 1U : 0U;
}
#endif /* _22_1665_CHAIN_PROBE == 8 */

/** @brief 画两位数字（左 = d1、右 = d2，居中于链 0 区域；探针 9 的「链号 + 脚号」读数） */
#if _22_1665_CHAIN_PROBE == 9
static void _22_1665_probe8_draw_digits2(uint8_t d1, uint8_t d2)
{
    memset(_22_1665_probe8_text, 0, sizeof(_22_1665_probe8_text));

    uint8_t rx = 0U, ry = 0U, rw = 0U, rh = 0U;
    _22_1665_probe8_region(&rx, &ry, &rw, &rh);

    const int gap = 1;
    const int wid = 5 + gap + 5;
    int x0 = (int)rx + (((int)rw - wid) / 2);
    int y0 = (int)ry + (((int)rh - 7) / 2);
    if (x0 < (int)rx) x0 = (int)rx;
    if (y0 < (int)ry) y0 = (int)ry;

    _22_1665_probe8_blit_digit(d1, x0, y0);
    _22_1665_probe8_blit_digit(d2, x0 + 5 + gap, y0);
    for (uint16_t p = 0; p < _22_1665_FRAME_BITS; p++)
        _22_1665_probe8_bits[p] = (_22_1665_probe8_text[_22_1665_chain_dst[0][p]] != 0U) ? 1U : 0U;
}
#endif /* _22_1665_CHAIN_PROBE == 9 */

#if _22_1665_CHAIN_PROBE == 8
static uint8_t s_probe8_k;       /* 当前被测脚序号（0..9，掩码内） */
static uint8_t s_probe8_h;       /* 停留内半周期号（0..DWELL_HALVES-1） */
static uint32_t s_probe8_frames; /* 半周期帧计数 */

/** @brief 是否处于「纹理段」（停留内后半段）；否则 = 闪烁段 */
static inline bool _22_1665_probe8_hatch(void)
{
    return s_probe8_h >= _22_1665_PROBE8_BLINK_HALVES;
}
#endif /* _22_1665_CHAIN_PROBE == 8 */

#if _22_1665_CHAIN_PROBE == 8
/** @brief 状态 st → 10 根候选脚的目标电平位掩码（未点亮的脚由建表阶段写 0 = 静默） */
static uint16_t _22_1665_probe8_state_bits(uint8_t st)
{
    const uint8_t bb = (uint8_t)(st >> 1U); /* 被测脚电平 */
    const uint8_t dg = (uint8_t)(st & 1U);  /* 已知链数字位 */

    /* R1 = 序号显示；被测脚恰为 R1 时：闪烁段跟闪烁、纹理段「纹理 ⊕ 实心数字」 */
    uint8_t pin0_lv;
    if (s_probe8_k == 0U)
        pin0_lv = _22_1665_probe8_hatch() ? (uint8_t)(bb | dg) : bb;
    else
        pin0_lv = dg;

    uint16_t bits = (uint16_t)pin0_lv; /* bit0 = R1 */
    if (s_probe8_k != 0U)
        bits |= (uint16_t)((uint16_t)bb << s_probe8_k);
    return bits;
}

/** @brief 推进到掩码内的下一根被测脚（升序循环），重画数字并打印一行 */
static void _22_1665_probe8_next_pin(void)
{
    for (uint8_t i = 0; i < _22_1665_LINE_COUNT; i++) {
        const uint8_t k = (uint8_t)((s_probe8_k + 1U + i) % _22_1665_LINE_COUNT);
        if ((_22_1665_DATA_LINES & (uint16_t)(1U << k)) != 0U) {
            s_probe8_k = k;
            break;
        }
    }
    _22_1665_probe8_draw_digit(s_probe8_k);
#if _22_1665_RTT_DIAG
    {
        const hub75_pin_t *pin = _22_1665_line_pin(s_probe8_k);
        SEGGER_RTT_printf(0, "[22_1665] probe8 dwell: pin=%s (P%c%u) k=%u\n",
                          _22_1665_line_names[s_probe8_k], _22_1665_port_char(pin->port),
                          (unsigned)_22_1665_pin_index(pin->pin), (unsigned)s_probe8_k);
    }
#endif
}

/** @brief 每帧一次：半周期到则推进相位；停留结束则换下一根脚；随后按 (脚, 相位) 重建表 */
static void _22_1665_probe8_tick(void)
{
    if (++s_probe8_frames < _22_1665_PROBE8_HALF_FRAMES)
        return;
    s_probe8_frames = 0U;

    uint8_t h = (uint8_t)(s_probe8_h + 1U);
    if (h >= _22_1665_PROBE8_DWELL_HALVES) {
        h = 0U;
        _22_1665_probe8_next_pin();
    }
    s_probe8_h = h;
    _22_1665_build_bsrr_table(); /* 4 状态 × ≤10 脚，重建开销可忽略（每半周期一次） */
}

/** @brief 探针 8 初始化：从掩码内第一根脚开始（建表与首次数字绘制） */
static void _22_1665_probe8_init(void)
{
    s_probe8_k      = (uint8_t)(_22_1665_LINE_COUNT - 1U); /* next_pin → 掩码内最小序号 */
    s_probe8_h      = 0U;
    s_probe8_frames = 0U;
    _22_1665_probe8_next_pin();
    _22_1665_build_bsrr_table();
}
#endif /* _22_1665_CHAIN_PROBE == 8 */

/* ================================================================
 *  十-3、探针 9「逐链 × 逐脚内容扫描」（第十七轮新增；**现场标定首选**）
 *
 *  问题：多链框架下「哪根脚驱动哪条链」未知，而**只有把每条链自己的内容送到它真正的脚上，
 *  屏上才会出现正确内容**（其余脚写 0 → 屏上只剩被测脚那条链在动，互不干扰）。
 *  做法：外层遍历链（链表行号 i），内层遍历候选脚（默认四个地址脚 A/B/C/D），每个组合停留
 *  1.5s —— 期间：
 *    · **只驱动该脚**（其余候选脚写 0）；
 *    · 该脚上送**第 i 条链自己的内容**（按该链的 `region` + `dst` 取像素，**非黑即亮** ——
 *      这样「亮的是哪种颜色的灯」直接就是该链的 die 颜色 = 该链该填的 `role`）；
 *    · 链 0 所在的 **R1** 脚不参与扫描，专职显示**两位数字**：左 = 链号 i（链表第几行）、
 *      右 = 驱动内部脚号 k（0..5 = R1/G1/B1/R2/G2/B2、6..9 = A/B/C/D，与探针 7/8 同一套编号）；
 *      RTT 另打一行 `probe9: chain=%u pin=%s`（带端口与位号，可直接照抄）。
 *  判读（一轮 4 链 × 4 脚 × 1.5s = 24s，循环播放）：看到某块屏出现**形状正确**的内容 →
 *    读数字：「左数字 = 该链在链表里的行号（角色/区域按该行填）、右数字 = 该链的数据脚」；
 *    颜色 = 该链的 die 颜色（红 → ROLE_RED、绿 → ROLE_GREEN）；
 *    若内容**上下颠倒/左右相反** → 该链要加 `flags`（见 `_22_1665_XF_*`，最常见 = ROT180）；
 *    若内容出现在**另一块区域** → 该链的 `region` 该填那块屏（说明它在链表里的区域行填错了）。
 *  若扫完 4 个地址脚什么都不出 → 说明该链的数据入口不在这几个脚上（改 `_22_1665_PROBE9_PINS`
 *    再烧一次，或退回探针 8 扫全部 10 根候选脚）。
 * ================================================================ */
#if _22_1665_CHAIN_PROBE == 9

/* 组合停留帧数（TIM3 = 500µs/帧 → 1500ms = 3000 帧） */
#define _22_1665_PROBE9_HALF_FRAMES ((_22_1665_PROBE9_FRAME_HZ * _22_1665_PROBE9_HALF_MS) / 1000U)

static const uint16_t _22_1665_probe9_pins = _22_1665_PROBE9_PINS; /* 扫描脚掩码（10 位） */

static uint8_t s_probe9_pin_list[_22_1665_LINE_COUNT]; /* 掩码内候选脚位号（升序，init 期建） */
static uint8_t s_probe9_pin_cnt;                       /* 列表长度（≥1，编译期已拦空掩码） */
static uint8_t s_probe9_pin_idx;                       /* 当前脚在列表中的下标 */
static uint8_t s_probe9_chain;                         /* 当前被测链号（链表行号） */
static uint32_t s_probe9_frames;                       /* 组合停留帧计数 */

/** @brief 掩码内下一条链（from 之后升序循环，含 from）；返回 false = 掩码内无链（防御） */
static bool _22_1665_probe9_pick_chain(uint8_t from, uint8_t *out)
{
    for (uint8_t n = 0; n < _22_1665_CHAIN_COUNT; n++) {
        const uint8_t i = (uint8_t)((from + n) % _22_1665_CHAIN_COUNT);
        if ((_22_1665_PROBE9_CHAINS & (uint8_t)(1U << i)) != 0U) {
            *out = i;
            return true;
        }
    }
    return false;
}

/** @brief 推进到下一个组合（脚在同一链内逐个走完 → 换下一条链），重画数字并打印一行 */
static void _22_1665_probe9_next_pair(void)
{
    if (s_probe9_pin_cnt == 0U) /* 防御：编译期已保证掩码非空 */
        return;
    s_probe9_pin_idx = (uint8_t)((s_probe9_pin_idx + 1U) % s_probe9_pin_cnt);
    if (s_probe9_pin_idx == 0U) { /* 该链的脚扫完一轮 → 换下一条链 */
        uint8_t nxt = s_probe9_chain;
        if (_22_1665_probe9_pick_chain((uint8_t)(s_probe9_chain + 1U), &nxt))
            s_probe9_chain = nxt;
    }
    _22_1665_probe8_draw_digits2(s_probe9_chain, s_probe9_pin_list[s_probe9_pin_idx]);
#if _22_1665_RTT_DIAG
    {
        const uint8_t k      = s_probe9_pin_list[s_probe9_pin_idx];
        const hub75_pin_t *pin = _22_1665_line_pin(k);
        SEGGER_RTT_printf(0, "[22_1665] probe9: chain=%u %-3s role=%s pin=%s (P%c%u) k=%u\n",
                          (unsigned)s_probe9_chain, _22_1665_chains[s_probe9_chain].name,
                          (_22_1665_chains[s_probe9_chain].role == _22_1665_ROLE_RED) ? "RED"
                                                                                        : "GREEN",
                          _22_1665_line_names[k], _22_1665_port_char(pin->port),
                          (unsigned)_22_1665_pin_index(pin->pin), (unsigned)k);
    }
#endif
}

/** @brief 每帧一次：停留到点则换下一个「链 × 脚」组合（数字随之更新；状态表**无需重建** ——
 *  状态语义恒为「(内容位 << 1) | 数字位」，被测脚由 `s_probe9_pin_list[s_probe9_pin_idx]` 决定） */
static void _22_1665_probe9_tick(void)
{
    if (++s_probe9_frames < _22_1665_PROBE9_HALF_FRAMES)
        return;
    s_probe9_frames = 0U;
    _22_1665_probe9_next_pair();
}

/** @brief 状态 st → 参与脚的目标电平（bit0 = R1 数字位；被测脚 = 内容位） */
static uint16_t _22_1665_probe9_state_bits(uint8_t st)
{
    const uint8_t cb = (uint8_t)(st >> 1U); /* 被测脚 = 被测链的内容位（该链 pixel 是否非黑） */
    const uint8_t dg = (uint8_t)(st & 1U);  /* 链 0（R1）= 序号数字位 */
    return (uint16_t)((uint16_t)dg | ((uint16_t)cb << s_probe9_pin_list[s_probe9_pin_idx]));
}

/** @brief 当前被测链（内容位取值用；越界防御回 0） */
static inline uint8_t _22_1665_probe9_sel_chain(void)
{
    return (s_probe9_chain < _22_1665_CHAIN_COUNT) ? s_probe9_chain : 0U;
}

/** @brief 探针 9 初始化：建候选脚列表 → 从第一条链 + 列表第一根脚开始 */
static void _22_1665_probe9_init(void)
{
    uint8_t first = 0U;
    (void)_22_1665_probe9_pick_chain(0U, &first);
    s_probe9_chain   = first;
    s_probe9_pin_cnt = 0U;
    for (uint8_t k = 0; k < _22_1665_LINE_COUNT; k++) {
        if ((_22_1665_probe9_pins & (uint16_t)(1U << k)) != 0U)
            s_probe9_pin_list[s_probe9_pin_cnt++] = k;
    }
    s_probe9_pin_idx = (uint8_t)(s_probe9_pin_cnt - 1U); /* next_pair → 列表第 0 项 */
    s_probe9_frames  = 0U;
    _22_1665_probe9_next_pair();
    _22_1665_build_bsrr_table();
}
#endif /* _22_1665_CHAIN_PROBE == 9 */

#endif /* _22_1665_CHAIN_PROBE == 8 || _22_1665_CHAIN_PROBE == 9 */

/* ================================================================
 *  十一、scan：一帧位流输出 — 逐时钟位取状态 + 合并写 BSRR + CLK 脉冲
 *
 *  静态扫描 → line 恒 0，一次 scan 输出整帧 = FRAME_BITS（128）个 CLK、每时钟 1 位；
 *  **N 条链并行喂**：每个时钟位的数据脚状态字节里同时含 N 条链的第 p 位
 *  （多链模式 = prepare 预算的状态数组；兼容模式 = 该位像素「非黑即亮」的 0/1）。
 *  先移入的位停在链尾，故按链位 p 递减遍历。帧末由基类在 OE 消隐窗口内打 LAT。
 * ================================================================ */

/** @brief 把「数据脚状态 st」写到全部参与端口（每端口一次合并 BSRR 写；本板 ≤3）。
 *  强制内联 + **展开三槽**：端口数是 init 期确定的常数（≤3），展开可省掉「逐次重读
 *  计数 + 重算表地址」的开销（实测比 for 循环每链位少 ~25 条指令）。
 *  换板后数据脚跨 >3 个 GPIO 端口：加大 `_22_1665_PORT_MAX` 并在下面补一槽。 */
[[gnu::always_inline]] static inline void _22_1665_flush_state(uint8_t st)
{
    pl_hub75_bsrr_flush(&g_bsrr_tab[0][st]);
    if (g_bsrr_port_cnt > 1U)
        pl_hub75_bsrr_flush(&g_bsrr_tab[1][st]);
    if (g_bsrr_port_cnt > 2U)
        pl_hub75_bsrr_flush(&g_bsrr_tab[2][st]);
}

static inline void _22_1665_scan(dev_display_t *dev, uint8_t line)
{
#if _22_1665_MULTI_CHAIN
    /* 多链模式：发送缓存 = 帧状态数组（1B/时钟位，prepare 已把 N 条链的位拼好） */
    const uint8_t *src = dev->hub75_buff;
    (void)line; /* 静态单扫（scan_lines = 1 → line 恒 0） */
#else
    /* 兼容模式：本扫描行的内容起点（静态：line 恒 0） */
    const uint8_t *src = dev->hub75_buff + (uint16_t)line * _22_1665_SCAN_LINE_PX;
#endif

#if _22_1665_CHAIN_PROBE == 8
    (void)src;                        /* 探针 8：序号由本文件自绘，与显示内容无关 */
    _22_1665_probe8_tick();           /* 每帧一次：半周期/停留推进 + 按 (脚, 相位) 重建表 */
    const bool p8_hatch = _22_1665_probe8_hatch();
    const uint8_t p8_bb = ((s_probe8_h & 1U) == 0U) ? 1U : 0U; /* 闪烁段：半周期 0/2 全 1，1 全 0 */
#elif _22_1665_CHAIN_PROBE == 9
    (void)src;                          /* 探针 9：内容直接取自被测链自己的落点表 */
    _22_1665_probe9_tick();             /* 每帧一次：「链 × 脚」组合停留推进 */
    const uint8_t *p9_map = dev->pixel_map;                                   /* 内容源 */
    const uint16_t *p9_dst = _22_1665_chain_dst[_22_1665_probe9_sel_chain()]; /* 被测链落点表 */
#elif _22_1665_CHAIN_PROBE == 7
    (void)src; /* 逐脚图案与显示内容无关 */
#elif _22_1665_CHAIN_PROBE != 0
    const uint16_t probe_step = _22_1665_probe_step(); /* 每帧调用一次（步进按帧计） */
#endif

    for (uint16_t p = _22_1665_FRAME_BITS; p-- > 0;) {
        uint8_t st;
#if _22_1665_CHAIN_PROBE == 8
        /* 状态 = (被测脚电平 bb << 1) | 已知链数字位 dg：
         *   闪烁段 bb = 该半周期恒定电平（全 1 亮 / 全 0 灭）；
         *   纹理段 bb = 链位奇偶（1010… → 真被驱动的链出细密规则纹理） */
        const uint8_t bb = p8_hatch ? (uint8_t)(p & 1U) : p8_bb;
        st = (uint8_t)((uint8_t)(bb << 1U) | (_22_1665_probe8_bits[p] & 1U));
#elif _22_1665_CHAIN_PROBE == 9
        /* 状态 = (被测链内容位 << 1) | 链 0 数字位：
         *   内容位 = 被测链落点表给出的像素「非黑即亮」（颜色由该链 die 决定 → 直接读出 role） */
        const uint8_t cb9 = (p9_map[p9_dst[p]] != (uint8_t)COLOR_BLACK) ? 1U : 0U;
        st = (uint8_t)((uint8_t)(cb9 << 1U) | (_22_1665_probe8_bits[p] & 1U));
#elif _22_1665_CHAIN_PROBE == 7
        /* 状态 = 片内级序 i（逐脚图案以一片 = 16 链位为周期）→ 直接查 16 项合并表 */
        st = (uint8_t)(p % _22_1665_CHIP_BITS);
#elif _22_1665_CHAIN_PROBE != 0
        /* 探针：忽略显示内容，只发探针图案 */
        (void)src;
        st = _22_1665_probe_on(p, probe_step);
#elif _22_1665_MULTI_CHAIN
        /* 多链：状态字节 = N 条链该时钟位的电平组合（prepare 已算好） */
        st = src[p];
#else
        /* 兼容模式：非黑即亮（单色口径），链位 → 像素由链 0 落点表给出 */
        st = (src[_22_1665_chain_dst[0][p]] != (uint8_t)COLOR_BLACK) ? 1U : 0U;
#endif
        _22_1665_flush_state(st);
        _22_1665_BIT_MARGIN();
        pl_hub75_clock_pulse();
    }
}

/* ---- set_row：静态扫描无行址线（与 p20 静态 1 扫同款，row 恒 0）。
 *
 *  约定：**被选进数据输出的地址脚归 scan 数据路径独占**（本帧真正参与输出的脚 =
 *  `s_out_lines`：多链 = 有效链数据脚并集 / 兼容与探针 = 候选掩码），本函数一个位都不
 *  写它们 —— 否则 set_row 会在 LAT 前的窗口里把它们清零，把帧末电平改掉。
 *  未被选中的地址脚行为与旧版**逐位相同**（row=0 → 写 0；四根都未选中时与
 *  `pl_hub75_Decoder_set_row(row)` 完全等价）。
 *
 *  实现用合并 BSRR 写法（A/B/C/D 同属 GPIOD）：一次写里「置位」给 row 中为 1 的脚、
 *  「复位位」给 row 中为 0 的脚；若四根都成了数据脚 → 本函数不产生任何写。 ---- */
static void _22_1665_set_row(uint8_t row)
{
    uint32_t set = 0U;
    uint32_t clr = 0U;
    for (uint8_t k = 0; k < _22_1665_ADDR_COUNT; k++) {
        if ((s_out_lines & (uint16_t)(1U << (_22_1665_RGB_COUNT + k))) != 0U)
            continue; /* 该地址脚已被数据路径接管 → set_row 不碰 */
        const uint16_t mask = _22_1665_addr_pins[k].pin;
        if ((row & (uint8_t)(1U << k)) != 0U)
            set |= mask;
        else
            clr |= (uint32_t)mask << 16U;
    }
    if ((set | clr) == 0U)
        return; /* 四根地址脚全部是数据脚：无行址可写 */

    const pl_hub75_bsrr_t v = {.val = set | clr, .port = _22_1665_addr_pins[0].port};
    pl_hub75_bsrr_flush(&v);
}

/* ---- ops 虚表 ---- */
static const dev_display_ops_t _22_1665_ops = {
    .prepare = _22_1665_prepare,
    .scan    = _22_1665_scan,
    .set_row = _22_1665_set_row,
};

/* ================================================================
 *  十二、init：链表校验 → 落点表 → 合并写 BSRR 表 + 上电引脚自检 + 绑定 ops
 * ================================================================ */

/**
 * @brief  生成合并写表：把**参与输出的脚**（`s_out_lines`）按 GPIO 端口分组，
 *         预计算「状态 → 该端口 BSRR 字」。
 *
 *  表项 = 该端口上**全部参与脚**的目标电平合并（亮 → 置位位，灭 → 复位位；极性由
 *  `_22_1665_DATA_ACTIVE_HIGH` 决定）——同一状态里每根参与脚都被写一次：要么置位、
 *  要么复位，**不存在「只在某些脚生效」的路径**（每时钟位每端口恰好一次合并 BSRR 写）。
 *  未参与的脚一个位都不写（保持其静默电平）。
 *
 *  状态语义：
 *   · 多链正常显示（模式 0 + MULTI_CHAIN）：状态位 i = 第 i 条链该时钟位的电平
 *     → 亮的脚 = 各「状态位为 1」的链的 line_mask 并集；
 *   · 兼容 / 探针 1..6：st=0 → 全灭、st=1 → 全部参与脚一起亮；
 *   · 探针 7 ：st = 片内级序 i（16 项，逐脚图案）；
 *   · 探针 8 ：st = (被测脚电平 bb << 1) | 已知链数字位 dg（4 项，随停留/相位重建）。
 *  端口槽若没有任何参与脚，会被裁掉 → 每时钟位只写真正用到的端口（本板 ≤3）。
 */
static void _22_1665_build_bsrr_table(void)
{
    /* ① 收集参与脚的 GPIO 端口（去重；本板只落 PG / PB / PD 三个） */
    const uint16_t wlines = s_out_lines;
    GPIO_TypeDef *ports[_22_1665_PORT_MAX];
    uint8_t line_slot[_22_1665_LINE_COUNT];
    uint8_t port_cnt = 0U;

    for (uint8_t k = 0; k < _22_1665_LINE_COUNT; k++) {
        line_slot[k] = 0xFFU; /* 默认：不参与（0xFF = 无端口槽） */
        if ((wlines & (uint16_t)(1U << k)) == 0U)
            continue;
        const hub75_pin_t *pin = _22_1665_line_pin(k);
        uint8_t slot           = 0xFFU;
        for (uint8_t i = 0; i < port_cnt; i++) {
            if (ports[i] == pin->port) {
                slot = i;
                break;
            }
        }
        if (slot == 0xFFU) {
            if (port_cnt >= _22_1665_PORT_MAX) {
                /* 端口数超过表容量：该脚被丢弃（本板不会发生 —— 10 脚只在 PG/PB/PD；
                 * 换板时加大 _22_1665_PORT_MAX 即可） */
                line_slot[k] = 0xFFU;
                continue;
            }
            slot              = port_cnt;
            ports[port_cnt++] = pin->port;
        }
        line_slot[k] = slot;
    }

    /* ② 逐状态逐端口合并 BSRR 字 */
    for (uint16_t st = 0U; st < (uint16_t)_22_1665_STATES; st++) {
        uint16_t st_bits;
#if _22_1665_CHAIN_PROBE == 8
        st_bits = _22_1665_probe8_state_bits(st);
#elif _22_1665_CHAIN_PROBE == 9
        st_bits = _22_1665_probe9_state_bits(st);
#elif _22_1665_CHAIN_PROBE == 7
        st_bits = 0U;
        for (uint8_t k = 0; k < _22_1665_LINE_COUNT; k++)
            if (_22_1665_probe7_line_on(k, st))
                st_bits |= (uint16_t)(1U << k);
#elif _22_1665_MULTI_CHAIN
        /* 多链：状态位 i = 第 i 条链的电平 → 亮的脚 = 各链 line_mask 并集 */
        st_bits = 0U;
        for (uint8_t i = 0; i < _22_1665_CHAIN_COUNT; i++)
            if ((st & (uint8_t)(1U << i)) != 0U)
                st_bits |= (uint16_t)(_22_1665_chains[i].line_mask & (s_chain_ok[i] ? 0xFFFFU : 0U));
#else
        st_bits = (st != 0U) ? wlines : 0U;
#endif
        for (uint8_t s = 0; s < port_cnt; s++) {
            g_bsrr_tab[s][st].port = ports[s];
            g_bsrr_tab[s][st].val  = 0U;
        }
        for (uint8_t k = 0; k < _22_1665_LINE_COUNT; k++) {
            if ((wlines & (uint16_t)(1U << k)) == 0U)
                continue; /* 未参与：一个位都不写 */
            if (line_slot[k] == 0xFFU)
                continue; /* 端口超表容量被丢弃（防御） */

            const hub75_pin_t *pin = _22_1665_line_pin(k);
            const uint32_t mask    = (uint32_t)pin->pin;
            const bool on          = (((st_bits >> k) & 1U) != 0U) ^ (_22_1665_DATA_ACTIVE_HIGH == 0U);
            g_bsrr_tab[line_slot[k]][st].val |= on ? mask : (mask << 16U);
        }
    }

    /* ③ 裁掉没有参与脚的端口槽（保持端口槽连续，scan 只遍历前 cnt 个） */
    uint8_t used = 0U;
    for (uint8_t s = 0; s < port_cnt; s++) {
        bool any = false;
        for (uint16_t st = 0U; st < (uint16_t)_22_1665_STATES; st++)
            any = any || (g_bsrr_tab[s][st].val != 0U);
        if (!any)
            continue;
        if (used != s)
            memcpy(&g_bsrr_tab[used], &g_bsrr_tab[s], sizeof(g_bsrr_tab[0]));
        used++;
    }
    g_bsrr_port_cnt = used;
}

#if _22_1665_PIN_SELFTEST_ON
/** @brief 回读前的稳定等待（长排线容性负载；~10µs 量级，仅 init 期一次性开销） */
static void _22_1665_settle(void)
{
    for (volatile uint32_t i = 0U; i < 400U; i++) {
    }
}

/**
 * @brief  数据脚回读自检：对**参与输出的脚**（`s_out_lines`；多链模式 = 有效链的数据脚，
 *         探针/兼容 = 候选掩码）逐脚「写 1 → 读回 → 写 0 → 读回」，RTT 逐脚一行：
 *         `pin=R1 (PG9) write1/read=1 write0/read=0 PASS`。
 *
 *  PASS = 两个电平均按写入值读回 → MCU 侧该引脚**确实被驱动**（GPIO 模式为输出、
 *  端口/位号映射正确、引脚未被外部强行拉死）——用于排除「某些脚根本没被驱动」的
 *  软件病因；FAIL / SKIP 行须原样回报。自检只翻电平，结束时全部拉低，不影响之后 scan。
 */
static void _22_1665_pin_selftest(void)
{
    SEGGER_RTT_printf(0, "[22_1665] pin self-test: out_lines=0x%03X lines=%u\n",
                      (unsigned)s_out_lines, (unsigned)__builtin_popcount((unsigned)s_out_lines));
    for (uint8_t k = 0; k < _22_1665_LINE_COUNT; k++) {
        if ((s_out_lines & (uint16_t)(1U << k)) == 0U)
            continue;
        const hub75_pin_t *pin = _22_1665_line_pin(k);
        const pl_port_t port   = _22_1665_pl_port(pin->port);
        const uint8_t n        = _22_1665_pin_index(pin->pin);
        if (port >= PL_PORT_MAX) {
            SEGGER_RTT_printf(0, "[22_1665] pin=%s (P%c%u) SKIP: port not in pl_gpio table\n",
                              _22_1665_line_names[k], _22_1665_port_char(pin->port), (unsigned)n);
            continue;
        }
        pl_gpio_write(port, n, true);
        _22_1665_settle();
        const unsigned r1 = pl_gpio_read(port, n) ? 1U : 0U;
        pl_gpio_write(port, n, false);
        _22_1665_settle();
        const unsigned r0 = pl_gpio_read(port, n) ? 1U : 0U;
        SEGGER_RTT_printf(0, "[22_1665] pin=%-2s (P%c%u) write1/read=%u write0/read=%u %s\n",
                          _22_1665_line_names[k], _22_1665_port_char(pin->port), (unsigned)n, r1, r0,
                          (r1 == 1U && r0 == 0U) ? "PASS" : "FAIL");
    }
    SEGGER_RTT_printf(0, "[22_1665] pin self-test done (all tested pins left LOW)\n");
}
#endif /* _22_1665_PIN_SELFTEST_ON */

void dev_display_22_1665_init(void)
{
    g_22_1665.me.ops = &_22_1665_ops;
    dev_display_register(&g_22_1665.me);

    _22_1665_chain_init();       /* 链表校验 + 落点表 + 参与脚集合（含 RTT 自检行） */
    _22_1665_build_bsrr_table(); /* 依赖 s_out_lines → 必须在 chain_init 之后 */
#if _22_1665_CHAIN_PROBE == 8
    _22_1665_probe8_init(); /* 内部按第一根被测脚再建一次表（含序号绘制 / RTT 首行） */
#elif _22_1665_CHAIN_PROBE == 9
    _22_1665_probe9_init(); /* 内部按第一个「链 × 脚」组合再建一次表（含双数字绘制 / RTT 首行） */
#endif
#if _22_1665_PIN_SELFTEST_ON
    _22_1665_pin_selftest();
#endif
}
hw_dev_initcall(dev_display_22_1665_init);
