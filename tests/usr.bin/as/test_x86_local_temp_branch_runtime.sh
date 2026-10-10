#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
LD="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/as-local-temp-branch-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/local_temp_rel64.s" <<'SRC'
.text
.globl _start
.type _start,@function
_start:
    jmp .Ltarget
    mov $42, %edi
    mov $60, %eax
    syscall
    .fill 64,1,0x90
.Ltarget:
    mov $0, %edi
    mov $60, %eax
    syscall
.size _start, .-_start

.globl tail
.type tail,@function
tail:
    mov $99, %edi
    mov $60, %eax
    syscall
.size tail, .-tail
SRC

"$AS" -64 -o "$TMP/local_temp_rel64.o" "$TMP/local_temp_rel64.s"
readelf --wide -r "$TMP/local_temp_rel64.o" > "$TMP/local_temp_rel64.relocs"
grep -q 'R_X86_64_PC32' "$TMP/local_temp_rel64.relocs"
grep -q '.Ltarget - 4' "$TMP/local_temp_rel64.relocs"

"$LD" -m elf_x86_64 -o "$TMP/local_temp_rel64" "$TMP/local_temp_rel64.o"
objdump -d "$TMP/local_temp_rel64" > "$TMP/local_temp_rel64.dis"
grep -q 'jmp' "$TMP/local_temp_rel64.dis"
grep -q '<.Ltarget>' "$TMP/local_temp_rel64.dis"

set +e
"$TMP/local_temp_rel64"
status=$?
set -e
[ "$status" -eq 0 ]

echo "ok: x86 local temp branch runtime"
