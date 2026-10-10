#!/bin/sh
# A far jump or call with its target written out: `ljmp $segment,
# $offset`.  The offset is encoded first and the two bytes of the
# segment after it.  When the offset was a symbol its relocation was put
# on the last four bytes of the instruction, which are not the offset's:
# `ljmp $0x10, $sym` came out with the relocation over the segment, and
# the segment as zero.  That is the jump a kernel makes to load %cs.
#
# Each line is held to GNU as 2.46: the bytes, and the place and type of
# the relocation (`-` for none).
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# s BYTES RELOCATION SOURCE
s() {
    cases=$((cases + 1))
    printf '\t.text\n\t%s\n' "$3" > t.s
    rm -f t.o
    if ! "$AS" -32 -o t.o t.s > out 2>&1; then
        echo "FAIL [$3]: refused: $(head -1 out)"
        fail=1
        return
    fi
    objcopy -O binary --only-section=.text t.o text.bin 2>/dev/null
    got=$(od -An -v -tx1 text.bin | tr -d ' \n')
    rel=$(readelf -rW t.o | awk '$1 ~ /^[0-9a-f]+$/ && $3 ~ /^R_/ { sub(/^0+/, "", $1); print $1 ":" $3; exit }')
    if [ "$got" != "$1" ] || [ "${rel:--}" != "$2" ]; then
        echo "FAIL [$3]: $got ${rel:--}, and GNU as: $1 $2"
        fail=1
    fi
}

# The offset a symbol.
s ea000000001000   1:R_386_32  'ljmp $0x10,$sym'
s 9a000000001000   1:R_386_32  'lcall $0x10,$sym'
s ea040000001000   1:R_386_32  'ljmp $0x10,$sym+4'
s ea000000001000   1:R_386_32  'ljmpl $0x10,$sym'

# A 16-bit offset has a 16-bit relocation.
s 66ea00001000     2:R_386_16  'ljmpw $0x10,$sym'
s 669a00001000     2:R_386_16  'lcallw $0x10,$sym'

# The offset a number, and the forms through memory, which were right.
s ea341200001000   -           'ljmp $0x10,$0x1234'
s 9a785634120800   -           'lcall $8,$0x12345678'
s 66ea34121000     -           'ljmpw $0x10,$0x1234'
s ff2d00000000     2:R_386_32  'ljmp *sym'
s ff18             -           'lcall *(%eax)'

[ "$fail" -eq 0 ] && echo "ok: far jumps and calls ($cases cases)"
exit "$fail"
