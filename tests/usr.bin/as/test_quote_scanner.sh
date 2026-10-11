#!/bin/sh
# Strings and comments are told from text in one way.
#
# What splits a line at `;`, what splits a macro's arguments, what takes
# the comments out and what reads a string each had its own scanner --
# six of them.  Two did not know that a backslash escapes a quote, so
# `.ascii "a\"; b"` was cut at its `;` and refused as an unterminated
# string; and only one knew what a comment was, so a `;` in a comment
# began a statement: `nop # a ; ret` assembled a ret, and the comment
# gcc -fverbose-asm writes beside a loop, "# for (i = 0; i < 32; i++)",
# was refused.
#
# The bytes are GNU as 2.46's.  `|` is a new line.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# q BYTES SOURCE: .text holds BYTES, in 32-bit code and in 64-bit.
q() {
    for mode in 32 64; do
        cases=$((cases + 1))
        { echo .text; printf '%s\n' "$2" | tr '|' '\n'; } > t.s
        rm -f t.o
        if ! "$AS" "-$mode" -o t.o t.s > out 2> err; then
            echo "FAIL $mode [$2]: $(head -1 err)"; fail=1; continue
        fi
        got=$(objcopy -O binary --only-section=.text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
        [ "$got" = "$1" ] || { echo "FAIL $mode [$2]: $got, and GNU as: $1"; fail=1; }
    done
}

# A `;` in a string is of the string, a quote after a backslash too.
q 783b7990      '.ascii "x;y"; nop'
q 61223b2062    '.ascii "a\"; b"'
q 615c90        '.ascii "a\\"; nop'
q 615c2290      '.ascii "a\\\"" ; nop'
q 613b62613b62  '.rept 2|.ascii "a;b"|.endr'
q b03b          "movb \$';', %al"
q b02390        "movb \$'#', %al; nop"

# What looks like a comment in a string is not one.
q 6120232062    '.ascii "a # b"'
q 61202f2f2062  '.ascii "a // b"'
q 2f2a2063202a2f '.ascii "/* c */"'
q 9023c3        'nop ; .ascii "#" ; ret'

# A `;` in a comment ends no statement.
q 90            'nop # a ; ret'
q 90c3          'nop # for (i = 0; i < 32; i++)|ret'
q 90c3          "nop # don't ; hlt|ret"
q 90c3          'nop /* x ; y */ ; ret'
q 90            '/* a|; b */ nop'
q 613b6290      '.ascii "a;b" # c ; d|nop'
q 0102          '.macro m a, b|.byte \a, \b|.endm|m 1, 2 # three ; four'
q 0102          '.irp x, 1, 2|.byte \x # y ; z|.endr'

# And a `;` that is text still ends one.
q 9090c3        'nop; nop; ret'

[ "$fail" -eq 0 ] && echo "ok: strings and comments ($cases cases)"
exit "$fail"
