/**
 * @file    app_anhui_proto_parse.h
 * @brief   安徽费显协议帧解析接口
 */
#pragma once

#include "app_anhui_proto.h"

#include <stdbool.h>

/**
 * @brief  判断命令字是否属于安徽协议支持集。
 * @param  cmd  命令字节。
 * @return true=支持，false=非法命令。
 * @note   probe（首字节快拒后的第二道预筛）与 parse 共用，保证两侧命令集一致。
 */
bool anhui_cmd_supported(uint8_t cmd);
