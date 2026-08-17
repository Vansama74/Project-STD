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
