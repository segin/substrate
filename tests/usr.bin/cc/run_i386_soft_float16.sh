#!/bin/sh
# i386 _Float16 under -mno-80387 converts as gcc converts it there: libgcc's
# routines (cc's __extendhfdf2 and __truncdfhf2), the half's bits passed on
# the stack and back in %xmm0, which needs SSE2 -- gcc refuses _Float16
# without it, and so does cc.  cc refused every _Float16 under -mno-80387,
# soft-float (-mfpmath=387) or with SSE's doubles (-mfpmath=sse) alike.
# _Float16 objects -- loaded, stored, computed with, compared, converted to
# and from integers and the other floating types, rounded, overflowing to
# infinity, subnormal -- print what gcc's program prints, -O0 and -O2, with
# nothing off the MIR path and no x87 instruction in cc's code (the
# routines from i386_soft_float_routines.c).
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
TDIR=$(cd "$(dirname "$0")" && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_i386_soft_float16: skip (no gcc)"
	exit 0
fi
if ! printf 'int main(void) { return 0; }\n' >"$CC_TMP/m32.c" ||
	! gcc -m32 -o "$CC_TMP/m32" "$CC_TMP/m32.c" 2>/dev/null; then
	echo "run_i386_soft_float16: skip (no gcc -m32)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>

volatile _Float16 h[6] = { 1.5, -2.25, 0.1, 65504, 6.1e-5, 3 };
volatile _Float16 out[8];
volatile double vd = 0.3;
volatile float vf = 1e-3f;
volatile int vi = -1234;
volatile unsigned short vus = 60000;

int main(void)
{
	int i;

	out[0] = h[0] * h[1];
	out[1] = h[0] + h[2];
	out[2] = h[3] * h[5];
	out[3] = h[4] / h[5];
	out[4] = vd;
	out[5] = vf;
	out[6] = vi;
	out[7] = vus;
	for (i = 0; i < 8; ++i)
		printf("%a ", (double)out[i]);
	printf("\n%d %d %d %u %a %a\n", h[0] < h[1], h[1] < h[0], h[5] == 3, (unsigned)h[3], (double)(h[2] - h[0]),
	       (float)h[4]);
	return 0;
}
EOF2
gcc -m32 -O1 -mno-sse -fno-pic -c -o "$CC_TMP/soft.o" "$TDIR/i386_soft_float_routines.c"
for FLAGS in "-m32 -march=pentium4 -msse2 -mfpmath=387 -mno-80387" "-m32 -march=pentium4 -msse2 -mfpmath=sse -mno-80387"; do
	# shellcheck disable=SC2086
	gcc $FLAGS -O0 -fno-pic -c -o "$CC_TMP/g.o" "$CC_TMP/v.c"
	gcc -m32 -no-pie -o "$CC_TMP/g" "$CC_TMP/g.o" "$CC_TMP/soft.o"
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		# shellcheck disable=SC2086
		if ! CC_MIR_STATS=1 "$CC_BIN" $FLAGS -$o -c -o "$CC_TMP/c.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
			echo "run_i386_soft_float16: [$FLAGS] -$o refused: $(grep -v '^mir:' "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep -q 'not taken' "$CC_TMP/c.err"; then
			echo "run_i386_soft_float16: [$FLAGS] -$o: a function left the MIR path:"
			grep 'not taken' "$CC_TMP/c.err"
			exit 1
		fi
		if objdump -d --no-show-raw-insn "$CC_TMP/c.o" | grep -E ':	+f[a-z0-9]+( |$)' >"$CC_TMP/x87"; then
			echo "run_i386_soft_float16: [$FLAGS] -$o: x87 instructions in cc's code:"
			head -5 "$CC_TMP/x87"
			exit 1
		fi
		gcc -m32 -no-pie -o "$CC_TMP/c" "$CC_TMP/c.o" "$CC_TMP/soft.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_i386_soft_float16: [$FLAGS] -$o differs from gcc's:"
			printf '%s\n' "$want" >"$CC_TMP/want"
			printf '%s\n' "$got" >"$CC_TMP/got"
			diff "$CC_TMP/want" "$CC_TMP/got" || true
			exit 1
		fi
	done
done
# without SSE2 gcc refuses it, and so does cc, saying why
printf '_Float16 h;\nvoid set(double d) { h = d; }\n' >"$CC_TMP/n.c"
if gcc -m32 -mno-80387 -mno-sse -c -o "$CC_TMP/g.o" "$CC_TMP/n.c" 2>/dev/null; then
	echo "run_i386_soft_float16: gcc compiles _Float16 under -mno-80387 -mno-sse, unlike this test expects"
	exit 1
fi
if "$CC_BIN" -m32 -mno-80387 -mno-sse -c -o "$CC_TMP/c.o" "$CC_TMP/n.c" 2>"$CC_TMP/c.err"; then
	echo "run_i386_soft_float16: _Float16 compiled under -mno-80387 -mno-sse, which gcc refuses"
	exit 1
fi
if ! grep -q 'SSE2' "$CC_TMP/c.err"; then
	echo "run_i386_soft_float16: refused without naming SSE2: $(head -1 "$CC_TMP/c.err")"
	exit 1
fi
echo "run_i386_soft_float16: ok"
