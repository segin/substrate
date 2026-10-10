#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-indirect-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/x64.s" <<'SRC'
.text
.globl x64_indirect
.type x64_indirect,@function
x64_indirect:
    call *%r11
    jmp *%r11
.size x64_indirect, .-x64_indirect
SRC

cat > "$TMP/x86.s" <<'SRC'
.text
.globl x86_indirect
.type x86_indirect,@function
x86_indirect:
    call *%eax
    jmp *%eax
.size x86_indirect, .-x86_indirect
SRC

"$AS" -64 -o "$TMP/x64.o" "$TMP/x64.s"
"$AS" -32 -o "$TMP/x86.o" "$TMP/x86.s"

objdump -dr "$TMP/x64.o" | grep -q "41 ff d3"
objdump -dr "$TMP/x64.o" | grep -q "41 ff e3"
objdump -dr "$TMP/x86.o" | grep -q "ff d0"
objdump -dr "$TMP/x86.o" | grep -q "ff e0"

echo "ok: x86 indirect call/jmp encoding"
