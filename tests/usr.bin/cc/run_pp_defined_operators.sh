#!/bin/sh
# The preprocessor's operators count as defined, as gcc counts them:
# `#ifdef __has_include` and `#if defined(__has_attribute)` are how code
# asks before using one, and cc answered no, so such code took its
# fallback.  Each operator cc has is defined for defined(), #ifdef,
# #ifndef, #elifdef and #elifndef as with gcc; __has_declspec_attribute is
# not, nor is a name that is no operator.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_defined_operators: skip (no gcc)"
	exit 0
fi

{
	for n in __has_include __has_include_next __has_attribute __has_c_attribute __has_builtin \
		__has_feature __has_extension __has_embed __has_declspec_attribute __has_nothing; do
		printf '#if defined(%s)\nd_%s yes\n#else\nd_%s no\n#endif\n' "$n" "$n" "$n"
		printf '#if defined %s\nb_%s yes\n#else\nb_%s no\n#endif\n' "$n" "$n" "$n"
		printf '#ifdef %s\ni_%s yes\n#else\ni_%s no\n#endif\n' "$n" "$n" "$n"
		printf '#ifndef %s\nn_%s no\n#else\nn_%s yes\n#endif\n' "$n" "$n" "$n"
		printf '#if 0\n#elifdef %s\ne_%s yes\n#else\ne_%s no\n#endif\n' "$n" "$n" "$n"
		printf '#if 0\n#elifndef %s\nf_%s no\n#else\nf_%s yes\n#endif\n' "$n" "$n" "$n"
	done
} >"$CC_TMP/d.c"
gcc -std=c23 -E -P "$CC_TMP/d.c" | grep . >"$CC_TMP/g.out"
"$CC_BIN" -std=c23 -E -P -o "$CC_TMP/c.raw" "$CC_TMP/d.c"
grep . "$CC_TMP/c.raw" | sed 's/^ *//' >"$CC_TMP/c.out"
if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/c.out"; then
	echo "run_pp_defined_operators: differs from gcc (cc's on the right):"
	diff "$CC_TMP/g.out" "$CC_TMP/c.out" | grep '^[<>]' | head -20
	exit 1
fi
echo "run_pp_defined_operators: ok"
