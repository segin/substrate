#!/bin/sh
# .code16, .code32 and .code64 (AS-T-345, and what of 347 has no
# symbol in it).
#
# What follows one of them is encoded for that mode, whatever --32 or
# --64 said and whatever the object is.  It was encoded for the mode of
# the command line in all but its 16-bit addresses: under --64,
# `.code32` and `inc %eax` was the two bytes of 64-bit code and not the
# one of 32-bit, `push %eax` was refused, and in an ELF32 object
# `.code64` and `movq %rax, %rbx` was "a register of 64-bit code".
#
# And in 16-bit code an instruction of 32-bit registers had no prefix
# unless a suffix said so: `xor %eax, %eax` was `xor %ax, %ax`, and
# `push %eax` pushed two bytes.
#
# A case is the option, GNU as 2.46's bytes (or `refused`) and the
# source, a `|` for each new line.  GNU as passes this test.
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

# The mode is the directive's.
t 64 89c3 '.code32|movl %eax,%ebx'
t 64 6689c3 '.code16|movl %eax,%ebx'
t 64 89c3 '.code16|movw %ax,%bx'
t 64 5040 '.code32|push %eax|inc %eax'
t 64 608b18 '.code32|pusha|movl (%eax),%ebx'
t 64 40ffc0 '.code32|inc %eax|.code64|inc %eax'
t 64 506650 '.code16|push %ax|push %eax'
t 64 refused '.code32|movq %rax,%rbx'
t 64 refused '.code32|push %rax'
t 64 refused '.code16|mov %r8,%rax'
t 32 4889c3 '.code64|movq %rax,%rbx'
t 32 50ffc04d89c1 '.code64|push %rax|inc %eax|mov %r8,%r9'
t 32 refused '.code64|push %eax'
t 32 31c06631c0 '.code16|.code32|xor %eax,%eax|xor %ax,%ax'

# 16-bit code: the prefix is of what is 32 bits wide.
t 32 6689c36631c06650 '.code16|movl %eax,%ebx|xor %eax,%eax|push %eax'
t 32 31c050 '.code16|xor %ax,%ax|push %ax'
t 32 8b00678b00 '.code16|mov (%bx,%si),%ax|mov (%eax),%ax'
t 32 67668b444b04 '.code16|mov 4(%ebx,%ecx,2),%eax'
t 32 ffd066ffd0 '.code16|call *%ax|call *%eax'
t 32 66830701830701 '.code16|addl $1,(%bx)|addw $1,(%bx)'
t 32 660fb6040fb604 '.code16|movzbl (%si),%eax|movzbw (%si),%ax'
t 32 666a016a016a01 '.code16|pushl $1|push $1|pushw $1'
t 32 ab66abf3a4 '.code16|stosw|stosl|rep movsb'
t 32 0f20c0c366c3c9c8080000 '.code16|mov %cr0,%eax|ret|retl|leave|enter $8,$0'
t 32 66c1e003c1e003666bd803 '.code16|shl $3,%eax|shl $3,%ax|imul $3,%eax,%ebx'
t 32 6685c06683f801668d4704678d4304 '.code16|test %eax,%eax|cmp $1,%eax|lea 4(%bx),%eax|lea 4(%ebx),%ax'
t 32 740190ebfe '.code16|je 1f|nop|1:|jmp 1b'
t 32 e200e3fe67e3fb '.code16|loop 1f|1:|jcxz 1b|jecxz 1b'

# .code16gcc (AS-T-348), which was "unknown directive": 16-bit code in
# which a call, a return and the instructions of the stack are of 32
# bits where nothing says otherwise.
t 32 66e80000000066c3 '.code16gcc|call f|f: ret'
t 32 66c3 '.code16gcc|ret'
t 32 66c2040066cbcf '.code16gcc|ret $4|lret|iret'
t 32 6650665b '.code16gcc|push %eax|pop %ebx'
t 32 50 '.code16gcc|push %ax'
t 32 666a01666a016a01 '.code16gcc|push $1|pushl $1|pushw $1'
t 32 66c808000066c9 '.code16gcc|enter $8,$0|leave'
t 32 669c669d66606661 '.code16gcc|pushf|popf|pusha|popa'
t 32 66ffd066ffe0 '.code16gcc|call *%eax|jmp *%eax'
t 32 6689c389c3 '.code16gcc|mov %eax,%ebx|mov %ax,%bx'
t 32 e200e3fe '.code16gcc|loop 1f|1:|jcxz 1b'
t 32 e80000c3 '.code16gcc|.code16|call f|f: ret'

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
