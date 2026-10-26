#!/bin/sh
# A product of two-limb integers on x86-64 -- __int128, unsigned
# __int128, _BitInt(65) to _BitInt(128) -- is made inline, as gcc makes
# it: mulq for the high word of the low limbs' product, imulq for the
# cross products.  cc called libgcc's __mulbitint3, which Linux's vDSO
# does not link: with CC_HAS_INT128, mul_u64_u32_shr() in
# vclock_gettime.c left `__mulbitint3` undefined and the x86-64 kernel
# did not link.  A wider _BitInt still multiplies through libgcc.
# Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_int128_mul_inline: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>

typedef unsigned long long u64;
typedef unsigned __int128 u128;
typedef __int128 s128;

u64 __attribute__((noinline)) mul_u64_u32_shr(u64 a, unsigned int mul, unsigned int shift)
{
	return (u64)(((u128)a * mul) >> shift);
}
u128 __attribute__((noinline)) umul(u128 a, u128 b) { return a * b; }
s128 __attribute__((noinline)) smul(s128 a, s128 b) { return a * b; }
_BitInt(100) __attribute__((noinline)) bmul(_BitInt(100) a, _BitInt(100) b) { return a * b; }
unsigned _BitInt(128) __attribute__((noinline)) ubmul(unsigned _BitInt(128) a, unsigned _BitInt(128) b) { return a * b; }
_BitInt(200) __attribute__((noinline)) wmul(_BitInt(200) a, _BitInt(200) b) { return a * b; }

static void put(u128 v) { printf(" %016llx%016llx", (u64)(v >> 64), (u64)v); }

int main(void)
{
	u64 v[] = { 0, 1, 3, 0xffffffffULL, 0x100000000ULL, 0xdeadbeefcafebabeULL, ~0ULL };
	int i, j;

	for (i = 0; i < 7; i++) {
		for (j = 0; j < 7; j++) {
			u128 a = ((u128)v[i] << 64) | v[j], b = ((u128)v[j] << 64) | v[i];
			_BitInt(200) wa = (_BitInt(200))(s128)a, wb = (_BitInt(200))(s128)b;
			_BitInt(200) wp = wmul(wa, wb);

			printf("%llx", (u64)mul_u64_u32_shr(v[i], (unsigned int)v[j], (unsigned int)(i * 9 + j)));
			put(umul(a, b));
			put((u128)smul((s128)a, -(s128)b));
			put((u128)(s128)bmul((_BitInt(100))(s128)a, (_BitInt(100))(s128)b));
			put((u128)ubmul((unsigned _BitInt(128))a, (unsigned _BitInt(128))b));
			put((u128)(s128)wp);
			put((u128)(s128)(wp >> 128));
			printf("\n");
		}
	}
	return 0;
}
EOF2
gcc -O1 -std=gnu23 -o "$CC_TMP/g" "$CC_TMP/m.c"
"$CC_TMP/g" >"$CC_TMP/want"
for o in O0 O1 O2 Os; do
	"$CC_BIN" -$o -std=gnu23 -o "$CC_TMP/c" "$CC_TMP/m.c"
	"$CC_TMP/c" >"$CC_TMP/got" || { echo "run_int128_mul_inline: -$o: the program fails"; exit 1; }
	if ! cmp -s "$CC_TMP/got" "$CC_TMP/want"; then
		echo "run_int128_mul_inline: -$o: the products differ from gcc's:"
		diff "$CC_TMP/got" "$CC_TMP/want" | head -6
		exit 1
	fi
	"$CC_BIN" -$o -std=gnu23 -S -o "$CC_TMP/c.s" "$CC_TMP/m.c"
	for f in mul_u64_u32_shr umul smul bmul ubmul; do
		if awk -v f="$f" '$0 == f ":" {x=1} $0 ~ "^\\.size " f "," {x=0} x' "$CC_TMP/c.s" | grep -q "__mulbitint3"; then
			echo "run_int128_mul_inline: -$o: $f() calls __mulbitint3"
			exit 1
		fi
	done
done
echo "run_int128_mul_inline: ok"
