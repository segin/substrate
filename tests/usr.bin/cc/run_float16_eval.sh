#!/bin/sh
# _Float16 arithmetic is evaluated as gcc evaluates it: in float where no
# instruction does half arithmetic (x86-64), in long double on the i386
# x87 (FLT_EVAL_METHOD 2), the value rounded to a half only where it
# takes the type -- assigned, cast, passed, returned -- and from a long
# double by __truncxfhf2, once.  cc evaluated each operation in double,
# so a chain such as `a * b + c` could round differently.  Many random
# halves through chains of + - * / and a compare give gcc's results: on
# x86-64 in gnu17 and c11, on i386 in c11 (gcc's GNU modes keep x87 excess
# precision where its spills leave it), -O0 and -O2, both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_float16_eval: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

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
# check MODE STD CCFLAGS: cc's result is gcc's
check() {
	gcc -m$1 -std=$2 -O2 -o "$CC_TMP/g" "$CC_TMP/ar.c"
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O2; do
			# shellcheck disable=SC2086
			$run "$CC_BIN" -m$1 $3 -std=$2 -$o -c -o "$CC_TMP/ar.o" "$CC_TMP/ar.c"
			gcc -m$1 -no-pie -o "$CC_TMP/c" "$CC_TMP/ar.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_float16_eval: -m$1 -std=$2 $path -$o: $got, gcc's $want"
				exit 1
			fi
		done
	done
}
check 64 gnu17 ""
check 64 c11 ""
printf 'int main(void){return 0;}\n' >"$CC_TMP/m32.c"
if gcc -m32 -o "$CC_TMP/m32" "$CC_TMP/m32.c" 2>/dev/null; then
	check 32 c11 "-march=pentium4"
fi
echo "run_float16_eval: ok"
