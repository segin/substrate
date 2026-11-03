#!/bin/sh
# Range folding through a shift whose count is a constant value rather
# than the instruction's own count -- lowering writes `x >> 2` as
# `lshr x, %k` of a constant %k -- and through i386's words of a 64-bit
# value (lo32, hi32, pair), which a `(long long)x` cast makes there.
# Linux's FIELD_GET() is `(mask & reg) >> shift`, and ad7768-1.c then
# asks clamp() of the field, whose BUILD_BUG_ON tests
# __builtin_constant_p((long long)val >= 0): with the shift's range not
# known, iio's ad7768-1 kept a call of an undefined
# __compiletime_assert_N.  What gcc folds leaves no reference; a shift
# by a count not known keeps its test, as gcc keeps it.  -O2, -m64 and
# -m32, both code paths, every function through MIR; and the program,
# run, prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_range_shift_count: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
extern void sel_bad(void), wide_bad(void), left_bad(void), count_kept(void);
#define NONNEG(x) (__builtin_constant_p((long long)(x) >= 0) && ((long long)(x) >= 0))

int sel(unsigned int x, int n)
{
	int v = (int)((x & 0x1c) >> 2);

	if (!NONNEG(v))
		sel_bad();
	return v < 1 ? 1 : v > n ? n : v;
}

long long wide(unsigned long long x)
{
	long long v = (long long)((x & 0xff00ULL) >> 8);

	if (!NONNEG(v) || v > 255)
		wide_bad();
	return v;
}

int left(const unsigned char *c)
{
	int v = *c << 3;

	if (v < 0 || v > 255 * 8)
		left_bad();
	return v;
}

/* by a count not known */
int count(unsigned int x, unsigned int k)
{
	int v = (int)(x >> (k & 7));

	if (!NONNEG(v))
		count_kept();
	return v;
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads, kepts;
void sel_bad(void) { bads++; }
void wide_bad(void) { bads++; }
void left_bad(void) { bads++; }
void count_kept(void) { kepts++; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	int a = sel(0x1f, 3) + sel(0x04, 9);
	long long b = wide(0x12345678ULL);
	static const unsigned char two_hundred = 200;
	int c = left(&two_hundred);
	int d = count(0x80000000u, 0);

	printf("%d %lld %d %d bads %d kepts %d\n", a, b, c, d, bads, kepts);
	return 0;
}
EOF2
cat "$CC_TMP/defs.c" "$CC_TMP/u.c" "$CC_TMP/main.c" >"$CC_TMP/p.c"

undefined() {
	nm -u "$1" | awk '{ print $NF }' | grep -E '_(bad|kept)$' | sort || true
}

fail=0
for m in 64 32; do
	gcc -m$m -O2 -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c" 2>/dev/null || continue
	want=$(undefined "$CC_TMP/g.o")
	gcc -m$m -O2 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
	want_run=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env CC_MIR_STATS=1"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		what="-m$m -O2 $path"
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
			echo "run_range_shift_count: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_range_shift_count: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ "$(echo $want)" != "count_kept" ]; then
			echo "run_range_shift_count: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_range_shift_count: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_range_shift_count: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_range_shift_count: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_range_shift_count: ok"
