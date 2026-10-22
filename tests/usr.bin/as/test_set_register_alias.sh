#!/bin/sh
# A name set to a register stands for that register in operands, as in
# GNU as: Linux's memmove_32.S has `.set src, %esi` and `.set dest, %edi`
# and then `movl (src), %eax` and `cmpl dest, src`, which this assembler
# refused ("the base of a memory operand, 'src', is not a register") --
# and `.set` of a register itself was a "malformed directive".  The name
# may be set again, to another register or to a number, and is no symbol
# of the object.  GNU as passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0

cat > t.s <<'EOF'
	.set dest_in, %eax
	.set src, %esi
	dest = %edi
	.text
f:	movl dest_in, dest
	movl (src), %eax
	cmpl dest, src
	leal 4(src,dest,2), %ecx
	movl $src_off, %eax
	.set src, %edx
	movl src, %ebx
	.set src_off, 8
EOF
# mov %eax,%edi; mov (%esi),%eax; cmp %edi,%esi; lea 4(%esi,%edi,2),%ecx; mov $8,%eax; mov %edx,%ebx
want=89c78b0639fe8d4c7e04b80800000089d3
if ! "$AS" --32 -o t.o t.s 2> err; then
    echo "FAIL: $(tail -1 err)"; exit 1
fi
bytes=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
[ "$bytes" = "$want" ] || { echo "FAIL: bytes $bytes, not $want"; fail=1; }
syms=$(nm t.o | awk '{print $NF}' | sort | tr '\n' ' ')
[ "$syms" = "f src_off " ] || { echo "FAIL: symbols [$syms], not [f src_off ]"; fail=1; }

[ "$fail" = 0 ] && echo "ok (2 cases)"
exit "$fail"
