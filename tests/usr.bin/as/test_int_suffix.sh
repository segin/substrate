#!/bin/sh
# C integer suffixes after a number: `u`, `l`, `ll` and their mixes, in
# either case, which GNU as accepts and ignores.  Headers shared between C
# and assembly hand them over: Linux's generated <asm/cpufeaturemasks.h>
# has `#define REQUIRED_MASK0 0x00008101U`, which its head_32.S uses as
# an immediate, and this assembler refused "$0x00008101U" as an invalid
# operand.
#
# A suffix counts only whole, with no identifier character after it, so
# the local-label references 1f and 1b are what they were.  GNU as passes
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

# is SECTION SOURCE BYTES, in both modes
is() {
    for m in 32 64; do
        cases=$((cases + 1))
        printf '\t%s\n%s\n' "$1" "$2" > t.s
        rm -f t.o
        if ! "$AS" --$m -o t.o t.s 2> err; then
            echo "FAIL --$m [$2]: $(tail -1 err)"; fail=1; continue
        fi
        bytes=$(objcopy -O binary -j "$1" t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
        [ "$bytes" = "$3" ] || { echo "FAIL --$m [$2]: bytes $bytes, not $3"; fail=1; }
    done
}

is .data '	.long 5u, 7U, 9l, 3L, 4ul, 6LU, 8ull, 2LLU, 1llu' \
    050000000700000009000000030000000400000006000000080000000200000001000000
is .data '	.long 0x10ULL, 017u, 0b101U' 100000000f00000005000000
is .data '	.byte (0x8101U & ((1 << 0) | (1 << 8)))' 01
# As an immediate, as head_32.S has it.
is .text '	movl $0x00008101U, %eax' b801810000
is .text '	movl $(0x00008101U & (1 << 8)), %ecx' b900010000
# 1f and 1b are labels, not 1 with a suffix.
is .text '	jmp 1f
	nop
1:	jmp 1b' eb0190ebfe

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
