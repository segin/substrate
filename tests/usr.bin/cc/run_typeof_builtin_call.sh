#!/bin/sh
# typeof() of an expression with a builtin's call in it: the parser had no
# type for __builtin_ctz(x) and its kin, so `typeof(__builtin_popcount(x)
# + 1UL)` was "cannot determine the type of typeof's operand".  The bit
# counts (clz, ctz, ffs, popcount, parity, clrsb, with l and ll) give int,
# __builtin_constant_p int and __builtin_expect long, as gcc's do; -m64
# and -m32, both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_typeof_builtin_call: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stdio.h>
#define K(x) _Generic((x), int: 'i', unsigned: 'u', long: 'l', unsigned long: 'm', long long: 'x', \
		      unsigned long long: 'y', default: '?')
int main(void)
{
	unsigned long long v = 0xf0;
	long c = 3;
	typeof(__builtin_ctzll(v)) a = 1;
	typeof(__builtin_clzl(v) + 1U) b = 1;
	typeof(__builtin_popcount(7) + 1UL) d = 1;
	typeof(__builtin_parityll(v) * 2) e = 1;
	typeof(__builtin_ffs(4) - 1LL) f = 1;
	typeof(__builtin_clrsb(-1)) g = 1;
	typeof(__builtin_constant_p(c)) h = 1;
	typeof(__builtin_expect(c, 1)) k = 1;
	typeof(-__builtin_ctz(8)) n = 1;
	printf("%c%c%c%c%c%c%c%c%c %zu\n", K(a), K(b), K(d), K(e), K(f), K(g), K(h), K(k), K(n), sizeof k);
	return (int)(a + b + d + e + f + g + h + k + n) - 9;
}
EOF2
for m in 64 32; do
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/b.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		if ! $run "$CC_BIN" -m$m -c -o "$CC_TMP/b.o" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
			echo "run_typeof_builtin_call: -m$m $path refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/b.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_typeof_builtin_call: -m$m $path prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_typeof_builtin_call: ok"
