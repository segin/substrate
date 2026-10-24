#!/bin/sh
# -z max-page-size.
#
# Linux's x86-64 vmlinux is linked with -z max-page-size=0x200000, and its
# decompressor refuses a PT_LOAD aligned to less: "Alignment of LOAD
# segment isn't multiple of 2MB".  The linker warned that the option was
# not supported and aligned every PT_LOAD to 4 KiB.  Each PT_LOAD is
# aligned to the size given now, with its file offset at the same place
# in such a page as its address -- as GNU ld places them -- in a script's
# PHDRS and in the default layout; and CONSTANT(MAXPAGESIZE) is that size.
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
printf '\t.text\n\t.globl _start\n_start:\tret\n\t.data\n\t.quad size\n' > a.s
cat > k.lds <<'EOF2'
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
  . = 0xffffffff81000000;
  .text : AT(0x1000000) { *(.text) } :text
  . = ALIGN(CONSTANT(MAXPAGESIZE));
  .data : AT(0x1200000) { *(.data) } :data
  size = CONSTANT(MAXPAGESIZE);
}
EOF2
if ! as --64 -o a.o a.s 2> err; then
    echo "skip: the host's as does not assemble --64"; exit 0
fi

# each PT_LOAD: its alignment, and its offset and address within such a page
loads() {
    readelf -lW "$1" | awk '$1 == "LOAD" {
        a = strtonum($NF); printf "%x %x %x\n", a, strtonum($2) % a, strtonum($3) % a }'
}

if ! ./ld -m elf_x86_64 -z max-page-size=0x200000 -T k.lds -o prog a.o > out 2>&1; then
    echo "FAIL: script: refused: $(head -1 out)"; exit 1
fi
grep -q "not supported" out && { echo "FAIL: script: $(head -1 out)"; fail=1; }
got=$(loads prog | tr '\n' ' ')
[ "$got" = "200000 0 0 200000 0 0 " ] ||
    { echo "FAIL: script: the PT_LOADs (align, offset, address mod align) are [$got]"; fail=1; }
got=$(objdump -s -j .data prog | awk 'NR > 4 { print $2 $3; exit }')
[ "$got" = "00002000""00000000" ] ||
    { echo "FAIL: CONSTANT(MAXPAGESIZE) is [$got], not 0x200000"; fail=1; }

./ld -m elf_x86_64 -static -z max-page-size=0x200000 --defsym size=0 -o dflt a.o > out 2>&1 ||
    { echo "FAIL: default layout: refused: $(head -1 out)"; exit 1; }
loads dflt | while read -r a o v; do
    [ "$a" = 200000 ] && [ "$o" = "$v" ] ||
        { echo "FAIL: default layout: a PT_LOAD aligned to $a, offset $o and address $v in its page"; exit 1; }
done || fail=1

./ld -m elf_x86_64 -T k.lds -o four a.o > out 2>&1 || { echo "FAIL: 4 KiB: refused: $(head -1 out)"; exit 1; }
got=$(loads four | awk '{ print $1 }' | sort -u)
[ "$got" = 1000 ] || { echo "FAIL: without the option the PT_LOADs are aligned to [$got], not 4 KiB"; fail=1; }

[ "$fail" -eq 0 ] && echo "ok: -z max-page-size"
exit "$fail"
