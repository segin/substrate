#!/bin/sh
# Instructions that were assembled to the bytes of another, or of
# another form of themselves, with no complaint: each is held here to
# what GNU as 2.46 writes for it, or to GNU's refusal.
#
# A line is the modes it is tried in, the bytes (or `refused`), and the
# source.  The cases are grouped by the fault that each group had.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# t MODES WANT SOURCE
t() {
    for mode in $1; do
        cases=$((cases + 1))
        printf '\t.text\n\t%s\n' "$3" > t.s
        rm -f t.o
        if "$AS" "-$mode" -o t.o t.s > out 2>&1; then
            objcopy -O binary --only-section=.text t.o text.bin 2>/dev/null
            got=$(od -An -v -tx1 text.bin | tr -d ' \n')
        else
            got=refused
        fi
        if [ "$got" != "$2" ]; then
            echo "FAIL $mode [$3]: $got, and GNU as: $2"
            fail=1
        fi
    done
}

# enter: the frame's size and then the nesting level, in that order in
# both syntaxes -- it has no destination to be written last.  The two
# had been exchanged, so `enter $16, $0` made a frame of no bytes at
# nesting level sixteen.
t "32 64" c8100000    'enter $16, $0'
t "32 64" c8341203    'enter $0x1234, $3'
t "32 64" c8000000    'enter $0, $0'
t "32 64" c8ffff1f    'enter $0xffff, $31'
t "32 64" c8ffff00    'enter $-1, $0'
t "32 64" 66c8080001  'enterw $8, $1'
t "32"    c8080001    'enterl $8, $1'
t "64"    c8080001    'enterq $8, $1'
t "32 64" refused     'enter $0x10000, $0'
t "32 64" refused     'enter $8, $256'
t "32 64" refused     'enter $8'

# The same in Intel syntax, which was right and must stay so.
for mode in 32 64; do
    cases=$((cases + 1))
    printf '\t.intel_syntax noprefix\n\t.text\n\tenter 16, 0\n\tenter 0x1234, 3\n' > t.s
    "$AS" "-$mode" -o t.o t.s > out 2>&1 || { echo "FAIL $mode Intel enter: refused: $(head -1 out)"; fail=1; continue; }
    objcopy -O binary --only-section=.text t.o text.bin
    got=$(od -An -v -tx1 text.bin | tr -d ' \n')
    [ "$got" = c8100000c8341203 ] || { echo "FAIL $mode Intel enter: $got"; fail=1; }
done

# ud2b is not ud2: it is the other undefined opcode, 0F B9.
t "32 64" 0f0b  'ud2'
t "32 64" 0f0b  'ud2a'
t "32 64" 0fb9  'ud2b'

[ "$fail" -eq 0 ] && echo "ok: instruction forms ($cases cases)"
exit "$fail"
