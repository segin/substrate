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

# A name set to another such name stands for the register that one stands
# for then, and keeps it when that one is set again: Linux's SHA-256
# rotates the registers of its rounds so (`TMP_ = h`, `h = g`, ..., `a =
# TMP_`), which was a "symbol definition loop".  The bytes are GNU as's.
cat > r.s <<'EOF'
	a = %eax
	b = %ecx
	c = %edx
	.text
	.macro ROT
	TMP_ = c
	c = b
	b = a
	a = TMP_
	.endm
	.macro STEP
	addl a, b
	movl (b,c,1), a
	ROT
	.endm
	STEP
	STEP
	STEP
	STEP
EOF
want=01c18b041101d08b140801ca8b0c0201c18b0411
if ! "$AS" --32 -o r.o r.s 2> err; then
    echo "FAIL: rotated names: $(tail -1 err)"; fail=1
else
    bytes=$(objcopy -O binary -j .text r.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    [ "$bytes" = "$want" ] || { echo "FAIL: rotated names: bytes $bytes, not $want"; fail=1; }
    syms=$(nm r.o | awk '{print $NF}' | tr '\n' ' ')
    [ -z "$syms" ] || { echo "FAIL: rotated names: symbols [$syms], not none"; fail=1; }
fi

[ "$fail" = 0 ] && echo "ok (3 cases)"
exit "$fail"
