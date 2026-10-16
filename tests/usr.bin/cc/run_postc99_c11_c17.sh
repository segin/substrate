#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
# a private directory for this run's files (the suite's Makefile passes its own)
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
INC_DIR="-I$ROOT/include"

"$CC_BIN" -std=c11 native_c11_static_assert.c -o $CC_TMP/cc_native_c11_static_assert
$CC_TMP/cc_native_c11_static_assert

"$CC_BIN" -std=c11 native_c11_generic.c -o $CC_TMP/cc_native_c11_generic
$CC_TMP/cc_native_c11_generic

"$CC_BIN" -std=c11 native_c11_align_thread_noreturn.c -o $CC_TMP/cc_native_c11_align_thread_noreturn
$CC_TMP/cc_native_c11_align_thread_noreturn

"$CC_BIN" -std=c11 native_c11_noreturn_nonvoid.c -o $CC_TMP/cc_native_c11_noreturn_nonvoid
$CC_TMP/cc_native_c11_noreturn_nonvoid

"$CC_BIN" -std=c11 $INC_DIR native_c11_atomic.c -o $CC_TMP/cc_native_c11_atomic
$CC_TMP/cc_native_c11_atomic

"$CC_BIN" -std=c11 native_c11_atomic_wrapper_type.c -o $CC_TMP/cc_native_c11_atomic_wrapper_type
$CC_TMP/cc_native_c11_atomic_wrapper_type

"$CC_BIN" -std=c11 native_c11_atomic_fence.c -o $CC_TMP/cc_native_c11_atomic_fence
$CC_TMP/cc_native_c11_atomic_fence
"$CC_BIN" -std=c11 -S native_c11_atomic_fence.c -o $CC_TMP/cc_native_c11_atomic_fence.s
grep -Eq 'lock(;|[[:space:]]+)orl \$0, \(%rsp\)' $CC_TMP/cc_native_c11_atomic_fence.s
grep -q "# asm clobber memory" $CC_TMP/cc_native_c11_atomic_fence.s
"$CC_BIN" -std=c11 -m32 -S native_c11_atomic_fence.c -o $CC_TMP/cc_native_c11_atomic_fence_32.s
grep -Eq 'lock(;|[[:space:]]+)orl \$0, \(%esp\)' $CC_TMP/cc_native_c11_atomic_fence_32.s
grep -q "# asm clobber memory" $CC_TMP/cc_native_c11_atomic_fence_32.s

"$CC_BIN" -std=c11 native_c11_unicode_literals.c -o $CC_TMP/cc_native_c11_unicode_literals
$CC_TMP/cc_native_c11_unicode_literals

"$CC_BIN" -std=c11 native_c11_u8_macro_literal.c -o $CC_TMP/cc_native_c11_u8_macro_literal
$CC_TMP/cc_native_c11_u8_macro_literal

! "$CC_BIN" -std=c11 -c native_bad_c11_static_assert_fail.c -o $CC_TMP/cc_native_bad_c11_static_assert_fail.o
! "$CC_BIN" -std=c11 -c native_bad_c11_thread_local_local.c -o $CC_TMP/cc_native_bad_c11_thread_local_local.o
! "$CC_BIN" -std=c11 -c native_bad_c11_atomic_memorder_nonconst.c -o $CC_TMP/cc_native_bad_c11_atomic_memorder_nonconst.o
! "$CC_BIN" -std=c11 -c native_bad_c11_atomic_memorder_load_release.c -o $CC_TMP/cc_native_bad_c11_atomic_memorder_load_release.o
! "$CC_BIN" -std=c11 -c native_bad_c11_atomic_memorder_store_acquire.c -o $CC_TMP/cc_native_bad_c11_atomic_memorder_store_acquire.o
! "$CC_BIN" -std=c11 -c native_bad_c11_atomic_memorder_range.c -o $CC_TMP/cc_native_bad_c11_atomic_memorder_range.o

"$CC_BIN" -std=c17 native_c17_register.c -o $CC_TMP/cc_native_c17_register
$CC_TMP/cc_native_c17_register
"$CC_BIN" -std=c17 -Wall -Wextra -Werror native_c17_empty_param_no_oldstyle.c -o $CC_TMP/cc_native_c17_empty_param_no_oldstyle
$CC_TMP/cc_native_c17_empty_param_no_oldstyle
! "$CC_BIN" -std=c17 -Wall -Werror -c native_bad_c17_register_obsolescent.c -o $CC_TMP/cc_native_bad_c17_register_obsolescent.o
! "$CC_BIN" -std=c17 -Wall -Werror -c native_bad_c17_oldstyle_obsolescent.c -o $CC_TMP/cc_native_bad_c17_oldstyle_obsolescent.o

"$CC_BIN" -std=c11 native_c11_pp_optional_macros.c -o $CC_TMP/cc_native_c11_pp_optional_macros
$CC_TMP/cc_native_c11_pp_optional_macros

"$CC_BIN" -std=c99 native_c99_pp_no_optional_macros.c -o $CC_TMP/cc_native_c99_pp_no_optional_macros
$CC_TMP/cc_native_c99_pp_no_optional_macros

"$CC_BIN" -std=c11 -E native_c11_pp_version.c -o $CC_TMP/cc_native_c11_pp_version.i
grep -q "stdc_version = 201112L;" $CC_TMP/cc_native_c11_pp_version.i
grep -q "stdc_no_threads = 1;" $CC_TMP/cc_native_c11_pp_version.i
grep -q "stdc_no_complex = 1;" $CC_TMP/cc_native_c11_pp_version.i

"$CC_BIN" -std=c17 -E native_c11_pp_version.c -o $CC_TMP/cc_native_c17_pp_version.i
grep -q "stdc_version = 201710L;" $CC_TMP/cc_native_c17_pp_version.i
grep -q "stdc_no_threads = 1;" $CC_TMP/cc_native_c17_pp_version.i
grep -q "stdc_no_complex = 1;" $CC_TMP/cc_native_c17_pp_version.i
