#!/usr/bin/env bash
# 联调第二轮（2026-09-17 晚）构建证据复跑脚本
#
# 目的：一次性复现本轮三口径构建 + 诊断 A/B，并落盘段尺寸 / md5 / 告警 / 字符串残留检查。
# 用法：bash .analysis/yn_ol/round2_verify.sh 2>&1 | tee .analysis/yn_ol/round2_verify.out.txt
# 注意：脚本会在三种口径间切换（每次切换口径指纹必然全量重编 + 重链接）。
set -u
cd "$(dirname "$0")/../.." || exit 1

SZ() {
  arm-none-eabi-size -A build/Debug/Project_STD.elf |
    awk '/^\.text/{t+=$2} /^\.rodata/{r+=$2} /^\.data/{d+=$2} /^\.bss/{b+=$2} /^\.ccmram/{c+=$2}
         END{printf "text=%-6d rodata=%-6d data=%-5d bss=%-6d ccmram=%-5d\n", t, r, d, b, c}'
}
WARN() { grep -icE "warning|error" "$1"; }
MD5S() { md5sum build/Debug/Project_STD.elf build/Debug/Project_STD.hex build/Debug/Project_STD.bin; }

echo "############ 1/4  APP_DIAG=0（诊断关；用于零开销对照）############"
make -j8 APP_DIAG=0 >.analysis/yn_ol/round2_diagoff.log 2>&1
echo "exit=$? warnings=$(WARN .analysis/yn_ol/round2_diagoff.log)"; SZ; MD5S
echo -n "residual diag strings (expect 0): "
arm-none-eabi-strings build/Debug/Project_STD.elf |
  grep -cE "^\[tcp_srv\]|^\[tcp_cli\]|^\[disp\] probe|notify queue FULL"

echo "############ 2/4  PROTO=ALL DISP=1_263（默认口径，诊断开）############"
make -j8 >.analysis/yn_ol/round2_all_1263.log 2>&1
echo "exit=$? warnings=$(WARN .analysis/yn_ol/round2_all_1263.log)"; SZ; MD5S

echo "############ 3/4  PROTO=ALL DISP=22_1665（工作区 1×5）############"
make -j8 DISP=22_1665 >.analysis/yn_ol/round2_all_221665.log 2>&1
echo "exit=$? warnings=$(WARN .analysis/yn_ol/round2_all_221665.log)"; SZ; MD5S

echo "############ 4/4  回到默认口径（留默认产物在 build/Debug）############"
make -j8 >.analysis/yn_ol/round2_all_1263_final.log 2>&1
echo "exit=$? warnings=$(WARN .analysis/yn_ol/round2_all_1263_final.log)"; SZ; MD5S
echo "############ done ############"
