# 01 背景、根因与影响

> **状态**：现行事实　|　2026-08-14  
> **触发**：EIDE 工程仅勾选 LDI 相关源文件时链接失败（见终端 `undefined reference to app_flash_iap_*` / `g_config`）。  
> **解耦方案** → [02_decoupling_solutions.md](./02_decoupling_solutions.md)  
> **临时选编** → [03_interim_build_rules.md](./03_interim_build_rules.md)

---

## §1 问题现象

典型链接错误（EIDE / unify_builder）：

```
undefined reference to `app_flash_iap_is_config_valid'
undefined reference to `app_flash_iap_update_net_cfg'
undefined reference to `g_config'
```

出现在：

- `Application/Src/LDI/app_ldi.c` → `ldi_ctx_init`
- `Application/Src/LDI/app_ldi_cmd.c` → `cmd_set_ip`

Makefile 全量编译时不易暴露；**按协议目录裁剪（只留 LDI）时必现**。

---

## §2 背景与设计意图（为何当初耦合）

板级存在两套「网络相关」持久化：

| 存储 | 位置 | 模块 | 内容 |
|------|------|------|------|
| LDI 设备配置 | W25Qxx 末扇区 | `app_ldi_cfg` / `dev_flash_ldi` | 车道、证书、模块表、IP/端口等 |
| IAP 系统配置 | 内部 Flash Sector1 `0x08004000` | `app_iap_cfg` | magic、升级状态、FW 信息、**net_cfg(IP/掩码/网关)** |

产品需求（历史合理）：

1. Bootloader / Recovery / 主固件共享 **同一套上电 IP**（写在 Sector1）。  
2. LDI `0AH` 改 IP 后，除写入 W25 外，还要 **同步到 Sector1**，避免下次启动 Boot 仍用旧 IP。  
3. 上电时若 W25 无 LDI 配置，可 **回退读取** Sector1 的 `net_cfg`。

因此 LDI 直接调用了 `app_iap_cfg` API，而不是通过中立的「板级网络配置」抽象。

---

## §3 根因（精确到模块边界）

### 3.1 依赖不是「IAP 协议」，而是「IAP 配置存储」

```
LDI 协议模块                IAP 目录
─────────────────          ─────────────────────────────
app_ldi.c  ──────────────► app_iap_cfg.c/.h   ← 链接必需
app_ldi_cmd.c              app_iap.c/.h         ← 升级协议，可裁
                           app_iap_cmd.c        ← 可裁
```

未定义符号均来自 **`app_iap_cfg.c`**：

| 符号 | 作用 |
|------|------|
| `g_config` | 映射到 `0x08004000` |
| `app_flash_iap_is_config_valid` | 校验 Sector1 记录 |
| `app_flash_iap_update_net_cfg` | 擦写更新 IP/掩码/网关 |

### 3.2 调用点（代码事实）

**上电 `ldi_ctx_init`**：

- W25 有有效 LDI 配置且与 Sector1 不一致 → `app_flash_iap_update_net_cfg`  
- W25 无效 → 尝试从 `g_config->net_cfg` 拷贝 IP  

**运行时 `cmd_set_ip`（0AH）**：

- `app_flash_ldi_save_config`（W25）之后立刻 `app_flash_iap_update_net_cfg`（Sector1）

### 3.3 与 doc/05「目录选编」的张力

doc/05 目标：**工程目录选编协议**（exclude 即可裁剪）。  
现行 LDI 破坏了「只编 LDI 目录」的可链接性——exclude 整个 `IAP/` 会连配置层一起丢掉。

---

## §4 影响面

### 4.1 构建 / 产品裁剪

| 场景 | 结果 |
|------|------|
| EIDE 只勾 LDI，排除整个 `Application/Src/IAP/` | **链接失败** |
| 只勾 LDI + `app_iap_cfg.c` | 可链接；无 UDP IAP 升级能力 |
| Makefile 全量 | 通过（掩盖问题） |

### 4.2 架构 / 可维护性

- 协议层（LDI）依赖另一协议目录下的实现，**违反「目录即模块、可独立选编」**。  
- 新人易误判为「LDI 必须绑死 IAP 升级协议」。  
- 重命名/搬迁 IAP 目录时易再次踩链接雷。

### 4.3 运行行为（解耦前仍成立的业务耦合）

即使编译通过，运行时仍假定：

- Sector1 布局与 `app_flash_iap_sys_info_t` 一致；  
- Boot/Recovery 读同一结构；  
- LDI 改 IP 会擦写内部 Flash（有总线停顿风险，代码已避免在「无配置上电路径」写 Flash）。

解耦时必须保留或显式放弃这些产品语义，不能只删调用。

### 4.4 非影响（澄清）

| 项 | 说明 |
|----|------|
| 多协议 RB / 链式 probe | 与本问题无关（doc/05） |
| LDI 与 IAP **帧队列** | 本就独立，不共享 |
| 仅缺 `app_iap.c` | **不会**因本问题报上述符号缺失 |

---

## §5 问题定性

| 维度 | 结论 |
|------|------|
| 缺陷类型 | **编译期模块边界错误**（强符号依赖放错层） |
| 严重度 | 裁剪构建 **阻塞**；全量构建 **隐性** |
| 是否安全漏洞 | 否 |
| 是否必须整包 IAP | **否**；见 [03](./03_interim_build_rules.md) |

---

## 修订

- 2026-08-14：首版，对齐 EIDE「只加 LDI」链接失败现场。
