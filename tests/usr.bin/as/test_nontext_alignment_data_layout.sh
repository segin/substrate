#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-layout-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/layout.s" <<'SRC'
.data
.align 8
a:
    .quad 0
.data
.align 4
i:
    .long 0
.data
.align 8
b:
    .quad 0
.data
.align 8
c:
    .quad 0
.data
.align 8
d:
    .quad 0
.section .rodata
stamp:
    .asciz "XYZ"
.text
    retq
SRC

"$AS" -64 -o "$TMP/layout.o" "$TMP/layout.s"

readelf -s -W "$TMP/layout.o" | grep -q " d$"
readelf -s -W "$TMP/layout.o" | grep -q " stamp$"
readelf -s -W "$TMP/layout.o" | awk '/ stamp$/{print $7}' | grep -qv "^2$"
readelf -x .data "$TMP/layout.o" | grep -q "0x00000020 00000000 00000000"
readelf -x .rodata "$TMP/layout.o" | grep -q "0x00000000 58595a00"

echo "ok: non-text alignment/data layout"
