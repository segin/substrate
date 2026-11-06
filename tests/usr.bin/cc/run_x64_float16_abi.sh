#!/bin/sh
# An x86-64 _Float16 crosses a call as gcc's does: its bits in an %xmm
# register (an eightbyte's low bytes on the stack past the eighth), to a
# variadic function too, unpromoted, and a result's in %xmm0.  cc gave a
# half-returning function an integer result and passed a half as the
# double it holds, so a call between gcc's code and cc's, either way, got
# garbage.  Objects of each compiler call the other's -- parameters and
# results, mixed with ints and doubles, ten halves (two on the stack),
# varargs past the registers, conversions and compares -- and print what
# gcc's alone print, -O0 and -O2, both code paths, no MIR fallback.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_x64_float16_abi: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/lib.c" <<'EOF'
#include <stdarg.h>
_Float16 g16 = 1.5f16;
_Float16 add(_Float16 a, _Float16 b) { return a + b; }
_Float16 mul3(int k, _Float16 a, double d, _Float16 b) { return (_Float16)(k * a * b + (_Float16)d); }
double tod(_Float16 a) { return a; }
_Float16 fromd(double d) { return d; }
int toi(_Float16 a) { return a; }
int lt(_Float16 a, _Float16 b) { return a < b; }
float vsum(int n, ...) { va_list ap; float s = 0; va_start(ap, n); while (n--) s += va_arg(ap, _Float16); va_end(ap); return s; }
_Float16 many(_Float16 a, _Float16 b, _Float16 c, _Float16 d, _Float16 e, _Float16 f, _Float16 g, _Float16 h,
              _Float16 i, _Float16 j) { return a + b + c + d + e + f + g + h + i * 2 + j * 3; }
EOF
cat >"$CC_TMP/main.c" <<'EOF'
#include <stdio.h>
extern _Float16 g16;
_Float16 add(_Float16, _Float16);
_Float16 mul3(int, _Float16, double, _Float16);
double tod(_Float16);
_Float16 fromd(double);
int toi(_Float16);
int lt(_Float16, _Float16);
float vsum(int, ...);
_Float16 many(_Float16, _Float16, _Float16, _Float16, _Float16, _Float16, _Float16, _Float16, _Float16, _Float16);
int main(void)
{
	_Float16 a = 2.25f16, b = -0.75f16;
	printf("%g %g %g %g %d %d %d %g %g %g\n", (double)add(a, b), (double)mul3(3, a, 0.5, b), tod(a),
	       (double)fromd(3.140625), toi(a * 3), lt(a, b), lt(b, a),
	       (double)vsum(10, a, b, g16, a, b, a, b, a, b, (_Float16)7), (double)(g16 * a),
	       (double)many(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
	return 0;
}
EOF
gcc -O2 -c -o "$CC_TMP/gl.o" "$CC_TMP/lib.c"
gcc -O2 -c -o "$CC_TMP/gm.o" "$CC_TMP/main.c"
gcc -o "$CC_TMP/g" "$CC_TMP/gl.o" "$CC_TMP/gm.o"
want=$("$CC_TMP/g")
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	for o in O0 O2; do
		for f in lib main; do
			if ! $run CC_MIR_STATS=2 "$CC_BIN" -$o -c -o "$CC_TMP/c$f.o" "$CC_TMP/$f.c" 2>"$CC_TMP/c.err"; then
				echo "run_x64_float16_abi: $path -$o $f.c refused: $(grep -v '^mir:' "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep -q 'not taken' "$CC_TMP/c.err"; then
				echo "run_x64_float16_abi: $path -$o $f.c: $(grep 'not taken' "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
		done
		gcc -o "$CC_TMP/a" "$CC_TMP/cmain.o" "$CC_TMP/gl.o"
		gcc -o "$CC_TMP/b" "$CC_TMP/gm.o" "$CC_TMP/clib.o"
		gcc -o "$CC_TMP/c" "$CC_TMP/cmain.o" "$CC_TMP/clib.o"
		for x in a b c; do
			got=$("$CC_TMP/$x")
			if [ "$got" != "$want" ]; then
				echo "run_x64_float16_abi: $path -$o ($x) prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_x64_float16_abi: ok"
