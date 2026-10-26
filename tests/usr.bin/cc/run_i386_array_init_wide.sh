#!/bin/sh
# An element of a local array initializer that is a constant, folded to
# an int, is stored as the element's type -- on i386 a long long's two
# words, its high word the int's sign or zero extension.  It was stored
# as the int it folded to, 8 bytes of a 4-byte value, which the machine-IR
# selector cannot store: every i386 function with `uint64_t z[2] = {0, 0}`
# (cc's own parse_primary() among them) fell back to the old emitter.
# Results are gcc's; on i386 no function may fall back.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_i386_array_init_wide: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/a.c" <<'EOF2'
#include <stdio.h>
#include <stdint.h>

void __attribute__((noinline)) show(const uint64_t *z, const int64_t *s, const long long *t, int n)
{
	printf("%llx %llx %lld %lld", (unsigned long long)z[0], (unsigned long long)z[1], (long long)s[0],
	       (long long)s[1]);
	while (n-- > 0)
		printf(" %lld", t[n]);
	printf("\n");
}

int __attribute__((noinline)) run(int k)
{
	uint64_t z[2] = { -1, 5 };
	int64_t s[2] = { k, -7 };
	long long t[4] = { 0, -1, 0x7fffffff, k * 3 };

	show(z, s, t, 4);
	return (int)(z[0] >> 60) + (int)s[1];
}

int main(void)
{
	printf("%d\n", run(-3));
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/a.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		CC_MIR_STATS=1 "$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/a.c" 2>"$CC_TMP/stats"
		if grep -q "not taken" "$CC_TMP/stats"; then
			echo "run_i386_array_init_wide: -m$m -$o: $(grep -m1 'not taken' "$CC_TMP/stats")"
			exit 1
		fi
		got=$("$CC_TMP/c") || { echo "run_i386_array_init_wide: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_i386_array_init_wide: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_i386_array_init_wide: ok"
