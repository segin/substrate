#!/bin/sh
# Preprocessed assembly keeps its strings as written.  After
# preprocessing a .S file cc read its output back and, inside each quoted
# string, turned `\\` into `\` and `\t` into a tab: `.ascii "a\\b"`
# reached the assembler as "a\b", a backspace, where gcc's has a
# backslash and a b.  And the read-back needed a file, so -E of a .S file
# to standard output failed ("failed to size preprocessed assembly
# output").  The -E text, to a file and to a pipe, is gcc's, and the
# data assembled is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_asm_strings: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
export AS

cat >"$CC_TMP/s.S" <<'EOF'
#define S(x) #x
	.data
	.ascii "a\\b\tc\n"
	.ascii "q\"uote\\\\"
	.asciz S(p\q)
	.byte '\\', '\t'
	.ascii "\\t is not a tab\\\\"
EOF
norm() { sed 's/^[ \t]*//' | grep -v '^$'; }
gcc -E -P "$CC_TMP/s.S" | norm >"$CC_TMP/g.out"
"$CC_BIN" -E -P -o "$CC_TMP/c.raw" "$CC_TMP/s.S"
norm <"$CC_TMP/c.raw" >"$CC_TMP/c.out"
if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/c.out"; then
	echo "run_pp_asm_strings: -E to a file differs from gcc (cc's on the right):"
	diff "$CC_TMP/g.out" "$CC_TMP/c.out" | grep '^[<>]' | head -10
	exit 1
fi
if ! "$CC_BIN" -E -P "$CC_TMP/s.S" 2>"$CC_TMP/c.err" | norm >"$CC_TMP/p.out" || [ -s "$CC_TMP/c.err" ]; then
	echo "run_pp_asm_strings: -E to a pipe failed: $(head -1 "$CC_TMP/c.err")"
	exit 1
fi
if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/p.out"; then
	echo "run_pp_asm_strings: -E to a pipe differs from gcc"
	exit 1
fi
gcc -c -o "$CC_TMP/g.o" "$CC_TMP/s.S"
"$CC_BIN" -c -o "$CC_TMP/c.o" "$CC_TMP/s.S"
objcopy -O binary -j .data "$CC_TMP/g.o" "$CC_TMP/g.bin"
objcopy -O binary -j .data "$CC_TMP/c.o" "$CC_TMP/c.bin"
if ! cmp -s "$CC_TMP/g.bin" "$CC_TMP/c.bin"; then
	echo "run_pp_asm_strings: the data is $(od -An -c "$CC_TMP/c.bin" | tr -s ' '), gcc's $(od -An -c "$CC_TMP/g.bin" | tr -s ' ')"
	exit 1
fi
echo "run_pp_asm_strings: ok"
