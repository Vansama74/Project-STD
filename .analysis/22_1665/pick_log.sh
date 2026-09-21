#!/usr/bin/env bash
# 22-1665 宿主脚本共用助手：挑出「含 22_1665 驱动编译命令的**最新**构建日志」。
#
# 背景：`link_variant.sh` / `check_round30_ab.sh` / `check_round32_ab.sh` / `check_round34_ab.sh`
# 都从「最新一份 `build_round*.log`」里抓驱动编译（与链接）命令行；此前直接取
# `ls -t | head -1`，于是**最近一次 make 若不是 22_1665 口径**（例如收尾用 `make -j8` 恢复
# 1_263 产物）就抓不到驱动命令而失败。本助手改为按 mtime 从新到旧扫描，取**第一份含
# 驱动编译命令**的日志（`--need-link` 时还要求含整项目链接命令）。
#
# 用法（在脚本里 source 后调用）：
#   . "$HERE/pick_log.sh"
#   LOG="$(pick_log_1665 --need-link)"       # 失败（找不到）时返回 1、不打印
#   CC_LINE="$(grep -m1 -- '-c -o build/Debug/Device/Display/dev_display_22_1665.o' "$LOG")"
pick_log_1665() {
    local need_link="${1:-}"
    local here root log
    here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    root="$(cd "$here/../.." && pwd)"
    for log in $(ls -t "$root"/.analysis/22_1665/build_round*.log 2>/dev/null); do
        grep -q -- '-c -o build/Debug/Device/Display/dev_display_22_1665.o' "$log" || continue
        if [ "$need_link" = "--need-link" ]; then
            grep -q -- '-o build/Debug/Project_STD.elf' "$log" || continue
        fi
        printf '%s\n' "$log"
        return 0
    done
    return 1
}
