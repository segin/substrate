#!/bin/sh
# The size prefixes by their names, and what they are written before.
#
# `data16` before a jump or a call to a label the assembler knew was
# left out and nothing said; before one to a symbol of another object
# the distance had four bytes behind a prefix that says two, and so had
# `callw sym` in 64-bit code.  `addr16 loop` was `loop`, which counts in
# another register.  `addr16 movss sym, %xmm0` had the prefix before
# the ModRM byte of a 32-bit address, which behind it is `(%di)`, and
# most emitters left the prefix out; `fldl sym(%eip)` was `fldl
# sym(%rip)`.  In 16-bit code `cmpxchg %eax, (%bx)`, `btl $1, sym`,
# `bsf (%si), %eax` and `shld` had no operand-size prefix and were of
# 16 bits, and `crc32w` had one and read 32.  `addr32` in 32-bit code,
# a jump to a label with a size letter, `es` before an instruction of
# 64-bit code, `stosb %bl, %es:(%edi)` and `movsxd %ax, %bx` were
# assembled, and GNU as refuses each.
#
# prefix_forms.txt holds what GNU as 2.46 makes of each statement: the
# bytes of .text, and the type, place and symbol of each relocation; or
# `refused`.  (`;` in a statement is a new line.)  GNU as passes this.
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
    if "$AS" --"$mode" -o t.o t.s 2> /dev/null; then
        objcopy -O binary --only-section=.text t.o t.bin
        got=$(od -An -v -tx1 t.bin | tr -d ' \n')
        got=$got$(readelf -rW t.o | awk '/R_/{printf "/%s@%s:%s", $3, $1, $5 $6 $7}' | sed 's/@0*\([0-9a-f]\)/@\1/g')
    else
        got=refused
    fi
    rm -f t.o t.bin
    if [ "$got" != "$want" ]; then
        echo "FAIL --$mode [$src]: $got, and GNU as: $want"
        fail=1
    fi
done < "$here/prefix_forms.txt"

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
