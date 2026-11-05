#!/bin/sh
# Where a script's link puts the sections the linker makes itself and the
# script places nowhere, as GNU ld puts them.
#
# The x86-64 Linux decompressor is linked -pie by a script that puts
# .head.text at 0, and its startup code takes every address as an offset
# from it.  When the script does not discard the dynamic tables, GNU ld
# puts them after the script's sections of their kind -- the read-only
# tables and their relocations after .text, .dynamic after .data -- and
# runs them on from the location counter.  This linker put them in front
# of .head.text, by its own order, and a page apart from the code and
# from each other where the protection changed.
#
# GNU ld is the judge: the allocated sections in order and their
# addresses, up to and including .dynamic (whose size, the tags in it,
# decides what follows), for a -pie and a -shared link, by a script that
# names .data and one that names only the code; and no PT_GNU_RELRO,
# which a script's layout has only from DATA_SEGMENT_RELRO_END.
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
cat > a.s <<'EOF'
.section .head.text,"ax"
.globl startup_32
startup_32: leaq data(%rip), %rax
ret
.text
.globl f
f: ret
.data
data: .quad data
EOF
${GNU_AS:-as} --64 -mx86-used-note=no -o a.o a.s || { echo "FAIL: as"; exit 1; }
cat > data.lds <<'EOF'
SECTIONS {
    . = 0;
    .head.text : { *(.head.text) }
    .text : { *(.text) }
    .data : { *(.data) }
}
EOF
cat > code.lds <<'EOF'
SECTIONS {
    . = 0;
    .head.text : { *(.head.text) }
    .text : { *(.text) }
}
EOF

# the allocated sections of $1 in order with their addresses, up to .dynamic, and its PT_GNU_RELRO
layout() {
    readelf -SW "$1" | awk '/^ *\[ *[0-9]+\]/ { sub(/^ *\[ *[0-9]+\] */, "");
        if ($7 ~ /A/ && !done) print $1, $3; if ($1 == ".dynamic") done = 1 }'
    readelf -lW "$1" | grep -c GNU_RELRO
}

for script in data.lds code.lds; do
    for kind in "-pie --no-dynamic-linker" "-shared"; do
        flags="-m elf_x86_64 $kind --hash-style=both -z noexecstack --no-warn-rwx-segments -e startup_32 -T $script"
        rm -f gnu our
        ${GNU_LD:-ld} $flags -o gnu a.o > gnu.err 2>&1 || { echo "FAIL: GNU ld:"; cat gnu.err; exit 1; }
        if ! ./ld $flags -o our a.o > our.err 2>&1; then
            echo "FAIL $script $kind: not linked:"; cat our.err; fail=1; continue
        fi
        layout gnu > gnu.lay
        layout our > our.lay
        if ! cmp -s gnu.lay our.lay; then
            echo "FAIL $script $kind: not GNU ld's layout:"; diff gnu.lay our.lay; fail=1
        fi
    done
done

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
