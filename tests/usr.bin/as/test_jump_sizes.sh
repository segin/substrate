#!/bin/sh
# The sizes of jumps, and of everything that is measured across one
# (AS-T-221, 223, 224).
#
# A jump to a label of its own section is two bytes where the label is
# within 127 bytes ahead or 128 behind, and five or six where it is not;
# and how far a label is depends on the sizes of the jumps between.  The
# sizes are settled together: every jump short, the source laid out,
# each jump that does not reach made long, again until none changes.
#
# Before that:
#
# - A jump to a numbered label was laid out as two bytes whatever it was
#   written as.  After `jmp 9f` over 200 bytes -- five bytes -- every
#   label was taken to be three bytes before its place by whatever worked
#   out a distance:
#
#       a:  jmp 9f
#           .skip 200
#       9:  ret
#           jmp a           went three bytes short of a
#           .long b - a     was three less than it is
#
#   and a jump to a numbered label over a `.org` or an alignment was
#   aimed three bytes past it.
# - A jump to any other label was short only if the statements between
#   came to 127 bytes with every jump among them counted long, and were
#   no more than 64: right, but long where it need not be.
#
# Each case is the bytes of .text -- a run of zeros as `~` and of ones
# as `#` -- and its relocations.  They are GNU as's, and GNU as passes
# this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is SOURCE BYTES32 BYTES64 [RELOCS32 RELOCS64]: `|` in SOURCE is a
# newline; BYTES64 of `=` is BYTES32.
is() {
    for m in 32 64; do
        cases=$((cases + 1))
        if [ $m = 32 ]; then want=$2; wantr=${4:-}; else want=$3; wantr=${5:-}; fi
        [ "$want" = "=" ] && want=$2
        { echo .text; printf '%s\n' "$1" | tr '|' '\n'; } > t.s
        rm -f t.o
        if ! "$AS" --$m -o t.o t.s 2> err; then
            echo "FAIL --$m [$1]: $(tail -1 err)"; fail=1; continue
        fi
        bytes=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n' |
                sed -E 's/(00){8,}/~/g; s/(01){8,}/#/g')
        relocs=$(readelf -rW t.o | awk '/ R_/ { printf "%s@%x:%s%s\n", $3, strtonum("0x" $1), $5, $6 $7 }' |
                 sort | tr '\n' ' ' | sed 's/ $//')
        [ "$bytes" = "$want" ] || { echo "FAIL --$m [$1]: bytes $bytes, not $want"; fail=1; }
        [ "$relocs" = "$wantr" ] || { echo "FAIL --$m [$1]: relocations [$relocs], not [$wantr]"; fail=1; }
    done
}

# At the edge of what two bytes reach.
is 'jmp 1f|.skip 127|1: ret' eb7f~c3 =
is 'jmp 1f|.skip 128|1: ret' e98~0c3 =
is 'je 1f|.skip 127|1: ret' 747f~c3 =
is 'je 1f|.skip 128|1: ret' 0f848~0c3 =
is '1: .skip 126|jmp 1b' ~eb80 =
is '1: .skip 127|jmp 1b' ~e97cffffff =
is '1: .skip 126|jne 1b' ~7580 =
is '1: .skip 127|jne 1b' ~0f857bffffff =
is 'jmp lab|.skip 127|lab: ret' eb7f~c3 =
is 'jmp lab|.skip 128|lab: ret' e98~0c3 =
is 'jmp .L1|.skip 127|.L1: ret' eb7f~c3 =
is 'jmp .L1|.skip 128|.L1: ret' e98~0c3 =

# A jump that grows moves what is behind it.
is 'a: jmp 9f|.skip 200|9: b: ret|jmp a' e9c8~c3e92dffffff =
is 'a: jmp 9f|.skip 200|9: b: ret|.long b-a' e9c8~c3cd000000 =
is 'a: je 9f|.skip 200|9: b: ret|.long b-a' 0f84c8~c3ce000000 =
is 'a: jmp 9f|.skip 200|9: b: ret|c: .long c-a' e9c8~c3ce000000 =
is 'a: jmp 9f|.skip 200|9: b: ret|movl $b-a,%eax' e9c8~c3b8cd000000 =
is 'a: jmp 9f|.skip 200|9: b: ret|.skip b-a,1' 'e9c8~c3#' =
is '9: .skip 200|a: jmp 9b|b: .long b-a' ~e933ffffff05000000 =
is 'a: jmp 9f|.skip 200|9: b: ret|.data|.long b-a' e9c8~c3 =
is 'a: jmp lab|.skip 200|lab: b: ret|.long b-a' e9c8~c3cd000000 =

# Each jump's size is of the others'.
is 'jmp 2f|jmp 1f|.skip 123|2: nop|1: ret' eb7deb7c~90c3 =
is 'jmp 2f|jmp 1f|.skip 124|2: nop|1: ret' eb7eeb7d~90c3 =
is 'jmp 2f|jmp 1f|.skip 125|2: nop|1: ret' eb7feb7e~90c3 =
is '1: .skip 125|je 1b|.skip 124|jmp 1b' ~7481~e900ffffff =
is '1: .skip 120|je 2f|je 1b|.skip 121|2: jmp 1b' ~747b7484~e906ffffff =
is 'jmp 1f|je 2f|je 3f|je 4f|.skip 119|1: nop|2: nop|3: nop|4: ret' eb7d747c747b747a~909090c3 =
is 'jmp 1f|je 2f|je 3f|je 4f|.skip 120|1: nop|2: nop|3: nop|4: ret' eb7e747d747c747b~909090c3 =
is 'jmp 1f|je 2f|je 3f|je 4f|.skip 121|1: nop|2: nop|3: nop|4: ret' eb7f747e747d747c~909090c3 =

# Across .org and across an alignment.
is 'jmp 1f|.skip 60|.org 0x90|1: ret' e98b~c3 =
is 'je 1f|.skip 60|.org 0x90|1: ret' 0f848a~c3 =
is 'jmp lab|.skip 60|.org 0x7f|.byte 1,2|lab: ret' eb7f~0102c3 =
is 'jmp lab|.skip 60|.org 0x81|lab: ret' eb7f~c3 =
is 'jmp lab|.skip 60|.org 0x82|lab: ret' e97d~c3 =
is 'nop|jmp 1f|jmp 2f|.org 0x84|1: ret|.org 0x108|2: ret' 90e97e000000e9fd~c3~c3 =
is 'jmp 1f|.skip 110|.p2align 4,0xcc|1: ret' eb6e~c3 =
is 'jmp 1f|.skip 111|.p2align 4,0xcc|1: ret' eb7e~ccccccccccccccccccccccccccccccc3 =
is 'jmp 1f|.skip 112|.p2align 4,0xcc|1: ret' eb7e~ccccccccccccccccccccccccccccc3 =
is 'jmp 1f|.skip 113|.p2align 4,0xcc|1: ret' eb7e~ccccccccccccccccccccccccccc3 =
is '1: .skip 62|.p2align 6,0xcc|.skip 62|jmp 1b' ~cccc~eb80 =
is '1: .skip 62|.p2align 6,0xcc|.skip 63|jmp 1b' ~cccc~e97cffffff =

# `.`, and a label with a number added: a place, sized like any other
# (AS-T-214).  These had been the long form with a relocation.
is 'jmp .' ebfe =
is 'jmp .+2' eb00 =
is 'jmp 2+.' eb00 =
is 'nop|nop|jmp .' 9090ebfe =
is 'nop|jmp .+200' 90e9c3000000 =
is 'nop|jmp .-200' 90e933ffffff =
is 'nop|je .-2' 9074fc =
is 'nop|je .+129' 90747f =
is 'nop|je .+130' 900f847c000000 =
is 'nop|jmp .-126' 90eb80 =
is 'nop|jmp .-127' 90e97cffffff =
is 'jmp .+2|.skip 300|jmp .-300' eb~e9cffeffff =
is 'lab: nop|jmp lab+1' 90ebfe =
is 'jmp lab+1|.skip 126|lab: ret' eb7f~c3 =
is 'jmp lab+1|.skip 127|lab: ret' e98~0c3 =
is 'jmp lab-1|.skip 128|lab: ret' eb7f~c3 =
is 'nop|1: nop|jmp 1b+1' 9090ebfe =
is 'jmp 1f+2|.skip 125|1: nop' eb7f~90 =
is 'jmp 1f+2|.skip 126|1: nop|jmp 1b-3' e98~090ebfa =
is 'lab: jmp lab|1: je 1b|.L3: jne .L3' ebfe74fe75fe =

# What is not sized here: another file's, another section's, a call.
is 'jmp ext' e9fcffffff e900000000 'R_386_PC32@1:ext' 'R_X86_64_PLT32@1:ext-4'
is 'je ext' 0f84fcffffff 0f8400000000 'R_386_PC32@2:ext' 'R_X86_64_PLT32@2:ext-4'
is 'call 1f|1: ret' e800000000c3 =
is 'jmp 1f|.data|1: .long 0' e9fcffffff e900000000 'R_386_PC32@1:.data' 'R_X86_64_PC32@1:.data-4'

# As many numbered labels as there are, each found (AS-T-226): there
# was room for 8192, and past that each was looked for by laying the
# source out again.
cases=$((cases + 1))
awk 'BEGIN { for (i = 0; i < 9000; i++) print "1:\t.long 1b - ., 2f - .\n2:\t.byte 0" }' > t.s
rm -f t.o
if ! timeout 60 "$AS" --64 -o t.o t.s 2> err; then
    echo "FAIL 9000 numbered labels: $(tail -1 err)"; fail=1
else
    got=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 -w9 | sort | uniq -c | tr -s ' ' | tr '\n' ';')
    [ "$got" = " 9000 00 00 00 00 04 00 00 00 00;" ] || { echo "FAIL 9000 numbered labels: $got"; fail=1; }
fi

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
