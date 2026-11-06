#!/bin/bash
# global! is v, and join! joins the lines as they are.  The `!` was not
# passed to either: g!/re/ acted as g/re/, and j! put spaces in.
#
# Usage: test_ex_force_variants.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_force.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

seq 1 5 >"$T/f"
got=$(printf 'g!/3/d\n%%p\nq!\n' | "$EX_BIN" -s "$T/f" 2>&1 | tr '\n' ' ')
[ "$got" = "3 " ] || fail "g!/3/d left [$got], expected [3]"
got=$(printf 'global!/[24]/d\n%%p\nq!\n' | "$EX_BIN" -s "$T/f" 2>&1 | tr '\n' ' ')
[ "$got" = "2 4 " ] || fail "global!/[24]/d left [$got], expected [2 4]"

printf 'a\nb\n  c\n' >"$T/j"
got=$(printf '1,3j!\n%%p\nq!\n' | "$EX_BIN" -s "$T/j" 2>&1)
[ "$got" = "ab  c" ] || fail "1,3j! gave [$got], expected [ab  c]"
got=$(printf '1,2j\n%%p\nq!\n' | "$EX_BIN" -s "$T/j" 2>&1 | head -1)
[ "$got" = "a b" ] || fail "1,2j gave [$got], expected [a b]"

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_force_variants"
