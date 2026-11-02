#!/bin/sh
# Macro expansion is bounded only as far as gcc's is: Linux's
# pack_fields() (include/linux/packing.h) nests CHECK_PACKED_FIELDS_N N
# macros deep, N up to 50, and one statement of ice_common.c expands to
# some 1.4 million tokens, past cc's bounds of 32 levels and a million
# tokens ("macro expansion depth exceeded", then "macro expansion token
# growth limit exceeded").  A chain of 50 compiles, as gcc compiles it,
# and an expansion of over a million tokens is gcc's, token for token.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_deep_large_expansion: skip (no gcc)"
	exit 0
fi

# a chain of 50, each level a macro using the one below, as CHECK_PACKED_FIELDS_N
{
	echo "#define SZ(a) (sizeof(a) / sizeof((a)[0]))"
	echo "#define ONE(f, i) (void)sizeof(char[1 - 2 * ((i) >= SZ(f))])"
	echo "#define C_1(f) do { ONE(f, 0); } while (0)"
	n=2
	while [ $n -le 50 ]; do
		echo "#define C_$n(f) do { C_$((n - 1))(f); ONE(f, $((n - 1))); } while (0)"
		n=$((n + 1))
	done
	echo "int t[60];"
	echo "void g(void) { C_50(t); }"
} >"$CC_TMP/chain.c"
gcc -c -o "$CC_TMP/g.o" "$CC_TMP/chain.c"
if ! "$CC_BIN" -c -o "$CC_TMP/c.o" "$CC_TMP/chain.c" 2>"$CC_TMP/c.err"; then
	echo "run_pp_deep_large_expansion: a chain of 50 macros refused: $(head -1 "$CC_TMP/c.err")"
	exit 1
fi

# 2^19 pairs of tokens from one macro
{
	echo "#define A0 x+"
	n=1
	while [ $n -le 19 ]; do
		echo "#define A$n A$((n - 1)) A$((n - 1))"
		n=$((n + 1))
	done
	echo "int y = A19 0;"
} >"$CC_TMP/grow.c"
gcc -E -P "$CC_TMP/grow.c" | tr -s ' \n' '\n\n' | grep -c . >"$CC_TMP/g.n"
if ! "$CC_BIN" -E -P -o "$CC_TMP/c.i" "$CC_TMP/grow.c" 2>"$CC_TMP/c.err"; then
	echo "run_pp_deep_large_expansion: an expansion of a million tokens refused: $(head -1 "$CC_TMP/c.err")"
	exit 1
fi
tr -s ' \n' '\n\n' <"$CC_TMP/c.i" | grep -c . >"$CC_TMP/c.n"
if ! cmp -s "$CC_TMP/g.n" "$CC_TMP/c.n"; then
	echo "run_pp_deep_large_expansion: $(cat "$CC_TMP/c.n") tokens, gcc's $(cat "$CC_TMP/g.n")"
	exit 1
fi
echo "run_pp_deep_large_expansion: ok"
