#!/bin/sh
# The numbers that directives take: alignments, counts and fills, and
# what a `.section` is told of its entries and its group (AS-T-245, 246,
# 248, 249, 250).
#
# - An alignment was checked in three places that did not agree: none
#   at all and `.align 0` were errors.  It is checked once, before any
#   of them: none or 0 is 1, it is a power of two, and 2^31 is the most.
# - A symbol set to a number was not one to these directives:
#   `.set A,4` / `.align A` was "malformed section directive", `.skip
#   K*2` and `.space K` of such a K were nothing at all, `.fill K` and
#   `.org K` were errors.
# - A count that is no number was taken for none by `.skip` and
#   `.space`: `.skip undefined` assembled.  It is refused, as it was for
#   `.zero`.  One below nothing is warned of and passed over.
# - `.section` with `M` and no entry size, with `G` and no group, or
#   with a type that is none, said nothing; each is GNU as's warning,
#   and the section is without the flag.
#
# The bytes of .data and the warnings are GNU as's, and GNU as passes
# this test but for the one case marked.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0
gnu=0
"$AS" --version 2> /dev/null | grep '^GNU assembler' | grep -qv '(Substrate)' && gnu=1

# is SOURCE BYTES, in both modes: `|` in SOURCE is a newline
is() {
    for m in 32 64; do
        cases=$((cases + 1))
        { echo .data; printf '%s\n' "$1" | tr '|' '\n'; } > t.s
        rm -f t.o
        if ! "$AS" --$m -o t.o t.s 2> err; then
            echo "FAIL --$m [$1]: $(tail -1 err)"; fail=1; continue
        fi
        bytes=$(objcopy -O binary -j .data t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
        [ "$bytes" = "$2" ] || { echo "FAIL --$m [$1]: bytes $bytes, not $2"; fail=1; }
    done
}

refused() {
    for m in 32 64; do
        cases=$((cases + 1))
        { echo .data; printf '%s\n' "$1" | tr '|' '\n'; } > t.s
        if "$AS" --$m -o t.o t.s > /dev/null 2> err; then
            echo "FAIL --$m [$1]: assembled"; fail=1
        elif ! grep -q '^t\.s:[0-9]' err; then
            echo "FAIL --$m [$1]: refused without a line: $(tail -1 err)"; fail=1
        fi
    done
}

# warned SOURCE MESSAGE BYTES: said on line 2, and the rest assembled
warned() {
    cases=$((cases + 1))
    { echo .data; printf '%s\n' "$1" | tr '|' '\n'; } > t.s
    rm -f t.o
    "$AS" --64 -o t.o t.s 2> err
    if [ ! -f t.o ]; then
        echo "FAIL [$1]: $(tail -1 err)"; fail=1; return
    fi
    grep -q "^t\.s:2: Warning: $2\$" err || { echo "FAIL [$1]: said $(tr '\n' '|' < err)"; fail=1; }
    bytes=$(objcopy -O binary -j .data t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    [ "$bytes" = "$3" ] || { echo "FAIL [$1]: bytes $bytes, not $3"; fail=1; }
}

# An alignment of none is the least there is.
is '.byte 1|.align 0|.byte 2' '0102'
is '.byte 1|.p2align 0|.byte 2' '0102'
is '.byte 1|.balign 1|.byte 2' '0102'
is '.byte 1|.balign 0|.byte 2' '0102'
is '.byte 1|.p2align|.byte 2' '0102'
is '.byte 1|.balign|.byte 2' '0102'
is '.byte 1|.align|.byte 2' '0102'
is '.byte 1|.balign 4|.byte 2' '0100000002'
is '.byte 1|.p2align 3|.byte 2' '010000000000000002'
is '.byte 1|.align 4,0x11|.byte 2' '0111111102'
is '.byte 1|.p2align 3,0xcc,2|.byte 2' '0102'
is '.byte 1|.p2align 2,0xcc,3|.byte 2' '01cccccc02'
refused '.byte 1|.balign 3|.byte 2'
refused '.byte 1|.align 6|.byte 2'
refused '.byte 1|.balign -4|.byte 2'

# The widest here is 2^20, which is as wide as the object library
# writes a section; one more is refused.  (GNU as goes to 2^31, or 2^63
# in a 64-bit object, and takes more for that with a warning.)
for d in '.p2align 20' '.balign 0x100000'; do
    cases=$((cases + 1))
    printf '\t.data\n\t%s\n\t.byte 1\n' "$d" > t.s
    rm -f t.o
    "$AS" --64 -o t.o t.s 2> err
    a=$(readelf -SW t.o 2> /dev/null | awk '$2 == ".data" || $3 == ".data" { print $NF }')
    [ "$a" = 1048576 ] || { echo "FAIL [$d]: .data is aligned to '$a' $(tail -1 err)"; fail=1; }
done
if [ "$gnu" = 0 ]; then
    refused '.p2align 21|.byte 1'
    refused '.balign 0x200000|.byte 1'
fi

# A symbol set to a number is its number.
is '.set A,4|.byte 1|.align A|.byte 2' '0100000002'
is '.set A,2|.byte 1|.p2align A|.byte 2' '0100000002'
is '.set A,4|.byte 1|.balign A*2|.byte 2' '010000000000000002'
is '.set A,4|.set F,0x22|.byte 1|.balign A,F|.byte 2' '0122222202'
is '.set e1,0|.set e2,3|.zero e2-e1' '000000'
is '.set K,3|.skip K*2' '000000000000'
is '.set K,3|.fill K,1,0x66' '666666'
is '.set K,3|.space K,0x11' '111111'
is '.set K,3|.byte 1|.org K|.byte 2' '01000002'
is '.set K,3|.fill 2,K,0x66' '660000660000'
is '.set K,0x66|.fill 2,1,K' '6666'
is '.set K,0x11|.skip 2,K' '1111'
is 'K = 2|.skip K|K = 3|.skip K,1' '0000010101'
is '.set K,2|.set J,K+1|.fill J,1,7' '070707'
# A count may be set further down; an alignment may not.
is '.skip later|.set later,3' '000000'
refused '.byte 1|.align B|.byte 2|.set B,4'
refused '.byte 1|.align und|.byte 2'

# A count is a number, or the difference of two places.
is 'a: .byte 1,2|b: .skip b-a,5' '01020505'
is '.skip b-a,6|a: .byte 1,2|b:' '06060102'
is '.fill b-a,1,0x55|a: .byte 1,2|b:' '55550102'
refused '.zero und'
refused '.skip und'
refused '.space und'
refused '.fill und,1,0'
refused '.zero 2,3,4'

# What is below nothing is said and passed over.
warned '.skip -1|.byte 1' '\.space repeat count is negative, ignored' '01'
warned '.zero -3|.byte 1' '\.space repeat count is negative, ignored' '01'
warned '.space -1,5|.byte 1' '\.space repeat count is negative, ignored' '01'
warned '.fill -2,1,1|.byte 1' 'repeat < 0; \.fill ignored' '01'
warned '.fill 2,-1,1|.byte 1' 'size negative; \.fill ignored' '01'
warned '.fill 2,9,1' '\.fill size clamped to 8' '01000000000000000100000000000000'

# What GNU as makes of the rest.
is '.zero 2,3' '0303'
is '.fill 2,0,1' ''
is '.fill 3' '000000'
is '.fill 2,2' '00000000'
is '.fill 2,8,0x1122334455667788' '88776655000000008877665500000000'
is '.fill 2,4,0x11223344' '4433221144332211'
is '.fill 2,3,0x11223344' '443322443322'
is '.org 2|.byte 1' '000001'
is '.byte 1|.org .+3|.byte 2' '0100000002'
is '.byte 1|.org 0x8,0xcc|.byte 2' '01cccccccccccccc02'
refused '.byte 1,2,3|.org 1'

# section SOURCE-LINE MESSAGE TYPE FLAGS: what .a is, and what was said
section() {
    cases=$((cases + 1))
    printf '\t.data\n\t%s\n\t.byte 1\n' "$1" > t.s
    rm -f t.o
    "$AS" --64 -o t.o t.s 2> err
    if [ ! -f t.o ]; then
        echo "FAIL [$1]: $(tail -1 err)"; fail=1; return
    fi
    if [ -n "$2" ]; then
        grep -q "^t\.s:2: Warning: $2\$" err || { echo "FAIL [$1]: said $(tr '\n' '|' < err)"; fail=1; }
    elif [ -s err ]; then
        echo "FAIL [$1]: said $(tr '\n' '|' < err)"; fail=1
    fi
    got=$(readelf -SW t.o | grep ' \.a ' | sed 's/.*\] //' | awk '{ print $2, ($7 ~ /^[A-Za-z]+$/ ? $7 : "-") }')
    [ "$got" = "$3 $4" ] || { echo "FAIL [$1]: .a is $got, not $3 $4"; fail=1; }
}
section '.section .a,"a",@bogus' 'unrecognized section type' PROGBITS A
section '.section .a,"aM",@progbits' 'entity size for SHF_MERGE not specified' PROGBITS A
section '.section .a,"aM"' 'entity size for SHF_MERGE not specified' PROGBITS A
section '.section .a,"aG",@progbits' 'group name for SHF_GROUP not specified' PROGBITS A
section '.section .a,"aMG",@progbits,4' 'group name for SHF_GROUP not specified' PROGBITS AM
section '.pushsection .a,"aG"' 'group name for SHF_GROUP not specified' PROGBITS A
section '.section .a,"aMS",@progbits,1' '' PROGBITS AMS
section '.section .a,"axG",@progbits,g,comdat' '' PROGBITS AXG
section '.section .a,"awo",@progbits,.data' '' PROGBITS WAL
section '.section .a,"a",@note' '' NOTE A
section '.section .a,"aw",@init_array' '' INIT_ARRAY WA
section '.section .a,"aw",@fini_array' '' FINI_ARRAY WA
section '.section .a,"aw",@preinit_array' '' PREINIT_ARRAY WA
section '.section .a,"a",@unwind' '' X86_64_UNWIND A
section '.section .a,"a",@0x7000000a' '' LOPROC+0xa A
section '.section .a,"a",%progbits' '' PROGBITS A
section '.section .a,"a","progbits"' '' PROGBITS A

# A letter that is no flag is refused.
refused '.section .a,"aq"|.byte 1'

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
