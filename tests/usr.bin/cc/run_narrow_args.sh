#!/bin/sh
# On x86-64 an int argument, parameter or result is extended to 64 bits
# only where something reads past its 32: the psABI leaves the upper half
# of a 32-bit argument undefined, so a callee reads its own bits and a
# caller of an int its result's.  Each was a movslq.  A char or a short
# is still extended to 32, which clang's callees read.  The callees here
# are gcc's, compiled apart, and the results gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_narrow_args: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/callee.c" <<'EOF2'
long take(int a, short b, signed char c, unsigned short d, long e)
{
	return (long)a * 1000000 + b * 1000 + c * 10 + d + e;
}
int give(int x) { return x * 3 - 7; }
short gives(int x) { return (short)(x * 1000); }
EOF2
cat >"$CC_TMP/caller.c" <<'EOF2'
#include <stdio.h>

long take(int a, short b, signed char c, unsigned short d, long e);
int give(int x);
short gives(int x);

/* parameters only compared: no extension */
int __attribute__((noinline)) pick(int a, int b, int c)
{
	return a < b ? b : c > a ? c : a;
}

long __attribute__((noinline)) run(int x, int y)
{
	int s = x + y;			/* wraps at 32 bits */
	short h = (short)(x * 7);
	signed char ch = (signed char)(y ^ 0x85);
	long r = take(s, h, ch, (unsigned short)(x - 1), (long)give(s) * 2);

	return r + gives(y) + give(pick(x, y, s)) + (long)give(x) * 3;
}

int main(void)
{
	printf("%ld %ld %d\n", run(0x7fffffff, 3), run(-12345, 678), pick(-1, -5, 2));
	return 0;
}
EOF2
gcc -O1 -fwrapv -c -o "$CC_TMP/callee.o" "$CC_TMP/callee.c"
gcc -O1 -fwrapv -o "$CC_TMP/g" "$CC_TMP/caller.c" "$CC_TMP/callee.o"
want=$("$CC_TMP/g")
for o in O0 O1 O2; do
	"$CC_BIN" -m64 -$o -c -o "$CC_TMP/caller.o" "$CC_TMP/caller.c"
	gcc -o "$CC_TMP/c" "$CC_TMP/caller.o" "$CC_TMP/callee.o"
	got=$("$CC_TMP/c")
	if [ "$got" != "$want" ]; then
		echo "run_narrow_args: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
"$CC_BIN" -m64 -O2 -S -o "$CC_TMP/caller.s" "$CC_TMP/caller.c"
if awk '/^pick:/{x=1} /^\.size pick,/{x=0} x' "$CC_TMP/caller.s" | grep -q movslq; then
	echo "run_narrow_args: pick() extends ints it only compares"
	exit 1
fi
echo "run_narrow_args: ok"
