# 07 — LDI 与 IAP 配置耦合 / 解耦

> **范围**：EIDE/目录选编时「只编 LDI、不编 IAP」导致链接失败的根因、影响面，以及解耦方案。  
> **状态**：问题 **已确认**（2026-08-14）；三处缺陷 **已定案并先行修复**（2026-08-14，见 [02 §9](./02_decoupling_solutions.md)）；两项补充修复（守卫细化 + 4B02 结果码）**已落地**（见 [02 §10](./02_decoupling_solutions.md)）；**方案 A 主体重构（抽出板级网络配置模块 `app_board_net_cfg`）已落地（2026-08-14）**。  
> **非本目录**：多协议 RB/绑定 → [`../05_协议模块多协议兼容优化/`](../05_协议模块多协议兼容优化/README.md)；SRAM/CCM → [`../06_SRAM内部分数据迁移/`](../06_SRAM内部分数据迁移/README.md)。  
> **备注（2026-08-14）**：`doc/CLAUDE.md` 中「当前固件入口 0x08000000、无 IAP bootloader 运行」已过时——链接脚本 `Compiler/STM32F407XX_FLASH.ld:67` 现为 `ORIGIN=0x08040000, LENGTH=768K`，`Core/Src/main.c:15` 已设 `SCB->VTOR = FLASH_BASE|0x40000`（主固件已按 IAP 适配入口链接）；本目录 01/02/03 均按此现状撰写。

---

## 文档结构

| 文件 | 角色 |
|------|------|
| [01_background_and_impact.md](./01_background_and_impact.md) | **背景、根因、影响面** |
| [02_decoupling_solutions.md](./02_decoupling_solutions.md) | **解耦方案**（推荐路径 + 备选 + 验收） |
| [03_interim_build_rules.md](./03_interim_build_rules.md) | **解耦前**工程选编临时规则 |

## 快速结论

| 问 | 答 |
|----|-----|
| 只加 LDI 为何链接失败？ | LDI **硬依赖** `app_iap_cfg`（Sector1 网络配置 API），不是依赖 IAP **升级协议** |
| 是否必须加整包 IAP？ | **否**。最小要 `app_iap_cfg.c`；`app_iap.c` / `app_iap_cmd.c` 可裁 |
| 长期怎么解？ | **已落地（2026-08-14）**：抽出中立模块 `app_board_net_cfg`（`Application/Config`），LDI 与 IAP 协议都依赖它；`app_iap_cfg.{c,h}` 与死代码 `Device/Inc/config_info.h` 已删除 |
| 改码纪律 | 方案在 `02`；**审核通过后才改代码**（三处缺陷修复已定案落地，见 [02 §9](./02_decoupling_solutions.md)） |
| 出厂新机 0AH 改 IP，Boot 为何读不到？ | 现状缺陷已修复（2026-08-14）：空 Sector1 写路径不置 magic（[01 §3.4](./01_background_and_impact.md)）→ 现按 Bootloader 语义完整初始化；0AH 写失败如实应答 01H，上位机重发恢复 |
| 三固件出厂默认网络配置 | 统一为 192.168.114.200/24、网关 192.168.114.1、端口 9528；Recovery 上电读 Sector1.net_cfg 配 netif、主固件 W25 空 + Sector1 有效时同步 set_ip（见 [02 §11](./02_decoupling_solutions.md)） |
| 网络配置真源与镜像 | Sector1.net_cfg 为**真源**（三固件共享）；W25 网络字段为**恢复镜像**（擦 Sector1 后还原用户配置）；主固件上电 Sector1 优先、W25 自愈回写、皆空用出厂默认（见 [02 §12](./02_decoupling_solutions.md)） |
| W25 镜像自愈 | 主固件上电分支 1 检测 Sector1 与 W25 网络字段不一致即刷新镜像（覆盖 4B02 单写 Sector1 或单次 W25 写失败的陈旧，见 [02 §12](./02_decoupling_solutions.md)） |
| Bootloader 损坏态自愈 | magic 不匹配或 config_crc 错 → 条件 C 重建出厂记录自愈；App 损坏仍 Recovery；Recovery 判空保持两哨兵（防死循环进 Recovery 的分工，见 [02 §12](./02_decoupling_solutions.md)） |
| 配置生效时机 | 所有改 IP 接口（LDI 0AH / IAP 4B02 / Recovery IAP）统一**重启生效**（既定策略，非缺陷；4B02 已删除即时应用段） |
| UDP 发现口 | 10011 固定（宏 `LDI_DISCOVERY_PORT`），禁止提供修改接口 |
| IAP 0x03 版本号来源 | 主固件启动时把 `PROGRAM_CODE` 落库 Sector1 `app_info.version`（`app_board_net_cfg_fw_version_update`，见 [02 §13](./02_decoupling_solutions.md)）——此前无人写入恒报全 0 |

## 与 05 的边界

| | 05 多协议 | 07 本专题 |
|--|-----------|-----------|
| 主题 | 谁绑哪个通道 / RB | **配置存储归属**与编译依赖 |
| 协议帧 | LDI / IAP 可同 RJ45 链式 | 即使不编 IAP 协议，配置层仍可能被 LDI 引用 |

阅读顺序：`01` → `03`（临时规避）→ `02`（正式解耦）。

---

## 修订

- 2026-08-14：首版，与 01/02/03 同步。
- 2026-08-14（同日修订）：补充固件入口现状备注（doc/CLAUDE.md 矛盾点）、出厂新机 0AH 缺陷入口。
- 2026-08-14（同日再修订）：状态改为「缺陷已定案并先行修复、主体重构暂缓」；快速结论同步 [02 §9](./02_decoupling_solutions.md) 三决策。
- 2026-08-14（同日三修订）：状态补两项补充修复（守卫细化 + 4B02 结果码，见 [02 §10](./02_decoupling_solutions.md)）。
- 2026-08-14（同日四修订）：**方案 A 落地**——抽出 `app_board_net_cfg`（`Application/Config`，Sector1 布局唯一归属 + static_assert 锁定），LDI/IAP 迁移到 `app_board_net_cfg_get/update/read`，删除 `app_iap_cfg.{c,h}` 与 `Device/Inc/config_info.h`；[03](./03_interim_build_rules.md) 选编临时规则废止。
- 2026-08-18：新增 [02 §11](./02_decoupling_solutions.md) 三固件出厂默认网络配置统一约定（192.168.114.200/24、网关 192.168.114.1、端口 9528）；快速结论同步三项一致性修复现状。
- 2026-08-18（同日再修订）：同步 [02 §12](./02_decoupling_solutions.md) 网络配置真源与生效语义——Sector1 真源/W25 恢复镜像、0AH/4B02 重启生效（既定策略）、UDP 发现口固定宏 `LDI_DISCOVERY_PORT`（删除 `app_udp_set_port`）。
- 2026-08-18（同日三修订）：快速结论同步 [02 §12](./02_decoupling_solutions.md) 缺陷 A/C/D/E 修复——W25 镜像自愈、Bootloader 损坏态重建（magic/CRC 错 → 条件 C 重建出厂记录；Recovery 判空保持两哨兵防死循环）、所有改 IP 接口（0AH/4B02/Recovery IAP）统一重启生效。
- 2026-08-18（同日四修订）：新增 [02 §13](./02_decoupling_solutions.md) 固件版本落库——IAP 0x03 上报的 version 由主固件启动时从 PROGRAM_CODE 写入 Sector1（空/损坏自愈、升级中间态拒绝、同值跳过）。
