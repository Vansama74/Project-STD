#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FreeRTOS heap_4 启动期分配账（离线可复现）— **本工作区唯一堆账脚本**

背景
----
PROTO=ALL 口径下全协议共存构建任务数 30+，`ucHeap` 只有 36KB
（`configTOTAL_HEAP_SIZE = 36*1024`，`ucHeap` 静态落 SRAM `.bss`，见
`Core/Inc/FreeRTOSConfig.h`）。2026-09-17 现场在
`app_rs485_start` → `osThreadNew` → `xTaskCreate` → `pvPortMalloc`
处命中 `vApplicationMallocFailedHook`。本脚本把「谁在什么时候要了多少堆」
变成可加总、可对分的静态账，用于定位临界点与验证修复余量。

heap_4 口径（源码实证，非估算）
------------------------------
* 每次分配实际占用 = align8(请求字节 + 8)：`heap_4.c` `xWantedSize += xHeapStructSize;`
  后按 8 对齐（`portBYTE_ALIGNMENT = 8` → `xHeapStructSize = 8`）。
* 初始可用 = `configTOTAL_HEAP_SIZE - xHeapStructSize` = 36864 − 8 = **36856**
  （`prvHeapInit()` 把第一个 BlockLink 头算掉）。
* `sizeof(TCB_t)` = **100**（`tasks.c` `prvAllocateTCBAndStack` 的 `pvPortMalloc( sizeof( TCB_t ) )`）
  → 块 **112**。
* `sizeof(Queue_t)` = **80** → 互斥/信号量块 **88**；
  消息队列 = 80 + 深度×项宽（6×4 口邮箱 = 104 → 块 **112**）。
* `sizeof(EventGroup_t)` = **32** → 块 **40**。
* 空闲任务 / 定时器任务 **不占堆**：`configSUPPORT_STATIC_ALLOCATION = 1` ⇒
  `vTaskStartScheduler` / `xTimerCreateTimerTask` 走 `xTaskCreateStatic`，
  缓冲由 `app_boot.c` 的 `vApplicationGetIdleTaskMemory` /
  `vApplicationGetTimerTaskMemory`（强定义覆盖 cmsis_os2.c 的 __WEAK）提供，
  2026-09-17 起落在 `.ccmram`。
* 协议帧队列 / ch_queue / 串口 RX 队列全部 `.cb_mem + .mq_mem` 静态
  （见各模块 `osThreadAttr_t`）→ 不占堆，本账不计。
* `netconn` = recvmbox（6×4B 队列 → 112）+ op_completed（二值信号量 → 88）= **200**。

静态 CCMRAM 任务（2026-09-17 修复）
----------------------------------
`pl_task_static.h` 的 `PL_TASK_STATIC_STORAGE/ATTR` 把「栈 + TCB」整体放入
`.ccmram`（CPU 专用、零等待、DMA 不可达；任务栈只被 CPU 访问，合法）。
一个 1KB 任务：堆 −1144B、CCM +1124B（1024 栈 + 100 `StaticTask_t`）。
`kind="ccm"` 的条目在「修复前」按动态计（历史口径），「修复后」不计堆。

用法
----
    python3 .analysis/heap/heap_ledger.py              # 修复前/后对照（PROTO=ALL）
    python3 .analysis/heap/heap_ledger.py --markdown   # 输出 markdown 表（贴文档）
    python3 .analysis/heap/heap_ledger.py --ab         # YN_OL 任务 A/B 对照
    python3 .analysis/heap/heap_ledger.py --eide       # EIDE Debug 口径（任务更少）
"""

import argparse
import sys

HEAP_TOTAL = 36 * 1024          # configTOTAL_HEAP_SIZE
HEAP_USABLE = HEAP_TOTAL - 8    # prvHeapInit() 后初始空闲 = 36856
TCB_REQ = 100                   # sizeof(TCB_t)
HDR = 8                         # xHeapStructSize


def blk(req):
    """heap_4 单次分配实际占用（字节）"""
    return (req + HDR + 7) & ~7


def task(words):
    """一个动态任务 = 栈块 + TCB 块"""
    return blk(words * 4) + blk(TCB_REQ)


T_1K, T_2K, T_512 = task(256), task(512), task(128)     # 1144 / 2168 / 632
SEM = blk(80)      # 88（互斥 / 二值 / 计数信号量）
MBOX6 = blk(80 + 6 * 4)  # 112
EVT = blk(32)      # 40
NETCONN = MBOX6 + SEM    # 200

# ---------------------------------------------------------------- 分配序列
# (阶段, 创建者, 对象, 字节, 类型)；类型 "ccm" = 2026-09-17 起栈/TCB 落 .ccmram
IO = "indirect"


def seq_all():
    s = []

    def add(stage, owner, what, size, kind="dyn", note=""):
        s.append(dict(stage=stage, owner=owner, what=what, size=size, kind=kind, note=note))

    # ① RTOS 启动前
    add("pre", "app_boot", "init_task（512 words）", T_2K)
    # ② dev_eth_start() → pl_net_init → tcpip_init + netif_add + 两个线程
    add("eth", "sys_arch", "lwip_sys_mutex（sys_init）", SEM)
    add("eth", "lwip/mem", "mem_mutex（mem_init → sys_mutex_new，!NO_SYS 恒建）", SEM)
    add("eth", "sys_arch", "tcpip_mbox（TCPIP_MBOX_SIZE=6）", MBOX6)
    add("eth", "sys_arch", "lock_tcpip_core（CORE_LOCKING）", SEM)
    add("eth", "sys_arch", "tcpip_thread（TCPIP_THREAD_STACKSIZE=1024）", T_1K)
    add("eth", "pl_eth", "RxPktSemaphore", SEM)
    add("eth", "pl_eth", "TxPktSemaphore", SEM)
    add("eth", "pl_eth", "ethernetif_input / EthIf（256 words×4）", T_1K)
    add("eth", "pl_net", "ethernet_link_thread / EthLink（1KB）", T_1K)

    # ③ sw_board_init() = initcall_run(.sw_initcall.*)
    add("sw2", "dev_key", "press_sem ×4（SW1/SW2/SW3/TEST；DIP 无 wait_press）", 4 * SEM)
    add("sw2", "dev_display", "scan evt flags", EVT)
    add("sw2", "dev_display", "scan_task（1KB，Realtime）", T_1K, "ccm")
    add("sw2", "dev_w25qxx", "s_w25_mutex", SEM)

    add("sw3", "app_factory_test", "factory_monitor（1KB）", T_1K, "ccm")
    add("sw3", "app_dispatch", "frame_dispatch_task（1KB）", T_1K, "ccm")
    add("sw3", "app_light_sensor", "light_sensor_task（128 words）", T_512, "ccm")
    add("sw3", "app_iap", "iap_handle_task（1KB）", T_1K, "ccm")
    add("sw3", "app_ldi", "ldi_handle_task（1KB）", T_1K, "ccm")
    add("sw3", "app_ldi", "ldi_timer_task（1KB）", T_1K, "ccm")
    add("sw3", "app_ldi", "ldi tx_lock（互斥）", SEM)
    add("sw3", "app_cq_proto", "cq_handle_task（1KB）", T_1K, "ccm")
    add("sw3", "app_cq_proto", "cq_timer_task（1KB）", T_1K, "ccm")
    add("sw3", "app_rls", "rls_handle_task（1KB）", T_1K, "ccm")
    add("sw3", "app_qh_proto", "qh_handle_task（1KB）", T_1K, "ccm")
    add("sw3", "app_sc_etc", "sc_etc_handle_task（1KB）", T_1K, "ccm")
    add("sw3", "app_sc_mtc", "sc_mtc_handle_task（1KB）", T_1K, "ccm")
    add("sw3", "app_sc_ol", "sc_ol_handle_task（1KB）", T_1K, "ccm")
    add("sw3", "app_sd_proto", "sd_handle_task（1KB）", T_1K, "ccm")
    add("sw3", "app_gz_proto", "gz_handle_task（1KB）", T_1K, "ccm")
    add("sw3", "app_gz_ol_proto", "gz_ol_handle_task（1KB）", T_1K, "ccm")
    add("sw3", "app_yn_proto", "yn_handle_task（1KB）", T_1K, "ccm")
    add("sw3", "app_yn_ol_proto", "yn_ol_handle_task（1KB）← 2026-09-17 新接入", T_1K, "ccm")
    add("sw3", "app_anhui_proto", "anhui_handle_task（1KB）", T_1K, "ccm")
    # 三条物理通道的 RB 互斥（rb_init，按首个 acquire 的协议先后）
    add("sw3", "ring_buffer", "RJ45 RB mutex（IAP/LDI/CQ 首个 acquire）", SEM)
    add("sw3", "ring_buffer", "RS485 RB mutex", SEM)
    add("sw3", "ring_buffer", "RS232 RB mutex", SEM)
    # 懒初始化（首次 W25 读，boot 期 ldi_ctx_init / 横幅字库查询即触发）
    add("sw3", "dev_w25qxx", "s_evt（首次 _read 懒建）", EVT)

    # ④ init_task 尾段：通道循环任务
    add("chan", "app_boot", "half_sec_task（128 words）", T_512)
    add("chan", "app_tcp_server", "tcp_server_task（1KB）", T_1K)
    add("chan", "app_tcp_client", "tcp_client_task（1KB）", T_1K)
    add("chan", "app_udp", "udp_task（1KB，10011）", T_1K)
    add("chan", "app_udp", "udp_cq_task（1KB，CQ 业务口）", T_1K)
    add("chan", "app_udp", "udp_gzol_task（1KB，GZ_OL 业务口）", T_1K)
    add("chan", "app_rs485", "rs485_task（1KB）← 现场失败点", T_1K, "ccm")
    add("chan", "app_rs232", "rs232_task（1KB）", T_1K, "ccm")

    # ⑤ 运行期（首帧前/首连前；任务内分配，不回收或按连接回收）
    add("run", "app_tcp_server", "netconn recvmbox + op_completed", NETCONN)
    add("run", "app_tcp_server", "acceptmbox（netconn_listen）", MBOX6)
    add("run", "app_tcp_client", "client_disconnect_sem", SEM)
    add("run", "app_tcp_client", "netconn recvmbox + op_completed", NETCONN)
    add("run", "app_udp", "udp_disconnect_sem", SEM)
    add("run", "app_udp", "netconn recvmbox + op_completed", NETCONN)
    add("run", "app_udp", "udp_connect_task（常驻 1KB）", T_1K)
    add("run", "app_udp", "udp_cq_disconnect_sem", SEM)
    add("run", "app_udp", "netconn recvmbox + op_completed", NETCONN)
    add("run", "app_udp", "udp_cq_connect_task（常驻 1KB）", T_1K)
    add("run", "app_udp", "udp_gzol_disconnect_sem", SEM)
    add("run", "app_udp", "netconn recvmbox + op_completed", NETCONN)
    add("run", "app_udp", "udp_gzol_connect_task（常驻 1KB）", T_1K)
    add("run", "app_scroll", "s_scroll_evt + 双互斥（首个滚动帧懒建）", EVT + 2 * SEM)
    add("run", "app_scroll", "scroll_task（1KB，首个滚动帧懒建）", T_1K, "ccm")
    add("run", "app_tcp_client", "tcp_client_conn_task（连上才建 1KB）", T_1K)
    add("run", "app_yn_proto", "yn_selftest_task（'2' 命令触发 1KB）", T_1K)
    add("run", "app_yn_ol_proto", "yn_ol_selftest_task（'2' 命令触发 1KB）", T_1K)
    return s


# EIDE Debug 口径：编入 IAP + LDI + 四川三协议 + 安徽 + 22_1665（见 doc/CLAUDE.md 选编口径）
EIDE_DROP_OWNERS = {
    "app_qh_proto", "app_sd_proto", "app_gz_proto", "app_gz_ol_proto",
    "app_yn_proto", "app_yn_ol_proto", "app_rls", "app_cq_proto", "app_factory_test",
}


def simulate(rows, label, markdown=False, stop=None):
    used = 0
    first_fail = None
    first_fail_before = 0
    out = []
    for it in rows:
        if stop and it["stage"] == stop:
            break
        free_before = HEAP_USABLE - used
        used += it["size"]
        free = HEAP_USABLE - used
        if free < 0 and first_fail is None:
            first_fail = it
            first_fail_before = free_before
        out.append((it, used, free, free < 0))
    print(f"\n=== {label} ===")
    print(f"configTOTAL_HEAP_SIZE={HEAP_TOTAL}B  初始可用={HEAP_USABLE}B")
    if markdown:
        print("| # | 阶段 | 创建者 | 对象 | 字节 | 累计 | 余量 |")
        print("|---|---|---|---|---|---|---|")
        for i, (it, u, f, bad) in enumerate(out, 1):
            print(f"| {i} | {it['stage']} | {it['owner']} | {it['what']}{' ⚠超限' if bad else ''} "
                  f"| {it['size']} | {u} | {f} |")
    else:
        for i, (it, u, f, bad) in enumerate(out, 1):
            mk = "  [CCM]" if it["kind"] == "ccm" else ""
            flag = "  <== pvPortMalloc 返回 NULL" if bad else ""
            print(f"{i:3d} {it['stage']:5s} {it['owner']:20s} {it['what']:46s} "
                  f"{it['size']:6d} Σ={u:6d} free={f:7d}{mk}{flag}")
    print(f"需求合计={used}B  余量={HEAP_USABLE - used}B"
          + (f"  ** 赤字 {used - HEAP_USABLE}B **" if used > HEAP_USABLE else ""))
    if first_fail:
        print(f"临界点 = 阶段 {first_fail['stage']} / {first_fail['owner']} / {first_fail['what']}"
              f"（需 {first_fail['size']}B，此前余 {first_fail_before}B → 申请后赤字 "
              f"{first_fail_before - first_fail['size']}B，pvPortMalloc 返回 NULL）")
    else:
        print("临界点：无（总需求未超堆）")
    return used, first_fail


def margin_at(rows, stop_what, ref):
    """沿 ref 的**分配顺序**累计，返回「创建 stop_what 之前」的余量。

    `rows` 是实际生效的条目集合（修复后 = ref 去掉静态 CCM 条目）；用
    (owner, what) 判存在，避免 pre/post 列表长度不同导致下标错位。
    """
    present = {(it["owner"], it["what"]) for it in rows}
    used = 0
    for it in ref:
        if it["what"].startswith(stop_what):
            return HEAP_USABLE - used
        if (it["owner"], it["what"]) in present:
            used += it["size"]
    return None


def boot_peak(rows):
    """启动期（不含 run 阶段的首帧/首连异步分配）需求与余量"""
    used = sum(it["size"] for it in rows if it["stage"] != "run")
    return used, HEAP_USABLE - used


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--markdown", action="store_true")
    ap.add_argument("--ab", action="store_true", help="YN_OL 任务 A/B 对照")
    ap.add_argument("--eide", action="store_true", help="EIDE Debug 口径（任务更少）")
    args = ap.parse_args()

    full = seq_all()
    if args.eide:
        full = [it for it in full if it["owner"] not in EIDE_DROP_OWNERS]

    pre = [dict(it) for it in full]                 # 修复前：全部任务动态
    post = [it for it in full if it["kind"] != "ccm"]  # 修复后：静态任务不占堆

    lbl = "PROTO=ALL（make 全协议 dev 构建）" if not args.eide else "EIDE Debug 口径"
    simulate(pre, f"修复前 — {lbl}（任务栈/TCB 全部自 ucHeap）", args.markdown)
    simulate(post, f"修复后 — {lbl}（任务栈/TCB 落 .ccmram）", args.markdown)

    pre_nohandle = margin_at(pre, "rs485_task", pre)
    post_nohandle = margin_at(post, "rs485_task", pre)
    print(f"\n【现场失败点核对】创建 rs485_task 之前：修复前余 {pre_nohandle}B"
          f"（1KB 任务需 {T_1K}B → {'会失败' if pre_nohandle is not None and pre_nohandle < T_1K else '恰好够'}）；"
          f"修复后余 {post_nohandle}B（rs485_task 已静态 → 不再申请堆）")
    pre_peak, pre_free = boot_peak(pre)
    post_peak, post_free = boot_peak(post)
    print(f"【启动期峰值需求（不含 run 阶段异步）】修复前 {pre_peak}B → 余 {pre_free}B"
          f"（{'赤字 ' + str(-pre_free) + 'B' if pre_free < 0 else '够'}）；"
          f"修复后 {post_peak}B → 余 {post_free}B"
          f"（口径：≥2500B 即满足「同一启动点空闲堆 ≥2.5KB」目标）")

    if args.ab:
        drop_yn = [it for it in full if it["owner"] != "app_yn_ol_proto"]
        simulate(drop_yn, f"修复前 A/B：去掉云南治超任务 — {lbl}", args.markdown)
        simulate([it for it in drop_yn if it["kind"] != "ccm"],
                 f"修复后 A/B：去掉云南治超任务 — {lbl}", args.markdown)
        print("\n判读：修复前「有/无 YN_OL 任务」的临界点相差恰好一个 1KB 任务位 "
              f"（{T_1K}B）——带 YN_OL 时死在 rs485_task、去掉后死在 rs232_task ⇒ "
              "YN_OL 处理任务确是压垮 ucHeap 的最后一份；修复后该任务的栈/TCB 落 CCMRAM，"
              "其创建不再申请堆（仍申请堆的只剩 '2' 自检任务与串口连接类任务，见 run 段）。")

    saved = sum(it["size"] for it in full if it["kind"] == "ccm")
    ccm = sum(it["size"] for it in full if it["kind"] == "ccm") - 0
    n_ccm = len([it for it in full if it["kind"] == "ccm"])
    # CCM 侧无 BlockLink 头：栈 words*4 + StaticTask_t 100
    ccm_bytes = 0
    for it in full:
        if it["kind"] != "ccm":
            continue
        if "128 words" in it["what"]:
            ccm_bytes += 512 + TCB_REQ
        elif "512 words" in it["what"]:
            ccm_bytes += 2048 + TCB_REQ
        else:
            ccm_bytes += 1024 + TCB_REQ
    print(f"\n静态 CCMRAM 任务 {n_ccm} 个：释放 ucHeap {saved}B，占用 .ccmram "
          f"{ccm_bytes}B（栈 + 100B StaticTask_t，无堆头）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
