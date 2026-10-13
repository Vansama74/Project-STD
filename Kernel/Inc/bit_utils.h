/**
 * @file    bit_utils.h
 * @brief   芯片无关的位操作
 */

#pragma once

#include <stdint.h>

/** @brief 计算 uint32_t 二进制尾部零的数量（替代 GCC __builtin_ctz） */
uint8_t bit_ctz(uint32_t x);

/** @brief 计算 uint64_t 二进制尾部零的数量
 *  @note  x==0 与 `bit_ctz(0)` 语义一致返回 0（调用方须先自行判非零）。 */
uint8_t bit_ctz64(uint64_t x);
