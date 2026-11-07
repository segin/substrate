#!/bin/bash
# \< and \> in a search that starts inside a line (EXVI-83).  After the
# first match of s///g, and in vi's searches, the rest of the line was
# matched as a string of its own, with no character before it, so \<
# matched inside a word: s/\<a/X/g on "aa a ba" gave "XX X ba".  Expected
# results are Vim's.
#
# Usage: test_ex_word_bound.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_word_bound.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

cd "$T" || exit 1

# check COMMAND EXPECTED: COMMAND on the line "aa a ba"
check() {
	printf 'aa a ba\n' >f
	out=$(printf '%s\n%%p\nq!\n' "$1" | "$EX_BIN" -s f 2>&1)
	if [ "$out" != "$2" ]; then
		echo "FAIL: $1: [$out], expected [$2]"
		fails=$((fails + 1))
	fi
}

check 's/\<a/X/g' 'Xa X ba'
check 's/a\>/X/g' 'aX X bX'
check 's/\<./X/g' 'Xa X Xa'
check 's/\<a/X/' 'Xa a ba'
check 's/b\|\<a/X/g' 'Xa X Xa'
check 's/\<a\|b/X/g' 'Xa X Xa'

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_word_bound"
