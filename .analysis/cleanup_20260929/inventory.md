# RTT 调试代码清单（2026-09-29 代码瘦身轮）

> 生成方式：`grep -rn "SEGGER_RTT\|pl_rtt\|app_diag\|APP_DIAG\|LWIP_PLATFORM_DIAG\|RTT_DIAG"`，
> 排除 `Drivers/`（本工程未用）、`Middlewares/`（第三方）、`doc/`、`.analysis/**/archive/**`。
> 原始行号清单：`rtt_sites.txt`（本目录）；备份：`backup/sources_before.tar.gz`。

## A. 包装模块 / 第三方（整文件级）

| # | 文件 | 处置 |
|---|------|------|
| A1 | `Platform/Src/pl_rtt.c`（+ `hw_initcall` 注册） | **删除文件**（git 跟踪，可恢复） |
| A2 | `Platform/Inc/pl_rtt.h` | 不存在（历史遗留引用，无需处理） |
| A3 | `Middlewares/Third_Party/SEGGER_RTT/SEGGER_RTT.c` | 磁盘保留，**移出编译** |
| A4 | `Middlewares/Third_Party/SEGGER_RTT/SEGGER_RTT_printf.c` | 磁盘保留，**移出编译** |
| A5 | `Middlewares/Third_Party/SEGGER_RTT/SEGGER_RTT_Syscalls_GCC.c` | 磁盘保留，**移出编译**（该文件把 `_write` 接到 RTT，是 printf→RTT 的桥） |

## B. 诊断设施（APP_DIAG 全套）

| # | 位置 | 内容 | 处置 |
|---|------|------|------|
| B1 | `Application/Inc/app_diag.h` | `APP_DIAG_BANNER` + 5 个构建指纹宏 | **删除文件** |
| B2 | `Makefile` L59~65 | `APP_DIAG` 命令行开关 → `-DAPP_DIAG_BANNER` | 删 |
| B3 | `Makefile` L84~91 | `DIAG_TREE_HASH` 树哈希 + `DIAG_DEFS`（5 宏，仅注入 app_boot.o） | 删 |
| B4 | `Makefile` | `APP_LWIP_DBG_LEVEL` 开关 | 删（LwIP 调试整体关闭，见 D1） |
| B5 | `Application/Src/app_boot.c` | `app_diag_heap()` / `app_diag_boot_banner()` / `app_diag_boot_late()` / `_diag_ch_state()` / 弱符号探测 / 12 处 `app_diag_heap()` 调用 / `osDelay(2500)` 体检窗口 | 删（保留各 `*_start()` 调用与顺序） |
| B6 | `Application/Src/app_boot.c` `app_boot()` | init_task 创建失败 RTT 报错 | 删打印，保留 `taskDISABLE_INTERRUPTS()+for(;;)` 止血 |
| B7 | `Application/Src/app_boot.c` `vApplicationMallocFailedHook` | RTT 两行报错 | 删打印，保留关中断死循环（调试器取证行为不变） |
| B8 | `Application/Src/app_dispatch.c` | `#include app_diag.h/SEGGER_RTT.h`、`app_dispatch_{qfull_drops,resync_count,notify_drops}` 访问器 | 删 include；**访问器与计数保留**（见报告「保留项」） |

## C. 通道 / 协议级 `*_RTT_DIAG` 与逐帧诊断

| # | 文件 | 诊断内容（宏） | 处置 |
|---|------|---------------|------|
| C1 | `Application/Src/Channel/app_tcp_server.c` | `TCP_SRV_RTT_DIAG`：listen/bind/accept/recv/close | 删 |
| C2 | `Application/Src/Channel/app_tcp_client.c` | `TCP_CLI_RTT_DIAG`：connect/recv/close/失败 | 删 |
| C3 | `Application/Src/Channel/app_udp.c` | 绑定/回退告警（`app_diag`+RTT） | 删 |
| C4 | `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c` | `GZ_OL_RTT_DIAG`：0x20/0x40 逐帧证据 + 字库实读 | 删 |
| C5 | `Application/Src/ProtocolParser_Anhui/app_anhui_proto_cmd.c` | `ANHUI_RTT_DIAG`：0x85 坐标原始序诊断 | 删 |
| C6 | `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c` | `YN_OL_RTT_DIAG`：probe/队列/自检诊断 | 删 |
| C7 | `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c` | `YN_OL_RTT_DIAG`：逐帧 rx/exec/drop | 删 |
| C8 | `Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c` | `MM_RTT_DIAG`：每帧灯位/引脚/极性 + TEST 切换行 | 删 |
| C9 | `Application/Src/app_factory_test_mm.c` | `MM_RTT_DIAG` 引用 | 删 |
| C10 | `Application/Src/app_dispatch.c` | `PROBE_DBG_MIN_MS`（盲区 B probe 日志）、`DISPATCH_NOPROTO_DBG_MIN_MS`（无协议承载告警）、FULL 丢帧告警、WAIT 预算到期证据、`s_pdbg_*` 限速态 | 删打印与限速态；保留 WAIT 预算/重同步/丢帧计数逻辑本体 |
| C11 | `Application/Src/app_net_boot.c` | netcfg 应用/默认落盘诊断 | 删 |
| C12 | `Application/Src/Config/app_board_net_cfg.c` | 记录读写诊断 | 删 |
| C13 | `Device/Display/dev_display.c` | `dev_display_guard()` 的 `[disp]` 限速告警 | 删打印，**保留 `dev_display_guard()` 判空语义**（无显示构建的守卫，被 14+ 处调用） |
| C14 | `Application/Src/app_render.c` / `Application/Inc/app_render.h` | RTT 引用（1 处） | 删 |
| C15 | `Application/Inc/Channel/app_udp.h` / `Application/Inc/ProtocolParser_*/**.h` | 诊断宏声明 doc | 删 |
| C16 | `Platform/Inc/pl_task_guard.h` | 创建失败 RTT 报错 | 删打印，保留判空返回值语义 |
| C17 | `Platform/Src/pl_eth.c` | EthIf 创建失败 RTT 报错 | 删打印，保留 `osThreadNew` |
| C18 | `Platform/Src/pl_net.c` | EthLink 创建失败 RTT 报错 | 删打印，保留 `osThreadNew` |
| C19 | `Core/Src/stm32f4xx_it.c` | HardFault RTT 打印 | 删打印，保留最小异常行为 |

## D. LwIP 调试输出

| # | 位置 | 处置 |
|---|------|------|
| D1 | `Platform/Inc/lwipopts.h`（`APP_LWIP_DBG_LEVEL` 覆盖块） | 改为 `#undef LWIP_DEBUG`（LwIP 调试整体编出，零输出零格式化开销）；`Core/Inc/main.h` 的 CubeMX 段 4 行保持不动（被 `#undef` 屏蔽） |

## E. 需用户裁决（疑似业务副作用，暂不动）

| # | 位置 | 说明 |
|---|------|------|
| E1 | `app_dispatch.c` `qfull_drop/resync_byte/notify_drop` 计数与三个只读访问器 | 非 RTT 依赖、公开 API；删除会改接口，本轮保留 |

## 残余扫描口径（用于清零举证）

```bash
grep -rn "SEGGER_RTT\|pl_rtt\|app_diag\|APP_DIAG\|RTT_DIAG\|LWIP_PLATFORM_DIAG\|LWIP_DEBUG" \
  Application Device Platform Kernel Compiler Core Makefile .eide/eide.yml \
  | grep -v "getters\|保留"
```
目标：0 命中（第三方 `Middlewares/`、`doc/`、`.analysis/**/archive/**` 不计）。
