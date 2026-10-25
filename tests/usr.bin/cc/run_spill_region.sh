#!/bin/sh
# A spilled register's references in a region -- blocks entered only at
# its top, the labels within reached by forward jumps from inside it --
# share one register, not only those in one block: p, spilled across
# the loop's calls, was reloaded in each block after it that reads it,
# where the pressure is gone; and that register spilled in turn, its
# references in each block share one, as before.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	echo "run_spill_region: skip (no gcc -m32)"
	exit 0
fi

cat >"$CC_TMP/h.c" <<'EOF2'
long g(long);

long h(long *q, long x, int n)
{
	long *p = q + (x & 15);
	long s = g(p[5]);
	long a = x * 3, b = x * 5, c = x * 7, d = x * 9;
	int i;

	for (i = 0; i < n; ++i)
		s += g(a) + g(b) + g(c) + g(d) + a * b * c * d;
	s += g(p[1]);
	if (s & 2)
		s += g(p[2]);
	if (s & 4)
		s += g(p[3]);
	s += g(p[4]);
	return s;
}
EOF2
cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>

long h(long *q, long x, int n);
long g(long v) { return v * 5 + 1; }

int main(void)
{
	long q[32];
	long a, b, c;
	int i;

	for (i = 0; i < 32; ++i)
		q[i] = i * 7 - 3;
	a = h(q, 2, 1);
	b = h(q, 3, 2);
	c = h(q, 12, 0);
	printf("%ld %ld %ld\n", a, b, c);
	return 0;
}
EOF2
gcc -m32 -O1 -o "$CC_TMP/g" "$CC_TMP/h.c" "$CC_TMP/m.c"
want=$("$CC_TMP/g")
gcc -m32 -O1 -c -o "$CC_TMP/m.o" "$CC_TMP/m.c"
for o in O1 O2 Os; do
	"$CC_BIN" -m32 -$o -c -o "$CC_TMP/h.o" "$CC_TMP/h.c"
	gcc -m32 -o "$CC_TMP/c" "$CC_TMP/h.o" "$CC_TMP/m.o"
	got=$("$CC_TMP/c") || { echo "run_spill_region: -$o: the program fails"; exit 1; }
	if [ "$got" != "$want" ]; then
		echo "run_spill_region: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
"$CC_BIN" -m32 -O2 -S -o "$CC_TMP/h.s" "$CC_TMP/h.c"
n=$(awk '/^h:/{x=1} /^\.size h,/{x=0} x' "$CC_TMP/h.s" | awk '/^	testl \$2,/{x=1} x' | grep -cE '\(%ebp\)' || true)
if [ "$n" -gt 1 ]; then
	echo "run_spill_region: h() reloads after its first branch $n times"
	exit 1
fi
echo "run_spill_region: ok"
