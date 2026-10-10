#!/bin/sh
# as_relax.c by itself: the branches it finds in a source, the passes it
# takes to settle their sizes, and the sizes -- held to distances counted
# in the bytes the instructions really are.  See test_relax_core.c.
#
# What the assembler does with a branch is test_branch_relax.sh.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if ! cc -Wall -Wextra -Werror -D_GNU_SOURCE -I"$top/usr.bin/as" \
        "$top/usr.bin/as/as_expr.c" "$top/usr.bin/as/as_lexer.c" "$top/usr.bin/as/as_parser.c" \
        "$top/usr.bin/as/as_relax.c" "$here/test_relax_core.c" \
        -o "$work/check" 2> "$work/build.err"; then
    echo "FAIL: the test of as_relax.c does not build"
    head -5 "$work/build.err"
    exit 1
fi

"$work/check" "$work" || exit 1
echo "ok: as_relax.c"
