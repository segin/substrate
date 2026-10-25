#!/bin/sh
# A local of a word or less -- a one-word structure among them -- is a
# value even where a memset zeroes it whole (-ftrivial-auto-var-init=zero,
# which Linux builds with, zeroes every local so; a structure's
# initializer does too) or a memcpy copies it whole (structure assignment
# on i386): the memset is the value 0, the copy a copy, a load or a store.
# Linux's pte_t helpers kept every pte in the frame, stored and read back
# at each assignment.  Results are gcc's; the function keeps nothing in
# its frame.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_promote_struct_word: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>

typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned short a, b; } pair_t;

static inline pte_t mk(unsigned long v) { pte_t r = { v }; return r; }
static inline pte_t setb(pte_t p, unsigned long b) { pte_t r; r.pte = p.pte | b; return r; }

unsigned long __attribute__((noinline)) f(unsigned long x, int n)
{
	pte_t p = mk(x), q;
	int i;

	for (i = 0; i < n; ++i) {
		q = setb(p, 1ul << (i & 7));
		if (q.pte & 32)
			p = setb(q, 64);
		else
			p = q;
	}
	return p.pte;
}

unsigned __attribute__((noinline)) g(unsigned short x, pair_t *out)
{
	pair_t a = { x, (unsigned short)(x * 3) }, b;

	b = a;
	b.a ^= 0x5a5a;
	*out = b;
	return (unsigned)a.a + b.b;
}

int main(void)
{
	pair_t o;
	unsigned r = g(0x1234, &o);

	printf("%lx %lx %u %u %u\n", f(0x1000, 9), f(3, 40), r, o.a, o.b);
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/p.c"
	want=$("$CC_TMP/g")
	for init in "" -ftrivial-auto-var-init=zero; do
		for o in O1 O2 Os; do
			"$CC_BIN" -m$m -$o $init -o "$CC_TMP/c" "$CC_TMP/p.c"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_promote_struct_word: -m$m -$o $init prints [$got], gcc's [$want]"
				exit 1
			fi
		done
		"$CC_BIN" -m$m -Os $init -S -o "$CC_TMP/p.s" "$CC_TMP/p.c"
		if awk '/^f:/{x=1} /^\.size f,/{x=0} x' "$CC_TMP/p.s" | grep -qE -- '-[0-9]+\(%[er]bp\)'; then
			echo "run_promote_struct_word: -m$m $init: f() keeps a pte in its frame"
			awk '/^f:/{x=1} /^\.size f,/{x=0} x' "$CC_TMP/p.s" | grep -E -- '-[0-9]+\(%[er]bp\)' | head -5
			exit 1
		fi
	done
done
echo "run_promote_struct_word: ok"
