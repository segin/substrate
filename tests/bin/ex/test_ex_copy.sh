#!/bin/bash
# copy (t, co) copies the addressed lines as they were before the command.
# It walked the source with ->next while inserting, so with the destination
# inside the range it ran onto the lines it had just put there: 2,4t3 on
# 1..10 gave 1 2 3 2 3 2 4 ... instead of 1 2 3 2 3 4 4 ....
#
# Usage: test_ex_copy.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_copy.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

check() {
	local cmd="$1" want="$2" got

	seq 1 6 >"$T/f"
	got=$(printf '%s\n%%p\nq!\n' "$cmd" | "$EX_BIN" -s "$T/f" 2>&1 | tr '\n' ' ')
	if [ "$got" != "$want " ]; then
		fail "$cmd: [$got], expected [$want]"
	fi
}

check '2,4t3' '1 2 3 2 3 4 4 5 6'
check '2,4t2' '1 2 2 3 4 3 4 5 6'
check '2,4t4' '1 2 3 4 2 3 4 5 6'
check '2,4co3' '1 2 3 2 3 4 4 5 6'
check '2,4t0' '2 3 4 1 2 3 4 5 6'
check '2,4t$' '1 2 3 4 5 6 2 3 4'
check '1,6t3' '1 2 3 1 2 3 4 5 6 4 5 6'

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_copy"
