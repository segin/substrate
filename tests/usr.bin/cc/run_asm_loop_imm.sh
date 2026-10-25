#!/bin/sh
# An asm input that may be an immediate ("Ir", "Nd") is one only if its
# value is a constant everywhere: one defined once.  A loop's variable is
# written before the loop and again at its end, and the write last before
# the asm in the text is the one before the loop -- `bt %1` of a loop's
# "Ir"(i & 7) was the `$0` of `i = 0`, and tested bit 0 every time round.
# Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_loop_imm: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/l.c" <<'EOF2'
#include <stdio.h>

static inline int bit(unsigned long w, long nr)
{
	_Bool c;

	asm("bt %2, %1" : "=@ccc"(c) : "r"(w), "Ir"(nr));
	return c;
}

int __attribute__((noinline)) bits(unsigned long w)
{
	int n = 0;
	long i;

	for (i = 0; i < 64; ++i)
		n = n * 3 + bit(w, i & 63) + (int)(i == 7);
	return n;
}

int main(void)
{
	printf("%d %d\n", bits(0xf0f0123456789abcul), bits(1ul << 63));
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	sed "s/unsigned long/unsigned long long/g; s/1ul << 63/1ull << 63/; s/ul)/ull)/" "$CC_TMP/l.c" >"$CC_TMP/l$m.c"
	[ "$m" = 64 ] && cp "$CC_TMP/l.c" "$CC_TMP/l$m.c"
	gcc -m$m -O1 -o "$CC_TMP/lg" "$CC_TMP/l$m.c" 2>/dev/null || continue
	want=$("$CC_TMP/lg")
	for o in O0 O2; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/lc" "$CC_TMP/l$m.c"
		got=$("$CC_TMP/lc")
		if [ "$got" != "$want" ]; then
			echo "run_asm_loop_imm: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_asm_loop_imm: ok"
