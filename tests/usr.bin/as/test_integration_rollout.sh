#!/bin/sh
# From source to a program that runs: a freestanding _start is assembled
# for each of 32 and 64 bits, linked by substrate's linker, and run, and
# its exit status is the one it asked for.
#
# This test used to `make clean` and build the assembler twice in the
# source directory, and to drive the in-tree compiler, which the suite
# does not build.  It uses the assembler and linker it is given, $AS and
# $LD, and builds nothing; what a compiler writes is the business of
# test_compiler_output.sh.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS=${AS:-"$ROOT/usr.bin/as/as"}
if [ -z "${LD:-}" ] || [ ! -x "$LD" ]; then
    echo "ok: integration rollout skipped: no linker in \$LD"
    exit 0
fi
TMP=${TMPDIR:-/tmp}/as-rollout-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

# The status is computed, so that an instruction assembled at the wrong
# width shows: 64-bit registers written with no suffix, and a value that
# does not fit in 32 bits on the way.
cat > "$TMP/start64.s" <<'SRC'
.text
.globl _start
.type _start,@function
_start:
    mov $0x100000029, %rdi
    mov %rdi, %rax
    shr $32, %rax
    add %rax, %rdi
    and $0xff, %rdi
    mov $60, %rax
    syscall
.size _start, .-_start
SRC
cat > "$TMP/start32.s" <<'SRC'
.text
.globl _start
.type _start,@function
_start:
    mov $41, %ebx
    mov $1, %ax
    movzwl %ax, %eax
    add %eax, %ebx
    mov $1, %eax
    int $0x80
.size _start, .-_start
SRC
"$AS" -64 -o "$TMP/start64.o" "$TMP/start64.s"
"$AS" -32 -o "$TMP/start32.o" "$TMP/start32.s"

for obj in "$TMP/start64.o" "$TMP/start32.o"; do
    readelf -h "$obj" | grep -q "Type:[[:space:]]*REL"
    for sec in .text .symtab .strtab .shstrtab; do
        readelf -SW "$obj" | grep -q "[[:space:]]$sec[[:space:]]" ||
            { echo "FAIL: $(basename "$obj") has no $sec"; exit 1; }
    done
    objdump -dr "$obj" > /dev/null
done
readelf -h "$TMP/start64.o" | grep -q "ELF64"
readelf -h "$TMP/start32.o" | grep -q "ELF32"

"$LD" -m64 -o "$TMP/start64.elf" "$TMP/start64.o"
"$LD" -m32 -o "$TMP/start32.elf" "$TMP/start32.o"
readelf -h "$TMP/start64.elf" | grep -q "Type:[[:space:]]*EXEC"
readelf -h "$TMP/start64.elf" | grep -q "Machine:[[:space:]]*Advanced Micro Devices X86-64"
readelf -h "$TMP/start32.elf" | grep -q "Type:[[:space:]]*EXEC"
readelf -h "$TMP/start32.elf" | grep -q "Machine:[[:space:]]*Intel 80386"

# 0x100000029 + 1 = 0x10000002a, and its low byte is 42.
rc=0
"$TMP/start64.elf" || rc=$?
[ "$rc" -eq 42 ] || { echo "FAIL: the 64-bit program exited $rc, not 42"; exit 1; }
# A 32-bit program runs where the kernel has the 32-bit entry; where it
# has not, the exec fails with 126 or 127 and there is nothing to learn.
rc=0
"$TMP/start32.elf" 2> /dev/null || rc=$?
case $rc in
42) ;;
126|127) echo "note: this host does not run 32-bit programs" ;;
*) echo "FAIL: the 32-bit program exited $rc, not 42"; exit 1 ;;
esac

echo "ok: integration and rollout validation"
