#!/bin/sh
# Floating-point programs that are not C: a bitwise operator, a pointer
# offset, a computed goto or a cast to a pointer of a floating value, a
# floating vector's %, a decimal and a binary floating operand mixed,
# _FloatN redeclared as another type, and C23 constexpr initializers a
# float cannot be (inexact, out of range, an integer's from a float, not
# constant).  Each is rejected by gcc, the judge, and by cc with its own
# diagnostic.  These cases sat in this directory with nothing running
# them.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

fails=0
# file, -std, the diagnostic cc gives
while IFS='|' read -r f std want; do
	[ -n "$f" ] || continue
	if command -v gcc >/dev/null 2>&1 && gcc -std="$std" -c "$HERE/$f" -o "$CC_TMP/g.o" 2>/dev/null; then
		echo "run_fp_sema_negative: gcc accepts $f, which is no longer a negative case"
		fails=$((fails + 1))
		continue
	fi
	if "$CC_BIN" -std="$std" -c "$HERE/$f" -o "$CC_TMP/c.o" 2>"$CC_TMP/c.err"; then
		echo "run_fp_sema_negative: cc accepts $f"
		fails=$((fails + 1))
	elif ! grep -qF "$want" "$CC_TMP/c.err"; then
		echo "run_fp_sema_negative: $f: cc says [$(grep -m1 error "$CC_TMP/c.err")], not [$want]"
		fails=$((fails + 1))
	fi
done <<'EOF2'
native_bad_bitwise_float.c|gnu17|bitwise operators require integer operands
native_bad_cast_ptr_float.c|gnu17|cast currently supports numeric and pointer/integer con
native_bad_computed_goto_float.c|gnu17|computed goto requires pointer or integer target
native_bad_ptr_add_float.c|gnu17|unsupported pointer arithmetic form
native_bad_vector_float_mod.c|gnu17|invalid operands to an integer operator: a floating vec
native_bad_c23_constexpr_float.c|c23|'constexpr' integer initializer is not an integer const
native_bad_c23_constexpr_float_inexact.c|c23|'constexpr' initializer not representable in type of ob
native_bad_c23_constexpr_float_int.c|c23|'constexpr' initializer not representable in type of ob
native_bad_c23_constexpr_float_nonconst.c|c23|initializer element is not constant
native_bad_c23_constexpr_float_range.c|c23|'constexpr' initializer not representable in type of ob
native_bad_c23_constexpr_member_float.c|c23|'constexpr' initializer not representable in type of ob
native_bad_c23_float128_decimal_mix.c|c23|cannot mix operands of decimal floating and other float
native_bad_c23_floatn_redecl.c|c23|conflicting declarations for function: g
EOF2
if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "run_fp_sema_negative: ok"
