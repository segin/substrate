#!/bin/sh
# Operands as they are written: what a memory operand may hold, and a
# local label used as an address.
#
# - What stood where a base or an index is to be was taken for a
#   register whatever it was -- and a name in particular, `bad`, for
#   %eax.  A fourth part in the parentheses was dropped, what followed
#   the parenthesis was not read, an empty operand was passed over, and
#   a scale of 0 or 3 was assembled as 1 by some encoders.
# - The address of a label of the section the instruction is in --
#   `pushl $.L5`, `movl $1f, %eax`, `leal 1f, %eax`, `movl 1b(%ebx),
#   %eax` -- was a zero and no relocation: taken for a distance, as the
#   target of a branch is.  And a numeric label in a data directive,
#   `.long 1b`, was a relocation against a symbol called "1b" that
#   nothing defines.
#
# The bytes and relocations are GNU as 2.46's: the relocation is
# against the section, with the label's offset in the instruction (or,
# in 64-bit code, in the relocation).  In a case, `|` is a new line.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# The mode; the bytes of .text and then each relocation as
# TYPE@OFFSET:SYMBOL+ADDEND, or REFUSED; the source.
cat > cases <<'EOF'
32	REFUSED	movl (%eax,bad),%ecx
32	REFUSED	movl 4(bad,%ebx),%ecx
32	REFUSED	movl (%eax)junk,%ebx
32	REFUSED	movl (%eax,%ebx,2,junk),%ecx
32	REFUSED	movl (%eax,%ebx,2,4),%ecx
32	REFUSED	movl $1,,%eax
32	REFUSED	movl $1,%eax,
32	REFUSED	movl ,%eax
32	REFUSED	movl (%eax,%ebx,3),%ecx
32	REFUSED	movl (%eax,%ebx,0),%ecx
32	REFUSED	movl (%eax,%ebx,16),%ecx
32	REFUSED	movl (,%ebx,5),%ecx
32	REFUSED	leal (%eax,%ebx,3),%ecx
32	REFUSED	addps (%eax,%ebx,3),%xmm0
32	REFUSED	fldl (%eax,%ebx,3)
32	REFUSED	vaddps (%eax,%ebx,3),%xmm1,%xmm2
32	8b0cd8	movl (%eax,%ebx,8),%ecx
32	8b0c9d04000000	movl 4(,%ebx,4),%ecx
32	8b1d00000000 R_386_32@00000002:sym	movl (sym),%ebx
32	680500000090 R_386_32@00000001:.text	pushl $1f|1: nop
32	b80500000090 R_386_32@00000001:.text	movl $1f,%eax|1: nop
32	8d050600000090 R_386_32@00000002:.text	leal 1f,%eax|1: nop
32	8b1d0600000090 R_386_32@00000002:.text	movl 1f,%ebx|1: nop
32	8b830600000090 R_386_32@00000002:.text	movl 1f(%ebx),%eax|1: nop
32	906800000000 R_386_32@00000002:.text	1: nop|pushl $1b
32	908b8100000000 R_386_32@00000003:.text	1: nop|movl 1b(%ecx),%eax
32	906800000000 R_386_32@00000002:.text	.Lx: nop|pushl $.Lx
32	680600000090c3 R_386_32@00000001:.text	pushl $.Ly|nop|.Ly: ret
32	8d1d06000000c3 R_386_32@00000002:.text	leal .Ly,%ebx|.Ly: ret
32	b800000000 R_386_32@00000001:.data	.data|1: .long 0|.text|movl $1b,%eax
32	90000000000900000090 R_386_32@00000001:.text R_386_32@00000005:.text	1: nop|.long 1b, 2f|2: nop
32	REFUSED	.long 1b|1: nop
32	9090ff	nop|1: nop|.byte 1b - .
32	90ebfd6800000000ebf6 R_386_32@00000004:.text	1: nop|jmp 1b|pushl $1b|jmp 1b
64	48c7c00000000090 R_X86_64_32S@0000000000000003:.text+7	movq $1f,%rax|1: nop
64	90b800000000 R_X86_64_32@0000000000000002:.text+0	1: nop|movl $1b, %eax
64	90c3488d05f8ffffff	nop|1: ret|leaq 1b(%rip), %rax
64	488b050000000090	movq 1f(%rip),%rax|1: nop
EOF

show() {
    objcopy -O binary --only-section=.text "$1" /dev/stdout 2>/dev/null | od -An -v -tx1 | tr -d ' \n'
    readelf -rW "$1" 2>/dev/null | awk '/ R_/ { printf " %s@%s:%s%s", $3, $1, $5, $6 $7 }'
}

while IFS='	' read -r mode want src; do
    cases=$((cases + 1))
    { echo .text; printf '%s\n' "$src" | tr '|' '\n'; } > t.s
    rm -f t.o
    if "$AS" "-$mode" -o t.o t.s > out 2> err; then
        got=$(show t.o)
    else
        got=REFUSED
    fi
    [ "$got" = "$want" ] || { echo "FAIL $mode [$src]: $got, and GNU as: $want $(head -1 err)"; fail=1; }
done < cases

# The names a numeric label is given are not symbols of the object.
cases=$((cases + 1))
printf '\t.text\n1:\tnop\n\tpushl $1b\n\t.long 1b\n' > n.s
if "$AS" -32 -o n.o n.s 2> err; then
    [ -z "$(nm n.o)" ] || { echo "FAIL a numeric label left a symbol: $(nm n.o | tr '\n' ' ')"; fail=1; }
else
    echo "FAIL numeric labels: $(head -1 err)"; fail=1
fi

[ "$fail" -eq 0 ] && echo "ok: operand forms ($cases cases)"
exit "$fail"
