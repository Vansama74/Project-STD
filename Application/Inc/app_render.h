/**
 * @file    app_render.h
 * @brief   文字/图形渲染 — tagged union 统一入参 + 多字库芯片切换
 *
 * 上电时通过 DEV_KEY_DIP2 拨码开关自动选择字库芯片配置：
 *   - DIP2 逻辑 0（开关 OFF，GPIO 高）→ W25Q64:   非连续布局, 16/24/32pt, GB2312 94列序
 *   - DIP2 逻辑 1（开关 ON，GPIO 低）→ MX25L256: 连续布局, 14/16/20/24/32pt, GBK 190列序
 *
 * 运行中也可通过 app_render_chip_select() 手动切换。
 * 每种芯片对应一个 font_chip_config_t 实例，包含地址查找表和自适应字号候选列表。
 * 新增字号/字型只需在对应配置的 region 表中追加条目。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "dev_display.h"
#include "dev_storage.h"

/* ---- 字号（像素高度，ASCII 半宽 = size/2）---- */
typedef enum {
    FONT_SELF_ADAPT = 0,
    FONT_14         = 14,
    FONT_16         = 16,
    FONT_20         = 20,
    FONT_24         = 24,
    FONT_32         = 32,
} font_size_t;

/* ---- 字型 ---- */
typedef enum {
    FONT_ST = 0, /* 宋体 */
    FONT_FS = 1, /* 仿宋 */
    FONT_KT = 2, /* 楷体 */
    FONT_HT = 3, /* 黑体 */
} font_type_t;

/* ---- 编码类型 ---- */
typedef enum {
    FONT_ENC_ASCII = 0, /* 字库编码 — ASCII 单字节 */
    FONT_ENC_GBK   = 1, /* 字库编码 — GBK  双字节 */
    FONT_ENC_UTF8  = 2, /* 输入文本编码 — 内部自动转 GBK */
} font_enc_t;

/* ---- 字库三元组：内部检索 key，调用方无需接触 ---- */
typedef struct {
    font_size_t size;   /* 字号 */
    font_enc_t charset; /* 字库编码 (ASCII/GBK) — 渲染器内部按字符自动填充 */
    font_type_t type;   /* 字型 */
} font_key_t;

/* ---- 水平/垂直对齐 ---- */
typedef enum {
    ALIGN_LEFT_UP    = 0, /* 左对齐 / 上对齐 */
    ALIGN_CENTER     = 1, /* 居中 */
    ALIGN_RIGHT_DOWN = 2, /* 右对齐 / 下对齐 */
} align_t;

/* ---- 渲染风格（文字专属）---- */
typedef struct {
    align_t h_align; /* 水平对齐 */
    align_t v_align; /* 垂直对齐 */
    bool word_wrap;  /* 超宽时自动换行 */
} render_style_t;

/* ---- 渲染类型：告诉 app_render 如何解析 union ---- */
typedef enum {
    RENDER_TEXT   = 0, /* 文字渲染 — 使用 text/len/font_key/style/text_enc */
    RENDER_BITMAP = 1, /* 位图渲染 — 使用 bitmap/w/h           */
    RENDER_FILL   = 2, /* 矩形填充 — 使用公共字段 x/y/w/h/color (w/h=0 全屏) */
} render_type_t;

/* ---- 统一渲染参数 — tagged union — type 决定哪个 union 分支生效 ---- */
typedef struct {
    /* 公共 — 调用方设置后渲染器只读 */
    const uint16_t x, y;         /* 目标起点 */
    const uint16_t w, h;         /* 目标宽高 (fill 时 w/h=0 表示全屏) */
    const display_color_t color; /* 绘制颜色 */
    const render_type_t type;    /* 标签: 指定使用哪个 union 分支 */

    union {
        /* RENDER_TEXT — 文字专属 */
        struct {
            const char *text;            /* 字符串 */
            const uint16_t len;          /* 字符串长度（字节数） */
            const font_size_t font_size; /* 字号 */
            const font_type_t font_type; /* 字型 */
            const render_style_t *style; /* 对齐/换行 (NULL=默认) */
            const font_enc_t text_enc;   /* 输入文本编码 (UTF8需转换/GBK直通) */
        };

        /* RENDER_BITMAP — 位图专属 */
        const uint8_t *const bitmap; /* 位图数据, 每行 (w+7)/8 字节, MSB first */

        /* RENDER_FILL — 无专属字段, 只用公共的 x/y/w/h/color */
    };
} render_cfg_t;

/* ---- 字库芯片配置 ---- */

/** @brief 字库芯片配置 — 每种芯片一个实例 */
typedef struct {
    const char *name;                    /* 配置名称 */
    const void *regions;                 /* flash_region_t 数组指针 */
    uint8_t region_count;                /* 数组元素个数 */
    const font_size_t *adaptive_sizes;   /* 自适应字号候选列表（从大到小） */
    uint8_t adaptive_count;              /* 候选列表长度 */
    bool ascii_raw_code;                 /* true=ASCII 用原始字符码（W25Q64）; false=减 0x20（MX25L256） */
    bool gbk_index_190;                  /* true=GBK 用 190 列序（MX25L256）; false=94 列序（W25Q64） */
} font_chip_config_t;

/** @brief 芯片配置ID */
typedef enum {
    FONT_CHIP_W25Q64  = 0,  /* W25Q64: 非连续布局, 16/24/32, ASCII 用原始字符码 */
    FONT_CHIP_MX25L256 = 1, /* MX25L256: 连续布局, 14/16/20/24/32, ASCII 用 ch-0x20 */
    FONT_CHIP_CNT,
} font_chip_id_t;

/* ---- API（模块自注册 sw_app_initcall，调用方无需传 display/font 句柄）---- */

/** @brief 统一渲染入口 — 根据 cfg->type 分派到内部实现 */
void app_render(const render_cfg_t *cfg);

/** @brief 切换字库芯片配置 — 可在任意时刻调用，立即生效
 *  @param id 芯片配置ID (FONT_CHIP_W25Q64 / FONT_CHIP_MX25L256)
 *  @return true=切换成功, false=无效ID */
bool app_render_chip_select(font_chip_id_t id);

/** @brief 获取当前激活的字库芯片配置
 *  @return 当前配置ID */
font_chip_id_t app_render_chip_current(void);

/* ---- 字库只读诊断（现场自证用，见 app_diag.h；不改任何渲染行为）---- */

/** @brief 当前激活字库芯片配置的关键参数快照 */
typedef struct {
    const char *name;        /* 配置名（"W25Q64" / "MX25L256"） */
    uint8_t region_count;    /* 区块表条目数 */
    uint8_t adaptive_count;  /* 自适应字号候选数 */
    bool ascii_raw_code;     /* ASCII 索引用原始字符码（W25Q64=true） */
    bool gbk_index_190;      /* GBK 用 190 列序（MX25L256=true） */
    uint32_t flash_capacity; /* 字库存储容量（字节） */
} app_render_chip_info_t;

/** @brief 读当前激活配置参数（只读）。@return true=成功填充 */
bool app_render_chip_info_get(app_render_chip_info_t *out);

/** @brief 单字模实读结果（一次 dev_storage_read 的证据快照） */
typedef struct {
    uint32_t addr;      /* Flash 绝对地址（按当前激活配置算得） */
    uint32_t capacity;  /* 字库存储容量（字节） */
    bool addr_in_range; /* addr + 字形字节数 ≤ 容量（越界 → 必读取失败） */
    int32_t ret;        /* dev_storage_read 返回码（<0 = 读取失败） */
    uint16_t total;     /* 字形打包字节数 */
    uint16_t nonzero;   /* 读回数据非零字节数（0=全零；=total=全非零，均可疑） */
    uint8_t head[8];    /* 读回数据前 8 字节 */
} app_render_glyph_probe_t;

/** @brief 按**当前激活配置**实读单字模（ASCII 1 字节 / GBK 2 字节）
 *  @return true = 已填充（ret<0 表示读取失败，head/nonzero 为 0）；false = 参数非法 */
bool app_render_glyph_probe(font_size_t size, font_type_t type, font_enc_t charset,
                            const uint8_t *ch, app_render_glyph_probe_t *out);

/** @brief 按**指定芯片配置**纯算术计算单字模地址（不读取、不切换全局配置）
 *  @return Flash 绝对地址；参数非法返回 0 */
uint32_t app_render_glyph_addr_for(font_chip_id_t chip, font_size_t size, font_type_t type,
                                   font_enc_t charset, const uint8_t *ch);

/* ---- 单字模裁剪绘制（动态滚动等逐像素渲染场景，app_scroll 消费） ---- */

/** @brief 查询指定字号/编码下单字形像素宽度
 *  @param  size    字号。
 *  @param  charset 字库编码（FONT_ENC_ASCII / FONT_ENC_GBK）。
 *  @return 字形像素宽（ASCII=size/2，GBK=size）。 */
uint8_t app_render_glyph_width_px(font_size_t size, font_enc_t charset);

/** @brief 单字模绘制（带逐像素裁剪）
 *
 *  从当前激活字库芯片读取单个字模位图，逐像素写入 pixel_map；
 *  每像素同时受「屏幕边界」与「裁剪矩形 (clip_x,clip_y,clip_w,clip_h)」双重限制，
 *  字形部分越界时平滑裁剪。滚动场景需要**逐像素裁剪矩形**（字形可整体位于矩形外
 *  往返滑动），故本函数独立于 dev_display_draw_bitmap 的矩形裁剪语义实现
 *  （后者 2026-09-14 起亦支持「按屏幕交集裁剪」，但无裁剪矩形参数、不处理负坐标）。
 *
 * @param  x,y       字形绘制起点（int16 允许屏幕外/负值，逐像素判界）。
 * @param  clip_x,clip_y,clip_w,clip_h  裁剪矩形（屏幕坐标）。
 * @param  font_size 字号；font_type 字型；charset 字库编码（UTF8 无效）。
 * @param  ch        字符指针（ASCII 1 字节 / GBK 2 字节）。
 * @param  color     绘制颜色。
 * @note   不提交帧；调用方在整帧渲染完成后自行 dev_display_commit_frame。
 *         字模经 dev_storage_read 读取（W25Qxx SPI 互斥锁保护）。 */
void app_render_draw_glyph_clipped(int16_t x, int16_t y,
                                   uint16_t clip_x, uint16_t clip_y,
                                   uint16_t clip_w, uint16_t clip_h,
                                   font_size_t font_size, font_type_t font_type,
                                   font_enc_t charset, const uint8_t *ch,
                                   display_color_t color);

/* ---- 持久化显示 ---- */

#define RENDER_PERSIST_MAGIC (0x0d000721U)

typedef struct [[gnu::packed]] {
    uint32_t magic;
    uint16_t screen_rows;
    uint16_t screen_cols;
    uint8_t  color;         /* 非黑像素颜色 (display_color_t) */
    uint32_t crc32;         /* bitmap 数据的 CRC32 */
    uint8_t  bitmap[];      /* ((rows*cols+7)/8) 字节, MSB first per row */
} render_persist_t;

/** @brief 将当前显存写入存储设备持久化扇区 */
void app_render_save(void);

/** @brief 从存储设备加载持久化数据恢复显存
 *  @return true=成功恢复并置脏标记, false=无有效数据/尺寸不匹配/CRC错误 */
bool app_render_restore(void);
