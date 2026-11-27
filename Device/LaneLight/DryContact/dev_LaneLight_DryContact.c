/**
 * @file    dev_LaneLight_DryContact.c
 * @brief   车道通行灯 — 干接点 IO 实现（S1/PE12 = 红叉，S2/PE11 = 绿箭，高有效）
 *
 * 接管顺序（无毛刺，同缅甸干接点先例 doc/16 §14.2）：
 *   ① 写「灭」电平（引脚仍为输入，写 ODR 不驱动）
 *   ② 逐线摘 EXTI（不碰 PE10/其它线、不碰 NVIC）
 *   ③ 切推挽输出（此刻起真正驱动「灭」电平）
 *   ④ 注册 → 驱动红叉（上电默认，fail-safe）
 *
 * 副作用（与缅甸构建一致）：SW1/SW2 输入语义失效（本板自驱电平被
 * dev_key_get_state 读到）；量产配置请相应排除 RLS 干接点轮询。
 */

#include "dev_lane_light.h"

#include "initcall.h"
#include "pl_gpio.h"

#define LL_RED_PORT   PL_PORT_E
#define LL_RED_PIN    12U /* S1 */
#define LL_GREEN_PORT PL_PORT_E
#define LL_GREEN_PIN  11U /* S2 */

/* 高有效：点亮 = 输出高（缅甸现场实测口径） */
static void _drive(pl_port_t port, uint8_t pin, bool on)
{
    pl_gpio_write(port, pin, on);
}

static void _set_red(dev_lane_light_t *self, bool on)
{
    (void)self;
    _drive(LL_RED_PORT, LL_RED_PIN, on);
}

static void _set_green(dev_lane_light_t *self, bool on)
{
    (void)self;
    _drive(LL_GREEN_PORT, LL_GREEN_PIN, on);
}

static const dev_lane_light_ops_t s_ops = {
    .name            = "dry-contact(S1/PE12,S2/PE11)",
    .set_red         = _set_red,
    .set_green       = _set_green,
    .switch_delay_ms = 10, /* 继电器死区 */
};

static dev_lane_light_t s_ll = {.ops = &s_ops, .state = DEV_LANE_LIGHT_RED_CROSS};

static void _ll_dry_init(void)
{
    /* ① 先写灭电平（输入态下只写 ODR，不驱动引脚） */
    _drive(LL_RED_PORT, LL_RED_PIN, false);
    _drive(LL_GREEN_PORT, LL_GREEN_PIN, false);
    /* ② 只摘本引脚的两条 EXTI 线 */
    pl_gpio_release_exti(LL_RED_PORT, LL_RED_PIN);
    pl_gpio_release_exti(LL_GREEN_PORT, LL_GREEN_PIN);
    /* ③ 切推挽输出：从「稳定的灭」开始驱动 */
    pl_gpio_config_output(LL_RED_PORT, LL_RED_PIN);
    pl_gpio_config_output(LL_GREEN_PORT, LL_GREEN_PIN);
    /* ④ 注册 → 红叉 */
    dev_lane_light_register(&s_ll);
}
hw_dev_initcall(_ll_dry_init);
