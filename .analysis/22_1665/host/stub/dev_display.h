/**
 * @file    dev_display.h
 * @brief   22-1665 宿主差分测试桩 — 显示基类（**不是**真机头文件）
 *
 * 字段名/顺序与真机 `Device/Inc/dev_display.h` 的基类一致（驱动只按名字访问），
 * 但只保留驱动用得到的部分：ops / 几何字段 / 缓冲指针 / 脏矩形 / 亮度上限。
 * 新旧两侧驱动用同一份桩编译 ⇒ 差分对照有效。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pl_hub75.h"

typedef enum {
    COLOR_BLACK  = 0,
    COLOR_RED    = 1,
    COLOR_GREEN  = 2,
    COLOR_YELLOW = 3,
    COLOR_BLUE   = 4,
    COLOR_PURPLE = 5,
    COLOR_CYAN   = 6,
    COLOR_WHITE  = 7,
} display_color_t;

typedef struct dev_display dev_display_t;

typedef struct dev_display_ops {
    void (*prepare)(dev_display_t *dev);
    void (*scan)(dev_display_t *dev, uint8_t line);
    void (*set_row)(uint8_t row);
} dev_display_ops_t;

struct dev_display {
    const dev_display_ops_t *ops; /* 第一个成员 */

    uint8_t module_rows;
    uint8_t module_cols;
    uint8_t channels_per_module;
    uint8_t modules_per_row;
    uint8_t modules_per_col;
    uint8_t scan_lines;

    uint16_t screen_rows;
    uint16_t screen_cols;
    uint8_t total_channels;
    uint16_t channel_pixels;
    uint16_t scan_line_pixels;
    uint16_t buffer_size;

    uint8_t *pixel_map;
    uint8_t *hub75_buff;

    const char *module_code;
    volatile uint8_t light_level;
    volatile bool dirty;

    uint16_t dirty_rect_x, dirty_rect_y, dirty_rect_w, dirty_rect_h;
    bool dirty_rect_valid;
};

/** @brief 注册活动显示实例（宿主桩只记录，供 harness 取用） */
void dev_display_register(dev_display_t *dev);

/** 硬件亮度上限（与真机一致） */
#define DEV_DISPLAY_BRIGHTNESS_MAX 8U
