#!/bin/sh
# x86-64 floating point goes through the register allocator: floats and
# doubles in vector registers (a float held as the double it rounds to),
# a long double in a frame slot of its own, each operation on the x87.
# Parameters and arguments in %xmm0..7 and on the stack, a float in single
# precision across a call, a long double on the stack and back in %st0,
# structures whose eightbytes are SSE, variadic calls, compares against
# NaN, conversions both ways (unsigned 64-bit included), and _Float16 by
# libgcc's calls.  Every function with a floating value used to go to the
# old emitter, a stack slot for every value.  CC_MIR_STATS says which path
# took each function; the program's output is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_x64_fp_regs: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/f.c" <<'EOF2'
#include <stdarg.h>
#include <stdio.h>

struct dd { double a, b; };
struct ffi { float x, y; int k; };
struct dl { double d; long l; };
struct ld1 { long double v; };

/* ten doubles: two of them on the stack */
static double __attribute__((noinline)) ten(double a, double b, double c, double d, double e, double f, double g,
                                            double h, double i, double j) {
    return a - b + c * d - e / f + g * h - i + j;
}

static float __attribute__((noinline)) fmix(float a, int k, float b, double c) {
    return a * (float)k + b - (float)c;
}

static long double __attribute__((noinline)) ldmix(long double a, double b, int k, long double c) {
    return a * b + (long double)k / c;
}

static struct dd __attribute__((noinline)) swap_dd(struct dd p) {
    struct dd r = {p.b * 2, p.a - 1};
    return r;
}

static struct ffi __attribute__((noinline)) scale_ffi(struct ffi p, float s) {
    struct ffi r = {p.x * s, p.y + s, p.k * 3};
    return r;
}

static struct dl __attribute__((noinline)) mk_dl(long l, double d) {
    struct dl r = {d / 4, l + 7};
    return r;
}

static struct ld1 __attribute__((noinline)) mk_ld1(long double v) {
    struct ld1 r = {v * 3};
    return r;
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

static int __attribute__((noinline)) lcmps(long double a, long double b) {
    return (a < b) + 2 * (a <= b) + 4 * (a > b) + 8 * (a >= b) + 16 * (a == b) + 32 * (a != b);
}

static unsigned long __attribute__((noinline)) d2u(double a) { return a; }
static double __attribute__((noinline)) u2d(unsigned long a) { return a; }
static long __attribute__((noinline)) ld2l(long double a) { return a; }
static long double __attribute__((noinline)) u2ld(unsigned long a) { return a; }
static int __attribute__((noinline)) truth(double a) { return a ? 1 : 2; }

static _Float16 h16;

static double __attribute__((noinline)) half_round(double v) {
    h16 = (_Float16)v;
    return h16;
}

int main(void) {
    struct dd p = {1.5, -2.25};
    struct ffi q = {1.25f, 2.5f, 7};
    double nan = __builtin_nan("");
    struct dd r = swap_dd(p);
    struct ffi s = scale_ffi(q, 0.5f);
    struct dl t = mk_dl(5, 9.0);
    struct ld1 u = mk_ld1(1.0L / 3);

    printf("%.17g\n", ten(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
    printf("%.9g %.20Lg\n", fmix(1.1f, 3, 2.2f, 0.3), ldmix(1.0L / 7, 3.5, 11, 2.0L / 3));
    printf("%g %g %g %g %d %g %ld %.20Lg\n", r.a, r.b, s.x, s.y, s.k, t.d, t.l, u.v);
    printf("%.17g\n", vsum(10, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.5));
    printf("%d %d %d %d\n", cmps(1, 2), cmps(2, 1), cmps(3, 3), cmps(nan, 1));
    printf("%d %d %d\n", lcmps(1, 2), lcmps(3, 3), lcmps(nan, 1));
    printf("%lu %lu %.17g %.17g\n", d2u(1e19), d2u(12345.9), u2d(18446744073709551615UL), u2d(5));
    printf("%ld %.20Lg %d %d %d\n", ld2l(-1e18L), u2ld(18446744073709551615UL), truth(0.0), truth(-0.0), truth(nan));
    printf("%.17g %.17g\n", half_round(1.0 / 3), half_round(65504.0));
    return 0;
}
EOF2
gcc -O1 -o "$CC_TMP/fg" "$CC_TMP/f.c"
want=$("$CC_TMP/fg")
for o in O0 O2; do
	CC_MIR_STATS=1 "$CC_BIN" -m64 -$o -o "$CC_TMP/fc" "$CC_TMP/f.c" 2>"$CC_TMP/stats"
	if grep "^mir: .*not taken" "$CC_TMP/stats"; then
		echo "run_x64_fp_regs: -$o: the above do not go through the allocator"
		exit 1
	fi
	got=$("$CC_TMP/fc")
	if [ "$got" != "$want" ]; then
		echo "run_x64_fp_regs: -$o prints"
		echo "$got"
		echo "where gcc's prints"
		echo "$want"
		exit 1
	fi
done
echo "run_x64_fp_regs: ok"
