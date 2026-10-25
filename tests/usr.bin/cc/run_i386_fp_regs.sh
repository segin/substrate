#!/bin/sh
# i386 floating point goes through the register allocator: every floating
# value in a frame slot of its own, each operation on the x87, so that the
# integers around it are given registers.  Floating parameters and
# arguments in their words on the stack -- a float's one in single
# precision, a double's two, a long double's three -- results back in
# %st0, one nothing takes popped; compares against NaN; conversions both
# ways, unsigned 64-bit ones included.  Every function with a floating
# value used to go to the old emitter, a stack slot for every value.  The
# values are exact in every precision, so gcc's x87 excess precision
# changes nothing.  CC_MIR_STATS says which path took each function; the
# program's output is gcc's.  Needs a host gcc that builds -m32 programs;
# skipped, with a note, if not.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	echo "run_i386_fp_regs: skip (gcc cannot build -m32 programs)"
	exit 0
fi

cat >"$CC_TMP/f.c" <<'EOF2'
#include <stdarg.h>
#include <stdio.h>

static double __attribute__((noinline)) six(double a, float b, long double c, int k, double d, float e) {
    return a - b + (double)c * k - d / e;
}

static float __attribute__((noinline)) half_of(float a) {
    return a / 2;
}

static long double __attribute__((noinline)) ldsum(long double a, long double b, int n) {
    while (n-- > 0)
        a += b;
    return a;
}

static double __attribute__((noinline)) vsum(int n, ...) {
    va_list ap;
    double t = 0;
    int i;

    va_start(ap, n);
    for (i = 0; i < n; ++i)
        t += va_arg(ap, double) * (i + 1);
    va_end(ap);
    return t;
}

static int __attribute__((noinline)) cmps(double a, double b) {
    return (a < b) + 2 * (a <= b) + 4 * (a > b) + 8 * (a >= b) + 16 * (a == b) + 32 * (a != b);
}

static unsigned long long __attribute__((noinline)) d2u(double a) { return a; }
static double __attribute__((noinline)) u2d(unsigned long long a) { return a; }
static unsigned __attribute__((noinline)) d2u32(double a) { return a; }
static double __attribute__((noinline)) u32d(unsigned a) { return a; }
static long long __attribute__((noinline)) ld2ll(long double a) { return a; }
static int __attribute__((noinline)) truth(double a) { return a ? 1 : 2; }

static double __attribute__((noinline)) unused(double a) { return a * 4; }

/* a value kept in registers across floating code */
static long __attribute__((noinline)) mixed(const double *v, int n) {
    long acc = 0;
    int i;

    for (i = 0; i < n; ++i) {
        double x = v[i] * 2;

        acc = acc * 3 + (long)x + i;
    }
    return acc;
}

int main(void) {
    double nan = __builtin_nan("");
    static const double v[5] = {1.5, -2.5, 8.25, 0.5, 100.0};
    int i;

    printf("%.17g %.9g %.20Lg\n", six(10, 2.5f, 1.25L, 4, 3, 0.5f), half_of(7.0f), ldsum(1, 0.125L, 17));
    printf("%.17g\n", vsum(10, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.5));
    printf("%d %d %d %d\n", cmps(1, 2), cmps(2, 1), cmps(3, 3), cmps(nan, 1));
    printf("%llu %llu %.17g %.17g\n", d2u(9223372036854777856.0), d2u(12345.75), u2d(18446744073709549568ULL),
           u2d(5));
    printf("%u %.17g %lld %d %d %d\n", d2u32(4294967040.0), u32d(4294967295U), ld2ll(-1099511627776.0L),
           truth(0.0), truth(-0.0), truth(nan));
    for (i = 0; i < 10; ++i)
        unused(i);
    printf("%ld\n", mixed(v, 5));
    return 0;
}
EOF2
gcc -m32 -O1 -o "$CC_TMP/fg" "$CC_TMP/f.c"
want=$("$CC_TMP/fg")
for o in O0 O2; do
	CC_MIR_STATS=1 "$CC_BIN" -m32 -$o -o "$CC_TMP/fc" "$CC_TMP/f.c" 2>"$CC_TMP/stats"
	if grep "^mir: .*not taken" "$CC_TMP/stats"; then
		echo "run_i386_fp_regs: -$o: the above do not go through the allocator"
		exit 1
	fi
	got=$("$CC_TMP/fc")
	if [ "$got" != "$want" ]; then
		echo "run_i386_fp_regs: -$o prints"
		echo "$got"
		echo "where gcc's prints"
		echo "$want"
		exit 1
	fi
done
echo "run_i386_fp_regs: ok"
