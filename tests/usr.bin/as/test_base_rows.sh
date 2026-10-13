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

[ "$fail" -eq 0 ] && echo "ok: the rows of the instructions of general registers ($cases cases)"
exit "$fail"
