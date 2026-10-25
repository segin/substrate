#!/bin/sh
# An add of an index scaled by 2, 4 or 8 that nothing else reads is one
# lea: Linux's pte_offset(), `tab + index` kept rather than loaded
# through, was imul $4 (shl $3) and an add.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_scaled_add_lea: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/e.c" <<'EOF2'
#include <stdio.h>

unsigned long *__attribute__((noinline)) ent(unsigned long *tab, unsigned long addr)
{
	return tab + ((addr >> 22) & 0x3ff);
}

short *__attribute__((noinline)) half(short *b, unsigned i) { return b + (i & 63); }

int main(void)
{
	static unsigned long tab[1024];
	static short h[64];

	printf("%ld %ld\n", (long)(ent(tab, 0xc0400000) - tab), (long)(half(h, 70) - h));
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/e.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/e.c"
		got=$("$CC_TMP/c") || { echo "run_scaled_add_lea: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_scaled_add_lea: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/e.s" "$CC_TMP/e.c"
	for fn in ent half; do
		if awk -v f="$fn" '$0 == f ":" {x=1} $0 ~ "^\\.size " f "," {x=0} x' "$CC_TMP/e.s" | grep -qE 'imul|^	sh[lr][lq] \$[123],'; then
			echo "run_scaled_add_lea: -m$m: $fn() scales its index apart from the add"
			awk -v f="$fn" '$0 == f ":" {x=1} $0 ~ "^\\.size " f "," {x=0} x' "$CC_TMP/e.s"
			exit 1
		fi
	done
done
echo "run_scaled_add_lea: ok"
