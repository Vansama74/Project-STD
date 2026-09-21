/**
 * @file    app_boot.c
 * @brief   系统启动编排器
 *
 * 职责：RTOS 生命周期 + 硬件无关的模块初始化 + HalfSecTask
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_board_net_cfg.h"
#include "app_boot.h"
#include "app_default_display.h"
#include "app_diag.h"
#include "app_dispatch.h" /* 调度层诊断计数（qfull/resync，post-boot 行） */
#include "app_key.h"
#include "app_net_boot.h"
#include "app_render.h"
#include "app_rs232.h"
#include "app_rs485.h"
#include "app_tcp_client.h"
#include "app_tcp_server.h"
#include "app_test.h"
#include "app_udp.h"
#include "pl_task_guard.h"
#include "pl_task_static.h"
#include "dev_display.h"
#include "dev_eth.h"
#include "dev_key.h"
#include "initcall.h"
#include "pl_dwt.h"
#include "pl_gpio.h"
#include "pl_iwdg.h"
#include "pl_rtc.h"
#include "pl_sys.h"
#include "SEGGER_RTT.h"

#define SPLASH_DURATION_MS 5000U

static void init_task(void *argument);

/* ---- HalfSecTask: 500ms 喂狗 / 60s RTC 备份 / LED 翻转 ---- */
static void half_sec_task(void *argument) {
  (void)argument;
  uint32_t run_time = 0;
  bool led_state = false;

  for (;;) {
    pl_iwdg_refresh(pl_iwdg_get_handle());

    if (run_time >= 60)
      pl_rtc_bkup_write(pl_rtc_get_handle(), 1, 0);
    else
      run_time++;

    led_state = !led_state;
    pl_gpio_write(PL_PORT_D, 9, led_state); /* LED = PD9 */

    osDelay(500);
  }
}

/* ---- 启动入口 ---- */
void app_boot(void) {
  const osThreadAttr_t attr = {
      .name = "init_task",
      .stack_size = 512 * 4,
      .priority = osPriorityHigh,
  };

  osKernelInitialize();
  /* init_task 是整条启动链的起点（协议任务 / 通道任务 / 队列初始化全在它里面），
   * 创建失败即「设备静默无任何功能」——必须留下证据再停。RTT 已在 hw_initcall
   * 的 pl_rtt_init() 中就绪，可在调度器启动前输出。 */
  osThreadId_t init_tid = osThreadNew(init_task, NULL, &attr);
  if (init_tid == nullptr) {
    SEGGER_RTT_printf(0,
                      "[err] init_task create FAILED (heap exhausted): free=%u min=%u "
                      "ucHeap=%u -> halt\n",
                      (unsigned)xPortGetFreeHeapSize(),
                      (unsigned)xPortGetMinimumEverFreeHeapSize(),
                      (unsigned)configTOTAL_HEAP_SIZE);
    taskDISABLE_INTERRUPTS();
    for (;;)
      ;
  }
  osKernelStart();
}

[[maybe_unused]] static void app_splash_display(void) {
  dev_display_t *dev = dev_display_get();
  char fw_line[32];
  char md_line[32];
  char splash_text[96];

  snprintf(fw_line, sizeof(fw_line), "FW:%s", PROGRAM_CODE);
  snprintf(md_line, sizeof(md_line), "MD:%s", dev->module_code);
  snprintf(splash_text, sizeof(splash_text), "%s\n%s", fw_line, md_line);

  dev_display_fill(dev, 0, 0, dev->screen_rows, dev->screen_cols, COLOR_BLACK);
  dev->dirty = false;

  app_render(&(render_cfg_t){
      .type = RENDER_TEXT,
      .x = 0,
      .y = 0,
      .w = dev->screen_rows,
      .h = dev->screen_cols,
      .style =
          &(render_style_t){
              .h_align = ALIGN_CENTER,
              .v_align = ALIGN_CENTER,
              .word_wrap = false,
          },
      .color = COLOR_GREEN,
      .text = splash_text,
      .len = strlen(splash_text),
      .font_size = FONT_SELF_ADAPT,
      .font_type = FONT_ST,
      .text_enc = FONT_ENC_UTF8,
  });

  osDelay(SPLASH_DURATION_MS);
  dev_display_fill(dev, 0, 0, dev->screen_rows, dev->screen_cols, COLOR_BLACK);
  dev->dirty = false;
}

/* ================================================================
 *  自证诊断（APP_DIAG_BANNER，见 app_diag.h）：开机横幅 + 延迟网络体检
 *
 *  全部只读打印，不改变任何行为；APP_DIAG_BANNER 置 0（或 make APP_DIAG=0）后
 *  本块整体消失。横幅把「板上跑的是不是最新构建 / 屏体几何是 32×16 还是 224×64 /
 *  激活的是哪套字库布局 / 端口到底绑到哪」变成 RTT 里可直接读出的事实。
 * ================================================================ */
#if APP_DIAG_BANNER

/**
 * @brief 堆余量探针（永久诊断）
 *
 * `free` = `xPortGetFreeHeapSize()` 当前余量；`min` = `xPortGetMinimumEverFreeHeapSize()`
 * **开机以来最小余量**（"是否曾经擦边"的权威值，比瞬时值更能反映风险）。
 * 用途：任何新增任务/队列/互斥**之前**先看这里——`min` 一旦逼近 0 即说明
 * 启动期曾经堆耗尽（2026-09-17 现场即如此，见 doc/06-04 §7.3）。
 */
static void app_diag_heap(const char *tag) {
  SEGGER_RTT_printf(0, "[diag] heap %s free=%u min=%u\n", (tag != nullptr) ? tag : "?",
                    (unsigned)xPortGetFreeHeapSize(), (unsigned)xPortGetMinimumEverFreeHeapSize());
}

/* 显示模组驱动是否编入本镜像：弱符号未编入 → 地址 0（链接期不报错） */
extern void dev_display_1_263_init(void) __attribute__((weak));
extern void dev_display_22_1703_init(void) __attribute__((weak));

/** @brief 通道服务状态字符串（未注册 = "-"） */
static const char *_diag_ch_state(const channel_t *ch) {
  if (ch == nullptr)
    return "-";
  return (ch->state == CH_STATE_UP) ? "UP" : "DOWN";
}

/** @brief 开机横幅（RTOS 起来、网络配置应用后、开机画面之前） */
static void app_diag_boot_banner(void) {
  const dev_display_t *d = dev_display_get();

  SEGGER_RTT_printf(0, "\n[diag] ================ SELF-PROVING BANNER ================\n");
  SEGGER_RTT_printf(0, "[diag] fw=%s built=%s %s tree=%s\n", PROGRAM_CODE, __DATE__, __TIME__,
                    APP_DIAG_TREE_HASH);
  SEGGER_RTT_printf(0, "[diag] build proto=%s disp=%s config=%s toolchain=%s\n", APP_DIAG_PROTO,
                    APP_DIAG_DISP, APP_DIAG_CONFIG, APP_DIAG_TOOLCHAIN);
  /* 复位原因 + 引导计数（2026-09-18 现场取证补强）：
   * ① pl_sys_reset_cause()：RCC->CSR 复位标志。**注意（实测）**：Bootloader 在跳转
   *    主固件前会 HAL_RCC_DeInit()（内部写 RMVF）清掉这些标志 ⇒ 主固件读到的
   *    csr 通常无复位位（本行保留原值，便于将来 Bootloader 若改为留档即可用）。
   * ② bkp0/bkp1：Bootloader 使用的 RTC 备份寄存器（掉电/复位不丢）——
   *      bkp0 = 强制升级标志（非 0 ⇒ 上次走「强制进 Recovery」路径）；
   *      **bkp1 = IWDG 复位计数**：Bootloader 仅在「上次复位是 IWDG」时 +1
   *      （Bootloader main.c: if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) BKUP(DR1)++），
   *      主固件稳定运行满 30s 后由 half_sec_task 清 0。
   *      ⇒ **bkp1 ≥ 1 = 上一次复位是「固件卡死被看门狗复位」**（真死机的铁证）；
   *        bkp1 = 0 = 非 IWDG 复位（外部烧录器的 SYSRESETREQ/NRST、掉电、软件复位）
   *      ⇒ 「板子自己死机重启」与「有人重烧/断电」由此一行即可区分（此前完全不可见）。 */
  {
    const pl_reset_cause_t rc = pl_sys_reset_cause();
    const pl_rtc_handle_t  rtc = pl_rtc_get_handle();
    SEGGER_RTT_printf(0,
                      "[diag] reset csr=0x%08x [iwdg=%u sft=%u por=%u pin=%u bor=%u lpw=%u wwdg=%u] "
                      "bkp0(force)=%u bkp1(iwdg_cnt)=%u\n",
                      (unsigned)rc.raw, rc.iwdg ? 1U : 0U, rc.sft ? 1U : 0U, rc.por ? 1U : 0U,
                      rc.pin ? 1U : 0U, rc.bor ? 1U : 0U, rc.lpw ? 1U : 0U, rc.wwdg ? 1U : 0U,
                      (unsigned)pl_rtc_bkup_read(rtc, 0), (unsigned)pl_rtc_bkup_read(rtc, 1));
  }
  /* 堆基线：banner 处只剩网络/键/显示等已建对象的余量；min 与 free 相等即未擦边 */
  SEGGER_RTT_printf(0, "[diag] heap at banner free=%u min=%u (ucHeap=%u)\n",
                    (unsigned)xPortGetFreeHeapSize(), (unsigned)xPortGetMinimumEverFreeHeapSize(),
                    (unsigned)configTOTAL_HEAP_SIZE);

  if (d != nullptr) {
    SEGGER_RTT_printf(0, "[diag] display screen=%ux%u code=%s scan_lines=%u chans=%u\n",
                      (unsigned)d->screen_rows, (unsigned)d->screen_cols,
                      (d->module_code != nullptr) ? d->module_code : "?", (unsigned)d->scan_lines,
                      (unsigned)d->total_channels);
    SEGGER_RTT_printf(0, "[diag] display module=%ux%u ch/mod=%u mods=%ux%u scan_line_px=%u buf=%u\n",
                      (unsigned)d->module_rows, (unsigned)d->module_cols,
                      (unsigned)d->channels_per_module, (unsigned)d->modules_per_row,
                      (unsigned)d->modules_per_col, (unsigned)d->scan_line_pixels,
                      (unsigned)d->buffer_size);
  } else {
    SEGGER_RTT_printf(0, "[diag] display dev_display_get()=NULL *** no display driver linked ***\n");
  }
  SEGGER_RTT_printf(0, "[diag] driver linked? 1_263=%u 22_1703=%u (exactly one expected)\n",
                    (dev_display_1_263_init != nullptr) ? 1U : 0U,
                    (dev_display_22_1703_init != nullptr) ? 1U : 0U);

  app_render_chip_info_t ci;
  if (app_render_chip_info_get(&ci)) {
    SEGGER_RTT_printf(0,
                      "[diag] font chip=%s regions=%u adapt=%u ascii_raw=%u gbk190=%u cap=%u dip2=%u\n",
                      ci.name, (unsigned)ci.region_count, (unsigned)ci.adaptive_count,
                      ci.ascii_raw_code ? 1U : 0U, ci.gbk_index_190 ? 1U : 0U,
                      (unsigned)ci.flash_capacity, dev_key_get_state(DEV_KEY_DIP2) ? 1U : 0U);
  }

  app_board_net_cfg_t cfg;
  const bool netcfg_ok = (app_board_net_cfg_get(&cfg) == 0);
  if (netcfg_ok) {
    SEGGER_RTT_printf(0,
                      "[diag] netcfg VALID ip=%u.%u.%u.%u mask=%u.%u.%u.%u gw=%u.%u.%u.%u "
                      "port=%u udp_port=%u\n",
                      cfg.ip[0], cfg.ip[1], cfg.ip[2], cfg.ip[3], cfg.mask[0], cfg.mask[1],
                      cfg.mask[2], cfg.mask[3], cfg.gw[0], cfg.gw[1], cfg.gw[2], cfg.gw[3],
                      (unsigned)cfg.port, (unsigned)cfg.udp_port);
  } else {
    app_board_sys_info_t raw;
    app_board_net_cfg_read(&raw);
    SEGGER_RTT_printf(0,
                      "[diag] netcfg INVALID magic=0x%x update_sta=%u raw_port=%u raw_udp_port=%u\n",
                      (unsigned)raw.magic, (unsigned)raw.update_sta, (unsigned)raw.net_cfg.port,
                      (unsigned)raw.net_cfg.udp_port);
  }
  /* gzol_cached = 任务启动前的静态初值（9528），**不是**实际服务口——实际值看本函数之后
   * 的 post-boot 行 `chan gzol=... port=... bind=...`；expect = net_cfg.port（真源）。 */
  SEGGER_RTT_printf(0,
                    "[diag] ports t0 udp10011=%u tcp_biz=%u cq_udp=%u gzol_cached=%u "
                    "(expect gzol=%u = net_cfg.port; rule: 0/invalid->9528; ==10011/20103->skip "
                    "bind)\n",
                    (unsigned)app_udp_get_port(), (unsigned)app_tcp_server_get_port(),
                    (unsigned)app_udp_cq_get_port(), (unsigned)app_udp_gzol_get_port(),
                    netcfg_ok ? (unsigned)cfg.port : 0U);
}

/** @brief 延迟体检（通道任务已跑起来后）：实际绑定端口 / 通道是否在服务 */
static void app_diag_boot_late(void) {
  app_board_net_cfg_t cfg;
  const bool cfg_ok = (app_board_net_cfg_get(&cfg) == 0);

  SEGGER_RTT_printf(0, "[diag] ---- post-boot check (channels started) ----\n");
  SEGGER_RTT_printf(0, "[diag] chan udp10011=%s port=%u | cq_udp=%s port=%u\n",
                    _diag_ch_state(app_channel_get(CH_ID_UDP)), (unsigned)app_udp_get_port(),
                    _diag_ch_state(app_channel_get(CH_ID_UDP_CQ)),
                    (unsigned)app_udp_cq_get_port());
  SEGGER_RTT_printf(0, "[diag] chan gzol=%s port=%u bind=%s | tcp_srv=%s port=%u\n",
                    _diag_ch_state(app_channel_get(CH_ID_UDP_GZOL)),
                    (unsigned)app_udp_gzol_get_port(), app_udp_gzol_bind_status_str(),
                    _diag_ch_state(app_channel_get(CH_ID_TCP_SERVER)),
                    (unsigned)app_tcp_server_get_port());
  if (cfg_ok) {
    SEGGER_RTT_printf(0, "[diag] netcfg readback port=%u udp_port=%u (source of truth)\n",
                      (unsigned)cfg.port, (unsigned)cfg.udp_port);
  }
  /* 调度层丢帧/重同步/通知丢弃累计计数（2026-09-17 YN_OL 联调轮新增，只读）：
   * qfull       = 协议帧队列满 → 丢帧次数（协议任务被卡住/过载的直接证据）；
   * resync      = 探测链 1 字节重同步次数（含残帧卡头预算到期的强制重同步）；
   * notify_drop = 通道通知（ch_queue）投递失败次数（含 20ms 重试；正常恒 0，
   *               非 0 = 「数据已进 RB 但无人唤醒分发任务」的历史现场）。
   * 这里打印的是**开机基线**（正常为 0/0/0）；现场运行期的增量以
   * `[disp] frame queue FULL` / `[disp] WAIT budget ... expired` /
   * `[disp] probe ...` / `[disp] notify queue FULL` 告警行为准。 */
  SEGGER_RTT_printf(0,
                    "[diag] dispatch qfull=%u resync=%u notify_drop=%u (boot baseline; watch "
                    "[disp] warnings)\n",
                    (unsigned)app_dispatch_qfull_drops(), (unsigned)app_dispatch_resync_count(),
                    (unsigned)app_dispatch_notify_drops());
  SEGGER_RTT_printf(0,
                    "[diag] tip TCP diagnostics: [tcp_srv]/[tcp_cli] accept/recv/close; probe "
                    "decisions [disp] probe ...\n");
  SEGGER_RTT_printf(0,
                    "[diag] tip GZ_OL frames: 9528 (or 0x40-set value) / 10011 both accepted; "
                    "0x20 & 0x40 print per-frame evidence under [gz_ol]\n");
  /* 启动期真正的最低水位：min 与「通道启动后」各探针的 free 之差即启动期净消耗 */
  app_diag_heap("post-boot(min-ever = boot 期最低水位)");
}

#else /* !APP_DIAG_BANNER */

/* 诊断关：探针退化为空语句（调用点无需 #if 包裹） */
#define app_diag_heap(tag) ((void)0)

#endif /* APP_DIAG_BANNER */

static void init_task(void *argument) {
  (void)argument;

  dev_eth_start();
  sw_board_init(); /* sw_initcall 自注册：协议 + 通道任务 */

  /* 版本落库：PROGRAM_CODE 写入 Sector1 app_info.version，供 IAP 0x03 上报。
   * 16KB 扇区擦除耗时数百 ms，写入前喂一次狗；同值跳过保证每次版本发布只擦一次 */
  pl_iwdg_refresh(pl_iwdg_get_handle());
  int32_t ver_ret = app_board_net_cfg_fw_version_update(PROGRAM_CODE);
  (void)ver_ret; /* 失败不阻断启动（升级中间态/擦写错误静默降级，IAP 0x03 报旧值） */

  /* 网络配置横切应用（app_net_boot 中立模块）：Sector1 net_cfg → netif + TCP
   * Server 口（PROTO_CHONGQING 只应用 netif）。顺序约束：fw_version_update 对
   * 空/损坏扇区初始化时 net_cfg 置 0 → 本函数判无效写默认（accept_write）；
   * ldi_module_init（sw_board_init 内）的 W25 自愈/回写已先于本调用完成。 */
  app_net_boot_apply();

#if APP_DIAG_BANNER
  /* 自证横幅：必须在 app_net_boot_apply() 之后（net_cfg 已定稿）、开机画面之前 */
  app_diag_boot_banner();
#endif

  app_splash_display();

  /* 半秒周期任务 */
  const osThreadAttr_t hst_attr = {
      .name = "half_sec_task",
      .stack_size = 128 * 4,
      .priority = osPriorityLow,
  };
  pl_task_create_checked(osThreadNew(half_sec_task, NULL, &hst_attr), "half_sec_task");

  /* 通道启动逐个探针：任何一步后 free/min 归零即说明 ucHeap 已到临界，
   * 无需上调试器即可定位（2026-09-17 现场为 rs485_task 处堆耗尽）。 */
  app_tcp_server_start();
  app_diag_heap("after tcp_server_start");
  /* 两口径无条件启动；远端默认 0.0.0.0:0，未配置则任务 idle 不 connect
   *（LDI 装载才 set_remote）。 */
  app_tcp_client_start();
  app_diag_heap("after tcp_client_start");

  app_udp_start();
  app_diag_heap("after udp_start(10011)");
  /* CQ 业务口 UDP（端口 Sector1 net_cfg.udp_port，默认 20103，PROTO_CHONGQING 读） */
  app_udp_cq_start();
  app_diag_heap("after udp_cq_start");
  /* 贵州治超业务口 UDP（端口 Sector1 net_cfg.port，与 TCP 业务口同号；GZ_OL `0x40`
   * 改端口生效对象。必须在 app_net_boot_apply() 之后启动——端口已按 Sector1 应用） */
  app_udp_gzol_start();
  app_diag_heap("after udp_gzol_start");
  app_rs485_start();
  app_diag_heap("after rs485_start <= 现场历史失败点(rs485_task)");
  /* TX1/RX1：地区协议 RS232（USART3） */
  app_rs232_start();
  app_diag_heap("after rs232_start <= 现场历史失败点(rs232_task)");
  /* USART6：语音板 TTS 旁路 TX（dev_rs232_voice），禁止协议 bind；不启 RX
   * 通道任务 */
  app_rs232_1_start();
  app_diag_heap("after channels(all)");

#if APP_DIAG_BANNER
  /* 延迟体检：给 udp_gzol_task / tcp_server_task 等绑定时间（2.5s），
   * 打印各通道实际端口与绑定结果——「改端口是否生效」的直接证据 */
  osDelay(2500);
  app_diag_boot_late();
#endif

  /* app_test_run() = 逐像素走位自检（app_test.c，**内部 for(;;) 不会返回**）：
   * 开启即独占屏幕——面板上只显示一个移动的绿点，normal 画面（app_default_display）
   * 永远不会被绘制。**出厂/现场口径必须保持注释**（app_test.h 原文：「正式发布时
   * 移除 app_test_run() 调用即可」）。
   * bring-up / 现场「显示乱」排查不需要改本文件：
   *   ① 出厂检测（TEST 键，app_factory_test.c）——逐次按键走到老化（单个 16x16 字形）
   *      与斜扫（移动对角线），可直接判「内容溢出」还是「映射/时序错」；
   *   ② 需要逐点走位时，把 app_test.c 的 app_test_run() 换成 app_test_pixel_scan()
   *      （全链路）或 app_test_led_mapping()（直接走 hub75_buff = 链序探针），
   *      再取消本行注释。判读表见 doc/01_显示系统/22-1665模组驱动分析与迁移记录.md §5
   *      （现场排障判读）与 `.analysis/22_1665/README.md`（现场判读速查）。 */
  // app_test_run();
  app_default_display();
  osThreadExit();
}

/* ---- FreeRTOS 内核静态任务缓冲：空闲任务 / 定时器服务任务 → CCMRAM ----
 *
 * `configSUPPORT_STATIC_ALLOCATION=1`（FreeRTOSConfig.h）⇒ 内核不用 ucHeap 装
 * 「空闲任务」与「定时器服务任务」，改为回调本函数索取静态 TCB + 栈。
 * cmsis_os2.c 里的默认实现是 `__WEAK`，把缓冲定义在**它自己的 .bss（SRAM）**：
 *   Idle_TCB 100 + Idle_Stack 512 + Timer_TCB 100 + Timer_Stack 1024 = 1736B
 * （`nm` 实测落在 0x20007e30 / 0x200079cc 等 SRAM 地址）——SRAM 余量仅数百 B，
 * 这块必须让位。此处以**强定义覆盖**弱符号，把两个任务的 TCB+栈整体改置 CCMRAM。
 *
 * 安全性：CCM 不可被 DMA/ETH 访问，但任务栈/TCB 只被 CPU（上下文切换、函数栈帧）
 * 读写，不经 DMA——与 `pl_task_static.h` 把 23 个应用任务下沉 CCM 同源同理；
 * `.ccmram` 由 `startup.c` 整体清零，满足「静态 TCB 缓冲须已清零」。
 *
 * ⚠ 仍未下沉的部分：`timers.c` 内的定时器队列（`xStaticTimerQueue` +
 * `ucStaticTimerQueueStorage`，实测 0x50+0xA0 = 240B）是 timers.c 的 static 局部，
 * 无法从外部改置；若将来需要再挤 SRAM，可评估 `configUSE_TIMERS=0`
 * （全项目无 `osTimerNew`/`xTimerCreate` 调用点，见 CLAUDE.md「待用户裁决」）。
 *
 * 调用时机：`vTaskStartScheduler()` 内（先空闲任务、后定时器任务），早于 init_task。 */

static StaticTask_t s_idle_task_tcb PL_TASK_CCMRAM;
static StackType_t s_idle_task_stack[configMINIMAL_STACK_SIZE] PL_TASK_CCMRAM;
static StaticTask_t s_timer_task_tcb PL_TASK_CCMRAM;
static StackType_t s_timer_task_stack[configTIMER_TASK_STACK_DEPTH] PL_TASK_CCMRAM;

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                  StackType_t **ppxIdleTaskStackBuffer,
                                  uint32_t *pulIdleTaskStackSize) {
  *ppxIdleTaskTCBBuffer   = &s_idle_task_tcb;
  *ppxIdleTaskStackBuffer = &s_idle_task_stack[0];
  *pulIdleTaskStackSize   = (uint32_t)configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                   StackType_t **ppxTimerTaskStackBuffer,
                                   uint32_t *pulTimerTaskStackSize) {
  *ppxTimerTaskTCBBuffer   = &s_timer_task_tcb;
  *ppxTimerTaskStackBuffer = &s_timer_task_stack[0];
  *pulTimerTaskStackSize   = (uint32_t)configTIMER_TASK_STACK_DEPTH;
}

/* ---- FreeRTOS 钩子 ---- */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  /* 调试时在此设断点，查看 pcTaskName 确定溢出任务 */
  volatile char *name = pcTaskName;
  volatile TaskHandle_t task = xTask;
  (void)name;
  (void)task;
  for (;;)
    ;
}

/** @brief 失败请求字节数（heap_4.c 本地诊断补丁导出）
 *  口径 = 调用方请求 + 8B 块头，再按 8B 向上对齐（heap_4 实耗口径，
 *  例：请求 1024 → 1032、请求 100 → 112），**不是裸请求值**。 */
extern size_t xLastFailedAllocSize;

void vApplicationMallocFailedHook(void) {
  /* 现场可判读证据（RTT 通道 0）：RTT 只做内存拷贝 + 轮询，不依赖中断，可在此
   * 上下文安全输出；`min` 是开机以来最小余量——失败瞬间它必然为「已归零」。
   * request = 本次申请字节数（heap_4 补丁），可直接对上「谁要多少」。 */
  SEGGER_RTT_printf(0,
                    "[err] pvPortMalloc FAILED: request=%u free=%u min=%u ucHeap=%u\n"
                    "[err] -> halt (keep stack for debugger); IWDG(256x4096/32kHz~33s) resets "
                    "if unattended\n",
                    (unsigned)xLastFailedAllocSize, (unsigned)xPortGetFreeHeapSize(),
                    (unsigned)xPortGetMinimumEverFreeHeapSize(), (unsigned)configTOTAL_HEAP_SIZE);
  /* 处置选择：关中断 + 死循环——保留完整调用栈/任务现场供调试器取证（调试期这是
   * 最高价值的行为）。若现场无人，独立看门狗 IWDG（LSI 独立时钟，不受关中断影响，
   * 超时 ≈33s）到期复位，设备不会永久静默卡死（表现为约 33s 周期重启）。 */
  taskDISABLE_INTERRUPTS();
  for (;;)
    ;
}

void vConfigureTimerForRunTimeStats(void) { pl_dwt_init(); }

uint32_t ulGetRunTimeCounterValue(void) { return pl_dwt_get_cycles(); }
