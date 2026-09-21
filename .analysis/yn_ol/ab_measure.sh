#!/usr/bin/env bash
# 三口径 A/B 增量实测：同口径下「含云南治超模块」与「剔除其 3 个 .o」的链接结果对比
# 用法：bash .analysis/yn_ol/ab_measure.sh
set -u
cd "$(dirname "$0")/../.." || exit 1
OUT=.analysis/yn_ol
mkdir -p "$OUT"

sizes() { arm-none-eabi-size -A "$1" | awk '
  /^\.text /{t=$2} /^\.rodata /{r=$2} /^\.data /{d=$2}
  /^\.ccmram /{c=$2} /^\.bss /{b=$2} /^\._user_heap_stack /{h=$2}
  END{printf "text=%s rodata=%s data=%s ccmram=%s bss=%s heap=%s sram=%s\n", t,r,d,c,b,h,d+b+h}'; }

measure() { # $1=DISP $2=PROTO $3=tag
  local dis="$1" proto="$2" tag="$3"
  echo "===== [$tag] DISP=$dis PROTO=$proto ====="
  make clean >"$OUT/ab_$tag.build.log" 2>&1
  if [ "$proto" = "CQ" ]; then
    make -j8 PROTO=CQ DISP="$dis" >>"$OUT/ab_$tag.build.log" 2>&1
    make -n PROTO=CQ DISP="$dis" 2>/dev/null | grep -m1 -- "-o build/Debug/Project_STD.elf" >"$OUT/ab_$tag.link.txt"
  else
    make -j8 DISP="$dis" >>"$OUT/ab_$tag.build.log" 2>&1
    make -n DISP="$dis" 2>/dev/null | grep -m1 -- "-o build/Debug/Project_STD.elf" >"$OUT/ab_$tag.link.txt"
  fi
  echo "build exit=$? warn=$(grep -c 'warning:' "$OUT/ab_$tag.build.log")"
  echo -n "WITH(elf=$tag):    "; sizes build/Debug/Project_STD.elf
  md5sum build/Debug/Project_STD.elf build/Debug/Project_STD.hex build/Debug/Project_STD.bin

  sed -e "s#-Wl,-Map=build/Debug/Project_STD.map,--cref#-Wl,-Map=$OUT/ab_$tag.map,--cref#" \
      -e "s#-o build/Debug/Project_STD.elf#-o $OUT/ab_${tag}_no_ynol.elf#" \
      -e "s#[^ ]*ProtocolParser_YunNan_Overload/app_yn_ol_proto[a-z_]*\.o##g" \
      "$OUT/ab_$tag.link.txt" >"$OUT/ab_$tag.link_no_ynol.sh"
  bash "$OUT/ab_$tag.link_no_ynol.sh" || { echo "AB link FAILED"; return 1; }
  echo -n "WITHOUT(no yn_ol): "; sizes "$OUT/ab_${tag}_no_ynol.elf"
}

measure 1_263 ALL all_1263
measure 1_263 CQ  cq_1263
measure 22_1665 ALL all_221665
echo "===== DONE ====="
