#!/bin/sh
# The 64-bit encoder returns what it encodes.
#
# as_x86_encode.c matches a mnemonic in two if/else chains, one after the
# other.  A stray brace had ended the first with no jump past the second,
# so everything the first chain encoded went on into the second, matched
# nothing there, and was refused by its final else: "unsupported x86_64
# mnemonic: test" -- and xchg, cmpxchg, cpuid, the rotates, every SSE and
# SSE2 instruction, the x87 register forms.  (AS-T-020.)
#
# And where a branch of the first chain set REX.W for anything that was
# not a byte operation, `roll $3, %eax` rotated %rax.
#
# The bytes are GNU as's.  Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

# enc INSTRUCTION BYTES
enc() {
    printf '\t.text\n\t%s\n' "$1" > t.s
    if ! "$AS" --64 -o t.o t.s 2> err; then
        echo "FAIL $1: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    if [ "$got" != "$2" ]; then echo "FAIL $1: $got, not $2"; fail=1; fi
}

enc 'testl %eax, %ebx'           85c3
enc 'testq %rdi, %rdi'           4885ff
enc 'adcl %eax, %ebx'            11c3
enc 'sbbq %rax, %rbx'            4819c3
enc 'roll $3, %eax'              c1c003
enc 'rolq $3, %rax'              48c1c003
enc 'rorq %cl, %rax'             48d3c8
enc 'rorl %cl, %eax'             d3c8
enc 'shll %eax'                  d1e0
enc 'sarq $1, (%rbx)'            48d13b
enc 'shrl $4, (%rbx)'            c12b04
enc 'negl %eax'                  f7d8
enc 'notq %rax'                  48f7d0
enc 'imull $10, %ebx, %eax'      6bc30a
enc 'xchgl %ecx, %ebx'           87cb
enc 'xchgq %rcx, %rbx'           4887cb
enc 'xaddl %eax, (%rbx)'         0fc103
enc 'xaddq %rax, (%rbx)'         480fc103
enc 'cmpxchgl %ebx, (%rax)'      0fb118
enc 'btl $3, %eax'               0fbae003
enc 'bsfl %ebx, %eax'            0fbcc3
enc 'bsrq %rbx, %rax'            480fbdc3
enc 'cpuid'                      0fa2
enc 'rdtsc'                      0f31
enc 'lgdt (%rax)'                0f0110
enc 'invlpg (%rax)'              0f0138
enc 'fnstsw %ax'                 dfe0
enc 'fld %st(1)'                 d9c1
enc 'fxch %st(1)'                d9c9
enc 'movd %eax, %xmm0'           660f6ec0
enc 'sqrtsd %xmm1, %xmm0'        f20f51c1
enc 'xorps %xmm0, %xmm0'         0f57c0
enc 'addpd %xmm1, %xmm2'         660f58d1
enc 'mulps (%rax), %xmm3'        0f5918
enc 'andps %xmm1, %xmm2'         0f54d1
enc 'movhlps %xmm1, %xmm2'       0f12d1
enc 'cvtsd2si %xmm0, %eax'       f20f2dc0
enc 'cvtps2pd %xmm1, %xmm2'      0f5ad1
enc 'punpcklbw %xmm1, %xmm2'     660f60d1
enc 'pcmpeqd %xmm1, %xmm2'       660f76d1
enc 'pshuflw $1, %xmm1, %xmm2'   f20f70d101
enc 'shufps $1, %xmm1, %xmm2'    0fc6d101
enc 'movmskps %xmm1, %eax'       0f50c1
enc 'ldmxcsr (%rax)'             0fae10
enc 'inb $0x60, %al'             e460
enc 'outb %al, $0x60'            e660

# What neither chain knows is still refused, by name.
printf '\tnosuchinsn %%eax\n' > t.s
if "$AS" --64 -o t.o t.s 2> err; then echo "FAIL: nosuchinsn assembled"; fail=1; fi
grep -q nosuchinsn err || { echo "FAIL: the refusal does not name the mnemonic"; fail=1; }

[ "$fail" -eq 0 ] && echo "ok: x86-64 first chain"
exit "$fail"
