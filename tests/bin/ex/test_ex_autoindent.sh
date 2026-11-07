#!/bin/bash
# autoindent in ex's text input, and a!, i!, c! (EXVI-54.1, 54.2).
# ex took the lines entered as typed whatever autoindent was, and a! was
# "Unknown command".  Each line entered starts with the indent of the line
# before it -- the one appended after, inserted before, or first changed,
# then each line entered; ^D at a line's start takes back a shiftwidth, 0^D
# all of it, ^^D all of it for that line only.  Expected results are Vim's.
#
# Usage: test_ex_autoindent.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_autoindent.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

cd "$T" || exit 1

# check NAME SCRIPT EXPECTED: SCRIPT (printf format; wq added) on
# "\tfoo" "bar", expecting the file EXPECTED (printf format) -- the file,
# as p shows a tab as spaces
check() {
	printf '\tfoo\nbar\n' >f
	printf "$2"'wq\n' | "$EX_BIN" -s f >/dev/null 2>&1
	out=$(cat f)
	want=$(printf "$3")
	if [ "$out" != "$want" ]; then
		echo "FAIL: $1: [$(printf '%s' "$out" | od -c | sed -n 1,2p | tr -s ' ')]"
		fails=$((fails + 1))
	fi
}

check "a" 'set ai\n1a\nx\ny\n.\n' '\tfoo\n\tx\n\ty\nbar'
check "a, blanks typed" 'set ai\n1a\n  x\ny\n.\n' '\tfoo\n\t  x\n\t  y\nbar'
check "i" 'set ai\n2i\nx\n.\n' '\tfoo\nx\nbar'
check "c" 'set ai\n1c\nx\n.\n' '\tx\nbar'
check "noautoindent" '1a\nx\n.\n' '\tfoo\nx\nbar'
check "a! with noautoindent" '1a!\nx\n.\n' '\tfoo\n\tx\nbar'
check "a! with autoindent" 'set ai\n1a!\nx\n.\n' '\tfoo\nx\nbar'
check "^D" 'set ai sw=4\n1a\n\004x\ny\n.\n' '\tfoo\n    x\n    y\nbar'
check "0^D" 'set ai\n1a\n0\004x\ny\n.\n' '\tfoo\nx\ny\nbar'
check "^^D" 'set ai\n1a\n^\004x\ny\n.\n' '\tfoo\nx\n\ty\nbar'

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_autoindent"
