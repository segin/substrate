#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-local-labels-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/local_labels.s" <<'SRC'
.text
.globl entry
entry:
    jmp 1f
0:  nop
    jmp 0b
1:  jne 2f
    nop
2:  nop
    jmp 1b
3:  nop
    jmp 3b
SRC

check_obj() {
    obj="$1"
    readelf -S "$obj" | grep -q "\\.text"
    readelf -r "$obj" | grep -q "There are no relocations in this file."
}

"$AS" -32 -o "$TMP/labels32.o" "$TMP/local_labels.s"
"$AS" -64 -o "$TMP/labels64.o" "$TMP/local_labels.s"
check_obj "$TMP/labels32.o"
check_obj "$TMP/labels64.o"

echo "ok: numeric local labels"
