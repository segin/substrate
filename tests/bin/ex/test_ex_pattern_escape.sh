#!/bin/bash
# A backslash in a delimited pattern goes only where it escapes the
# delimiter; before anything else it is the pattern's own (POSIX ex).  Every
# backslash was stripped, so s/\./X/g replaced every character, and global's
# pattern ended at the first delimiter, escaped or not, so g/a\/b/ found
# nothing.  vim -es agrees on each result below.
#
# Usage: test_ex_pattern_escape.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_pattern_escape.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

# check TEXT CMD WANT: run CMD on a file of TEXT (printf %b), want the lines WANT, space-joined
check() {
	local text="$1" cmd="$2" want="$3" got

	printf '%b' "$text" >"$T/f"
	got=$(printf '%s\n%%p\nq!\n' "$cmd" | "$EX_BIN" -s "$T/f" 2>&1 | tr '\n' ' ')
	if [ "$got" != "$want " ]; then
		fail "$cmd: [$got], expected [$want]"
	fi
}

check 'a.b axb\n' 's/\./X/g' 'aXb axb'
check 'a/b\n' 's/a\/b/X/' 'X'
check 'a\\b\n' 's/\\/X/' 'aXb'
check 'a.b\n' 's,\.,\,,' 'a,b'
check 'a/b\nab\nc\n' 'g/a\/b/d' 'ab c'
check 'x.y\nxzy\n' 'g/x\.y/d' 'xzy'
check 'x.y\nxzy\n' 'v/x\.y/d' 'x.y'
check 'x\na/b\nz\n' '/a\/b/d' 'x z'
check 'x\na.b\naxb\n' '/a\.b/d' 'x axb'

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_pattern_escape"
