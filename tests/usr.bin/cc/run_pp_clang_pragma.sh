#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
# a private directory for this run's files (the suite's Makefile passes its own)
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

"$CC_BIN" -E -std=gnu11 -P pp_s63_clang_pragma.c -o $CC_TMP/cc_pp_s63_clang_pragma.i
grep -q '^int clang_pragma_ok = 11;$' $CC_TMP/cc_pp_s63_clang_pragma.i

! "$CC_BIN" -E -std=gnu11 -P pp_s63_bad_clang_pragma.c -o $CC_TMP/cc_pp_s63_bad_clang_pragma.i 2>$CC_TMP/cc_pp_s63_bad_clang_pragma.log
grep -q '^pp_s63_bad_clang_pragma.c:1:1: error:' $CC_TMP/cc_pp_s63_bad_clang_pragma.log
grep -q 'unsupported #pragma clang diagnostic action' $CC_TMP/cc_pp_s63_bad_clang_pragma.log

! "$CC_BIN" -E -std=gnu11 -P pp_s63_bad_clang_section.c -o $CC_TMP/cc_pp_s63_bad_clang_section.i 2>$CC_TMP/cc_pp_s63_bad_clang_section.log
grep -q '^pp_s63_bad_clang_section.c:1:1: error:' $CC_TMP/cc_pp_s63_bad_clang_section.log
grep -q 'malformed #pragma clang section string' $CC_TMP/cc_pp_s63_bad_clang_section.log

! "$CC_BIN" -E -std=gnu11 -P pp_s63_bad_clang_fp.c -o $CC_TMP/cc_pp_s63_bad_clang_fp.i 2>$CC_TMP/cc_pp_s63_bad_clang_fp.log
grep -q '^pp_s63_bad_clang_fp.c:1:1: error:' $CC_TMP/cc_pp_s63_bad_clang_fp.log
grep -q 'unsupported #pragma clang fp option' $CC_TMP/cc_pp_s63_bad_clang_fp.log

! "$CC_BIN" -E -std=gnu11 -P pp_s63_bad_clang_attribute.c -o $CC_TMP/cc_pp_s63_bad_clang_attribute.i 2>$CC_TMP/cc_pp_s63_bad_clang_attribute.log
grep -q '^pp_s63_bad_clang_attribute.c:1:1: error:' $CC_TMP/cc_pp_s63_bad_clang_attribute.log
grep -q 'malformed #pragma clang attribute push' $CC_TMP/cc_pp_s63_bad_clang_attribute.log

! "$CC_BIN" -E -std=gnu11 -P pp_s63_bad_clang_loop.c -o $CC_TMP/cc_pp_s63_bad_clang_loop.i 2>$CC_TMP/cc_pp_s63_bad_clang_loop.log
grep -q '^pp_s63_bad_clang_loop.c:1:1: error:' $CC_TMP/cc_pp_s63_bad_clang_loop.log
grep -q 'unsupported #pragma clang loop option' $CC_TMP/cc_pp_s63_bad_clang_loop.log
