#!/bin/sh
# A number where an instruction takes a register or memory (AS-T-181,
# AS-T-182).
#
# In 32-bit code an operand written `$n` was read as the address n by
# every instruction the emitter encodes from its own tables.  So
#
#   psllw $1, %mm0      shifted by what was in memory at address 1
#   pxor  $3, %mm1      was pxor 3, %mm1
#   addps $1, %xmm0     was addps 1, %xmm0
#   bsfl  $1, %eax      was bsfl 1, %eax
#
# and none of them was refused or said anything.  The shifts of MMX and
# SSE2 have a form with a count that is a number, 0F 71, 72 and 73, and
# are assembled to it; the rest take no number and are refused.
#
# A line is the modes it is tried in, the bytes, which are GNU as's (or
# `refused`), and the source, `|` between its statements.  GNU as passes
# this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

while IFS='	' read -r modes want src; do
    case $modes in '' | '#'*) continue ;; esac
    for mode in $modes; do
        cases=$((cases + 1))
        { echo '	.text'; printf '%s\n' "$src" | tr '|' '\n'; } > t.s
        rm -f t.o
        if "$AS" "--$mode" -o t.o t.s > out 2>&1; then
            got=$(objcopy -O binary --only-section=.text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
        else
            got=refused
        fi
        [ "$got" = "$want" ] || { echo "FAIL $mode [$src]: $got, and GNU as: $want"; fail=1; }
    done
done <<'EOF'
# The shifts, by a number.
32 64	0f71f001660f72d1020f71e203660f73f3040f71d705660f72e7060f72f107660f73d208	psllw $1,%mm0|psrld $2,%xmm1|psraw $3,%mm2|psllq $4,%xmm3|psrlw $5,%mm7|psrad $6,%xmm7|pslld $7,%mm1|psrlq $8,%xmm2
32 64	660f73f801660f73d902	pslldq $1,%xmm0|psrldq $2,%xmm1
# And by a register or by what is in memory, as they were.
32	0ff1c10ff100660fd2ca0fe15304	psllw %mm1,%mm0|psllw (%eax),%mm0|psrld %xmm2,%xmm1|psraw 4(%ebx),%mm2
# What takes a number takes it still.
32 64	660f70c8010fc6c802660fc5c001660fc4c001	pshufd $1,%xmm0,%xmm1|shufps $2,%xmm0,%xmm1|pextrw $1,%xmm0,%eax|pinsrw $1,%eax,%xmm0
# The VEX shifts by a number, six of which were not assembled.
32	c5f171f001c5ed72d102c5e171e203c5dd73f304c5f173f801c5ed73d902	vpsllw $1,%xmm0,%xmm1|vpsrld $2,%ymm1,%ymm2|vpsraw $3,%xmm2,%xmm3|vpsllq $4,%ymm3,%ymm4|vpslldq $1,%xmm0,%xmm1|vpsrldq $2,%ymm1,%ymm2
32	c5f571d009c5f972e701c5cd72f503	vpsrlw $9,%ymm0,%ymm1|vpsrad $1,%xmm7,%xmm0|vpslld $3,%ymm5,%ymm6
# cmpsd is two instructions: the compare of SSE2, and the string one.
32 64	f20fc2d101f30fc2d1020fc2d103660fc2d104	cmpsd $1,%xmm1,%xmm2|cmpss $2,%xmm1,%xmm2|cmpps $3,%xmm1,%xmm2|cmppd $4,%xmm1,%xmm2
32 64	f20fc2d100f30fc2d101	cmpeqsd %xmm1,%xmm2|cmpltss %xmm1,%xmm2
32 64	a7a766a7a6	cmpsd|cmpsl|cmpsw|cmpsb
# What takes none.
32 64	refused	paddw $1,%mm0
32 64	refused	pand $1,%xmm0
32 64	refused	pxor $3,%mm1
32 64	refused	paddb $1,%xmm1
32 64	refused	addps $1,%xmm0
32 64	refused	movaps $1,%xmm0
32 64	refused	bsfl $1,%eax
32 64	refused	cmovel $1,%eax
32 64	refused	movzbl $1,%eax
32 64	refused	xchgl $1,%eax
32 64	refused	leal $1,%eax
32 64	refused	setne $1
32 64	refused	mull $2
32 64	refused	incl $1
32 64	refused	addl %eax,$1
32 64	refused	fld $1
# A number with no `$` is an address, and is one still.
32	0fef0d03000000	pxor 3,%mm1
32	0f580501000000	addps 1,%xmm0
EOF

[ "$fail" -eq 0 ] && echo "ok: numbers where a register or memory is taken ($cases cases)"
exit "$fail"
