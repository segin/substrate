#!/bin/sh
# The linker's own relocation, GOT and PLT sections when a -pie script
# discards the dynamic tables, as GNU ld does.
#
# Under --orphan-handling=discard the linker left its .rela.dyn in, and
# a script that discarded .dynamic (as the x86-64 Linux decompressor's
# does) got a .rela.dyn whose sh_entsize was 0, which readelf rejects:
# the record size was only given beside a .dynsym.
#
# GNU ld is the judge:
#  - /DISCARD/ of .dynamic: the RELATIVE records stay, in a .rela.dyn of
#    24-byte records linked to no symbol table;
#  - --orphan-handling=discard: .rela.dyn is left out with them, said
#    nowhere, and the output has GNU ld's sections;
#  - --orphan-handling=discard and a GOT the code needs: the link fails,
#    "discarded output section: `.got.plt'", "final link failed".
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
# the decompressor's shape, with two absolute addresses in .data
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
.quad startup_32
.bss
.zero 16
EOF
# and one that needs a GOT entry (an add takes no relaxing to a lea)
cat > g.s <<'EOF'
.section .head.text,"ax"
.globl startup_32
startup_32: addq f@GOTPCREL(%rip), %rax
ret
.text
.globl f
f: ret
EOF
${GNU_AS:-as} --64 -mx86-used-note=no -o a.o a.s &&
    ${GNU_AS:-as} --64 -mx86-used-note=no -mrelax-relocations=no -o g.o g.s || { echo "FAIL: as"; exit 1; }
cat > dd.lds <<'EOF'
SECTIONS {
    . = 0;
    .head.text : { *(.head.text) }
    .text : { *(.text) }
    .data : { *(.data) }
    .bss : { *(.bss) }
    /DISCARD/ : { *(.dynamic) *(.dynsym) *(.dynstr) *(.dynbss) *(.hash) *(.gnu.hash) }
}
EOF
grep -v DISCARD dd.lds > nd.lds
flags="-m elf_x86_64 -pie --no-dynamic-linker -z noexecstack --no-warn-rwx-segments -e startup_32"

# readelf's section line of $2 in $1: type, entsize, link
shdr() { readelf -SW "$1" | awk -v n="$2" '{ sub(/^ *\[ *[0-9]+\] */, "") } $1 == n { print $2, $6, $8 }'; }
# the dynamic relocations of $1: their offsets from the section they are in aside, their types
rels() { readelf -rW "$1" 2>&1 | awk '/R_X86_64/ { print $3 }'; }
# the allocated sections of $1, by name
asecs() { readelf -SW "$1" | awk '/^ *\[ *[0-9]+\]/ { sub(/^ *\[ *[0-9]+\] */, ""); if ($7 ~ /A/) print $1 }' | sort; }

# /DISCARD/ of .dynamic: the records stay, in a .rela.dyn readelf takes
${GNU_LD:-ld} $flags -T dd.lds -o gnu a.o > gnu.err 2>&1 || { echo "FAIL: GNU ld:"; cat gnu.err; exit 1; }
if ! ./ld $flags -T dd.lds -o our a.o > our.err 2>&1; then
    echo "FAIL /DISCARD/: not linked:"; cat our.err; fail=1
else
    if [ "$(shdr our .rela.dyn)" != "$(shdr gnu .rela.dyn)" ]; then
        echo "FAIL /DISCARD/: .rela.dyn is [$(shdr our .rela.dyn)], GNU ld's [$(shdr gnu .rela.dyn)]"; fail=1
    fi
    if readelf -aW our 2>&1 | grep -q 'Error'; then
        echo "FAIL /DISCARD/: readelf rejects it:"; readelf -aW our 2>&1 | grep Error; fail=1
    fi
    if [ "$(rels our)" != "$(rels gnu)" ]; then
        echo "FAIL /DISCARD/: relocations [$(rels our | tr '\n' ' ')], GNU ld's [$(rels gnu | tr '\n' ' ')]"; fail=1
    fi
fi

# --orphan-handling=discard: GNU ld's sections, and no relocations
${GNU_LD:-ld} $flags --orphan-handling=discard -T nd.lds -o gnu a.o > gnu.err 2>&1 ||
    { echo "FAIL: GNU ld:"; cat gnu.err; exit 1; }
if ! ./ld $flags --orphan-handling=discard -T nd.lds -o our a.o > our.err 2>&1; then
    echo "FAIL discard: not linked:"; cat our.err; fail=1
else
    if [ "$(asecs our | tr '\n' ' ')" != "$(asecs gnu | tr '\n' ' ')" ]; then
        echo "FAIL discard: sections [$(asecs our | tr '\n' ' ')], GNU ld's [$(asecs gnu | tr '\n' ' ')]"; fail=1
    fi
    if [ -n "$(rels our)" ]; then
        echo "FAIL discard: relocations left: $(rels our | tr '\n' ' ')"; fail=1
    fi
    if [ -s our.err ]; then
        echo "FAIL discard: something said:"; cat our.err; fail=1
    fi
fi

# --orphan-handling=discard of a GOT the code needs: GNU ld's failure, in its words
rm -f gnu our
${GNU_LD:-ld} $flags --orphan-handling=discard -T nd.lds -o gnu g.o > gnu.err 2>&1 && { echo "FAIL: GNU ld linked g.o"; exit 1; }
if ./ld $flags --orphan-handling=discard -T nd.lds -o our g.o > our.err 2>&1; then
    echo "FAIL discard of a needed GOT: linked"; fail=1
fi
# (each linker's name off the front: ld, or Ubuntu's .../x86_64-linux-gnu-ld.bfd)
sed -En 's/^[^:]*ld(\.[a-z]+)?: //p' gnu.err > gnu.msg
sed -En 's/^[^:]*ld(\.[a-z]+)?: //p' our.err > our.msg
if ! cmp -s gnu.msg our.msg; then
    echo "FAIL discard of a needed GOT: not GNU ld's words:"; diff gnu.msg our.msg; fail=1
fi

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
