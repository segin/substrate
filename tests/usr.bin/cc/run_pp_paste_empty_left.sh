#!/bin/sh
# An empty macro argument on the left of `##` is a placemarker: the paste
# leaves the tokens before it apart from the right operand.  cc dropped
# the marker before the paste saw it and joined them -- `int p##x` with p
# empty became `intx` -- so Linux's CONFIGFS_ATTR(, enabled), whose
# `struct configfs_attribute _pfx##attr_##_name` became
# `configfs_attributeattr_enabled`, was refused in netconsole.c with
# "expected identifier after declaration type"; two empty operands in a
# row (`p##q##n`) likewise.  The preprocessed text is gcc's, but for
# white space, and the program's values are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_paste_empty_left: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/pp.h" <<'EOF2'
#define D(p, n) int p##x##n = 1;
#define E(p, n) struct s p##attr_##n = { 2 };
#define F(p, n) p ## n
#define G(p, q, n) int p##q##n = 3;
#define CAT3(a, b, c) a##b##c

struct s { int v; };
D(, y)
E(, enabled)
E(cfg_, enabled)
int F(, z) = F(1, 2);
G(, , w)
int CAT3(, , v) = 4, CAT3(a, , b) = 5;
EOF2
cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>
#include "pp.h"

int main(void)
{
	printf("%d %d %d %d %d %d %d\n", xy, attr_enabled.v, cfg_attr_enabled.v, z, w, v, ab);
	return 0;
}
EOF2
gcc -E -P -x c "$CC_TMP/pp.h" | tr -s ' \n' '  ' | sed 's/^ *//' >"$CC_TMP/g.i"
"$CC_BIN" -E -P -x c "$CC_TMP/pp.h" | tr -s ' \n' '  ' | sed 's/^ *//' >"$CC_TMP/c.i"
if ! cmp -s "$CC_TMP/g.i" "$CC_TMP/c.i"; then
	echo "run_pp_paste_empty_left: preprocessed text differs from gcc's:"
	diff "$CC_TMP/g.i" "$CC_TMP/c.i" || true
	exit 1
fi
gcc -O0 -o "$CC_TMP/g" "$CC_TMP/v.c"
want=$("$CC_TMP/g")
"$CC_BIN" -O2 -c -o "$CC_TMP/v.o" "$CC_TMP/v.c"
gcc -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
got=$("$CC_TMP/c")
if [ "$got" != "$want" ]; then
	echo "run_pp_paste_empty_left: prints [$got], gcc's [$want]"
	exit 1
fi
echo "run_pp_paste_empty_left: ok"
