#!/bin/sh
# Tokens an expansion puts side by side stay two tokens.  cc's
# preprocessor works on text and reads its result again, and `(-x)` of
# `-111` came out `(--111)`, a decrement: Linux's
# NF_DROP_ERR(-ECONNREFUSED) (security/selinux/hooks.c, the Arch
# config) did not compile.  A space parts what would join, as gcc's
# -E writes it: the punctuators that join, a name and what continues
# it, a number and `.`, `+` or `-`.  `##` still joins.  The -E text,
# without its line markers and blank lines, and the values are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_avoid_paste: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>

#define NEG(x) (-x)
#define PLUS(x) +x
#define MINUS1 -1
#define ONE 1
#define CAT(a, b) a##b
#define ERR(x) (((-x) << 16) | 1)

int main(void)
{
	int v = 5;
	int *p = &v;
	int a = NEG(-1);
	int b = NEG(MINUS1);
	int c = 1 PLUS(+2);
	int e = NEG(-MINUS1);
	int f = ERR(-111) >> 16;
	int i = CAT(1, 2) - ONE-1;
	int k = *p-MINUS1;
	int m = v+PLUS(1);

	printf("%d %d %d %d %d %d %d %d\n", a, b, c, e, f, i, k, m);
	return 0;
}
EOF2
gcc -O1 -o "$CC_TMP/g" "$CC_TMP/p.c" 2>/dev/null
want=$("$CC_TMP/g")
for o in O0 O2; do
	if ! "$CC_BIN" -$o -o "$CC_TMP/c" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
		echo "run_pp_avoid_paste: -$o: $(head -1 "$CC_TMP/c.err")"
		exit 1
	fi
	got=$("$CC_TMP/c")
	if [ "$got" != "$want" ]; then
		echo "run_pp_avoid_paste: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
# the expansion's text, from main on
gcc -E -P "$CC_TMP/p.c" | sed -n '/^int main/,$p' >"$CC_TMP/g.i"
"$CC_BIN" -E -P "$CC_TMP/p.c" | sed -n '/^int main/,$p' >"$CC_TMP/c.i"
if ! diff -bB "$CC_TMP/g.i" "$CC_TMP/c.i" >"$CC_TMP/d"; then
	echo "run_pp_avoid_paste: -E differs from gcc's"
	cat "$CC_TMP/d"
	exit 1
fi
# what parts them is no character of a string # makes: Linux's trace headers
# `#include __stringify(PATH/name.h)` of a PATH ending in `/`
mkdir -p "$CC_TMP/inc/sub"
echo 'int from_sub = 2;' >"$CC_TMP/inc/sub/vt.h"
cat >"$CC_TMP/i.c" <<'EOF2'
#define __stringify_1(x...) #x
#define __stringify(x...) __stringify_1(x)
#define TRACE_INCLUDE_PATH sub/
#define TRACE_INCLUDE_FILE vt
#define __TRACE_INCLUDE(system) __stringify(TRACE_INCLUDE_PATH/system.h)
#define TRACE_INCLUDE(system) __TRACE_INCLUDE(system)
const char *s = TRACE_INCLUDE(TRACE_INCLUDE_FILE);
#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)
EOF2
want=$(gcc -E -P -I"$CC_TMP/inc" "$CC_TMP/i.c")
if ! got=$("$CC_BIN" -E -P -I"$CC_TMP/inc" "$CC_TMP/i.c" 2>"$CC_TMP/i.err"); then
	echo "run_pp_avoid_paste: a stringified include: $(head -1 "$CC_TMP/i.err")"
	exit 1
fi
if [ "$got" != "$want" ]; then
	echo "run_pp_avoid_paste: a stringified include gives [$got], gcc's [$want]"
	exit 1
fi
echo "run_pp_avoid_paste: ok"
