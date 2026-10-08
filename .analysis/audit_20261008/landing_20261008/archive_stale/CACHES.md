# clangd / EIDE 缓存核查与处置（2026-10-08，D6）

## 结论

| 路径 | 内容 | 处置 | 依据 |
|---|---|---|---|
| `build/Release/`（4 文件） | `.lock` / `builder.params` / `compile_commands.json` / `ref.json`，均 2026-08-11 | **归档后删除**（→ `archive_stale/build_Release/`） | 201/201 条编译条目指向已不存在的 `/home/yystation/Desktop/Project-STD-main`；EIDE Release 目标本身不可用 |
| `build/Debug/.cache/clangd/index/`（4.0MB） | clangd 项目索引（与 `build/Debug/compile_commands.json` 配套） | **保留** | 活着（最新 mtime = 2026-10-08 09:06，与 EIDE/clangd 现役会话同步）；0 处引用旧路径 |
| `build/Debug/compile_commands.json` | make 生成的编译数据库（2026-10-08 10:57） | **保留** | clangd 正在使用（`.clangd`/编辑器配置指向 `build/Debug`） |
| `~/.cache/clangd/index/`（2357 文件） | **全局** clangd 索引缓存，含其它项目（Qt 头等） | **不动** | 跨项目共用；实测 0 处引用本仓旧路径 ⇒ 不存在「本仓陈旧残留」可清 |

## 缓存路径判定过程（证据）

```bash
# 项目内 clangd 缓存：与 compile_commands.json 同目录（clangd 约定：索引落
#   <compile_commands.json 所在目录>/.cache/clangd/index）
ls -la build/Debug/.cache/clangd/index | head      # 4.0MB、最新 2026-10-08 09:06
grep -rl "Desktop/Project-STD-main" build/Debug/.cache/clangd/index | wc -l   # → 0

# 全局缓存：他项目共用，且无本仓旧路径
ls ~/.cache/clangd/index | head                    # FILE.h.….idx / QAbstractButton.….idx（Qt）
find ~/.cache/clangd -type f | wc -l               # → 2357
grep -rl "Desktop/Project-STD-main" ~/.cache/clangd | wc -l                   # → 0
```

`build/Release/` 归档清单见同目录 `build_Release_MANIFEST.md`。
