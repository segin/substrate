#!/bin/bash
# Reading commands from something other than a terminal, ex stops at the
# first command that fails, writes nothing more, and exits non-zero (POSIX
# ex, Consequences of Errors); end of input with the buffer modified is a
# quit that fails.  Errors were reported and ignored: the commands after
# them ran, `w` wrote the file, and the status was 0.
#
# Usage: test_ex_batch_error.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_batch_error.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

printf 'one\ntwo\n' >"$T/f"
out=$(printf 'bogus\n1d\nw\nq\n' | "$EX_BIN" -s "$T/f" 2>&1)
status=$?
[ "$status" -ne 0 ] || fail "an unknown command left the status 0: [$out]"
[ "$(cat "$T/f")" = "$(printf 'one\ntwo')" ] || fail "the commands after a failed one ran: f is [$(cat "$T/f")]"
grep -q 'Unknown command' <<<"$out" || fail "the failed command was not reported: [$out]"

for bad in '/nomatch/' 's/x/y/x' 'set nosuchoption'; do
	printf 'one\ntwo\n' >"$T/f"
	out=$(printf '%s\n1d\nw\nq\n' "$bad" | "$EX_BIN" -s "$T/f" 2>&1)
	status=$?
	[ "$status" -ne 0 ] || fail "'$bad' failing left the status 0: [$out]"
	[ "$(cat "$T/f")" = "$(printf 'one\ntwo')" ] || fail "commands after a failed '$bad' ran"
done

printf 'one\ntwo\n' >"$T/f"
out=$(printf '1d\n' | "$EX_BIN" -s "$T/f" 2>&1)
status=$?
[ "$status" -ne 0 ] || fail "end of input with the buffer modified left the status 0: [$out]"

out=$(printf '1p\nq\n' | "$EX_BIN" -s "$T/f" 2>&1)
status=$?
[ "$status" -eq 0 ] && [ "$out" = one ] || fail "a session without errors: status $status [$out]"

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_batch_error"
