# STD 架构冲突审计（2026-10-08）

**范围**：`/home/yystation/Program/3833024/Project-STD-main` 主固件工程的构建口径、显示模组、协议接入、
内存预算、EIDE/Makefile 双构建入口、代码-文档一致性。
**方法**：全部只读；证据 = 源码行号 + 只读脚本输出 + 本次实构/实编译输出（产物落 `/tmp/audit_build/`）。
**未做**：未改任何源码/配置/文档；未`make clean`；未 git 网络操作；未烧录。

**本次实证构建（口径与代价，供复核）**

| 构建 | 命令 | 产物 | 结果 |
|---|---|---|---|
| ALL/22_1665(8×4) | `make PROTO=ALL DISP=22_1665 -j8 BUILD_DIR=/tmp/audit_build/all221665` | `/tmp/audit_build/all221665` | **链接失败**：`region CCMRAM overflowed by 17700 bytes` |
| CQ/22_1665(8×4) | `make PROTO=CQ DISP=22_1665 -j8 BUILD_DIR=/tmp/audit_build/cq221665` | `/tmp/audit_build/cq221665` | **链接失败**：`overflowed by 15444 bytes` |
| ALL/1_263（基线） | `make PROTO=ALL DISP=1_263 -j8 BUILD_DIR=/tmp/audit_build/all1263` | `/tmp/audit_build/all1263` | 成功：text 167752 / rodata 195776 / data 1656 / **ccmram 39652** / bss 124768 / heap_stack 2560（与 doc/06-04 §8.4.4 逐项一致），3 条既有 HAL 告警 |
| 无显示缅甸等价口径 | `make -f .analysis/myanmar/build_nodisp/Makefile.mm_nodisp BUILD_DIR=/tmp/audit_build/nodisp CONFIG=EIDEMyanmarNoDisp -j8` | `/tmp/audit_build/nodisp` | 成功：text 131928 / rodata 191968 / data 1584 / **ccmram 16084** / bss 115704 / heap_stack 2560（CCM 与 doc 一致） |
| 单 TU 编译 | 4 个显示驱动各编译一次（`-Og -DSTD_ALL_PROTO …`） | `/tmp/audit_build/drv/*.o` | 驱动对象 `.ccmram`：1_263 **2496** / 22_1703 **31648** / 22_1665(8×4) **46080** / 1_969 **38208** B |
| PROTO 非法值 | `make PROTO=bogus BUILD_DIR=/tmp/audit_build/protobogus -j8` | `/tmp/audit_build/protobogus` | 成功，stamp 记 `PROTO=bogus`，源集 = ALL（LDI 7 次编译命中、Myanmar 5 次） |

**现有 EIDE 产物**（未改动，仅读取）：`build/Debug/Project_STD.elf`（2026-10-08 09:10:49，EIDE 写）=
text 146352 / rodata 103920 / data 1608 / **ccmram 52972** / bss 115960 / heap_stack 2560；
其源清单 = `build/Debug/builder.params` 206 条（与 `.eide/eide.yml` 推导逐条一致）。

**读-only 脚本复跑**：`check_eide_targets.py` **39/45**（6 条失败，全部指向 Debug 口径）；
`mm_frame_sim.py` 157/157；`check_display_guards.py` 16/16 + 95/95；`yn_ol_frame_sim.py` 184/184。

---

# 一、冲突清单

## C1【高】22_1665 工作区宏值 8×4（128×64）在任何 make 口径下都链接失败，且文档给出的溢出量已过期

**一句话本质**：显示驱动几何宏被外部改成 8×4（本模组 CCM 46080B），CCMRAM 装不下；文档写的
溢出 16380B / 14124B 是**加缅甸模块之前**的旧值，真实值是 17700B / 15444B。

**证据（实证）**
- 驱动宏现值：`Device/Display/dev_display_22_1665.c:33-34` = `MODULE_ROWS 8U` / `MODULE_COLS 4U`；通式
  `1408M + 256·COLS` ⇒ M=32 ⇒ 46080B；单 TU 编译实测 `.ccmram 46080`。
- 实构：`PROTO=ALL DISP=22_1665`（本次）= **overflowed by 17700 bytes**；`PROTO=CQ DISP=22_1665` =
  **overflowed by 15444 bytes**。算术自洽：ALL 非显示部分 = 39652（实测基线）− 2496（1_263 驱动实测）
  = **37156B**，+46080 −65536 = 17700 ✓；CQ 非显示 34900B ⇒ 15444 ✓。
- 链接脚本：`Compiler/STM32F407XX_FLASH.ld:67` `CCMRAM LENGTH = 64K`，无 CCM 断言 ⇒ 唯一反馈是链接期报错。
- EIDE 口径算术（**推算，未实构**）：现行 Debug 产物 ccmram 52972（含 1_969 38208）⇒ 换 22_1665 8×4 后
  ≈60844B（余 4692B）⇒ **EIDE Debug 子集装得下**（驱动 CCM 是定长数组，与 -O3/-Og 无关）。

**证据（文档声称，已过期）**：`doc/CLAUDE.md:23/316/477`、`doc/06-04:1007-1015/662`、`doc/01:16/1392`、
`.analysis/22_1665/verify_all.sh:160` 均写「ALL 溢出 16380B / CQ 14124B、只有 EIDE Debug 子集能装
（57484B）」。16380 的来源经核对 = `.analysis/22_1665/build_round34_baseline.log:34`（2026-09-18 实测），
当时**缅甸模块尚未加入**（MM 占 CCM 1320B）⇒ 与现值的差恰为 1320B。

**影响面**：构建（`make DISP=22_1665` 两口径全废）；交付（若现场要 128×64 = 8×4 拼屏，当前无任何
make 口径可出镜像）；维护（文档数字会误导排障）。严重度 **高**。

**可选项**
- **A** 宏值改回 **8×2**（128×32，23040B）：文档口径称整片 `.ccmram 58876`（第三十二轮实测链接通过）；
  代价 = 与 8×4 的屏体不一致（若屏真是 8×4 则显示不全）。
- **B** 宏值改回 **1×2**（16×32，3328B，唯一有现场联调证据的几何，第二十七轮）：代价 = 若屏已按
  128×64 拼装则完全错。
- **C** 保持 8×4，接受「make 不可构建」，只用 EIDE Debug 子集出镜像（算术 60844B）：代价 = 口径
  与 make 分叉、无 `tree=` 指纹、且 8×4 的实时性未闭环（见 C11）。

**推荐**：**先拍板屏体几何，再二选一改宏值**；若几何确为 8×4 → C（并补 C11 的实测），否则 A/B。
理由：几何是"用户旋钮"，代码/文档都不能替它决定；唯一确定不可接受的是现状（代码 8×4、文档三处
互相矛盾、make 全废）。
**需要用户裁决**：这块屏的实际拼装到底是几行×几列模块？（128×64 / 128×32 / 16×32 / 16×80 任选一）

---

## C2【高】EIDE Debug 现行口径与文档「2026-09-28 用户裁决」完全相反（文件已被 EIDE 于今日重写）

**一句话本质**：文档写「Debug 编入缅甸、排除 LDI+四川三（193 源文件）、编缅甸版厂测」，**实际
`.eide/eide.yml` 与今日 09:10 的 EIDE 构建产物都是反面**：排缅甸、编 LDI+四川三、编标准版厂测、
且仍带 `PROTO_CHONGQING`（206 源文件）。

**证据（实证）**
- `.eide/eide.yml` Debug：`411` = `- PROTO_CHONGQING`；excludeList（448-469）含
  `ProtocolParser_Myanmar`(469)、`app_factory_test_mm.c`(468)、`ProtocolParser_ChongQing`(460)；
  **不含** ldi / SiChuang ETC·MTC·治超 ⇒ 三者被编译；**不含** `dev_display_1_969.c`
  （反证：Debug 编 1_969，排除 1_263/1_260/22_1703/22_1665/1_577/p20）。
- `build/Debug/builder.params`（EIDE 自己写的，2026-10-08 09:10:47）= `sourceList` **206** 条：
  LDI 5、Myanmar 0、ChongQing 0、SiChuang 10、`dev_display` 2（基类+1_969）、
  `app_factory_test.c` 1、`defines` 含 `PROTO_CHONGQING`。
- 文件时间戳：`.eide/eide.yml`、`builder.params` 同为 **2026-10-08 09:10:47**，elf 09:10:49 ⇒ 今日
  EIDE 打开/构建时写的。`git diff` 显示 yml 相对 HEAD 改了 192 行（新增缅甸条目 + 全套 excludeList）。
- `check_eide_targets.py`（只读复跑）**39/45**，6 条失败 = 「Debug 未编缅甸版厂测 / 未排除标准版 /
  未编缅甸模块」——与文档断言方向完全相反。
- `targets.Myanmar`（582-642）本身自洽：不排缅甸、排全部显示驱动、排标准版厂测、defineList 无
  `PROTO_CHONGQING` ✓（`--emit-make` 的等价口径实测链接通过，ccmram 16084）。

**证据（文档声称）**：`doc/CLAUDE.md:1066/1069/1210`、`doc/16:28/353/354/442/683` 均以「现行口径 = 编缅甸 +
排 LDI + 四川三（193 源文件）」为准，并把 206 条口径标为"历史"。

**影响面**：构建（到底哪份是"现行"？）；交付（现场烧的是 1_969 屏 + LDI 还是 1_969 屏 + 缅甸灯控，
功能集完全不同）；维护（文档/脚本期望值与文件状态持续打架）。严重度 **高**。

**可选项**
- **A** 恢复文档口径（在 EIDE GUI 把 Debug 改成"编缅甸 + 排 LDI/四川三 + 排标准版厂测"，再
  `EIDE: Reload Project`），脚本回 45/45。
- **B** 采纳现行文件口径，**改文档**（把 193 条那套全部标注为历史，现行 = 206 条）——但必须同时
  处理 `PROTO_CHONGQING`（见 C3）。
- **C** 折中：Debug 保持"编 LDI + 四川三 + 1_969"但**去掉 `PROTO_CHONGQING`**（dev 语义自洽），
  缅甸功能只走 `targets.Myanmar`（其厂测/干接点/无显示都齐）；文档同时改写。

**推荐**：**C**（零功能损失、消除混编）+ 把 `check_eide_targets.py` 的期望值改成事实口径。
理由：Debug 的用途是"现场调试编得全"，缅甸的完整能力已在 `targets.Myanmar` 里；带 CQ 宏却排 CQ
模块是纯负债（C3）。
**需要用户裁决**：Debug 口径以哪一份为准（A/B/C），以及是否允许把文档改成事实口径。

---

## C3【高】Debug 口径是"混编"：定义 `PROTO_CHONGQING` 却排掉 CQ 模块 + 同时编入 LDI ⇒ LDI 上报 IP 与实际不一致、20103/9528 两个 UDP 口无人消费

**一句话本质**：`PROTO_CHONGQING` 是**量产语义开关**（默认 IP 192.168.1.5、CQ 业务口读 Sector1），
Debug 却把它和 LDI 混编并排掉 CQ 协议模块 ⇒ 同一台设备两套 IP 说法、两个绑了没人认领的 UDP 口。

**证据（实证，代码路径可推）**
- 宏与模块矛盾：`.eide/eide.yml:411`（定义）vs `460`（排除 CQ 目录）⇒ `app_net_boot.c:52-59` 走
  **CQ 默认 IP 192.168.1.5**，而 `Device/Network/dev_eth.c:21` 硬编码上电 netif = **192.168.114.200**。
- 顺序：`app_boot.c:110-140`：`dev_eth_start()`（114.200）→ `sw_board_init()`（`ldi_module_init` →
  `ldi_ctx_init`）→ `app_net_boot_apply()`（写 Sector1 = 192.168.1.5 并应用）。
- LDI 的缓存不跟随：`app_ldi.c:210-218` 分支 3（Sector1 与 W25 皆空）把 `cfg.device_ip` 取
  `pl_net_get_ip()` 的当时值（= 114.200）且**不写 Flash**、`cfg_valid = true`；随后 app_net_boot 把
  netif 改成 192.168.1.5 ⇒ `app_ldi_cmd.c:1129-1135`（0x12 搜索应答）**报 114.200**，而
  `app_iap_cmd.c:82-96`（0x01）读 Sector1 **报 192.168.1.5**。同一设备两个上报源互相矛盾（现场
  用 LDI 搜索口找设备时会找到错误 IP）。
- 无人消费的端口：`app_boot.c:153-159` 无条件启动 5 条网络通道；本口径下 CQ 模块不在镜像里 ⇒
  `CH_ID_UDP_CQ`(20103) 与 `CH_ID_UDP_GZOL`(=net_cfg.port，默认 9528) 都**绑定成功但没有协议绑定**，
  收到数据静默丢弃（`[disp] ch=N has NO protocol bound` 告警已随 2026-09-29 瘦身轮删除 ⇒ 现场零线索）。
- 反过来：`app_net_boot.c:11` 的注释**自己承认**这种状态存在（"否则「PROTO_CHONGQING 与 LDI 共存」
  的混合构建（如 EIDE Debug 口径错配）…"）⇒ 代码作者已知这是错配。

**影响面**：运行（IP 上报不一致、两个空转 UDP 口、占 2 个 netconn 配额见 C7）；现场交付（用 LDI
搜索工具找设备会对不上 IP）。严重度 **高**（若 Debug 就是现场常用镜像）。

**可选项**
- **A** Debug 去掉 `PROTO_CHONGQING`（GUI 改 defineList）⇒ 恢复 dev 语义（114.200 + 无 CQ 端口语义），
  LDI/IAP 两个上报源一致。
- **B** Debug 改成"编 CQ、排 LDI"（= `make PROTO=CQ` 的等价）⇒ 语义自洽但丢掉 LDI 调试能力。
- **C** 代码层加固（治本）：把「构建口径」与「实际应用」绑定，例如 `app_net_boot` 应用后回写 `g_ldi.cfg`
  的 IP/端口（或 LDI 0x12 直接读 Sector1），并给"通道无协议绑定"恢复一个 `volatile` 计数器供调试器读。
- **D** 把 CQ/GZ_OL 通道改为"有协议绑定才启动"（`app_boot.c` 按注册结果决定），从根上消除空转口。

**推荐**：**A（立即）+ C/D（本轮或下轮加固）**。理由：A 一行配置即可消除全部现场可见矛盾；
C/D 把"口径错配"从"注释里承认的隐患"变成不可表达的状态，代价小。
**需要用户裁决**：是否接受 Debug 去 `PROTO_CHONGQING`；是否授权改 `app_boot.c`/`app_net_boot.c`（C/D）。

---

## C4【中】缅甸干接点接管 PE12/PE11/PE10 与 RLS 干接点轮询在同一构建里语义打架（ALL 构建持续显示「车道关闭」）

**一句话本质**：缅甸模块把三个原按键脚改成输出（上电红灯亮 = PE12 高），RLS 又把同三脚当输入读
⇒ ALL 开发构建里 RLS 会一直判「车道关闭」。

**证据（实证）**
- 输出侧：`Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c:47-54`（S1/S2/S3 = PE12/PE11/PE10，
  `active_low=false`）、`100-140`（接管：写灭电平 → 逐线摘 EXTI → 推挽输出 → **红点亮**）。
- 输入侧：`Device/IO/dev_key.c:62-64`（SW1/2/3 同脚、`active_low=true`）；`app_key.c:13-18` 直读封装。
- 消费者：`Application/Src/RLS/app_rls_cmd.c:88-104`（SW3 最高优先级）+ `189-192`（读三脚）⇒
  上电默认态（PE12 高、PE11/PE10 低）= SW1"未闭合"、SW2/SW3"闭合" ⇒ **渲染「车道关闭」**。
- 编译集：`make PROTO=ALL` 同时编入两者；`targets.Myanmar` 的 excludeList 含 `rls`(630) ✓ ⇒
  缅甸目标无此问题；**当前 EIDE Debug 也排除了 rls(467)** 但同时排除了缅甸 ⇒ 该问题只在 make ALL 口径出现。
- DIP1：MM 要求 9600（文档约束），`app_uart_baud` 是全局 DIP1 选择（ON=115200/OFF=9600）——与 RLS 无关，
  但同属"现场必须知道"的约束。

**影响面**：运行（ALL 开发构建画面被 RLS 抢占；若哪天现场用 ALL 镜像 + 缅甸协议则误显示）。
严重度 **中**（有构建隔离兜底，但依赖"记得排除 RLS"）。

**可选项**
- **A** 维持现状：量产/缅甸构建排除 RLS（文档已写）；代价 = 继续依赖排除表（见 C9）。
- **B** 代码层解耦：缅甸接管时置一个"脚位已被借用"标记（如 `pl_gpio_is_borrowed()` 或 MM 导出的
  `mm_owns_contact()`），RLS 轮询先查标记 ⇒ 全部构建下都正确；代价 = 改 RLS + Platform 小接口（约 20 行）。
- **C** 把 RLS 干接点轮询做成编译期可选（`-DRLS_DRY_CONTACT=0`）；代价 = 多一个开关（与瘦身轮"删开关"方向相反）。

**推荐**：**B**，A 作为过渡。理由：B 把"构建期互斥"降级为"运行期共存"，成本低、彻底、不新增开关。
**需要用户裁决**：是否允许改 RLS/Platform（B），还是维持"靠排除表"（A）。

---

## C5【中】`{` 帧族互斥守卫在默认 make 口径下被 `STD_ALL_PROTO` 关闭 ⇒ 云南常规与云南治超在同一镜像里"谁先注册谁认领"

**一句话本质**：六个 `{` 族模块各定义一个 `g_brace_proto_guard`，但 `make` 默认定义 `STD_ALL_PROTO`
把守卫编译掉，串口上同首字节的命令只能靠**注册序**决定归属，云南常规会先吃掉云南治超的 `'1'~'5'/'8'/'A'` 帧。

**证据（实证）**
- 守卫定义（`#ifndef STD_ALL_PROTO` 包裹）：`app_qh_proto.c:20`、`app_sd_proto.c:31`、`app_gz_proto.c:32`、
  `app_yn_proto.c:33`、`app_sc_mtc_proto.c:34`、`app_yn_ol_proto.c:78`（6 个）。
- `Makefile:44/49`：默认 `PROTO ?= ALL` 且 `DEFINES` 恒含 `-DSTD_ALL_PROTO` ⇒ 守卫失效、6 个族全部编入。
- 注册序 = 源收录序：`Makefile` 内 `ProtocolParser_QingHai` 在最前，`YunNan` 在 `YunNan_Overload` 之前
  ⇒ 串口链式 probe 中青海/云南常规先试。YN_OL 的 `'3'`/`'4'` 等帧会被 `app_yn_proto` 认领并按其语义
  （FONT_24 等）渲染。
- 量产靠 EIDE 排除表（当前 Debug：`459` 排 `ProtocolParser_YunNan`、`463` 排 `YunNan_Overload` ⇒ 两者都被排、
  实际无人认领 `{` 帧！）——**这正是 2026-09-18 现场"TCP 连上无响应"的同类成因**（那次是漏编 YN_OL，
  告警打印现已删除）。
- 反向安全设计（实证，成立）：MM 与四川治超同 `0xFF` 首字节靠"正签名"隔离
  （`app_mm_proto.c:205-235` 要求 `[15..18]==0`，否则 FAKE 不消费）✓；CQ 只绑 UDP（`app_cq_proto.c:112/116`）、
  YN_OL 不绑 UDP（`app_yn_ol_proto.c:127-149`）⇒ 「通道掩码隔离」成立 ✓。

**影响面**：运行（ALL 构建串口语义串味）；交付（量产靠人工维护排除表，漏一个就是"零响应"）。
严重度 **中**。

**可选项**
- **A** 维持（EIDE 排除表 + 文档纪律）：代价 = 继续依赖人工 + 无告警（RTT 打印已删）。
- **B** 给 `{` 族 probe 增加更严判别（命令字 + 长度 + 载荷形态）：代价 = 六个模块都要改、仍有理论歧义。
- **C** 新增 make 口径开关（如 `PROTO=YN_OL` / `PROTO=SD`）实现"单协议量产构建"，把量产从 EIDE 手工
  排除改成可复现命令：代价 = Makefile 增一段白名单逻辑（约 20 行）+ 文档。
- **D** 恢复一个"通道无协议绑定 / 全 FAKE"的 `volatile` 计数器（调试器可读），替代已删的 `[disp]` 告警。

**推荐**：**C + D**（让量产可复现、让漏编可观测），A 保留为兜底。理由：C5 的根因不是 probe 写错，
而是"量产口径靠 GUI 手工维护 + 无可观测性"。
**需要用户裁决**：是否新增 make 单协议口径（C）与恢复轻量可观测点（D）。

---

## C6【中】CCMRAM 是全局瓶颈：25.3KB 静态任务栈 + 显存 + 协议队列挤在 64KB，且**无编译期守卫**；`configUSE_TIMERS=1` 空转白占 1124B

**一句话本质**：CCM 是最紧的资源（1_263 口径已用 39652/65536，22_1665 直接溢出），唯一反馈是
链接期报错；同时一个全项目从不使用的定时器服务任务静态占着 1124B CCM。

**证据（实证）**
- `ld:67` CCMRAM 64K；无任何 `ASSERT`/`_Static_assert` 覆盖整片占用（22_1665 的 17 条断言已按用户裁决删除，
  `dev_display_22_1665.c` 现状 grep `_Static_assert` = 0）。
- 实测占用：ALL/1_263 = 39652（本次构建）；8×4 溢出 17700；无显示口径 = 16084；缅甸队列 1316 + 栈 1124。
- `osTimerNew`/`xTimerCreate` 全项目 **0 命中**（grep），而 `FreeRTOSConfig.h:95/98` `configUSE_TIMERS=1` +
  `configTIMER_TASK_STACK_DEPTH=256`，`app_boot.c:179-180` 把定时器任务栈/TCB 强定义到 CCM ⇒
  `nm` 实测 `s_timer_task_stack 0x400`(1024B) + `s_timer_task_tcb 0x64`(100B) = **1124B CCM**
  （另 `s_idle` 同类 612B 是必需）。文档称可省「3KB CCM」（`doc/CLAUDE.md:613`）与实际 1124B 不符。

**影响面**：构建/交付（CCM 余量决定可编口径）；维护（误以为还有 3KB 可挖）。严重度 **中**。

**可选项**
- **A** `configUSE_TIMERS=0`：省 1124B CCM + 约 240B SRAM（定时器队列在 timers.c 的 .bss）。代价 = 将来要用
  `osTimerNew` 需改回（一行），且 `osTimer*` API 变不可用（当前 0 使用）。
- **B** 给 22_1665 恢复 1~2 条编译期守卫（`MODULE_COLS ≤ 5` + CCM 预算），让错误宏值在编译期而非链接期发现。
  代价 = 与"第三十四轮删防御"的裁决相反，需重新拍板。
- **C** 现状（只在链接期发现）：代价 = 8×4 这类宏值会以"链接失败"的形式暴露，且文档数字可能再次过期。

**推荐**：**A + B**。理由：A 是纯收益（当前零使用）；B 只加 2 条断言就能防止"越界读 `_22_1665_lines`"
这类**静默**错误（`COLS ≥ 6` 时不会链接失败，只会越界）。
**需要用户裁决**：是否接受 `configUSE_TIMERS=0`；是否允许给 22_1665 加回少量编译期守卫。

---

## C7【中】SRAM 余 2088B、netconn 池峰值 = 容量 8（零冗余）；一旦 LDI 配置了远端或再加一个 UDP 口，池满且**无任何日志**

**一句话本质**：`MEMP_NUM_NETCONN = 8` 恰好等于"4 常驻 + 1 TCP 连接 + 1 TCP Client + 2 广播回退"的峰值；
LDI 装载 W25 配置时会 `set_remote` 多占 1 个 ⇒ 峰值 9 > 8，`netconn_new` 返回 NULL 而**现场无输出**。

**证据（实证）**
- `Platform/Inc/lwipopts.h:146-147` = 8/8；SRAM 实测余量：data 1656 + bss 124768 + heap_stack 2560
  = 128984 / 131072 ⇒ **余 2088B**（96.2% bss+data 占用）。
- 常驻 conn 清单：`app_boot.c:153-157` 起 5 条通道；`app_udp.c:137-190` 每 UDP 通道 1 个 conn（`netconn_new`）;
  `app_tcp_server.c:109/128` listener + accept 各 1；`app_tcp_client.c:103` 仅在远端非哨兵时创建。
- 多 1 个的来源：`app_ldi.c:143`（W25 有效时 `app_tcp_client_set_remote(host_ip, host_port)`）⇒
  `tcp_client_task` 建 conn（`app_tcp_client.c:103`）。默认是哨兵 `0.0.0.0:0`（不连），但只要现场 W25
  里存过 host 就会 +1。
- 失败静默：`app_tcp_server.c:110-118` 池耗尽只 `osDelay(500)` 重试（原注释说明现场判读靠 RTT，而 RTT 打印已于
  2026-09-29 删除）；`app_udp.c:92-96` 广播回退失败直接 return。

**影响面**：运行（偶发"连不上/丢包"，无日志）；排障（需调试器读变量）。严重度 **中**。

**可选项**
- **A** `MEMP_NUM_NETCONN` 8→10：代价 = 约 +300B（netconn 结构 + memp 开销，需实测）+ SRAM 只剩 ~1.7KB。
- **B** 明确"TCP Client 不做常驻"（保持哨兵、LDI 不落 host 到 W25），并在文档/上位机流程里固定该语义：
  代价 = 失去 LDI 直连上位机能力（现场若在用则不可行）。
- **C** 加 `volatile` 观测点（`netconn` 创建失败计数 / 各通道绑定状态），替代已删日志：代价 ≈ 10 行。

**推荐**：**C（先做，零风险）+ B 或 A 按现场实际需要择一**。理由：无论扩容还是收敛，都必须先恢复
"池满可观测"，否则这个问题永远只能靠猜。
**需要用户裁决**：现场是否使用 LDI host 直连（决定 B 可行性）；是否允许扩池（A）。

---

## C8【中】`net_cfg.port` 一个字段两个消费者：TCP 业务口与贵州治超 UDP 业务口同号

**一句话本质**：GZ_OL 的 `0x40 改端口`改的是 `net_cfg.port`，而同一字段又驱动 TCP Server 监听口
⇒ 改贵州治超的服务口会**连带**改掉 TCP 业务口（LDI / YN_OL / 云南等所有走 TCP 的协议）。

**证据（实证）**
- 字段定义与语义：`Application/Inc/Config/app_board_net_cfg.h:32-34`（port = TCP 业务口 + 贵州治超专用 UDP 口）。
- 消费者 1：`app_net_boot.c:76-77`（`app_tcp_server_set_port(cfg.port)`）；
  消费者 2：`app_udp.c:441-457`（`_udp_gzol_read_port` 读同一字段）。
- 冲突处置：`app_udp.c:446-449`（与 10011 / CQ 口同号则跳过绑定，`BIND_SKIPPED`）——设计已考虑同号，但
  **没有**考虑"改 GZ_OL 端口 = 改 TCP 端口"的跨协议副作用（改到 10011 时 GZ_OL 静默退化，改到别的值
  则 TCP 口跟着变）。
- 三固件布局：72B 记录（`app_board_net_cfg.h:68-77` 断言）与 Bootloader/Recovery
  `config_info.h` 完全一致（20/40/72 + 偏移 48/68/12/16，本次逐一核对）✓。

**影响面**：运行（跨协议副作用）；交付（现场改端口须知影响面）。严重度 **中**。

**可选项**
- **A** 保持（文档已注明"同号不同协议栈"）：代价 = 跨协议耦合继续存在，误配只表现为"另一个口不通"。
- **B** 拆字段（Sector1 布局 72B → 76B）：语义最干净；代价 = **三固件必须同步烧录 + 旧固件反复重建记录**
  （Bootloader 条件 D 校验风险，见 CLAUDE.md 的既有纪律），且需 Bootloader/Recovery 一并改。
- **C** 保持字段但限制：`0x40` 只允许改到"非 TCP 影响面"的值，或在 `0x40` 应答里回显"TCP 口将随之改变"：
  代价 = 上位机联调。

**推荐**：**A（现状）+ 文档显式写出副作用**，除非现场确实要"GZ_OL 口与 TCP 口分开"→ 才做 B。
理由：B 会触发三固件同步这一高风险纪律，收益不足以覆盖成本。
**需要用户裁决**：现场是否需要"贵州治超 UDP 口独立于 TCP 业务口"。

---

## C9【中】EIDE 回写 vs 外部编辑的脆弱性已被**今天的事实**再次证明；量产口径仍靠人工排除表

**一句话本质**：`.eide/eide.yml` 是 EIDE 的运行时状态（今天 09:10:47 又写了一次），任何"文档/脚本
期望的口径"都可能在一次 GUI 打开/构建后失效——C2 就是活证。

**证据（实证）**
- mtime：`.eide/eide.yml` = `build/Debug/builder.params` = **2026-10-08 09:10:47**；elf 09:10:49。
- `git diff .eide/eide.yml` = 186 增 / 6 删（缅甸 folders + incList + Debug/Myanmar 两套 excludeList +
  factory 条目）⇒ 该文件相对 HEAD 已是另一套配置。
- 脚本侧：`check_eide_targets.py` 45 条断言中 6 条失败，全部是"Debug 口径差异"——脚本设计者已把这种
  差异当"已知"（40/45 预期），**说明这个漂移是长期存在的常态而非一次性事故**。
- 反向证据（好的一面）：`files` 里 249 条路径在磁盘上**全部存在**（本次脚本核对，missing = 0）⇒ 不存在
  指向已删文件的悬空条目。

**影响面**：构建/交付（镜像身份不可预期）；维护（文档-文件-脚本三方长期不一致）。严重度 **中**。

**可选项**
- **A** 维持现状（脚本 + `Reload Project` 纪律）：代价 = 每次都要人盯，且文档会持续过期。
- **B** 把"期望口径"写进脚本并在提交前跑（现在脚本已具备断言能力，只差"期望值改成事实 + 纳入流程"）：
  代价 = 一轮口径确认 + 一句 CI/钩子。
- **C** 量产完全走 make（`make PROTO=… DISP=… FACTORY=…`，口径指纹已在 `.build_stamp`），EIDE 只做调试：
  代价 = 需要 C5-C 的单协议 make 口径 + 烧录流程改走 `.hex`（`tool/flash_all.sh` 已支持）。

**推荐**：**B 立即 + C 中期**。理由：B 让"漂移"变成可检测；C 让量产口径可复现（EIDE 的 GUI 状态
不再是唯一真源）。
**需要用户裁决**：量产镜像以 make 为准还是 EIDE 为准（决定 B/C 的力度）。

---

## C10【中】工作区几何宏与文档表/历史结论三方不一致（1_263 = 1×1、22_1665 = 8×4、22_1703 = 7×4），且无任何编译期提示

**一句话本质**：三个显示驱动的"分辨率"是手工宏，代码现值与文档表互相矛盾，谁也不知道哪份是"用户的屏"。

**证据（实证）**
- `dev_display_1_263.c:35-38` = ROWS 1 / COLS 1 ⇒ 32×32（CCM 2496B 实测），而 `doc/CLAUDE.md` 的模组表
  写「1-263 = 224×64（7×32 / 2×32）/ CCM 29568B」，同文件另有一处 `[待核对 2026-09-16]` 承认"工作区
  实为 1×1"（= 该矛盾已被记录但未闭环）。
- `dev_display_22_1665.c:33-34` = 8/4（128×64）；文档三处不同口径（`doc/CLAUDE.md:95` 说 1×5、
  `doc/01:203-204` 说 8/4、`doc/01:291` 表头说"现行工作区值 = 8×2"）——**实测 = 8×4**。
- `dev_display_22_1703.c:39-40` = 7/4（224×64）⇒ 与文档主口径一致，但同表又注"用户 WIP，可改 1×1"。
- 无编译期提示：几何宏是普通 `#define`（`-D` 不生效，改文件生效），驱动内无断言（22_1665 已删、1_263/22_1703
  本就没有）⇒ 改错只表现为显示不对或链接失败。

**影响面**：构建/交付（决定显存与可构建口径）；维护（新人无法从代码判断"当前意图"）。严重度 **中**。

**可选项**
- **A** 逐个拍板宏值 → 同步代码 + 文档表 + 构建口径（一次收口）。
- **B** 保持宏值现状，仅在三个驱动文件头各加一行"本文件当前几何 = W×H（CCM = N B）"并在 doc 表里
  与之互指：代价 = 仍无强制。
- **C** 加编译期自检（如 1_263 的 224×64 表值与宏不一致时报错/告警）：与"删防御"裁决方向相反。

**推荐**：**A**（先定几何，其它冲突 C1/C11 都依赖它）；1_263 的"表 224×64 vs 代码 1×1"必须二选一修掉。
**需要用户裁决**：三块模组的实际屏体几何各是什么。

---

## C11【中】实时性未闭环：22_1665 在 8×4 下的 scan 估算被文档低估（按每组线性放大 ≈0.7~0.9ms > TIM3 的 500µs）

**一句话本质**：2 组时每时钟 4 次 BSRR 写、文档给 55 指令/时钟与 45~60µs/帧；8×4 是 **4 组**，
每时钟写次数翻倍而时钟数不变（1024）⇒ 单帧 ≈0.7~0.9ms，超出 TIM3 500µs 周期；文档把 8×4 与 8×2
并列写「0.36~0.48ms」是低估。

**证据（实证）**
- `dev_display_22_1665.c:236-244` scan：外层 `CLOCKS = 128×ROWS(=1024)`，内层 `GROUP_COUNT = MODULE_COLS`
  组，每组 1~2 次 `pl_hub75_bsrr_flush` ⇒ **每时钟工作量 ∝ COLS**。
- 文档自身口径：`doc/CLAUDE.md`（22_1665 段）写「≈55 指令/时钟位（1×2 两组）」「8×2/8×4 ≈0.36~0.48ms
  占 TIM3 72%~96%」；TIM3 周期实测 = `Core/Src/tim.c:82-84`（84MHz/84 × 500 = **500µs**）。
- 8×4 的时钟数 = 128×8 = 1024（与 8×2 同），但每时钟写 4 组 ⇒ 估算应约 2×8×2 ⇒ ≈0.7~0.9ms
  （估算，**未经 DWT 上机实测**）。
- prepare 侧：8×4 整帧重算 ≈1.0~1.2ms（文档值），跨帧提交 ⇒ scan_task 与 prepare 都会挤占 500µs 节拍。

**影响面**：运行（若真用 8×4：画面撕裂/刷新下降）；交付（现场实测前无法承诺）。严重度 **中**
（潜在性能缺陷，尚属"待验证"）。

**可选项**
- **A** 上机 DWT 实测（`pl_dwt` 已在，scan 前后读周期计数即可，无需改动驱动逻辑）：代价 = 一次实验。
- **B** 若 8×4 必须用 → 改设计：TIM3 周期放大到 ≥1ms（刷新率下降）或把 4 组拆两拍（帧时间×2）。
- **C** 维持估算并在 8×4 之外的口径使用（8×2 已接近上限 72%~96%）。

**推荐**：**A（先测）**，再决定 B/C。理由：这是唯一能闭环的路径；数学上 8×4 超周期是大概率事件。
**需要用户裁决**：是否同意做一次 DWT 实测；若超时，接受 B 的哪种代价。

---

## C12【低】文档-代码漂移 6 例（含"已删设施仍当现行"与"同一事实三个值"）

**一句话本质**：多轮改造后文档里混着现行与历史口径，其中数条会直接误导操作。

**证据（实证，逐条 `grep` 复核）**
1. `doc/CLAUDE.md:100` 写「`app_default_display.c` 当前仍是第二十六轮现场的测试画面 `"1\n2"`」——
   **实际**：该文件渲染「欢迎行驶\n高速公路」（`Application/Src/app_default_display.c:21-46`），
   `grep '"1'` 在 `Application/Src/*.c` 中 0 命中。文档声称 vs 代码实证矛盾。
2. `doc/CLAUDE.md:497`（initcall 表）与 `:864`（数据流）写「`app_key` 创建 `key_poll_task` (20ms)」、
   「`dev_key_clear_state()`」——**两符号全仓库 0 命中**（`grep`）；`app_key.c` 实际只有 28 行（DIP 缓存）。
3. `doc/构建开关总表.md:100` 的 `APP_DIAG_TREE_HASH/…` 行**未标"已删除"**，仍写「注入（Makefile
   `DIAG_DEFS`）」——Makefile 已无 `DIAG_DEFS`（全文 grep 0）。
4. 22_1665 溢出量：文档 16380/14124 vs 实测 17700/15444（3 处文档，见 C1）。
5. 22_1665 工作区宏值：`doc/CLAUDE.md:95` 写 1×5、`doc/01:74` 写 1/5/16/16、`doc/01:291` 写"8×2 现行"、
   `doc/01:203-204` 写 8/4 —— **实测 8/4**，四个口径互相矛盾。
6. `doc/CLAUDE.md:826` 写「TIM4（PWM, 频率 = TIM3×8）」——实测 `Core/Src/tim.c:118-120` TIM4 period
   20µs（50kHz）vs TIM3 500µs（2kHz）= **25×**，不是 8×。
   （另：`doc/CLAUDE.md` 堆预算段称关 `configUSE_TIMERS` 可省 3KB CCM，实测 1124B，见 C6。）

**影响面**：维护（照文档操作会踩空）；排障（判据过期）。严重度 **低**（不直接影响镜像，但会误导）。

**可选项**
- **A** 现在批量更正（只更正"会误导操作"的六处）+ 其余历史口径统一加 `〔历史〕` 前缀。
- **B** 逐处加 `[待更新: …]` 标记、分批处理。
- **C** 不处理（继续累积，下次审计再收）。

**推荐**：**A**（限定范围：几何/口径/开关/上报源四类），历史演进内容保留并显式标注。
**需要用户裁决**：是否本轮就做文档收口（以及是否允许改 `doc/`）。

---

## C13【低】构建机制不对称：`PROTO` 无非法值守卫（bogus 静默按 ALL）；同名"Debug"在 EIDE(-O3) 与 make(-Og) 是两套优化

**一句话本质**：`DISP`/`FACTORY` 写错会 `$(error)`，`PROTO` 写错却静默按全协议构建；EIDE 的 Debug
用 -O3，make 的 Debug 用 -Og，两者尺寸/时序不可直接对比。

**证据（实证）**
- `Makefile:44-52`（PROTO 只判 `CQ`）+ `74-76`/`93-95`（DISP/FACTORY 有 `$(error)`）；实构
  `make PROTO=bogus` = 成功、stamp 记 `PROTO=bogus`、源集与 ALL 相同（LDI/Myanmar 均编译）。
- `.eide/eide.yml` Debug `toolchainConfigMap.GCC.options.c/cpp-compiler.optimization = level-3`（= -O3）
  vs `Makefile:142` `CFLAGS += -Og -g`。
- 口径指纹机制本身工作正常：`.build_stamp` 含 CONFIG/TOOLCHAIN/PROTO/DISP/FACTORY/DEFINES/CFLAGS/LDFLAGS/
  MAKEFILE_CKSUM/SRC_FILES；本次四次不同口径构建均按预期重建。

**影响面**：构建（拼错口径 = 编出另一个口径，无提示）；维护（EIDE/make 产物不可互比）。严重度 **低**。

**可选项**
- **A** 给 PROTO 加 `$(error)`（与 DISP/FACTORY 对齐，允许值 ALL|CQ + 未来白名单）。
- **B** 保持现状（文档已把它记为"已知行为，按用户裁决保持"）。
- **C** 把 EIDE Debug 改成 `-Og` 对齐 make（或反向把 make 加 `CONFIG=EIDE` 口径）；**D** 只在文档显式声明
  「EIDE Debug = -O3」并把"EIDE 子集能装"的结论标注为"-O3 口径"。

**推荐**：**A + D**。理由：A 是 3 行改动、消除静默；D 消除"两套 Debug"的误解（C1 的 EIDE 算术依赖它）。
**需要用户裁决**：是否打破"PROTO 保持现状"的既有裁决。

---

## C14【低】"重复注册"语义两个 API 相反（显示实例 last-wins，默认画面 first-wins）且无告警

**一句话本质**：`dev_display_register` 后注册者生效，`app_default_display_register` 首注册者生效——
两类"注册"行为相反，误配时表现为"屏/画面与预期相反"且零提示。

**证据（实证）**
- `Device/Display/dev_display.c:40`：`s_active_display = dev;`（覆盖 = last wins，无判空/无计数）。
- `Application/Src/app_default_display.c:47-50`：`if (s_default_fn == nullptr) s_default_fn = fn;`（first wins）。
- 默认画面提供者 3 个：`app_sd_proto_default.c`、`app_gz_ol_proto_default.c`、`app_mm_proto.c`
  （ALL 构建下山东先占槽，其余静默丢弃）；显示实例提供者 7 个驱动文件（靠 DISP/excludeList 保证只编一个）。

**影响面**：维护（配置错误无提示）。严重度 **低**。

**可选项**
- **A** 保持（文档已写"链接序决定"/"首个生效"）。
- **B** 两个 API 统一 first-wins + 加重复注册计数器（`volatile` 供调试器读）：代价 ≈ 6 行。
- **C** 改成显式优先级/配置项（如按屏体几何或协议通道选择）。

**推荐**：**B**（低成本消除静默）。**需要用户裁决**：是否允许改动这两处 API 语义（B 会改 last→first，
需确认没有依赖 last-wins 的现场口径）。

---

## C15【低】外部 Flash 扇区/三固件布局：现状**无重叠**（已验证），但约束只存在于代码注释里

**一句话本质**：W25 末尾三个 4KB 扇区被三方各占一个、互不重叠 ✓；Sector1 72B 记录三方固件一致 ✓——
但这两条纪律都没有任何自动校验，第四个消费者或旧固件一旦出现就静默破坏。

**证据（实证）**
- W25：LDI `capacity - 4096`（`app_ldi_cfg.c:15`）；app_render 持久化 `capacity - 4096*2`（`app_render.c:372`）；
  YN_OL `0x49` 参数 `capacity - 12288`（`app_yn_ol_proto_cmd.c:110-125`）⇒ 三个不同 4KB 扇区 ✓
  （`dev_storage_capacity` 的全部调用点已 grep 核对，共 3 处消费者 + 2 处读 capacity）。
- Sector1：`app_board_net_cfg.h:68-77`（20/40/72 + 偏移 48/68/12/16）与 Bootloader/Recovery 的
  `config_info.h` 逐条一致（两份都对拍过）✓。
- 风险面：新增消费者需自行知道"下一个可用 = capacity-16384"，无集中定义；`flash_all.sh` 默认擦 Sector1
  只是流程纪律（CLAUDE.md 有长段说明，属人工步骤）。

**影响面**：交付（三固件不同步 = 设备可能永久进 Recovery，历史已发生过）；维护。严重度 **低**（当前正确）。

**可选项**
- **A** 保持现状（文档 + `tool/flash_all.sh` 纪律）。
- **B** 把 W25 末尾分区集中到一处（如 `dev_storage.h` 或 `app_flash_layout.h` 定义
  `W25_SECTOR_LDI / _RENDER / _YN_OL` 偏移宏），三处消费者改用宏：代价 ≈ 20 行、无行为变化。
- **C** 加运行期自检（启动时校验各记录 magic，并保留一个 `volatile` 分区占用表）。

**推荐**：**B**（零行为变化、把隐含约束显式化）。**需要用户裁决**：是否值得为"防未来错误"改一次代码。

---

# 二、裁决速览表（按严重度）

| 编号 | 冲突标题 | 严重度 | 推荐选项 | 一句话理由 |
|---|---|---|---|---|
| C1 | 22_1665 = 8×4 在 make 两口径均链接失败（实测溢出 17700/15444B），文档溢出量过期 | 高 | 先定几何 → A/B/C 择一 | 几何是用户旋钮，现状"代码 8×4 + 文档三值 + make 全废"不可持续 |
| C2 | EIDE Debug 现行口径与文档「2026-09-28 裁决」完全相反（206 vs 193） | 高 | C（去 CQ 宏）+ 文档改事实 | 文件被 EIDE 今日重写，文档已失真；Debug 应与缅甸目标分工 |
| C3 | Debug 混编：定义 `PROTO_CHONGQING` 却排 CQ 模块 + 编 LDI ⇒ LDI/IAP 上报 IP 不一致、两个空转 UDP 口 | 高 | A 立即 + C/D 加固 | 一行配置消除现场可见矛盾；顺带把"错配"变成不可表达 |
| C4 | 缅甸干接点接管 PE12/11/10 与 RLS 干接点轮询冲突（ALL 构建显示「车道关闭」） | 中 | B（脚位借用标记） | 运行期共存优于继续依赖排除表 |
| C5 | `{` 族守卫被 `STD_ALL_PROTO` 关闭；量产口径靠 EIDE 手工排除，无告警 | 中 | C + D | 让量产可复现（make 单协议）+ 漏编可观测 |
| C6 | CCM 无编译期守卫 + `configUSE_TIMERS=1` 空转占 1124B CCM | 中 | A + B | 纯收益省 CCM；2 条断言防静默越界 |
| C7 | netconn 池峰值 = 容量 8，LDI 配远端即池满且失败无日志 | 中 | C（先）+ A/B | 先恢复可观测，再决定扩容或收敛 |
| C8 | `net_cfg.port` 双消费者（TCP 口 / GZ_OL UDP 口），改一个连带改另一个 | 中 | A（现状）+ 文档写明 | 拆字段会触发三固件同步高风险，收益不足 |
| C9 | EIDE 回写脆弱性（今日再次被证实）；量产口径无版本化 | 中 | B + C | 把期望写进脚本；量产转 make 口径 |
| C10 | 三驱动几何宏与文档表互相矛盾（1_263 = 1×1 vs 表 224×64 等） | 中 | A（逐个拍板） | C1/C11 都依赖它，必须先定 |
| C11 | 22_1665 8×4 的 scan 估算低估，可能超 TIM3 500µs（未 DWT 实测） | 中 | A（先实测） | 唯一能闭环的路径；数学上大概率超周期 |
| C12 | 文档-代码漂移 6 例（`"1\n2"`、`key_poll_task`、`DIAG_DEFS`、溢出量、几何、TIM4×8） | 低 | A（限定范围更正） | 只修会误导操作的，历史口径显式标注 |
| C13 | `PROTO` 无非法值守卫（bogus→ALL）；EIDE Debug(-O3) vs make Debug(-Og) | 低 | A + D | 3 行改动消除静默；D 消除两套 Debug 的误解 |
| C14 | 注册语义相反：显示实例 last-wins / 默认画面 first-wins | 低 | B（统一 + 计数） | 消除误配静默 |
| C15 | W25 扇区 / Sector1 72B 纪律无自动校验（当前无重叠、三方一致） | 低 | B（分区宏集中） | 零行为变化地把隐含约束显式化 |

**分布**：高 3（C1/C2/C3）、中 7（C4-C11）、低 5（C12-C15），共 **15 条**。

---

# 三、本次审计中"已验证不冲突"的点（供放心使用）

1. **协议帧头共存**：MM(0xFF) × 四川治超(0xFF) 双向不误吞成立（正签名 `app_mm_proto.c:205-235` +
   probe 链契约 `app_dispatch.c:375-440`：WAIT 不消费、READY/SKIP 消费）✓。
2. **CQ × YN_OL 的 TCP/UDP 隔离**：CQ 只绑 `CH_ID_UDP/_CQ`（`app_cq_proto.c:112/116`），YN_OL 只绑
   `RS485/RS232/TCP_SERVER/TCP_CLIENT`（`app_yn_ol_proto.c:127-149`）⇒ 通道掩码隔离生效 ✓。
3. **W25 三扇区不重叠**、**Sector1 72B 记录三方固件逐字段一致**（含 static_assert 全对齐）✓。
4. **22_1665 group1 引脚无外设冲突**：PB8/PB9/PE1/PE2 在 `.ioc` 明确为 `HUB75_R3/G3/R4/G4` 输出、
   ETH 为 RMII（`pl_eth.c:120`）⇒ 与以太网/MII 无冲突 ✓。
5. **ucHeap 预算健康**：`.analysis/heap/heap_ledger.py` 复算 = 启动期峰值需求 13376B / 余 **23480B**，
   运行期（含懒建任务）需求 21920B / 余 14936B ⇒ 堆不是瓶颈（瓶颈是 CCM 与 netconn 池）✓。
6. **`STD_ALL_PROTO` 之外的量产互斥**：`g_brace_proto_guard` 在无 `STD_ALL_PROTO` 时确实生效
   （6 个模块 `#ifndef` 包裹）✓；`FACTORY=STD/MM` 与 `DISP` 非法值 `$(error)` 生效 ✓。
7. **`Tcp Server` 串行服务 + keepalive 自愈**、`len > 0` 单字节分片修复、`notify` 重试修复均在位
   （`app_tcp_server.c:128-175`）✓。
8. **EIDE `files` 无悬空路径**（249/249 存在）✓；EIDE 口径的**无显示缅甸等价构建可链接**
   （本次实构 ccmram 16084、余 SRAM 11224B、3 条既有 HAL 告警）✓。

---

**报告生成**：2026-10-08；审计为只读；所有构建产物位于 `/tmp/audit_build/`（未污染 `build/` 与既有
`.analysis/` 目录）。如需复核，命令见本文开头表格。
