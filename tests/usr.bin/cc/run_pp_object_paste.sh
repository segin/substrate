#!/bin/sh
# `##` in an object-like macro's body pastes its operands, unexpanded,
# before the result is rescanned (C11 6.10.3.3): cc rescanned the body
# first and left the `##` in it, so Linux's f_uac1.c, with
# `#define uac_format_type_i_discrete_descriptor
# uac_format_type_i_discrete_descriptor_##UAC_MAX_RATES`, was refused
# with "invalid token".  A `##` in a string literal is no operator.  The
# preprocessed text is gcc's, but for white space, and the program's
# values are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_object_paste: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/pp.h" <<'EOF2'
#define N 5
#define dd dd_##N
#define ee pre_ ## N ## _post
#define ff "a##b"
#define gg dd
struct dd { int x; };
struct dd_5 { char y; };
int ee = 1, gg = 2;
const char *s = ff;
int k = N;
EOF2
cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>
#include "pp.h"

int main(void)
{
	struct dd_N v = { 3 };

	printf("%d %d %d %s %d %zu\n", pre_N_post, dd_N, v.x, s, k, sizeof(struct dd));
	return 0;
}
EOF2
gcc -E -P -x c "$CC_TMP/pp.h" | tr -d ' \t\n' >"$CC_TMP/g.i"
"$CC_BIN" -E -P -x c "$CC_TMP/pp.h" | tr -d ' \t\n' >"$CC_TMP/c.i"
if ! cmp -s "$CC_TMP/g.i" "$CC_TMP/c.i"; then
	echo "run_pp_object_paste: preprocessed text differs from gcc's:"
	diff "$CC_TMP/g.i" "$CC_TMP/c.i" || true
	exit 1
fi
gcc -O0 -o "$CC_TMP/g" "$CC_TMP/v.c"
want=$("$CC_TMP/g")
"$CC_BIN" -O2 -c -o "$CC_TMP/v.o" "$CC_TMP/v.c"
gcc -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
got=$("$CC_TMP/c")
if [ "$got" != "$want" ]; then
	echo "run_pp_object_paste: prints [$got], gcc's [$want]"
	exit 1
fi
echo "run_pp_object_paste: ok"
