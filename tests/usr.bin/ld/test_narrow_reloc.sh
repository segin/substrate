#!/bin/sh
# A byte or a word relocated with a negative addend.
#
# Linux's arch/x86/boot/header.S stores `.byte setup_sects - 1`: an
# R_386_8 whose addend, kept in the byte itself, is 0xff, that is -1.
# The linker read it as 255, and refused the link with a relocation error
# where GNU ld writes setup_sects - 1.  R_386_16 the same.
#
# The object is assembled by the host's as, so that the test is of the
# linker.
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
printf '\t.text\n\t.globl _start\n_start:\tnop\n\t.data\n\t.byte sects - 1\n\t.byte sects + 3\n\t.word words - 1\n' > a.s
if ! as --32 -o a.o a.s 2> err; then
    echo "skip: the host's as does not assemble --32"; exit 0
fi
if ! ./ld -m elf_i386 --defsym sects=0x40 --defsym words=0x1234 -o prog a.o > out 2>&1; then
    echo "FAIL: refused: $(head -1 out)"; exit 1
fi
got=$(objdump -s -j .data prog | awk 'NR > 4 { print $2; exit }')
[ "$got" = "3f433312" ] ||
    { echo "FAIL: .data is [$got], not 3f 43 33 12 (0x40 - 1, 0x40 + 3, 0x1234 - 1)"; fail=1; }

[ "$fail" -eq 0 ] && echo "ok: narrow relocations with a negative addend"
exit "$fail"
