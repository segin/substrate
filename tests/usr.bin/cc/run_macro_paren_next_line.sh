#!/bin/sh
# A function-like macro's name at the end of a line, its `(` on a later
# one, is a call: new lines between them are white space (C11 6.10.3p10).
# ACPICA writes `return_ACPI_STATUS\n    (status);`, and cc took the name
# for an undeclared function -- a kernel with ACPI did not build.  The
# lines are now expanded together, blank lines keep the lines after them
# where they were (__LINE__), and a directive between the name and the
# `(` leaves the name alone, as gcc leaves it.  gcc is the judge.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_macro_paren_next_line: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>
#define F(x) ((x) + 1)
#define G(x) x
#define return_STATUS(s) return (s)
static int status_of(int k)
{
	if (k > 3)
		return_STATUS
		    (k * 2);
	return_STATUS /* a comment */
	(k);
}
int main(void)
{
	int a = F
	    (2);
	int b = F

	(3);
	int l1 = __LINE__;
	int c = F(1 +
		  1);
	int l2 = __LINE__;

	printf("%d %d %d %d %d %d %d\n", a, b, c, status_of(5), status_of(1), l1, l2);
	return 0;
}
EOF2
gcc -O1 -o "$CC_TMP/g" "$CC_TMP/m.c"
want=$("$CC_TMP/g")
for o in O0 O2; do
	if ! "$CC_BIN" -$o -o "$CC_TMP/c" "$CC_TMP/m.c" 2>"$CC_TMP/c.err"; then
		echo "run_macro_paren_next_line: -$o: $(head -1 "$CC_TMP/c.err")"
		exit 1
	fi
	got=$("$CC_TMP/c")
	if [ "$got" != "$want" ]; then
		echo "run_macro_paren_next_line: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done

# a directive between the name and the `(`: no call, in either compiler
printf '#define G(x) x\nint d = G\n#if 1\n(5)\n#endif\n;\n' >"$CC_TMP/d.c"
g=$(gcc -E -P "$CC_TMP/d.c" | tr -d ' \n')
c=$("$CC_BIN" -E -P "$CC_TMP/d.c" | tr -d ' \n')
if [ "$g" != "$c" ]; then
	echo "run_macro_paren_next_line: a directive between G and its ( gives [$c], gcc's [$g]"
	exit 1
fi
echo "run_macro_paren_next_line: ok"
