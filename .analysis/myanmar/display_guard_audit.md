# 「整机无显示驱动」边界防护审计表（自动生成）

- 生成脚本：`.analysis/myanmar/check_display_guards.py`
- 守卫硬断言：16/16 通过

## 守卫硬断言清单

| 文件 | 断言 | 说明 |
|---|---|---|
| `Device/Inc/dev_display.h` | `bool dev_display_guard(const char *what);` | 守卫助手声明 |
| `Device/Display/dev_display.c` | `bool dev_display_guard(const char *what)` | 守卫助手实现（限速告警） |
| `Device/Display/dev_display.c` | `dev_display_guard("scan_task")` | dev_display_start：无显示不建 scan_task（⇒ 不启动 TIM3/TIM4） |
| `Device/Display/dev_display.c` | `if (dev == nullptr)
        return;` | _on_tim4_period：PWM ISR 判空（纵深防御） |
| `Application/Src/app_boot.c` | `dev_display_guard("splash")` | 开机 splash 判空跳过 |
| `Application/Src/app_default_display.c` | `dev_display_guard("default-display")` | 默认欢迎画面判空跳过 |
| `Device/IO/dev_light_sensor.c` | `if (dev == nullptr || dev->display == nullptr)` | 光敏调光判空跳过（无显示不写 light_level） |
| `Application/Src/app_factory_test.c` | `dev_display_guard("factory-test")` | 出厂检测 monitor 判空 → 待机降级 |
| `Application/Src/LDI/app_ldi_device.c` | `dev_display_guard("ldi.e6")` | LDI E6 显示命令判空 → 回 LDI_DEV_FAIL |
| `Application/Src/LDI/app_vms_ctrl.c` | `dev_display_guard("ldi.vms")` | LDI E9 VMS 判空跳过 |
| `Application/Src/ProtocolParser_Myanmar/app_mm_proto.c` | `if (dev_display_get() != nullptr)
        app_default_displa` | 缅甸开机默认画面（红叉）：仅带显示驱动时注册（无显示不注册、零告警） |
| `Application/Src/ProtocolParser_Myanmar/app_mm_proto.c` | `if (d == nullptr)
        return;` | 缅甸红叉回调判空（无显示纵深防御） |
| `Device/Display/dev_display.c` | `void dev_display_set_pixel(` 首部判空 | 像素 API 纵深防御 |
| `Device/Display/dev_display.c` | `void dev_display_set_brightness(` 首部判空 | 像素 API 纵深防御 |
| `Device/Display/dev_display.c` | `void dev_display_fill(` 首部判空 | 像素 API 纵深防御 |
| `Device/Display/dev_display.c` | `void dev_display_draw_bitmap(` 首部判空 | 像素 API 纵深防御 |

## `dev_display_get()` 调用点分类（启发式扫描）

| 文件:行 | 分类 | 源码 |
|---|---|---|
| `Application/Src/AH_MQTT/ah_mqtt.c:158` | 已判空/三元 | `const dev_display_t *disp = dev_display_get();` |
| `Application/Src/AH_MQTT/ah_mqtt_cmd.c:53` | 已判空/三元 | `dev_display_t *dsp = dev_display_get();` |
| `Application/Src/LDI/app_ldi_device.c:173` | 已判空/三元 | `dev_display_t *d  = dev_display_get();` |
| `Application/Src/LDI/app_vms_ctrl.c:89` | 已判空/三元 | `dev_display_t *d  = dev_display_get();` |
| `Application/Src/ProtocolParser_Anhui/app_anhui_proto_cmd.c:35` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_Anhui/app_anhui_proto_cmd.c:56` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_Anhui/app_anhui_proto_cmd.c:114` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_Anhui/app_anhui_proto_cmd.c:194` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_Anhui/app_anhui_proto_cmd.c:219` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_ChongQing/app_cq_proto_cmd.c:137` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_ChongQing/app_cq_proto_cmd.c:198` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_ChongQing/app_cq_proto_cmd.c:237` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_ChongQing/app_cq_proto_cmd.c:280` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_ChongQing/app_cq_proto_cmd.c:377` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_ChongQing/app_cq_proto_cmd.c:479` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_GuiZhou/app_gz_proto_cmd.c:59` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_GuiZhou/app_gz_proto_cmd.c:84` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_GuiZhou/app_gz_proto_cmd.c:119` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_GuiZhou/app_gz_proto_cmd.c:165` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_GuiZhou/app_gz_proto_cmd.c:185` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_GuiZhou/app_gz_proto_cmd.c:220` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_GuiZhou/app_gz_proto_cmd.c:253` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_GuiZhou/app_gz_proto_cmd.c:271` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_GuiZhou/app_gz_proto_cmd.c:309` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_GuiZhou/app_gz_proto_cmd.c:382` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c:297` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c:410` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c:444` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_default.c:62` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_Myanmar/app_mm_proto.c:139` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_Myanmar/app_mm_proto.c:181` | 已判空/三元 | `if (dev_display_get() != nullptr)` |
| `Application/Src/ProtocolParser_QingHai/app_qh_proto_cmd.c:51` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_QingHai/app_qh_proto_cmd.c:70` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_QingHai/app_qh_proto_cmd.c:102` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_QingHai/app_qh_proto_cmd.c:135` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_QingHai/app_qh_proto_cmd.c:161` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_QingHai/app_qh_proto_cmd.c:206` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_ShanDong/app_sd_proto_cmd.c:48` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_ShanDong/app_sd_proto_cmd.c:61` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_ShanDong/app_sd_proto_cmd.c:75` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_ShanDong/app_sd_proto_cmd.c:109` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_ShanDong/app_sd_proto_cmd.c:143` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_ShanDong/app_sd_proto_default.c:23` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto_cmd.c:49` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto_cmd.c:66` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto_cmd.c:104` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto_cmd.c:163` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_ETC/app_sc_etc_proto_cmd.c:178` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto_cmd.c:39` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto_cmd.c:56` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto_cmd.c:90` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto_cmd.c:136` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto_cmd.c:170` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto_cmd.c:313` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_MTC/app_sc_mtc_proto_cmd.c:363` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_Overload/app_sc_ol_proto_cmd.c:85` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_Overload/app_sc_ol_proto_cmd.c:104` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_Overload/app_sc_ol_proto_cmd.c:139` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_SiChuang_Overload/app_sc_ol_proto_cmd.c:177` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan/app_yn_proto_cmd.c:161` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan/app_yn_proto_cmd.c:252` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan/app_yn_proto_cmd.c:296` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan/app_yn_proto_cmd.c:331` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan/app_yn_proto_cmd.c:370` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan/app_yn_proto_cmd.c:401` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan/app_yn_proto_cmd.c:495` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c:388` | 已判空/三元 | `const dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c:407` | 已判空/三元 | `const dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c:281` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c:355` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c:396` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c:436` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c:448` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c:466` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c:489` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c:642` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/RLS/app_rls_cmd.c:30` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/RLS/app_rls_cmd.c:60` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/RLS/app_rls_cmd.c:131` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/app_boot.c:97` | 已判空/三元 | `dev_display_t *dev = dev_display_get();` |
| `Application/Src/app_boot.c:169` | 已判空/三元 | `const dev_display_t *d = dev_display_get();` |
| `Application/Src/app_boot.c:214` | 已判空/三元 | `SEGGER_RTT_printf(0, "[diag] display dev_display_get()=NULL *** no dis` |
| `Application/Src/app_default_display.c:31` | 已判空/三元 | `.w     = dev_display_get()->screen_rows,` |
| `Application/Src/app_default_display.c:32` | 已判空/三元 | `.h     = dev_display_get()->screen_cols,` |
| `Application/Src/app_default_display.c:72` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/app_factory_test.c:95` | 已判空/三元 | `dev_display_t *dsp = dev_display_get();` |
| `Application/Src/app_factory_test.c:143` | 已判空/三元 | `dev_display_t *dsp = dev_display_get();` |
| `Application/Src/app_light_sensor.c:37` | 已人工确认（句柄赋值（不解引用）；解引用点在 dev_light_sensor_auto_adjust 判空） | `dev_light_sensor_init(&s_sensor_dev, dev_display_get());` |
| `Application/Src/app_render.c:370` | 已判空/三元 | `s_render_display = dev_display_get();` |
| `Application/Src/app_scroll.c:169` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/app_scroll.c:249` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/app_scroll.c:352` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/app_scroll.c:466` | 已判空/三元 | `dev_display_t *d = dev_display_get();` |
| `Application/Src/app_test.c:21` | 已判空/三元 | `dev_display_t *dsp = dev_display_get();` |
| `Application/Src/app_test.c:48` | 已判空/三元 | `dev_display_t *dsp = dev_display_get();` |
| `Application/Src/app_test.c:121` | 已判空/三元 | `dev_display_t *dsp = dev_display_get();` |
| `Application/Src/app_test.c:143` | 已判空/三元 | `dev_display_t *dsp = dev_display_get();` |
| `Application/Src/app_test.c:196` | 已判空/三元 | `dev_display_t *dsp = dev_display_get();` |
| `Application/Src/app_test.c:228` | 已判空/三元 | `dev_display_t *dsp = dev_display_get();` |
