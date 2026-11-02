#!/bin/sh
# i386 -mno-fp-ret-in-387 on its own -- the x87 still there -- returns a
# floating result in the general registers, as gcc does: a float in %eax,
# a double in %edx:%eax, a long double's 12 bytes in %eax:%edx:%ecx, the
# x87 computing everything else.  cc refused every function that returned
# one or called one ("returns its result in the x87's %st0").  Functions
# compiled by cc and by gcc call each other both ways -- results of each
# type, through pointers, a callback, results thrown away -- and print what
# the program gcc compiles whole prints, -O0 and -O2, through both code
# paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_i386_no_fp_ret_in_387: skip (no gcc)"
	exit 0
fi
if ! printf 'int main(void) { return 0; }\n' >"$CC_TMP/m32.c" ||
	! gcc -m32 -o "$CC_TMP/m32" "$CC_TMP/m32.c" 2>/dev/null; then
	echo "run_i386_no_fp_ret_in_387: skip (no gcc -m32)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/lib.c" <<'EOF2'
int bumps;

float halve(float x) { return x / 2; }
double cube(double x) { return x * x * x; }
long double third(long double x) { return x / 3; }
long double bump(void) { ++bumps; return bumps * 1.25L; }
double (*pick(int i))(double) { return i ? cube : 0; }
double apply(double (*f)(double), double x) { return f(x) + 1; }
float fapply(float (*f)(float), float x) { return f(x) * 3; }
long double xapply(long double (*f)(long double), long double x) { return f(x) - 1; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
#include <stdio.h>

extern int bumps;
float halve(float);
double cube(double);
long double third(long double);
long double bump(void);
double (*pick(int))(double);
double apply(double (*)(double), double);
float fapply(float (*)(float), float);
long double xapply(long double (*)(long double), long double);

static double twice(double x) { return x * 2; }
static float negf(float x) { return -x; }
static long double sq(long double x) { return x * x; }
volatile float vf = 5.5f;
volatile double vd = 1.75;
volatile long double vx = 2.5L;

int main(void)
{
	float (*fh)(float) = halve;
	long double b;
	int i;

	printf("%a %a %La\n", halve(vf), cube(vd), third(vx));
	printf("%a %a %La\n", fh(vf + 1), pick(1)(vd + 1), third(vx) + bump());
	for (i = 0; i < 3; ++i)
		(void)bump();
	bump();
	b = bump();
	printf("%d %La\n", bumps, b);
	printf("%a %a %La\n", apply(twice, vd), fapply(negf, vf), xapply(sq, vx));
	printf("%a\n", halve(vf) + cube(vd) * (double)third(vx));
	return 0;
}
EOF2
gcc -m32 -O2 -mno-fp-ret-in-387 -fno-pic -c -o "$CC_TMP/glib.o" "$CC_TMP/lib.c"
gcc -m32 -O2 -mno-fp-ret-in-387 -fno-pic -c -o "$CC_TMP/gmain.o" "$CC_TMP/main.c"
gcc -m32 -no-pie -o "$CC_TMP/g" "$CC_TMP/gmain.o" "$CC_TMP/glib.o"
want=$("$CC_TMP/g")
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	for o in O0 O2; do
		for f in lib main; do
			if ! $run CC_MIR_STATS=1 "$CC_BIN" -m32 -$o -mno-fp-ret-in-387 -c -o "$CC_TMP/c$f.o" "$CC_TMP/$f.c" \
				2>"$CC_TMP/c.err"; then
				echo "run_i386_no_fp_ret_in_387: $path -$o $f.c refused: $(grep -v '^mir:' "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if [ $path = mir ] && grep -q 'not taken' "$CC_TMP/c.err"; then
				echo "run_i386_no_fp_ret_in_387: $path -$o $f.c: a function left the MIR path:"
				grep 'not taken' "$CC_TMP/c.err"
				exit 1
			fi
		done
		for pair in "cmain glib" "gmain clib" "cmain clib"; do
			set -- $pair
			gcc -m32 -no-pie -o "$CC_TMP/c" "$CC_TMP/$1.o" "$CC_TMP/$2.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_i386_no_fp_ret_in_387: $path -$o $1.o with $2.o differs from gcc's:"
				printf '%s\n' "$want" >"$CC_TMP/want"
				printf '%s\n' "$got" >"$CC_TMP/got"
				diff "$CC_TMP/want" "$CC_TMP/got" || true
				exit 1
			fi
		done
	done
done
echo "run_i386_no_fp_ret_in_387: ok"
