# 07 — LDI 与 IAP 配置耦合 / 解耦

> **范围**：EIDE/目录选编时「只编 LDI、不编 IAP」导致链接失败的根因、影响面，以及解耦方案。  
> **状态**：问题 **已确认**（2026-08-14）；三处缺陷 **已定案并先行修复**（2026-08-14，见 [02 §9](./02_decoupling_solutions.md)）；两项补充修复（守卫细化 + 4B02 结果码）**已落地**（见 [02 §10](./02_decoupling_solutions.md)）；**方案 A 主体重构（抽出板级网络配置模块 `app_board_net_cfg`）已落地（2026-08-14）**。  
> **非本目录**：多协议 RB/绑定 → [`../05_协议模块多协议兼容优化/`](../05_协议模块多协议兼容优化/README.md)；SRAM/CCM → [`../06_SRAM内部分数据迁移/`](../06_SRAM内部分数据迁移/README.md)。  
---

## 文档结构

| 文件 | 角色 |
|------|------|
| [01_background_and_impact.md](./01_background_and_impact.md) | **背景、根因、影响面** |
| [02_decoupling_solutions.md](./02_decoupling_solutions.md) | **解耦方案**（推荐路径 + 备选 + 验收） |

> 原 `03_interim_build_rules.md`（解耦前选编临时规则）已于 2026-11-18 删除（历史见 git）；
> 其唯一仍有效的结论：配置层 `app_board_net_cfg.c` 为公共必选，LDI / IAP 目录可各自独立裁剪。

## 快速结论

| 问 | 答 |
|----|-----|
| 只加 LDI 为何链接失败？ | LDI **硬依赖** `app_iap_cfg`（Sector1 网络配置 API），不是依赖 IAP **升级协议** |
| 是否必须加整包 IAP？ | **否**。最小要 `app_iap_cfg.c`；`app_iap.c` / `app_iap_cmd.c` 可裁 |
| 长期怎么解？ | **已落地（2026-08-14）**：抽出中立模块 `app_board_net_cfg`（`Application/Config`），LDI 与 IAP 协议都依赖它；`app_iap_cfg.{c,h}` 与死代码 `Device/Inc/config_info.h` 已删除 |
| 改码纪律 | 方案在 `02`；**审核通过后才改代码**（三处缺陷修复已定案落地，见 [02 §9](./02_decoupling_solutions.md)） |
| 出厂新机 0AH 改 IP，Boot 为何读不到？ | 现状缺陷已修复（2026-08-14）：空 Sector1 写路径不置 magic（[01 §3.4](./01_background_and_impact.md)）→ 现按 Bootloader 语义完整初始化；0AH 写失败如实应答 01H，上位机重发恢复 |
| 三固件出厂默认网络配置 | 统一为 192.168.114.200/24、网关 192.168.114.1、端口 9528；Recovery 上电读 Sector1.net_cfg 配 netif、主固件记录空 + Sector1 有效时同步 set_ip（见 [02 §11](./02_decoupling_solutions.md)） |
| 网络配置真源与镜像 | Sector1.net_cfg 为**真源**（三固件共享）；LDI 配置记录（**跨介质双库**：Sector 11 槽日志 + W25 末扇区，2026-11-26 起；原 W25）的网络字段为**恢复镜像**（擦 Sector1 后还原用户配置）；主固件上电 Sector1 优先、记录自愈回写、皆空用出厂默认（见 [02 §12](./02_decoupling_solutions.md)、[§17](./02_decoupling_solutions.md)、[§18](./02_decoupling_solutions.md)） |
| 身份第二副本（镜像） | lane/cert/host 存 Sector1 偏移 0x100 的 36B 身份镜像（magic `0x0D0F0A05` + CRC，2026-11-26 起）；Sector1 全部擦写路径统一 `_rmw` 携带镜像；双库皆失时镜像救援身份并重建记录，镜像被 Bootloader 整扇区擦除后由双库记录上电自愈补回（见 [02 §18.2/§18.3](./02_decoupling_solutions.md)） |
| 旧 W25 记录迁移 | 双库皆无有效记录时，迁移垫片只读扫 W25 末 4KB 旧格式记录（magic `0x0D001B00`，v3/v2/v1 三级 CRC 试探）自动恢复现场配置（一次性，见 [02 §18.4](./02_decoupling_solutions.md)；§17 的「旧记录不做迁移」裁决由 §18 推翻） |
| 记录镜像自愈 | 主固件上电分支 1 检测 Sector1 与记录的设备网络四项不一致即刷新镜像（覆盖 4B02 单写 Sector1 或单次记录写失败的陈旧，见 [02 §12](./02_decoupling_solutions.md)） |
| Bootloader 损坏态自愈 | magic 不匹配或 config_crc 错 → 条件 C 重建出厂记录自愈；App 损坏仍 Recovery；Recovery 判空保持两哨兵（防死循环进 Recovery 的分工，见 [02 §12](./02_decoupling_solutions.md)） |
| 搜索/上报端口字段语义 | LDI 12H 应答与 IAP 0x01 上报的 `port` 字段 = 配置功能端口（TCP 业务口，出厂默认 9528，宏 `LDI_DEFAULT_CONFIG_PORT`）；UDP 10011（宏 `LDI_DISCOVERY_PORT`）仅为搜索/上报的**传输渠道**，勿混淆（2026-08-18 修复 12H 误报 10011 污染循环，见 [02 §12 ④](./02_decoupling_solutions.md)） |
| LDI 12H 应答发送路径 | **广播为主、回源为辅**（2026-08-21）：先 `app_udp_broadcast` 再 `channel_send` 回源（失败静默）；广播复用常驻通道 conn（`lwipopts.h` `MEMP_NUM_NETCONN=8`/`MEMP_NUM_UDP_PCB=8` 扩池根治池满丢包）。**已知限制**：回源依赖通道级 src 快照，多协议交错下可能发错目标；IAP 回源同源竞态不修改（见 [02 修订 2026-08-21 五](./02_decoupling_solutions.md)） |
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

