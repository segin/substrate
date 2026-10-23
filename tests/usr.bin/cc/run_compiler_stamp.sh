#!/bin/sh
# The compiler's name is in .comment, as gcc's .ident is, and nowhere a
# program loads.  It was a string in .rodata: 26 bytes in every object's
# image, which Linux's 16-bit setup code, at most 32 KiB, paid twenty
# times over.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

printf 'int counter;\n' > "$CC_TMP/stamp.c"
status=0
for m in 32 64; do
	"$CC_BIN" -m$m -S -o "$CC_TMP/stamp.s" "$CC_TMP/stamp.c"
	if ! grep -q '^[[:space:]]*\.ident "Substrate C Compiler' "$CC_TMP/stamp.s"; then
		echo "FAIL -m$m: no .ident naming the compiler"
		status=1
	fi
	if grep -q '\.asciz "Substrate C Compiler' "$CC_TMP/stamp.s"; then
		echo "FAIL -m$m: the compiler's name is a string of the program's"
		status=1
	fi
done
[ "$status" -eq 0 ] && echo "run_compiler_stamp: ok"
exit "$status"
