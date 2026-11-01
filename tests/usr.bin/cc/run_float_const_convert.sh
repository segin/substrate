#!/bin/sh
# A floating constant's conversion to an integer is a constant at -O0
# too, as gcc folds it: cc converted `(int)3.5` at run time, unless the
# optimizer ran, and refused it under -mno-80387 -mno-sse, which gcc
# compiles.  The values of such conversions -- of double, float and long
# double constants, negative ones, to integers of each width and
# signedness and _Bool (0.5 is true), explicit and implicit, those past
# a long long's range left
# to the code -- are gcc's, at -O0 and -O2, -m64 and -m32, through both
# code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_float_const_convert: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>

int main(void)
{
	int i = -41.25 * 4;
	unsigned u = 4e9;
	long long ll = -1e18;
	unsigned long long big = 1.5e19;
	short s = -3.99;
	unsigned char c = 255.9;

	_Bool b0 = 0.5, b1 = -0.25, b2 = 0.0, b3 = 2.5;

	printf("%d %d %d %d %d %d %u %lld %llu %d %u %lld %lld %d %d%d%d%d %d%d\n", i, (int)3.5, (int)-3.5, (int)3.5f, (int)-2.5f,
	       (int)-3.5L, u, ll, big, s, c, (long long)(3.141592653589 * 32768.0), (long long)-0.5, (int)(1e9 + 0.75),
	       b0, b1, b2, b3, (_Bool)0.5, (_Bool)-0.0);
	return 0;
}
EOF2
for m in 64 32; do
	# (i386 evaluates a double operation in long double: its folding keeps that precision)
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O2; do
			$run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c"
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_float_const_convert: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
# at -O0 no conversion is left to the code for them
printf 'int f(void) { return (int)3.5 + (int)(-41.25 * 4) + (int)2.5f + (int)-2.5f; }\n' >"$CC_TMP/f.c"
"$CC_BIN" -O0 -S -o "$CC_TMP/f.s" "$CC_TMP/f.c"
if grep -qE 'cvtt|fist|xmm' "$CC_TMP/f.s"; then
	echo "run_float_const_convert: -O0 converts a constant at run time"
	grep -E 'cvtt|fist|xmm' "$CC_TMP/f.s"
	exit 1
fi
echo "run_float_const_convert: ok"
