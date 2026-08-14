# 02 解耦方案

> **状态**：待审核　|　2026-08-14  
> **前提**：已读 [01_background_and_impact.md](./01_background_and_impact.md)  
> **纪律**：本文件仅方案；**未经授权不改代码**。

---

## §0 目标与非目标

### 目标

| ID | 目标 |
|----|------|
| D-1 | EIDE/Makefile **只编 LDI、不编 IAP 升级协议** 时链接成功 |
| D-2 | 仍可在需要时编入完整 IAP；Sector1 网络配置语义可保留或可显式关闭 |
| D-3 | LDI 源文件 **不再** `#include "app_iap_cfg.h"` / 直调 `app_flash_iap_*` |
| D-4 | Boot/Recovery 与主固件对 `0x08004000` 布局的兼容策略有文档与代码单一归属 |

### 非目标

- 不改 LDI 帧格式、probe、RB 绑定（属 doc/05）  
- 不强制取消 Sector1 存 IP（产品可保留，只改归属模块名）  
- 不在本方案内重做整板配置中心（可分阶段）

---

## §1 方案对比

| 方案 | 摘要 | 满足 D-1 | 工作量 | 推荐 |
|------|------|----------|--------|------|
| **A. 抽出板级网络配置模块** | `app_iap_cfg` → `app_board_net_cfg`（或 `Device/Config`），LDI/IAP 协议都依赖它 | ✅ | 中 | **首选** |
| **B. 弱符号 + 空实现** | `app_flash_iap_*` 改为 weak；无 IAP 配置 TU 时空操作 | ✅ | 小 | 过渡可接受 |
| **C. 接口注入 / 函数指针** | LDI 只依赖 `ldi_net_persist_ops`；默认挂 IAP cfg 或 stub | ✅ | 中 | 与 A 可合并 |
| **D. LDI 彻底不碰 Sector1** | 删同步调用；IP 只活在 W25 + 运行时 LwIP | ✅ | 小 | 需产品确认 Boot 行为 |
| **E. 仅文档规定必带 cfg** | 不改代码，选编规则强制带 `app_iap_cfg.c` | ⚠️ 治标 | 极小 | 见 [03](./03_interim_build_rules.md)，非终态 |

---

## §2 方案 A（推荐）：抽出「板级网络配置」

### 2.1 目标结构

```
Application/Src/Config/   或  Device/Config/
  app_board_net_cfg.c/.h     ← 原 app_iap_cfg 中与 Sector1/net 相关的 API
Application/Src/IAP/
  app_iap.c / app_iap_cmd.c  ← 只依赖 board_net_cfg + 升级逻辑
Application/Src/LDI/
  app_ldi.c / app_ldi_cmd.c  ← 只依赖 board_net_cfg（或经 ops）
```

命名示例（落地时可调整，但勿再叫「iap」若模块已中立）：

```c
/* app_board_net_cfg.h */
bool board_net_cfg_valid(void);
bool board_net_cfg_get(uint8_t ip[4], uint8_t mask[4], uint8_t gw[4]);
int  board_net_cfg_set(const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4]);
```

内部仍可读写 `0x08004000` 的既有布局（**二进制兼容 Boot/Recovery**），或在同文件内保留 `app_flash_iap_sys_info_t` 别名。

### 2.2 机械步骤（审核通过后执行）

1. 新建 `app_board_net_cfg.{c,h}`，从 `app_iap_cfg` **迁出**（或整体改名 + 更新引用）。  
2. `app_iap_cfg.h` 可薄包装 `#include "app_board_net_cfg.h"` + deprecated 别名（过渡一期）。  
3. LDI：`#include "app_board_net_cfg.h"`，替换三处调用为 `board_net_cfg_*`。  
4. IAP 协议：继续用同一头文件做升级状态 / FWInfo（若 FWInfo 仍在同结构，可同模块或再拆 `app_board_fw_meta`）。  
5. EIDE/Makefile：`Config` 或 `board_net_cfg` 列为 **公共必选**；`IAP/` 协议目录可选。  
6. 更新本目录 README 状态 → 已落地；补回归清单。

### 2.3 验收（DoD）

- [ ] 工程 **exclude 全部** `app_iap.c` / `app_iap_cmd.c`，保留 `app_board_net_cfg.c` + LDI → **链接成功**  
- [ ] 工程 **exclude LDI**，保留 IAP 协议 + board_net_cfg → 链接成功  
- [ ] LDI 目录内 `rg app_iap_cfg` / `rg app_flash_iap` 无匹配（或仅测试桩）  
- [ ] `0AH` 改 IP 后 Sector1 `net_cfg` 与 W25 一致（若保留同步语义）  
- [ ] 上电：W25 空 / Sector1 有值 / 皆空 三条路径行为与现网一致或有产品签字差异说明  

---

## §3 方案 B：弱符号空实现（低成本过渡）

在公共层提供：

```c
__attribute__((weak)) bool app_flash_iap_is_config_valid(...) { return false; }
__attribute__((weak)) void app_flash_iap_update_net_cfg(...) { (void)ip;(void)mask;(void)gw; }
app_flash_iap_sys_info_t *g_config __attribute__((weak));
```

强符号仍由 `app_iap_cfg.c` 提供。

| 优点 | 缺点 |
|------|------|
| 改动小，立刻可「只编 LDI」 | LDI 仍 `#include app_iap_cfg.h`，目录边界未清 |
| | 无 cfg 时静默放弃 Sector1 同步，易忽略产品风险 |
| | `g_config` weak 空指针需所有路径判空 |

建议：仅作 **过渡**，终态仍走方案 A。

---

## §4 方案 C：LDI 持久化 ops 注入

```c
typedef struct {
    bool (*net_valid)(void);
    void (*net_load)(uint8_t ip[4], uint8_t mask[4], uint8_t gw[4]);
    void (*net_store)(const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4]);
} ldi_net_bridge_ops_t;

void ldi_set_net_bridge(const ldi_net_bridge_ops_t *ops); /* nullptr = 不桥接 Sector1 */
```

- 默认产品固件：在 `app_boot` 或 IAP cfg initcall 里注入真实 ops。  
- 纯 LDI 演示固件：不注入 → 只走 W25 / `pl_net_get_ip`。  

可与方案 A 组合：ops 的默认实现放在 `app_board_net_cfg`。

---

## §5 方案 D：取消 Sector1 同步（产品决策）

删除 LDI 中所有 `app_flash_iap_*` 调用，IP 仅：

- W25 LDI 记录  
- 运行时 `pl_net_*`  

| 条件 | 说明 |
|------|------|
| 必须产品确认 | Boot/Recovery **不再**依赖 Sector1 的 net_cfg，或另有配置源 |
| 风险 | 出厂只配 LDI、未写 Sector1 时，升级/恢复流程 IP 不一致 |

若产品确认「主业务只认 W25」，D 最干净；否则不要单独选 D。

---

## §6 推荐落地路径

```
短期（不改架构）     → 执行 03 选编规则（必带 app_iap_cfg.c）
中期（推荐）         → 方案 A 抽 board_net_cfg；可选加方案 C 注入
可选加速过渡         → 方案 B weak stub（同时排期 A）
产品放弃 Sector1 IP  → 方案 D + 更新 Boot/Recovery 文档
```

---

## §7 风险与回滚

| 风险 | 缓解 |
|------|------|
| 搬文件漏改 EIDE/Makefile | 以「只 LDI / 只 IAP」两套链接为 CI 门禁 |
| 结构体搬迁破坏 Boot | **保持** `0x08004000` 布局与 CRC 算法不变；只改源码归属 |
| 擦写 Flash 时机 | 维持现逻辑：避免无配置上电路径 erase |

回滚：恢复 `app_iap_cfg` 路径与 LDI include 即可。

---

## 修订

- 2026-08-14：首版方案，待审核。
