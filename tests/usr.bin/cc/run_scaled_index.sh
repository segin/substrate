#!/bin/sh
# An address of a base plus an index scaled by 1, 2, 4 or 8 -- a multiply
# by one of them, or a shift left by up to 3, of a value written once --
# is one memory operand, (base, index, scale), as an array's element is.
# a[i] was an imul and a load through (base, product, 1).  Results are
# gcc's, of arrays of each element size.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_scaled_index: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/s.c" <<'EOF2'
#include <stdio.h>

long __attribute__((noinline)) sum8(const long *a, long n)
{
	long s = 0;
	long i;

	for (i = 0; i < n; ++i)
		s += a[i] * (i + 1);
	return s;
}

int __attribute__((noinline)) mix(const int *a, const short *b, const unsigned char *c, long n)
{
	int s = 0;
	long i;

	for (i = 0; i < n; ++i)
		s = s * 31 + a[i] + b[i] + c[i] + a[n - 1 - i];
	return s;
}

int main(void)
{
	long a[10];
	int b[10];
	short h[10];
	unsigned char c[10];
	int i;

	for (i = 0; i < 10; ++i) {
		a[i] = i * 1000003L - 7;
		b[i] = i * 17 - 50;
		h[i] = (short)(i * -301);
		c[i] = (unsigned char)(i * 29);
	}
	printf("%ld %d\n", sum8(a, 10), mix(b, h, c, 10));
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/s.c"
	want=$("$CC_TMP/g")
	for o in O1 O2; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/s.c"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_scaled_index: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
"$CC_BIN" -m64 -O2 -S -o "$CC_TMP/s.s" "$CC_TMP/s.c"
if ! awk '/^sum8:/{f=1} /^\.size sum8/{f=0} f' "$CC_TMP/s.s" | grep -qE '\(%[a-z0-9]+,%[a-z0-9]+,8\)'; then
	echo "run_scaled_index: sum8()'s a[i] is not one scaled operand"
	awk '/^sum8:/{f=1} /^\.size sum8/{f=0} f' "$CC_TMP/s.s"
	exit 1
fi
if awk '/^sum8:/{f=1} /^\.size sum8/{f=0} f' "$CC_TMP/s.s" | grep -qE 'imul[q]? +\$8'; then
	echo "run_scaled_index: sum8() multiplies its index by 8"
	exit 1
fi
echo "run_scaled_index: ok"
