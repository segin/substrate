#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
# a private directory for this run's files (the suite's Makefile passes its own)
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

"$CC_BIN" -std=c23 native_c23_attr_std.c -o $CC_TMP/cc_native_c23_attr_std 2>$CC_TMP/cc_native_c23_attr_std.log
$CC_TMP/cc_native_c23_attr_std
grep -q "deprecated function used: old_api" $CC_TMP/cc_native_c23_attr_std.log
grep -q "ignoring nodiscard return value from must_use" $CC_TMP/cc_native_c23_attr_std.log

! "$CC_BIN" -std=c23 -Werror native_c23_attr_std.c -o $CC_TMP/cc_native_c23_attr_std_werror 2>$CC_TMP/cc_native_c23_attr_std_werror.log
grep -q "deprecated function used: old_api" $CC_TMP/cc_native_c23_attr_std_werror.log

! "$CC_BIN" -std=c23 -c native_bad_c23_attr_fallthrough_outside.c -o $CC_TMP/cc_native_bad_c23_attr_fallthrough_outside.o

"$CC_BIN" -std=c23 -E native_c23_attr_has.c -o $CC_TMP/cc_native_c23_attr_has.i
grep -q "has_deprecated_attr = 1;" $CC_TMP/cc_native_c23_attr_has.i
grep -q "has_nodiscard_attr = 1;" $CC_TMP/cc_native_c23_attr_has.i
grep -q "has_reproducible_attr = 1;" $CC_TMP/cc_native_c23_attr_has.i
grep -q "has_unsequenced_attr = 1;" $CC_TMP/cc_native_c23_attr_has.i
