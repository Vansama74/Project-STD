# Project_STD Makefile
# 参照 .eide/eide.yml Debug 目标配置生成

# ---- Default Goal ----
# 显式指定默认目标：Makefile 中先于 all 定义的规则（如口径指纹 stamp）若成为
# 文件中第一个普通目标，会劫持无参数 `make` 的默认目标
.DEFAULT_GOAL := all

# ---- Toolchain ----
TOOLCHAIN ?= gcc

ifeq ($(TOOLCHAIN),gcc)
  CC       = arm-none-eabi-gcc
  LD       = arm-none-eabi-gcc
  TC_FLAGS =
  SPECS    = --specs=nano.specs --specs=nosys.specs
else
  CC       = clang
  LD       = arm-none-eabi-gcc
  TC_FLAGS = --target=arm-none-eabi --sysroot=/usr/arm-none-eabi
  SPECS    = --specs=nano.specs --specs=nosys.specs
endif

OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

# ---- Directories ----
CONFIG    ?= Debug
BUILD_DIR  = build/$(CONFIG)

# ---- MCU Flags ----
CPU       = -mcpu=cortex-m4
FPU       = -mfpu=fpv4-sp-d16
FLOAT-ABI = -mfloat-abi=hard
MCU_FLAGS = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

# ---- Protocol selection ----
# PROTO=ALL：全协议共存开发构建（默认，-DSTD_ALL_PROTO 豁免 '{' 帧族互斥守卫）
# PROTO=CQ ：重庆量产口径——编入 CQ + cJSON，剔除 LDI 目录文件（网络配置应用由中立
#            模块 app_net_boot 承担，CQ 构建不启动 TCP Server/Client 通道），
#            追加 -DPROTO_CHONGQING（'{' 帧族仍编入，保持 -DSTD_ALL_PROTO 豁免守卫）
PROTO ?= ALL

# ---- Common Flags ----
# -DSTD_ALL_PROTO：全协议共存开发构建——'{' 帧族互斥守卫 g_brace_proto_guard 失效；
# EIDE 量产构建不定义该宏，多 '{' 族协议编入即链接报 multiple definition 强制互斥。
DEFINES = -DUSE_HAL_DRIVER -DSTM32F407xx -DSTD_ALL_PROTO
ifeq ($(PROTO),CQ)
  DEFINES += -DPROTO_CHONGQING
endif

# 功能开关显式覆盖（命令行传入即追加）：例如 make PROTO=CQ CQ_FAULT_SCREEN=0
# 关闭 CQ 心跳故障屏。勿直接覆盖 DEFINES——命令行 DEFINES 会整体替换 makefile
# 内赋值（含 += 不追加），丢掉平台宏并破坏「排除 LDI ↔ PROTO_CHONGQING」成对。
ifdef CQ_FAULT_SCREEN
  DEFINES += -DCQ_FAULT_SCREEN=$(CQ_FAULT_SCREEN)
endif

# ---- 诊断开关（APP_DIAG_BANNER）----
# 自证版诊断固件（2026-09-14）：RTT 开机横幅 + 延迟体检 + GZ_OL 0x20/0x40 逐帧证据。
# 默认开（源码 Application/Inc/app_diag.h 内 APP_DIAG_BANNER=1）；
# 关闭：`make APP_DIAG=0`（或把 app_diag.h 里的 1 改成 0）。
# 关闭 = 无任何 RTT 诊断输出，固件行为与开启时逐字节一致（诊断全部只读）。
ifdef APP_DIAG
  DEFINES += -DAPP_DIAG_BANNER=$(APP_DIAG)
endif

# ---- LwIP 调试档位（2026-09-18 YN_OL TCP 现场取证）----
# 默认 0x01（= LWIP_DBG_LEVEL_WARNING）：过滤 LwIP 的 LEVEL_ALL 常规信息行
# （tcp_slowtmr/tcp_recved 等，实测 5~17 行/秒），避免 RTT 1KB 上行缓冲被噪声
# 灌满后**所有现场诊断静默丢失**（详见 Platform/Inc/lwipopts.h 覆盖说明）。
# 诊断 LwIP 本身时可 `make APP_LWIP_DBG_LEVEL=0x00` 恢复全量日志（该值进口径
# 指纹 stamp → 切口径必然全量重编 + 重链接，不依赖手工删产物）。
ifdef APP_LWIP_DBG_LEVEL
  DEFINES += -DAPP_LWIP_DBG_LEVEL=$(APP_LWIP_DBG_LEVEL)
endif

# ---- 诊断构建指纹（自证「板上固件 = 哪棵树 + 哪套口径」）----
# 注入方式：仅对 app_boot.o 追加宏（不进 DEFINES / stamp）——不改变增量构建语义；
# 口径切换（PROTO/DISP/CONFIG/TOOLCHAIN）经 stamp 机制本就全量重编，注入值必然同步刷新。
# 树哈希变化（任何源文件内容变化）本身就会触发对应 .o 重编 + 重链接，指纹不滞后。
DIAG_TREE_HASH := $(shell { find Application Device Kernel Platform Core Compiler -type f \( -name '*.c' -o -name '*.h' -o -name '*.ld' \) -print0 | LC_ALL=C sort -z | xargs -0 cat; cat Makefile; } 2>/dev/null | md5sum | cut -c1-8)
ifeq ($(strip $(DIAG_TREE_HASH)),)
  DIAG_TREE_HASH := unknown
endif
DIAG_DEFS = -DAPP_DIAG_TREE_HASH="\"$(DIAG_TREE_HASH)\"" \
            -DAPP_DIAG_PROTO="\"$(PROTO)\"" \
            -DAPP_DIAG_DISP="\"$(DISP)\"" \
            -DAPP_DIAG_CONFIG="\"$(CONFIG)\"" \
            -DAPP_DIAG_TOOLCHAIN="\"$(TOOLCHAIN)\""
$(BUILD_DIR)/Application/Src/app_boot.o: CFLAGS += $(DIAG_DEFS)

# ---- Display module selection ----
# DISP=1_263（默认，P6 32x32 单模块口径 224x64，现行调试/量产口径）
# DISP=22_1703（P10 32x16，料号 2200001703，1/4 扫描，224x64）
# DISP=22_1665（16x16 红绿双色，料号 2200001665，**静态扫描**，单链 MBI5034B 512 位）
# 各模组显存（1-263 29568B / 22_1703 31648B / 22_1665 2832B）+ CQ 6377B 合计超 64KB
# CCMRAM 或双实例注册语义冲突 → **编译期三选一，不可同编**。
# EIDE 侧用 excludeList 实现同一语义（见 doc/01_显示系统/*模组驱动*.md）。
DISP ?= 1_263
ifneq ($(filter $(DISP),1_263 22_1703 22_1665),$(DISP))
  $(error DISP must be 1_263, 22_1703 or 22_1665, got '$(DISP)')
endif
ifeq ($(DISP),22_1703)
  SRC_DISPLAY = Device/Display/dev_display_22_1703.c
else ifeq ($(DISP),22_1665)
  SRC_DISPLAY = Device/Display/dev_display_22_1665.c
else
  SRC_DISPLAY = Device/Display/dev_display_1_263.c
endif

INC_DIRS = \
	-I Application/Inc \
	-I Application/Inc/IAP \
	-I Application/Inc/LDI \
	-I Application/Inc/Config \
	-I Application/Inc/ProtocolParser_QingHai \
	-I Application/Inc/ProtocolParser_SiChuang_ETC \
	-I Application/Inc/ProtocolParser_SiChuang_MTC \
	-I Application/Inc/ProtocolParser_SiChuang_Overload \
	-I Application/Inc/ProtocolParser_GuiZhou \
	-I Application/Inc/ProtocolParser_GuiZhou_Overload \
	-I Application/Inc/ProtocolParser_YunNan \
	-I Application/Inc/ProtocolParser_YunNan_Overload \
	-I Application/Inc/ProtocolParser_ShanDong \
	-I Application/Inc/ProtocolParser_ChongQing \
	-I Application/Inc/ProtocolParser_Anhui \
	-I Application/Inc/AH_MQTT \
	-I Application/Inc/RLS \
	-I Application/Inc/Channel \
	-I Device/Inc \
	-I Platform/Inc \
	-I Kernel/Inc \
	-I Core/Inc \
	-I Drivers/CMSIS/Include \
	-I Drivers/CMSIS/Device/ST/STM32F4xx/Include \
	-I Drivers/STM32F4xx_HAL_Driver/Inc \
	-I Middlewares/Third_Party/SEGGER_RTT \
	-I Middlewares/Third_Party/cJSON \
	-I Middlewares/Third_Party/LwIP/src/include \
	-I Middlewares/Third_Party/LwIP/system \
	-I Middlewares/Third_Party/FreeRTOS/Source/include \
	-I Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 \
	-I Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F \
	-I Compiler

# ---- C Flags ----
CFLAGS  = $(MCU_FLAGS) $(DEFINES) $(INC_DIRS) $(TC_FLAGS)
CFLAGS += -std=gnu23
CFLAGS += -Og -g
CFLAGS += -Wall -Wextra
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -fno-common
CFLAGS += -fno-exceptions
CFLAGS += -fshort-enums
# 头文件依赖自动生成（.d 与 .o 同目录，-MP 为头文件补空规则防删除报错）：
# 头文件改动由 .d 记录触发重编译，不依赖手工全量重建
CFLAGS += -MMD -MP

# ---- LDFLAGS ----
LDSCRIPT  = Compiler/STM32F407XX_FLASH.ld
LDFLAGS  = $(MCU_FLAGS)
LDFLAGS += -T $(LDSCRIPT)
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/Project_STD.map,--cref
LDFLAGS += -Wl,--gc-sections
LDFLAGS += $(SPECS)
LDFLAGS += -u _printf_float
LDFLAGS += -lm

# ---- Source Files ----
# Core/Application
SRC_CORE = \
	Core/Src/main.c \
	Core/Src/stm32f4xx_it.c \
	Core/Src/syscalls.c \
	Core/Src/sysmem.c \
	Core/Src/adc.c \
	Core/Src/dma.c \
	Core/Src/gpio.c \
	Core/Src/iwdg.c \
	Core/Src/rtc.c \
	Core/Src/spi.c \
	Core/Src/stm32f4xx_hal_msp.c \
	Core/Src/stm32f4xx_hal_timebase_tim.c \
	Core/Src/system_stm32f4xx.c \
	Core/Src/tim.c \
	Core/Src/usart.c \
	Core/Src/crc.c \

# LWIP
# STM32 HAL Driver
SRC_HAL = \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc_ex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_eth.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ramfunc.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_iwdg.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rtc.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rtc_ex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c \
	Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_crc.c

# BSP (旧协议层依赖 display/render/text_cvt，等协议迁移完成后移除)

# display/render 已迁移至 dev_display + app_render; text_cvt 已移至 Kernel

# BSP Protocol

# RTT
SRC_RTT = \
	Middlewares/Third_Party/SEGGER_RTT/SEGGER_RTT.c \
	Middlewares/Third_Party/SEGGER_RTT/SEGGER_RTT_printf.c \
	Middlewares/Third_Party/SEGGER_RTT/SEGGER_RTT_Syscalls_GCC.c

# LwIP Middleware
# LwIP Middleware
SRC_LWIP = \
	Middlewares/Third_Party/LwIP/system/OS/sys_arch.c \
	Middlewares/Third_Party/LwIP/src/api/api_lib.c \
	Middlewares/Third_Party/LwIP/src/api/api_msg.c \
	Middlewares/Third_Party/LwIP/src/api/err.c \
	Middlewares/Third_Party/LwIP/src/api/if_api.c \
	Middlewares/Third_Party/LwIP/src/api/netbuf.c \
	Middlewares/Third_Party/LwIP/src/api/netdb.c \
	Middlewares/Third_Party/LwIP/src/api/netifapi.c \
	Middlewares/Third_Party/LwIP/src/api/sockets.c \
	Middlewares/Third_Party/LwIP/src/api/tcpip.c \
	Middlewares/Third_Party/LwIP/src/core/altcp.c \
	Middlewares/Third_Party/LwIP/src/core/altcp_alloc.c \
	Middlewares/Third_Party/LwIP/src/core/altcp_tcp.c \
	Middlewares/Third_Party/LwIP/src/core/def.c \
	Middlewares/Third_Party/LwIP/src/core/dns.c \
	Middlewares/Third_Party/LwIP/src/core/inet_chksum.c \
	Middlewares/Third_Party/LwIP/src/core/init.c \
	Middlewares/Third_Party/LwIP/src/core/ip.c \
	Middlewares/Third_Party/LwIP/src/core/mem.c \
	Middlewares/Third_Party/LwIP/src/core/memp.c \
	Middlewares/Third_Party/LwIP/src/core/netif.c \
	Middlewares/Third_Party/LwIP/src/core/pbuf.c \
	Middlewares/Third_Party/LwIP/src/core/raw.c \
	Middlewares/Third_Party/LwIP/src/core/stats.c \
	Middlewares/Third_Party/LwIP/src/core/sys.c \
	Middlewares/Third_Party/LwIP/src/core/tcp.c \
	Middlewares/Third_Party/LwIP/src/core/tcp_in.c \
	Middlewares/Third_Party/LwIP/src/core/tcp_out.c \
	Middlewares/Third_Party/LwIP/src/core/timeouts.c \
	Middlewares/Third_Party/LwIP/src/core/udp.c \
	Middlewares/Third_Party/LwIP/src/core/ipv4/autoip.c \
	Middlewares/Third_Party/LwIP/src/core/ipv4/dhcp.c \
	Middlewares/Third_Party/LwIP/src/core/ipv4/etharp.c \
	Middlewares/Third_Party/LwIP/src/core/ipv4/icmp.c \
	Middlewares/Third_Party/LwIP/src/core/ipv4/igmp.c \
	Middlewares/Third_Party/LwIP/src/core/ipv4/ip4.c \
	Middlewares/Third_Party/LwIP/src/core/ipv4/ip4_addr.c \
	Middlewares/Third_Party/LwIP/src/core/ipv4/ip4_frag.c \
	Middlewares/Third_Party/LwIP/src/core/ipv6/dhcp6.c \
	Middlewares/Third_Party/LwIP/src/core/ipv6/ethip6.c \
	Middlewares/Third_Party/LwIP/src/core/ipv6/icmp6.c \
	Middlewares/Third_Party/LwIP/src/core/ipv6/inet6.c \
	Middlewares/Third_Party/LwIP/src/core/ipv6/ip6.c \
	Middlewares/Third_Party/LwIP/src/core/ipv6/ip6_addr.c \
	Middlewares/Third_Party/LwIP/src/core/ipv6/ip6_frag.c \
	Middlewares/Third_Party/LwIP/src/core/ipv6/mld6.c \
	Middlewares/Third_Party/LwIP/src/core/ipv6/nd6.c \
	Middlewares/Third_Party/LwIP/src/apps/mqtt/mqtt.c \
	Middlewares/Third_Party/LwIP/src/netif/bridgeif.c \
	Middlewares/Third_Party/LwIP/src/netif/bridgeif_fdb.c \
	Middlewares/Third_Party/LwIP/src/netif/ethernet.c \
	Middlewares/Third_Party/LwIP/src/netif/lowpan6.c \
	Middlewares/Third_Party/LwIP/src/netif/lowpan6_ble.c \
	Middlewares/Third_Party/LwIP/src/netif/lowpan6_common.c \
	Middlewares/Third_Party/LwIP/src/netif/slipif.c \
	Middlewares/Third_Party/LwIP/src/netif/zepif.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/auth.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/ccp.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/chap_ms.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/chap-md5.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/chap-new.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/demand.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/eap.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/ecp.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/eui64.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/fsm.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/ipcp.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/ipv6cp.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/lcp.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/magic.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/mppe.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/multilink.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/ppp.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/pppapi.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/pppcrypt.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/pppoe.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/pppol2tp.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/pppos.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/upap.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/utils.c \
	Middlewares/Third_Party/LwIP/src/netif/ppp/vj.c


# FreeRTOS
SRC_FREERTOS = \
	Middlewares/Third_Party/FreeRTOS/Source/croutine.c \
	Middlewares/Third_Party/FreeRTOS/Source/event_groups.c \
	Middlewares/Third_Party/FreeRTOS/Source/list.c \
	Middlewares/Third_Party/FreeRTOS/Source/queue.c \
	Middlewares/Third_Party/FreeRTOS/Source/stream_buffer.c \
	Middlewares/Third_Party/FreeRTOS/Source/tasks.c \
	Middlewares/Third_Party/FreeRTOS/Source/timers.c \
	Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os2.c \
	Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c \
	Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c

# Startup
SRC_STARTUP = \
	Compiler/startup.c

# Kernel
SRC_KERNEL = \
	Kernel/Src/initcall.c \
	Kernel/Src/ring_buffer.c \
	Kernel/Src/bit_utils.c \
	Kernel/Src/crc_utils.c \
	Kernel/Src/text_cvt.c \
	Kernel/Src/bcc_utils.c

# Platform（仅含无冲突的文件，其他在 Phase 3 逐步加入）
SRC_PLATFORM = \
	Platform/Src/pl_gpio.c \
	Platform/Src/pl_rtt.c \
	Platform/Src/pl_exti.c \
	Platform/Src/pl_net.c \
	Platform/Src/pl_eth.c \
	Platform/Src/pl_crc.c \
	Platform/Src/pl_iwdg.c \
	Platform/Src/pl_dma.c \
	Platform/Src/pl_dwt.c \
	Platform/Src/pl_tim.c \
	Platform/Src/pl_rtc.c \
	Platform/Src/pl_sys.c \
	Platform/Src/pl_flash.c \
	Platform/Src/pl_hub75.c \
	Platform/Src/pl_adc.c \
	Platform/Src/pl_spi.c \
	Platform/Src/pl_uart.c

# Device (仅 Project_STD 新模块，resend dev_* 等 Phase 6 Platform 集成后加入)
# 显示模组二选一（1-263 / 22_1703 由 DISP 选择，见上）
SRC_DEVICE = \
	Device/IO/dev_io_ctrl.c \
	Device/IO/dev_key.c \
	Device/Display/dev_display.c \
	$(SRC_DISPLAY) \
	Device/IO/dev_light_sensor.c \
	Device/Storage/dev_w25qxx.c \
	Device/Storage/dev_flash_int.c \
	Device/Network/dev_dp83848.c \
	Device/Network/dev_eth.c \
	Device/Comm/dev_rs485.c \
	Device/Comm/dev_rs232.c \
	Device/Comm/dev_rs232_voice.c

# Application — 兼容协议同时编入（EIDE 目录编入等价；互斥靠排除目录，不用宏）
SRC_APPLICATION = \
	Application/Src/app_test.c \
	Application/Src/app_factory_test.c \
	Application/Src/app_boot.c \
	Application/Src/app_net_boot.c \
	Application/Src/app_default_display.c \
	Application/Src/app_dispatch.c \
	Application/Src/app_render.c \
	Application/Src/app_scroll.c \
	Application/Src/app_key.c \
	Application/Src/app_light_sensor.c \
	Application/Src/IAP/app_iap.c \
	Application/Src/IAP/app_iap_cmd.c \
	Application/Src/Config/app_board_net_cfg.c \
	Application/Src/LDI/app_ldi.c \
	Application/Src/LDI/app_ldi_cmd.c \
	Application/Src/LDI/app_ldi_cfg.c \
	Application/Src/LDI/app_ldi_device.c \
	Application/Src/LDI/app_vms_ctrl.c \
	Application/Src/ProtocolParser_ChongQing/app_cq_proto.c \
	Application/Src/ProtocolParser_ChongQing/app_cq_proto_parse.c \
	Application/Src/ProtocolParser_ChongQing/app_cq_proto_cmd.c \
	Application/Src/AH_MQTT/ah_mqtt.c \
	Application/Src/AH_MQTT/ah_mqtt_cmd.c \
	Application/Src/RLS/app_rls.c \
	Application/Src/RLS/app_rls_cmd.c \
	Application/Src/ProtocolParser_QingHai/app_qh_proto.c \
	Application/Src/ProtocolParser_QingHai/app_qh_proto_parse.c \
	Application/Src/ProtocolParser_QingHai/app_qh_proto_cmd.c \
	Application/Src/ProtocolParser_QingHai/app_qh_proto_voice.c \
	Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto.c \
	Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto_parse.c \
	Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto_cmd.c \
	Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto.c \
	Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto_parse.c \
	Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto_cmd.c \
	Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto_voice.c \
	Application/Src/ProtocolParser_SiChuang_Overload/app_sc_ol_proto.c \
	Application/Src/ProtocolParser_SiChuang_Overload/app_sc_ol_proto_parse.c \
	Application/Src/ProtocolParser_SiChuang_Overload/app_sc_ol_proto_cmd.c \
	Application/Src/ProtocolParser_ShanDong/app_sd_proto.c \
	Application/Src/ProtocolParser_ShanDong/app_sd_proto_parse.c \
	Application/Src/ProtocolParser_ShanDong/app_sd_proto_cmd.c \
	Application/Src/ProtocolParser_ShanDong/app_sd_proto_default.c \
	Application/Src/ProtocolParser_GuiZhou/app_gz_proto.c \
	Application/Src/ProtocolParser_GuiZhou/app_gz_proto_parse.c \
	Application/Src/ProtocolParser_GuiZhou/app_gz_proto_cmd.c \
	Application/Src/ProtocolParser_GuiZhou/app_gz_proto_voice.c \
	Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c \
	Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.c \
	Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c \
	Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_default.c \
	Application/Src/ProtocolParser_YunNan/app_yn_proto.c \
	Application/Src/ProtocolParser_YunNan/app_yn_proto_parse.c \
	Application/Src/ProtocolParser_YunNan/app_yn_proto_cmd.c \
	Application/Src/ProtocolParser_YunNan/app_yn_proto_voice.c \
	Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c \
	Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_parse.c \
	Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c \
	Application/Src/ProtocolParser_Anhui/app_anhui_proto.c \
	Application/Src/ProtocolParser_Anhui/app_anhui_proto_parse.c \
	Application/Src/ProtocolParser_Anhui/app_anhui_proto_cmd.c \
	Application/Src/ProtocolParser_Anhui/app_anhui_proto_voice.c \
	Application/Src/app_uart_baud.c \
	Application/Src/Channel/app_udp.c \
	Application/Src/Channel/app_tcp_server.c \
	Application/Src/Channel/app_tcp_client.c \
	Application/Src/Channel/app_mqtt.c \
	Application/Src/Channel/app_rs232.c \
	Application/Src/Channel/app_rs485.c

# PROTO=CQ：剔除 LDI 目录文件（网络配置应用由中立模块 app_net_boot 承担）
ifeq ($(PROTO),CQ)
  SRC_APPLICATION := $(filter-out Application/Src/LDI/%,$(SRC_APPLICATION))
endif

# cJSON 第三方中间件（CQ JSON 解析）
SRC_CJSON = \
	Middlewares/Third_Party/cJSON/cJSON.c

# ---- All Sources ----
SRC_ALL = \
	$(SRC_KERNEL) \
	$(SRC_PLATFORM) \
	$(SRC_DEVICE) \
	$(SRC_APPLICATION) \
	$(SRC_CJSON) \
	$(SRC_CORE) \
	$(SRC_HAL) \
	$(SRC_RTT) \
	$(SRC_LWIP) \
	$(SRC_FREERTOS) \
	$(SRC_STARTUP)

# ---- Object Files ----
OBJ_ALL = $(addprefix $(BUILD_DIR)/,$(SRC_ALL:.c=.o))

# ---- Header Dependencies ----
# -MMD 生成的依赖文件（首次构建时不存在，-include 静默忽略）
DEP_ALL = $(OBJ_ALL:.o=.d)

# ---- Build Configuration Stamp（口径指纹） ----
# 缺陷背景（2026-09-14 修复）：elf 原本只依赖 .o 列表，而「源文件列表」（PROTO 剔除
# LDI 目录、DISP 替换模组文件）与「编译开关口径」（DEFINES/CFLAGS/LDFLAGS）不在依赖
# 图上 —— 执行 make PROTO=CQ / DISP=22_1703 时若 elf 时间戳更新，make 跳过重链接，
# **静默复用上一口径的 elf**（曾靠手工删除 build/$(CONFIG)/Project_STD.* 规避）。
# 这里把口径变量、源列表、Makefile 自身指纹写入 $(STAMP_FILE)：
#   - 内容变化（切口径/改开关/增删源文件/改 Makefile）→ 改写 stamp（mtime 更新）→
#     全部 .o 重编译 + elf 重链接；
#   - 内容不变（同一口径重复 make）→ cmp 判定相同，不触碰 stamp → 增量构建照常。
# 新增构建开关：只要它流入 DEFINES/CFLAGS/LDFLAGS/SRC 即自动纳入；否则改 Makefile
# 本身也会因 MAKEFILE_CKSUM 变化而触发重建（见 doc/构建开关总表.md §4）。
STAMP_FILE = $(BUILD_DIR)/.build_stamp

# Makefile 内容指纹（cksum：内容相同则同一值，改注释也会变——偏保守，保证不漏重建）
MAKEFILE_CKSUM := $(shell cksum Makefile 2>/dev/null | cut -d' ' -f1)

define BUILD_STAMP_TEXT
STAMP_VERSION=1
CONFIG=$(CONFIG)
TOOLCHAIN=$(TOOLCHAIN)
PROTO=$(PROTO)
DISP=$(DISP)
DEFINES=$(DEFINES)
CFLAGS=$(CFLAGS)
LDFLAGS=$(LDFLAGS)
MAKEFILE_CKSUM=$(MAKEFILE_CKSUM)
SRC_FILES=$(SRC_ALL)
endef
export BUILD_STAMP_TEXT

$(STAMP_FILE): FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' "$$BUILD_STAMP_TEXT" > $@.tmp
	@cmp -s $@.tmp $@ && rm -f $@.tmp || mv -f $@.tmp $@

# ---- Targets ----
.PHONY: all clean compile_commands FORCE

FORCE:

all: $(BUILD_DIR)/Project_STD.elf $(BUILD_DIR)/Project_STD.hex $(BUILD_DIR)/Project_STD.bin
	@echo "==== Build complete ===="
	@$(SIZE) $(BUILD_DIR)/Project_STD.elf

compile_commands: $(BUILD_DIR)/compile_commands.json
	@ln -sf $(BUILD_DIR)/compile_commands.json compile_commands.json
	@echo "==== compile_commands.json ready ===="

$(BUILD_DIR)/compile_commands.json:
	@mkdir -p $(BUILD_DIR)
	@SRC_LIST="$(SRC_ALL)" CC_BIN="$(CC)" CFLAGS_STR="$(CFLAGS)" BUILD_DIR="$(BUILD_DIR)" \
	  python3 -c 'import json,os,shlex; from pathlib import Path; root=Path.cwd(); build_dir=root/Path(os.environ["BUILD_DIR"]); cc_bin=os.environ["CC_BIN"]; cflags=shlex.split(os.environ["CFLAGS_STR"]); src_list=[s for s in os.environ["SRC_LIST"].split() if s.endswith(".c")]; entries=[]; \
for src in src_list: entries.append({"directory": str(root), "command": " ".join(shlex.quote(x) for x in [cc_bin, *cflags, "-c", "-o", str(build_dir / src.replace(".c", ".o")), src]), "file": str(root / src), "output": str(build_dir / src.replace(".c", ".o"))}); (build_dir / "compile_commands.json").write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")'

# elf 依赖 = 源文件列表生成的全部 .o + 口径指纹 stamp（切口径必然重链接）+ 链接脚本
# 链接输入必须用 $(OBJ_ALL)（不可用 $^）——保持源文件收录序；initcall 段序依赖它
$(BUILD_DIR)/Project_STD.elf: $(OBJ_ALL) $(STAMP_FILE) $(LDSCRIPT)
	@echo "Linking $@"
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJ_ALL)

$(BUILD_DIR)/Project_STD.hex: $(BUILD_DIR)/Project_STD.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/Project_STD.bin: $(BUILD_DIR)/Project_STD.elf
	$(OBJCOPY) -O binary $< $@

# ---- Compile Rule ----
# .o 同时依赖口径指纹 stamp：口径切换（DEFINES/源列表等变化）时全部重编译，
# 避免旧口径的 .o 参与新口径链接（stamp 内容不变则不触碰，增量不受影响）
$(BUILD_DIR)/%.o: %.c $(STAMP_FILE)
	@echo "Compiling $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) compile_commands.json

# 头文件依赖（-MMD 生成；首次构建缺失时静默忽略，不尝试构建）
-include $(DEP_ALL)
