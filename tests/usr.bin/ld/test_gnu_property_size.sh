#!/bin/sh
# The merge of .note.gnu.property over a note or property whose size runs
# past the end.
#
# Reading an input's property notes stepped from one note to the next by
# `off + 12 + namesz`, rounded, and from one property to the next by
# `8 + pr_datasz`, rounded, testing the sum only after it was made.  With
# a 32-bit size_t, as the linker has on Substrate i386, a size near 4 GiB
# wraps those sums: a name size of 0xfffffff4 or a property size of
# 0xfffffff8 moved the walk on by nothing, so the link never finished;
# 0xfffffff0 and 0xfffffff4 moved it back, and a property walk that went
# back past the start of the note read outside the section.  Such a size
# comes straight from an input object.
#
# The linker is built for the host 32-bit here, so its size_t is 32 bits.
# GNU ld is the judge: with each bad note, after a good one and alone,
# linked with an object that has good properties, the link must finish
# (in 20 seconds) and succeed or fail as GNU ld does.  Where only a note's
# size is bad, GNU ld reads the notes before it and the output must have
# the properties GNU ld's has; over a corrupt property GNU ld warns and
# gives up merging, copying the input notes as they are, which is not
# compared.
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
if ! ${CC:-cc} -m32 -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c 2>/dev/null; then
    echo "SKIP: the linker does not build for the host 32-bit"
    exit 0
fi

cd "$work" || exit 1

AND=0xc0000002      # GNU_PROPERTY_X86_FEATURE_1_AND
NEEDED=0xc0008002   # GNU_PROPERTY_X86_ISA_1_NEEDED

# The properties readelf reads from OBJ.
props() {
    readelf -nW "$1" 2>/dev/null | sed -n '/Properties:/,/^$/p' | sed 's/^.*Properties: *//; s/^[[:space:]]*//' | grep .
}

# A good note: two 4-byte properties.
good='	.long 4, 24, 5
	.asciz "GNU"
	.long '$AND', 4, 3
	.long '$NEEDED', 4, 1'

cat > a.s <<EOF
	.section .note.gnu.property,"a",@note
	.p2align 2
$good
	.text
	.globl _start
_start:
	ret
EOF
${CC:-cc} -m32 -c -o a.o a.s || { echo "SKIP: no 32-bit assembler"; exit 0; }

# bad NAME FIRST BODY: an object NAME whose .note.gnu.property holds BODY,
# after the good note unless FIRST is 1
bad() {
    {
        echo '	.section .note.gnu.property,"a",@note'
        echo '	.p2align 2'
        [ "$2" = 1 ] || echo "$good"
        echo "$3"
        echo '	.long 0, 0, 0, 0'
        echo '	.text'
        echo "	.globl f_$1"
        echo "f_$1:	ret"
    } > "$1.s"
    ${CC:-cc} -m32 -c -o "$1.o" "$1.s" || exit 1
}

for first in 1 0; do
    # a note whose name size wraps the note's end to its start, or before it
    bad name0 $first '	.long 0xfffffff4, 0, 5'
    bad nameback $first '	.long 0xfffffff0, 0, 5'
    bad name3 $first '	.long 0xfffffffd, 0, 5'
    # a GNU note with a property whose size wraps the property's end to its start, or before it
    bad prop0 $first '	.long 4, 16, 5
	.asciz "GNU"
	.long '$AND', 0xfffffff8, 3, 0'
    bad propback $first '	.long 4, 16, 5
	.asciz "GNU"
	.long '$AND', 0xfffffff4, 3, 0'
    for o in name0 nameback name3 prop0 propback; do
        gok=0; ook=0
        "${GNU_LD:-ld}" -m elf_i386 -o gnu.out a.o $o.o > gnu.said 2>&1 && gok=1
        timeout 20 ./ld -m elf_i386 -o ours.out a.o $o.o > said 2>&1
        rc=$?
        if [ $rc -eq 124 ] || [ $rc -gt 128 ]; then
            echo "FAIL: $o (first=$first): the link did not finish (status $rc)"; fail=1; continue
        fi
        [ $rc -eq 0 ] && ook=1
        if [ $gok != $ook ]; then
            echo "FAIL: $o (first=$first): ld status $rc, GNU ld's: $(head -1 gnu.said)"; fail=1; continue
        fi
        case $o in prop*) continue ;; esac
        [ $gok = 1 ] || continue
        props gnu.out > want
        props ours.out > have
        if ! cmp -s want have; then
            echo "FAIL: $o (first=$first): properties [$(tr '\n' '|' < have)], GNU ld's [$(tr '\n' '|' < want)]"
            fail=1
        fi
    done
done

[ $fail -eq 0 ] && echo "PASS: test_gnu_property_size"
exit $fail
