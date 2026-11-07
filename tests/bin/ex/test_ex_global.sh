#!/bin/bash
# global's command list (POSIX ex, global):
#  - escaped newlines separate its commands (g/2/s/2/X/\ + s/X/Y/ was
#    "Bad substitute flags");
#  - text input in it ends at a `.` or the end of the list (g/3/a\ + NEW
#    swallowed the rest of the input);
#  - global, v and undo may not appear in it (g/5/g/./p printed every line).
#
# Usage: test_ex_global.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_global.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

cd "$T" || exit 1

seq 1 5 >f
out=$(printf 'g/2/s/2/X/\\\ns/X/Y/\n%%p\nq!\n' | "$EX_BIN" -s f 2>&1)
[ "$out" = "$(printf '1\nY\n3\n4\n5')" ] || fail "two commands on escaped lines: [$out]"

seq 1 5 >f
out=$(printf 'g/3/a\\\nNEW\n%%p\nq!\n' | "$EX_BIN" -s f 2>&1)
[ "$out" = "$(printf '1\n2\n3\nNEW\n4\n5')" ] || fail "a with no . in a global: [$out]"

seq 1 5 >f
out=$(printf 'g/5/g/./p\nq!\n' | "$EX_BIN" -s f 2>&1)
status=$?
if [ "$status" -eq 0 ] || grep -qx 1 <<<"$out"; then
	fail "global within global: status $status [$out]"
fi

seq 1 5 >f
out=$(printf 'g/5/u\nq!\n' | "$EX_BIN" -s f 2>&1)
status=$?
[ "$status" -ne 0 ] || fail "undo within global: status $status [$out]"

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_global"
