#!/bin/sh
# A macro's replacement that ends in a function-like macro's name takes
# that macro's arguments from the text after it, as written (C11
# 6.10.3.4p1): `I(F)(T)` with I(x) x and F(a) a##_s is T_s.  cc expanded
# the `(T)` as text of its own before F met it, so a `##` operand among the
# arguments was pasted expanded: `(1)_s`.  Linux's nouveau NVDEF(),
# `NVDEF_(X, ##A, NVDEF_I, NVDEF_N)(X, ##A)` choosing a macro to paste
# `d##_##r##_##f##_##v` with v = TRUE, came out `..._PD_(1)`.  The -E text
# of each is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_trailing_function_macro: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/a.c" <<'EOF2'
#define TRUE (1)
#define F(a) a##_s
#define I(x) x
#define K(x, y) y
#define G F
#define H(x) x G
#define L 1 F
#define W(z) I(F)(z)
#define NVVAL_X(drf, v) (((v) & 1) << drf)
#define NVDEF_N(X, d, r, f, v) NVVAL_X(d##_##r##_##f, d##_##r##_##f##_##v)
#define NVDEF_I(X, d, r, f, i, v) NVVAL_X(d##_##r##_##f(i), d##_##r##_##f##_##v)
#define NVDEF_(X, _1, _2, _3, _4, _5, IMPL, ...) IMPL
#define NVDEF(A...) NVDEF_(X, ##A, NVDEF_I, NVDEF_N)(X, ##A)
a1 I(F)(TRUE)
a2 K(1, F)(TRUE)
a3 G(TRUE)
a4 H(1)(TRUE)
a5 L(TRUE)
a6 W(x)
a7 I(F) (TRUE) I(F)
a8 NVDEF(NV0073, CTRL_DFP, PD, TRUE)
a9 NVDEF(NV0073, CTRL_DFP, PD, 2, TRUE)
EOF2
gcc -E -P "$CC_TMP/a.c" | tr -s ' \t' '  ' | sed 's/^ //; s/ $//' | grep . >"$CC_TMP/g.out"
"$CC_BIN" -E -P -o "$CC_TMP/c.raw" "$CC_TMP/a.c"
tr -s ' \t' '  ' <"$CC_TMP/c.raw" | sed 's/^ //; s/ $//' | grep . >"$CC_TMP/c.out"
if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/c.out"; then
	echo "run_pp_trailing_function_macro: differs from gcc (cc's on the right):"
	diff "$CC_TMP/g.out" "$CC_TMP/c.out" | grep '^[<>]' | head -20
	exit 1
fi
echo "run_pp_trailing_function_macro: ok"
