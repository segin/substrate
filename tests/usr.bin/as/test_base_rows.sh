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

[ "$fail" -eq 0 ] && echo "ok: the rows of the instructions of general registers ($cases cases)"
exit "$fail"
