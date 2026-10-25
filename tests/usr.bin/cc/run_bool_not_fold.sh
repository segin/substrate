#!/bin/sh
# A compare's truth tested again -- `!!(x <= 16)` in unlikely(), `!(a == b)`
# -- is the compare itself, or the compare under the other condition: a
# compare and a branch, where each test was a setcc, a movzbl and a test of
# its own (Linux's kfree() tests ZERO_OR_NULL_PTR() so).  The program's
# output is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_bool_not_fold: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/k.c" <<'EOF2'
extern void slow(const void *);

void kf(const void *x) {
    if (__builtin_expect(!!((unsigned long)x <= 16), 0))
        return;
    slow(x);
}
EOF2
cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>

static int __attribute__((noinline)) f(int a, int b, unsigned u) {
    int r = 0;

    if (!(a == b))
        r += 1;
    if (!!(a < b))
        r += 2;
    if (!(u > 7u))
        r += 4;
    if (!!!(a >= 0))
        r += 8;
    return r + !(a <= b) * 16 + !!(u != 0) * 32;
}

int main(void) {
    printf("%d %d %d %d\n", f(1, 2, 3), f(2, 1, 9), f(-5, -5, 0), f(7, 7, 8));
    return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/k.s" "$CC_TMP/k.c"
	if awk '/^kf:/ { f = 1 } f && /^\.size kf/ { exit } f && /\tset/ { found = 1 } END { exit !found }' "$CC_TMP/k.s"; then
		echo "run_bool_not_fold: -m$m -O2: kf() makes a truth value it only branches on:"
		sed -n '/^kf:/,/^\.size kf/p' "$CC_TMP/k.s"
		exit 1
	fi
	gcc -m$m -O1 -o "$CC_TMP/pg" "$CC_TMP/p.c"
	want=$("$CC_TMP/pg")
	for o in O0 O1 O2; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/pc" "$CC_TMP/p.c"
		got=$("$CC_TMP/pc")
		if [ "$got" != "$want" ]; then
			echo "run_bool_not_fold: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_bool_not_fold: ok"
