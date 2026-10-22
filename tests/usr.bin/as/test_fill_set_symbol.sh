#!/bin/sh
# A .fill count that is an expression of labels and of a symbol set with
# `=` takes the symbol's value where the .fill stands, as GNU as does.
# Linux's head_32.S pads each early IDT stub to 9 bytes with
#     i = 0
#     .rept 32
#     ...
#     i = i + 1
#     .fill early_idt_handler_array + i*9 - ., 1, 0xcc
#     .endr
# and this assembler stopped with "malformed directive data": the emitter,
# which knows the labels, did not know i.  GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is SOURCE BYTES (of .text, --32)
is() {
    cases=$((cases + 1))
    printf '%b\n' "$1" > t.s
    rm -f t.o
    if ! "$AS" --32 -o t.o t.s 2> err; then
        echo "FAIL [$1]: $(tail -1 err)"; fail=1; return
    fi
    bytes=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    [ "$bytes" = "$2" ] || { echo "FAIL [$1]: bytes $bytes, not $2"; fail=1; }
}

is 'a:\n\ti = 1\n\tnop\n\t.fill a + i*4 - ., 1, 0xcc' 90cccccc
# head_32.S's stubs, three of them: pushl $0 only where bit i of 0x5 is clear
is 'a:\n\ti = 0\n\t.rept 3\n\t.if ((0x5 >> i) & 1) == 0\n\tpushl $0\n\t.endif\n\tpushl $i\n\tjmp a\n\ti = i + 1\n\t.fill a + i*9 - ., 1, 0xcc\n\t.endr' \
    6a00ebfccccccccccc6a006a01ebf1cccccc6a02ebeacccccccccc

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
