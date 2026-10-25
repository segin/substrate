#!/bin/sh
# An instruction that defines no value -- a store, a label, a branch, a
# return -- has no dst.  Lowering built some stores from a zeroed
# instruction, their dst 0, which named %v0: a second definition of it,
# and its signedness and size overwritten.  A long double negated by
# flipping its sign bit in memory did so, and on i386 a string literal
# that was %v0 lost the definition the register allocator reads it from:
# printf was passed an unset register.  The program's output is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_store_no_dst: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/n.c" <<'EOF2'
#include <stdio.h>

static int __attribute__((noinline)) f(double a, long double b, int k) {
    return (int)a * 1000 + (int)b + k;
}

int main(void) {
    printf("%d\n", f(42.75, -7.0L, 3));
    printf("%s %.3Lf\n", "neg", -(long double)f(1.5, -2.0L, 1));
    return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/ng" "$CC_TMP/n.c"
	want=$("$CC_TMP/ng")
	for o in O0 O2; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/nc" "$CC_TMP/n.c"
		got=$("$CC_TMP/nc")
		if [ "$got" != "$want" ]; then
			echo "run_store_no_dst: -m$m -$o prints"
			echo "$got"
			echo "where gcc's prints"
			echo "$want"
			exit 1
		fi
	done
done
echo "run_store_no_dst: ok"
