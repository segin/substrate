#!/bin/sh
# The data directives in a flat image (-O binary): the bytes they are in
# an object.
#
# The flat writer had an emitter of the data directives of its own, to
# be kept in step with the object writer's by hand, and it was not:
# `.fill 2,8,0x0102030405060708` was all eight bytes of the value in an
# image and the low four in an object (which is what GNU as writes).
# There is one emitter now.
#
# The image wanted is what `objcopy -O binary -j .text` makes of the
# object GNU as 2.47 assembles from the same source.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

cat > t.s <<'EOF'
	.text
q:	nop
	.p2align 3
r:	ret
	.byte 1, r-q
	.word 0x1234, r-q
	.long 7, r-q
	.quad 9
	.ascii "ab"
	.asciz "cd"
	.float 1.5
	.double 2.25
	.zero 2
	.skip 2, 0x41
	.fill 2,8,0x0102030405060708
	.fill r-q,1,7
	.uleb128 300
	.sleb128 -3
	.dc.w 2
	.octa 0x0102030405060708090a0b0c0d0e0f10
	.balign 4
	.balign 8,0xcc
	.org 120
	.byte 0xff
EOF
want=900f1f8000000000c30108341208000700000008000000090000000000000061626364000000c03f000000000000024000004141080706050000000008070605000000000707070707070707ac027d0200100f0e0d0c0b0a090807060504030201906690cccccccc00000000000000000000000000000000ff

fail=0
if ! "$AS" --64 -O binary -o t.bin t.s 2> err; then
    echo "FAIL: not assembled: $(head -1 err)"
    exit 1
fi
got=$(od -An -v -tx1 t.bin | tr -d ' \n')
if [ "$got" != "$want" ]; then
    echo "FAIL: the image is $got"
    fail=1
fi

# What is no expression is said, and a place behind this one is.
refused() {
    printf '\t.text\nq:\tnop\n\tnop\n\t%s\n' "$1" > e.s
    if "$AS" --64 -O binary -o e.bin e.s 2> err; then
        echo "FAIL: '$1' was assembled to a flat image"
        fail=1
    fi
}
refused '.long 1+'
refused '.org 1'

[ "$fail" = 0 ] && echo "ok: data directives in a flat image"
exit "$fail"
