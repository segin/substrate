#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
# a private directory for this run's files (the suite's Makefile passes its own)
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

"$CC_BIN" -std=c11 -E native_c11_pp_version.c -o $CC_TMP/cc_postc99_mode_c11.i
grep -q "stdc_version = 201112L;" $CC_TMP/cc_postc99_mode_c11.i

"$CC_BIN" -std=c17 -E native_c11_pp_version.c -o $CC_TMP/cc_postc99_mode_c17.i
grep -q "stdc_version = 201710L;" $CC_TMP/cc_postc99_mode_c17.i

"$CC_BIN" -std=c18 -E native_c11_pp_version.c -o $CC_TMP/cc_postc99_mode_c18.i
grep -q "stdc_version = 201710L;" $CC_TMP/cc_postc99_mode_c18.i

"$CC_BIN" -std=c23 -E native_c23_pp_version.c -o $CC_TMP/cc_postc99_mode_c23.i
grep -q "stdc_version = 202311L;" $CC_TMP/cc_postc99_mode_c23.i

"$CC_BIN" -std=c2x -E native_c23_pp_version.c -o $CC_TMP/cc_postc99_mode_c2x.i
grep -q "stdc_version = 202311L;" $CC_TMP/cc_postc99_mode_c2x.i

"$CC_BIN" -std=c11 native_c11_generic.c -o $CC_TMP/cc_postc99_mode_c11
$CC_TMP/cc_postc99_mode_c11

"$CC_BIN" -std=c17 native_c17_register.c -o $CC_TMP/cc_postc99_mode_c17
$CC_TMP/cc_postc99_mode_c17

"$CC_BIN" -std=c23 native_c23_keywords.c -o $CC_TMP/cc_postc99_mode_c23
$CC_TMP/cc_postc99_mode_c23

"$CC_BIN" -std=c2x native_c23_keywords.c -o $CC_TMP/cc_postc99_mode_c2x
$CC_TMP/cc_postc99_mode_c2x
