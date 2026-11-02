#!/bin/sh
# Padding between input sections in code is NOPs, GNU ld's.
#
# GNU ld fills the gap an input section's alignment leaves in an
# executable output section with no-ops: on x86-64 the 10-byte NOP over
# and over and then the NOP of what is left, on i386 `66 90` over and
# over and a `90` (bfd_arch_i386_fill()).  The linker wrote zeros, which
# a disassembler reading through the padding takes for instructions that
# run over into the next function: Linux's objtool refused the
# vmlinux.o it linked ("xen_asm_exc_divide_error(): can't find starting
# instruction").  Data sections keep their zeros.
#
# GNU ld is the judge: .text and .data of a relocatable link and of an
# executable, for every gap from 1 to 31 bytes, x86-64 and i386, are
# GNU ld's byte for byte.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

if ! ${GNU_LD:-ld} --version 2>/dev/null | grep -q 'GNU ld'; then
    echo "skip: GNU ld is the judge"
    exit 0
fi
if ! ${GNU_AS:-as} --version 2>/dev/null | grep -q 'GNU assembler'; then
    echo "skip: GNU as makes the objects"
    exit 0
fi
${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
# the bytes of section $2 of $1
section() {
    objcopy -O binary --only-section="$2" "$1" "$1$2.bin" 2>/dev/null && od -An -tx1 -v "$1$2.bin" | tr -d ' \n'
}
for m in 64 32; do
    if [ $m = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    gap=1
    while [ $gap -le 31 ]; do
        printf '.text\n.globl _start\n_start:\n.fill %d,1,0xcc\n.data\n.byte 1\n' $((64 - gap)) > a.s
        printf '.text\n.p2align 5\nf:\n.byte 0xc3\n.data\n.p2align 4\n.byte 2\n' > b.s
        ${GNU_AS:-as} --$m -o a.o a.s && ${GNU_AS:-as} --$m -o b.o b.s || { echo "FAIL: as"; exit 1; }
        for kind in -r exe; do
            if [ $kind = -r ]; then opt=-r; else opt="-static -e _start"; fi
            rm -f gnu our
            ${GNU_LD:-ld} -m $emu $opt -o gnu a.o b.o || { echo "FAIL: GNU ld did not link it"; exit 1; }
            if ! ./ld -m $emu $opt -o our a.o b.o; then
                echo "FAIL: $emu $kind gap $gap: not linked"; fail=1; continue
            fi
            for s in .text .data; do
                want=$(section gnu $s)
                got=$(section our $s)
                if [ "$got" != "$want" ]; then
                    echo "FAIL: $emu $kind gap $gap: $s is $got, GNU ld's $want"; fail=1
                fi
            done
        done
        gap=$((gap + 1))
    done
done

if [ $fail = 0 ]; then
    echo "test_code_padding: ok"
fi
exit $fail
