#!/usr/bin/env bash
# 核心业务函数 objdump A/B 对照（2026-09-29 代码瘦身轮）
#   A = 改动前源码树（/tmp/before_tree，由 backup/sources_before.tar.gz + 第三方目录组成，已实测基线一致）
#   B = 当前工作区 build/Debug（瘦身后）
# 输出：逐函数摘要（一致 / 有差异）+ 差异明细写入 .analysis/cleanup_20260929/ab/
set -u
ROOT="/home/yystation/Program/3833024/Project-STD-main"
REF="/tmp/before_tree"
OUT="$ROOT/.analysis/cleanup_20260929/ab"
mkdir -p "$OUT"

# 对象:函数 列表（核心业务路径）
FUNCS=(
  "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.o:mm_parse_frame"
  "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.o:mm_apply_lamp"
  "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.o:_mm_drive"
  "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.o:mm_execute_cmd"
  "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.o:mm_apply_init"
  "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.o:_mm_effective_lamp"
  "Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.o:mm_arrow_map_toggle"
  "Application/Src/app_dispatch.o:frame_dispatch_task"
  "Application/Src/app_dispatch.o:app_channel_dispatch"
  "Application/Src/app_dispatch.o:channel_send"
  "Application/Src/app_dispatch.o:app_dispatch_init"
  "Application/Src/app_boot.o:init_task"
  "Application/Src/app_boot.o:app_splash_display"
  "Application/Src/app_boot.o:half_sec_task"
  "Application/Src/app_boot.o:vApplicationMallocFailedHook"
  "Device/Display/dev_display.o:dev_display_guard"
  "Device/Display/dev_display.o:dev_display_commit_frame_rect"
  "Device/Display/dev_display.o:dev_display_draw_bitmap"
  "Device/Display/dev_display_1_263.o:dev_display_1_263_scan"
  "Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.o:gz_ol_execute_cmd"
  "Application/Src/ProtocolParser_Anhui/app_anhui_proto_cmd.o:anhui_execute_cmd"
  "Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.o:yn_ol_execute_cmd"
  "Application/Src/Channel/app_tcp_server.o:tcp_server_task"
  "Application/Src/Channel/app_udp.o:udp_gzol_task"
  "Application/Src/Config/app_board_net_cfg.o:app_board_net_cfg_fw_version_update"
  "Platform/Src/pl_net.o:pl_net_init"
  "Platform/Src/pl_eth.o:pl_eth_mac_hw_init"
)

# 取单函数反汇编（去地址列与目标数值，保留助记符+符号名）
extract() {
  local obj="$1" fn="$2"
  arm-none-eabi-objdump -d "$obj" 2>/dev/null | awk -v f="$fn" '
    $0 ~ ("^[0-9a-f]+ <" f ">:$") {on=1; print; next}
    on && /^$/ {exit}
    on && /^[0-9a-f]+ </ {exit}
    on {print}
  ' | sed -E 's/^ *[0-9a-f]+:\t([0-9a-f]{2} )+\t*/  /; s/\b[0-9a-f]{4,}\b//g' \
    | sed -E 's/ +$//' | grep -v '^$'
}

printf "%-46s %-34s %s\n" "对象" "函数" "结论"
printf '%s\n' "---------------------------------------------------------------------------------------------------"
ndiff=0; ntot=0; ndbg=0
for item in "${FUNCS[@]}"; do
  obj="${item%%:*}"; fn="${item##*:}"
  a=$(extract "$REF/build/Debug/$obj" "$fn")
  b=$(extract "$ROOT/build/Debug/$obj" "$fn")
  ntot=$((ntot+1))
  if [ -z "$a" ] || [ -z "$b" ]; then
    printf "%-46s %-34s %s\n" "$obj" "$fn" "跳过（缺对象/函数）"
    continue
  fi
  if [ "$a" = "$b" ]; then
    printf "%-46s %-34s %s\n" "$obj" "$fn" "完全一致"
  else
    ndiff=$((ndiff+1))
    safe=$(printf '%s' "$fn" | tr -c 'A-Za-z0-9_' '_')
    diff -u <(printf '%s\n' "$a") <(printf '%s\n' "$b") > "$OUT/${safe}.diff" || true
    dl=$(grep -c '^[+-][^+-]' "$OUT/${safe}.diff" || true)
    dbg=$(grep -E '^[+-][^+-]' "$OUT/${safe}.diff" | grep -ciE 'SEGGER_RTT|printf|diag|delay' || true)
    printf "%-46s %-34s %s\n" "$obj" "$fn" "有差异（diff 行 $dl，其中含 RTT/诊断/延时关键词 $dbg）→ ab/${safe}.diff"
    ndbg=$((ndbg+dbg))
  fi
done
printf '%s\n' "---------------------------------------------------------------------------------------------------"
echo "合计 $ntot 个函数：完全一致 $((ntot-ndiff)) / 有差异 $ndiff（差异行中含 RTT/诊断/延时关键词合计 $ndbg 行）"
echo "差异明细目录：$OUT"
