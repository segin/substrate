#!/bin/sh
# In AT&T syntax a bare name is a symbol, whatever it spells.
#
# The operand parser took `p0`..`p15` and `c0`..`c15`, ARM's coprocessor
# names, and `st` for x87 registers on x86 too, so `movl p1, %ecx` -- what
# a C compiler writes for `static int p1` on i386 -- was refused ("unknown
# x87 register: p1"), and `movl st, %ecx` ("does not take these
# operands").  Registers are written with `%` there; Intel syntax, which
# writes them bare, keeps `st`.
#
# GNU as is the judge: each operand, as a load, a store, an address with
# an addend and a call, assembles to GNU as's bytes and relocations, --32
# and --64; and %st, st in Intel syntax, are still the x87 registers.
# Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
GAS=${AS_GNU:-as}
if ! "$GAS" --version 2>/dev/null | grep -q 'GNU assembler'; then
    echo "skip: GNU as is the judge"
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

text_of() { objcopy -O binary -j .text "$1" /dev/stdout | od -An -v -tx1 | tr -d ' \n'; }
relocs_of() { readelf -rW "$1" | awk '/R_/ { print $1, $3, $5 }'; }

# like MODE SOURCE: ours assembles SOURCE as GNU as does
like() {
    printf '%s\n' "$2" > t.s
    if ! "$GAS" "--$1" -o g.o t.s 2> err; then
        echo "FAIL --$1: GNU as refused it: $(head -1 err)"; fail=1; return
    fi
    if ! "$AS" "--$1" -o o.o t.s 2> err; then
        echo "FAIL --$1 '$(echo "$2" | tr '\n' ';')': $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    if [ "$(text_of o.o)" != "$(text_of g.o)" ] || [ "$(relocs_of o.o)" != "$(relocs_of g.o)" ]; then
        echo "FAIL --$1 '$(echo "$2" | tr '\n' ';')': $(text_of o.o), GNU as $(text_of g.o)"; fail=1
    fi
}

for m in 32 64; do
    if [ $m = 32 ]; then w=l; r=%edx; else w=q; r=%rdx; fi
    for name in p1 p0 p15 c3 C7 P2 st ST; do
        like $m "	.data
	.globl $name
$name:	.long 0
	.text
	movl $name, %ecx
	movl %ecx, $name
	movl $name+4, %eax
	lea$w $name, $r
	push$w \$$name"
    done
    like $m "	.text
	call p1
	jmp c2
p1:	ret
c2:	ret"
    like $m "	.text
	fld %st(1)
	fadd %st, %st(2)
	fstp %st"
    like $m "	.intel_syntax noprefix
	.text
	fld st(1)
	fadd st, st(2)
	fstp st"
done

if [ $fail = 0 ]; then
    echo "test_bare_symbol_names: ok"
fi
exit $fail
