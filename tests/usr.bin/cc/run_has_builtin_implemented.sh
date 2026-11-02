#!/bin/sh
# __has_builtin answers for the builtins cc implements, as gcc answers:
# the preprocessor had a list of its own, which left out many cc compiles
# (__builtin_memcpy, __builtin_popcount, __builtin_alloca, the libc
# builtins, the __sync and __atomic ones), so code choosing by
# __has_builtin took its fallback where gcc's takes the builtin.  Each of
# these is 1 with gcc and with cc, in #if and in ordinary text; a name
# that is no builtin is 0 with both.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_has_builtin_implemented: skip (no gcc)"
	exit 0
fi

names="__builtin_memcpy __builtin_memmove __builtin_memset __builtin_memcmp __builtin_strlen
__builtin_strcmp __builtin_strncpy __builtin_abort __builtin_malloc __builtin_alloca
__builtin_popcount __builtin_popcountll __builtin_parity __builtin_clz __builtin_ctzll
__builtin_ffs __builtin_bswap16 __builtin_bswap64 __builtin_expect __builtin_constant_p
__builtin_trap __builtin_unreachable __builtin_assume_aligned __builtin_object_size
__builtin_return_address __builtin_frame_address __builtin_prefetch __builtin_huge_val
__builtin_inf __builtin_nan __builtin_isnan __builtin_isinf __builtin_isfinite
__builtin_signbit __builtin_complex __builtin_add_overflow __builtin_uaddll_overflow
__builtin_mul_overflow_p __builtin_va_start __builtin_va_end __builtin_va_copy
__builtin___memcpy_chk __builtin_types_compatible_p __builtin_choose_expr
__builtin_offsetof __builtin_extract_return_addr __builtin_cpu_supports
__sync_fetch_and_add __sync_lock_test_and_set __sync_bool_compare_and_swap
__sync_val_compare_and_swap __sync_lock_release __sync_synchronize __atomic_load_n
__atomic_store_n __atomic_exchange_n __atomic_compare_exchange_n __atomic_fetch_add
__atomic_add_fetch __atomic_thread_fence __atomic_signal_fence __atomic_load
__atomic_always_lock_free not_a_builtin __builtin_not_one_either"

{
	for n in $names; do
		printf '#if __has_builtin(%s)\nif_%s 1\n#else\nif_%s 0\n#endif\ntext_%s __has_builtin(%s)\n' \
			"$n" "$n" "$n" "$n" "$n"
	done
} >"$CC_TMP/h.c"
gcc -E -P "$CC_TMP/h.c" | tr -s ' \t' '  ' | sed 's/^ //; s/ $//' | grep . >"$CC_TMP/g.out"
"$CC_BIN" -E -P -o "$CC_TMP/c.raw" "$CC_TMP/h.c"
tr -s ' \t' '  ' <"$CC_TMP/c.raw" | sed 's/^ //; s/ $//' | grep . >"$CC_TMP/c.out"
if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/c.out"; then
	echo "run_has_builtin_implemented: __has_builtin differs from gcc's (cc's on the right):"
	diff "$CC_TMP/g.out" "$CC_TMP/c.out" | grep '^[<>]' | head -20
	exit 1
fi
echo "run_has_builtin_implemented: ok"
