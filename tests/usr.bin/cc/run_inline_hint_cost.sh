#!/bin/sh
# `inline` is a hint: a function so declared, but not always_inline, and
# larger than a call of it costs is not copied into each of several
# callers -- cc did that for any of up to 160 instructions, which made
# Linux's maple_tree.o 1.66 times gcc's.  One called from one place, an
# always_inline one, and one a call's constant arguments fold down to
# about a call's size (pick(0, ...) is an add; on i386 run_i386_wide_ops
# has rol64() by a constant shld) are still inlined.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_inline_hint_cost: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/h.c" <<'EOF2'
#include <stdio.h>

struct node { int slot[8]; int pivot[8]; struct node *parent; };

static inline int walk(const struct node *n, int key)
{
	int i, best, up = 0;

	for (;;) {
		best = -1;
		for (i = 0; i < 8; i++) {
			if (n->pivot[i] >= key && (best < 0 || n->pivot[i] < n->pivot[best]))
				best = i;
		}
		if (best >= 0 || n->parent == NULL)
			break;
		n = n->parent;
		up += 100;
	}
	return best < 0 ? -1 : n->slot[best] * 3 + (key & 7) + up;
}

static inline int pick(int mode, const struct node *n, int key)
{
	int i, s = 0;

	if (mode == 0)
		return key + 1;
	for (i = 0; i < 8; i++)
		s += mode == 1 ? n->slot[i] * key : n->pivot[i] - key;
	return s;
}

static inline __attribute__((always_inline)) int forced(const struct node *n, int key)
{
	int i, s = 0;

	for (i = 0; i < 8; i++)
		s += n->pivot[i] > key ? n->slot[i] : -n->slot[i];
	return s ^ key;
}

static inline int single(const struct node *n, int key)
{
	int i, s = 0;

	for (i = 0; i < 8; i++)
		s += (n->slot[i] * key) % 13 - n->pivot[i];
	return s;
}

int __attribute__((noinline)) one(const struct node *n, int k) { return walk(n, k) + forced(n, k) + pick(0, n, k); }
int __attribute__((noinline)) two(const struct node *n, int k) { return walk(n, k * 2) - forced(n, k + 1) + pick(0, n, k); }
int __attribute__((noinline)) three(const struct node *n, int k) { return walk(n, k + 9) ^ single(n, k) ^ pick(2, n, k); }
int __attribute__((noinline)) four(const struct node *n, int k) { return pick(1, n, k) - pick(k & 1, n, 3); }

int main(void)
{
	struct node p = { { 1, 2, 3, 4, 5, 6, 7, 8 }, { 100, 200, 300, 400, 500, 600, 700, 800 }, NULL };
	struct node c = { { 9, 8, 7, 6, 5, 4, 3, 2 }, { 10, 20, 30, 40, 50, 60, 70, 80 }, &p };
	int a = one(&c, 25), b = two(&c, 45), d = three(&c, 77), e = four(&c, 5);

	printf("%d %d %d %d\n", a, b, d, e);
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/h.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/h.c"
		got=$("$CC_TMP/c") || { echo "run_inline_hint_cost: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_inline_hint_cost: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/h.s" "$CC_TMP/h.c"
	for f in one two three; do
		if ! awk -v f="$f" '$0 == f ":" {x=1} $0 ~ "^\\.size " f "," {x=0} x' "$CC_TMP/h.s" | grep -qE '^	(call|jmp)[lq]?	?[ ]*walk$'; then
			echo "run_inline_hint_cost: -m$m: $f() does not call walk(), inlined into three callers"
			exit 1
		fi
	done
	if grep -qE '^(forced|single):' "$CC_TMP/h.s"; then
		echo "run_inline_hint_cost: -m$m: the always_inline forced() or the once-called single() is out of line"
		exit 1
	fi
	for f in one two; do
		if awk -v f="$f" '$0 == f ":" {x=1} $0 ~ "^\\.size " f "," {x=0} x' "$CC_TMP/h.s" | grep -qE 'pick'; then
			echo "run_inline_hint_cost: -m$m: $f() calls pick(0, ...), which its constant folds to an add"
			exit 1
		fi
	done
	if ! awk '$0 == "four:" {x=1} /^\.size four,/ {x=0} x' "$CC_TMP/h.s" | grep -qE 'pick'; then
		echo "run_inline_hint_cost: -m$m: four() inlines pick() for a mode it does not know"
		exit 1
	fi
done
echo "run_inline_hint_cost: ok"
