#!/bin/sh
# On x86-64 a division of two ints, or of two unsigned ints, is one idivl
# or divl of their 32-bit registers, as gcc's is: a 64-bit divide is
# several times slower, and an int computed in a 64-bit register needs
# no extension to be divided.  Dhrystone's `Int_2_Loc / Int_3_Loc` was
# two movslq and an idivq.  A quotient read as a long is sign-extended
# after the idivl; an int divided by a long, or an int by an unsigned
# (which converts it), is not a division of ints.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_int_div32: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/d.c" <<'EOF2'
#include <stdio.h>

#define NI __attribute__((noinline))

NI int q(int a, int b) { return a / b; }
NI int r(int a, int b) { return a % b; }
NI long wide(int a, int b) { return a / b; }
NI long by7(int a) { return a / -7; }
NI unsigned uq(unsigned a, unsigned b) { return a / b; }
NI unsigned ur(unsigned a, unsigned b) { return a % b; }
NI unsigned long uwide(unsigned a, unsigned b) { return a / b; }
NI long mixed(int a, unsigned b) { return a / b; }
NI long bylong(int a, long b) { return a / b; }
NI long longby(long a, int b) { return a / b; }
NI int sum(const int *v, int n, int d)
{
	int s = 0;
	int i;

	for (i = 0; i < n; i++)
		s += (v[i] + i) / d - v[i] % (d + 1);
	return s;
}

int main(void)
{
	static const int iv[] = {0, 1, -1, 7, -7, 100, -100, 0x7fffffff, -0x7fffffff - 1, 123456789, -987654321};
	static const unsigned uv[] = {1, 2, 3, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu, 1000000007u};
	unsigned long h = 0;
	size_t i, j;

	for (i = 0; i < sizeof iv / sizeof iv[0]; i++) {
		for (j = 0; j < sizeof iv / sizeof iv[0]; j++) {
			int a = iv[i], b = iv[j];

			if (b == 0 || (a == -0x7fffffff - 1 && b == -1))
				continue;
			h = h * 31 + (unsigned)q(a, b);
			h = h * 31 + (unsigned)r(a, b);
			h = h * 31 + (unsigned long)wide(a, b);
			h = h * 31 + (unsigned long)bylong(a, b);
			h = h * 31 + (unsigned long)longby((long)a * 3, b);
		}
		h = h * 31 + (unsigned long)by7(iv[i]);
		h = h * 31 + (unsigned long)mixed(iv[i], uv[i % 8]);
	}
	for (i = 0; i < sizeof uv / sizeof uv[0]; i++)
		for (j = 0; j < sizeof uv / sizeof uv[0]; j++) {
			h = h * 31 + uq(uv[i], uv[j]);
			h = h * 31 + ur(uv[i], uv[j]);
			h = h * 31 + uwide(uv[i], uv[j]);
		}
	printf("%lu %ld %ld %d\n", h, wide(-9, 2), bylong(-9, 0x100000000L), sum(iv, 11, 13));
	return 0;
}
EOF2
gcc -O1 -o "$CC_TMP/g" "$CC_TMP/d.c"
want=$("$CC_TMP/g")
for o in O0 O1 O2; do
	"$CC_BIN" -m64 -$o -o "$CC_TMP/c" "$CC_TMP/d.c"
	got=$("$CC_TMP/c")
	if [ "$got" != "$want" ]; then
		echo "run_int_div32: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
"$CC_BIN" -m64 -O2 -S -o "$CC_TMP/d.s" "$CC_TMP/d.c"
body() {
	awk -v f="$1" '$0 == f ":" {x=1} $0 ~ "^[ \t]*\\.size " f ", " {x=0} x' "$CC_TMP/d.s"
}
for f in q r uq ur uwide sum; do
	if ! body $f | grep -qE '^	i?divl '; then
		echo "run_int_div32: $f() divides its ints as longs"
		body $f
		exit 1
	fi
	if body $f | grep -B3 -E '^	(cltd|cqto|xorl %edx, %edx)$' | grep -qE 'movslq %|cqto'; then
		echo "run_int_div32: $f() extends an int only to divide it"
		body $f
		exit 1
	fi
done
for f in bylong longby; do
	if ! body $f | grep -qE '^	idivq '; then
		echo "run_int_div32: $f() divides a long as an int"
		body $f
		exit 1
	fi
done
echo "run_int_div32: ok"
