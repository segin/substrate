#!/bin/sh
# Empty arguments keep their places, and an alignment has a maximum
# (AS-T-025, AS-T-026).
#
# gcc writes `.p2align 4,,10` before every loop and function at -O2: align
# to 16, with the default fill, unless that takes more than 10 bytes.  The
# parser dropped the empty argument, so the directive read `.p2align 4,10`
# and the gap was filled with bytes of 0x0a -- `or (%edx),%cl`, run on the
# way into the loop.  And nothing knew of a maximum at all.
#
# What is compared with GNU as is how much padding there is, and the fill
# where one is given.  In code with no fill the padding is NOPs of some
# kind; which kind is AS-T-251.  Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

# bytes WHAT SECTION SOURCE BYTES
bytes() {
    printf "$3" > t.s
    if ! "$AS" --32 -o t.o t.s 2> err; then
        echo "FAIL $1: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    objcopy -O binary -j "$2" t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$4" ] || { echo "FAIL $1: $got, not $4"; fail=1; }
}

# length WHAT SOURCE N: .text is N bytes long.
length() {
    printf "$2" > t.s
    if ! "$AS" --32 -o t.o t.s 2> err; then
        echo "FAIL $1: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    objcopy -O binary -j .text t.o t.bin
    got=$(wc -c < t.bin | tr -d ' ')
    [ "$got" = "$3" ] || { echo "FAIL $1: .text is $got bytes, not $3"; fail=1; }
    if od -An -v -tx1 t.bin | grep -q ' 0a'; then echo "FAIL $1: padded with 0a"; fail=1; fi
}

# 15 bytes would be needed, and 10 is the most: none.
length "p2align 4,,10 one byte in"   '.text\nnop\n.p2align 4,,10\nnop\n'            2
# 15 is allowed: padded to 16, then the nop.
length "p2align 4,,15 one byte in"   '.text\nnop\n.p2align 4,,15\nnop\n'            17
length "p2align 2,,2 needs 3"        '.text\nnop\n.p2align 2,,2\nnop\n'             2
length "p2align 2,,3 needs 3"        '.text\nnop\n.p2align 2,,3\nnop\n'             5
# Six bytes in, ten to go: just allowed.
length "p2align 4,,10 six bytes in"  '.text\n.skip 6,0x90\n.p2align 4,,10\nnop\n'   17
length "p2align 4,,10 five bytes in" '.text\n.skip 5,0x90\n.p2align 4,,10\nnop\n'   6

bytes "balign 8,,3 needs 7"   .data '.data\n.byte 1\n.balign 8,,3\n.byte 2\n'       0102
bytes "balign 4,,3 needs 3"   .data '.data\n.byte 1\n.balign 4,,3\n.byte 2\n'       0100000002
bytes "a fill and a maximum"  .data '.data\n.byte 1\n.balign 4,0x55,3\n.byte 2\n'   0155555502
bytes "the maximum exceeded"  .data '.data\n.byte 1\n.balign 4,0x55,2\n.byte 2\n'   0102
bytes "a fill, no maximum"    .data '.data\n.byte 1\n.p2align 2,0x55\n.byte 2\n'    0155555502
bytes ".align 4,,1"           .data '.data\n.byte 1\n.align 4,,1\n.byte 2\n'        0102
bytes "both empty"            .data '.data\n.byte 1\n.p2align 3,,\n.byte 2\n'       010000000000000002
bytes "no arguments after"    .data '.data\n.byte 1\n.p2align 3\n.byte 2\n'         010000000000000002

# In a data directive GNU as takes an empty argument for zero and warns.
# This assembler has no warnings yet (AS-T-064) and refuses it, which at
# least is not `.byte 1,,2` assembled as 01 02.
printf '.data\n.byte 1,,2\n' > t.s
if "$AS" --32 -o t.o t.s 2> /dev/null; then
    objcopy -O binary -j .data t.o t.bin
    [ "$(od -An -v -tx1 t.bin | tr -d ' \n')" = 010002 ] ||
        { echo "FAIL .byte 1,,2: $(od -An -v -tx1 t.bin | tr -d ' \n')"; fail=1; }
fi

[ "$fail" -eq 0 ] && echo "ok: alignment maximum and empty arguments"
exit "$fail"
