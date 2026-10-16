#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
# a private directory for this run's files (the suite's Makefile passes its own)
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

"$CC_BIN" -E -std=gnu11 -P pp_s5_named_va.c -o $CC_TMP/cc_pp_s5_named_va.i
grep -Eq '^int a0 = 7[[:space:]]*;$' $CC_TMP/cc_pp_s5_named_va.i
grep -Eq '^int a1 = 9[[:space:]]*,[[:space:]]*4[[:space:]]*;$' $CC_TMP/cc_pp_s5_named_va.i
! "$CC_BIN" -E -std=c99 -P pp_s5_named_va.c -o $CC_TMP/cc_pp_s5_named_va_c99.i

"$CC_BIN" -E -std=gnu11 -P pp_s5_dynmac.c -o $CC_TMP/cc_pp_s5_dynmac.i
grep -q '^int c0 = 0;$' $CC_TMP/cc_pp_s5_dynmac.i
grep -q '^int c1 = 1;$' $CC_TMP/cc_pp_s5_dynmac.i
grep -q '^const char \*bf = "pp_s5_dynmac.c";$' $CC_TMP/cc_pp_s5_dynmac.i
grep -q '^const char \*fn = "pp_s5_dynmac.c";$' $CC_TMP/cc_pp_s5_dynmac.i
grep -q '^const char \*ts = "Thu Jan  1 00:00:00 1970";$' $CC_TMP/cc_pp_s5_dynmac.i
grep -q '^int il = 0;$' $CC_TMP/cc_pp_s5_dynmac.i

PP_S5_BASE=$CC_TMP/cc_pp_s5_include_next_$$
mkdir -p "$PP_S5_BASE/a" "$PP_S5_BASE/b"
cat > "$PP_S5_BASE/a/next.h" <<'EOF'
#define NEXT_A 1
#include_next "next.h"
EOF
cat > "$PP_S5_BASE/b/next.h" <<'EOF'
#define NEXT_B 2
EOF
cat > "$PP_S5_BASE/main.c" <<'EOF'
#include "next.h"
int v = NEXT_A + NEXT_B;
EOF
"$CC_BIN" -E -std=gnu11 -P -I"$PP_S5_BASE/a" -I"$PP_S5_BASE/b" "$PP_S5_BASE/main.c" -o $CC_TMP/cc_pp_s5_include_next.i
grep -q '^int v = 1 + 2;$' $CC_TMP/cc_pp_s5_include_next.i
rm -rf "$PP_S5_BASE"

"$CC_BIN" -E -std=gnu11 -P pp_s5_pragma.c -o $CC_TMP/cc_pp_s5_pragma.i
grep -q '^int p = 1;$' $CC_TMP/cc_pp_s5_pragma.i
