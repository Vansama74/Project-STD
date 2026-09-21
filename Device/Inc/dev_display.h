/**
 * @file    dev_display.h
 * @brief   HUB75 LED 点阵显示设备 — OCP 虚表基类
 *
 * 基类提供通用参数和 scan_task 调度骨架。
 * 派生类通过 ops 虚表注入模组差异：像素映射、行地址编码、扫描策略。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pl_hub75.h"

/* ---- 颜色（上层 API 使用，不与 HUB75 引脚耦合）---- */
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

/* ---- 操作虚表 ---- */
typedef struct dev_display_ops {
    void (*prepare)(dev_display_t *dev);            /* 像素→发送缓存 (dirty 时调用) */
    void (*scan)(dev_display_t *dev, uint8_t line); /* 输出一个扫描行 */
    void (*set_row)(uint8_t row);                   /* ABCD 行地址编码 */
} dev_display_ops_t;

/* ---- 基类 (派生类必须将其放在第一个成员位置) ---- */
struct dev_display {
    const dev_display_ops_t *ops; /* 第一个成员 */

    /* 通用参数 */
    uint8_t module_rows;         /* 单模块像素行数 */
    uint8_t module_cols;         /* 单模块像素列数 */
    uint8_t channels_per_module; /* 每模块通道数 */
    uint8_t modules_per_row;     /* 每行模块数 */
    uint8_t modules_per_col;     /* 每列模块数 */
    uint8_t scan_lines;          /* 扫描行数 (静态=1, 1/4扫=4...) */

    /* 派生参数 */
    uint16_t screen_rows;      /* = modules_per_row * module_rows */
    uint16_t screen_cols;      /* = modules_per_col * module_cols */
    uint8_t total_channels;    /* = modules_per_col * channels_per_module */
    uint16_t channel_pixels;   /* = module_rows * module_cols * modules_per_row / total_channels */
    uint16_t scan_line_pixels; /* = channel_pixels / scan_lines */
    uint16_t buffer_size;      /* = screen_rows * screen_cols */

    /* 缓冲区 (CCMRAM，派生实例静态分配) */
    uint8_t *pixel_map;
    uint8_t *hub75_buff;

    /* 运行时 */
    const char *module_code; /* 模组编码字符串，由派生模组绑定 */
    volatile uint8_t light_level;
    volatile bool dirty;

    /* ④a 脏矩形（2026-09-08）：dev_display_commit_frame_rect 提交的矩形。
     * scan_task 消费 frame_ready 时写入本组字段，ops->prepare 读取（签名不变），
     * prepare 返回后由 scan_task 复位为 invalid。valid=false = 全量重排。 */
    uint16_t dirty_rect_x, dirty_rect_y, dirty_rect_w, dirty_rect_h;
    bool dirty_rect_valid;
};

/* ---- 通用 API ---- */

/** @brief 硬件初始化 (hw_dev_initcall): HUB75 引脚 + DBG 冻结 */
void dev_display_init(void);

/** @brief 软件初始化 (sw_dev_initcall): 创建 scan_task + 启动 TIM3/4 */
void dev_display_start(void);

/** @brief 设置单个像素颜色，置脏标记
 *
 *  坐标约定: x 为水平方向 (0..screen_rows-1), y 为垂直方向 (0..screen_cols-1)。
 *  注意: screen_rows 是每行像素数(宽度), screen_cols 是每列像素数(高度)。
 *  pixel_map 按行主序存储: pixel_map[y * screen_rows + x] */
void dev_display_set_pixel(dev_display_t *dev, uint16_t x, uint16_t y, display_color_t color);

/** @brief 矩形区域填充纯色: (x,y)起点, w宽h高, 置脏标记
 *
 *  坐标约定同 dev_display_set_pixel。
 *  越界语义：起点在屏幕外（x>=rows 或 y>=cols）→ 整区域丢弃不绘制；
 *  部分超出 → 截断到屏幕边界。 */
void dev_display_fill(dev_display_t *dev, uint16_t x, uint16_t y, uint16_t w, uint16_t h, display_color_t color);

/** @brief 叠加绘制位图: (x,y)起点, w宽h高, bitmap每行( (w+7)/8 )字节, bit=1写color, bit=0不改变原像素。
 *  如需不透明绘制(bit=0置黑), 调用方先 dev_display_fill 填充背景色
 *
 *  越界语义（2026-09-14 修订，与 fill 的截断语义对齐）：
 *  - 起点在屏幕外（x>=screen_rows 或 y>=screen_cols）→ 整区域丢弃；
 *  - 部分越出屏幕右/下边界 → **按屏幕交集逐像素裁剪绘制（只画可见部分）**，
 *    不再整块丢弃（字号大于可用高度时字形露出可见部分；位图为 MSB-first 行打包，
 *    起点坐标无符号 → 裁剪只发生在右/下边界，位序不会错位）；
 *  - 完全在屏内 → 与旧实现逐像素等价（快路径，无额外开销）；
 *  - 空矩形（w=0 或 h=0）→ 不落笔。
 *
 *  坐标约定同 dev_display_set_pixel */
void dev_display_draw_bitmap(dev_display_t *dev,
                             uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             const uint8_t *bitmap, display_color_t color);

/** @brief 获取 1-969 模组显示实例 */
dev_display_t *dev_display_1_969_get(void);

/** @brief 获取 1-260 模组显示实例 */
dev_display_t *dev_display_1_260_get(void);

/** @brief 获取 1-577 模组显示实例 */
dev_display_t *dev_display_1_577_get(void);

/** @brief 获取 1-263 模组显示实例（P6 32x32，1/8 扫，2 通道） */
dev_display_t *dev_display_1_263_get(void);

/** @brief 获取 22-1703 模组显示实例（P10 32x16，1/4 扫，2 通道/模块；料号 2200001703） */
dev_display_t *dev_display_22_1703_get(void);

/** @brief 获取 22-1665 模组显示实例（16x16 红绿双色，静态单扫，MBI5034B 多链；料号 2200001665） */
dev_display_t *dev_display_22_1665_get(void);

/** @brief 注册活动显示实例（由显示模组的 hw_dev_initcall 调用） */
void dev_display_register(dev_display_t *dev);

/** @brief 获取当前活动显示实例 */
dev_display_t *dev_display_get(void);

extern volatile uint32_t g_dev_display_commit_count;
extern volatile uint32_t g_dev_display_scan_count;

/** @brief 将当前逻辑帧提交到扫描帧，供 scan_task 读取（全屏语义） */
void dev_display_commit_frame(dev_display_t *dev);

/** @brief 提交帧并携带脏矩形（④a，2026-09-08）：只重排矩形覆盖区域，
 *  消除滚动/行渲染场景的全屏 prepare 亮度抖。
 *
 *  与 commit_frame 同机制（osKernelLock 下记指针+标志），另记录矩形；
 *  scan_task 消费时经 dev 字段传给 ops->prepare——prepare 签名不变，模组可
 *  按行子集优化，不支持/矩形无效时全量重排（保守回退，正确性不变）。
 *  未消费前多次提交自动合并为包围盒（stop_all 连续多槽提交不丢中间清行）。
 *  矩形钳位到屏幕；空矩形（w/h=0 或整体越界）不提交帧。 */
void dev_display_commit_frame_rect(dev_display_t *dev, uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/** 硬件亮度上限：0=关闭，8=OE 100% 常亮（PWM 8 档，最亮） */
#define DEV_DISPLAY_BRIGHTNESS_MAX 8U

/** @brief 设置亮度 (0=最暗/关闭, 8=最亮)，PWM 粒度 1/8 */
void dev_display_set_brightness(dev_display_t *dev, uint8_t level);
