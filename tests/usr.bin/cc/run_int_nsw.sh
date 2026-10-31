#!/bin/sh
# On x86-64 an int lives in a 64-bit register, extended; a sum of two
# ints was extended again after the add, as if it might have wrapped.
# C leaves signed overflow undefined and gcc assumes it does not
# happen, so a sum, difference or product of ints held extended is held
# extended: a loop's `i++` is one add, not an add and a movslq.  Under
# -fwrapv or -fno-strict-overflow (Linux's) an int wraps and is extended
# as before.  What wraps by definition still does: a long's sum
# converted to int, and unsigned arithmetic.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_int_nsw: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/n.c" <<'EOF2'
#include <stdio.h>

#define NI __attribute__((noinline))

NI long sum(const int *v, int n)
{
	long s = 0;
	int i;

	for (i = 0; i < n; i++)
		s += v[i];
	return s;
}

NI long mix(const int *v, int n, int k)
{
	long s = 0;
	int i;

	for (i = n - 1; i >= 0; --i)
		s += (long)(v[i] * k - i) + v[(i + k) % n];
	return s;
}

/* a long's sum converted to int: the conversion wraps */
NI int narrow(long a, long b) { return (int)(a + b); }
/* the same of ints widened: the sum is a long's, and wraps converted to int, read as a long again */
NI long narrow2(int a, int b) { return (int)((long)a + (long)b); }
/* unsigned arithmetic wraps */
NI long uwrap(unsigned a, unsigned b) { return (long)(a + b) + (long)(a * b); }
NI long widen(int a, int b) { return a - b; }

int main(void)
{
	static const int v[] = {5, -3, 0x7fff0000, -0x7fff0000, 17, 1 << 20, -9, 1000};

	printf("%ld %ld %ld %ld %d %d %ld %ld %ld\n", narrow2(0x7fffffff, 9), sum(v, 8), mix(v, 8, 3), mix(v, 5, -2), narrow(0x7fffffffL, 9),
	       narrow(-0x80000000L, -1), uwrap(0xfffffff0u, 0x20u), widen(-0x40000000, 0x3fffffff), widen(7, 9));
	return 0;
}
EOF2
cat >"$CC_TMP/w.c" <<'EOF2'
#include <stdio.h>

#define NI __attribute__((noinline))

/* overflows: defined under -fwrapv */
NI long next(int i) { return (long)(i + 1); }
NI int count(int from, int n)
{
	int c = 0;
	int i;

	for (i = from; c < n; i++)
		c += i < 0;
	return c;
}

int main(void)
{
	printf("%ld %ld %d\n", next(0x7fffffff), next(-5), count(0x7ffffffe, 3));
	return 0;
}
EOF2
gcc -O1 -o "$CC_TMP/g" "$CC_TMP/n.c"
want=$("$CC_TMP/g")
gcc -O1 -fwrapv -o "$CC_TMP/gw" "$CC_TMP/w.c"
wantw=$("$CC_TMP/gw")
for o in O0 O1 O2; do
	"$CC_BIN" -m64 -$o -c -o "$CC_TMP/n.o" "$CC_TMP/n.c"
	gcc -no-pie -o "$CC_TMP/c" "$CC_TMP/n.o"
	got=$("$CC_TMP/c")
	if [ "$got" != "$want" ]; then
		echo "run_int_nsw: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
	for f in -fwrapv -fno-strict-overflow; do
		"$CC_BIN" -m64 -$o $f -c -o "$CC_TMP/w.o" "$CC_TMP/w.c"
		gcc -no-pie -o "$CC_TMP/cw" "$CC_TMP/w.o"
		got=$("$CC_TMP/cw")
		if [ "$got" != "$wantw" ]; then
			echo "run_int_nsw: -$o $f prints [$got], gcc -fwrapv's [$wantw]"
			exit 1
		fi
	done
done
# sum()'s loop: i++ is an add, no movslq of it; under -fwrapv the movslq is back
body() {
	"$CC_BIN" -m64 -O2 $1 -S -o "$CC_TMP/n.s" "$CC_TMP/n.c"
	awk '/^sum:/{x=1} /^\.size sum,/{x=0} x' "$CC_TMP/n.s"
}
if body "" | grep -qE '^	movslq %e?[a-z0-9]+d?, '; then
	echo "run_int_nsw: sum() extends i++ on every trip"
	body ""
	exit 1
fi
for f in -fwrapv -fno-strict-overflow; do
	if ! body $f | grep -qE '^	movslq %e?[a-z0-9]+d?, '; then
		echo "run_int_nsw: sum() under $f does not extend i++"
		body $f
		exit 1
	fi
done
echo "run_int_nsw: ok"
