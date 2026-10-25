#!/bin/sh
# On x86-64 an int is computed in a 64-bit register and extended where a
# reader needs all of it; a compare of ints is made of their 32-bit
# registers, which reads only their low halves, and an extension whose
# readers read no more than its source's bits -- the compare, a narrow
# store, the low bits of a sum -- is a copy.  `i + 1 < n` sign-extended
# i + 1 to compare it as a long.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_int_cmp32: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/i.c" <<'EOF2'
#include <stdio.h>

int __attribute__((noinline)) count(const int *a, int n, int lim)
{
	int c = 0;
	int i;

	for (i = 0; i + 1 < n; i += 2)
		if (a[i] + a[i + 1] < lim && (unsigned)a[i] > 3u)
			c++;
	return c;
}

int __attribute__((noinline)) wrap(int x, int y, short *out)
{
	int s = x + y;	/* may wrap at 32 bits: compared and stored as an int */

	*out = (short)s;
	return s < 0 ? -1 : s > 1000 ? 1 : 0;
}

int main(void)
{
	int a[12] = {5, -7, 3, 9, 100, -200, 7, 7, 0x7fffffff, 1, -1, 4};
	short o, o2;
	int r = wrap(0x7fffffff, 2, &o);
	int r2 = wrap(600, 401, &o2);

	printf("%d %d %d %d %d %d\n", count(a, 12, 10), count(a, 11, -5), r, o, r2, o2);
	return 0;
}
EOF2
gcc -O1 -fwrapv -o "$CC_TMP/g" "$CC_TMP/i.c"
want=$("$CC_TMP/g")
for o in O1 O2; do
	"$CC_BIN" -m64 -$o -o "$CC_TMP/c" "$CC_TMP/i.c"
	got=$("$CC_TMP/c")
	if [ "$got" != "$want" ]; then
		echo "run_int_cmp32: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
"$CC_BIN" -m64 -O2 -S -o "$CC_TMP/i.s" "$CC_TMP/i.c"
awk '/^count:/{x=1} /^\.size count,/{x=0} x' "$CC_TMP/i.s" >"$CC_TMP/count.s"
if ! grep -q 'cmpl' "$CC_TMP/count.s"; then
	echo "run_int_cmp32: count() compares its ints as longs"
	exit 1
fi
if grep -B1 -E '^	cmp[lq] ' "$CC_TMP/count.s" | grep -qE 'movslq %'; then
	echo "run_int_cmp32: count() sign-extends an int only to compare it"
	grep -B1 -E '^	cmp[lq] ' "$CC_TMP/count.s"
	exit 1
fi
echo "run_int_cmp32: ok"
