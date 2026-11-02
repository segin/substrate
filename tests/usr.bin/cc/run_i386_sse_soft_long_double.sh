#!/bin/sh
# i386 -mno-80387 where SSE does the doubles (-mfpmath=sse) makes long
# double soft-float, as gcc does: its operations calls of libgcc's routines
# (__addxf3, __extenddfxf2, __truncxfdf2, ...), the conversions of a 64-bit
# integer too (__floatdidf, __fixdfdi), floats and doubles SSE's, and a
# float, a double and a long double returned in %eax, %edx:%eax and
# %eax:%edx:%ecx.  cc refused every function with a long double, and every
# one that returned a floating value.  Functions compiled by cc and by gcc
# call each other both ways and print what the program gcc compiles whole
# prints, -O0 and -O2, with nothing off the MIR path and no x87
# instruction in cc's code; __FLT_EVAL_METHOD__ is 0 and _SOFT_FLOAT is
# defined, as gcc defines them.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
TDIR=$(cd "$(dirname "$0")" && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_i386_sse_soft_long_double: skip (no gcc)"
	exit 0
fi
if ! printf 'int main(void) { return 0; }\n' >"$CC_TMP/m32.c" ||
	! gcc -m32 -o "$CC_TMP/m32" "$CC_TMP/m32.c" 2>/dev/null; then
	echo "run_i386_sse_soft_long_double: skip (no gcc -m32)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD
FLAGS="-m32 -march=pentium4 -msse2 -mfpmath=sse -mno-80387"

cat >"$CC_TMP/lib.c" <<'EOF2'
float halve(float x) { return x / 2; }
double cube(double x) { return x * x * x; }
long double third(long double x) { return x / 3; }
long double mixed(long double a, double b, float c) { return a * b + c; }
double narrow(long double x) { return x; }
long double widen(double x) { return x; }
int below(long double a, long double b) { return a < b; }
long double fromll(long long i) { return i; }
unsigned long long toull(long double x) { return x; }
double dfromll(long long i) { return i; }
long long dtoll(double d) { return d; }
float ffromull(unsigned long long i) { return i; }
unsigned dtou(double d) { return d; }
double dfromu(unsigned u) { return u; }
int dtoi(double d) { return d; }
long double xapply(long double (*f)(long double), long double x) { return f(x) - 1; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
#include <stdio.h>

float halve(float);
double cube(double);
long double third(long double);
long double mixed(long double, double, float);
double narrow(long double);
long double widen(double);
int below(long double, long double);
long double fromll(long long);
unsigned long long toull(long double);
double dfromll(long long);
long long dtoll(double);
float ffromull(unsigned long long);
unsigned dtou(double);
double dfromu(unsigned);
int dtoi(double);
long double xapply(long double (*)(long double), long double);

static long double sq(long double x) { return x * x; }
volatile float vf = 5.5f;
volatile double vd = 1.75;
volatile long double vx = 2.5L, vy = 0.1L, zero = 0;
volatile long long vll = 1152921573326323713LL;
volatile unsigned long long vull = 18446744073709549568ULL;

int main(void)
{
	long double x = vx, y = vy, nan = zero / zero, s = 0;
	int i;

	printf("%a %a %La\n", halve(vf), cube(vd), third(vx));
	printf("%La %La %La %La\n", x + y, x - y, x * y, x / y);
	printf("%La %a %La\n", mixed(x, vd, vf), narrow(y), widen(vd));
	printf("%d %d %d %d %d %d\n", below(x, y), below(y, x), x == y, x != y, nan < x, nan != nan);
	printf("%La %llu %a %lld %a\n", fromll(vll), toull(x * 1e18L), dfromll(vll), dtoll(vd * 1e15), ffromull(vull));
	printf("%u %a %d\n", dtou(vd * 2e9), dfromu(4000000000u), dtoi(-vd * 1e6));
	printf("%La %a %a\n", xapply(sq, x), (double)(x / 7), (float)(y * 3));
	for (i = 1; i <= 10; ++i)
		s += 1.0L / i;
	printf("%La %d\n", s, s > 2 ? 1 : 0);
	return 0;
}
EOF2
gcc -m32 -O1 -mno-sse -fno-pic -c -o "$CC_TMP/soft.o" "$TDIR/i386_soft_float_routines.c"
# -mfpmath=sse, and SSE2 with no -mfpmath, where cc's SSE does the doubles (gcc's soft-float, by the same ABI)
for FLAGS in "$FLAGS" "-m32 -march=pentium4 -msse2 -mno-80387"; do
	# shellcheck disable=SC2086
	gcc $FLAGS -O2 -fno-pic -c -o "$CC_TMP/glib.o" "$CC_TMP/lib.c"
	# shellcheck disable=SC2086
	gcc $FLAGS -O2 -fno-pic -c -o "$CC_TMP/gmain.o" "$CC_TMP/main.c"
	gcc -m32 -no-pie -o "$CC_TMP/g" "$CC_TMP/gmain.o" "$CC_TMP/glib.o" "$CC_TMP/soft.o"
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		for f in lib main; do
			# shellcheck disable=SC2086
			if ! CC_MIR_STATS=1 "$CC_BIN" $FLAGS -$o -c -o "$CC_TMP/c$f.o" "$CC_TMP/$f.c" 2>"$CC_TMP/c.err"; then
				echo "run_i386_sse_soft_long_double: [$FLAGS] -$o $f.c refused: $(grep -v '^mir:' "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep -q 'not taken' "$CC_TMP/c.err"; then
				echo "run_i386_sse_soft_long_double: [$FLAGS] -$o $f.c: a function left the MIR path:"
				grep 'not taken' "$CC_TMP/c.err"
				exit 1
			fi
			if objdump -d --no-show-raw-insn "$CC_TMP/c$f.o" | grep -E ':	+f[a-z0-9]+( |$)' >"$CC_TMP/x87"; then
				echo "run_i386_sse_soft_long_double: [$FLAGS] -$o $f.c: x87 instructions in cc's code:"
				head -5 "$CC_TMP/x87"
				exit 1
			fi
		done
		for pair in "cmain glib" "gmain clib" "cmain clib"; do
			set -- $pair
			gcc -m32 -no-pie -o "$CC_TMP/c" "$CC_TMP/$1.o" "$CC_TMP/$2.o" "$CC_TMP/soft.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_i386_sse_soft_long_double: [$FLAGS] -$o $1.o with $2.o differs from gcc's:"
				printf '%s\n' "$want" >"$CC_TMP/want"
				printf '%s\n' "$got" >"$CC_TMP/got"
				diff "$CC_TMP/want" "$CC_TMP/got" || true
				exit 1
			fi
		done
	done
	for m in "__FLT_EVAL_METHOD__ 0" "_SOFT_FLOAT 1"; do
		# shellcheck disable=SC2086
		if ! echo | "$CC_BIN" $FLAGS -dM -E - | grep -q "#define $m\$"; then
			echo "run_i386_sse_soft_long_double: $FLAGS does not define $m, as gcc does"
			exit 1
		fi
	done
done
echo "run_i386_sse_soft_long_double: ok"
