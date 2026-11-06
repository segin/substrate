#!/bin/sh
# i386 soft-float (-mno-80387) takes a floating operand of an asm as gcc
# takes it there: a float's bits in a general register, a double's in a
# pair, any of them in memory -- only the x87's "t", "u" and "f" are
# refused ("impossible constraint").  cc refused every asm with a floating
# operand under -mno-80387.  Floats and doubles through "r", "+r", "0",
# "g" and "m", the asm changing a float's bits in its register, a long
# double through "m": the program prints what gcc's prints, -O0 and -O2,
# through both code paths, with nothing off the MIR path (the routines from
# i386_soft_float_routines.c); and "+t" is refused, as gcc refuses it.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
TDIR=$(cd "$(dirname "$0")" && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_i386_soft_float_asm: skip (no gcc)"
	exit 0
fi
if ! printf 'int main(void) { return 0; }\n' >"$CC_TMP/m32.c" ||
	! gcc -m32 -o "$CC_TMP/m32" "$CC_TMP/m32.c" 2>/dev/null; then
	echo "run_i386_soft_float_asm: skip (no gcc -m32)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>

volatile float vf = 2.5f;
volatile double vd = -1.125;
volatile long double vx = 3.75L;

static float negf(float f)
{
	__asm__("xorl $0x80000000, %0" : "+r"(f));
	return f;
}

static double same(double x)
{
	__asm__("" : "+r"(x));
	return x;
}

static float fsame(float x)
{
	float y;

	__asm__("" : "=r"(y) : "0"(x));
	return y;
}

static double through_mem(double x)
{
	double y;

	__asm__("movl %1, %%eax\n\tmovl %%eax, %0\n\tmovl 4+%1, %%eax\n\tmovl %%eax, 4+%0" : "=m"(y) : "m"(x) : "eax");
	return y;
}

static long double ld_mem(long double x)
{
	long double y;

	__asm__("movl %1, %%eax\n\tmovl %%eax, %0\n\tmovl 4+%1, %%eax\n\tmovl %%eax, 4+%0\n\t"
		"movl 8+%1, %%eax\n\tmovl %%eax, 8+%0" : "=m"(y) : "m"(x) : "eax");
	return y;
}

static float fbits_in(float f, unsigned *u)
{
	__asm__("movl %1, %0" : "=r"(*u) : "r"(f));
	return f * 2;
}

static double g_in(double x)
{
	__asm__ volatile("" : : "g"(x));
	return x + 1;
}

int main(void)
{
	unsigned u;
	float f = fbits_in(vf, &u);

	printf("%a %a %a %La\n", negf(vf), same(vd), through_mem(vd), ld_mem(vx));
	printf("%#x %a %a %a\n", u, f, g_in(vd), fsame(vf));
	return 0;
}
EOF2
gcc -m32 -O1 -mno-sse -fno-pic -c -o "$CC_TMP/soft.o" "$TDIR/i386_soft_float_routines.c"
gcc -m32 -O0 -mno-80387 -mno-sse -fno-pic -c -o "$CC_TMP/g.o" "$CC_TMP/v.c"
gcc -m32 -no-pie -o "$CC_TMP/g" "$CC_TMP/g.o" "$CC_TMP/soft.o"
want=$("$CC_TMP/g")
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	for o in O0 O2; do
		if ! $run CC_MIR_STATS=2 "$CC_BIN" -m32 -$o -mno-80387 -c -o "$CC_TMP/c.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
			echo "run_i386_soft_float_asm: $path -$o refused: $(grep -v '^mir:' "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if [ $path = mir ] && grep -q 'not taken' "$CC_TMP/c.err"; then
			echo "run_i386_soft_float_asm: $path -$o: a function left the MIR path:"
			grep 'not taken' "$CC_TMP/c.err"
			exit 1
		fi
		gcc -m32 -no-pie -o "$CC_TMP/c" "$CC_TMP/c.o" "$CC_TMP/soft.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_i386_soft_float_asm: $path -$o differs from gcc -mno-80387:"
			printf '%s\n' "$want" >"$CC_TMP/want"
			printf '%s\n' "$got" >"$CC_TMP/got"
			diff "$CC_TMP/want" "$CC_TMP/got" || true
			exit 1
		fi
	done
done
# an x87 register for a floating operand: gcc's "impossible constraint", and cc's refusal
printf 'double f(double x) { __asm__("" : "+t"(x)); return x; }\n' >"$CC_TMP/t.c"
if gcc -m32 -O2 -mno-80387 -mno-sse -c -o "$CC_TMP/g.o" "$CC_TMP/t.c" 2>/dev/null; then
	echo "run_i386_soft_float_asm: gcc compiles \"+t\" under -mno-80387, unlike this test expects"
	exit 1
fi
if "$CC_BIN" -m32 -O2 -mno-80387 -c -o "$CC_TMP/c.o" "$CC_TMP/t.c" 2>"$CC_TMP/c.err"; then
	echo "run_i386_soft_float_asm: \"+t\" compiled under -mno-80387, which gcc refuses"
	exit 1
fi
if ! grep -q 'impossible constraint' "$CC_TMP/c.err"; then
	echo "run_i386_soft_float_asm: \"+t\" refused without saying why: $(head -1 "$CC_TMP/c.err")"
	exit 1
fi
echo "run_i386_soft_float_asm: ok"
