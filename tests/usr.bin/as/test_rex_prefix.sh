#!/bin/sh
# REX comes last among the prefixes, and REX.W only where the operand is
# 64 bits (AS-T-034, AS-T-066).
#
# The 64-bit encoder keeps a place for REX and then encodes the
# instruction, and many instructions begin with 66, F2 or F3.  Those went
# after the place: `pxor %xmm8, %xmm8` was 45 66 0f ef c0, where a REX not
# directly before the opcode is ignored -- so it was `pxor %xmm0, %xmm0`.
# Every SSE2 instruction on %xmm8-15 or with %r8-15 in its address, and
# `push %r9w`, used the wrong register.
#
# And `cvtsi2sd %eax, %xmm0` -- an int to a double -- had REX.W, and so
# converted all of %rax; `cvtsd2si %xmm0, %rax` had none and gave 32 bits.
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
    [ "$got" = "$2" ] || { echo "FAIL $1: $got, not $2"; fail=1; }
}

# The prefix, then REX, then the opcode.
enc 'pxor %xmm8, %xmm8'              66450fefc0
enc 'paddb %xmm1, %xmm10'            66440ffcd1
enc 'paddb %xmm9, %xmm10'            66450ffcd1
enc 'psubsw (%r8,%rcx,8), %xmm2'     66410fe914c8
enc 'pshufd $1, %xmm9, %xmm10'       66450f70d101
enc 'addsd %xmm9, %xmm1'             f2410f58c9
enc 'mulss (%r9), %xmm1'             f3410f5909
enc 'movd %r9d, %xmm0'               66410f6ec1
enc 'movq %rax, %xmm0'               66480f6ec0
enc 'movq %xmm0, %rax'               66480f7ec0
enc 'push %r9w'                      664151

# REX.W by the width of the integer.
enc 'cvtsi2sd %eax, %xmm0'           f20f2ac0
enc 'cvtsi2sdl %eax, %xmm0'          f20f2ac0
enc 'cvtsi2sd %rax, %xmm0'           f2480f2ac0
enc 'cvtsi2sdq %rax, %xmm0'          f2480f2ac0
enc 'cvtsi2sdl (%rax), %xmm0'        f20f2a00
enc 'cvtsi2sdq (%rax), %xmm0'        f2480f2a00
enc 'cvtsi2sd %r9d, %xmm10'          f2450f2ad1
enc 'cvtsi2ss %eax, %xmm1'           f30f2ac8
enc 'cvtsi2ss %r9, %xmm10'           f34d0f2ad1
enc 'cvtsi2ssq (%r9), %xmm1'         f3490f2a09
enc 'cvtsd2si %xmm0, %eax'           f20f2dc0
enc 'cvtsd2si %xmm0, %rax'           f2480f2dc0
enc 'cvtss2si %xmm0, %rax'           f3480f2dc0
enc 'cvttsd2si %xmm0, %eax'          f20f2cc0
enc 'cvttsd2si %xmm0, %rax'          f2480f2cc0

[ "$fail" -eq 0 ] && echo "ok: REX and the other prefixes"
exit "$fail"
