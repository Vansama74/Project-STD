#!/usr/bin/env bash
# 单文件语法检查（与 Makefile 同口径 flags；2026-09-29 第二轮补回 SEGGER include 路径）
# 用法: cc_check.sh <file.c> [...]
set -u
cd "$(dirname "$0")/../.."
CFLAGS="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
  -DUSE_HAL_DRIVER -DSTM32F407xx -DSTD_ALL_PROTO -I Application/Inc -I Application/Inc/IAP
  -I Application/Inc/LDI -I Application/Inc/Config -I Application/Inc/ProtocolParser_QingHai
  -I Application/Inc/ProtocolParser_SiChuang_ETC -I Application/Inc/ProtocolParser_SiChuang_MTC
  -I Application/Inc/ProtocolParser_SiChuang_Overload -I Application/Inc/ProtocolParser_GuiZhou
  -I Application/Inc/ProtocolParser_GuiZhou_Overload -I Application/Inc/ProtocolParser_YunNan
  -I Application/Inc/ProtocolParser_YunNan_Overload -I Application/Inc/ProtocolParser_ShanDong
  -I Application/Inc/ProtocolParser_ChongQing -I Application/Inc/ProtocolParser_Anhui
  -I Application/Inc/ProtocolParser_Myanmar -I Application/Inc/AH_MQTT -I Application/Inc/RLS
  -I Application/Inc/Channel -I Device/Inc -I Platform/Inc -I Kernel/Inc -I Core/Inc
  -I Drivers/CMSIS/Include -I Drivers/CMSIS/Device/ST/STM32F4xx/Include
  -I Drivers/STM32F4xx_HAL_Driver/Inc -I Middlewares/Third_Party/SEGGER_RTT -I Middlewares/Third_Party/cJSON
  -I Middlewares/Third_Party/LwIP/src/include -I Middlewares/Third_Party/LwIP/system
  -I Middlewares/Third_Party/FreeRTOS/Source/include
  -I Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
  -I Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I Compiler
  -std=gnu23 -Og -g -Wall -Wextra -Wno-pragma-once-outside-header -ffunction-sections -fdata-sections -fno-common
  -fno-exceptions -fshort-enums"
rc=0
for f in "$@"; do
  out=$(arm-none-eabi-gcc $CFLAGS -fsyntax-only "$f" 2>&1)
  if [ $? -ne 0 ] || [ -n "$(printf '%s' "$out" | grep 'warning:\|error:')" ]; then
    echo "== $f"
    printf '%s\n' "$out" | grep -v "^$" | head -20
    [ -n "$out" ] && rc=1
  else
    echo "ok  $f"
  fi
done
exit $rc
