#!/bin/sh
# Many macros are looked up as fast as few.  The macro table was a sorted
# array sorted again at the lookup after every new #define, so a file
# whose headers define tens of thousands -- Linux's fs/ntfs/lcnalloc.c --
# took a minute to preprocess where gcc takes a tenth of a second.  60000
# defines, redefinitions, #undefs and uses: -E within 30 seconds, and its
# text gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_many_macros: skip (no gcc)"
	exit 0
fi

awk 'BEGIN {
	for (i = 0; i < 60000; i++) {
		printf "#define M_%d %d\n", i, i
		if (i % 7 == 0)
			printf "#undef M_%d\n#define M_%d (%d + 1)\n", i / 2, i / 2, i
		if (i % 1000 == 0)
			printf "int v_%d = M_%d + M_%d;\n", i, i, i / 3
	}
	printf "#undef M_5\nint last = M_59999 + M_14;\n#ifdef M_5\nint bad;\n#endif\n"
}' >"$CC_TMP/m.c"
gcc -E -P "$CC_TMP/m.c" | tr -s ' \t' '  ' | sed 's/^ //; s/ $//' | grep . >"$CC_TMP/g.out"
if ! timeout 30 "$CC_BIN" -E -P -o "$CC_TMP/c.raw" "$CC_TMP/m.c" 2>"$CC_TMP/c.err"; then
	echo "run_pp_many_macros: -E did not finish within 30 seconds: $(head -1 "$CC_TMP/c.err")"
	exit 1
fi
tr -s ' \t' '  ' <"$CC_TMP/c.raw" | sed 's/^ //; s/ $//' | grep . >"$CC_TMP/c.out"
if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/c.out"; then
	echo "run_pp_many_macros: differs from gcc (cc's on the right):"
	diff "$CC_TMP/g.out" "$CC_TMP/c.out" | grep '^[<>]' | head -10
	exit 1
fi
echo "run_pp_many_macros: ok"
