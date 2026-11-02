#!/bin/sh
# GNU's named variadic macro parameter may have white space before its
# `...`: Linux spells it `args ...` (esas2r_log.h, mv_sas.h), and cc,
# which took `...` only right after the name, refused the #define as
# malformed.  The preprocessed text is gcc's, but for white space.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_named_variadic_space: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/pp.h" <<'EOF2'
#define d1(f, args ...) p(f, ## args)
#define d2(fmt, arg	...)	p(fmt, ## arg)
#define d3(f, a...) p(f, ## a)
#define d4(args ...) q(args)
d1(1, 2, 3) d1(1) d2(4, 5) d3(6, 7) d4() d4(8, 9)
EOF2
gcc -E -P -x c "$CC_TMP/pp.h" | tr -d ' \t\n' >"$CC_TMP/g.i"
"$CC_BIN" -E -P -x c "$CC_TMP/pp.h" | tr -d ' \t\n' >"$CC_TMP/c.i"
if ! cmp -s "$CC_TMP/g.i" "$CC_TMP/c.i"; then
	echo "run_pp_named_variadic_space: preprocessed text differs from gcc's:"
	diff "$CC_TMP/g.i" "$CC_TMP/c.i" || true
	exit 1
fi
echo "run_pp_named_variadic_space: ok"
