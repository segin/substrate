#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-i386-tail-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/i386_tail.s" <<'SRC'
.text
.globl i386_tail
.type i386_tail,@function
i386_tail:
    prefetchwt1 0x11223344
    cldemote 0x11223344
    movmskps %xmm0,%eax
    movmskpd %xmm0,%eax
    pmovmskb %mm0,%eax
    pmovmskb %xmm0,%eax
    movntq %mm0,0x11223344
    movntdq %xmm0,0x11223344
    movntps %xmm0,0x11223344
    movntpd %xmm0,0x11223344
    ucomisd %xmm0,%xmm1
    comisd %xmm0,%xmm1
    psrldq $0x90,%xmm0
    pslldq $0x90,%xmm0
    pshuflw $0x90,%xmm0,%xmm1
    pshufhw $0x90,%xmm0,%xmm1
    extrq $0x90,$0x91,%xmm0
    insertq $0x90,$0x91,%xmm0,%xmm1
    movdq2q %xmm0,%mm1
    movq2dq %mm0,%xmm1
    cvtdq2pd %xmm0,%xmm1
    ret
.size i386_tail, .-i386_tail
SRC

"$AS" --32 -o "$TMP/i386_tail.o" "$TMP/i386_tail.s"
objcopy -O binary --only-section=.text "$TMP/i386_tail.o" "$TMP/i386_tail.bin"

actual=$(od -An -tx1 -v "$TMP/i386_tail.bin" | tr -s ' \n' ' ' | sed 's/^ //; s/ $//')
expected="0f 0d 15 44 33 22 11 0f 1c 05 44 33 22 11 0f 50 c0 66 0f 50 c0 0f d7 c0 66 0f d7 c0 0f e7 05 44 33 22 11 66 0f e7 05 44 33 22 11 0f 2b 05 44 33 22 11 66 0f 2b 05 44 33 22 11 66 0f 2e c8 66 0f 2f c8 66 0f 73 d8 90 66 0f 73 f8 90 f2 0f 70 c8 90 f3 0f 70 c8 90 66 0f 78 c0 90 91 f2 0f 78 c8 90 91 f2 0f d6 c8 f3 0f d6 c8 f3 0f e6 c8 c3"

test "$actual" = "$expected"

echo "ok: i386 refactor tail coverage"
