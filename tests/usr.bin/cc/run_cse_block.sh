#!/bin/sh
# Within a block, a pure computation made twice from the same operands is
# made once: the second is a copy of the first, which copy propagation then
# reads.  `s[v / 32] |= 1u << (v % 32)` divided three times (Linux's
# bitmap helpers do it in one statement); now once, as gcc does.  The
# addresses of two different globals are not one computation, nor are
# operands written between the two.  The program's output is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_cse_block: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/b.c" <<'EOF2'
void bit_set(unsigned int *s, int v) {
    s[v / 32] |= 1u << (v % 32);
}
EOF2
cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>

static int ga = 3;
static int gb = 4;
static unsigned int bits[4];

static void __attribute__((noinline)) bit_set(unsigned int *s, int v) {
    s[v / 32] |= 1u << (v % 32);
}

/* x is written between the two x + y: they are not the same */
static int __attribute__((noinline)) twice(int x, int y) {
    int a = x + y;

    x = x * 3;
    return a * 100 + (x + y);
}

int main(void) {
    int *p = &ga;
    int *q = &gb;

    bit_set(bits, 3);
    bit_set(bits, 40);
    bit_set(bits, 127);
    printf("%x %x %x %x\n", bits[0], bits[1], bits[2], bits[3]);
    printf("%d %d %d\n", *p, *q, twice(5, 7));
    return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/b.s" "$CC_TMP/b.c"
	n=$(grep -c 'idiv' "$CC_TMP/b.s" || true)
	if [ "$n" -ne 1 ]; then
		echo "run_cse_block: -m$m -O2 bit_set divides $n times, not once"
		exit 1
	fi
	gcc -m$m -O1 -o "$CC_TMP/pg" "$CC_TMP/p.c"
	want=$("$CC_TMP/pg")
	for o in O1 O2; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/pc" "$CC_TMP/p.c"
		got=$("$CC_TMP/pc")
		if [ "$got" != "$want" ]; then
			echo "run_cse_block: -m$m -$o prints"
			echo "$got"
			echo "where gcc's prints"
			echo "$want"
			exit 1
		fi
	done
done
echo "run_cse_block: ok"
