#!/bin/sh
# A diagnostic gives the line it is of, whatever directives came before
# it.  The preprocessor wrote nothing for the lines a directive or a
# skipped group took, and the compiler counted the lines it was given:
# an error after a #define, an #if group or an #include was reported
# that many lines early.  It now brings the output to the line, as gcc
# does: blank lines for a few, a line marker for more.  The line of
# each error is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_diag_line_after_directive: skip (no gcc)"
	exit 0
fi

mkdir -p "$CC_TMP/inc"
printf 'int from_header;\n#define H 1\n\n\n' >"$CC_TMP/inc/h.h"

# case NAME: the file NAME.c, with one error; the line both compilers give it
case_line() {
	g=$(gcc -c -o /dev/null -I"$CC_TMP/inc" "$CC_TMP/$1.c" 2>&1 | grep -m1 'error' | cut -d: -f2)
	c=$("$CC_BIN" -c -o /dev/null -I"$CC_TMP/inc" "$CC_TMP/$1.c" 2>&1 | grep -m1 'error' | cut -d: -f2 || true)
	if [ -z "$g" ] || [ "$c" != "$g" ]; then
		echo "run_diag_line_after_directive: $1: cc gives line [$c], gcc [$g]"
		"$CC_BIN" -c -o /dev/null -I"$CC_TMP/inc" "$CC_TMP/$1.c" 2>&1 | head -3
		exit 1
	fi
}

printf '#define F(x)\t\\\n\tdo { \\\n\t\tg(x); \\\n\t} while (0)\nint a;\nint d = ;\n' >"$CC_TMP/define.c"
case_line define

printf 'int a;\n#if 0\nint b;\nint c;\n#endif\nint d = ;\n' >"$CC_TMP/short_if.c"
case_line short_if

{
	printf 'int a;\n#if 0\n'
	i=0
	while [ $i -lt 30 ]; do
		printf 'int skipped%d;\n' $i
		i=$((i + 1))
	done
	printf '#else\nint e;\n#endif\n\n#define G 2\nint d = ;\n'
} >"$CC_TMP/long_if.c"
case_line long_if

printf '#include "h.h"\n#define X 3\n\nint a = H + X;\nint d = ;\n' >"$CC_TMP/include.c"
case_line include

printf 'int a;\n#line 100\nint b;\n#define Y 4\nint d = ;\n' >"$CC_TMP/line.c"
case_line line

# a #line naming a file: the error is of that file's line
printf 'int a;\n#line 200 "other.c"\n#define Z 5\n\nint d = ;\n' >"$CC_TMP/linefile.c"
g=$(gcc -c -o /dev/null "$CC_TMP/linefile.c" 2>&1 | grep -m1 'error' | cut -d: -f1,2)
c=$("$CC_BIN" -c -o /dev/null "$CC_TMP/linefile.c" 2>&1 | grep -m1 'error' | cut -d: -f1,2 || true)
if [ "$c" != "$g" ]; then
	echo "run_diag_line_after_directive: #line with a file: cc gives [$c], gcc [$g]"
	exit 1
fi
echo "run_diag_line_after_directive: ok"
