#!/bin/sh
# `#` spells its argument as the standard says (C11 6.10.3.2p2), as gcc
# does: each run of white space between tokens one space, none at either
# end, and `\` and `"` escaped only inside string literals and character
# constants.  cc escaped every backslash, so `#x` of `p\q` was "p\\q"
# where gcc's is "p\q", and wrote a tab or newline in the argument as
# `\t` or `\n`.  The -E text of each case is gcc's, and a program printing
# them prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_stringify_spelling: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/s.c" <<'EOF'
#include <stdio.h>
#define S(x) #x
#define V(...) #__VA_ARGS__
const char *t[] = {
	S(p\q), S("a\n" "b\\c"), S(  a 	  b	), S(x
	  y), S('"'), S('\\'), S('\''),
	S("q\"uote"), V(1,  2,	3), S(), S(a\b "\\" c\), S(L"w" u8"x")
};
int main(void) { unsigned i; for (i = 0; i < sizeof t / sizeof *t; i++) printf("[%s]\n", t[i]); return 0; }
EOF
gcc -E -P "$CC_TMP/s.c" | sed -n '/^const char \*t/,/^};/p' | tr -s ' \t\n' '   ' >"$CC_TMP/g.out"
"$CC_BIN" -E -P -o "$CC_TMP/c.raw" "$CC_TMP/s.c"
sed -n '/^const char \*t/,/^};/p' "$CC_TMP/c.raw" | tr -s ' \t\n' '   ' >"$CC_TMP/c.out"
if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/c.out"; then
	echo "run_pp_stringify_spelling: -E gives [$(cat "$CC_TMP/c.out")], gcc's [$(cat "$CC_TMP/g.out")]"
	exit 1
fi
gcc -w -o "$CC_TMP/g" "$CC_TMP/s.c"
"$CC_BIN" -w -c -o "$CC_TMP/c.o" "$CC_TMP/s.c"
gcc -no-pie -o "$CC_TMP/c" "$CC_TMP/c.o"
if [ "$("$CC_TMP/c")" != "$("$CC_TMP/g")" ]; then
	echo "run_pp_stringify_spelling: prints otherwise than gcc's"
	exit 1
fi
echo "run_pp_stringify_spelling: ok"
