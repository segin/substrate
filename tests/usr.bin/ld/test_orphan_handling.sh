#!/bin/sh
# --orphan-handling=place|warn|error|discard, as GNU ld does it.
#
# The option was taken as an unknown one and ignored ("unsupported option
# ignored"), and Linux's build passes --orphan-handling=warn to every link
# of vmlinux and its decompressor: the warning that would have said the
# dynamic tables had landed in front of the decompressor's .head.text was
# never made.
#
# GNU ld is the judge: under warn and error the input sections a script
# places nowhere, each said in GNU ld's words, input by input and in each
# input's order (GNU ld says too the sections it makes itself and places
# nowhere, such as an empty .got, which this linker does not make: those
# are left out of the comparison); error fails the link; discard leaves
# the orphans out; place says nothing; a mode it does not know is refused;
# a script that places everything has no orphan; and -r has them too.
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
	.text
	.globl _start
_start:	call f
	.section .foo,"a"
	.long 1
	.section .empty,"a"
	.section .nb,""
	.long 2
	.section .note.x,"a",@note
	.long 0, 0, 0
	.section .bar,"aw",@nobits
	.zero 4
	.section .data.x,"aw"
	.long _start
EOF
printf '\t.globl f\n\t.text\nf:\tret\n' > b.s
${GNU_AS:-as} --64 -o a.o a.s && ${GNU_AS:-as} --64 -o b.o b.s || { echo "FAIL: as"; exit 1; }
echo 'SECTIONS { .text : { *(.text) } .data : { *(.data*) } }' > s.lds

# The orphan lines of $1, the linker's own name taken off, without the
# sections GNU ld makes for itself.
orphans() {
    sed -n 's/^[^:]*ld: //p' "$1" | grep 'orphan section' |
        grep -Ev "orphan section \`\\.(got|got\\.plt|rela\\.got|iplt|rela\\.iplt|igot\\.plt|interp|gnu\\.version.*)'"
}
secs() { readelf -SW "$1" | awk '/^ *\[ *[0-9]+\]/ { sub(/^ *\[ *[0-9]+\] */, ""); print $1 }' | grep -v '^NULL$'; }

for mode in warn error discard place; do
    rm -f gnu our
    ${GNU_LD:-ld} --no-warn-rwx-segments --orphan-handling=$mode -T s.lds -o gnu a.o b.o > gnu.err 2>&1
    grc=$?
    ./ld --no-warn-rwx-segments --orphan-handling=$mode -T s.lds -o our a.o b.o > our.err 2>&1
    orc=$?
    if grep -q 'unsupported option' our.err; then
        echo "FAIL $mode: the option is not taken:"; cat our.err; fail=1; continue
    fi
    [ $grc = 0 ] && grc=ok || grc=failed
    [ $orc = 0 ] && orc=ok || orc=failed
    if [ $grc != $orc ]; then
        echo "FAIL $mode: the link $orc, GNU ld's $grc"; cat our.err; fail=1
    fi
    orphans gnu.err > gnu.o9
    orphans our.err > our.o9
    if ! cmp -s gnu.o9 our.o9; then
        echo "FAIL $mode: not GNU ld's orphans:"; diff gnu.o9 our.o9; fail=1
    fi
    if [ $mode = discard ] && [ "$(secs our | sort | tr '\n' ' ')" != "$(secs gnu | sort | tr '\n' ' ')" ]; then
        echo "FAIL discard: sections $(secs our | sort | tr '\n' ' '), GNU ld's $(secs gnu | sort | tr '\n' ' ')"
        fail=1
    fi
done

# -r says them too
rm -f gnu.r our.r
${GNU_LD:-ld} -r --orphan-handling=warn -T s.lds -o gnu.r a.o b.o > gnu.err 2>&1
./ld -r --orphan-handling=warn -T s.lds -o our.r a.o b.o > our.err 2>&1 || { echo "FAIL -r: not linked:"; cat our.err; fail=1; }
orphans gnu.err > gnu.o9
orphans our.err > our.o9
if ! cmp -s gnu.o9 our.o9; then
    echo "FAIL -r: not GNU ld's orphans:"; diff gnu.o9 our.o9; fail=1
fi

# a script that places everything has none
cat > all.lds <<'EOF'
SECTIONS { .text : { *(.text) } .data : { *(.data*) *(.foo) *(.empty) } .note : { *(.note.*) } .bss : { *(.bss) *(.bar) }
           /DISCARD/ : { *(.nb) } }
EOF
./ld --no-warn-rwx-segments --orphan-handling=error -T all.lds -o our a.o b.o > our.err 2>&1 ||
    { echo "FAIL: a script that places everything is said to leave orphans:"; cat our.err; fail=1; }

# a mode that is not one
if ./ld --orphan-handling=bogus -T s.lds -o our a.o b.o > our.err 2>&1; then
    echo "FAIL: --orphan-handling=bogus taken"; fail=1
elif ! grep -q 'invalid argument to option "--orphan-handling"' our.err; then
    echo "FAIL: --orphan-handling=bogus: not GNU ld's words:"; cat our.err; fail=1
fi

if [ $fail = 0 ]; then
    echo "PASS"
fi
exit $fail
