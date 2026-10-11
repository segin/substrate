#!/bin/sh
# The x87 instructions with an operand in memory (AS-T-179).
#
# In 64-bit code they went through an emitter of ModRM that had no index
# register and no address without one, and in both modes some had no
# entry.  So
#
#   fildq (%rax,%rbx,8)   "unsupported x86_64 mnemonic: fildq", and so
#                         with any index for some twenty of them
#   fildq sym             the same, for an address that is a symbol
#   fst (%rax)            "unsupported operand form for 'fst'"
#   fadd (%eax)           "unsupported mnemonic: fadd", in 32-bit code
#   fsave %fs:(%rax)      64 9b 64 dd 30: the override twice, once on
#                         each side of the wait
#   fsave (%eax)          67 9b dd 30: the address size before the wait,
#                         which is an instruction of its own
#
# Every instruction of the family is tried here at every address below.
# What is expected is put together from the instruction's opcode and
# digit and the address's bytes, and GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# name, 9b if it waits first (- if not), opcode, digit.
cat > insns <<'EOF'
flds - d9 0
fldl - dd 0
fldt - db 5
fld - d9 0
fsts - d9 2
fstl - dd 2
fst - d9 2
fstps - d9 3
fstpl - dd 3
fstpt - db 7
fstp - d9 3
filds - df 0
fildl - db 0
fildq - df 5
fildll - df 5
fild - df 0
fists - df 2
fistl - db 2
fist - df 2
fistps - df 3
fistpl - db 3
fistpq - df 7
fistpll - df 7
fistp - df 3
fisttps - df 1
fisttpl - db 1
fisttpq - dd 1
fisttpll - dd 1
fisttp - df 1
fadds - d8 0
faddl - dc 0
fadd - d8 0
fmuls - d8 1
fmull - dc 1
fmul - d8 1
fcoms - d8 2
fcoml - dc 2
fcom - d8 2
fcomps - d8 3
fcompl - dc 3
fcomp - d8 3
fsubs - d8 4
fsubl - dc 4
fsub - d8 4
fsubrs - d8 5
fsubrl - dc 5
fsubr - d8 5
fdivs - d8 6
fdivl - dc 6
fdiv - d8 6
fdivrs - d8 7
fdivrl - dc 7
fdivr - d8 7
fiadds - de 0
fiaddl - da 0
fiadd - de 0
fimuls - de 1
fimull - da 1
ficoms - de 2
ficoml - da 2
ficom - de 2
ficomps - de 3
ficompl - da 3
fisubs - de 4
fisubl - da 4
fisubrs - de 5
fisubrl - da 5
fidivs - de 6
fidivl - da 6
fidivrs - de 7
fidivrl - da 7
fldcw - d9 5
fnstcw - d9 7
fstcw 9b d9 7
fnstsw - dd 7
fstsw 9b dd 7
fldenv - d9 4
fnstenv - d9 6
fstenv 9b d9 6
fnsave - dd 6
fsave 9b dd 6
frstor - dd 4
fbld - df 4
fbstp - df 6
EOF

# mode, the address, the prefixes it has (- if none), ModRM's mod and
# r/m, and the bytes after ModRM (- if none).
cat > addrs <<'EOF'
64 (%rax) - 0 0 -
64 8(%rax) - 1 0 08
64 (%rax,%rbx,8) - 0 4 d8
64 4(,%rcx,4) - 0 4 8d04000000
64 (%r12) 41 0 4 24
64 (%r13,%r9,2) 43 1 4 4d00
64 -129(%rbp,%r15) 42 2 4 3d7fffffff
64 sym - 0 4 2500000000
64 sym(%rip) - 0 5 00000000
64 sym(%rax) - 2 0 00000000
64 sym+8(%rax,%rbx,2) - 2 4 5800000000
64 %fs:(%rax) 64 0 0 -
64 (%eax) 67 0 0 -
64 %fs:8(%eax,%ebx,2) 6467 1 4 5808
32 (%eax) - 0 0 -
32 (%eax,%ebx,8) - 0 4 d8
32 4(,%ecx,4) - 0 4 8d04000000
32 sym - 0 5 00000000
32 sym(%ebx) - 2 3 00000000
32 %gs:sym 65 0 5 00000000
32 4(%esp) - 1 4 2404
EOF

while read -r mode addr prefix mod rm tail; do
    [ "$prefix" = - ] && prefix=
    [ "$tail" = - ] && tail=
    while read -r name wait op digit; do
        cases=$((cases + 1))
        [ "$wait" = - ] && wait=
        want=$(printf '%s%s%s%02x%s' "$wait" "$prefix" "$op" $((mod * 64 + digit * 8 + rm)) "$tail")
        printf '\t.text\n\t%s %s\n' "$name" "$addr" > t.s
        rm -f t.o
        if "$AS" "--$mode" -o t.o t.s > out 2>&1; then
            got=$(objcopy -O binary --only-section=.text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
        else
            got=refused
        fi
        [ "$got" = "$want" ] || { echo "FAIL $mode [$name $addr]: $got, and GNU as: $want"; fail=1; }
    done < insns
done < addrs

# An address that is none, and operands that are not the instruction's.
while IFS='	' read -r modes src; do
    for mode in $modes; do
        cases=$((cases + 1))
        printf '\t.text\n\t%s\n' "$src" > t.s
        if "$AS" "--$mode" -o t.o t.s > out 2>&1; then
            echo "FAIL $mode [$src]: assembled, and GNU as refuses it"
            fail=1
        fi
    done
done <<'EOF'
64	fldl (%rax,%ebx)
64	fldl (%rax,%rsp)
64	fldl (%rip,%rax)
64	fldl (%rax,%rbx,3)
64	fldl (%ax,%bx)
64	fldl (%eax,%bx)
32 64	fadd %eax
32 64	fldl $1
32 64	fldl (%eax),%st
EOF

[ "$fail" -eq 0 ] && echo "ok: x87 memory operands ($cases cases)"
exit "$fail"
