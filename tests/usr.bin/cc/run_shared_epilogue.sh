#!/bin/sh
# A function with several returns and callee-saved registers to restore has
# one epilogue: the other returns jump to it, as gcc keeps it, where each
# had the restores and the ret of its own (Linux's and cc's own parsers
# return from a dozen places).  A function whose epilogue is `leave; ret`
# keeps its returns.  The program's output is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_shared_epilogue: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/e.c" <<'EOF2'
#include <stdio.h>

extern int step(int);

int __attribute__((noinline)) step(int k) {
    return k * 3 + 1;
}

/* values live across calls: callee-saved registers, and five ways out */
int __attribute__((noinline)) many(int a, int b, int c) {
    int x = step(a);
    int y = step(b);

    if (x > 100)
        return x - y;
    if (y > 100)
        return y + c;
    x = step(x + y);
    if (x & 1)
        return x * c;
    y = step(c);
    if (y == x)
        return 0;
    return x + y + a;
}

int main(void) {
    printf("%d %d %d %d %d\n", many(50, 1, 2), many(1, 50, 2), many(1, 2, 3), many(2, 2, 2), many(0, 0, 0));
    return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/e.s" "$CC_TMP/e.c"
	n=$(awk '/^many:/ { f = 1 } f && /^\.size many/ { exit } f && /\tret/ { r++ } END { print r + 0 }' "$CC_TMP/e.s")
	if [ "$n" -ne 1 ]; then
		echo "run_shared_epilogue: -m$m -O2 many() has $n returns, not one epilogue"
		exit 1
	fi
	gcc -m$m -O1 -o "$CC_TMP/eg" "$CC_TMP/e.c"
	want=$("$CC_TMP/eg")
	for o in O0 O2; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/ec" "$CC_TMP/e.c"
		got=$("$CC_TMP/ec")
		if [ "$got" != "$want" ]; then
			echo "run_shared_epilogue: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_shared_epilogue: ok"
