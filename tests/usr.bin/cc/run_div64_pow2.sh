#!/bin/sh
# A 64-bit division or remainder by a constant power of two compiles, at
# -m32, to shifts and masks with no call of libgcc's __udivdi3, __divdi3,
# __umoddi3 or __moddi3, as with gcc: Linux does not link libgcc, and
# head32.o's PAGE_TABLE_SIZE(LOWMEM_PAGES) left __udivdi3 undefined at the
# vmlinux link.  codegen/div64_pow2.c checks the values.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

# and codegen/const64_fold.c: a division of constants is computed at
# compile time, at -O0 too (Linux's KTIME_MAX / NSEC_PER_SEC)
# and codegen/mul_overflow64.c: a 64-bit __builtin_mul_overflow divides nothing
for t in div64_pow2 const64_fold mul_overflow64; do
	for o in O0 O2; do
		"$CC_BIN" -m32 -$o -S -o "$CC_TMP/$t.s" "$ROOT/tests/usr.bin/cc/codegen/$t.c"
		if grep -Eq '__u?(div|mod)di3' "$CC_TMP/$t.s"; then
			echo "run_div64_pow2: $t.c -$o calls libgcc"
			exit 1
		fi
	done
done
echo "run_div64_pow2: ok"
