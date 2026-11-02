#!/bin/sh
# The .note.gnu.property of an x86 link.
#
# The linker laid every input's note end to end: our cc's was 0xb20
# bytes of notes, one for each object, saying different things.  GNU ld
# merges them into one note: an AND property (the x86 feature_1 bits,
# IBT and SHSTK) is ANDed and gone if any input lacks it, an OR one (ISA
# needed, ISA and features used) ORed; one that comes out 0 is dropped,
# and with nothing left there is no note.
#
# GNU ld is the judge: for inputs with different properties, and some
# with none, -m elf_x86_64 and elf_i386, a program and -r, the
# properties readelf reads from the output are GNU ld's.
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
${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1

# note NAME BITS PROPS...: an object NAME with a property note of the given
# TYPE=VALUE properties (hex), for BITS 32 or 64
note() {
    name=$1; bits=$2; shift 2
    if [ "$bits" = 64 ]; then al=3; else al=2; fi
    {
        echo '	.section .note.gnu.property,"a",@note'
        echo "	.p2align $al"
        echo '	.long 4'
        echo '	.long 1f - 0f'
        echo '	.long 5'
        echo '	.asciz "GNU"'
        echo '0:'
        for p in "$@"; do
            echo "	.long ${p%=*}"
            echo '	.long 4'
            echo "	.long ${p#*=}"
            echo "	.p2align $al"
        done
        echo '1:'
        echo '	.text'
        echo "	.globl f_$name"
        echo "f_$name:	ret"
    } > "$name.s"
    ${AS_GNU:-as} --$bits -o "$name.o" "$name.s" || exit 1
}

AND=0xc0000002      # GNU_PROPERTY_X86_FEATURE_1_AND
NEEDED=0xc0008002   # GNU_PROPERTY_X86_ISA_1_NEEDED
USED=0xc0010002     # GNU_PROPERTY_X86_ISA_1_USED
FUSED=0xc0010001    # GNU_PROPERTY_X86_FEATURE_2_USED

# The properties readelf reads from OBJ.
props() {
    readelf -nW "$1" 2>/dev/null | sed -n '/Properties:/,/^$/p' | sed 's/^.*Properties: *//; s/^[[:space:]]*//' | grep .
}

# check BITS ARGS... OBJS: GNU ld and this one say the same
check() {
    bits=$1; shift
    if [ "$bits" = 64 ]; then em=elf_x86_64; else em=elf_i386; fi
    entry=
    for a in "$@"; do
        case $a in *.o) entry=f_${a%.o}; break ;; esac
    done
    "${GNU_LD:-ld}" -m $em -e $entry -o gnu.out "$@" 2>/dev/null || { echo "skip: GNU ld $*"; return; }
    if ! ./ld -m $em -e $entry -o ours.out "$@" > said 2>&1; then
        echo "FAIL: ld -m $em $*: $(head -1 said)"; fail=1; return
    fi
    props gnu.out > want
    props ours.out > have
    if ! cmp -s want have; then
        echo "FAIL: ld -m $em $*: properties [$(tr '\n' '|' < have)], GNU ld's [$(tr '\n' '|' < want)]"
        fail=1
    fi
}

for b in 64 32; do
    note a $b $AND=3 $NEEDED=1 $USED=1 $FUSED=1
    note b $b $AND=1 $NEEDED=2 $USED=2
    note c $b $NEEDED=1 $FUSED=2
    note d $b $AND=3
    { echo '	.text'; echo '	.globl f_e'; echo 'f_e:	ret'; } > e.s
    ${AS_GNU:-as} --$b -o e.o e.s || exit 1

    check $b a.o b.o            # AND to 1, OR to 3
    check $b a.o d.o            # AND kept, the ORs only a's
    check $b a.o c.o            # AND gone: c has none
    check $b a.o e.o            # e has no note: AND gone, ORs kept
    check $b e.o a.o            # the first with no note
    check $b d.o a.o b.o c.o
    check $b -r a.o b.o
    check $b -r a.o e.o
    check $b e.o                # no note at all
done

if [ $fail -eq 0 ]; then
    echo "ok   test_gnu_property"
fi
exit $fail
