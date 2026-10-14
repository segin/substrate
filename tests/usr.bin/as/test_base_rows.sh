#!/bin/sh
# The rows of the instructions of general registers: what went wrong
# as they came to the table, each held to what GNU as 2.47 makes of it.
#
# The rows are generated from GNU as's own output, a family at a time
# (as_x86_vec_table_base.h), and the forms files hold the bulk of them.
# These are the statements a generated row once had wrong, or took from
# the encoder that had them right:
#
#   - `ror %al, %cl`: D2 /1 has a 1 where %cl's number would be, and
#     the row was of any two byte registers;
#   - `or $1, %eax`: 83 /1, and not the accumulator's 0D with four bytes;
#   - `imul $5, %ecx`: the register is in ModRM.reg as well as in r/m;
#   - `adcb %r16b, %r17b`: %r16b is not %ah;
#   - `mov sym@GOT, %eax` keeps its ModRM, and a store there does not;
#   - `pushw $0xffff` is the byte it is once cut to 16 bits;
#   - a displacement 32 bits do not hold is refused of any form;
#   - `{load}` asks for the other way about.
#
# GNU as passes this.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# is MODE BYTES STATEMENT: the statement assembles to those bytes.
is() {
    cases=$((cases + 1))
    printf '%s\n' "$3" > t.s
    rm -f t.o t.bin
    if ! "$AS" --"$1" -o t.o t.s > out 2>&1; then
        echo "FAIL --$1 [$3]: $(tail -1 out)"; fail=1; return
    fi
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 t.bin | tr -d ' \n')
    [ "$got" = "$2" ] || { echo "FAIL --$1 [$3]: $got, not $2"; fail=1; }
}

# starts MODE BYTES STATEMENT: what it assembles to begins with them.
starts() {
    cases=$((cases + 1))
    printf '%s\n' "$3" > t.s
    rm -f t.o t.bin
    if ! "$AS" --"$1" -o t.o t.s > out 2>&1; then
        echo "FAIL --$1 [$3]: $(tail -1 out)"; fail=1; return
    fi
    objcopy -O binary -j .text t.o t.bin
    got=$(od -An -v -tx1 -N ${#2} t.bin | tr -d ' \n' | cut -c1-${#2})
    [ "$got" = "$2" ] || { echo "FAIL --$1 [$3]: $got, not $2..."; fail=1; }
}

# refused MODE STATEMENT: it is no instruction.
refused() {
    cases=$((cases + 1))
    printf '%s\n' "$2" > t.s
    if "$AS" --"$1" -o t.o t.s > out 2>&1; then
        echo "FAIL --$1 [$2]: assembled"; fail=1
    fi
}

refused 32 'ror %al, %cl'
refused 64 'ror %al, %cl'
refused 64 'ror %al, 0x12345678'
refused 64 'rorb %dl, (%rax)'
is 32 d2c9 'ror %cl, %cl'
is 32 d0c9 'ror %cl'
is 32 83c801 'or $1, %eax'
is 32 0d78563412 'or $0x12345678, %eax'
is 64 6683c8ff 'or $-1, %ax'
is 32 fec9 'dec %cl'
is 32 6bc905 'imul $5, %ecx'
is 64 486bd17f 'imul $0x7f, %rcx, %rdx'
is 64 d55010c1 'adcb %r16b, %r17b'
is 64 10e1 'adcb %ah, %cl'
refused 64 'adcb %ah, %sil'
is 32 8b0500000000 'movl sym@GOT, %eax'
is 32 a300000000 'movl %eax, sym@GOT'
is 32 a100000000 'movl sym, %eax'
is 64 8b042500000000 'movl sym, %eax'
is 32 666aff 'pushw $0xffff'
is 32 6aff 'pushl $0xffffffff'
is 32 83c1ff 'addl $0xffffffff, %ecx'
refused 64 'addl $4, 0x123456789(%rax)'
refused 64 'incl 0x80000000(%rax)'
is 32 33d1 '{load} xorl %ecx, %edx'
is 32 31ca 'xorl %ecx, %edx'
is 32 41 'inc %ecx'
is 64 ffc1 'inc %ecx'
is 64 d51157 'push %r31'
is 64 62f47f1847c0 'seta %eax'
is 32 d8c3 'fadd %st(3), %st'
is 32 dcc3 'fadd %st, %st(3)'
is 32 c20800 'ret $8'
# (A symbol that marks a call for the linker is no part of its address.)
is 32 ff10 'call *sym@TLSCALL(%eax)'
is 32 ff5008 'call *8(%eax)'
is 64 ffd1 'call *%rcx'

# What asking the table alone for a name found.  `{load}` and `{store}`
# before an instruction with one way to be written are that way:
is 32 0f44d8 '{load} cmove %eax, %ebx'
is 32 0fa3c3 '{store} bt %eax, %ebx'
is 32 0f94c0 '{store} sete %al'
# a lock goes after the prefixes of size, and xacquire ahead of it:
is 32 66f0830001 'lock addw $1, (%eax)'
is 32 f2f00fba2801 'xacquire lock btsl $1, (%eax)'
refused 32 'lock btsl $1, %eax'
# nor is one taken before what is no row of the table and is assembled
# after it: a lock there was written, once the check was thought the
# table's alone.
refused 32 'lock extrq %xmm1, %xmm2'
refused 32 'lock xbegin 0x10'
refused 64 'lock xbegin 0x10'
refused 64 'lock rex.w'
# the instructions that wait for an event: the time's registers may be
# written and are in no byte, and umonitor's register is an address,
# with the prefix that says so where it is not of the code's size.
is 32 660faef3 'tpause %ebx, %edx, %eax'
is 64 f20faef1 'umwait %ecx, %edx, %eax'
refused 32 'tpause %ebx, %eax, %edx'
is 32 67f30faef0 'umonitor %ax'
is 32 f30faef0 '.code16
umonitor %ax'
is 32 67f30faef0 '.code16
umonitor %eax'
is 64 67f3410faef2 'umonitor %r10d'
refused 64 'umonitor %ax'
# PadLock's names with a hyphen, and a repeat written before one:
is 32 f30fa7c8 'xcrypt-ecb'
is 64 f30fa7d0 'rep xcrypt-cbc'
# movabs to and from an address of eight bytes, which is rows too:
is 64 a08877665544332211 'movabs 0x1122334455667788, %al'
is 64 66a18877665544332211 'movabs 0x1122334455667788, %ax'
is 64 48a31000000000000000 'movabsq %rax, 0x10'
is 64 6448a11000000000000000 'movabs %fs:0x10, %rax'
refused 64 'movabs 0x10, %ebx'
refused 32 'movabs 0x10, %eax'
# xbegin with data16 is of two bytes of distance; a REX written alone
# has what is written before it, in GNU's order:
is 32 66c7f80000 'data16 xbegin 1f
1:'
is 64 66c7f80000 'data16 xbegin 1f
1:'
is 64 6648 'data16 rex.w'
is 64 64676641 'data16 fs addr32 rex.b'
refused 64 'lock rex.w'
# A branch to a number is a relocation with no symbol, after any prefix
# and of the size data16 makes it; in ELF32 the addend is in the bytes,
# in ELF64 in the record:
is 32 3e0f850c000000 'ds jne 0x10'
is 32 66e90e00 'data16 jmp 0x10'
is 32 663e0f850e00 'ds data16 jne 0x10'
is 32 0f850e00 '.code16
jne 0x10'
is 64 e90000 '.code16
jmp 0x10'
is 32 c7f80c000000 'xbegin 0x10'
# two bytes in the object hold a number of either sign, and no more:
is 32 66e9ffff 'data16 jmp 0x10001'
refused 32 'data16 jmp 0x10002'
refused 32 '.code16
jne 0x14943'
is 64 0f850000 '.code16
jne 0x14943'
# To a label in 16-bit code a jump's two bytes go round, and a call's
# are of either sign:
starts 32 0f851000 '.code16
jne 1f
.fill 0x10010
1:'
starts 32 e80090 '.code16
call 1f
.fill 0x9000
1:'
refused 32 '.code16
call 1f
.fill 0x10010
1:'
refused 32 '.code16
xbegin 1f
.fill 0x9000
1:'
# A branch to a label is the table's too: a prefix that says nothing
# to it is left out, one that says what it has is refused, and the
# distance is from the end of all of them:
is 64 eb00 'jmp 1f
1:'
is 64 eb00 'addr32 jmp 1f
1:'
is 32 e200 'data16 loop 1f
1:'
is 32 66c7f80000 'cs data16 xbegin 1f
1:'
is 32 e200 'loopl 1f
1:'
is 32 67e200 'loopw 1f
1:'
is 64 e2fe '1: loopq 1b'
refused 64 'addr32 jecxz 1f
1:'
refused 32 'data16 callw sym'
starts 64 673ee27e '.code64
ds addr32 loop 1f
.fill 0x7e
1:'
# A REX.W written where the instruction has its own is the prefix twice,
# but not where REX2 takes it in:
refused 64 'rex.w lretq'
refused 64 'rex.w movq %rax, %rbx'
is 64 d5d8bfd1 'rex.w movsx %r17w, %r18'
# and the four of the coprocessor that store its state after a wait:
is 32 9bdd30 'fsave (%eax)'
is 32 9b2667d93f 'fstcw %es:(%bx)'
is 64 9bdfe0 'fstsw %ax'
is 32 9bd930 'fstenv (%eax)'
# `{disp8}` before an address of 16-bit registers with no displacement
# is one of nothing, and `{disp32}` asks for what such an address lacks:
is 32 114f00 '.code16
{disp8} adc %cx, (%bx)'
refused 32 '.code16
{disp32} btl %eax, (%bx)'
# and neither outlasts its statement:
is 32 318800000000831001 '{disp32} xorl %ecx, (%eax)
adcl $1, (%eax)'
# A number too large for where it goes keeps its low bytes; one that is
# a byte once cut is the byte:
is 32 6681c17856 'addw $0x12345678, %cx'
is 32 80c134 'addb $0x1234, %cl'
is 32 0434 'addb $0x1234, %al'
is 32 6683c180 'addw $0xffffff80, %cx'
refused 32 'roll $0x1234, %eax'
# With no suffix and no register the size is the code's:
is 32 830001 'add $1, (%eax)'
is 32 810034120000 'add $0x1234, (%eax)'
is 32 81073412 '.code16
add $0x1234, (%bx)'
is 64 66ffc9 'dec %cx'
is 32 c70001000000 'mov $1, (%eax)'
is 64 48c7c001000000 'mov $1, %rax'
is 32 d320 'shl %cl, (%eax)'
# The accumulator has no form of its own with a shift's count, and a
# product's number may be more than a byte with it:
is 32 c0e07f 'shlb $0x7f, %al'
is 32 d2c8 'ror %cl, %al'
is 32 6669c03412 'imul $0x1234, %ax'
# A repeat before bsf is the byte; xrelease before a move to memory is
# of the form with ModRM, and not the accumulator's with an address:
is 32 f30fbcd1 'rep bsf %ecx, %edx'
is 32 f3890500000000 'xrelease mov %eax, sym'
refused 32 'xacquire mov %eax, sym'
# What is pushed with no suffix is of the code's size; a number that is
# a byte once cut to it is the byte, and one that 64 bits are not made
# of by the sign of four bytes is refused:
is 32 6880000000 'push $128'
is 32 6aff 'push $0xffffffff'
is 64 6a01 'push $1'
refused 64 'push $0xffffffff'
is 32 6668ffff 'pushw $0x7fffffff'
# The segment registers, the first four in an opcode of one byte and
# not in 64-bit code, %fs and %gs in one of two; %cs is not popped:
is 32 1e 'push %ds'
is 32 0fa0 'push %fs'
is 32 660fa0 'pushw %fs'
is 32 0fa9 'pop %gs'
is 64 0fa0 'push %fs'
refused 64 'push %es'
refused 32 'pop %cs'
# A name that is all rows is refused, not handed on, where no row has
# that many operands -- `retl $4` was not one, and was assembled:
is 32 c20400 'retl $4'
# mov: a number that four bytes extended by their sign do not make is
# moved with eight; a control register with a suffix; `{rex}` where
# there may be no REX is left out, and `rex` is not; `{load}` of two
# byte registers; and a displacement cut to an address's sixteen bits:
is 64 48b8ffffffff00000000 'mov $0xffffffff, %rax'
is 64 48c7c0ffffffff 'mov $-1, %rax'
is 32 0f20c0 'movl %cr0, %eax'
is 64 88e0 '{rex} movb %ah, %al'
refused 64 'rex movb %ah, %al'
is 32 8ac4 '{load} movb %ah, %al'
is 32 678b870000 'movl 0x10000(%bx), %eax'
# movq of general registers shares its name with the one of vector
# registers, and a prefix before it is taken as before any move:
is 64 4889c3 'movq %rax, %rbx'
is 64 664889c3 'data16 movq %rax, %rbx'
is 64 644889c3 'fs movq %rax, %rbx'
is 64 66480f7ec0 'movq %xmm0, %rax'
# int's third has a byte to itself:
is 32 cc 'int $3'
is 32 cd80 'int $0x80'
refused 32 'int $256'
is 64 f3480faec0 'rdfsbase %rax'

[ "$fail" -eq 0 ] && echo "ok: the rows of the instructions of general registers ($cases cases)"
exit "$fail"
