#!/bin/bash
# A NUL byte in a line read from a file stays: the line was cut at it on
# reading (and every undo snapshot copied lines up to it), so "ab\0cd" was
# written back as "ab".  p and l show it as ^@.  vim -es agrees on each
# result below.
#
# Usage: test_ex_nul.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_nul.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

# check CMDS WANT: run CMDS (printf %b) then wq on a file "ab\0cd\nx\n";
# want the file to be WANT (printf %b), compared byte for byte
check() {
	local cmds="$1" want="$2"

	printf 'ab\0cd\nx\n' >"$T/f"
	printf '%b\nwq\n' "$cmds" | "$EX_BIN" -s "$T/f" >/dev/null 2>&1
	printf '%b' "$want" >"$T/want"
	if ! cmp -s "$T/f" "$T/want"; then
		fail "$cmds: [$(od -c "$T/f" | head -2 | tr -s ' ' | tr '\n' '|')]"
	fi
}

check '' 'ab\0cd\nx\n'
check '2s/x/y/' 'ab\0cd\ny\n'
check '1s/a/A/' 'Ab\0cd\nx\n'
check '2d\nu' 'ab\0cd\nx\n'
check '1t$' 'ab\0cd\nx\nab\0cd\n'
check '1m$' 'x\nab\0cd\n'
check '1y\n$put' 'ab\0cd\nx\nab\0cd\n'
check '1,2j' 'ab\0cd x\n'

got=$(printf 'ab\0cd\n' >"$T/f"; printf '1p\n1l\nq!\n' | "$EX_BIN" -s "$T/f" 2>&1 | tr '\n' ' ')
[ "$got" = 'ab^@cd ab^@cd$ ' ] || fail "p, l: [$got], expected [ab^@cd ab^@cd\$]"

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_nul"
