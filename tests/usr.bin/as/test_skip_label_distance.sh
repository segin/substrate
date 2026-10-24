#!/bin/sh
# A .skip whose count is the distance between two labels both after it --
# Linux's clear_bhb_loop pads with `.skip 32 - (.Lret1 - 1f)` -- is that
# many bytes, and the labels after it are where they are: places were
# found as though the .skip were nothing, and every `1f` and `1b` pointed
# short by it, though the code after was laid out right.  The bytes are
# GNU as's, at --32 and --64.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

cat > t.s <<'EOS'
	.text
f:	call	1f
	jmp	5f
	.skip	32 - (.Lret1 - 1f), 0xcc
1:	call	2f
.Lret1:	ret
	.align	16, 0xcc
	.skip	(3f - 2f) * 2, 0xcc
2:	jmp	1b
3:	nop
5:	ret
EOS
want=e81d000000eb30cccccccccccccccccccccccccccccccccccccccccccccccccccccce80d000000c3ccccccccccccccccccccccccebec90c3
fail=0
for m in --32 --64; do
    if ! "$AS" $m -o t.o t.s 2> err; then
        echo "FAIL $m: $(tail -1 err)"
        fail=1
        continue
    fi
    bytes=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    [ "$bytes" = "$want" ] || { echo "FAIL $m: bytes $bytes, not $want"; fail=1; }
done
[ "$fail" = 0 ] && echo "ok (2 cases)"
exit "$fail"
