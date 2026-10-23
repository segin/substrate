#!/bin/sh
# Two i386 shapes Linux's 16-bit setup code, which must fit 32 KiB, pays
# for: a compare against 0 is testl, a byte shorter than cmpl $0 and the
# flags the same for every condition; and an 8-byte integer stored to or
# loaded from a stack object is copied word by word off %ebp, with no
# leal of the object's address first.  codegen/ checks the values.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat > "$CC_TMP/fs.c" <<'EOF'
int negative(int x) { return x < 0; }
int above(unsigned x) { return x > 0; }
unsigned long long boxed(unsigned long long v) {
	unsigned long long box = v;

	__asm__("" : "+m"(box));
	return box;
}
EOF
status=0
"$CC_BIN" -m32 -O2 -S -o "$CC_TMP/fs.s" "$CC_TMP/fs.c"
if grep -q 'cmpl \$0, %e' "$CC_TMP/fs.s"; then
	echo "FAIL: a compare against 0 is cmpl \$0, not testl"
	status=1
fi
# (one leal is the asm's: its "+m" operand is given by its address)
n=$(sed -n '/^boxed:/,/\.size boxed/p' "$CC_TMP/fs.s" | grep -c 'leal -[0-9]*(%ebp)' || true)
if [ "$n" -gt 1 ]; then
	echo "FAIL: the 8-byte stack object is stored or loaded through leal of its address ($n leal)"
	status=1
fi
[ "$status" -eq 0 ] && echo "run_i386_frame_shapes: ok"
exit "$status"
