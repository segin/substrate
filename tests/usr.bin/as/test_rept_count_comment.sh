#!/bin/sh
# A .rept count with a comment after it.
#
# The count was evaluated with the comment in it, so
# `.rept (0x7F - 0x50 + 1)  # 0x50 .. 0x7F`, as the Substrate kernel's
# isr.S writes it, was "the count is not a number that is known here".
# GNU as reads the count as the line's text before its comment, a
# block comment being a space.
#
# What each assembles to is what GNU as makes of it, and GNU as passes
# this.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is NAME BYTES: NAME.s assembles, and its .data is BYTES.
is() {
    cases=$((cases + 1))
    rm -f t.o t.bin
    if ! "$AS" --32 -o t.o "$1.s" > out 2>&1; then
        echo "FAIL $1: $(tail -1 out)"; fail=1; return
    fi
    objcopy -O binary -j .data t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$2" ] || { echo "FAIL $1: [$got], not [$2]"; fail=1; }
}

printf '\t.data\n\t.rept (0x7F - 0x7C + 1)          # 0x7C .. 0x7F\n\t.byte 1\n\t.endr\n' > line.s
is line 01010101

printf '\t.data\n\t.rept 2 /* twice */ + 1\n\t.byte 2\n\t.endr\n' > block.s
is block 020202

# A `#` in a string is no comment: the count after it is still read.
printf '\t.data\n\t.rept 2\n\t.ascii "#"\n\t.endr\n' > string.s
is string 2323

[ "$fail" -eq 0 ] && echo "ok: repeat counts with comments ($cases cases)"
exit "$fail"
