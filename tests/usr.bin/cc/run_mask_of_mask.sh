#!/bin/sh
# A mask of a mask is one and, of both: Linux's pte_flags(pte) & _PAGE_RW
# is (pte & 0xfff) & 2, through the copy an inlined function's result is,
# and every test of a page-table bit was two ands.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_mask_of_mask: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>

typedef struct { unsigned long pte; } pte_t;

static inline unsigned long pte_flags(pte_t p) { return p.pte & 0xfff; }

int __attribute__((noinline)) f(pte_t p, int k)
{
	return (pte_flags(p) & 0x40) ? k + 1 : (int)(pte_flags(p) & 0x22);
}

unsigned __attribute__((noinline)) g(unsigned x)
{
	unsigned y = x & 0xf0f0;

	return (y & 0x3c) + (y & 0xff00) + y;	/* y is read again: its mask stays */
}

int main(void)
{
	pte_t a = {0x12345fff}, b = {0x1000};

	printf("%d %d %u %u\n", f(a, 3), f(b, 3), g(0xffffffffu), g(0x1234));
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
	for o in O1 O2; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/m.c"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_mask_of_mask: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/m.s" "$CC_TMP/m.c"
	if awk '/^f:/{x=1} /^\.size f,/{x=0} x' "$CC_TMP/m.s" | grep -qE 'and[lq] +\$4095'; then
		echo "run_mask_of_mask: -m$m: f() masks with 0xfff before its bit"
		exit 1
	fi
done
echo "run_mask_of_mask: ok"
