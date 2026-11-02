#!/bin/sh
# A file-scope initializer may read a const object whose value is known,
# as gcc folds it: `unsigned int x = nmin;` of a `static const unsigned
# int nmin = 5000` is 5000.  cc wrote the object's address there instead
# -- Linux's kernel/rseq.c, `unsigned int rseq_slice_ext_nsecs
# __read_mostly = rseq_slice_ext_nsecs_min;`, held the low half of an
# address, an R_X86_64_32 the kernel link refused -- and refused one
# computed from it, `lmin * 2`, as unsupported.  Values of every integer
# width and signedness, a chain of consts, an expression of them, an
# extern const array's element left alone, and a function's local of the
# same name not folded; the program prints what gcc's prints, -O0 and
# -O2, -m64 and -m32, both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_global_init_const_value: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>
static const unsigned int nmin = 5 * 1000;
unsigned int cur = nmin;
static const long lmin = -7;
long lcur = lmin * 2;
const signed char sc = -3;
int from_sc = sc;
static const unsigned short us = 65535;
unsigned long from_us = us + 1UL;
const long long big = 0x123456789LL;
long long from_big = big >> 4;
static const int a = 3;
static const int b = a * a;
int chain = b + a;
unsigned char narrowed = nmin;
int local_shadow(void)
{
	int nmin = 42;
	static int s = 7;

	return nmin + s;
}
int main(void)
{
	printf("%u %ld %d %lu %lld %d %u %d\n", cur, lcur, from_sc, from_us, from_big, chain,
	       (unsigned)narrowed, local_shadow());
	return 0;
}
EOF2
for m in 64 32; do
	for o in -O0 -O2; do
		gcc -m$m $o -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
		want=$("$CC_TMP/g")
		for path in mir legacy; do
			run="env"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			if ! $run "$CC_BIN" -m$m $o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
				echo "run_global_init_const_value: -m$m $o $path refused: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_global_init_const_value: -m$m $o $path prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_global_init_const_value: ok"
