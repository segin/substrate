#!/bin/bash
# s///g goes on matching from after each match, inside the line, where `^`
# does not match: it searched the rest as a string of its own, so
# %s/^/>/g on "ab" gave ">a>b>" and s/^a/X/g on "aaa" gave "XXX".  vim -es
# agrees on each result below.
#
# Usage: test_ex_sub_bol.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_sub_bol.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

# check TEXT CMD WANT: run CMD on a file of TEXT (printf %b), want its lines WANT, space-joined
check() {
	local text="$1" cmd="$2" want="$3" got

	printf '%b' "$text" >"$T/f"
	got=$(printf '%s\n%%p\nq!\n' "$cmd" | "$EX_BIN" -s "$T/f" 2>&1 | tr '\n' ' ')
	if [ "$got" != "$want " ]; then
		fail "$cmd: [$got], expected [$want]"
	fi
}

check 'ab\n' '%s/^/>/g' '>ab'
check 'aaa\n' 's/^a/X/g' 'Xaa'
check 'abab\n' 's/^ab/X/g' 'Xab'
check 'ab\ncd\n' '%s/^./[&]/g' '[a]b [c]d'
check 'aaa\n' 's/a$/X/g' 'aaX'

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_sub_bol"
