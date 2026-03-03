#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"

"$CC_BIN" -E -std=gnu11 -P pp_s63_clang_pragma.c -o /tmp/cc_pp_s63_clang_pragma.i
grep -q '^int clang_pragma_ok = 11;$' /tmp/cc_pp_s63_clang_pragma.i

! "$CC_BIN" -E -std=gnu11 -P pp_s63_bad_clang_pragma.c -o /tmp/cc_pp_s63_bad_clang_pragma.i 2>/tmp/cc_pp_s63_bad_clang_pragma.log
grep -q '^pp_s63_bad_clang_pragma.c:1:1: error:' /tmp/cc_pp_s63_bad_clang_pragma.log
grep -q 'unsupported #pragma clang diagnostic action' /tmp/cc_pp_s63_bad_clang_pragma.log

! "$CC_BIN" -E -std=gnu11 -P pp_s63_bad_clang_section.c -o /tmp/cc_pp_s63_bad_clang_section.i 2>/tmp/cc_pp_s63_bad_clang_section.log
grep -q '^pp_s63_bad_clang_section.c:1:1: error:' /tmp/cc_pp_s63_bad_clang_section.log
grep -q 'malformed #pragma clang section string' /tmp/cc_pp_s63_bad_clang_section.log

! "$CC_BIN" -E -std=gnu11 -P pp_s63_bad_clang_fp.c -o /tmp/cc_pp_s63_bad_clang_fp.i 2>/tmp/cc_pp_s63_bad_clang_fp.log
grep -q '^pp_s63_bad_clang_fp.c:1:1: error:' /tmp/cc_pp_s63_bad_clang_fp.log
grep -q 'unsupported #pragma clang fp option' /tmp/cc_pp_s63_bad_clang_fp.log

! "$CC_BIN" -E -std=gnu11 -P pp_s63_bad_clang_attribute.c -o /tmp/cc_pp_s63_bad_clang_attribute.i 2>/tmp/cc_pp_s63_bad_clang_attribute.log
grep -q '^pp_s63_bad_clang_attribute.c:1:1: error:' /tmp/cc_pp_s63_bad_clang_attribute.log
grep -q 'malformed #pragma clang attribute push' /tmp/cc_pp_s63_bad_clang_attribute.log

! "$CC_BIN" -E -std=gnu11 -P pp_s63_bad_clang_loop.c -o /tmp/cc_pp_s63_bad_clang_loop.i 2>/tmp/cc_pp_s63_bad_clang_loop.log
grep -q '^pp_s63_bad_clang_loop.c:1:1: error:' /tmp/cc_pp_s63_bad_clang_loop.log
grep -q 'unsupported #pragma clang loop option' /tmp/cc_pp_s63_bad_clang_loop.log
