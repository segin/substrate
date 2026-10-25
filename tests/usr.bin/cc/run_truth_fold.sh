#!/bin/sh
# A value that is only ever a 0 or a 1 -- an asm's flag output ("=@ccc"),
# a bool a variable keeps, a constant 0 or 1, a compare -- tested again
# (`x != 0`) or extended is itself.  Linux's test_bit() is
# `!!arch_test_bit()`, of a flag output: it was set, extended, tested and
# set again twice before its branch.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_truth_fold: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/t.c" <<'EOF2'
#include <stdio.h>

static inline __attribute__((always_inline)) _Bool arch_test_bit(long nr, const unsigned long *addr)
{
	_Bool old;

	asm volatile("btq %2,%1" : "=@ccc"(old) : "m"(*addr), "Ir"(nr) : "memory");
	return old;
}
#define test_bit(nr, a) (!!arch_test_bit(nr, a))

int __attribute__((noinline)) count(const unsigned long *w, int n)
{
	int c = 0;
	int i;
	_Bool seen = 0;

	for (i = 0; i < n; ++i) {
		if (test_bit(i & 63, &w[i / 64]) && i != 5)
			c++;
		seen = seen || (w[i / 64] >> (i & 63) & 1);
		if (!!seen == 1 && (i & 1))
			c += 2;
	}
	return c + (seen != 0);
}

int main(void)
{
	unsigned long w[2] = {0x5a5a00f0f0f0f0f0ul, 0x8000000000000001ul};

	printf("%d %d\n", count(w, 128), count(w + 1, 64));
	return 0;
}
EOF2
gcc -O1 -o "$CC_TMP/tg" "$CC_TMP/t.c"
want=$("$CC_TMP/tg")
for o in O1 O2; do
	"$CC_BIN" -m64 -$o -o "$CC_TMP/tc" "$CC_TMP/t.c"
	got=$("$CC_TMP/tc")
	if [ "$got" != "$want" ]; then
		echo "run_truth_fold: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
# the flag output is tested once, by the branch: no setne after the asm's setc
"$CC_BIN" -m64 -O2 -S -o "$CC_TMP/t.s" "$CC_TMP/t.c"
if awk '/setc/{f=1; next} f && /^\tj/{f=0} f && /setne/{bad=1} END{exit !bad}' "$CC_TMP/t.s"; then
	echo "run_truth_fold: the flag output is tested again before its branch"
	exit 1
fi
echo "run_truth_fold: ok"
