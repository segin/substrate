#!/bin/bash
# Usage: symbolize.sh <crash_full.txt>  (must contain MAPS + TRAP backtrace)
CF="$1"
A2L=/opt/substrate/bin/i386-unknown-substrate-addr2line
WORK=/tmp/symwork; mkdir -p "$WORK"
# 1) parse maps -> per-lib base (min start) ; build a region table
declare -A BASE
MAPLINES=$(awk '/=====MAPS/{p=1;next} /MAPS-END/{p=0} p' "$CF")
# regions file: start end name
echo "$MAPLINES" | awk 'NF>=6 && $1 ~ /^[0-9a-f]+-/ {
  split($1,a,"-"); name=$NF; if(name=="0")name="[anon]";
  print a[1], a[2], name }' > "$WORK/regions.txt"
# compute base = min start per name
while read s e n; do
  [ "$n" = "[anon]" ] && continue
  cur=${BASE[$n]}
  if [ -z "$cur" ] || [ $((16#$s)) -lt $((16#$cur)) ]; then BASE[$n]=$s; fi
done < "$WORK/regions.txt"
# dump each needed lib from the image once
declare -A LIBPATH
for n in "${!BASE[@]}"; do
  for d in /usr/lib /lib /usr/dt/lib; do
    debugfs -R "dump $d/$n $WORK/$n" rootfs.img 2>/dev/null
    [ -s "$WORK/$n" ] && { LIBPATH[$n]="$WORK/$n"; break; }
  done
done
# also Xfbdev (the exe, 0x080xxxxx)
debugfs -R "dump /usr/bin/Xfbdev $WORK/Xfbdev" rootfs.img 2>/dev/null
sym() { # $1 = addr (no 0x)
  local addr=$((16#$1)) name base off
  if [ $addr -ge $((0x08000000)) ]; then
    off=$1; printf "    0x%s  Xfbdev!" "$1"; $A2L -f -e "$WORK/Xfbdev" 0x$1 2>/dev/null | tr '\n' ' '; echo; return
  fi
  # find region
  while read s e n; do
    if [ $addr -ge $((16#$s)) ] && [ $addr -lt $((16#$e)) ]; then name=$n; break; fi
  done < "$WORK/regions.txt"
  [ -z "$name" ] || [ "$name" = "[anon]" ] && { printf "    0x%s  [%s]\n" "$1" "${name:-?}"; return; }
  base=${BASE[$name]}; off=$(printf '0x%x' $((addr - 16#$base)))
  printf "    0x%s  %s+%s  " "$1" "$name" "$off"
  [ -n "${LIBPATH[$name]}" ] && $A2L -f -e "${LIBPATH[$name]}" "$off" 2>/dev/null | tr '\n' ' '
  echo
}
echo "=== FAULT ==="
eip=$(grep -aoE 'eip=0x[0-9A-Fa-f]+' "$CF" | head -1 | sed 's/eip=0x//')
[ -n "$eip" ] && sym "$eip"
echo "=== BACKTRACE ==="
grep -aoE 'ret=0x[0-9A-Fa-f]+' "$CF" | sed 's/ret=0x//' | while read a; do sym "$a"; done
