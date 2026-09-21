# 归档说明（`.analysis/22_1665/archive/`）

本目录存放 22-1665 模组调试过程中**已被后续轮次取代**的脚本、报告、预测图、构建日志与驱动版本，
仅供追溯 / 复跑。现行工具与索引见 **上一级 `../README.md`**。

| 子目录 | 内容 |
|---|---|
| `scripts/` | 历史宿主脚本：第十二~十八轮（`check_round14/15/17/18.py`、`check_switch_matrix.sh`、`verify.py`、`solve_wiring*.py`、`reverse_engineer.py`、`level_cell_order.py`、`dump_glyph.py`、`gen_*.py`、`sync_docs.py`、`fix_doc_header.py`）+ **第二十三轮新归档**（`check_round20.py`、`check_round21.py`、`check_multichain.py`、`check_model_matrix.sh`——后四个的 `SRC` 已固定指向 `prev_round/…round23_pre_refactor.c`，断言的是**重构前驱动形态**，复跑 58/58、23/23、31/31、96/96 全通过）。脚本内 `ROOT` 已改为 `parents[4]`（shell 为 `../../../..`），其生成的 `.md` 会写在本子目录内 |
| `reports/` | 各轮报告：`round17_report.md`、`round18_explain_and_field.md`、`round19_probe_request.md`、`round20_calibration.md`、`round21_diagnosis.md`、`round22_cleanup.md`、**`round23_refactor.md`（本轮：驱动重构 + 零回归证明）**、`candidates.md`、`signatures.md`、`verify_report.txt` |
| `screens/` | 历史口径预测图 / 症状签名：`expected_screens.md`、`probe7_screens.md`、`probe8_screens.md`、`round17_screens.md`、`round18_color_matrix.md` |
| `logs/` | 历史构建日志：`build_round15/17/20.log`（历史报告引用的那几次构建） |
| `prev_round/` | 驱动历史版本：`*.20260917.bak`（第十一轮 512 位推定模型）、`*_round16_multichain.c`、`*_round17_probe9.c`、`*_round21_pre_round22.c`（第二十二轮整理前）、**`*_round23_pre_refactor.c`（第二十三轮重构前 = 探针 7/8/9 + 上电回读自检 + RTT 判读齐全的标定版，换屏标定从这里取回）**、**`*_round24_pre_resolution.c`（第二十四轮几何参数化前 = 现场调通的 1×1 四链版，`check_equivalence.py` / `check_host_differential.py` 的零回归对照基线）** |
| `finalize/` | **第二十九轮生产化收口（2026-09-17）的删除前快照**：`driver_pre_finalize_20260917.c`（**591 行**，md5 `2ccd650cc24c4360b24b151cc220e1b3`；含已删除的 `_22_1665_HUB_WIRING` 模型 A 全路径、`_22_1665_BLUE_AS_LIT`、`_22_1665_DATA_ACTIVE_HIGH`）——**现行驱动的唯一恢复路径**：`cp` 回 `Device/Display/dev_display_22_1665.c` 即第二十八轮形态，加 `-D` 三开关可复现旧口径；`check_equivalence.py`（归档 vs 现行机器级对照）与历史脚本 `round25/26/27_two_*.py` 也锚定本目录。另存 `app_default_display_test_1n2_20260917.c`（第二十五/二十六轮现场默认画面测试串 `"1\n2"` 快照，**不参与构建**，仅作历史脚本断言锚点） |

> 各轮结论的**现行状态**（哪些作废、哪些仍是 as-built）见
> `doc/01_显示系统/22-1665模组驱动分析与迁移记录.md`（§0 as-built + 文末「历史轮次」）。

## 归档脚本的可跑性（2026-09-17 第二十二轮实测）

| 脚本 | 现在跑的结果 | 说明 |
|---|---|---|
| `check_round14.py` / `check_round15.py` / `check_round17.py` / `check_round18.py` | 14/14、98/98、35/35、35/35 **全通过** | 断言的是仍在用的结构/语义（掩码位序、探针 7/8 图案、落点旗标、观测回溯） |
| `check_switch_matrix.sh` | 通过 26 / **未通过 9** | **预期**：它枚举的是第十一轮那批**已在第十二轮删除**的开关（`SCAN_SEL`/`CHAIN_DUAL`/`BLK_W`/`SEG_LAYOUT`/`G_FIRST`/`LINE_REVERSE`/`BIT_REVERSE`/`BLUE_LINK`），现在编译必然失败——这正是它被归档的原因 |
| `verify.py` | 输出「驱动=None / 模型=512」等差异清单 | **预期**：它对照的是第十轮 512 位推定模型，该模型已于第十二轮整体作废；仅作历史对照，**不是回归工具** |
| `check_round20.py` / `check_round21.py` / `check_multichain.py` / `check_model_matrix.sh`（第二十三轮新归档） | 58/58、23/23、31/31、96/96 **全通过** | 断言的是**重构前驱动形态**（探针 / 多链链表 / 兼容口径），`SRC` 已固定指向 `prev_round/…round23_pre_refactor.c`；现行形态的对应证明见顶层 `check_equivalence.py` / `check_driver.py` / `check_compile_matrix.sh` |

> 归档脚本的 `ROOT` 已改为 `parents[4]`（`verify.py` 为 `HERE.parents[3]`），其在同目录生成的
> `.md` 属于历史产物，可随时删除重跑。
>
> **路径说明**：历史报告 / 记录内部引用的路径（如 `.analysis/22_1665/round20_calibration.md`、
> `build_round20.log`）均按**归档前**的目录结构书写——同名文件现在位于本目录内的对应子目录
> （`reports/`、`logs/`、`screens/`、`scripts/`）；对 `doc/01` 的 `§NN` 小节号引用同理，
> 已由重构后的 `doc/01`（§0 / 附录 A）取代。
