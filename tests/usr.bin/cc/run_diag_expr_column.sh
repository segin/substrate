#!/bin/sh
# A semantic error in an expression is placed where gcc places it: at an
# identifier itself ("undeclared"), at an operator's token for what the
# operator does -- a binary operator, an assignment's `=`, unary `*`, a
# member's `.` or `->`, a subscript's `[` -- and at a call's callee.  cc
# gave every one the column of the statement it was in, and an error an
# operator found after checking its operands the column of the operand
# checked last.  The line and column are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_diag_expr_column: skip (no gcc)"
	exit 0
fi
cd "$CC_TMP"

n=0
check() {
	n=$((n + 1))
	printf '%b\n' "$1" >col$n.c
	g=$(gcc -std=gnu17 -c -o /dev/null col$n.c 2>&1 | grep -m1 'error:' | cut -d: -f1-3)
	c=$("$CC_BIN" -c -o /dev/null col$n.c 2>&1 | grep -m1 'error:' | cut -d: -f1-3)
	if [ -z "$g" ] || [ "$c" != "$g" ]; then
		echo "run_diag_expr_column: [$1]: cc's error at [$c], gcc's at [$g]"
		exit 1
	fi
}

# (indented with spaces: gcc counts a tab to the next multiple of 8, cc as one column)
check 'int g(void)\n{\n    return 1 + nope;\n}'
check 'int g(void)\n{\n    int x = 2;\n    x = x * zork + 1;\n    return x;\n}'
check 'struct s { int a; };\nint g(struct s v)\n{\n    return v.b + 1;\n}'
check 'struct s { int a; };\nint g(struct s *v)\n{\n    return 3 + v->b;\n}'
check 'int g(int x)\n{\n    return x(1);\n}'
check 'int f(int, int);\nint g(void)\n{\n    return 2 + f(1);\n}'
check 'int g(void)\n{\n    const int k = 1;\n    k = 2;\n    return k;\n}'
check 'struct s { int a; };\nint g(struct s v)\n{\n    return 1 + (v * 2);\n}'
check 'int g(int *p)\n{\n    return *p + *3;\n}'
check 'int g(void)\n{\n    if (undeclared_q)\n        return 1;\n    return 0;\n}'
check 'int g(int a)\n{\n    return a ? nope2 : 0;\n}'
check 'int arr[3];\nint g(void)\n{\n    return arr[ix];\n}'
check 'int g(void)\n{\n    int y = 0;\n    y += nope3;\n    return y;\n}'
echo "run_diag_expr_column: ok"
