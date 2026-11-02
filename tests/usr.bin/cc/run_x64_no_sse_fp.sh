#!/bin/sh
# x86-64 under -mno-sse makes no SSE floating point.  cc computed floats
# and doubles in %xmm registers anyway -- the very instructions it was
# told not to use -- returned them in %xmm0, and said
# __FLT_EVAL_METHOD__ 0.  Now, as gcc: a float or a double returned,
# the function's own or a call's, is refused with gcc's error at gcc's
# place ("SSE register return with SSE disabled"); __FLT_EVAL_METHOD__
# is gcc's (2, and -1 under -mno-sse2); and a long double is the x87's
# as ever, the program printing what gcc's prints.  gcc computes the
# other floats and doubles on the x87, which cc does not on x86-64:
# cc refuses them, and under -mno-sse2 the doubles, rather than make
# SSE code.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v objdump >/dev/null 2>&1; then
	echo "run_x64_no_sse_fp: skip (no gcc or objdump)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD
cd "$CC_TMP"

# __FLT_EVAL_METHOD__
for f in "-mno-sse" "-mno-sse2" "-mno-sse -mno-80387" "-msse -mno-sse" "-mno-sse -msse" "-mgeneral-regs-only"; do
	g=$(gcc -m64 $f -E -dM -x c /dev/null | grep '#define __FLT_EVAL_METHOD__ ')
	c=$("$CC_BIN" -m64 $f -E -dM -x c /dev/null | grep '#define __FLT_EVAL_METHOD__ ')
	if [ "$g" != "$c" ]; then
		echo "run_x64_no_sse_fp: -m64 $f: [$c], gcc's [$g]"
		exit 1
	fi
done

# gcc's errors, at gcc's places (indented with spaces, which both count alike)
printf 'double r(double a)\n{\n    return a;\n}\n' >ret.c
printf 'float fr(float a)\n{\n    return a;\n}\n' >fret.c
printf 'extern double g(int);\nint c(int x)\n{\n    int y = x;\n\n    return g(y) > 1;\n}\n' >callret.c
for f in ret.c fret.c callret.c; do
	for o in "-O0" "-O2" "-O2 -mno-80387"; do
		g=$(gcc -m64 $o -mno-sse -S -o /dev/null $f 2>&1 | grep -m1 'error:' | sed 's/ *\[.*//')
		c=$("$CC_BIN" -m64 $o -mno-sse -S -o /dev/null $f 2>&1 | grep -m1 'error:' || true)
		if [ -z "$g" ] || [ "$c" != "$g" ]; then
			echo "run_x64_no_sse_fp: $f $o: [$c], gcc's [$g]"
			exit 1
		fi
	done
done

# what gcc makes on the x87, cc refuses, rather than make SSE code
printf 'int p(int x, double a)\n{\n    return x + (a > 1);\n}\n' >arg.c
printf 'int q(int a)\n{\n    double x = a;\n\n    return (int)(x * 2.5);\n}\n' >internal.c
for t in "arg.c -mno-sse" "internal.c -mno-sse" "internal.c -mno-sse2"; do
	set -- $t
	gcc -m64 -O2 $2 -c -o g.o $1
	if "$CC_BIN" -m64 -O2 $2 -c -o c.o $1 2>c.err; then
		if objdump -d c.o | grep -q '%xmm'; then
			echo "run_x64_no_sse_fp: $t: SSE code made"
			exit 1
		fi
	elif ! grep -q "which cc makes with SSE" c.err; then
		echo "run_x64_no_sse_fp: $t: $(head -1 c.err)"
		exit 1
	fi
done

# a long double is the x87's, -mno-sse or not; integer code is as ever
cat >ld.c <<'EOF2'
#include <stdio.h>

__attribute__((noinline)) long double scale(long double a, int k) { return a * k + 0.5L; }
__attribute__((noinline)) long mix(long a, long b) { return a * 7 - b / 3; }
EOF2
cat >main.c <<'EOF2'
#include <stdio.h>

long double scale(long double a, int k);
long mix(long a, long b);

int main(void)
{
	printf("%.3Lf %ld\n", scale(2.25L, 3), mix(100, 33));
	return 0;
}
EOF2
gcc -m64 -O1 -o g ld.c main.c
want=$(./g)
for o in O0 O2; do
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		$run "$CC_BIN" -m64 -$o -mno-sse -c -o c.o ld.c
		if objdump -d c.o | grep -q '%xmm'; then
			echo "run_x64_no_sse_fp: -$o $path: %xmm in -mno-sse code"
			exit 1
		fi
		gcc -m64 -o c c.o main.c
		if [ "$(./c)" != "$want" ]; then
			echo "run_x64_no_sse_fp: -$o $path prints [$(./c)], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_x64_no_sse_fp: ok"
