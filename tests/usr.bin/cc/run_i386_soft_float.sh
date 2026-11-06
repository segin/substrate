#!/bin/sh
# i386 floating point without the x87 (-mno-80387) is soft-float, as gcc
# compiles it: each operation a call of libgcc's soft-float routines
# (__adddf3, __ltdf2, __fixdfsi, __extendsfdf2, ...), a float returned in
# %eax, a double in %edx:%eax, a long double in %eax:%edx:%ecx, and each
# operation evaluated in its type (__FLT_EVAL_METHOD__ 0, _SOFT_FLOAT).
# cc refused every function that computed in floating point.
#
# i386 libgcc ships no such routines (it has the x87), so the test makes
# them (i386_soft_float_routines.c): in C, compiled by gcc with the x87,
# taking and returning the bits as integers, and for a long double result
# a thunk that returns its 12 bytes in %eax:%edx:%ecx.  One program -- float, double and long double
# arithmetic, the six compares with NaN among them, conversions to and
# from integers of each width and signedness, among the three floating
# types (one that would round twice through a double among them),
# constants, parameters and results, calls through a pointer, a loop,
# conditions on floating values -- prints the same with gcc -mno-80387 and
# with cc, -O0 and -O2, through both code paths, and cc's code has no x87
# instruction in it.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_i386_soft_float: skip (no gcc)"
	exit 0
fi
if ! printf 'int main(void) { return 0; }\n' >"$CC_TMP/m32.c" ||
	! gcc -m32 -o "$CC_TMP/m32" "$CC_TMP/m32.c" 2>/dev/null; then
	echo "run_i386_soft_float: skip (no gcc -m32)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

TDIR=$(cd "$(dirname "$0")" && pwd)
cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>

volatile float vf[4] = { 1.5f, -2.25f, 3.0f, 0.1f };
volatile double vd[4] = { 1.5, -2.25, 3.0, 0.1 };
volatile long double vx[4] = { 1.5L, -2.25L, 3.0L, 0.1L };
volatile double zero = 0.0;
volatile long long vll = 1152921573326323713LL; /* 2^60 + 2^36 + 1: twice rounded, 2^60 */
volatile unsigned long long vull = 18446744073709549568ULL;
volatile long double vtwice = 1.0L + 0x1p-24L + 0x1p-60L;
volatile int vi = -7;
volatile unsigned vu = 4000000000u;
volatile signed char vsc = -100;
volatile unsigned char vuc = 200;
volatile short vs = -30000;
volatile unsigned short vus = 60000;

struct pt { float x; double y; long double z; };

static float addf(float a, float b) { return a + b; }
static double muld(double a, double b) { return a * b; }
static long double twice(long double x) { x = x * 2; return x; }
long double sub3(long double a, long double b, long double c) { return a - b - c; }
double (*fp)(double, double) = muld;

static void cmps(const char *t, int lt, int le, int gt, int ge, int eq, int ne)
{
	printf("%s %d%d%d%d%d%d\n", t, lt, le, gt, ge, eq, ne);
}

int main(void)
{
	float f0 = vf[0], f1 = vf[1], f3 = vf[3];
	double d0 = vd[0], d1 = vd[1], d3 = vd[3], nan = zero / zero;
	long double x0 = vx[0], x1 = vx[1], x3 = vx[3];
	struct pt p = { f3, d3, x3 };
	double sum = 0;
	float fl = 8.0f;
	int i, n = 0;

	printf("f %a %a %a %a\n", f0 + f1, f0 - f3, f1 * f3, f0 / f3);
	printf("d %a %a %a %a\n", d0 + d1, d0 - d3, d1 * d3, d0 / d3);
	printf("x %La %La %La %La\n", x0 + x1, x0 - x3, x1 * x3, x0 / x3);
	cmps("f", f0 < f1, f0 <= f0, f0 > f1, f1 >= f0, f0 == f0, f0 != f1);
	cmps("d", d0 < d1, d0 <= d0, d0 > d1, d1 >= d0, d0 == d0, d0 != d1);
	cmps("x", x0 < x1, x0 <= x0, x0 > x1, x1 >= x0, x0 == x0, x0 != x1);
	cmps("n", nan < d0, nan <= d0, nan > d0, nan >= d0, nan == nan, nan != nan);
	printf("i2f %a %a %a %a %a %a %a\n", (float)vi, (double)vu, (double)vsc, (float)vuc, (double)vs, (float)vus,
	       (double)vull);
	printf("i2x %La %La %La\n", (long double)vi, (long double)vu, (long double)vll);
	printf("ll %a %a %a\n", (float)vll, (double)vll, (float)vull);
	printf("f2i %d %u %d %u %d %u %lld %llu\n", (int)d1, (unsigned)(d0 * 1e9), (signed char)f1, (unsigned char)f0,
	       (short)(d1 * 1000), (unsigned short)(f0 * 1000), (long long)(d0 * 1e15), (unsigned long long)(vx[2] * 6e18L));
	printf("x2i %d %lld %u\n", (int)x1, (long long)(x0 * 1e17L), (unsigned)(x0 * 2e9L));
	printf("cvt %a %a %La %La %a %a\n", (double)f3, (float)d3, (long double)f3, (long double)d3, (double)x3,
	       (float)x3);
	printf("twice %a %a\n", (float)vtwice, (double)vtwice);
	printf("const %a %a %La %La\n", 1e-30f, 3.141592653589793, 1.0L / 3, -0.0L);
	printf("call %a %a %La %La %a\n", addf(f0, f3), muld(d0, d1), twice(x1), sub3(x0, x1, x3), fp(d3, d0));
	printf("pt %a %a %La\n", p.x + 1, p.y * 2, p.z - 1);
	for (i = 1; i <= 10; ++i)
		sum += 1.0 / i;
	while (fl > 0.5f) {
		fl /= 2;
		++n;
	}
	printf("loop %a %d %a\n", sum, n, fl);
	printf("cond %d %d %d %d %d\n", d3 ? 1 : 2, !f3, x3 && d0, zero ? 3 : 4, nan ? 5 : 6);
	return 0;
}
EOF2
gcc -m32 -O1 -mno-sse -fno-pic -c -o "$CC_TMP/soft.o" "$TDIR/i386_soft_float_routines.c"
gcc -m32 -O0 -mno-80387 -fno-pic -c -o "$CC_TMP/g.o" "$CC_TMP/v.c"
gcc -m32 -no-pie -o "$CC_TMP/g" "$CC_TMP/g.o" "$CC_TMP/soft.o"
want=$("$CC_TMP/g")
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	for o in O0 O2; do
		if ! $run CC_MIR_STATS=2 "$CC_BIN" -m32 -$o -mno-80387 -c -o "$CC_TMP/c.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
			echo "run_i386_soft_float: $path -$o: refused: $(grep -v '^mir:' "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if [ $path = mir ] && grep -q 'not taken' "$CC_TMP/c.err"; then
			echo "run_i386_soft_float: $path -$o: a function left the MIR path:"
			grep 'not taken' "$CC_TMP/c.err"
			exit 1
		fi
		if objdump -d --no-show-raw-insn "$CC_TMP/c.o" | grep -E ':	+f[a-z0-9]+( |$)' >"$CC_TMP/x87"; then
			echo "run_i386_soft_float: $path -$o: x87 instructions in cc's code:"
			head -5 "$CC_TMP/x87"
			exit 1
		fi
		gcc -m32 -no-pie -o "$CC_TMP/c" "$CC_TMP/c.o" "$CC_TMP/soft.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_i386_soft_float: $path -$o differs from gcc -mno-80387:"
			printf '%s\n' "$want" >"$CC_TMP/want"
			printf '%s\n' "$got" >"$CC_TMP/got"
			diff "$CC_TMP/want" "$CC_TMP/got" || true
			exit 1
		fi
	done
done
# gcc's predefined macros for it
for m in "__FLT_EVAL_METHOD__ 0" "_SOFT_FLOAT 1"; do
	if ! echo | "$CC_BIN" -m32 -mno-80387 -dM -E - | grep -q "#define $m\$"; then
		echo "run_i386_soft_float: -m32 -mno-80387 does not define $m, as gcc does"
		exit 1
	fi
done
echo "run_i386_soft_float: ok"
