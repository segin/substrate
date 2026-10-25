#!/bin/sh
# A branch both of whose ways go to one place, or to two returns of the
# same value, is a jump: Linux's fix_spurious_fault() calls one of two
# flushes that are empty on i386 and x86-64, and kept `cmpl $1, level`
# and a branch to the label after it, then the test of the flag it was
# under, a load and a branch to a second `ret`.  With the branch folded
# the compare goes, the block it was in is empty, and the branch before
# it folds in turn: fsf() is the frame and a return.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_empty_arms_fold: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/f.c" <<'EOF2'
#include <stdio.h>

struct vmf { int pad[5]; unsigned flags; };
static int flushes;
static inline void flush_a(struct vmf *v) { (void)v; }
static inline void flush_b(struct vmf *v) { (void)v; }

void __attribute__((noinline)) fsf(struct vmf *v, int level)
{
	if (v->flags & 32)
		return;
	if (v->flags & 1) {
		if (level == 1)
			flush_a(v);
		else
			flush_b(v);
	}
}

int __attribute__((noinline)) pick(int a, int b)
{
	if (a > b) {
		if (b == 3)
			flushes++;
		else
			flushes += 2;
	}
	return flushes;
}

int main(void)
{
	struct vmf v = {{0}, 33};
	int a, b, c;

	fsf(&v, 1);
	v.flags = 1;
	fsf(&v, 2);
	a = pick(5, 3);
	b = pick(5, 4);
	c = pick(1, 2);
	printf("%d %d %d\n", a, b, c);
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/f.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/f.c"
		got=$("$CC_TMP/c") || { echo "run_empty_arms_fold: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_empty_arms_fold: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/f.s" "$CC_TMP/f.c"
	if awk '/^fsf:/{x=1} /^\.size fsf,/{x=0} x' "$CC_TMP/f.s" | grep -qE '^	(j|cmp|test|mov[a-z]* [0-9]*\()'; then
		echo "run_empty_arms_fold: -m$m: fsf() tests what it does nothing with"
		awk '/^fsf:/{x=1} /^\.size fsf,/{x=0} x' "$CC_TMP/f.s"
		exit 1
	fi
done
echo "run_empty_arms_fold: ok"
