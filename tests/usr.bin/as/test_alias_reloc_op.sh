#!/bin/sh
# A name for a symbol, with a relocation operator on it.
#
# A compiler names one constant twice and loads it by either name:
#
#	.set	.LC24,.LC25
#	movq	.LC24@GOTOFF(%ebx), %xmm4
#
# The names a `.set` makes are put out of the way before anything is
# assembled, each replaced by what it stands for -- but one with an
# operator on it was left as it was written, and the `.set` that made it
# was gone with the rest: the object asked every other for a symbol
# `.LC24`, and what gcc -m32 -fPIC makes of a file with such a constant
# did not link.
#
# The operator is on what the name stands for.  Each case here is
# assembled, the object may have no undefined symbol, and its relocation
# is of the kind written, against the symbol that is defined.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# check MODE TYPE ADDEND-BYTES SOURCE...: the lines of a source, `;` between them.
check() {
    mode=$1
    want_type=$2
    want_bytes=$3
    cases=$((cases + 1))
    printf '%s\n' "$4" | tr ';' '\n' > t.s
    if ! "$AS" "--$mode" -o t.o t.s 2> err; then
        echo "FAIL --$mode [$4]: $(head -1 err)"
        fail=1
        return
    fi
    undefined=$(nm -u t.o | awk '{print $NF}' | tr '\n' ' ')
    if [ -n "$undefined" ]; then
        echo "FAIL --$mode [$4]: undefined: $undefined"
        fail=1
    fi
    type=$(readelf -rW t.o | awk '/R_/{print $3}' | tr '\n' ' ')
    if [ "$type" != "$want_type " ]; then
        echo "FAIL --$mode [$4]: relocation $type, not $want_type"
        fail=1
    fi
    objcopy -O binary --only-section=.text t.o t.bin
    bytes=$(od -An -v -tx1 t.bin | tr -d ' \n')
    if [ "$bytes" != "$want_bytes" ]; then
        echo "FAIL --$mode [$4]: $bytes, not $want_bytes"
        fail=1
    fi
}

check 32 R_386_GOTOFF f30f7ea300000000 \
    '.set .LC24,.LC25; movq .LC24@GOTOFF(%ebx),%xmm4; .section .rodata.cst16,"aM",@progbits,16; .LC25: .long 1,2,3,4'
check 32 R_386_GOTOFF 8d8300000000 '.set a,b; leal a@GOTOFF(%ebx),%eax; .data; b: .long 1'
# (A name for a symbol and some bytes on: the bytes are the addend.)
check 32 R_386_GOTOFF 8b8304000000 '.set a,b+4; movl a@GOTOFF(%ebx),%eax; .data; b: .long 1,2'
check 32 R_386_GOTOFF 8d83f8ffffff '.set a,b-8; leal a@GOTOFF(%ebx),%eax; .data; b: .long 1,2'
# (A name for a name.)
check 32 R_386_GOTOFF 8b8300000000 '.set a,b; .set c,a; movl c@GOTOFF(%ebx),%eax; .data; b: .long 1'
check 32 R_386_GOT32X 8b8300000000 '.set a,b; movl a@GOT(%ebx),%eax; .data; b: .long 1'
check 64 R_X86_64_REX_GOTPCRELX 488b0500000000 '.set a,b; movq a@GOTPCREL(%rip),%rax; .data; b: .long 1'

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "ok: a name with a relocation operator ($cases cases)"
