#!/bin/sh
# What is a register, and what is not (AS-T-096).
#
# The lexer, the parser and the encoders each had lists of the x86
# registers, a dozen in all, and they did not agree: a name was a
# register to one and not to the next -- %xmm99 to the lexer, any %rN to
# the parser -- and the lexer's list was of lower-case names only, so
# that `movl %EAX, %ebx` was refused.  There is one table now
# (as_x86_regs.h), and a name is a register if it is in it, in either
# case.
#
# Each case is an instruction and its bytes, which are GNU as's, or
# `refused`; GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is MODE BYTES SOURCE
is() {
    cases=$((cases + 1))
    printf '\t.text\n\t%s\n' "$3" > t.s
    rm -f t.o
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        got=refused
    else
        got=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    fi
    [ "$got" = "$2" ] || { echo "FAIL --$1 [$3]: $got, not $2"; fail=1; }
}

# In either case, and in both.
is 32 89c3     'movl %eax, %ebx'
is 32 89c3     'movl %EAX, %EBX'
is 32 89c3     'movl %Eax, %eBx'
is 32 88e0     'movb %AH, %AL'
is 32 8ed8     'movw %AX, %DS'
is 64 4c89c0   'movq %R8, %RAX'
is 64 4588c8   'movb %R9B, %r8b'
is 64 0f58c1   'addps %XMM1, %xmm0'
is 32 0f6fc1   'movq %MM1, %mm0'
is 32 d8c1     'fadd %ST(1), %st'
is 32 0f20c0   'movl %CR0, %eax'
is 32 648b03   'movl %FS:(%EBX), %eax'

# Every kind there is.
is 32 0f20c0   'movl %cr0, %eax'
is 32 0f21f8   'movl %db7, %eax'
is 32 0f21f8   'movl %dr7, %eax'
is 32 0f26d8   'movl %eax, %tr3'
is 64 4889e5   'movq %rsp, %rbp'
is 64 4088f7   'movb %sil, %dil'
is 64 8b0500000000 'movl (%rip), %eax'

# What is not a register is not one, whatever it looks like.
is 32 refused  'movl %nosuch, %eax'
is 32 refused  'movl %eaxx, %eax'
is 64 refused  'movq %r32, %rax'
is 64 refused  'addps %xmm32, %xmm0'
is 32 refused  'movq %mm8, %mm0'
is 64 refused  'kmovw %k8, %k1'
is 32 refused  'movl %cr16, %eax'
is 32 refused  'movw %ax, %hs'

[ "$fail" -eq 0 ] && echo "ok: register names ($cases cases)"
exit "$fail"
