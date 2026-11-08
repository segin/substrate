#!/bin/sh
# --orphan-handling=warn|error says the sections the linker makes itself
# when the script places them nowhere, as GNU ld does.
#
# It said the inputs' sections only.  The x86-64 Linux decompressor is
# linked -pie by a script that puts .head.text at 0; when that script
# did not discard the dynamic tables, the linker made .dynsym, .dynstr,
# .hash, .gnu.hash, .rela.dyn and .dynamic, placed them as orphans in
# front of .head.text, and --orphan-handling=warn, which Linux passes,
# said nothing of them.
#
# GNU ld is the judge: it makes its sections in the first input and says
# them as that input's, after that input's own and before the next
# input's.  It also says sections this linker does not make (the version
# tables, an empty .got or .plt, two .eh_frame for the PLT's unwind
# information, the input names of its relocation sections such as
# .rela.data); those are left out of the comparison (no input here has
# an .eh_frame of its own), and
# this linker's .rela.dyn, which GNU ld says by those input names, is
# checked on its own.  Under warn the link is made; under error it fails.
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
# the decompressor's shape: .head.text first, and an absolute address in
# .data that a -pie link must relocate at run time
cat > a.s <<'EOF'
.section .head.text,"ax"
.globl startup_32
startup_32: leaq data(%rip), %rax
ret
.data
data: .quad data
.bss
.zero 16
EOF
printf '\t.section .foo,"a"\n\t.long 1\n' > b.s
${GNU_AS:-as} --64 -mx86-used-note=no -o a.o a.s && ${GNU_AS:-as} --64 -mx86-used-note=no -o b.o b.s ||
    { echo "FAIL: as"; exit 1; }
cat > s.lds <<'EOF'
SECTIONS {
    . = 0;
    .head.text : { *(.head.text) }
    .text : { *(.text) }
    .data : { *(.data) }
    .bss : { *(.bss) }
}
EOF

# The orphan lines of $1, the linker's own name taken off, without what
# GNU ld makes and this linker does not, and without .rela.dyn.
orphans() {
    sed -En 's/^[^:]*ld(\.[a-z]+)?: //p' "$1" | grep 'orphan section' |
        grep -Ev "orphan section \`\\.(gnu\\.version.*|plt|got|got\\.plt|plt\\.got|iplt|igot\\.plt|dynbss|data\\.rel\\.ro|eh_frame|rela\\..*)'"
}

for case in "-pie --no-dynamic-linker" "-shared" "--build-id"; do
    for mode in warn error; do
        rm -f gnu our
        flags="-m elf_x86_64 --hash-style=both -z noexecstack --no-warn-rwx-segments --orphan-handling=$mode -T s.lds"
        ${GNU_LD:-ld} $case $flags -o gnu a.o b.o > gnu.err 2>&1
        grc=$?
        ./ld $case $flags -o our a.o b.o > our.err 2>&1
        orc=$?
        [ $grc = 0 ] && grc=made || grc=failed
        [ $orc = 0 ] && orc=made || orc=failed
        if [ $grc != $orc ]; then
            echo "FAIL $case $mode: the link $orc, GNU ld's $grc"; cat our.err; fail=1
        fi
        orphans gnu.err > gnu.o9
        orphans our.err > our.o9
        if ! cmp -s gnu.o9 our.o9; then
            echo "FAIL $case $mode: not GNU ld's orphans:"; diff gnu.o9 our.o9; fail=1
        fi
        if [ "$case" != "--build-id" ]; then
            if [ $mode = warn ]; then
                want="orphan section \`.rela.dyn' from \`a.o' being placed in section \`.rela.dyn'"
            else
                want="unplaced orphan section \`.rela.dyn' from \`a.o'"
            fi
            if ! grep -qF "$want" our.err; then
                echo "FAIL $case $mode: .rela.dyn not said:"; cat our.err; fail=1
            fi
        fi
    done
done

# what the script places, or discards, is not an orphan
cat > all.lds <<'EOF'
SECTIONS {
    . = 0;
    .head.text : { *(.head.text) }
    .text : { *(.text) }
    .dynsym : { *(.dynsym) } .dynstr : { *(.dynstr) } .hash : { *(.hash) } .gnu.hash : { *(.gnu.hash) }
    .rela.dyn : { *(.rela.dyn) }
    .data : { *(.data) *(.foo) }
    .dynamic : { *(.dynamic) }
    .bss : { *(.bss) }
}
EOF
./ld -pie --no-dynamic-linker --hash-style=both -z noexecstack --no-warn-rwx-segments --orphan-handling=error \
    -T all.lds -o our a.o b.o > our.err 2>&1 ||
    { echo "FAIL: a script that places the linker's sections is said to leave orphans:"; cat our.err; fail=1; }

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
