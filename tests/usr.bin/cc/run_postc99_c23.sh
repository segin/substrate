#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
# a private directory for this run's files (the suite's Makefile passes its own)
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

"$CC_BIN" -std=c23 native_c23_keywords.c -o $CC_TMP/cc_native_c23_keywords
$CC_TMP/cc_native_c23_keywords

"$CC_BIN" -std=c23 native_c23_nullptr.c -o $CC_TMP/cc_native_c23_nullptr
$CC_TMP/cc_native_c23_nullptr

"$CC_BIN" -std=c23 native_c23_typeof.c -o $CC_TMP/cc_native_c23_typeof
$CC_TMP/cc_native_c23_typeof

"$CC_BIN" -std=c23 native_c23_bitint.c -o $CC_TMP/cc_native_c23_bitint
$CC_TMP/cc_native_c23_bitint

"$CC_BIN" -std=c23 native_c23_bitint_size.c -o $CC_TMP/cc_native_c23_bitint_size
$CC_TMP/cc_native_c23_bitint_size

"$CC_BIN" -std=c23 native_c23_binary_sep.c -o $CC_TMP/cc_native_c23_binary_sep
$CC_TMP/cc_native_c23_binary_sep

"$CC_BIN" -std=c23 native_c23_empty_init.c -o $CC_TMP/cc_native_c23_empty_init
$CC_TMP/cc_native_c23_empty_init

"$CC_BIN" -std=c23 native_c23_enum_underlying.c -o $CC_TMP/cc_native_c23_enum_underlying
$CC_TMP/cc_native_c23_enum_underlying

"$CC_BIN" -std=c23 native_c23_label_decl.c -o $CC_TMP/cc_native_c23_label_decl
$CC_TMP/cc_native_c23_label_decl

"$CC_BIN" -std=c23 native_c23_constexpr_auto_decimal.c -o $CC_TMP/cc_native_c23_constexpr_auto_decimal
$CC_TMP/cc_native_c23_constexpr_auto_decimal

"$CC_BIN" -std=c23 native_c23_decimal_sizes.c -o $CC_TMP/cc_native_c23_decimal_sizes
$CC_TMP/cc_native_c23_decimal_sizes

"$CC_BIN" -std=c23 native_c23_empty_param.c -o $CC_TMP/cc_native_c23_empty_param
$CC_TMP/cc_native_c23_empty_param

"$CC_BIN" -std=c23 native_c23_asm_extension.c -o $CC_TMP/cc_native_c23_asm_extension
$CC_TMP/cc_native_c23_asm_extension

! "$CC_BIN" -std=c23 -c native_bad_c23_auto_no_init.c -o $CC_TMP/cc_native_bad_c23_auto_no_init.o
! "$CC_BIN" -std=c23 -c native_bad_c23_bitint_zero.c -o $CC_TMP/cc_native_bad_c23_bitint_zero.o
! "$CC_BIN" -std=c23 -c native_bad_c23_static_assert_single_fail.c -o $CC_TMP/cc_native_bad_c23_static_assert_single_fail.o
! "$CC_BIN" -std=c23 -c native_bad_c23_empty_param_call.c -o $CC_TMP/cc_native_bad_c23_empty_param_call.o

"$CC_BIN" -std=c23 -E native_c23_pp_version.c -o $CC_TMP/cc_native_c23_pp_version.i
grep -q "stdc_version = 202311L;" $CC_TMP/cc_native_c23_pp_version.i
grep -q "has_deprecated = 1;" $CC_TMP/cc_native_c23_pp_version.i
grep -q "has_nodiscard = 1;" $CC_TMP/cc_native_c23_pp_version.i
grep -q "has_unknown = 0;" $CC_TMP/cc_native_c23_pp_version.i
