#!/bin/sh
# --emit-relocs: relocations kept in an executable say where by address.
#
# GNU ld writes r_offset as the place's address once the sections have
# addresses; this linker wrote the offset in the section.  Linux builds
# the real-mode trampoline's fixup list from these
# (arch/x86/tools/relocs): at section offsets it patched the wrong
# words, and setup_real_mode() wrote through a header field 0xb0000000
# too far.  GNU ld passes this test.
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
printf '\t.text\n\t.globl _start\n_start:\tnop\n\tnop\n\tnop\n\tnop\n\t.long target\n\t.data\ntarget:\t.long 0\n' > a.s
if ! as --32 -o a.o a.s 2> err; then
    echo "skip: the host's as does not assemble --32"; exit 0
fi
printf 'SECTIONS { . = 0x1000; .text : { *(.text) } .data : { *(.data) } }\n' > a.lds
LD=${LD_UNDER_TEST:-./ld}
if ! "$LD" -m elf_i386 --emit-relocs -T a.lds -o prog a.o > out 2>&1; then
    echo "FAIL: refused: $(head -1 out)"; exit 1
fi
got=$(readelf -rW prog | awk '/R_386_32/ { print $1; exit }')
[ "$got" = "00001004" ] ||
    { echo "FAIL: the kept relocation is at [$got], not 00001004 (.text at 0x1000, the word at 4)"; fail=1; }

[ "$fail" -eq 0 ] && echo "ok: --emit-relocs keeps relocations by address"
exit "$fail"
