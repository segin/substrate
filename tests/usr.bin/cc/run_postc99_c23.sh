#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"

"$CC_BIN" -std=c23 native_c23_keywords.c -o /tmp/cc_native_c23_keywords
/tmp/cc_native_c23_keywords

"$CC_BIN" -std=c23 native_c23_nullptr.c -o /tmp/cc_native_c23_nullptr
/tmp/cc_native_c23_nullptr

"$CC_BIN" -std=c23 native_c23_typeof.c -o /tmp/cc_native_c23_typeof
/tmp/cc_native_c23_typeof

"$CC_BIN" -std=c23 native_c23_bitint.c -o /tmp/cc_native_c23_bitint
/tmp/cc_native_c23_bitint

"$CC_BIN" -std=c23 native_c23_bitint_size.c -o /tmp/cc_native_c23_bitint_size
/tmp/cc_native_c23_bitint_size

"$CC_BIN" -std=c23 native_c23_binary_sep.c -o /tmp/cc_native_c23_binary_sep
/tmp/cc_native_c23_binary_sep

"$CC_BIN" -std=c23 native_c23_empty_init.c -o /tmp/cc_native_c23_empty_init
/tmp/cc_native_c23_empty_init

"$CC_BIN" -std=c23 native_c23_enum_underlying.c -o /tmp/cc_native_c23_enum_underlying
/tmp/cc_native_c23_enum_underlying

"$CC_BIN" -std=c23 native_c23_label_decl.c -o /tmp/cc_native_c23_label_decl
/tmp/cc_native_c23_label_decl

"$CC_BIN" -std=c23 native_c23_constexpr_auto_decimal.c -o /tmp/cc_native_c23_constexpr_auto_decimal
/tmp/cc_native_c23_constexpr_auto_decimal

"$CC_BIN" -std=c23 native_c23_decimal_sizes.c -o /tmp/cc_native_c23_decimal_sizes
/tmp/cc_native_c23_decimal_sizes

! "$CC_BIN" -std=c23 -c native_bad_c23_auto_no_init.c -o /tmp/cc_native_bad_c23_auto_no_init.o
! "$CC_BIN" -std=c23 -c native_bad_c23_bitint_zero.c -o /tmp/cc_native_bad_c23_bitint_zero.o
! "$CC_BIN" -std=c23 -c native_bad_c23_static_assert_single_fail.c -o /tmp/cc_native_bad_c23_static_assert_single_fail.o

"$CC_BIN" -std=c23 -E native_c23_pp_version.c -o /tmp/cc_native_c23_pp_version.i
grep -q "stdc_version = 202311L;" /tmp/cc_native_c23_pp_version.i
grep -q "has_deprecated = 1;" /tmp/cc_native_c23_pp_version.i
grep -q "has_nodiscard = 1;" /tmp/cc_native_c23_pp_version.i
grep -q "has_unknown = 0;" /tmp/cc_native_c23_pp_version.i
