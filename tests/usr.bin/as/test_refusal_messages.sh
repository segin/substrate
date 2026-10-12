#!/bin/sh
# What is said of a statement that is refused is what is wrong with it
# (AS-T-166).
#
# An instruction went through several encoders in turn, and what the
# user was told was what the last of them said, or nothing: `rorx` with
# operands it does not take had no message, `fisttpl %eax` was
# "unsupported mnemonic", `movq %cr0, (%rax)` "unknown x86 register:
# cr0", `int %eax` "cannot be assembled".  An instruction with a VEX or
# an EVEX prefix has one encoder now, and its reason is the one given;
# of the rest, a name this assembler has is no unknown mnemonic, and a
# register it has no unknown register.
#
# Each line below: the mode, the statement, and the message it has.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
n=0
while IFS='|' read -r mode stmt want; do
    [ -n "$mode" ] || continue
    n=$((n + 1))
    printf '\t%s\n' "$stmt" > t.s
    if "$AS" "--$mode" -o t.o t.s 2> t.err; then
        echo "FAIL --$mode [$stmt]: assembled"
        fail=1
        continue
    fi
    got=$(sed -n '1s/^[^:]*:[0-9]*: Error: //p' t.err)
    if [ "$got" != "$want" ]; then
        echo "FAIL --$mode [$stmt]: \"$got\", not \"$want\""
        fail=1
    fi
done <<'EOF'
64|rorx $1,%ax,%bx|'rorx' does not take these operands
64|vaddps %eax,%xmm0,%xmm1|'vaddps' does not take these operands
64|vaddps (%rax){1to8},%xmm0,%xmm1|'vaddps' has a broadcast of another count than its vector's
64|vaddps %xmm0,%xmm1,%xmm2{%k0}|'vaddps' has %k0 for a write mask, which is none
64|vaddps %xmm0,%xmm1,%xmm2{z}|'vaddps' has {z} and no write mask
64|fisttpl %eax|'fisttpl' does not take these operands
64|int %eax|'int' does not take these operands
64|movq %cr0,(%rax)|'movq' does not take these operands
64|push %xmm1|'push' does not take these operands
64|verr %xmm1|'verr' does not take these operands
64|lldt %cl|'lldt' does not take these operands
64|addl %xmm0,%eax|'addl' does not take these operands
64|bswap %ax|'bswap' does not take these operands
64|lahf %eax|'lahf' does not take these operands
64|pushl %eax,%ebx|'pushl' takes 1 operand at most, and has 2
64|nosuch %eax|unsupported x86_64 mnemonic: nosuch
64|vnosuch %xmm0|unsupported x86_64 mnemonic: vnosuch
32|rorx $1,%ax,%bx|'rorx' does not take these operands
32|vaddps %eax,%xmm0,%xmm1|'vaddps' does not take these operands
32|fisttpl %eax|'fisttpl' does not take these operands
32|fld %eax|'fld' does not take these operands
32|movl %cr0,(%eax)|'movl' does not take these operands
32|push %xmm1|'push' does not take these operands
32|lldt %cl|'lldt' does not take these operands
32|addl %xmm0,%eax|'addl' does not take these operands
32|nosuch %eax|unsupported mnemonic: nosuch
32|vnosuch %xmm0|unsupported mnemonic: vnosuch
EOF

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "ok: what a refusal says ($n statements)"
