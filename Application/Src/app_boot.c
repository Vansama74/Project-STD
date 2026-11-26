/**
 * @file    app_boot.c
 * @brief   系统启动编排器
 *
 * 职责：RTOS 生命周期 + 硬件无关的模块初始化 + HalfSecTask
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "main.h" /* RTC_BKP_DRx（Bootloader 崩溃计数清零用） */

#include "app_board_net_cfg.h"
#include "app_boot.h"
#include "app_default_display.h"
#include "app_dispatch.h"
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
#include "initcall.h"
#include "pl_dwt.h"
#include "pl_gpio.h"
#include "pl_iwdg.h"
#include "pl_rtc.h"
#include "SEGGER_RTT.h" /* 2026-11-20 临时根因诊断（定位后清理） */

#define SPLASH_DURATION_MS 5000U

static void init_task(void *argument);

/* ---- HalfSecTask: 500ms 喂狗 / 60s RTC 备份 / LED 翻转 ---- */
/* 喂狗门控：关键路径心跳（EthIf 线程 1s 唤醒 / LDI 定时任务 1s）任一停摆
 * 超过 15s 即停止喂狗，由 IWDG（~33s）兜底复位——看门狗不再只证明
 * 低优先级调度还活着（8h 冻结：网口死 7h42m 仍被无条件喂狗，未复位）。 */
extern volatile uint32_t g_ethwdg_hb;
/* LDI 心跳经弱函数读取：Base 目标（clangd，排除 LDI 族）链接时符号不存在，
 * 弱引用解析为 NULL → LDI 不参与门控（只门控 EthIf），不影响链接。 */
extern uint32_t app_ldi_health_hb(void) __attribute__((weak));

static void half_sec_task(void *argument) {
  (void)argument;
  /* run_time 已随 DR1 30s 清零块一并移除（2026-11-26） */
  bool led_state = false;
  uint32_t hst_div = 0U; /* 临时诊断：每 10s 打印喂狗时间戳 */
  uint32_t last_eth = 0U, last_ldi = 0U;
  uint32_t stall_eth = 0U, stall_ldi = 0U;
  uint8_t armed = 0U;
  uint32_t gate_log = 0U;

  for (;;) {
    uint32_t now = osKernelGetTickCount();
    uint32_t eth = g_ethwdg_hb; /* 盯看门狗任务：它被饿死 = 系统级异常 */
    uint32_t ldi = (app_ldi_health_hb != NULL) ? app_ldi_health_hb() : last_ldi;
    if (armed == 0U && now > 15000U) {
      armed = 1U;
      stall_eth = now;
      stall_ldi = now;
    }
    if (eth != last_eth) {
      last_eth = eth;
      stall_eth = now;
    }
    if (ldi != last_ldi) {
      last_ldi = ldi;
      stall_ldi = now;
    }
    if (app_ldi_health_hb == NULL) {
      stall_ldi = now; /* Base 目标无 LDI 族：LDI 不参与门控 */
    }
    /* 分阈值（2026-11-20 定级）：EthIf 是 Realtime 线程，15s 不推进=系统级
     * 停摆；LDI 定时任务是 Normal，风暴/洪泛下会被合法饿死，给 60s 宽限，
     * 避免洪泛本身把设备推进 IWDG 复位循环。 */
    if (armed != 0U && ((now - stall_eth) > 15000U
                        || (now - stall_ldi) > 60000U)) {
      /* 关键路径停摆：停止喂狗，等 IWDG 复位 */
      if ((now - gate_log) > 5000U) {
        gate_log = now;
        SEGGER_RTT_printf(0, "G t=%u eth=%u ldi=%u\n", (unsigned)now,
                (unsigned)eth, (unsigned)ldi);
      }
      osDelay(500);
      continue;
    }
    pl_iwdg_refresh(pl_iwdg_get_handle());
    /* 健康运行满 10 分钟：清零 Bootloader 的崩溃计数（RTC_BKP_DR1）。
     * Bootloader 只在 IWDG 复位时 +1、只在条件 A/B 时清零，长期运行中
     * 分散的 IWDG 复位会累积，>3 次即判"崩溃过频"跳 Recovery（永久失联）。
     * 本处按"持续健康 10 分钟"视为已稳定，重置计数。 */
    static uint8_t s_crash_cnt_cleared;
    if (s_crash_cnt_cleared == 0U && now > 600000U) {
      pl_rtc_bkup_write(pl_rtc_get_handle(), RTC_BKP_DR1, 0U);
      s_crash_cnt_cleared = 1U;
      SEGGER_RTT_printf(0, "K t=%u dr1=0\n", (unsigned)now);
    }
    if ((++hst_div % 120U) == 0U) {
      SEGGER_RTT_printf(0, "F t=%u\n", (unsigned)now);
    }

    /* 2026-11-26 修复：原「启动 30s 后每 500ms 清零 RTC_BKP_DR1」遗留块已移除。
     * 它让 Bootloader 的崩溃计数永远攒不到 4（条件 B 永不触发），且使上面的
     * 10 分钟健康清零形同虚设——崩溃计数语义被彻底架空。现仅保留「持续健康
     * 10 分钟」一次清零（设计意图：短周期反复崩溃才计数，长期稳定运行豁免）。 */

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

  osThreadId_t init_tid = osThreadNew(init_task, NULL, &attr);
  if (init_tid == nullptr) {
    taskDISABLE_INTERRUPTS();
    for (;;)
      ;
  }
  osKernelStart();
}

[[maybe_unused]] static void app_splash_display(void) {

  if (!dev_display_guard("splash"))
    return;

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

static void init_task(void *argument) {
  (void)argument;

  /* 复位源（2026-11-20）：Bootloader 在清标志前把原始 RCC->CSR 存入 DR10、
   * 崩溃计数副本存 DR11、跳转目标存 DR12（见 Bootloader main.c）。
   * dr1=崩溃计数；jump=0x08040000 为 app / 0x08008000 为 Recovery。 */
  SEGGER_RTT_printf(0, "RC pre=%08X now=%08X dr1=%u jump=%08X\n",
          (unsigned)RTC->BKP10R, (unsigned)RCC->CSR,
          (unsigned)RTC->BKP11R, (unsigned)RTC->BKP12R);
  /* 不清除：Bootloader 每次启动都会重写 DR10/DR12，DR11 仅 IWDG 时写；
   * 保留现场便于 J-Link 随时回读（不被复位清零）。 */

  /* 回读上次 HardFault 现场（stm32f4xx_it.c 写 RTC DR7/DR8/DR9） */
  HAL_PWR_EnableBkUpAccess();
  if (RTC->BKP9R == 0x48465A54U) {
    SEGGER_RTT_printf(0, "HF pc=%08X cfsr=%08X\n",
            (unsigned)RTC->BKP7R, (unsigned)RTC->BKP8R);
    RTC->BKP7R = 0U;
    RTC->BKP8R = 0U;
    RTC->BKP9R = 0U;
  }

  /* 临时诊断：启动阶段时间线（配合 IWDG 无人喂窗口分析） */
  SEGGER_RTT_printf(0, "B t=%u eth_start\n", (unsigned)osKernelGetTickCount());
  dev_eth_start();
  SEGGER_RTT_printf(0, "B t=%u sw_init\n", (unsigned)osKernelGetTickCount());
  sw_board_init(); /* sw_initcall 自注册：协议 + 通道任务 */

  SEGGER_RTT_printf(0, "B t=%u fed\n", (unsigned)osKernelGetTickCount());
  pl_iwdg_refresh(pl_iwdg_get_handle());
  int32_t ver_ret = app_board_net_cfg_fw_version_update(PROGRAM_CODE);
  (void)ver_ret; 

  app_net_boot_apply();
  SEGGER_RTT_printf(0, "B t=%u net_apply\n", (unsigned)osKernelGetTickCount());

  app_splash_display();
  SEGGER_RTT_printf(0, "B t=%u splash_done\n", (unsigned)osKernelGetTickCount());

  /* 半秒周期任务。
   * 优先级 Realtime（2026-11-20 二次定级）：AboveNormal 仍会被 Realtime 的
   * 显示扫描任务（scan_task）饿死——fuzz 实测扫描任务在负载下占满 CPU，
   * 喂狗停止 → IWDG 复位。Realtime + 时间片与扫描任务共存；喂狗只做
   * 刷新+LED，开销极小。健康门控仍在：EthIf 心跳 15s 不推进 = 系统级停摆，
   * 照停喂由 IWDG 复位。 */
  const osThreadAttr_t hst_attr = {
      .name = "half_sec_task",
      .stack_size = 128 * 4,
      .priority = osPriorityRealtime,
  };
  pl_task_create_checked(osThreadNew(half_sec_task, NULL, &hst_attr), "half_sec_task");
  SEGGER_RTT_printf(0, "B t=%u hst_started\n", (unsigned)osKernelGetTickCount());

  if (app_dispatch_channel_has_proto(CH_ID_TCP_SERVER))
    app_tcp_server_start();
  if (app_dispatch_channel_has_proto(CH_ID_TCP_CLIENT))
    app_tcp_client_start();
  if (app_dispatch_channel_has_proto(CH_ID_UDP))
    app_udp_start();
  if (app_dispatch_channel_has_proto(CH_ID_UDP_CQ))
    app_udp_cq_start();
  if (app_dispatch_channel_has_proto(CH_ID_UDP_GZOL))
    app_udp_gzol_start();
  if (app_dispatch_channel_has_proto(CH_ID_RS485))
    app_rs485_start();
  if (app_dispatch_channel_has_proto(CH_ID_RS232))
    app_rs232_start();
  app_rs232_1_start();

  // app_test_run();
  SEGGER_RTT_printf(0, "B t=%u servers\n", (unsigned)osKernelGetTickCount());
  app_default_display();
  SEGGER_RTT_printf(0, "B t=%u init_done\n", (unsigned)osKernelGetTickCount());
  osThreadExit();
}

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

void vApplicationMallocFailedHook(void) {
  /* 处置：关中断 + 死循环——保留完整调用栈/任务现场供调试器取证。若现场无人，
   * 独立看门狗 IWDG（LSI 独立时钟，不受关中断影响，超时 ≈33s）到期复位，
   * 设备不会永久静默卡死（表现为约 33s 周期重启）。 */
  taskDISABLE_INTERRUPTS();
  for (;;)
    ;
}

void vConfigureTimerForRunTimeStats(void) { pl_dwt_init(); }

uint32_t ulGetRunTimeCounterValue(void) { return pl_dwt_get_cycles(); }
