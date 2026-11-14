#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CPIO=${CPIO:-"$ROOT/usr.bin/cpio/cpio"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src"
if [ "$(id -u)" -eq 0 ]; then
  mknod "$TMP/src/nullnode" c 1 3
  printf 'src/nullnode\n' | (cd "$TMP" && "$CPIO" -o -H newc -F a.cpio)
  mkdir -p "$TMP/out"
  (cd "$TMP/out" && "$CPIO" -i -d -F "$TMP/a.cpio")
  test -c "$TMP/out/src/nullnode"
else
  echo "non-root: device-node create test skipped"
fi

# A device number past 8 bits each way survives copy-out: the newc
# header's rdevmajor and rdevminor (hex, at bytes 78 and 86) are the
# node's own.  Any such node in /dev will do; lstat needs no access.
node=
for n in /dev/* /dev/*/*; do
  if [ -b "$n" ] || [ -c "$n" ]; then
    ma=$((16#$(stat -c %t "$n"))); mi=$((16#$(stat -c %T "$n")))
    if [ "$ma" -gt 255 ] || [ "$mi" -gt 255 ]; then node=$n; break; fi
  fi
done
if [ -n "$node" ]; then
  printf '%s\n' "$node" | "$CPIO" -o -H newc -F "$TMP/dev.cpio" 2>/dev/null
  got_ma=$((16#$(dd if="$TMP/dev.cpio" bs=1 skip=78 count=8 status=none)))
  got_mi=$((16#$(dd if="$TMP/dev.cpio" bs=1 skip=86 count=8 status=none)))
  if [ "$got_ma,$got_mi" != "$ma,$mi" ]; then
    echo "FAIL: $node is $ma,$mi; the archive says $got_ma,$got_mi"
    exit 1
  fi
else
  echo "no device number past 8 bits in /dev: wide-number test skipped"
fi
