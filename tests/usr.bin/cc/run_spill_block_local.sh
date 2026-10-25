#!/bin/sh
# A spilled register read or written more than once before the next
# label has one register for them: loaded at the first read, stored
# after each write, so the slot holds it at every label.  Spilled whole
# under the pressure of the calls at the top of h(), p was loaded from
# its slot before each call in the block at the end, where the pressure
# is gone and a callee-saved register keeps it.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_spill_block_local: skip (no gcc)"
	exit 0
fi
if ! printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	echo "run_spill_block_local: skip (no gcc -m32)"
	exit 0
fi

cat >"$CC_TMP/h.c" <<'EOF2'
long g(long);

long h(long *q, long x)
{
	long *p = q + (x & 15);
	long a = x * 3, b = x ^ 5, c = x + 7, d = x - 9, e = x | 1, f = x & 6;
	long s = g(a) + g(b) + g(c) + g(d) + g(e) + g(f);

	s += a * b + c * d + e * f;
	if (s & 1) {
		s += g(p[0]);
		s += g(p[1]);
		s += g(p[2]);
		s += g(p[3]);
	}
	return s;
}
EOF2
cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>

long h(long *q, long x);
long g(long v) { return v * 5 + 1; }

int main(void)
{
	long q[32];
	int i;

	for (i = 0; i < 32; ++i)
		q[i] = i * 7 - 3;
	printf("%ld %ld %ld\n", h(q, 2), h(q, 3), h(q, 12));
	return 0;
}
EOF2
gcc -m32 -O1 -o "$CC_TMP/g" "$CC_TMP/h.c" "$CC_TMP/m.c"
want=$("$CC_TMP/g")
gcc -m32 -O1 -c -o "$CC_TMP/m.o" "$CC_TMP/m.c"
for o in O1 O2 Os; do
	"$CC_BIN" -m32 -$o -c -o "$CC_TMP/h.o" "$CC_TMP/h.c"
	gcc -m32 -o "$CC_TMP/c" "$CC_TMP/h.o" "$CC_TMP/m.o"
	got=$("$CC_TMP/c") || { echo "run_spill_block_local: -$o: the program fails"; exit 1; }
	if [ "$got" != "$want" ]; then
		echo "run_spill_block_local: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
"$CC_BIN" -m32 -O2 -S -o "$CC_TMP/h.s" "$CC_TMP/h.c"
n=$(awk '/^h:/{x=1} /^\.size h,/{x=0} x' "$CC_TMP/h.s" | awk '/^	test[lb] \$1,/{x=1; next} x && /^\./{x=0} x' | grep -cE '\(%ebp\)' || true)
if [ "$n" -gt 1 ]; then
	echo "run_spill_block_local: the block loads p from its slot $n times"
	exit 1
fi
echo "run_spill_block_local: ok"
