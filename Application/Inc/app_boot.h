/**
 * @file    app_boot.h
 * @brief   系统启动编排器接口
 *
 * PROGRAM_CODE 为产品程序编码：开机画面（FW 行）与协议版本应答
 */

#pragma once

#define PROGRAM_CODE "9K10212482"

void app_boot(void);