#!/bin/sh
# A loaded section after unloaded ones in the section table still takes its
# place in the file among the loaded ones.
#
# Linux's vmlinux.lds places .modinfo, which is loaded, after the DWARF
# sections.  GNU ld lays the file out with every loaded section first,
# each where its address puts it in its segment, and the rest after them;
# the output writer laid the sections out in table order, so .modinfo went
# past the debugging information, its segment's file size taking that in
# and growing past its memory size.
#
# GNU ld is the judge: the program headers' offsets and sizes and the
# loaded sections' offsets, x86-64 and i386.
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
# the program headers of $1, offsets and sizes
headers() {
    readelf -lW "$1" | awk '/^  LOAD/ { print $1, $2, $3, $5, $6 }'
}
# the loaded sections of $1 and their offsets
loaded() {
    readelf -SW "$1" | awk '{ sub(/^ *\[ *[0-9]+\] */, ""); if ($0 ~ / A/ && $2 == "PROGBITS") print $1, $3, $4 }'
}

cat > a.s <<'EOF'
.text
.globl _start
_start: ret
.data
.quad 1
.section .modinfo,"a"
.asciz "license=GPL"
.section .debug_info,"",@progbits
.fill 0x3000, 1, 0x11
EOF
cat > s.lds <<'EOF'
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x400000;
    .text : { *(.text) } :text
    . = ALIGN(0x1000);
    .data : { *(.data) } :data
    .debug_info 0 : { *(.debug_info) }
    .modinfo : { *(.modinfo) } :data
}
EOF

for m in 64 32; do
    if [ $m = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    ${GNU_AS:-as} --$m -mx86-used-note=no -o a.o a.s || { echo "FAIL: as"; exit 1; }
    rm -f gnu our
    ${GNU_LD:-ld} -m $emu -T s.lds -o gnu a.o || { echo "FAIL: GNU ld did not link"; exit 1; }
    if ! ./ld -m $emu -T s.lds -o our a.o; then
        echo "FAIL: $emu: not linked"; fail=1; continue
    fi
    for what in headers loaded; do
        $what gnu > gnu.$what
        $what our > our.$what
        if ! cmp -s gnu.$what our.$what; then
            echo "FAIL: $emu: the $what are not GNU ld's:"; diff gnu.$what our.$what; fail=1
        fi
    done
done

if [ $fail = 0 ]; then
    echo "test_loaded_after_debug: ok"
fi
exit $fail
