#!/bin/sh
# An object-like macro that names a function-like one takes its
# arguments from the next line, as the function-like one itself does:
# Linux's `#define cpu_to_le32 __cpu_to_le32` used as `cpu_to_le32\n
# (x)`, and orangefs's `gossip_err\n ("...")` through pr_err, were left
# unexpanded, and refused as implicit declarations of __cpu_to_le32 and
# pr_err.  The preprocessed text is gcc's, but for white space, and the
# program's values are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_macro_alias_paren_next_line: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/pp.h" <<'EOF2'
#define __f(x) ((x) + 1)
#define f __f
#define ff f
#define g(x) ((x) * 2)
#define h g
int a = f
	(3);
int b = h
	(4);
int c = ff

	(5);
#define pe(fmt) fmt
#define gerr pe
const char *d = gerr
	("x");
#define obj 7
int e = obj
	;
EOF2
cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>
#include "pp.h"

int main(void)
{
	printf("%d %d %d %s %d\n", a, b, c, d, e);
	return 0;
}
EOF2
gcc -E -P -x c "$CC_TMP/pp.h" | tr -s ' \t\n' '   ' | sed 's/^ *//' >"$CC_TMP/g.i"
"$CC_BIN" -E -P -x c "$CC_TMP/pp.h" | tr -s ' \t\n' '   ' | sed 's/^ *//' >"$CC_TMP/c.i"
if ! cmp -s "$CC_TMP/g.i" "$CC_TMP/c.i"; then
	echo "run_macro_alias_paren_next_line: preprocessed text differs from gcc's:"
	diff "$CC_TMP/g.i" "$CC_TMP/c.i" || true
	exit 1
fi
gcc -O0 -o "$CC_TMP/g" "$CC_TMP/v.c"
want=$("$CC_TMP/g")
"$CC_BIN" -O2 -c -o "$CC_TMP/v.o" "$CC_TMP/v.c"
gcc -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
got=$("$CC_TMP/c")
if [ "$got" != "$want" ]; then
	echo "run_macro_alias_paren_next_line: prints [$got], gcc's [$want]"
	exit 1
fi
echo "run_macro_alias_paren_next_line: ok"
