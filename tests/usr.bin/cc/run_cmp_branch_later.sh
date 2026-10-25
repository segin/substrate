#!/bin/sh
# A compare that only a branch later in its block reads -- other work
# between, none of it writing the compare's operands -- is made at the
# branch, which is on its flags: no truth value made in a register and
# tested.  Only a branch right after the compare was so before.  A compare
# whose operand changes in between keeps its value.  The program's output
# is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_cmp_branch_later: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/c.c" <<'EOF2'
extern int g;

int later(int a, int b, int *p) {
    int t = a < b;

    *p = a * 3 + b;
    g = b - a;
    if (t)
        return 1;
    return 2;
}
EOF2
cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>

int g;

static int __attribute__((noinline)) later(int a, int b, int *p) {
    int t = a < b;

    *p = a * 3 + b;
    g = b - a;
    if (t)
        return 1;
    return 2;
}

/* a changes between the compare and the branch */
static int __attribute__((noinline)) changed(int a, int b) {
    int t = a < b;

    a = a + 100;
    if (t)
        return a;
    return -a;
}

int main(void) {
    int x;
    int r = later(1, 5, &x);
    int s = later(5, 1, &x);

    printf("%d %d %d %d %d %d\n", r, s, x, g, changed(1, 2), changed(3, 2));
    return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/c.s" "$CC_TMP/c.c"
	if awk '/^later:/ { f = 1 } f && /^\.size later/ { exit } f && /\tset/ { found = 1 } END { exit !found }' "$CC_TMP/c.s"; then
		echo "run_cmp_branch_later: -m$m -O2: later() makes a truth value it only branches on:"
		sed -n '/^later:/,/^\.size later/p' "$CC_TMP/c.s"
		exit 1
	fi
	gcc -m$m -O1 -o "$CC_TMP/pg" "$CC_TMP/p.c"
	want=$("$CC_TMP/pg")
	for o in O0 O2; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/pc" "$CC_TMP/p.c"
		got=$("$CC_TMP/pc")
		if [ "$got" != "$want" ]; then
			echo "run_cmp_branch_later: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_cmp_branch_later: ok"
