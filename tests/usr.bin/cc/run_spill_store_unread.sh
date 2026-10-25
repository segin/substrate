#!/bin/sh
# A spill slot nothing reads after allocation is not stored to: a
# spilled register shared through a block is stored after each write so
# that a later reference may reload it, and where every reference was
# given the register -- b and c below, in %ebx and %esi across the
# calls -- the stores were for nothing.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	echo "run_spill_store_unread: skip (no gcc -m32)"
	exit 0
fi

cat >"$CC_TMP/h.c" <<'EOF2'
long g(long);

long h(long *q, long x)
{
	long *p = q + (x & 15);
	long a = x * 3, b = x * 4, c = x * 5;
	long s = g(a) + g(b) + g(c);

	s += a * b * c;
	s += g(p[0]);
	if (s & 1)
		s += g(p[1]);
	s += g(p[2]);
	if (s & 2)
		s += g(p[3]);
	s += g(p[4]);
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
	long a, b, c;
	int i;

	for (i = 0; i < 32; ++i)
		q[i] = i * 7 - 3;
	a = h(q, 2);
	b = h(q, 3);
	c = h(q, 12);
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
	got=$("$CC_TMP/c") || { echo "run_spill_store_unread: -$o: the program fails"; exit 1; }
	if [ "$got" != "$want" ]; then
		echo "run_spill_store_unread: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
"$CC_BIN" -m32 -O2 -S -o "$CC_TMP/h.s" "$CC_TMP/h.c"
if awk '/^h:/{x=1} /^\.size h,/{x=0} x' "$CC_TMP/h.s" | grep -qE '^	movl %e(bx|si|di), -[0-9]+\(%ebp\)$'; then
	echo "run_spill_store_unread: h() stores a callee-saved register to a slot nothing reads"
	exit 1
fi
echo "run_spill_store_unread: ok"
