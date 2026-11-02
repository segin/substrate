#!/bin/sh
# The content of __VA_OPT__ is replaced as the rest of the body is: the
# macro's parameters in it take their arguments, and `##` in it pastes.
# cc replaced only __VA_ARGS__ there, so Linux's xe_args.h, with
# `#define IF_ARGS(then, else, ...) FIRST_ARG(__VA_OPT__(then,) else)`,
# left `then` in xe_guc.c's GUC_FIRMWARE_VER_AT_LEAST(), refused as an
# undeclared identifier.  The preprocessed text is gcc's, but for white
# space.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_va_opt_params: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/pp.h" <<'EOF2'
#define F(a, ...) a
#define IF(then, else, ...) F(__VA_OPT__(then,) else)
#define G(x, ...) [__VA_OPT__(x - __VA_ARGS__ ; x)]
#define H(p, ...) __VA_OPT__(p##_tail)
#define S(p, ...) __VA_OPT__(#p)
IF(1, 2) IF(1, 2, z) IF(1, 2, ) G(k) G(k, m, n) H(q) H(q, 1) S(w) S(w, 1)
EOF2
gcc -std=gnu2x -E -P -x c "$CC_TMP/pp.h" | tr -d ' \t\n' >"$CC_TMP/g.i"
"$CC_BIN" -E -P -x c "$CC_TMP/pp.h" | tr -d ' \t\n' >"$CC_TMP/c.i"
if ! cmp -s "$CC_TMP/g.i" "$CC_TMP/c.i"; then
	echo "run_pp_va_opt_params: preprocessed text differs from gcc's:"
	diff "$CC_TMP/g.i" "$CC_TMP/c.i" || true
	exit 1
fi
echo "run_pp_va_opt_params: ok"
