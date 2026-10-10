#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS=${AS:-"$ROOT/usr.bin/as/as"}
LD=${LD:-"$ROOT/usr.bin/ld/ld"}
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
# The branch is within its section: the assembler resolves it and writes
# no relocation.  (This test once wanted an R_X86_64_PC32 against
# .Ltarget.)  What matters is that the program, linked, takes the jump.
readelf --wide -r "$TMP/local_temp_rel64.o" > "$TMP/local_temp_rel64.relocs"
if grep -q 'R_X86_64_' "$TMP/local_temp_rel64.relocs"; then
    echo "a relocation for a branch within the section"
    exit 1
fi

"$LD" -m elf_x86_64 -o "$TMP/local_temp_rel64" "$TMP/local_temp_rel64.o"
objdump -d "$TMP/local_temp_rel64" > "$TMP/local_temp_rel64.dis"
grep -q 'jmp' "$TMP/local_temp_rel64.dis"

set +e
"$TMP/local_temp_rel64"
status=$?
set -e
[ "$status" -eq 0 ]

echo "ok: x86 local temp branch runtime"
