#!/usr/bin/env python3
"""续接轮（2026-10-13 第二轮）：全仓业务代码编译期断言清理。

口径（用户裁决）：业务代码不再用 `_Static_assert` / `static_assert` 做防御 —— 每条改为
「一行契约注释（含越限后果）+ doc 指针」。例外（文档记载的用户裁决保留守卫，逐条列入报告）：
  ① `Device/Display/dev_display_22_1665.c` 2 条（doc/01 §0.3）；
  ② `Application/Src/app_dispatch.c` RB 表/尺寸 4 条（用户点名）。
每个替换要求**恰好命中一次**，否则脚本报错退出（防误改）。
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

RB8485 = ("/* 契约：payload 上限须 ≤ 串口 RB 容量（越限被 RB 截断，静默收错帧），\n"
          " * 详见 doc/08_协议模块接入规则/01_通用规则与模块骨架.md §4。 */\n")
RB3 = ("/* 契约：payload 上限须 ≤ 三槽 RB 容量（越限被 RB 截断，静默收错帧），\n"
       " * 详见 doc/08_协议模块接入规则/01_通用规则与模块骨架.md §4。 */\n")

EDITS: list[tuple[str, str, str]] = [
    # ---- 协议模块：payload 上限 ≤ RB（原 _Static_assert）----
    ("Application/Src/ProtocolParser_Anhui/app_anhui_proto.c",
     '_Static_assert(ANHUI_PAYLOAD_MAX <= RB_SIZE_RS485, "ANHUI frame must fit RS485 RB");\n'
     '_Static_assert(ANHUI_PAYLOAD_MAX <= RB_SIZE_RS232, "ANHUI frame must fit RS232 RB");\n',
     RB8485),
    ("Application/Src/ProtocolParser_ChongQing/app_cq_proto.c",
     '_Static_assert(CQ_PAYLOAD_MAX <= RB_SIZE_RJ45, "CQ frame must fit RJ45 RB");\n',
     "/* 契约：payload 上限须 ≤ RJ45 RB 容量（越限被 RB 截断，静默收错帧），\n"
     " * 详见 doc/08_协议模块接入规则/01_通用规则与模块骨架.md §4。 */\n"),
    ("Application/Src/ProtocolParser_GuiZhou/app_gz_proto.c",
     '_Static_assert(GZ_PAYLOAD_MAX <= RB_SIZE_RS485, "GZ frame must fit RS485 RB");\n'
     '_Static_assert(GZ_PAYLOAD_MAX <= RB_SIZE_RS232, "GZ frame must fit RS232 RB");\n',
     RB8485),
    ("Application/Src/ProtocolParser_QingHai/app_qh_proto.c",
     '_Static_assert(QH_PAYLOAD_MAX <= RB_SIZE_RS485, "QH frame must fit RS485 RB");\n'
     '_Static_assert(QH_PAYLOAD_MAX <= RB_SIZE_RS232, "QH frame must fit RS232 RB");\n',
     RB8485),
    ("Application/Src/ProtocolParser_ShanDong/app_sd_proto.c",
     '_Static_assert(SD_PAYLOAD_MAX <= RB_SIZE_RS485, "SD frame must fit RS485 RB");\n'
     '_Static_assert(SD_PAYLOAD_MAX <= RB_SIZE_RS232, "SD frame must fit RS232 RB");\n',
     RB8485),
    ("Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto.c",
     '_Static_assert(SC_ETC_PAYLOAD_MAX <= RB_SIZE_RS485, "SC_ETC frame must fit RS485 RB");\n'
     '_Static_assert(SC_ETC_PAYLOAD_MAX <= RB_SIZE_RS232, "SC_ETC frame must fit RS232 RB");\n',
     RB8485),
    ("Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto.c",
     '_Static_assert(SC_MTC_PAYLOAD_MAX <= RB_SIZE_RS485, "SC_MTC frame must fit RS485 RB");\n'
     '_Static_assert(SC_MTC_PAYLOAD_MAX <= RB_SIZE_RS232, "SC_MTC frame must fit RS232 RB");\n',
     RB8485),
    ("Application/Src/ProtocolParser_SiChuang_Overload/app_sc_ol_proto.c",
     '_Static_assert(SC_OL_PAYLOAD_MAX <= RB_SIZE_RS485, "SC_OL frame must fit RS485 RB");\n'
     '_Static_assert(SC_OL_PAYLOAD_MAX <= RB_SIZE_RS232, "SC_OL frame must fit RS232 RB");\n',
     RB8485),
    ("Application/Src/ProtocolParser_YunNan/app_yn_proto.c",
     '_Static_assert(YN_PAYLOAD_MAX <= RB_SIZE_RS485, "YN frame must fit RS485 RB");\n'
     '_Static_assert(YN_PAYLOAD_MAX <= RB_SIZE_RS232, "YN frame must fit RS232 RB");\n',
     RB8485),
    ("Application/Src/RLS/app_rls.c",
     '_Static_assert(RLS_PAYLOAD_MAX <= RB_SIZE_RS485, "RLS max frame must fit RS485 RB");\n',
     RB8485),
    # ---- 头文件侧（GZ_OL / YN_OL / MM）----
    ("Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h",
     '_Static_assert(GZ_OL_PAYLOAD_MAX <= RB_SIZE_RS485, "GZ_OL frame must fit RS485 RB");\n'
     '_Static_assert(GZ_OL_PAYLOAD_MAX <= RB_SIZE_RS232, "GZ_OL frame must fit RS232 RB");\n'
     '_Static_assert(GZ_OL_PAYLOAD_MAX <= RB_SIZE_RJ45, "GZ_OL frame must fit RJ45 RB (UDP 10011)");\n',
     RB3),
    ("Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h",
     '_Static_assert(YN_OL_FRAME_LEN_MAX <= RB_SIZE_RS485, "YN_OL frame must fit RS485 RB");\n'
     '_Static_assert(YN_OL_FRAME_LEN_MAX <= RB_SIZE_RS232, "YN_OL frame must fit RS232 RB");\n'
     '_Static_assert(YN_OL_FRAME_LEN_MAX <= RB_SIZE_RJ45, "YN_OL frame must fit RJ45 RB (UDP 10011)");\n',
     RB3),
    ("Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h",
     "/** 0x49 屏体参数持久化记录（12B；布局由 _Static_assert 锁定） */\n",
     "/** 0x49 屏体参数持久化记录（12B，字段与 CRC 覆盖见 doc/15_云南治超屏协议/README.md §7） */\n"),
    ("Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h",
     '_Static_assert(sizeof(yn_ol_screen_cfg_record_t) == YN_OL_CFG_RECORD_SIZE,\n'
     '               "yn_ol_screen_cfg_record_t must be 12B");\n',
     "/* 契约：记录固定 12B（magic4 + version2 + font1 + size1 + crc4；CRC 覆盖前 8B），\n"
     " * 改错读/写错偏移，详见 doc/15_云南治超屏协议/README.md §7。 */\n"),
    ("Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h",
     '_Static_assert(MM_PAYLOAD_MAX <= RB_SIZE_RS232, "MM frame must fit RS232 RB");\n'
     '_Static_assert(MM_FRAME_LEN == 20U, "MM frame is fixed 20 bytes per protocol doc");\n',
     "/* 契约：帧定长 20B（协议文档固定）且 ≤ RS232 RB 容量；改错静默解析失败，\n"
     " * 详见 doc/16_缅甸2D费显协议/README.md §2。 */\n"),
    # ---- 板级配置记录布局（三固件二进制兼容）----
    ("Application/Inc/Config/app_board_net_cfg.h",
     " * 记录布局与 Bootloader / Recovery 各自的 Drivers/BSP/Config/config_info.h 二进制\n"
     " * 兼容，由下方 static_assert 锁定尺寸与偏移。写入守卫语义（空/损坏自愈、升级中间态\n",
     " * 记录布局与 Bootloader / Recovery 各自的 Drivers/BSP/Config/config_info.h 二进制\n"
     " * 兼容（布局契约见下方注释）。写入守卫语义（空/损坏自愈、升级中间态\n"),
    ("Application/Inc/Config/app_board_net_cfg.h",
     "/* ---- 布局锁定：与 Bootloader / Recovery config_info.h 二进制兼容 ---- */\n"
     'static_assert(sizeof(app_board_net_cfg_t) == 20, "net_cfg 布局 20B");\n'
     'static_assert(sizeof(app_board_fw_info_t) == 40, "fw_info 布局 40B");\n'
     'static_assert(sizeof(app_board_sys_info_t) == 72, "sys_info 布局 72B");\n'
     'static_assert(offsetof(app_board_sys_info_t, magic) == 0, "magic 偏移 0");\n'
     'static_assert(offsetof(app_board_sys_info_t, update_sta) == 4, "update_sta 偏移 4");\n'
     'static_assert(offsetof(app_board_sys_info_t, app_info) == 8, "app_info 偏移 8");\n'
     'static_assert(offsetof(app_board_sys_info_t, net_cfg) == 48, "net_cfg 偏移 48");\n'
     'static_assert(offsetof(app_board_net_cfg_t, port) == 12, "port 偏移 12");\n'
     'static_assert(offsetof(app_board_net_cfg_t, udp_port) == 16, "udp_port 偏移 16");\n'
     'static_assert(offsetof(app_board_sys_info_t, config_crc) == 68, "config_crc 偏移 68");\n',
     "/* ---- 布局契约：与 Bootloader / Recovery config_info.h 二进制兼容 ----\n"
     " * sys_info 72B = magic(0) + update_sta(4) + app_info(8, 40B) + net_cfg(48, 20B) +\n"
     " * config_crc(68)；net_cfg 内 port 偏移 12、udp_port 偏移 16（CRC 覆盖前 68B）。\n"
     " * 改错 = 三固件读同一扇区错位（Bootloader 判 App 损坏），\n"
     " * 详见 doc/07_LDI与IAP配置解耦/02_decoupling_solutions.md §15。 */\n"),
    # ---- LDI 结构体布局 ----
    ("Application/Src/LDI/app_ldi_cmd.c",
     'static_assert(sizeof(ldi_network_info_t) == 65, "LDI network block must be 65 bytes");\n'
     'static_assert(offsetof(ldi_network_info_t, ntp_ip) == 45, "NtpIP offset must be 45");\n'
     'static_assert(offsetof(ldi_network_info_t, device_ip) == 49, "DeviceIP offset must be 49");\n'
     'static_assert(offsetof(ldi_network_info_t, device_port) == 53, "DevicePort offset must be 53");\n'
     'static_assert(offsetof(ldi_network_info_t, gateway) == 55, "Gateway offset must be 55");\n'
     'static_assert(offsetof(ldi_network_info_t, netmask) == 59, "Netmask offset must be 59");\n'
     'static_assert(offsetof(ldi_network_info_t, reserve2) == 63, "D1H reserved offset must be 63");\n',
     "/* 契约：本结构固定 65B（D1H network 块，DATA 偏移 20 起）；NtpIP 45 / DeviceIP 49 /\n"
     " * DevicePort 53 / Gateway 55 / Netmask 59 / reserve2 63——padding/对齐错位 = 静默收发\n"
     " * 错帧，详见 doc/02 附件2。 */\n"),
    ("Application/Src/LDI/app_ldi_cmd.c",
     'static_assert(sizeof(ldi_init_rsp_module_t) == 10, "ldi_init_rsp_module_t fixed part must be 10 bytes");\n',
     "/* 契约：本结构固定部分 10B（custom_init[] 为可变长尾部）；详见 doc/02 附件2。 */\n"),
    ("Application/Src/LDI/app_ldi_cmd.c",
     "// ============================================================================\n"
     "// 编译期尺寸校验\n"
     "// ============================================================================\n"
     "// 确保 struct 布局与协议定义逐字节一致, 避免因 padding/对齐导致帧解析错位\n"
     'static_assert(sizeof(ldi_module_head_t) == 2, "ldi_module_head_t must be 2 byte");\n'
     "\n"
     'static_assert(sizeof(ldi_cfg_barrier_t) == 11, "ldi_cfg_barrier_t must be 11 byte");\n'
     'static_assert(sizeof(ldi_cfg_display_t) == 11, "ldi_cfg_display_t must be 11 byte");\n'
     'static_assert(sizeof(ldi_cfg_signal_t) == 2, "ldi_cfg_signal_t must be 3 byte");\n'
     'static_assert(sizeof(ldi_cfg_vms_t) == 10, "ldi_cfg_vms_t must be 11 byte");\n'
     "\n"
     'static_assert(sizeof(ldi_reboot_payload_t) == 10, "ldi_reboot_payload_t must be 10 byte");\n'
     "\n"
     'static_assert(sizeof(ldi_ctrl_barrier_t) == 2, "ldi_ctrl_barrier_t must be 2 byte");\n'
     'static_assert(sizeof(ldi_ctrl_display_t) == 5, "ldi_ctrl_display_t fixed part must be 5 byte");\n'
     'static_assert(sizeof(ldi_ctrl_signal_t) == 2, "ldi_ctrl_signal_t must be 2 byte");\n'
     'static_assert(sizeof(ldi_ctrl_alarm_t) == 4, "ldi_ctrl_alarm_t must be 4 byte");\n'
     'static_assert(sizeof(ldi_ctrl_vms_t) == 6, "ldi_ctrl_vms_t fixed part must be 6 byte");\n'
     'static_assert(sizeof(ldi_ctrl_canopy_light_t) == 2, "ldi_ctrl_canopy_light_t must be 2 byte");\n'
     'static_assert(sizeof(ldi_ctrl_fog_light_t) == 3, "ldi_ctrl_fog_light_t must be 3 byte");\n'
     'static_assert(sizeof(ldi_ctrl_voice_t) == 4, "ldi_ctrl_voice_t fixed part must be 4 byte");\n',
     "// ============================================================================\n"
     "// 布局契约：上方 payload 长度助手按 sizeof(结构体) 推算固定段，要求各配置/控制\n"
     "// 结构体与协议文档逐字节一致（padding/对齐错位 = 静默收发错帧，如 cfg.barrier 11B /\n"
     "// ctrl.display 固定 5B / ctrl.voice 固定 4B）；详见 doc/02 附件2。\n"
     "// ============================================================================\n"),
    ("Application/Inc/LDI/app_ldi_cfg.h",
     "static_assert(sizeof(app_flash_ldi_module_cfg_t) == 12);\n",
     "/* 契约：单模块条目固定 12B（device_type 1 + device_index 1 + vendor 10），\n"
     " * W25 记录按此布局读写，改错静默错位。 */\n"),
    ("Application/Inc/LDI/app_ldi_cmd.h",
     'static_assert(sizeof(ldi_device_info_t) == 17, "ldi_device_info_t must be 17 bytes");\n',
     "/* 契约：固定 17B（含 reserved[3]）；padding/对齐错位 = 静默收发错帧。 */\n"),
    ("Application/Src/LDI/app_ldi.c",
     'static_assert(sizeof(ldi_device_t) == 1, "ldi_device_t must be 1 byte");\n'
     'static_assert(sizeof(ldi_cmd_type_t) == 1, "ldi_cmd_type_t must be 1 byte");\n',
     "/* 契约：枚举按 1 字节编解码（LDI 帧内字段宽度）；改错静默解析错位。 */\n"),
    ("Application/Src/IAP/app_iap_cmd.c",
     'static_assert(sizeof(g_iap_cmd_table) / sizeof(g_iap_cmd_table[0]) == IAP_CMD_COUNT,\n'
     '              "IAP 命令表条目数与 IAP_CMD_COUNT 不同步");\n',
     ""),
]


def main() -> int:
    fail = 0
    for rel, old, new in EDITS:
        path = ROOT / rel
        text = path.read_text(encoding="utf-8")
        n = text.count(old)
        if n != 1:
            print(f"✘ {rel}: 命中 {n} 次（期望 1）——跳过")
            fail = 1
            continue
        path.write_text(text.replace(old, new, 1), encoding="utf-8")
        print(f"✔ {rel}: 替换 1 处（{len(old.splitlines())} 行 → {len(new.splitlines())} 行）")
    print("总计替换", len(EDITS), "处；失败", fail)
    return fail


if __name__ == "__main__":
    sys.exit(main())
