#!/bin/sh
# An i386 _Float16 crosses a call as gcc's does: a parameter or an
# argument is its bits in the low half of a stack word -- to a variadic
# function too, unpromoted -- and a result its bits in %xmm0.  cc took a
# _Float16 parameter for a double at 8(%ebp), returned one as an integer
# (fistpll) and passed one as a double, so a call between gcc's code and
# cc's, either way, got garbage.  Objects of each compiler call the
# other's -- parameters, results, mixed with ints and doubles, varargs,
# conversions and compares -- and print what gcc's alone print, -O0 and
# -O2, both code paths, no MIR fallback.  Arithmetic is gcc's under
# -std=c11, where gcc evaluates in long double (in GNU modes its excess
# precision depends on where it spills).  Without SSE2 a _Float16 is
# refused, as gcc refuses its conversions and operations.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_i386_float16_abi: skip (no gcc)"
	exit 0
fi
printf 'int main(void){return 0;}\n' >"$CC_TMP/m32.c"
if ! gcc -m32 -msse2 -o "$CC_TMP/m32" "$CC_TMP/m32.c" 2>/dev/null; then
	echo "run_i386_float16_abi: skip (no gcc -m32)"
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
_Float16 pick(int i, _Float16 a, _Float16 b, _Float16 c) { return i == 0 ? a : i == 1 ? b : c; }
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
_Float16 pick(int, _Float16, _Float16, _Float16);
int main(void)
{
	_Float16 a = 2.25f16, b = -0.75f16;
	printf("%g %g %g %g %d %d %d %g %g %g %g\n", (double)add(a, b), (double)mul3(3, a, 0.5, b), tod(a),
	       (double)fromd(3.140625), toi(a * 3), lt(a, b), lt(b, a), (double)vsum(3, a, b, g16), (double)(g16 * a),
	       (double)pick(2, a, b, -65504.0f16), (double)pick(0, (_Float16)1e-7, a, b));
	return 0;
}
EOF
gcc -m32 -O2 -c -o "$CC_TMP/gl.o" "$CC_TMP/lib.c"
gcc -m32 -O2 -c -o "$CC_TMP/gm.o" "$CC_TMP/main.c"
gcc -m32 -o "$CC_TMP/g" "$CC_TMP/gl.o" "$CC_TMP/gm.o"
want=$("$CC_TMP/g")
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	for o in O0 O2; do
		for f in lib main; do
			if ! $run CC_MIR_STATS=1 "$CC_BIN" -m32 -march=pentium4 -msse2 -$o -c -o "$CC_TMP/c$f.o" "$CC_TMP/$f.c" 2>"$CC_TMP/c.err"; then
				echo "run_i386_float16_abi: $path -$o $f.c refused: $(grep -v '^mir:' "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep -q 'not taken' "$CC_TMP/c.err"; then
				echo "run_i386_float16_abi: $path -$o $f.c: $(grep 'not taken' "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
		done
		gcc -m32 -no-pie -o "$CC_TMP/a" "$CC_TMP/cmain.o" "$CC_TMP/gl.o"
		gcc -m32 -no-pie -o "$CC_TMP/b" "$CC_TMP/gm.o" "$CC_TMP/clib.o"
		gcc -m32 -no-pie -o "$CC_TMP/c" "$CC_TMP/cmain.o" "$CC_TMP/clib.o"
		for x in a b c; do
			got=$("$CC_TMP/$x")
			if [ "$got" != "$want" ]; then
				echo "run_i386_float16_abi: $path -$o ($x) prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done

# arithmetic on many halves, gcc's under -std=c11
cat >"$CC_TMP/ar.c" <<'EOF'
#include <stdio.h>
#include <string.h>
__attribute__((noinline)) _Float16 e1(_Float16 a, _Float16 b, _Float16 c) { return a * b + c; }
__attribute__((noinline)) _Float16 e2(_Float16 a, _Float16 b, _Float16 c) { return (a + b) * c / a; }
__attribute__((noinline)) _Float16 e3(_Float16 a, _Float16 b, _Float16 c) { _Float16 t = a / b; return t - c * a; }
__attribute__((noinline)) int e4(_Float16 a, _Float16 b) { return a * b < a + b; }
static unsigned s = 12345;
static _Float16 rnd(void)
{
	unsigned short h;
	_Float16 f;

	s = s * 1103515245u + 12345u;
	h = (unsigned short)(s >> 8) & 0x7bff;
	if (s & 1)
		h |= 0x8000;
	memcpy(&f, &h, 2);
	return f;
}
int main(void)
{
	unsigned long sum = 0;
	int i;

	for (i = 0; i < 20000; i++) {
		_Float16 a = rnd(), b = rnd(), c = rnd(), r[3];
		unsigned short h[3];

		r[0] = e1(a, b, c);
		r[1] = e2(a, b, c);
		r[2] = e3(a, b, c);
		memcpy(h, r, 6);
		sum = sum * 31 + h[0] * 7 + h[1] * 3 + h[2] + e4(a, b);
	}
	printf("%lx\n", sum);
	return 0;
}
EOF
gcc -m32 -std=c11 -O2 -o "$CC_TMP/g" "$CC_TMP/ar.c"
want=$("$CC_TMP/g")
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	for o in O0 O2; do
		$run "$CC_BIN" -m32 -march=pentium4 -msse2 -std=c11 -$o -c -o "$CC_TMP/ar.o" "$CC_TMP/ar.c"
		gcc -m32 -no-pie -o "$CC_TMP/c" "$CC_TMP/ar.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_i386_float16_abi: arithmetic, $path -$o: $got, gcc's $want"
			exit 1
		fi
	done
done

# without SSE2: refused, as gcc refuses the conversion
printf 'float f(_Float16 a) { return a; }\n' >"$CC_TMP/n.c"
if gcc -m32 -march=i686 -S -o /dev/null "$CC_TMP/n.c" 2>/dev/null; then
	echo "run_i386_float16_abi: gcc takes _Float16 without SSE2; the test is wrong"
	exit 1
fi
if "$CC_BIN" -m32 -march=i686 -S -o "$CC_TMP/n.s" "$CC_TMP/n.c" 2>/dev/null; then
	echo "run_i386_float16_abi: a _Float16 without SSE2 is taken; gcc refuses it"
	exit 1
fi
echo "run_i386_float16_abi: ok"
