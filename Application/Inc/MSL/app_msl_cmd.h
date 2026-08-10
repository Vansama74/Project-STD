#pragma once

#include "string.h"

#include "app_msl.h"
#include "app_dispatch.h"

typedef struct {
    uint8_t width;
    uint8_t high;
    uint8_t color;
    uint8_t pers;
    uint8_t bitmap[];
} msl_bitmap_t;

typedef struct {
    uint8_t x[2];
    uint8_t y[2];
    uint8_t color;
    uint8_t font_size;
    uint8_t font_type;
    uint8_t row_style;
    uint8_t col_style;
    uint8_t wrap;
    uint8_t encode;
    uint8_t pers;
    uint8_t text[];
} msl_text_t;

/**
 * MSL 命令处理函数指针类型
 * @param meta  通道元信息（来源通道类型、编号等）
 * @param data  指向帧 DATA 域首字节
 */
typedef void (*msl_cmd_handler_fn_t)(channel_t *, void *);

/** MSL 命令处理函数表，按命令码索引 */
extern const msl_cmd_handler_fn_t g_msl_cmd_table[];
