#!/bin/sh
# Only parentheses group a macro's arguments (C11 6.10.3p11): a comma in
# braces or brackets parts them, as gcc parts them.  cc grouped braces and
# brackets too, so `V({1, 2}, 3)` gave its first parameter `{1, 2}` where
# gcc's gets `{1`, an argument count came out one short, and `F({1, 2})`
# of a one-parameter F was taken where gcc refuses it.  The -E text of
# each call is gcc's, and the refusal is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_args_brace_comma: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/a.c" <<'EOF'
#define COUNT(...) COUNT_(__VA_ARGS__, 6, 5, 4, 3, 2, 1, 0)
#define COUNT_(a, b, c, d, e, f, n, ...) n
#define FIRST(a, ...) [a] [__VA_ARGS__]
#define TWO(a, b) <a|b>
#define S(...) #__VA_ARGS__
c1 COUNT({1, 2})
c2 COUNT([0] = 1, [1] = 2)
c3 COUNT(f(1, 2), {3})
f1 FIRST({1, 2}, 3)
f2 FIRST(x[1, 2])
t1 TWO({a, b})
t2 TWO((a, b), [c])
s1 S({1, 2}, [3])
EOF
gcc -E -P "$CC_TMP/a.c" | tr -s ' \t' '  ' | sed 's/^ //; s/ $//' | grep . >"$CC_TMP/g.out"
"$CC_BIN" -E -P -o "$CC_TMP/c.raw" "$CC_TMP/a.c"
tr -s ' \t' '  ' <"$CC_TMP/c.raw" | sed 's/^ //; s/ $//' | grep . >"$CC_TMP/c.out"
if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/c.out"; then
	echo "run_pp_args_brace_comma: differs from gcc (cc's on the right):"
	diff "$CC_TMP/g.out" "$CC_TMP/c.out" | grep '^[<>]' | head -20
	exit 1
fi

printf '#define F(x) x\nint v[] = F({1, 2});\n' >"$CC_TMP/n.c"
if gcc -E "$CC_TMP/n.c" >/dev/null 2>&1; then
	echo "run_pp_args_brace_comma: gcc took F({1, 2}); the test is wrong"
	exit 1
fi
if "$CC_BIN" -E -o "$CC_TMP/n.i" "$CC_TMP/n.c" 2>/dev/null; then
	echo "run_pp_args_brace_comma: F({1, 2}) of a one-parameter F is taken; gcc refuses it"
	exit 1
fi
echo "run_pp_args_brace_comma: ok"
