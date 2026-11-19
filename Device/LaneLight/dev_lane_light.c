/**
 * @file    dev_lane_light.c
 * @brief   车道通行灯抽象基类：注册 + 两态互锁
 *
 * 与具体硬件解耦；协议层（LDI E7 等）只调用 dev_lane_light_set()。
 */

#include "dev_lane_light.h"

#include "cmsis_os2.h"

static dev_lane_light_t *s_ll;

void dev_lane_light_register(dev_lane_light_t *ll)
{
    s_ll = ll;
    if (ll == NULL || ll->ops == NULL || ll->ops->set_red == NULL || ll->ops->set_green == NULL)
        return;

    /* 注册发生在 hw_dev_initcall（RTOS 启动前）：此处不得调用 osDelay，
     * 直接按「灭两路 → 亮红叉」驱动，完成上电 fail-safe 态。 */
    ll->ops->set_red(ll, false);
    ll->ops->set_green(ll, false);
    ll->ops->set_red(ll, true);
    ll->state = DEV_LANE_LIGHT_RED_CROSS;
}

dev_lane_light_t *dev_lane_light_get(void)
{
    return s_ll;
}

bool dev_lane_light_set(dev_lane_light_state_t state)
{
    dev_lane_light_t *ll = s_ll;

    if (ll == NULL || ll->ops == NULL || ll->ops->set_red == NULL || ll->ops->set_green == NULL)
        return false;
    if (state != DEV_LANE_LIGHT_RED_CROSS && state != DEV_LANE_LIGHT_GREEN_ARROW)
        return false;
    if (ll->state == state)
        return true;

    /* 互锁：先全灭（内部 ALL_OFF）→ 死区 → 点亮目标 */
    ll->ops->set_red(ll, false);
    ll->ops->set_green(ll, false);
    if (ll->ops->switch_delay_ms > 0U)
        osDelay(ll->ops->switch_delay_ms);

    if (state == DEV_LANE_LIGHT_RED_CROSS)
        ll->ops->set_red(ll, true);
    else
        ll->ops->set_green(ll, true);

    ll->state = state;
    return true;
}

dev_lane_light_state_t dev_lane_light_state(void)
{
    return (s_ll != NULL) ? s_ll->state : DEV_LANE_LIGHT_RED_CROSS;
}
