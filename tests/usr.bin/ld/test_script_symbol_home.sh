#!/bin/sh
# The section of a symbol set in an output section's body where the body
# has made the section longer, and of one set in an output section nothing
# went into.
#
# Linux's vmlinux.lds ends .data with `. = ALIGN(64); _edata = .;` and
# .altinstr_aux with `. = ALIGN(PAGE_SIZE); __inittext_end = .;`, and has
# `.init.begin : { __init_begin = .; }` and `.init.end : { __init_end = .; }`,
# into which nothing goes.  GNU ld puts the first two in the section they
# end, and one in a section that is not made in the section before it or
# the one after, whichever is more like it (fix_syms()): __init_begin in
# the writable .orc_lookup before it rather than the code after it, and
# __init_end in .data_nosave, which it begins, after it.  The linker looked
# for the section the address fell in without the room the body had added,
# so _edata went into the section after .data (whose start it is),
# __inittext_end was absolute, and __init_begin went into the code after.
# nm, and so System.map and the kernel's kallsyms, gave them the wrong
# letters.
#
# GNU ld is the judge: the script's symbols' values and sections, x86-64
# and i386.
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
# the script's symbols in $1: name, value and the name of their section (or ABS)
symbols() {
    readelf -SW "$1" | awk '{ sub(/^ *\[ */, ""); sub(/\] */, " "); if ($1 ~ /^[0-9]+$/) print $1, $2 }' > "$1.secs"
    readelf -sW "$1" | awk -v secs="$1.secs" '
        BEGIN { while ((getline l < secs) > 0) { split(l, f, " "); name[f[1]] = f[2] } }
        $8 ~ /^(_edata|__init_begin|__init_end|__xe|__ds)$/ { print $8, $2, ($7 in name) ? name[$7] : $7 }' | sort
}

cat > a.s <<'EOF'
.text
.globl _start
_start: ret
.data
.quad 1
.section .bt,"a"
.quad 2
.section .text.x,"ax"
nop
.section .zp,"aw"
.quad 3
.section .zn,"aw"
.quad 4
EOF
cat > s.lds <<'EOF'
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x400000;
    .text : { *(.text) } :text
    . = ALIGN(0x1000);
    .data : { __ds = .; *(.data) . = ALIGN(64); _edata = .; } :data
    .bt : { *(.bt) }
    . = ALIGN(16);
    .init.begin : { __init_begin = .; }
    .x : { *(.text.x) . = ALIGN(0x1000); __xe = .; }
    .zp : { *(.zp) }
    . = ALIGN(0x1000);
    .init.end : { __init_end = .; }
    .zn : { *(.zn) }
}
EOF

for m in 64 32; do
    if [ $m = 64 ]; then emu=elf_x86_64; else emu=elf_i386; fi
    ${GNU_AS:-as} --$m -mx86-used-note=no -o a.o a.s || { echo "FAIL: as"; exit 1; }
    rm -f gnu our
    ${GNU_LD:-ld} -m $emu --no-warn-rwx-segments -T s.lds -o gnu a.o || { echo "FAIL: GNU ld did not link"; exit 1; }
    if ! ./ld -m $emu --no-warn-rwx-segments -T s.lds -o our a.o; then
        echo "FAIL: $emu: not linked"; fail=1; continue
    fi
    symbols gnu > gnu.sym
    symbols our > our.sym
    if ! cmp -s gnu.sym our.sym; then
        echo "FAIL: $emu: the script's symbols are not GNU ld's:"; diff gnu.sym our.sym; fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "test_script_symbol_home: ok"
fi
exit $fail
