#!/bin/sh
# Floating-point numbers and strings in data (AS-T-281 to 283).
#
# The numbers of `.float` and `.double` were read in three places, by
# the C library's strtod as it stood.  They are read in one, as GNU as
# 2.46 reads them:
#
#   .float 1e39        was infinity; it is refused, as no number of the
#                      width (one that only rounds up to infinity is it)
#   .float nan         was 7fc00000; GNU's is 7fffffff
#   .float 0f1.5       was refused: `0` and a letter are passed over
#   .float 0x:3fc00000 the bits themselves
#   .float 0x1p3       was taken, as C has it; GNU has no such number
#
# and the escapes of a string are GNU's: there is none for a bell, `\x`
# with no digit is a zero byte, `\X` is `\x`, and 8 and 9 are digits of
# an octal escape.
#
# The bytes of .data are GNU as's, and GNU as passes this test.  (One
# thing is not GNU's and is not here: a number between two that the
# width holds is rounded to the nearer, where GNU as rounds some up --
# `.float 16777217` is 16777216 here and 16777218 there.)
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is DIRECTIVE BYTES, in both modes
is() {
    for m in 32 64; do
        cases=$((cases + 1))
        printf '\t.data\n\t%s\n' "$1" > t.s
        rm -f t.o
        if ! "$AS" --$m -o t.o t.s 2> err; then
            echo "FAIL --$m [$1]: $(tail -1 err)"; fail=1; continue
        fi
        bytes=$(objcopy -O binary -j .data t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
        [ "$bytes" = "$2" ] || { echo "FAIL --$m [$1]: bytes $bytes, not $2"; fail=1; }
    done
}

refused() {
    cases=$((cases + 1))
    printf '\t.data\n\t%s\n' "$1" > t.s
    if "$AS" --64 -o t.o t.s > /dev/null 2> err; then
        echo "FAIL [$1]: assembled"; fail=1
    elif ! grep -q '^t\.s:2' err; then
        echo "FAIL [$1]: refused without its line: $(tail -1 err)"; fail=1
    fi
}

# Numbers as they are usually written.
is '.float 1.5, -2.25, 3' 0000c03f000010c000004040
is '.single 1.5' 0000c03f
is '.float .5, 5.' 0000003f0000a040
is '.float 077' 00009a42
is '.float 3.14159274' db0f4940
is '.float 123456789' a379eb4c
is '.double 3.141592653589793' 182d4454fb210940
is '.dc.s 1.5' 0000c03f
is '.dc.d 2.5' 0000000000000440
is '.float 1,' 0000803f00000000
is '.float -0.0' 00000080

# Too large, too small, and at the edge.
refused '.float 1e39'
refused '.float -1e39'
refused '.double 1e309'
refused '.double 1e400'
refused '.float 0f1e39'
is '.float 1e38' 9976967e
is '.float 3.4028235e38' ffff7f7f
is '.float 3.4028236e38' 0000807f
is '.double 1.7976931348623157e308' ffffffffffffef7f
is '.float 1e-46' 00000000
is '.float 1e-45' 01000000
is '.float 1e-40' c2160100
is '.double 1e-320' e807000000000000

# What is not a number of the ordinary kind.
is '.float inf' 0000807f
is '.float -inf' 000080ff
is '.float infinity' 0000807f
is '.float Inf, NaN, +inf' 0000807fffffff7f0000807f
is '.float nan' ffffff7f
is '.float qnan, snan, -nan' ffffff7fffffbf7fffffffff
is '.double inf, nan, -nan, snan' 000000000000f07fffffffffffffff7ffffffffffffffffffffffffffffff77f
is '.double 0d-inf' 000000000000f0ff

# `0` and a letter before the number, and the bits themselves.
is '.float 0f1.5' 0000c03f
is '.float 0e1.5' 0000c03f
is '.double 0d2.5' 0000000000000440
is '.float 0f-1.5e2' 000016c3
is '.float 0e-0' 00000080
is '.float 0x10' 00002041
is '.float 0b101' 0000ca42
is '.double 0x10' 0000000000002440
is '.float 0x:3fc00000' 0000c03f
is '.double 0x:4004000000000000' 0000000000000440
refused '.float 0x3fc00000'
refused '.float -0x10'
refused '.float 0x1p3'

# An exponent with nothing in it, and what is no number.
is '.float 1.0e' 0000803f
is '.float 1e' 0000803f
refused '.float 1.5f'
refused '.float 1_0'
refused '.float sym'

# str DIRECTIVE BYTES: DIRECTIVE is written with printf, so a backslash
# is doubled here.
str() {
    cases=$((cases + 1))
    printf '\t.data\n\t' > t.s
    printf '%s\n' "$1" >> t.s
    rm -f t.o
    if ! "$AS" --64 -o t.o t.s 2> err; then
        echo "FAIL [$1]: $(tail -1 err)"; fail=1; return
    fi
    bytes=$(objcopy -O binary -j .data t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    [ "$bytes" = "$2" ] || { echo "FAIL [$1]: bytes $bytes, not $2"; fail=1; }
}

str '.ascii "\b\f\n\r\t"' 080c0a0d09
str '.ascii "\\"' 5c
str '.ascii "abc\\"' 6162635c
str '.ascii "\\\\"' 5c5c
str '.ascii "\""' 22
str '.ascii "a\"b"' 612262
str '.ascii "\a"' 61
str '.ascii "\q"' 71
str '.ascii "\e"' 65
str '.ascii "\x41"' 41
str '.ascii "\X41"' 41
str '.ascii "\x414"' 14
str '.ascii "\x4142"' 42
str '.ascii "\x4"' 04
str '.ascii "\x"' 00
str '.ascii "\xg"' 0067
str '.ascii "\101"' 41
str '.ascii "\1012"' 4132
str '.ascii "\0"' 00
str '.ascii "\8"' 08
str '.ascii "\9"' 09
str '.ascii "\18"' 10
str '.ascii "\777"' ff
str '.ascii "\400"' 00
str '.ascii "\0\00\000\0000"' 0000000030
str '.ascii "a" "b"' 6162
str '.ascii "a", "b"' 6162
str '.asciz "a", "b"' 61006200
str '.string "a" "b"' 616200
str '.string "a\0b"' 61006200
str '.ascii ""' ''

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
