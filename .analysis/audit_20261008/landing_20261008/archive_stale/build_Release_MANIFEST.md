# build/Release 陈旧残留归档清单（2026-10-08 D6）

来源：`build/Release/`（未跟踪，.gitignore 含 /build）
判定陈旧依据：① 时间戳 2026-08-11（早于本工作区迁移）；② `compile_commands.json` 201 条条目全部指向 `/home/yystation/Desktop/Project-STD-main`（已不存在的旧目录），实测 201/201 文件不存在；③ EIDE Release 目标本身为不可用入口（doc/CLAUDE.md）。

| 文件 | 大小 | 说明 |
|---|---|---|
| .lock | 0 | Aug 11 11:02 |
| builder.params | 14874 | Aug 11 11:02 |
| compile_commands.json | 285112 | Aug 11 11:02 |
| ref.json | 37784 | Aug 11 11:02 |
