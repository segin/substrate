#!/bin/sh
# The opmask instructions (AS-T-185).
#
# Two emitters found an opmask instruction by how its name began.  So
#
#   kmovw %eax, %k1     was kmovw %k0, %k1: the general register's
#                       number was taken for a mask register's
#   kunpckbw, kunpckwd  each had the next one's prefix
#   kand, kandfoo, kaddz, which are not instructions, were assembled as
#                       kandw or kaddw
#
# and every other opmask instruction -- a move to memory or to a general
# register, knot, kortest, ktest, the shifts, and kmov of any size but
# sixteen bits -- was "unknown x86 register: k1".
#
# They are one table now: the moves of four sizes in each of their four
# directions, the six binary operations and the three unpacks, knot,
# kortest and ktest, and the shifts.
#
# A line is the modes it is tried in, the bytes, which are GNU as's (or
# `refused`), and the source, `|` between its statements.  GNU as passes
# this test.  In 64-bit mode this assembler is asked for
# -march=x86-64-v4, without which it refuses AVX-512.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0
gnu=0
"$AS" --version 2> /dev/null | grep '^GNU assembler' | grep -qv '(Substrate)' && gnu=1

while IFS='	' read -r modes want src; do
    case $modes in '' | '#'*) continue ;; esac
    for mode in $modes; do
        cases=$((cases + 1))
        { echo '	.text'; printf '%s\n' "$src" | tr '|' '\n'; } > t.s
        rm -f t.o
        march=
        [ "$mode" = 64 ] && [ "$gnu" = 0 ] && march=-march=x86-64-v4
        # shellcheck disable=SC2086
        if "$AS" "--$mode" $march -o t.o t.s > out 2>&1; then
            got=$(objcopy -O binary --only-section=.text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
        else
            got=refused
        fi
        [ "$got" = "$want" ] || { echo "FAIL $mode [$src]: $got, and GNU as: $want"; fail=1; }
    done
done <<'EOF'
# Moves: between masks, from and to memory, from and to a general
# register; of 16 bits, 8, 32 and 64.
32 64	c5f890d1	kmovw %k1,%k2
32 64	c5f892c8	kmovw %eax,%k1
32 64	c5f893c1	kmovw %k1,%eax
32	c5f89008	kmovw (%eax),%k1
64	67c5f89008	kmovw (%eax),%k1
32	c5f89108	kmovw %k1,(%eax)
64	67c5f89108	kmovw %k1,(%eax)
32	c5f990d1c5f992c8c5f993c1c5f99008c5f99108	kmovb %k1,%k2|kmovb %eax,%k1|kmovb %k1,%eax|kmovb (%eax),%k1|kmovb %k1,(%eax)
64	c5f990d1c5f992c8c5f993c167c5f9900867c5f99108	kmovb %k1,%k2|kmovb %eax,%k1|kmovb %k1,%eax|kmovb (%eax),%k1|kmovb %k1,(%eax)
32	c4e1f990d1c5fb92c8c5fb93c1c4e1f99008c4e1f99108	kmovd %k1,%k2|kmovd %eax,%k1|kmovd %k1,%eax|kmovd (%eax),%k1|kmovd %k1,(%eax)
64	c4e1f990d1c5fb92c8c5fb93c167c4e1f9900867c4e1f99108	kmovd %k1,%k2|kmovd %eax,%k1|kmovd %k1,%eax|kmovd (%eax),%k1|kmovd %k1,(%eax)
32	c4e1f890d1c4e1f89008c4e1f89108	kmovq %k1,%k2|kmovq (%eax),%k1|kmovq %k1,(%eax)
64	c4e1f890d167c4e1f8900867c4e1f89108	kmovq %k1,%k2|kmovq (%eax),%k1|kmovq %k1,(%eax)
64	c4e1fb92c8c4e1fb93c1	kmovq %rax,%k1|kmovq %k1,%rax
64	c5f89008c4c1789108c4a17890544808c4c17892c8c57893c9c4c1fb92fa	kmovw (%rax),%k1|kmovw %k1,(%r8)|kmovw 8(%rax,%r9,2),%k2|kmovw %r8d,%k1|kmovw %k1,%r9d|kmovq %r10,%k7
32	c5f8900d00000000c4e1f9919300000000c5f9910d00000000	kmovw sym,%k1|kmovd %k2,sym(%ebx)|kmovb %k1,sym
64	c5f9911d00000000c5f8900d00000000c4e1f8909000000000	kmovb %k3,sym(%rip)|kmovw sym(%rip),%k1|kmovq sym+8(%rax),%k2
# The binary operations and the unpacks.
32 64	c5ec41d9c5ec42d9c5ec45d9c5ec47d9c5ec46d9c5ec4ad9	kandw %k1,%k2,%k3|kandnw %k1,%k2,%k3|korw %k1,%k2,%k3|kxorw %k1,%k2,%k3|kxnorw %k1,%k2,%k3|kaddw %k1,%k2,%k3
32 64	c5ed41d9c4e1ed41d9c4e1ec41d9c4e1ec45d9c5ed47d9	kandb %k1,%k2,%k3|kandd %k1,%k2,%k3|kandq %k1,%k2,%k3|korq %k1,%k2,%k3|kxorb %k1,%k2,%k3
32 64	c5ed4bd9c5ec4bd9c4e1ec4bd9	kunpckbw %k1,%k2,%k3|kunpckwd %k1,%k2,%k3|kunpckdq %k1,%k2,%k3
# Of one operand, and the shifts.
32 64	c5f844d1c5f944d1c4e1f944d1c4e1f844d1	knotw %k1,%k2|knotb %k1,%k2|knotd %k1,%k2|knotq %k1,%k2
32 64	c5f898d1c5f998d1c4e1f998d1c4e1f898d1	kortestw %k1,%k2|kortestb %k1,%k2|kortestd %k1,%k2|kortestq %k1,%k2
32 64	c5f899d1c5f999d1c4e1f999d1c4e1f899d1	ktestw %k1,%k2|ktestb %k1,%k2|ktestd %k1,%k2|ktestq %k1,%k2
32 64	c4e3f932d101c4e37932d101c4e37933d101c4e3f933d101	kshiftlw $1,%k1,%k2|kshiftlb $1,%k1,%k2|kshiftld $1,%k1,%k2|kshiftlq $1,%k1,%k2
32 64	c4e3f930d101c4e37930d101c4e37931d101c4e3f931d101	kshiftrw $1,%k1,%k2|kshiftrb $1,%k1,%k2|kshiftrd $1,%k1,%k2|kshiftrq $1,%k1,%k2
# Names that are not instructions.
32 64	refused	kand %k1,%k2,%k3
32 64	refused	kandfoo %k1,%k2,%k3
32 64	refused	kaddz %k1,%k2,%k3
# Operands that are not the instruction's.
32 64	refused	kmovw %k1,%k2,%k3
32 64	refused	kmovw %eax,%ebx
32 64	refused	kmovw %ax,%k1
32 64	refused	kmovw $1,%k1
64	refused	kmovw %rax,%k1
64	refused	kmovq %eax,%k1
32 64	refused	kandw %k1,%k2
32 64	refused	knotw %k1,%eax
32 64	refused	kshiftlw %k1,%k2,%k3
EOF

[ "$fail" -eq 0 ] && echo "ok: opmask instructions ($cases cases)"
exit "$fail"
