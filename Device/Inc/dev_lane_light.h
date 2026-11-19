/**
 * @file    dev_lane_light.h
 * @brief   车道通行灯设备抽象（红叉 / 绿箭 两态，互锁）
 *
 * 两种并列实现（EIDE 二选一编译，协议解析器一律通过本抽象调用）：
 *   - Device/LaneLight/default/dev_LaneLight_default.c     ：现有单路车道灯 PD14
 *   - Device/LaneLight/DryContact/dev_LaneLight_DryContact.c：干接点 IO（S1/PE12 红叉、S2/PE11 绿箭）
 *
 * 契约：
 *   - 两态互锁：切换时先灭两路，再点亮目标（死区由实现的 switch_delay_ms 决定）。
 *   - 上电默认态 = 红叉（车道关闭，fail-safe）。
 *   - ALL_OFF 仅本抽象内部使用（切换中间态），不对外暴露。
 */

#ifndef DEV_LANE_LIGHT_H
#define DEV_LANE_LIGHT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DEV_LANE_LIGHT_RED_CROSS   = 0, /**< 红叉：车道关闭 */
    DEV_LANE_LIGHT_GREEN_ARROW = 1, /**< 绿箭：车道通行 */
} dev_lane_light_state_t;

typedef struct dev_lane_light dev_lane_light_t;

typedef struct {
    const char *name;
    /** 仅驱动红叉灯（不做互锁；由抽象层保证互锁顺序） */
    void (*set_red)(dev_lane_light_t *self, bool on);
    /** 仅驱动绿箭灯（不做互锁） */
    void (*set_green)(dev_lane_light_t *self, bool on);
    /** 灭→亮之间的死区（继电器建议 10ms；0 = 不延时） */
    uint16_t switch_delay_ms;
} dev_lane_light_ops_t;

struct dev_lane_light {
    const dev_lane_light_ops_t *ops;
    dev_lane_light_state_t state;
};

/** 实现文件初始化时注册唯一实例（内部将状态置为"未知"并立即驱动红叉） */
void dev_lane_light_register(dev_lane_light_t *ll);

/** 当前实例（未注册返回 NULL） */
dev_lane_light_t *dev_lane_light_get(void);

/**
 * @brief 设置通行灯状态（互锁）
 * @return true  = 已生效（或已是目标态）；
 *         false = 未注册 / 状态非法。
 */
bool dev_lane_light_set(dev_lane_light_state_t state);

/** 当前状态（未注册时按 fail-safe 返回红叉） */
dev_lane_light_state_t dev_lane_light_state(void);

#endif /* DEV_LANE_LIGHT_H */
