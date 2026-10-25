#!/bin/sh
# A test of bits is one test: `if (x & k)`, `(x & k) == 0` with the mask
# read only there is a test of k against x, in a testb on x86-64 or a
# testl -- the and and a test against 0 were two flag settings.  And a
# test of what an and, or or xor (or, read only for its zero and sign
# flags, an add or subtract) has just set the flags from goes.  Results
# are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_mask_test: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/t.c" <<'EOF2'
#include <stdio.h>

int __attribute__((noinline)) f(unsigned long x, unsigned y, long z)
{
	int r = 0;

	if (x & 1)
		r += 1;
	if (!(x & 0x80))
		r += 2;
	if (y & 0x10000)
		r += 4;
	if (x & (1UL << (sizeof(long) * 8 - 1)))
		r += 8;
	if (z & -16L)
		r += 16;
	if ((y & 0xff00) == 0)
		r += 32;
	return r;
}

long __attribute__((noinline)) g(long a, long b, long *out)
{
	long s = a + b;	/* its flags read for zero and sign alone, then for carry */

	*out = s;
	if (s == 0)
		return 1;
	if (s < 0)
		return 2;
	return (unsigned long)a + (unsigned long)b < (unsigned long)a ? 3 : 4;
}

int main(void)
{
	long o, o2, o3;
	long r1 = g(5, -5, &o), r2 = g(-9, 2, &o2), r3 = g(-1, 3, &o3);

	printf("%d %d %d\n", f(0x81, 0x10000, -1), f(2, 0x100, 15), f(1, 0x30000, 16));
	printf("%ld %ld %ld %ld %ld %ld\n", r1, o, r2, o2, r3, o3);
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/t.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/t.c"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_mask_test: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/t.s" "$CC_TMP/t.c"
	awk '/^f:/{x=1} /^\.size f,/{x=0} x' "$CC_TMP/t.s" >"$CC_TMP/f.s"
	if grep -qE '^	and[lq] \$' "$CC_TMP/f.s"; then
		echo "run_mask_test: -m$m: f() masks before a test"
		grep -E '^	(and|test)' "$CC_TMP/f.s"
		exit 1
	fi
done
echo "run_mask_test: ok"
