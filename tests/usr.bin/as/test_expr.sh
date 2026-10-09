#!/bin/sh
# The assembler's one expression parser and evaluator, on its own: the
# value of each expression against GNU as's, the symbol-plus-addend form
# relocations are made from, and the bounds on what it will take.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

${CC:-cc} -std=gnu17 -O1 -Wall -Wextra -Werror -o "$work/test_expr" \
    -I"$top/usr.bin/as" "$here/test_expr.c" "$top/usr.bin/as/as_expr.c" ||
    { echo "FAIL: the expression test does not build"; exit 1; }

"$work/test_expr"
