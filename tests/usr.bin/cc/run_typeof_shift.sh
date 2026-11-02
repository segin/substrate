#!/bin/sh
# A shift's type is its promoted left operand's (C11 6.5.7p3), whatever
# its count's; the parser asked for both before it would say, so
# `typeof(x << __builtin_ctzll(m))`, a builtin's call being untyped there,
# was "cannot determine the type of typeof's operand".  Linux's
# FIELD_PREP, `((typeof(mask))(val) << __bf_shf(mask)) & (mask)` with
# __bf_shf __builtin_ctzll, meets it in every FIELD_GET of a FIELD_PREP
# (drivers/iio/dac/ad7293.c).  The types are gcc's, -m64 and -m32.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_typeof_shift: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/s.c" <<'EOF2'
#include <stdio.h>
#define K(x) _Generic((x), int: 'i', unsigned: 'u', long: 'l', unsigned long: 'm', long long: 'x', \
		      unsigned long long: 'y', default: '?')
#define FP(m, v) ({ ((typeof(m))(v) << __builtin_ctzll(m)) & (m); })
int main(void)
{
	unsigned long m = 0xff00UL;
	unsigned char c = 3;
	short s = 1;
	typeof((unsigned long)0 << __builtin_ctzll(m)) a = 1;
	typeof(c << __builtin_ctz(5)) b = 1;
	typeof(s >> __builtin_popcount(7)) d = 1;
	typeof(1ULL << __builtin_ffs(4)) e = 1;
	typeof(FP(0xff00UL, 2)) f = 1;
	typeof(_Generic(FP(0xff00U, 2), default: FP(0xff00U, 2))) g = 1;
	printf("%c%c%c%c%c%c %zu %zu\n", K(a), K(b), K(d), K(e), K(f), K(g), sizeof a, sizeof f);
	return (int)(a + b + d + e + f + g) - 6;
}
EOF2
for m in 64 32; do
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/s.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		if ! $run "$CC_BIN" -m$m -c -o "$CC_TMP/s.o" "$CC_TMP/s.c" 2>"$CC_TMP/c.err"; then
			echo "run_typeof_shift: -m$m $path refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/s.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_typeof_shift: -m$m $path prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_typeof_shift: ok"
