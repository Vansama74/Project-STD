/**
 * @file    app_diag.h
 * @brief   实机自证诊断开关与构建指纹（2026-09-14 GZ_OL ×2 问题复测轮）
 *
 * 目的：让固件自己在 RTT 里自报「我是谁 / 编译于何时 / 屏体几何 / 字库布局 /
 * 网络与端口绑定实况」，把「板上跑的是不是最新构建」与「根因在哪一层」变成
 * 可直接读出的证据，而不是靠推断。
 *
 * 三处诊断（全部 RTT 只读打印，不改变任何协议动作 / 渲染结果）：
 *   ① 开机横幅 + 延迟网络体检（`app_boot.c`，宏 APP_DIAG_BANNER）；
 *   ② GZ_OL `0x20` 每帧：解析结果 + 字形框越屏判定 + **字库三档实读**
 *      （`app_gz_ol_proto_cmd.c`，宏 GZ_OL_RTT_DIAG，默认跟随 APP_DIAG_BANNER）；
 *   ③ GZ_OL `0x40` 每帧：落库返回值 + 写前/写后 read-back（同上）。
 *
 * 一键关闭（两种等价方式，任选其一）：
 *   ① 编辑本文件，把 APP_DIAG_BANNER 由 (1) 改为 (0)；
 *   ② 命令行 `make APP_DIAG=0`（Makefile 转成 -DAPP_DIAG_BANNER=0，见「诊断开关」段）。
 * 关闭后：横幅/体检/0x20/0x40 诊断全部消失，固件行为与开启前逐字节一致
 * （诊断代码全部只读；关闭时辅助函数因未被引用被 --gc-sections 丢弃）。
 */
#pragma once

/* ---- 诊断总开关：1=开（默认，交付现场复测）／0=关 ---- */
#ifndef APP_DIAG_BANNER
#define APP_DIAG_BANNER (1)
#endif

/* ---- 构建指纹（自证「板上固件 = 哪棵树 + 哪套口径」）----
 * Makefile 仅对 app_boot.o 注入（不进 DEFINES/stamp，不改增量构建语义）；
 * 其它构建入口（EIDE 等）未注入时用下面的占位值，保证能编过。 */
#ifndef APP_DIAG_TREE_HASH
#define APP_DIAG_TREE_HASH "no-fingerprint"
#endif
#ifndef APP_DIAG_PROTO
#define APP_DIAG_PROTO "?"
#endif
#ifndef APP_DIAG_DISP
#define APP_DIAG_DISP "?"
#endif
#ifndef APP_DIAG_CONFIG
#define APP_DIAG_CONFIG "?"
#endif
#ifndef APP_DIAG_TOOLCHAIN
#define APP_DIAG_TOOLCHAIN "?"
#endif
