#!/bin/sh
# Two things about the directives that store numbers.
#
# An argument left empty is zero, and GNU as says "zero assumed for
# missing expression": `.byte 1,,2` is 01 00 02.  This assembler refused
# the line.
#
# .int and .value are .long and .short by other names.  Nothing knew
# them, and a directive nothing knows was passed over in silence: `.int
# 5` and `.value 5` put nothing in the section, and what followed was
# at the wrong address.  .value is what a compiler writes for a 16-bit
# datum.
#
# The bytes are those of GNU as 2.46.  A line of several statements has
# ` | ` between them.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# d WANT SOURCE: the bytes of .data, in both modes.
d() {
    for mode in 32 64; do
        cases=$((cases + 1))
        { printf '\t.data\n'; printf '%s\n' "$2" | sed 's/ | /\n/g'; } > t.s
        rm -f t.o
        if "$AS" "-$mode" -o t.o t.s > out 2> err; then
            objcopy -O binary --only-section=.data t.o data.bin 2>/dev/null
            got=$(od -An -v -tx1 data.bin | tr -d ' \n')
        else
            got="refused ($(head -1 err))"
        fi
        if [ "$got" != "$1" ]; then
            echo "FAIL $mode [$2]: $got, and GNU as: $1"
            fail=1
        fi
    done
}

# An empty argument.
d 010002                    '.byte 1,,2'
d 010002                    '.byte 1, ,2'
d 0000000005000000          '.long ,5'
d 01000000                  '.word 1,'
d 0000                      '.byte ,'
d 010200                    '.byte 1,2,'
d 070000000800              '.short 7,,8'
d 00000100                  '.hword ,1'
d 00000000                  '.2byte ,'
d 010000000000000000000000  '.4byte 1,,'
d 00000000000000000300000000000000 '.8byte ,3'
d 000000000000000000000000000000000000000000000000 '.quad ,,'
d 0000000001000000          '.int ,1'
d 00000100                  '.value ,1'

# And it is said, once for the line, and counted as a warning.
cases=$((cases + 3))
printf '\t.data\n\t.byte 1,,2\n\t.long ,5\n\t.byte 3\n' > w.s
"$AS" -32 -o w.o w.s > out 2> err || { echo "FAIL: .byte 1,,2 is refused"; fail=1; }
[ "$(grep -c 'zero assumed for missing expression' err)" -eq 2 ] ||
    { echo "FAIL: two lines with an empty argument, and $(grep -c 'zero assumed' err) warnings"; fail=1; }
"$AS" -32 --fatal-warnings -o w.o w.s > out 2> err &&
    { echo "FAIL: --fatal-warnings does not count the empty argument"; fail=1; }
"$AS" -32 --no-warn -o w.o w.s > out 2> err
[ -s err ] && { echo "FAIL: --no-warn does not silence it: $(head -1 err)"; fail=1; }

# .int and .value.
d ee05000000ff              '.byte 0xEE | .int 5 | .byte 0xFF'
d 01341202                  '.byte 1 | .value 0x1234 | .byte 2'
d 01000000                  '.int 1'
d 78563412ffffffff          '.int 0x12345678, -1'
d 0100                      '.value 1'
d 3412feff                  '.value 0x1234, -2'
d 04000000                  '.int 1f-. | 1:'
d 030000                    '.value 1f-. | .byte 0 | 1:'
# And the three that are widths by name, which were right.
d 3412                      '.2byte 0x1234'
d 78563412                  '.4byte 0x12345678'
d 8877665544332211          '.8byte 0x1122334455667788'

[ "$fail" -eq 0 ] && echo "ok: empty arguments, .int and .value ($cases cases)"
exit "$fail"
