#!/bin/sh
# Whitespace in a macro invocation's arguments, as GNU as reads it on x86:
# its scrubber keeps whitespace in operands only between two characters an
# operand can hold (letters, digits, `_ . $` and `* % - ( [`), so a space
# separates arguments in `1 2` and `1 -2` but not in `1 << 2` or
# `a=1 + 2`.  Linux's _ASM_EXTABLE_TYPE_REG passes
# `type=(17 | ((-14) << 16)) | ((3) << 12)` with spaces around the `|`s,
# and this assembler split it into stray positional arguments, so the
# kernel's extable_type_reg macro reported "bad register argument".
# GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is ARGS BYTES: `show ARGS` emits a, 0x77, then b if given
is() {
    for m in 32 64; do
        cases=$((cases + 1))
        printf '\t.macro show a:req b\n\t.long \\a\n\t.long 0x77\n\t.ifnb \\b\n\t.long \\b\n\t.endif\n\t.endm\n\tshow %s\n' \
            "$1" > t.s
        rm -f t.o
        if ! "$AS" --$m -o t.o t.s 2> err; then
            echo "FAIL --$m [$1]: $(tail -1 err)"; fail=1; continue
        fi
        bytes=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
        [ "$bytes" = "$2" ] || { echo "FAIL --$m [$1]: bytes $bytes, not $2"; fail=1; }
    done
}

is '1 2' 0100000077000000""02000000
is '1 -2' 0100000077000000feffffff
is '-1 2' ffffffff7700000002000000
is '1 << 2' 0400000077000000
is '3 >> 1' 0100000077000000
is '1 & 3' 0100000077000000
is 'a=1 + 2' 0300000077000000
is 'a=(1 | 2) | 4' 0700000077000000
is 'b=3, a=(1) + 2' 030000007700000003000000
is 'a=(1|2) |4, b=7' 070000007700000007000000
# Linux's, with the tabs its macros leave
is 'b=5, a=(17   | ((-14	 ) << 16)) |						   ((3) << 12)' 1130f2ff7700000005000000

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
