/**
 * @file    dev_LaneLight_default.c
 * @brief   车道通行灯 — 默认实现（现有单路车道灯 PD14）
 *
 * 映射（保持现场既有行为）：
 *   绿箭 = PD14 点亮；红叉 = PD14 熄灭；上电默认红叉。
 * 与 DryContact 实现二选一编译（EIDE 排除表）。
 */

#include "dev_lane_light.h"

#include "dev_io_ctrl.h"
#include "initcall.h"

static void _set_red(dev_lane_light_t *self, bool on)
{
    (void)self;
    /* 单路灯：红叉 = 熄灭 */
    if (on)
        dev_io_lane_light(false);
}

static void _set_green(dev_lane_light_t *self, bool on)
{
    (void)self;
    dev_io_lane_light(on);
}

static const dev_lane_light_ops_t s_ops = {
    .name            = "default(PD14)",
    .set_red         = _set_red,
    .set_green       = _set_green,
    .switch_delay_ms = 0,
};

static dev_lane_light_t s_ll = {.ops = &s_ops, .state = DEV_LANE_LIGHT_RED_CROSS};

static void _ll_default_init(void)
{
    /* 注册即驱动红叉（fail-safe：车道关闭） */
    dev_lane_light_register(&s_ll);
}
hw_dev_initcall(_ll_default_init);
