#!/bin/sh
# The negation of a double constant expression is a constant, as gcc
# folds it.  cc flipped its sign bit through memory, which the optimizer
# does not see through: `(int)-3.5` stayed a floating computation, and
# Linux's rtw89 util.c, whose RTW89_MIN_DBM is (-41.25 * 4), was refused
# under -mno-80387 -mno-sse, which gcc compiles.  Each such function
# compiles at -O2 under them, as gcc's does; and the values -- -0.0's
# sign, a negated NaN's, a quotient's rounding -- are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_double_negate_fold: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/k.c" <<'EOF2'
#define MIN_DBM (-41.25 * (1 << 2))
#define OFFSET (-MIN_DBM)
int quarter(int m) { return m - (int)OFFSET; }
int lowest(void) { return MIN_DBM; }
int neg(void) { return (int)-3.5 + (int)(-3.0); }
EOF2
cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>
#include <math.h>

volatile double z = 0.0;

int main(void)
{
	double a = -0.0, b = -(-41.25 * 4), c = -(1.0 / 3), d = -(double)(1u << 31), e = -(z / z), f = -(0.1 + 0.2);

	printf("%g %d %.17g %.17g %.17g %d %.17g %d\n", a, signbit(a) != 0, b, c, d, signbit(e) != 0, f, (int)-41.75);
	return 0;
}
EOF2
gcc -O0 -o "$CC_TMP/g" "$CC_TMP/v.c" -lm
want=$("$CC_TMP/g")
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	gcc -O2 -mno-80387 -mno-sse -mno-sse2 -c -o "$CC_TMP/g.o" "$CC_TMP/k.c"
	if ! $run "$CC_BIN" -O2 -mno-80387 -mno-sse -mno-sse2 -c -o "$CC_TMP/k.o" "$CC_TMP/k.c" 2>"$CC_TMP/c.err"; then
		echo "run_double_negate_fold: $path: refused, gcc compiles it: $(head -1 "$CC_TMP/c.err")"
		exit 1
	fi
	for o in O0 O2; do
		$run "$CC_BIN" -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c"
		gcc -o "$CC_TMP/c" "$CC_TMP/v.o" -lm
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_double_negate_fold: $path -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_double_negate_fold: ok"
