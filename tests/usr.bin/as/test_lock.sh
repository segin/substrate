#!/bin/sh
# A lock prefix that is written is assembled (AS-T-030).
#
# In 32-bit mode cmpxchg, cmpxchg8b and the bit-test instructions are
# encoded by a function that is not given the instruction's prefixes, and
# `lock cmpxchgl %ebx, (%eax)` came out as 0f b1 18: the compare and
# exchange every lock and atomic counter is made of, not atomic, with
# nothing to say so.
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

enc 32 'lock cmpxchgl %ebx, (%eax)'  f00fb118
enc 32 'lock cmpxchgb %bl, (%eax)'   f00fb018
enc 32 'lock cmpxchg8b (%eax)'       f00fc708
enc 32 'lock cmpxchgl %ebx, sym'     f00fb11d00000000
enc 32 'lock btsl $3, (%eax)'        f00fba2803
enc 32 'lock btrl %eax, (%eax)'      f00fb300
enc 32 'lock btcl $1, (%eax)'        f00fba3801
enc 32 'lock xaddl %eax, (%ebx)'     f00fc103
enc 32 'lock xaddb %al, (%eax)'      f00fc000
enc 32 'lock incl (%eax)'            f0ff00
enc 32 'lock decl (%eax)'            f0ff08
enc 32 'lock orl %eax, (%eax)'       f00900
enc 32 'lock subl %eax, (%eax)'      f02900
enc 32 'lock xorl %eax, (%eax)'      f03100
enc 32 'lock adcl %eax, (%eax)'      f01100
enc 32 'lock sbbl %eax, (%eax)'      f01900
enc 32 'lock negl (%eax)'            f0f718
enc 32 'lock notl (%eax)'            f0f710
enc 32 'lock xchgl %eax, (%eax)'     f08700
# Without the prefix, without the byte.
enc 32 'cmpxchgl %ebx, (%eax)'       0fb118

enc 64 'lock cmpxchgl %ebx, (%rax)'  f00fb118
enc 64 'lock cmpxchgq %rbx, (%rax)'  f0480fb118
enc 64 'lock cmpxchg8b (%rax)'       f00fc708
enc 64 'lock btsl $3, (%rax)'        f00fba2803
enc 64 'lock xaddl %eax, (%rbx)'     f00fc103
enc 64 'lock incl (%rax)'            f0ff00
enc 64 'lock negl (%rax)'            f0f718

# With a segment override the two prefixes may come in either order; both
# must be there, once, before the opcode.
printf '\t.text\n\tlock cmpxchgl %%ebx, %%fs:(%%eax)\n' > t.s
"$AS" --32 -o t.o t.s 2> err || { echo "FAIL lock with %fs: $(head -1 err)"; fail=1; }
objcopy -O binary -j .text t.o t.bin
case "$(od -An -v -tx1 t.bin | tr -d ' \n')" in
f0640fb118|64f00fb118) ;;
*) echo "FAIL lock with %fs: $(od -An -v -tx1 t.bin | tr -d ' \n')"; fail=1 ;;
esac

[ "$fail" -eq 0 ] && echo "ok: lock"
exit "$fail"
