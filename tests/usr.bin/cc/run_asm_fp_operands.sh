#!/bin/sh
# Inline asm takes floating operands in registers, as gcc does.  On the
# x87: "t" is st(0), "u" st(1), "f" any position; the inputs are pushed
# before the asm, the outputs ("=t", "=u") popped into their values after,
# and the inputs the asm left on the stack -- neither clobbered (as gcc
# requires of one it pops, "st", "st(1)") nor matched to an output --
# thrown away.  "x" is an %xmm register: a double's 8 bytes, a float
# rounded to single precision.  cc refused every floating operand that was
# not in memory ("floating asm inputs are not supported yet").  Checked
# through the register allocator and the old emitter (CC_NO_MIR=1), on
# x86-64 and i386 (-msse2 for "x"), against gcc's build of the same
# program.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_fp_operands: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/a.c" <<'EOF2'
#include <stdio.h>

static double __attribute__((noinline)) sq(double x) {
    double r;

    __asm__("fsqrt" : "=t"(r) : "0"(x));
    return r;
}

static long double __attribute__((noinline)) lsq(long double x) {
    long double r;

    __asm__("fsqrt" : "=t"(r) : "0"(x));
    return r;
}

/* y * log2(x): both popped, the result pushed */
static double __attribute__((noinline)) yl2x(double x, double y) {
    double r;

    __asm__("fyl2x" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

/* popped by the asm: clobbered */
static int __attribute__((noinline)) round_int(double x) {
    int i;

    __asm__("fistpl %0" : "=m"(i) : "t"(x) : "st");
    return i;
}

static double __attribute__((noinline)) one(void) {
    double r;

    __asm__("fld1" : "=t"(r));
    return r;
}

/* x is still on the stack after the asm, under the output */
static double __attribute__((noinline)) twice(double x) {
    double r;

    __asm__("fld %1\n\tfadd %%st(0), %%st" : "=&t"(r) : "f"(x));
    return r;
}

/* two outputs */
static void __attribute__((noinline)) sincos2(double x, double *s, double *c) {
    double ss, cc;

    __asm__("fsincos" : "=t"(cc), "=u"(ss) : "0"(x));
    *s = ss;
    *c = cc;
}

#ifdef __SSE2__
static double __attribute__((noinline)) xadd(double a, double b) {
    __asm__("addsd %1, %0" : "+x"(a) : "x"(b));
    return a;
}

static float __attribute__((noinline)) xmulf(float a, float b) {
    float r;

    __asm__("movss %1, %0\n\tmulss %2, %0" : "=&x"(r) : "x"(a), "x"(b));
    return r;
}

static int __attribute__((noinline)) xtrunc(double a) {
    int r;

    __asm__("cvttsd2si %1, %0" : "=r"(r) : "x"(a));
    return r;
}
#endif

int main(void) {
    double s, c;

    sincos2(0.0, &s, &c);
    printf("%.17g %.20Lg %.17g %d %g %g %g %g\n", sq(2.0), lsq(2.0L), yl2x(8.0, 3.0), round_int(41.5), one(),
           twice(2.25), s, c);
#ifdef __SSE2__
    printf("%.17g %.9g %d\n", xadd(1.25, 2.5), xmulf(1.5f, 3.0f), xtrunc(-7.75));
#endif
    return 0;
}
EOF2
check() {
	m=$1
	shift
	# shellcheck disable=SC2068
	gcc -m$m $@ -O1 -o "$CC_TMP/ag" "$CC_TMP/a.c"
	want=$("$CC_TMP/ag")
	for which in mir old; do
		for o in O0 O2; do
			if [ $which = old ]; then
				# shellcheck disable=SC2068
				CC_NO_MIR=1 "$CC_BIN" -m$m $@ -$o -o "$CC_TMP/ac" "$CC_TMP/a.c"
			else
				# shellcheck disable=SC2068
				CC_MIR_STATS=1 "$CC_BIN" -m$m $@ -$o -o "$CC_TMP/ac" "$CC_TMP/a.c" 2>"$CC_TMP/stats"
				if grep "^mir: .*not taken" "$CC_TMP/stats"; then
					echo "run_asm_fp_operands: -m$m $* -$o: the above do not go through the allocator"
					exit 1
				fi
			fi
			got=$("$CC_TMP/ac")
			if [ "$got" != "$want" ]; then
				echo "run_asm_fp_operands: -m$m $* -$o ($which) prints"
				echo "$got"
				echo "where gcc's prints"
				echo "$want"
				exit 1
			fi
		done
	done
}
check 64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	check 32 -march=i686 -mno-sse
	check 32 -march=pentium4 -msse2
fi
echo "run_asm_fp_operands: ok"
