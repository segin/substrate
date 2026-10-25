#!/bin/sh
# i386 two-word arithmetic as gcc writes it: a 64-bit add or subtract is
# an add (sub) and an adc (sbb) through the carry flag, and a shift or
# rotate of one by a constant a shld or shrd per word.  Legalization
# writes the carry as an unsigned compare of the low words, and the
# selector made it a cmp, a setb, a movzbl and a third add; each word of
# a shift was two shifts and an or.  SipHash, which Linux hashes with,
# came out at twice gcc's size.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 ||
	! printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	echo "run_i386_wide_ops: skip (no gcc -m32)"
	exit 0
fi

cat >"$CC_TMP/w.c" <<'EOF2'
#include <stdio.h>

typedef unsigned long long u64;
typedef long long s64;

static inline u64 rol64(u64 w, unsigned s) { return (w << s) | (w >> (64 - s)); }

#define SIPROUND(v0, v1, v2, v3) do { \
	v0 += v1; v1 = rol64(v1, 13); v1 ^= v0; v0 = rol64(v0, 32); \
	v2 += v3; v3 = rol64(v3, 16); v3 ^= v2; \
	v0 += v3; v3 = rol64(v3, 21); v3 ^= v0; \
	v2 += v1; v1 = rol64(v1, 17); v1 ^= v2; v2 = rol64(v2, 32); \
} while (0)

u64 __attribute__((noinline)) sip(const u64 *m, int n, u64 k0, u64 k1)
{
	u64 v0 = 0x736f6d6570736575ULL ^ k0, v1 = 0x646f72616e646f6dULL ^ k1;
	u64 v2 = 0x6c7967656e657261ULL ^ k0, v3 = 0x7465646279746573ULL ^ k1;
	int i;

	for (i = 0; i < n; ++i) {
		v3 ^= m[i];
		SIPROUND(v0, v1, v2, v3);
		SIPROUND(v0, v1, v2, v3);
		v0 ^= m[i];
	}
	v2 ^= 0xff;
	SIPROUND(v0, v1, v2, v3);
	SIPROUND(v0, v1, v2, v3);
	return (v0 ^ v1) ^ (v2 ^ v3);
}

s64 __attribute__((noinline)) mix(s64 a, s64 b, unsigned c)
{
	s64 r = a - b;

	r -= (s64)c;
	r += a >> 7;
	r ^= (u64)b << 41;
	r -= (s64)((u64)a >> 3);
	r += b << 5;
	return r + (s64)((u64)r >> 33) - (a >> 40);
}

/* only the high word of the sum read: the low word's add stays for its carry */
s64 __attribute__((noinline)) q32(s64 s) { return s / 0x100000000LL; }

int main(void)
{
	u64 m[5] = {1, 0xffffffffffffffffULL, 0x8000000000000000ULL, 0x00000000ffffffffULL, 0x123456789abcdef0ULL};

	printf("%016llx\n", sip(m, 5, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL));
	printf("%lld %lld\n", mix(-5, 0x7fffffff, 0xffffffffu), mix(0x123456789LL, -0x7fffffffffffLL, 3));
	printf("%lld %lld %lld\n", q32(-1), q32(-0x100000000LL), q32(0x1ffffffffLL));
	return 0;
}
EOF2
gcc -m32 -O1 -o "$CC_TMP/g" "$CC_TMP/w.c"
want=$("$CC_TMP/g")
for o in O1 O2 Os; do
	"$CC_BIN" -m32 -$o -o "$CC_TMP/c" "$CC_TMP/w.c"
	got=$("$CC_TMP/c")
	if [ "$got" != "$want" ]; then
		echo "run_i386_wide_ops: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
"$CC_BIN" -m32 -O2 -S -o "$CC_TMP/w.s" "$CC_TMP/w.c"
awk '/^sip:/{f=1} /^\.size sip/{f=0} f' "$CC_TMP/w.s" >"$CC_TMP/sip.s"
for want in adcl shldl; do
	if ! grep -q "$want" "$CC_TMP/sip.s"; then
		echo "run_i386_wide_ops: sip() has no $want"
		exit 1
	fi
done
if grep -q setb "$CC_TMP/sip.s"; then
	echo "run_i386_wide_ops: sip() makes a carry with setb"
	exit 1
fi
echo "run_i386_wide_ops: ok"
