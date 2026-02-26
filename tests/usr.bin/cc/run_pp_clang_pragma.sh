#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"

"$CC_BIN" -E -std=gnu11 -P pp_s63_clang_pragma.c -o /tmp/cc_pp_s63_clang_pragma.i
grep -q '^int clang_pragma_ok = 11;$' /tmp/cc_pp_s63_clang_pragma.i

! "$CC_BIN" -E -std=gnu11 -P pp_s63_bad_clang_pragma.c -o /tmp/cc_pp_s63_bad_clang_pragma.i
