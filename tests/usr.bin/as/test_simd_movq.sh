#!/bin/sh
# movq with an MMX or XMM register is the SIMD move (AS-T-180).
#
# The q of movq was taken for a size suffix: the instruction became mov,
# and the vector register's number a general register's.  In 32-bit code
# `movq %xmm0, (%esp)` -- how a compiler stores a 64-bit integer it holds
# in an XMM register -- was `movl %eax, (%esp)`, so a `long long` passed
# to printf was whatever %eax held.  In 64-bit code `movq %xmm1, %xmm2`
# moved %rcx into %xmm2.
#
# The bytes are GNU as's.  Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

# enc MODE INSTRUCTION BYTES
enc() {
    printf '\t.text\n\t%s\n' "$2" > t.s
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 $2: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$3" ] || { echo "FAIL --$1 $2: $got, not $3"; fail=1; }
}

enc 32 'movq %xmm1, %xmm2'      f30f7ed1
enc 32 'movq %xmm0, (%esp)'     660fd60424
enc 32 'movq (%eax), %xmm2'     f30f7e10
enc 32 'movq 8(%esp), %xmm1'    f30f7e4c2408
enc 32 'movq %xmm3, 16(%eax)'   660fd65810
enc 32 'movq sym, %xmm0'        f30f7e0500000000
enc 32 'movq %xmm0, sym'        660fd60500000000
enc 32 'movq %mm1, %mm2'        0f6fd1
enc 32 'movq (%eax), %mm2'      0f6f10
enc 32 'movq %mm2, (%eax)'      0f7f10

enc 64 'movq %xmm1, %xmm2'      f30f7ed1
enc 64 'movq %xmm9, %xmm10'     f3450f7ed1
enc 64 'movq %xmm0, (%rsp)'     660fd60424
enc 64 'movq %xmm9, (%r10)'     66450fd60a
enc 64 'movq (%rax), %xmm2'     f30f7e10
enc 64 'movq 8(%rsp), %xmm1'    f30f7e4c2408
enc 64 'movq sym(%rip), %xmm1'  f30f7e0d00000000
enc 64 'movq %xmm1, sym(%rip)'  660fd60d00000000
enc 64 'movq %mm1, %mm2'        0f6fd1
enc 64 'movq (%rax), %mm2'      0f6f10
enc 64 'movq %mm2, (%rax)'      0f7f10
enc 64 'movq %rax, %mm1'        480f6ec8
enc 64 'movq %mm1, %rax'        480f7ec8
enc 64 'movq %r9, %mm3'         490f6ed9
# Between general registers, and to and from an XMM register, it is what
# it was.
enc 64 'movq %rax, %xmm0'       66480f6ec0
enc 64 'movq %xmm0, %rax'       66480f7ec0
enc 64 'movq $1, %rax'          48c7c001000000
enc 64 'movq %rax, (%rbx)'      488903

[ "$fail" -eq 0 ] && echo "ok: SIMD movq"
exit "$fail"
