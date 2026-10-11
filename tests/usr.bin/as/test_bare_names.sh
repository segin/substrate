#!/bin/sh
# A name written without `%` is a symbol.
#
# In AT&T syntax a register has a `%`.  `call sp` calls the function sp
# and `movl r12, %eax` reads the variable r12; they were assembled as
# the registers of those names -- `call sp` as `call *%sp` -- and x1, s1,
# lr, pc, cpsr and their like were taken for ARM's registers, which this
# assembler does not assemble for, and refused as "unknown x86
# register".  A C program with a global called s1 did not assemble.
#
# In Intel syntax the x86 registers are written bare, and the rest are
# names still.
#
# The bytes are GNU as 2.46's.  `|` is a new line.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# n MODES BYTES SYMBOL SOURCE: .text holds BYTES and has a relocation
# against SYMBOL (`-` for none).
n() {
    for mode in $1; do
        cases=$((cases + 1))
        { echo .text; printf '%s\n' "$4" | tr '|' '\n'; } > t.s
        rm -f t.o
        if ! "$AS" "-$mode" -o t.o t.s > out 2> err; then
            echo "FAIL $mode [$4]: $(head -1 err)"; fail=1; continue
        fi
        got=$(objcopy -O binary --only-section=.text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
        [ "$got" = "$2" ] || { echo "FAIL $mode [$4]: $got, and GNU as: $2"; fail=1; }
        if [ "$3" = - ]; then
            [ -z "$(readelf -rW t.o | grep ' R_')" ] || { echo "FAIL $mode [$4]: a relocation"; fail=1; }
        else
            readelf -rW t.o | grep -q " R_.* $3\( \|\$\)" || { echo "FAIL $mode [$4]: no relocation against $3"; fail=1; }
        fi
    done
}

# x86's own register names, without the `%`.
n "32"    e8fcffffff       sp   'call sp'
n "64"    e800000000       sp   'call sp'
n "32"    8b1d00000000     sp   'movl sp,%ebx'
n "64"    8b0500000000     sp   'movl sp(%rip),%eax'
n "32"    892500000000     sp   'movl %esp, sp'
n "32"    010500000000     eax  'addl %eax, eax'
n "32"    b800000000       ebx  'movl $ebx, %eax'
n "64"    e800000000       r12  'call r12'
n "64"    488b0500000000   rax  'movq rax(%rip), %rax'
n "64"    4c8d0d00000000   r8   'leaq r8(%rip), %r9'
n "32"    ff0500000000     r15  'incl r15'

# ARM's.
n "32"    e9fcffffff       x1   'jmp x1'
n "64"    e900000000       x1   'jmp x1'
n "32"    e8fcffffff       lr   'call lr'
n "64"    e800000000       lr   'call lr'
n "32"    e9fcffffff       pc   'jmp pc'
n "32"    8b1d00000000     s1   'movl s1,%ebx'
n "32"    8b1d00000000     cpsr 'movl cpsr,%ebx'
n "32"    8b1d00000000     wzr  'movl wzr,%ebx'
n "32"    b800000000       xzr  'movl $xzr,%eax'
n "32"    6800000000       q0   'pushl $q0'
n "32"    8b0d00000000     d3   'movl d3,%ecx'
n "32"    8b1500000000     v7   'movl v7,%edx'
n "32"    8b3500000000     w0   'movl w0,%esi'
n "32"    8b3d00000000     x30  'movl x30,%edi'
n "32"    8b1d00000000     r0   'movl r0,%ebx'
n "32"    8d9800000000     r1   'leal r1(%eax),%ebx'
n "32"    891d00000000     r3   'movl %ebx, r3'

# With the `%` they are the registers.
n "32"    89e3             -    'movl %esp,%ebx'
n "64"    4489e0           -    'movl %r12d,%eax'

# Intel syntax: x86's registers bare, and the others names.
n "32"    6689e3           -    '.intel_syntax noprefix|mov bx, sp'
n "32"    6689f3           -    '.intel_syntax noprefix|mov bx, si'
n "32"    6689fb           -    '.intel_syntax noprefix|mov bx, di'
n "32"    6689eb           -    '.intel_syntax noprefix|mov bx, bp'
n "32"    89e3             -    '.intel_syntax noprefix|mov ebx, esp'
n "32"    8b1d00000000     lr   '.intel_syntax noprefix|mov ebx, lr'
n "32"    8b1d00000000     x1   '.intel_syntax noprefix|mov ebx, x1'
n "32"    e8fcffffff       s1   '.intel_syntax noprefix|call s1'

# What a compiler writes for
#     int s1; int sp; int x1(void) { return s1 + sp; }
#     int r12(void) { return x1(); }
cases=$((cases + 1))
cat > c.s <<'EOF'
	.text
	.globl	x1
	.type	x1, @function
x1:
	movl	sp, %edx
	addl	s1, %edx
	movl	%edx, %eax
	ret
	.size	x1, .-x1
	.globl	r12
	.type	r12, @function
r12:
	call	x1
	ret
	.size	r12, .-r12
	.globl	sp
	.bss
	.align 4
	.type	sp, @object
	.size	sp, 4
sp:
	.zero	4
	.globl	s1
	.align 4
	.type	s1, @object
	.size	s1, 4
s1:
	.zero	4
EOF
if "$AS" -32 -o c.o c.s 2> err; then
    got=$(nm c.o | awk '{print $2 $3}' | sort | tr '\n' ' ')
    [ "$got" = "Bs1 Bsp Tr12 Tx1 " ] || { echo "FAIL a compiler's output: symbols $got"; fail=1; }
    got=$(objcopy -O binary --only-section=.text c.o /dev/stdout | od -An -v -tx1 | tr -d ' \n' | cut -c1-28)
    [ "$got" = 8b1500000000031500000000 ] || [ "$got" = 8b150000000003150000000089d0 ] ||
        { echo "FAIL a compiler's output: .text begins $got"; fail=1; }
else
    echo "FAIL a compiler's output: $(head -1 err)"; fail=1
fi

[ "$fail" -eq 0 ] && echo "ok: names without a % are symbols ($cases cases)"
exit "$fail"
