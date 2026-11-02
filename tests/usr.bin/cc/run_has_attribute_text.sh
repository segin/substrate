#!/bin/sh
# gcc's __has_attribute and __has_builtin are 1 or 0 outside #if too:
# Linux's trace.c tests `IS_ENABLED(...) && __has_attribute(btf_type_tag)`
# in a statement, and cc left the operator in the text, where it was an
# undeclared identifier.  The preprocessed text is gcc's, but for white
# space, through a macro too.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_has_attribute_text: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/pp.h" <<'EOF2'
int a = __has_attribute(packed), b = __has_attribute(nonexistent_attr);
int c = __has_builtin(__builtin_expect), d = __has_builtin(nope);
int e = __has_attribute( aligned ) + __has_builtin (__builtin_expect);
#if __has_attribute(packed) && !__has_builtin(nope)
int f = 1;
#endif
#define HA(x) __has_attribute(x)
int g = HA(used);
EOF2
gcc -E -P -x c "$CC_TMP/pp.h" | tr -d ' \t\n' >"$CC_TMP/g.i"
"$CC_BIN" -E -P -x c "$CC_TMP/pp.h" | tr -d ' \t\n' >"$CC_TMP/c.i"
if ! cmp -s "$CC_TMP/g.i" "$CC_TMP/c.i"; then
	echo "run_has_attribute_text: preprocessed text differs from gcc's:"
	diff "$CC_TMP/g.i" "$CC_TMP/c.i" || true
	exit 1
fi
echo "run_has_attribute_text: ok"
