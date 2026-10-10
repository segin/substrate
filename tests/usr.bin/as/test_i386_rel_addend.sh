#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-i386-rel-addend-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/call.s" <<'SRC'
.text
.globl f
.type f,@function
f:
    call ext
    jmp ext
    movl $ext + 4, %eax
    ret
.size f, .-f
SRC

"$AS" -32 -o "$TMP/call.o" "$TMP/call.s"

objdump -dr "$TMP/call.o" > "$TMP/dump.txt"
readelf -x .text "$TMP/call.o" > "$TMP/text.hex"

# i386 uses REL relocations, so the encoded fields must carry the addends.
grep -q "e8 fc ff ff ff" "$TMP/dump.txt"
grep -q "e9 fc ff ff ff" "$TMP/dump.txt"
grep -q "b8 04 00 00 00" "$TMP/dump.txt"
readelf -S "$TMP/call.o" | grep -q "\.rel\.text"
readelf --wide -r "$TMP/call.o" | grep -q "R_386_PLT32"
readelf --wide -r "$TMP/call.o" | grep -q "R_386_PC32"
readelf --wide -r "$TMP/call.o" | grep -q "R_386_32"

echo "ok: i386 REL relocation addends are encoded in-place"
