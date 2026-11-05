#!/bin/sh
# A script's /DISCARD/ of the dynamic sections leaves them out of a -pie link.
#
# The x86-64 Linux decompressor is linked -pie --no-dynamic-linker by a
# script that puts .head.text at 0 and discards .dynamic, .dynsym,
# .dynstr, .hash and .gnu.hash; its startup code takes every address as
# an offset from that first section.  GNU ld makes none of those
# sections.  The linker made them after the script's discard had been
# done, so they stayed, and as orphans they went in front of .head.text:
# the startup code was 0x50 bytes from 0 and the kernel's first page
# table was 0x50 bytes off a page.
#
# GNU ld is the judge: the output's allocated sections and their
# addresses, and that neither has a PT_DYNAMIC.
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
# the allocated sections of $1, with their addresses, and its PT_DYNAMIC
layout() {
    readelf -SW "$1" | awk '/^ *\[ *[0-9]+\]/ { sub(/^ *\[ */, ""); sub(/\]/, "");
        if ($2 != "" && $3 != "NULL" && $8 ~ /A/) print $2, $4 }'
    readelf -lW "$1" | grep -c DYNAMIC
}

cat > a.s <<'EOF'
.section .head.text,"ax"
.globl startup_32
startup_32: leaq data(%rip), %rax
ret
.text
.globl f
f: ret
.data
data: .quad 1
.bss
.zero 16
EOF
${GNU_AS:-as} --64 -mx86-used-note=no -o a.o a.s || { echo "FAIL: as"; exit 1; }

for style in sysv gnu both; do
    cat > s.lds <<'EOF'
SECTIONS {
    . = 0;
    .head.text : { *(.head.text) }
    .text : { *(.text) }
    .data : { *(.data) }
    .bss : { *(.bss) }
    /DISCARD/ : { *(.dynamic) *(.dynsym) *(.dynstr) *(.dynbss) *(.hash) *(.gnu.hash) }
}
EOF
    flags="-m elf_x86_64 -pie --no-dynamic-linker --hash-style=$style -z noexecstack --no-warn-rwx-segments -T s.lds"
    ${GNU_LD:-ld} $flags -o gnu a.o 2>gnu.err || { echo "FAIL: GNU ld did not link:"; cat gnu.err; exit 1; }
    if ! ./ld $flags -o our a.o 2>our.err; then
        echo "FAIL: --hash-style=$style: not linked:"; cat our.err; fail=1; continue
    fi
    layout gnu > gnu.lay
    layout our > our.lay
    if ! cmp -s gnu.lay our.lay; then
        echo "FAIL: --hash-style=$style: the layout is not GNU ld's:"; diff gnu.lay our.lay; fail=1
    fi
done

# without the discard both make the dynamic tables
sed '/DISCARD/d' s.lds > k.lds
./ld -m elf_x86_64 -pie --no-dynamic-linker -z noexecstack --no-warn-rwx-segments -T k.lds -o keep a.o 2>keep.err ||
    { echo "FAIL: not linked without the discard:"; cat keep.err; fail=1; }
if [ -f keep ] && ! readelf -SW keep | grep -q ' \.dynamic '; then
    echo "FAIL: no .dynamic without the discard"; fail=1
fi

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
