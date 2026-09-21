/**
 * @file    app_scroll.h
 * @brief   通用动态滚动显示模块（Application 层，尺寸无关）
 *
 * 安徽费显协议 0x86~0x89 动态显示为第一个消费者；API 预留 LDI/VMS 复用，
 * 不含任何安徽专用语义。行区域矩形由调用方给出，模块启动时以
 * dev_display_get() 实际 screen_rows（宽）/screen_cols（高）钳位。
 *
 * 语义（用户裁决 2026-09-07；2026-09-09 修订停止语义）：
 *   - 循环滚动：文本按指定方向持续滚动，收到停止才停，越过边界后从另一侧重新进入；
 *   - 停止语义：app_scroll_stop = 冻结（不清行、不提交，像素停在当前位置）；
 *     app_scroll_stop_all/_nolock = 清行（0x81/0x85 依赖，独立实现）；
 *   - 方向枚举值对齐安徽运动模式字节（1=LEFT…4=DOWN），映射零开销；
 *   - stay_ms（停留时间）解析并保存，v1 忽略（语义待定，见 doc/13 §8）；
 *   - 文本为 GBK 字节流（安徽 ANSI=GBK 直通；LDI/VMS 复用前先 UTF8ToGBK）。
 *
 * 实现机制见 Application/Src/app_scroll.c 文件头与
 * doc/01_显示系统/动态滚动显示实现记录.md。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "app_render.h"

/* ---- 滚动方向：值对齐安徽协议 0x86~0x89 运动模式字节 ---- */
typedef enum {
    SCROLL_DIR_NONE  = 0, /* 无方向（不合法起点，start 拒绝） */
    SCROLL_DIR_LEFT  = 1, /* 从右往左：文本整体向左移动 */
    SCROLL_DIR_RIGHT = 2, /* 从左往右 */
    SCROLL_DIR_UP    = 3, /* 从下往上 */
    SCROLL_DIR_DOWN  = 4, /* 从上往下 */
} scroll_dir_t;

/* 滚动槽位数（≥4：安徽 4 行够用） */
#define SCROLL_SLOT_MAX (4U)

/**
 * @brief  启动/覆盖一个滚动槽位（同一槽位新启动覆盖旧状态）。
 * @param  slot       槽位号 0..SCROLL_SLOT_MAX-1。
 * @param  x,y,w,h    行区域矩形（屏幕坐标）；越界部分自动钳位到实际屏幕。
 * @param  text       滚动文本（GBK 字节流），立即拷入槽位静态缓冲。
 * @param  text_len   文本字节数（>64 截断至 64，见 SCROLL_TEXT_MAX）。
 * @param  dir        滚动方向（NONE 拒绝）。
 * @param  color      绘制颜色。
 * @param  font_size  字号（0 回退 FONT_16）。
 * @param  font_type  字型。
 * @param  step_ms    每像素步进间隔（毫秒，最小 2；安徽 speed×2ms）。
 * @param  stay_ms    停留时间（安徽 ×100ms 大端；v1 保存但忽略）。
 * @return true=启动成功；false=参数非法（矩形钳位后空/文本空/引擎创建失败）。
 */
bool app_scroll_start(uint8_t slot,
                      uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      const uint8_t *text, uint16_t text_len,
                      scroll_dir_t dir, display_color_t color,
                      font_size_t font_size, font_type_t font_type,
                      uint16_t step_ms, uint16_t stay_ms);

/**
 * @brief  停止指定槽位滚动 = 冻结（用户裁决 2026-09-09，原「停止即清行」）。
 * @param  slot  槽位号；越界忽略。
 * @note   置槽位非活跃、不清行、不提交——像素停在当前位置静态显示
 *         （render 锁保证在途渲染完成后才返回，无半帧风险）。
 *         ⑤ 内部持 app_scroll_render_lock 与滚动渲染串行。
 */
void app_scroll_stop(uint8_t slot);

/**
 * @brief  停止全部槽位滚动并清行（安徽 0x81 清屏 / 0x85 静态显示前调用）。
 * @note   与 app_scroll_stop 的冻结语义独立：置非活跃 + 清行 + 矩形提交，
 *         保证 0x81/0x85 清掉滚动内容（用户裁决 2026-09-09）。
 *         ⑤ 内部持 app_scroll_render_lock 包裹全部清行提交。
 */
void app_scroll_stop_all(void);

/* ---- ⑤ 渲染互斥 API（2026-09-08）---- */

/**
 * @brief  获取渲染互斥锁：滚动渲染（scroll_task 渲染+commit 整段）与
 *         安徽静态渲染（0x81/0x85/0x82/0x83）经此串行，消除混合帧黑条。
 * @note   引擎未创建时无锁直通（无滚动渲染 → 无并发）。
 *         锁序固定 render → slot（槽位互斥），start 仅持 slot，无反向获取。
 */
void app_scroll_render_lock(void);

/** @brief 释放渲染互斥锁（与 app_scroll_render_lock 配对）。 */
void app_scroll_render_unlock(void);

/**
 * @brief  停止全部槽位（无锁变体，清行语义）：调用方须先持 app_scroll_render_lock。
 * @note   供安徽 0x81/0x85 的「stop_all + 渲染 + commit」整段持锁场景使用，
 *         避免公共版 app_scroll_stop_all 内部重复加锁死锁。
 *         与 app_scroll_stop 的冻结语义独立（清行，用户裁决 2026-09-09）。
 */
void app_scroll_stop_all_nolock(void);
