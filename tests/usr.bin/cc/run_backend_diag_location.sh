#!/bin/sh
# An error the code generator finds has a place, as gcc's has: the asm
# statement's line and column for one in an asm (an impossible
# constraint, an operand number out of range), the call's for a call
# whose result is in a register the options forbid, and the function's
# name for its own such result.  cc printed "cc: error: ..." with no
# file and no line at all.  The file, line and column are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_backend_diag_location: skip (no gcc)"
	exit 0
fi

# (indented with spaces: gcc counts a tab to the next multiple of 8, cc as one column)
printf 'long double h(long double a)\n{\n    return a * 2;\n}\n' >"$CC_TMP/ret.c"
printf 'extern long double g(int);\nint c(int x)\n{\n    int y = x;\n\n    return g(y) > 1;\n}\n' >"$CC_TMP/call.c"
printf 'int z(int x)\n{\n    int r;\n\n    __asm__("" : "=r"(r) : "i"(x));\n    return r;\n}\n' >"$CC_TMP/imm.c"
printf 'int q(int x)\n{\n    int r;\n\n    __asm__("%%5" : "=r"(r) : "r"(x));\n    return r;\n}\n' >"$CC_TMP/idx.c"

# the place of the first error: file:line:col
place() {
	"$@" -S -o /dev/null 2>&1 | grep -m1 'error:' | sed -n 's/^\([^ :]*:[0-9]*:[0-9]*\): error:.*/\1/p'
}

cd "$CC_TMP"
for t in "ret.c -m64 -mno-80387" "call.c -m64 -mno-80387" "imm.c -m64 -O0" "imm.c -m64 -O2" "imm.c -m32 -O2" \
	"idx.c -m64" "idx.c -m32 -O2"; do
	set -- $t
	g=$(place gcc "$@")
	c=$(place "$CC_BIN" "$@")
	if [ -z "$g" ]; then
		echo "run_backend_diag_location: $t: gcc gives no error"
		exit 1
	fi
	if [ "$c" != "$g" ]; then
		echo "run_backend_diag_location: $t: cc's error is at [$c], gcc's at [$g]"
		"$CC_BIN" -S -o /dev/null "$@" 2>&1 | head -2
		exit 1
	fi
done
echo "run_backend_diag_location: ok"
