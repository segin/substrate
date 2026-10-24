#!/bin/sh
# A sign-extended 32-bit relocation of an address in the top 2 GiB, and
# a PC-relative one of an address below 0.
#
# Linux's x86-64 kernel is linked at 0xffffffff81000000, and its code
# takes symbols' addresses as sign-extended immediates (R_X86_64_32S,
# `movq $sym, %rax`, entry_64.S's `cmpq $.Lgs_change, ...`).  The linker
# summed the address and the addend in more than 64 bits, took the
# address for a number past 2^63, and refused the link:
#
#   relocation error: ... type=R_X86_64_32S ...: relocation error
#
# So with R_X86_64_PC32 in Linux's vDSO, a shared object at 0 whose data
# its script places below it (`PROVIDE(vdso_u_data = . - 6 * PAGE_SIZE)`).
#
# The object is assembled by the host's as, so that the test is of the
# linker; each immediate must be the address's low 32 bits.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
printf '\t.section .entry.text,"ax"\n\t.globl _start\n_start:\tcmpq $1f, 8(%%rsp)\n\tmovq $_start, %%rax\n1:\tret\n' > a.s
printf 'SECTIONS {\n  . = 0xffffffff81000000;\n  .text : { *(.text) *(.entry.text) }\n}\n' > k.lds
if ! as --64 -o a.o a.s 2> err; then
    echo "skip: the host's as does not assemble --64"; exit 0
fi
if ! ./ld -m elf_x86_64 -T k.lds -o prog a.o > out 2>&1; then
    echo "FAIL: refused: $(head -1 out)"; exit 1
fi
# cmpq $imm32, 8(%rsp) is 48 81 7c 24 08 imm32 (9 bytes); movq $imm32, %rax 48 c7 c0 imm32 (7)
got=$(objdump -d --insn-width=16 prog | grep -oE '(48 81 7c 24 08|48 c7 c0)( [0-9a-f]{2}){4}' | tr '\n' ' ')
want="48 81 7c 24 08 10 00 00 81 48 c7 c0 00 00 00 81 "
[ "$got" = "$want" ] ||
    { echo "FAIL: the immediates are [$got], not [$want]"; fail=1; }

printf '\t.text\n\t.globl f\n\t.hidden below\nf:\tleaq below(%%rip), %%rax\n\tret\n' > b.s
printf 'SECTIONS {\n  PROVIDE(below = . - 0x4000);\n  . = SIZEOF_HEADERS;\n  .text : { *(.text) }\n}\n' > v.lds
as --64 -o b.o b.s
if ! ./ld -m elf_x86_64 -shared -T v.lds -o lib.so b.o > out 2>&1; then
    echo "FAIL: below 0: refused: $(head -1 out)"; exit 1
fi
want=$(nm lib.so | awk '$3 == "below" { print $1 }' | sed 's/^0*//')
got=$(objdump -d lib.so | sed -n 's/.*lea .*(%rip),%rax *# *\([0-9a-f]*\) <below>.*/\1/p')
[ -n "$want" ] && [ "$got" = "$want" ] ||
    { echo "FAIL: below 0: the lea reaches [$got], not below at [$want]"; fail=1; }

[ "$fail" -eq 0 ] && echo "ok: sign-extended relocations in the top 2 GiB, PC-relative ones below 0"
exit "$fail"
