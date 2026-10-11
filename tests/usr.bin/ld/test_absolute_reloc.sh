#!/bin/sh
# A relocation that names no symbol.
#
# `jmp 0x1234` is assembled as a PC-relative relocation with symbol
# number 0: the address is 0x1234 wherever the section is put.  The
# linker took symbol 0, the null symbol every table begins with, for a
# local symbol nobody had defined, and refused the object:
#
#   relocation error: ... symbol=__elfobj_local_0_0_0: the symbol is not
#   defined
#
# The object is assembled by the host's as, so that the test is of the
# linker; the branches must go where the source says.
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
printf '\t.text\n\t.globl _start\n_start:\tnop\n\tjmp 0x1234\n\tcall 0x8049000\n\tje 0x10\n' > a.s

for mode in 32 64; do
    emul=elf_i386
    [ "$mode" = 64 ] && emul=elf_x86_64
    if ! as --$mode -o a.o a.s 2> err; then
        echo "skip: the host's as does not assemble --$mode"; continue
    fi
    readelf -rW a.o | grep -q 'PC32 *\(0*[0-9a-f]*\)\{0,1\}$' ||
        { echo "skip --$mode: the host's as did not write a relocation without a symbol"; continue; }
    if ! ./ld -m $emul -o prog a.o > out 2>&1; then
        echo "FAIL --$mode: refused: $(head -1 out)"; fail=1; continue
    fi
    got=$(objdump -d prog | awk '/jmp|call|je / { printf "%s ", $(NF - 1) }')
    [ "$got" = "1234 8049000 10 " ] ||
        { echo "FAIL --$mode: the branches go to [$got], not to 1234, 8049000 and 10"; fail=1; }
done

[ "$fail" -eq 0 ] && echo "ok: relocations that name no symbol"
exit "$fail"
