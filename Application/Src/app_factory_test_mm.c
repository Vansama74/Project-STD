/**
 * @file    app_factory_test_mm.c
 * @brief   出厂检测（缅甸版）：TEST 键切换 0x0A / 0x0B 箭头落点（不做掉电保存）
 *
 * 与标准版 `Application/Src/app_factory_test.c` **二选一编译**：两文件对外符号同名同义
 * （`app_factory_mode_interrupt` / `app_factory_test_active` / `g_factory_test`）⇒ 同编即
 * 链接报 multiple definition（天然守卫）。选择入口：Makefile `FACTORY=STD|MM`、
 * EIDE `targets.<目标>.excludeList`。语义/日志与现场测试矩阵见
 * `doc/16_缅甸2D费显协议/README.md` §13。
 */

#include "app_factory_test.h"

#include "cmsis_os2.h"
#include "initcall.h"

#include "app_mm_proto.h" /* mm_arrow_map_toggle()（缅甸模块的映射交换 API） */
#include "dev_key.h"      /* dev_key_wait_press(DEV_KEY_TST, …) */
#include "pl_task_guard.h"
#include "pl_task_static.h"

/* 栈 + TCB 落 CCMRAM（全生命周期只创建一次，不占 ucHeap；栈 256 words 与标准版同口径） */
PL_TASK_STATIC_STORAGE(mm_factory_monitor, 256);

/** 监控任务句柄（与标准版同名的对外符号；工程内无外部引用者，保留以对齐契约）。 */
osThreadId_t g_factory_test;

/**
 * @brief  缅甸版监视线程：等 TEST 键 → 切换 0x0A / 0x0B 箭头落点。
 * @param  argument  未使用。
 */
static void mm_factory_monitor_task(void *argument)
{
    (void)argument;

    for (;;) {
        /* 一次性消费 TEST 键（EXTI 信号量）；被唤醒即切换映射，再按一次还原。 */
        if (!dev_key_wait_press(DEV_KEY_TST, osWaitForever))
            continue;
        (void)mm_arrow_map_toggle();
    }
}

/* 自注册（与标准版同为 sw_app_initcall，收录序一致 ⇒ initcall 次序不变） */
static void _factory_test_init(void)
{
    const osThreadAttr_t attr = {
        .name     = "mm_factory_test",
        .priority = osPriorityHigh,
        PL_TASK_STATIC_ATTR(mm_factory_monitor, 256),
    };
    g_factory_test =
        pl_task_create_checked(osThreadNew(mm_factory_monitor_task, NULL, &attr), "mm_factory_test");
}
sw_app_initcall(_factory_test_init);

/**
 * @brief  业务数据到达中止钩子 —— 缅甸版空实现。
 *
 * 标准版语义 = 置中止标志让 monitor 回 IDLE；缅甸版没有会被业务数据打断的检测序列
 * （TEST 键仅切换映射、不占屏），保留空函数体纯为「同一套对外符号」契约
 *（`app_dispatch.c` 恒调用它）。
 */
void app_factory_mode_interrupt(void)
{
    /* 无序列可中止：TEST 键切换映射与业务数据互不干扰，不做任何动作。 */
}

/**
 * @brief  工厂测试激活查询（供 CQ 心跳类定时任务判定是否暂停计数）。
 * @note   缅甸版不进入任何占用显示资源的检测序列 ⇒ 恒 false（心跳计数照常）。
 */
bool app_factory_test_active(void)
{
    return false;
}
