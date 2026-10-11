#!/bin/sh
# What is of one mode and is written in another (AS-T-349, 350).
#
# An instruction, a register or a suffix that only 64-bit code has is
# refused in 32-bit code, and what 64-bit code has not -- a push of 32
# bits, the decimal adjustments, `pusha`, `into`, `bound`, the far
# branches to a number -- is refused there.  They were mended one by
# one under other tasks; this holds the whole of the two lists to GNU
# as 2.46's verdict, and found `enterl $1, $2` taken in 64-bit code for
# the frame `enterq` makes.
#
# A case is the option, GNU's bytes or `refused`, and the source, a `|`
# for each new line.  GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

t() {
    cases=$((cases + 1))
    printf '\t.text\n%s\n' "$3" | tr '|' '\n' > t.s
    rm -f t.o
    if "$AS" "--$1" -o t.o t.s > out 2>&1; then
        objcopy -O binary --only-section=.text t.o text.bin 2> /dev/null
        got=$(od -An -v -tx1 text.bin | tr -d ' \n')
    else
        got=refused
    fi
    [ "$got" = "$2" ] || { echo "FAIL --$1 [$3]: $got, and GNU as: $2"; fail=1; }
}

# Of 64-bit code, in 32-bit code.
t 32 refused 'pushq %rax'
t 32 refused 'incq (%eax)'
t 32 refused 'retq'
t 32 refused 'leaveq'
t 32 refused 'callq foo'
t 32 refused 'jmpq *%eax'
t 32 refused 'stosq'
t 32 refused 'rex movb %al,%bl'
t 32 refused 'in $0x60,%rax'
t 32 refused 'addq $1,%rax'
t 32 refused 'movq %rax,%rbx'
t 32 refused 'mov %r8,%rax'
t 32 refused 'mov %sil,%al'
t 32 refused 'mov %r8d,%eax'
t 32 0f05 'syscall'
t 32 refused 'swapgs'
t 32 refused 'sysretq'
t 32 refused 'cdqe'
t 32 refused 'cqo'
t 32 refused 'movsxd %eax,%rax'
t 32 refused 'movabs $1,%rax'
t 32 refused 'popq %rax'
t 32 refused 'pushfq'
t 32 refused 'iretq'
t 32 refused 'cmpxchg16b (%eax)'
t 32 refused 'movslq %eax,%rax'
t 32 refused 'lodsq'
t 32 refused 'xchg %rax,%rax'

# Not of 64-bit code, in it.
t 64 refused 'push %eax'
t 64 refused 'pop %ebx'
t 64 refused 'into'
t 64 refused 'salc'
t 64 refused 'pushfl'
t 64 refused 'popfl'
t 64 refused 'retl'
t 64 refused 'jmp *%eax'
t 64 refused 'calll *(%rax)'
t 64 refused 'pusha'
t 64 refused 'popa'
t 64 refused 'popal'
t 64 refused 'aaa'
t 64 refused 'aas'
t 64 refused 'aam'
t 64 refused 'aad'
t 64 refused 'daa'
t 64 refused 'das'
t 64 refused 'bound %eax,(%rax)'
t 64 refused 'les (%rax),%eax'
t 64 refused 'lds (%rax),%eax'
t 64 refused 'pushl $1'
t 64 refused 'push %cs'
t 64 refused 'pop %ds'
t 64 refused 'lcall $1,$2'
t 64 refused 'ljmp $1,$2'
t 64 refused 'arpl %ax,%bx'
t 64 refused 'leavel'
t 64 refused 'enterl $1,$2'
t 64 refused 'jcxz 1f|1:'
t 64 refused 'loopw 1f|1:'

# And what it has, in the form it has it.
t 64 ffc0 'inc %eax'
t 64 ffcb 'dec %ebx'
t 64 0fa0 'push %fs'
t 64 66ffe0 'jmp *%ax'
t 64 ffd0 'call *%rax'
t 64 6650 'push %ax'
t 64 666a01 'pushw $1'
t 64 8cc8 'mov %cs,%eax'
t 64 67e300 'jecxz 1f|1:'
t 64 e300 'jrcxz 1f|1:'
t 64 0f34 'sysenter'
t 64 0f35 'sysexit'
t 64 cf 'iretl'

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
