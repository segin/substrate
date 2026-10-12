#!/bin/sh
# `.nops size[, most]`: bytes of instructions that do nothing.
#
# It was no directive.  It is what an alignment is padded with: the
# long NOPs of the object, the odd length first, and where there are
# many of them a jump over the rest; `most` is the longest one of them
# may be, and more than the longest there is is refused.  Whether there
# is a jump is by the longest there is, whatever was asked for.  In
# 16-bit code they are 16-bit code's.
#
# nops_forms.txt holds what GNU as 2.47 makes of each source: the bytes
# of .text and .data, `none` for no bytes, or `refused`.  (`;` is a new
# line.)  GNU as passes this.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0
while IFS='	' read -r mode want src; do
    cases=$((cases + 1))
    printf '%s\n' "$src" | tr ';' '\n' > t.s
    rm -f t.o
    if "$AS" --"$mode" -o t.o t.s 2> /dev/null && [ -f t.o ]; then
        objcopy -O binary --only-section=.text --only-section=.data t.o t.bin
        got=$(od -An -v -tx1 t.bin | tr -d ' \n')
        [ -n "$got" ] || got=none
    else
        got=refused
    fi
    if [ "$got" != "$want" ]; then
        echo "FAIL --$mode [$src]: $got, and GNU as: $want"
        fail=1
    fi
done < "$here/nops_forms.txt"

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
