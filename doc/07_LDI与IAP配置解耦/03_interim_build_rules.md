# 03 解耦前：工程选编临时规则

> **状态**：现行临时规范　|　直到 [02](./02_decoupling_solutions.md) 方案 A/B/C/D 落地  
> **目的**：在不改代码的前提下，避免「只加 LDI」链接失败。

---

## §1 规则（强制）

### 编入 LDI 时

| 必须编入 | 可选 |
|----------|------|
| `Application/Src/LDI/**`（协议） | — |
| **`Application/Src/IAP/app_iap_cfg.c`** | — |
| `Application/Inc/IAP/app_iap_cfg.h`（头路径） | — |
| LDI 已有的存储依赖（`app_ldi_cfg` / W25 等） | — |
| | `app_iap.c` / `app_iap_cmd.c`（仅当需要 UDP IAP 升级） |

**一句话**：LDI ⇒ 必带 **IAP 配置存储**；不必带 **IAP 升级协议**。

### 不编 LDI、只编 IAP 升级时

| 必须 | 说明 |
|------|------|
| `app_iap_cfg.c` + `app_iap.c` + `app_iap_cmd.c` | 协议本身依赖配置区 |

### 两者都不编

可不编 `app_iap_cfg.c`（无引用则 `--gc-sections` 可丢；若他处仍引用则仍要保留）。

---

## §2 EIDE 操作提示

1. 排除整个 `IAP/` 目录前，确认是否仍勾选了 LDI。  
2. 若保留 LDI：在源文件列表中 **单独加回** `app_iap_cfg.c`（不要依赖「整个 IAP 文件夹」开关）。  
3. 链接失败时先 `rg app_flash_iap Application/Src/LDI`，对照本表，而不是先怀疑工具链。

---

## §3 Makefile 对照

全量 `make` 默认编入 IAP+LDI，一般无此问题。  
若增加 `PROTOCOL=ldi-only` 之类裁剪目标，须在源列表中显式保留 `app_iap_cfg.c`，或改为已解耦的 `app_board_net_cfg.c`。

---

## §4 验收（临时）

- [ ] EIDE：LDI + 仅 `app_iap_cfg.c`，无 `app_iap.c` → **链接成功**  
- [ ] EIDE：LDI，无任何 IAP 文件 → **链接失败**（符合当前代码预期；解耦后此条应反过来）

---

## 修订

- 2026-08-14：与 01/02 同步首版。
