/**
 * @file    app_light_sensor.c
 * @brief   环境光传感器 RTOS 任务 — 自注册 initcall，1s 周期自动调光
 */

#include "app_light_sensor.h"
#include "cmsis_os2.h"
#include "initcall.h"
#include "dev_display.h"
#include "pl_task_guard.h"
#include "pl_task_static.h"

/* ---- 任务静态存储（栈 + TCB 落 CCMRAM，见 pl_task_static.h）----
 * light_sensor_task：启动期创建一次、永不退出；静态化后不再占 ucHeap
 * （省 632B = 栈 512 + TCB 112 块），CCM 占 612B。 */
PL_TASK_STATIC_STORAGE(light_sensor, 128);

static light_sensor_dev_t s_sensor_dev;
osThreadId_t g_light_sensor_task_handle;

void app_light_sensor_task(void *argument)
{
    (void)argument;
    for (;;) {
        dev_light_sensor_auto_adjust(&s_sensor_dev);
        osDelay(1000);
    }
}

void app_light_sensor_set_range(uint8_t min, uint8_t max)
{
    dev_light_sensor_set_range(&s_sensor_dev, min, max);
}

void app_light_sensor_init(void)
{
    dev_light_sensor_init(&s_sensor_dev, dev_display_get());
    dev_light_sensor_set_range(&s_sensor_dev, LIGHT_SENSOR_MIN_LEVEL, LIGHT_SENSOR_MAX_LEVEL);

    const osThreadAttr_t attr = {
        .name       = "light_sensor_task",
        .priority   = osPriorityLow,
        PL_TASK_STATIC_ATTR(light_sensor, 128),
    };
    g_light_sensor_task_handle =
        pl_task_create_checked(osThreadNew(app_light_sensor_task, NULL, &attr), "light_sensor_task");
}
sw_app_initcall(app_light_sensor_init);
