#!/bin/sh
# A multiply by a power of two is a shift where that is no larger: from
# 128 on, imul's constant takes 32 bits where shl's takes 8 -- Linux's
# `index * PAGE_SIZE`, imul $4096 -- and below it where the product goes
# back into the register multiplied.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_mul_pow2_shift: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>

long __attribute__((noinline)) m1(long x) { return x * 4096; }
unsigned __attribute__((noinline)) m3(unsigned x) { return x * 0x80000000u; }
long long __attribute__((noinline)) m4(long long x) { return x * 1024; }
int __attribute__((noinline)) m5(int x) { return x * 256 + 1; }

int main(void)
{
	printf("%ld %u %lld %d\n", m1(3), m3(3), m4(-7), m5(-2));
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/m.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/m.c"
		got=$("$CC_TMP/c") || { echo "run_mul_pow2_shift: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_mul_pow2_shift: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/m.s" "$CC_TMP/m.c"
	for fn in m1 m5; do
		if awk -v f="$fn" '$0 == f ":" {x=1} $0 ~ "^\\.size " f "," {x=0} x' "$CC_TMP/m.s" | grep -q imul; then
			echo "run_mul_pow2_shift: -m$m: $fn() multiplies by a power of two"
			exit 1
		fi
	done
done
echo "run_mul_pow2_shift: ok"
