#!/bin/sh
# The x87 instructions that take no operand (part of AS-T-179).
#
# A compiler loads 0.0 with fldz and 1.0 with fld1, and takes a square
# root with fsqrt; none of them assembled, nor did the other constants,
# the functions of the top of the stack, or the control instructions.
# 32-bit code that touches a double -- which is all of it that is not
# built with SSE arithmetic -- could not be assembled.
#
# The bytes are GNU as's, the same in both modes.  Run by run-suite.sh,
# which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

for pair in fld1:d9e8 fldl2t:d9e9 fldl2e:d9ea fldpi:d9eb fldlg2:d9ec fldln2:d9ed fldz:d9ee \
            fchs:d9e0 fabs:d9e1 ftst:d9e4 fxam:d9e5 \
            f2xm1:d9f0 fyl2x:d9f1 fptan:d9f2 fpatan:d9f3 fxtract:d9f4 fprem1:d9f5 \
            fdecstp:d9f6 fincstp:d9f7 fprem:d9f8 fyl2xp1:d9f9 fsqrt:d9fa fsincos:d9fb \
            frndint:d9fc fscale:d9fd fsin:d9fe fcos:d9ff fnop:d9d0 \
            fcompp:ded9 fucompp:dae9 fnclex:dbe2 fninit:dbe3 fclex:9bdbe2 finit:9bdbe3 \
            fwait:9b wait:9b; do
    insn=${pair%%:*}; want=${pair##*:}
    for m in 32 64; do
        printf '\t.text\n\t%s\n' "$insn" > t.s
        if ! "$AS" --$m -o t.o t.s 2> err; then
            echo "FAIL --$m $insn: $(head -1 err | sed 's/.*: //')"; fail=1; continue
        fi
        objcopy -O binary -j .text t.o t.bin
        got=$(od -An -v -tx1 t.bin | tr -d ' \n')
        [ "$got" = "$want" ] || { echo "FAIL --$m $insn: $got, not $want"; fail=1; }
    done
done

# With an operand they are not these instructions.
printf '\tfldz %%eax\n' > t.s
if "$AS" --32 -o t.o t.s 2> /dev/null; then echo "FAIL: fldz %eax assembled"; fail=1; fi

[ "$fail" -eq 0 ] && echo "ok: x87 without operands"
exit "$fail"
