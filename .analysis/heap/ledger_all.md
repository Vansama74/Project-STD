
=== 修复前 — PROTO=ALL（make 全协议 dev 构建）（任务栈/TCB 全部自 ucHeap） ===
configTOTAL_HEAP_SIZE=36864B  初始可用=36856B
| # | 阶段 | 创建者 | 对象 | 字节 | 累计 | 余量 |
|---|---|---|---|---|---|---|
| 1 | pre | app_boot | init_task（512 words） | 2168 | 2168 | 34688 |
| 2 | eth | sys_arch | lwip_sys_mutex（sys_init） | 88 | 2256 | 34600 |
| 3 | eth | lwip/mem | mem_mutex（mem_init → sys_mutex_new，!NO_SYS 恒建） | 88 | 2344 | 34512 |
| 4 | eth | sys_arch | tcpip_mbox（TCPIP_MBOX_SIZE=6） | 112 | 2456 | 34400 |
| 5 | eth | sys_arch | lock_tcpip_core（CORE_LOCKING） | 88 | 2544 | 34312 |
| 6 | eth | sys_arch | tcpip_thread（TCPIP_THREAD_STACKSIZE=1024） | 1144 | 3688 | 33168 |
| 7 | eth | pl_eth | RxPktSemaphore | 88 | 3776 | 33080 |
| 8 | eth | pl_eth | TxPktSemaphore | 88 | 3864 | 32992 |
| 9 | eth | pl_eth | ethernetif_input / EthIf（256 words×4） | 1144 | 5008 | 31848 |
| 10 | eth | pl_net | ethernet_link_thread / EthLink（1KB） | 1144 | 6152 | 30704 |
| 11 | sw2 | dev_key | press_sem ×4（SW1/SW2/SW3/TEST；DIP 无 wait_press） | 352 | 6504 | 30352 |
| 12 | sw2 | dev_display | scan evt flags | 40 | 6544 | 30312 |
| 13 | sw2 | dev_display | scan_task（1KB，Realtime） | 1144 | 7688 | 29168 |
| 14 | sw2 | dev_w25qxx | s_w25_mutex | 88 | 7776 | 29080 |
| 15 | sw3 | app_factory_test | factory_monitor（1KB） | 1144 | 8920 | 27936 |
| 16 | sw3 | app_dispatch | frame_dispatch_task（1KB） | 1144 | 10064 | 26792 |
| 17 | sw3 | app_light_sensor | light_sensor_task（128 words） | 632 | 10696 | 26160 |
| 18 | sw3 | app_iap | iap_handle_task（1KB） | 1144 | 11840 | 25016 |
| 19 | sw3 | app_ldi | ldi_handle_task（1KB） | 1144 | 12984 | 23872 |
| 20 | sw3 | app_ldi | ldi_timer_task（1KB） | 1144 | 14128 | 22728 |
| 21 | sw3 | app_ldi | ldi tx_lock（互斥） | 88 | 14216 | 22640 |
| 22 | sw3 | app_cq_proto | cq_handle_task（1KB） | 1144 | 15360 | 21496 |
| 23 | sw3 | app_cq_proto | cq_timer_task（1KB） | 1144 | 16504 | 20352 |
| 24 | sw3 | app_rls | rls_handle_task（1KB） | 1144 | 17648 | 19208 |
| 25 | sw3 | app_qh_proto | qh_handle_task（1KB） | 1144 | 18792 | 18064 |
| 26 | sw3 | app_sc_etc | sc_etc_handle_task（1KB） | 1144 | 19936 | 16920 |
| 27 | sw3 | app_sc_mtc | sc_mtc_handle_task（1KB） | 1144 | 21080 | 15776 |
| 28 | sw3 | app_sc_ol | sc_ol_handle_task（1KB） | 1144 | 22224 | 14632 |
| 29 | sw3 | app_sd_proto | sd_handle_task（1KB） | 1144 | 23368 | 13488 |
| 30 | sw3 | app_gz_proto | gz_handle_task（1KB） | 1144 | 24512 | 12344 |
| 31 | sw3 | app_gz_ol_proto | gz_ol_handle_task（1KB） | 1144 | 25656 | 11200 |
| 32 | sw3 | app_yn_proto | yn_handle_task（1KB） | 1144 | 26800 | 10056 |
| 33 | sw3 | app_yn_ol_proto | yn_ol_handle_task（1KB）← 2026-09-17 新接入 | 1144 | 27944 | 8912 |
| 34 | sw3 | app_anhui_proto | anhui_handle_task（1KB） | 1144 | 29088 | 7768 |
| 35 | sw3 | ring_buffer | RJ45 RB mutex（IAP/LDI/CQ 首个 acquire） | 88 | 29176 | 7680 |
| 36 | sw3 | ring_buffer | RS485 RB mutex | 88 | 29264 | 7592 |
| 37 | sw3 | ring_buffer | RS232 RB mutex | 88 | 29352 | 7504 |
| 38 | sw3 | dev_w25qxx | s_evt（首次 _read 懒建） | 40 | 29392 | 7464 |
| 39 | chan | app_boot | half_sec_task（128 words） | 632 | 30024 | 6832 |
| 40 | chan | app_tcp_server | tcp_server_task（1KB） | 1144 | 31168 | 5688 |
| 41 | chan | app_tcp_client | tcp_client_task（1KB） | 1144 | 32312 | 4544 |
| 42 | chan | app_udp | udp_task（1KB，10011） | 1144 | 33456 | 3400 |
| 43 | chan | app_udp | udp_cq_task（1KB，CQ 业务口） | 1144 | 34600 | 2256 |
| 44 | chan | app_udp | udp_gzol_task（1KB，GZ_OL 业务口） | 1144 | 35744 | 1112 |
| 45 | chan | app_rs485 | rs485_task（1KB）← 现场失败点 ⚠超限 | 1144 | 36888 | -32 |
| 46 | chan | app_rs232 | rs232_task（1KB） ⚠超限 | 1144 | 38032 | -1176 |
| 47 | run | app_tcp_server | netconn recvmbox + op_completed ⚠超限 | 200 | 38232 | -1376 |
| 48 | run | app_tcp_server | acceptmbox（netconn_listen） ⚠超限 | 112 | 38344 | -1488 |
| 49 | run | app_tcp_client | client_disconnect_sem ⚠超限 | 88 | 38432 | -1576 |
| 50 | run | app_tcp_client | netconn recvmbox + op_completed ⚠超限 | 200 | 38632 | -1776 |
| 51 | run | app_udp | udp_disconnect_sem ⚠超限 | 88 | 38720 | -1864 |
| 52 | run | app_udp | netconn recvmbox + op_completed ⚠超限 | 200 | 38920 | -2064 |
| 53 | run | app_udp | udp_connect_task（常驻 1KB） ⚠超限 | 1144 | 40064 | -3208 |
| 54 | run | app_udp | udp_cq_disconnect_sem ⚠超限 | 88 | 40152 | -3296 |
| 55 | run | app_udp | netconn recvmbox + op_completed ⚠超限 | 200 | 40352 | -3496 |
| 56 | run | app_udp | udp_cq_connect_task（常驻 1KB） ⚠超限 | 1144 | 41496 | -4640 |
| 57 | run | app_udp | udp_gzol_disconnect_sem ⚠超限 | 88 | 41584 | -4728 |
| 58 | run | app_udp | netconn recvmbox + op_completed ⚠超限 | 200 | 41784 | -4928 |
| 59 | run | app_udp | udp_gzol_connect_task（常驻 1KB） ⚠超限 | 1144 | 42928 | -6072 |
| 60 | run | app_scroll | s_scroll_evt + 双互斥（首个滚动帧懒建） ⚠超限 | 216 | 43144 | -6288 |
| 61 | run | app_scroll | scroll_task（1KB，首个滚动帧懒建） ⚠超限 | 1144 | 44288 | -7432 |
| 62 | run | app_tcp_client | tcp_client_conn_task（连上才建 1KB） ⚠超限 | 1144 | 45432 | -8576 |
| 63 | run | app_yn_proto | yn_selftest_task（'2' 命令触发 1KB） ⚠超限 | 1144 | 46576 | -9720 |
| 64 | run | app_yn_ol_proto | yn_ol_selftest_task（'2' 命令触发 1KB） ⚠超限 | 1144 | 47720 | -10864 |
需求合计=47720B  余量=-10864B  ** 赤字 10864B **
临界点 = 阶段 chan / app_rs485 / rs485_task（1KB）← 现场失败点（需 1144B，此前余 1112B → 申请后赤字 -32B，pvPortMalloc 返回 NULL）

=== 修复后 — PROTO=ALL（make 全协议 dev 构建）（任务栈/TCB 落 .ccmram） ===
configTOTAL_HEAP_SIZE=36864B  初始可用=36856B
| # | 阶段 | 创建者 | 对象 | 字节 | 累计 | 余量 |
|---|---|---|---|---|---|---|
| 1 | pre | app_boot | init_task（512 words） | 2168 | 2168 | 34688 |
| 2 | eth | sys_arch | lwip_sys_mutex（sys_init） | 88 | 2256 | 34600 |
| 3 | eth | lwip/mem | mem_mutex（mem_init → sys_mutex_new，!NO_SYS 恒建） | 88 | 2344 | 34512 |
| 4 | eth | sys_arch | tcpip_mbox（TCPIP_MBOX_SIZE=6） | 112 | 2456 | 34400 |
| 5 | eth | sys_arch | lock_tcpip_core（CORE_LOCKING） | 88 | 2544 | 34312 |
| 6 | eth | sys_arch | tcpip_thread（TCPIP_THREAD_STACKSIZE=1024） | 1144 | 3688 | 33168 |
| 7 | eth | pl_eth | RxPktSemaphore | 88 | 3776 | 33080 |
| 8 | eth | pl_eth | TxPktSemaphore | 88 | 3864 | 32992 |
| 9 | eth | pl_eth | ethernetif_input / EthIf（256 words×4） | 1144 | 5008 | 31848 |
| 10 | eth | pl_net | ethernet_link_thread / EthLink（1KB） | 1144 | 6152 | 30704 |
| 11 | sw2 | dev_key | press_sem ×4（SW1/SW2/SW3/TEST；DIP 无 wait_press） | 352 | 6504 | 30352 |
| 12 | sw2 | dev_display | scan evt flags | 40 | 6544 | 30312 |
| 13 | sw2 | dev_w25qxx | s_w25_mutex | 88 | 6632 | 30224 |
| 14 | sw3 | app_ldi | ldi tx_lock（互斥） | 88 | 6720 | 30136 |
| 15 | sw3 | ring_buffer | RJ45 RB mutex（IAP/LDI/CQ 首个 acquire） | 88 | 6808 | 30048 |
| 16 | sw3 | ring_buffer | RS485 RB mutex | 88 | 6896 | 29960 |
| 17 | sw3 | ring_buffer | RS232 RB mutex | 88 | 6984 | 29872 |
| 18 | sw3 | dev_w25qxx | s_evt（首次 _read 懒建） | 40 | 7024 | 29832 |
| 19 | chan | app_boot | half_sec_task（128 words） | 632 | 7656 | 29200 |
| 20 | chan | app_tcp_server | tcp_server_task（1KB） | 1144 | 8800 | 28056 |
| 21 | chan | app_tcp_client | tcp_client_task（1KB） | 1144 | 9944 | 26912 |
| 22 | chan | app_udp | udp_task（1KB，10011） | 1144 | 11088 | 25768 |
| 23 | chan | app_udp | udp_cq_task（1KB，CQ 业务口） | 1144 | 12232 | 24624 |
| 24 | chan | app_udp | udp_gzol_task（1KB，GZ_OL 业务口） | 1144 | 13376 | 23480 |
| 25 | run | app_tcp_server | netconn recvmbox + op_completed | 200 | 13576 | 23280 |
| 26 | run | app_tcp_server | acceptmbox（netconn_listen） | 112 | 13688 | 23168 |
| 27 | run | app_tcp_client | client_disconnect_sem | 88 | 13776 | 23080 |
| 28 | run | app_tcp_client | netconn recvmbox + op_completed | 200 | 13976 | 22880 |
| 29 | run | app_udp | udp_disconnect_sem | 88 | 14064 | 22792 |
| 30 | run | app_udp | netconn recvmbox + op_completed | 200 | 14264 | 22592 |
| 31 | run | app_udp | udp_connect_task（常驻 1KB） | 1144 | 15408 | 21448 |
| 32 | run | app_udp | udp_cq_disconnect_sem | 88 | 15496 | 21360 |
| 33 | run | app_udp | netconn recvmbox + op_completed | 200 | 15696 | 21160 |
| 34 | run | app_udp | udp_cq_connect_task（常驻 1KB） | 1144 | 16840 | 20016 |
| 35 | run | app_udp | udp_gzol_disconnect_sem | 88 | 16928 | 19928 |
| 36 | run | app_udp | netconn recvmbox + op_completed | 200 | 17128 | 19728 |
| 37 | run | app_udp | udp_gzol_connect_task（常驻 1KB） | 1144 | 18272 | 18584 |
| 38 | run | app_scroll | s_scroll_evt + 双互斥（首个滚动帧懒建） | 216 | 18488 | 18368 |
| 39 | run | app_tcp_client | tcp_client_conn_task（连上才建 1KB） | 1144 | 19632 | 17224 |
| 40 | run | app_yn_proto | yn_selftest_task（'2' 命令触发 1KB） | 1144 | 20776 | 16080 |
| 41 | run | app_yn_ol_proto | yn_ol_selftest_task（'2' 命令触发 1KB） | 1144 | 21920 | 14936 |
需求合计=21920B  余量=14936B
临界点：无（总需求未超堆）

【现场失败点核对】创建 rs485_task 之前：修复前余 1112B（1KB 任务需 1144B → 会失败）；修复后余 23480B（rs485_task 已静态 → 不再申请堆）
【启动期峰值需求（不含 run 阶段异步）】修复前 38032B → 余 -1176B（赤字 1176B）；修复后 13376B → 余 23480B（口径：≥2500B 即满足「同一启动点空闲堆 ≥2.5KB」目标）

静态 CCMRAM 任务 23 个：释放 ucHeap 25800B，占用 .ccmram 25340B（栈 + 100B StaticTask_t，无堆头）
