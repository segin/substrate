#!/bin/sh
# Prefixes, and the count of operands.
#
#   - A segment written as a prefix -- `fs flds (%eax)` -- was dropped
#     by every emitter that is handed the statement and not its
#     prefixes: x87, MMX and SSE moves, the 0F-map instructions.  The
#     instruction read from %ds.
#   - lock is for a short list of instructions, and only where the
#     destination is in memory.  `lock addl %eax, %ebx` and `lock addps`
#     were assembled; `lock fldl (%eax)` was assembled without it.
#   - lock and the rep prefixes as a statement of their own were a parse
#     error: `lock; cmpxchgl %ebx, (%eax)`, `rep; nop`.
#   - data16 is the byte 66 before whatever the instruction is; it was
#     made the instruction's size, and a suffix then undid it.
#   - movabs to or from %ax had no 66; movabsb, w and l were unknown.
#   - A statement with more operands than any form of its instruction
#     takes was assembled from the ones the emitter looked at.
#
# The bytes and the refusals are those of GNU as 2.46.  ` | ` stands for
# the end of a line.
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
        { printf '\t.text\n'; printf '%s\n' "$3" | sed 's/ | /\n/g'; } > t.s
        rm -f t.o
        if "$AS" "-$mode" -o t.o t.s > out 2>&1; then
            objcopy -O binary --only-section=.text t.o text.bin 2>/dev/null
            got=$(od -An -v -tx1 text.bin | tr -d ' \n')
        else
            got=refused
        fi
        if [ "$got" != "$2" ]; then
            [ "$got" = refused ] && got="refused ($(head -1 out))"
            echo "FAIL $mode [$3]: $got, and GNU as: $2"
            fail=1
        fi
    done
}

# A segment as a prefix.
t "32"    648b18           'fs movl (%eax),%ebx'
t "32"    3e8b18           'ds movl (%eax),%ebx'
t "32"    65ff00           'gs incl (%eax)'
t "32"    2e0fb608         'cs movzbl (%eax),%ecx'
t "32"    64d900           'fs flds (%eax)'
t "32"    64dd38           'fs fnstsw (%eax)'
t "32"    64d928           'fs fldcw (%eax)'
t "32"    650f2800         'gs movaps (%eax),%xmm0'
t "32"    260f1008         'es movups (%eax),%xmm1'
t "32"    64660f6e00       'fs movd (%eax),%xmm0'
t "32"    640f6f00         'fs movq (%eax),%mm0'
t "32"    640ffc08         'fs paddb (%eax),%mm1'
t "32"    64f20f2a00       'fs cvtsi2sd (%eax),%xmm0'
t "32"    640fae00         'fs fxsave (%eax)'
t "32"    640fae38         'fs clflush (%eax)'
t "32"    650f1800         'gs prefetchnta (%eax)'
t "32"    650fb108         'gs cmpxchg %ecx,(%eax)'
t "32"    640fa303         'fs bt %eax,(%ebx)'
t "32"    640fa40301       'fs shld $1,%eax,(%ebx)'
t "32"    65f30fb808       'gs popcnt (%eax),%ecx'
t "32"    640fbc08         'fs bsf (%eax),%ecx'
t "32"    640f0000         'fs sldt (%eax)'
t "32"    260f0110         'es lgdt (%eax)'
t "64"    648b18           'fs movl (%rax),%ebx'
t "64"    650f2800         'gs movaps (%rax),%xmm0'
t "64"    64d900           'fs flds (%rax)'
t "64"    64660f6e00       'fs movd (%rax),%xmm0'
t "64"    65f30f7e00       'gs movq (%rax),%xmm0'
t "64"    6467dd00         'fs fldl (%eax)'
# On the operand, where it was right, and with lock.
t "32"    64d900           'flds %fs:(%eax)'
t "32"    650f284004       'movaps %gs:4(%eax),%xmm0'
t "32"    64f00fb108       'lock fs cmpxchg %ecx,(%eax)'
t "32"    64f0ff00         'fs lock incl (%eax)'
t "64"    64f0480fb108     'fs lock cmpxchg %rcx,(%rax)'
t "32"    6490             'fs nop'
t "32"    2e90             'cs nop'

# Where lock may stand.
t "32"    f00103           'lock addl %eax,(%ebx)'
t "32"    f0ff00           'lock incl (%eax)'
t "32"    f0f718           'lock negl (%eax)'
t "32"    f08703           'lock xchg %eax,(%ebx)'
t "32"    f08703           'lock xchg (%ebx),%eax'
t "32"    f00fab03         'lock bts %eax,(%ebx)'
t "32"    f00fb103         'lock cmpxchg %eax,(%ebx)'
t "32"    f00fc708         'lock cmpxchg8b (%eax)'
t "32 64" refused          'lock addl %eax,%ebx'
t "32"    refused          'lock addl (%ebx),%eax'
t "32"    refused          'lock subl (%eax),%ebx'
t "32"    refused          'lock incl %eax'
t "32"    refused          'lock notl %eax'
t "32"    refused          'lock orl $1,%eax'
t "32"    refused          'lock xchg %eax,%ebx'
t "32"    refused          'lock bts %eax,%ebx'
t "32"    refused          'lock cmpxchg %eax,%ebx'
t "32"    refused          'lock xadd %eax,%ebx'
t "32"    refused          'lock bt %eax,(%ebx)'
t "32"    refused          'lock cmpl %eax,(%ebx)'
t "32"    refused          'lock testl %eax,(%ebx)'
t "32"    refused          'lock movl %eax,(%ebx)'
t "32"    refused          'lock movl (%eax),%ebx'
t "32"    refused          'lock shll $1,(%eax)'
t "32"    refused          'lock imull (%eax),%ebx'
t "32"    refused          'lock pushl (%eax)'
t "32"    refused          'lock leal (%eax),%ebx'
t "32"    refused          'lock jmp *(%eax)'
t "32"    refused          'lock ret'
t "32"    refused          'lock movsb'
t "32"    refused          'lock fldl (%eax)'
t "32"    refused          'lock addps (%eax),%xmm0'
t "32 64" refused          'lock nop'

# A prefix as a statement of its own: its byte, where it stands.
t "32"    f00fb118         'lock; cmpxchgl %ebx,(%eax)'
t "32"    f0ff00           'lock; incl (%eax)'
t "32"    f0ff00           'lock | incl (%eax)'
t "32"    f390             'rep; nop'
t "32"    f3a4             'rep | movsb'
t "32"    f2ae             'repne; scasb'
t "32"    f3f3a4           'rep | rep | movsb'
t "32"    648b18           'fs; movl (%eax),%ebx'
t "32"    f090             'lock | nop'
t "32 64" f0               'lock'
t "32 64" f3               'rep'
t "32 64" f3               'repe'
t "32 64" f2               'repnz'
t "32"    f3a4             'rep movsb'
t "32"    f3c3             'rep ret'

# data16 is the byte.
t "32 64" 6689c3           'data16 movl %eax,%ebx'
t "32 64" 6631c0           'data16 xorl %eax,%eax'
t "32 64" 66b801000000     'data16 movl $1,%eax'
t "32 64" 6681fbe8030000   'data16 cmpl $1000,%ebx'
t "64"    664889c3         'data16 movq %rax,%rbx'
t "32 64" 6688c3           'data16 movb %al,%bl'
t "32"    6650             'data16 pushl %eax'
t "32"    66ff00           'data16 incl (%eax)'
t "32 64" 6690             'data16 nop'
t "32 64" 66c3             'data16 ret'
t "32 64" 66c9             'data16 leave'
t "32 64" 6699             'data16 cltd'
t "32 64" refused          'data16 movw %ax,%bx'

# movabs at each width.
t "64"    66a30000000000000000 'movabs %ax,sym'
t "64"    66a10000000000000000 'movabs sym,%ax'
t "64"    66a38877665544332211 'movabs %ax,0x1122334455667788'
t "64"    66a18877665544332211 'movabs 0x1122334455667788,%ax'
t "64"    66a38877665544332211 'movabsw %ax,0x1122334455667788'
t "64"    66a18877665544332211 'movabsw 0x1122334455667788,%ax'
t "64"    a28877665544332211   'movabs %al,0x1122334455667788'
t "64"    a28877665544332211   'movabsb %al,0x1122334455667788'
t "64"    a18877665544332211   'movabs 0x1122334455667788,%eax'
t "64"    a38877665544332211   'movabsl %eax,0x1122334455667788'
t "64"    48a38877665544332211 'movabs %rax,0x1122334455667788'
t "64"    48a38877665544332211 'movabsq %rax,0x1122334455667788'
t "64"    48b88877665544332211 'movabsq $0x1122334455667788,%rax'
t "64"    48b80100000000000000 'movabs $1,%rax'
t "64"    a30000000000000000   'movabs %eax,sym'
t "64"    a20000000000000000   'movabs %al,sym'

# The 16-bit selector instructions on memory have no prefix for a w.
t "64"    0f0000           'sldtw (%rax)'
t "64"    660f00c0         'sldt %ax'

# More operands than any form takes.
t "32 64" refused          'ret $4, $5'
t "32 64" refused          'nop %eax,%ebx'
t "32 64" refused          'nop %eax,%ebx,%ecx,%edx'
t "64"    refused          'lahf -0x100(%rbx),%ebx,%ecx'
t "64"    refused          'haddps $1,%xmm1,%zmm2,%zmm3'
t "32 64" refused          'movl %eax,%ebx,%ecx'
t "32 64" refused          'movaps %xmm0,%xmm1,%xmm2'
t "32 64" refused          'imull $2,%eax,%ebx,%ecx'
t "32"    refused          'pushl %eax,%ebx'
t "32 64" refused          'int $3,$4'
t "32 64" refused          'fld %st(1),%st(2)'
t "32 64" refused          'xlat %eax,%ebx'
for mn in cld hlt cpuid leave rdtsc syscall cmc fninit emms sfence ud2 cwtl cltd popf iret int3 wait lahf sahf clc pause; do
    t "32 64" refused      "$mn %eax"
done
# And the most each does take.
t "32 64" 660faef1         'tpause %ecx, %edx, %eax'
t "32 64" 0fa4c301         'shld $1,%eax,%ebx'
t "32 64" 6bd802           'imull $2,%eax,%ebx'
t "32 64" c8080000         'enter $8, $0'
t "32 64" c20400           'ret $4'

[ "$fail" -eq 0 ] && echo "ok: prefixes and operand counts ($cases cases)"
exit "$fail"
