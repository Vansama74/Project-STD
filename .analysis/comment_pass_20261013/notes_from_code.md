# 注释外移信息台账（2026-10-13 全项目注释精简轮）

口径：本轮从业务代码注释中移除「调试说明 / 历史修复记录 / 轮次叙事 / A/B 测量与段尺寸 /
踩坑与现场事故 / 历史口径长段落」；**保留下来的结论已压成一句话**（源码内可见）。
本文件记录**被移除信息 → 落点**，按「文档已有（注释直接删）」与「本轮新补 / 记于此」两类。

> 说明：本轮所有被移除的信息，除下表「须另存」几项外，**均已在对应模块 README 或
> doc/CLAUDE.md 中查到**（逐条 grep 核对）；删除不改变任何结论。

## 1. 已确认文档已有（注释直接删）

| 模块 / 文件 | 被删内容摘要 | 落点 |
|---|---|---|
| `Application/Src/LDI/app_ldi.c`、`app_ldi_cmd.c` | Sector1/W25 真源与自愈分支的由来与逐次修订史（缺陷 B/C/A、8-18/8-21 修订）；12H 端口字段误用发现口的历史事故 | `doc/07` §9/§10/§12/§14/§15/§16、`doc/02` 附件2 |
| `Application/Src/app_net_boot.c` | 「两口径统一应用 TCP 口」的由来（混编口径矛盾） | `doc/07` §14 |
| `Application/Src/Config/app_board_net_cfg.c/.h` | 72B 记录布局与升级中间态语义的修订日期 | `doc/07` §9/§10/§13、`doc/06-04` |
| `Application/Inc/Config/app_board_net_cfg.h` | 方案 B 双端口分离缘由 | `doc/07` §15 |
| `Application/Src/IAP/app_iap_cmd.c` | 4B02 应答带结果码的协议扩展说明与兼容风险 | `doc/07` §10、doc/CLAUDE.md「0E 待完善项」 |
| `Application/Src/Channel/app_tcp_server.c` / `app_tcp_client.c` | keepalive 现场成因叙事（「连上没反应、要重启板子」） | `doc/15` §12、doc/CLAUDE.md LwIP 段 |
| `Application/Src/Channel/app_udp.c` | 广播丢包修复史、CQ/GZ_OL 业务口裁决由来 | `doc/03` PartB B.7.1、`doc/14` §13 |
| `Application/Src/app_dispatch.c/.h` | 通知重试/WAIT 预算的联调现场叙事与日期 | `doc/15` §11、doc/05-01、`doc/19` §2.1 |
| `Application/Src/app_scroll.c`、`Application/Inc/app_scroll.h` | 停止语义/回绕/渲染互斥/脏矩形提交的裁决日期 | `doc/01_显示系统/动态滚动显示实现记录.md`、`doc/13` |
| `Application/Src/ProtocolParser_Anhui/*` | 0x85 坐标两轮口径撤销史、24 点阵改字号、渲染互斥编号 | `doc/13` §1.1/§1.2/§5/§8 |
| `Application/Src/ProtocolParser_GuiZhou/*` | 设备实测差异（颜色/音量/行文）、0x4 坐标行为 | `doc/10` §2/§3 |
| `Application/Src/ProtocolParser_YunNan/*` | 自检序列、行 5 不落屏、版本号口径、音量映射的裁决编号 | `doc/11` §8、§9 |
| `Application/Src/ProtocolParser_SiChuang_ETC/*` | 心跳计时任务停用与恢复方法 | 源码保留一句（`#if 0` 用途）；语义细节见 doc/CLAUDE.md「四川三协议」段 |
| `Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto_cmd.c` | 旧「16B/行固定切分」实现与其缺陷 | doc/CLAUDE.md「四川三协议」段（MTC '4' 全屏口径） |
| `Application/Src/ProtocolParser_SiChuang_Overload/app_sc_ol_proto_cmd.c` | 亮度 0 档致全灭的排查过程 | 源码保留一句防护约束；口径见 doc/CLAUDE.md「四川三协议」段 |
| `Application/Src/ProtocolParser_ChongQing/*` | 12B 帧与 LDI 共存判定、setip 端口语义 | `doc/03` PartB B.7.1/B.7.2/Q23 |
| `Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c` | 极性翻转实测过程（PE12 3.3V） | `doc/16` §4.1/§4.3 |
| `Application/Src/app_test.c` | 22-1665 现场判读与测试前提的轮次标注 | `doc/01_显示系统/22-1665…记录.md` §5 |
| `Application/Src/app_render.c`、`Application/Inc/app_render.h` | 字库区块地址交叉验证记录、越界裁剪裁决日期 | doc/01（字库引擎相关）、doc/13 §1.2 |
| `Device/Display/dev_display_1_263.c` | 「PIXEL_ROW 必须 32」的历史教训（旧 64 宽布局假象） | `doc/01_显示系统/1-263模组驱动分析与修复记录.md` §5 |
| `Device/Display/dev_display_22_1665.c` | 编译守卫恢复日期、现场定标日期、历史轮次指针 | `doc/01_显示系统/22-1665…记录.md` §0 与附录 A |
| `Device/Display/dev_display_22_1703.c` | 通道序/位序裁决编号 | `doc/01_显示系统/22-1703…记录.md` §2/§4 |
| `Device/Display/dev_display.c`、`dev_display_1_969.c`、`Device/Inc/dev_display.h` | ④a 脏矩形编号与日期、`dev_display_guard` 限速告警（RTT）表述 | doc/16 §10、doc/CLAUDE.md（显示子系统） |
| `Platform/Inc/lwipopts.h` | 瘦身轮日期、资源池修复日期 | `doc/17`、doc/06-04 |
| `Platform/Src/pl_sys.c`、`Platform/Inc/pl_sys.h` | 「现场取证」叙事 | 源码保留用途句；doc/15 §12（复位取证行） |
| `Core/Src/syscalls.c` | 「RTT 调试通道已移除」旧述（与实态不符：RTT 底层已恢复） | doc/CLAUDE.md「调试」段、`doc/17` §5 |
| `Application/Src/ProtocolParser_XinJiang/*` | 自备 RB 的现场事故叙事；音量映射「实测」措辞 | `doc/19` §2.2/§2.6、§11（本轮新增） |

## 2. 本轮新补 / 记于此（无既有落点者）

| 项 | 内容 | 落点 |
|---|---|---|
| 新疆模块 12 条编译期断言的逐条契约 | 每条的「改错会怎样」与现契约文字 | `doc/19` §11（本轮新增） |
| 注释规范新裁决（九类移出清单 + 去处） | 调试/历史/轮次/测量/坑点等一律不入源码 | `doc/18_注释规范.md` §2 第 9 条（本轮改写） |
| 业务代码默认禁止新增 RTT 调试 | 纪律条文 + 回退方式 | `.cursor/rules/no-rtt-debug.mdc`、`doc/CLAUDE.md`「代码风格」 |
| 保留待裁决的只读观察 API | `app_dispatch_qfull_drops/resync_count/notify_drops`、`app_render_chip_info_get`、`app_render_glyph_probe` 等（2026-09-29 保留决定延续；**本轮未删**） | 见本轮报告 §「宁留不删清单」 |

## 3. 有意保留（属契约 / 待确认标记 / 操作约束，不应删）

- 「待联调校准」「待上位机确认」「待厂商确认」等**待确认标记**（§2.7 纪律）。
- 现场操作约束：`DIP1 置 OFF`（9600）、`-D` 覆盖开关说明、`#if 0` 停用块的用途与恢复方法。
- 一句话硬约束：帧长/容量上限、字节序、极性、通道掩码、CCM 放置、Cronological 无关的
  「必须与 X 同序」「不得按 avail 重置」等。
- RTT 底层（`Middlewares/Third_Party/SEGGER_RTT/**`、`Platform/Src/pl_rtt.c`、Makefile/EIDE 收录）。
