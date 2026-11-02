#!/bin/sh
# An output section nothing goes into still says, with :phdr, which
# program headers the sections after it go in.
#
# Linux's vmlinux.lds has
#     .notes : { ... } :text :note
#     __restore_ph : { *(.__restore_ph) } :text
# where nothing is in __restore_ph: it is there so that what follows goes
# in the text segment only, and not in the PT_NOTE too.  GNU ld takes its
# :text so; the linker made no section of it and passed its :text over,
# and the kernel's .BTF_ids was in its PT_NOTE.
#
# GNU ld is the judge: the program headers and which sections each holds,
# x86-64 and i386.
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
# the program headers of $1, and the sections in each
headers() {
    readelf -lW "$1" | awk '/^  [A-Z]/ && $1 != "Type" { print $1, $3, $5, $6 } /^   [0-9][0-9] / { print }'
}

cat > a.s <<'EOF'
.text
.globl _start
_start: ret
.section .note.a,"a",@note
.balign 4
.long 4, 4, 1
.ascii "GNU\0"
.long 0
.section .after,"a"
.globl after
after: .byte 1, 2, 3, 4
EOF
cat > s.lds <<'EOF'
PHDRS { text PT_LOAD FLAGS(5); note PT_NOTE FLAGS(0); }
SECTIONS {
    . = 0x400000;
    .text : { *(.text) } :text
    .notes : { *(.note.*) } :text :note
    __restore_ph : { *(.__restore_ph) } :text
    .after : { *(.after) }
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
    headers gnu > gnu.ph
    headers our > our.ph
    if ! cmp -s gnu.ph our.ph; then
        echo "FAIL: $emu: the program headers are not GNU ld's:"; diff gnu.ph our.ph; fail=1
    fi
done

if [ $fail = 0 ]; then
    echo "test_script_phdr_empty: ok"
fi
exit $fail
