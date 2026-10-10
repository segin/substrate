#!/bin/sh
# The x87 instructions with operands (AS-T-178, and more of AS-T-179).
#
# The arithmetic instructions were taken only as `op %st(i), %st`.  A
# compiler writes `faddp %st, %st(1)`, `fmulp %st, %st(1)`, `fdivp`; a
# person writes `faddp`, `fxch`, `fadd %st(2)`; none of them assembled.
# Nor did the integer instructions without a size suffix (`fild`, `fist`,
# `fiadd`), the 64-bit integer loads and stores (`fildq`, `fistpq`), or
# the forms that wait (`fstcw`, `fstsw`, `fsave`, `fstenv`).  And in 64-bit
# mode `fisubl`, `fisubrl` and `fimull` had the 16-bit opcode.
#
# corpus/x87_forms.txt has 653 lines of mode|instruction|bytes, the bytes
# being those GNU as 2.47 gives.  Forms whose operand is a symbol are in
# test_symbolic_disp.sh.  Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0
count=0

# One source for each mode, a label before each instruction, so that the
# assembler runs twice and not 653 times; then each instruction's bytes
# are those between its label and the next.
for mode in 32 64; do
    grep "^$mode|" "$here/corpus/x87_forms.txt" > lines
    awk -F'|' '{ printf "L%d:\t%s\n", NR, $2 } END { printf "L%d:\n", NR + 1 }' lines > t.s
    if ! "$AS" --$mode -o t.o t.s 2> err; then
        n=$(sed -n 's/^as: error: [^:]*:\([0-9]*\): .*/\1/p' err | head -1)
        echo "FAIL --$mode: $(head -1 err | sed 's/^as: error: [^:]*:[0-9]*: //'): $(sed -n "${n:-1}p" lines | cut -d'|' -f2)"
        fail=1
        continue
    fi
    objcopy -O binary -j .text t.o t.bin
    od -An -v -tx1 t.bin | tr -d ' \n' > t.hex
    # Each label's number and its offset, which readelf prints in
    # hexadecimal, in the order of the labels.
    readelf -sW t.o | awk '$8 ~ /^L[0-9]+$/ { print substr($8, 2), $2 }' | sort -n > offs
    total=$(wc -l < lines | tr -d ' ')
    [ "$(wc -l < offs | tr -d ' ')" = "$((total + 1))" ] ||
        { echo "FAIL --$mode: $(wc -l < offs) labels in the object, not $((total + 1))"; fail=1; continue; }
    n=0
    prev=
    while read -r number value; do
        cur=$((0x$value))
        if [ -n "$prev" ]; then
            n=$((n + 1))
            count=$((count + 1))
            got=$(cut -c$((prev * 2 + 1))-$((cur * 2)) t.hex)
            want_bytes=$(sed -n "${n}p" lines | cut -d'|' -f3)
            if [ "$got" != "$want_bytes" ]; then
                echo "FAIL --$mode $(sed -n "${n}p" lines | cut -d'|' -f2): $got, not $want_bytes"; fail=1
            fi
        fi
        prev=$cur
    done < offs
done

[ "$fail" -eq 0 ] && echo "ok: x87 forms ($count instructions)"
exit "$fail"
