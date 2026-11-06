#!/bin/bash
# `w !cmd` survives a command that stops reading.  With SIGPIPE at its
# default the editor was killed (status 141) and the buffer lost when the
# command exited before reading it all; now the write fails with EPIPE, is
# reported, and the session goes on.
#
# Usage: test_ex_pipe_write.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_pipe_write.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

seq 1 50000 >"$T/big"
# (with -c: a command read from a pipe that fails ends the session, and
# the next would never run)
out=$("$EX_BIN" -s -c 'w !true' -c '$=' -c 'q!' "$T/big" </dev/null 2>&1)
status=$?
if [ "$status" -ge 128 ]; then
	fail "w !true killed ex with signal $((status - 128))"
fi
if ! grep -qx 50000 <<<"$out"; then
	fail "ex did not go on after w !true: [$(tail -3 <<<"$out")]"
fi
if ! grep -q 'Broken pipe' <<<"$out"; then
	fail "the broken pipe was not reported: [$(tail -3 <<<"$out")]"
fi

# a command that reads it all still gets it all
out=$(printf 'w !cat\nq\n' | "$EX_BIN" -s "$T/big" 2>&1)
if [ "$(grep -c . <<<"$out")" -ne 50000 ]; then
	fail "w !cat passed $(grep -c . <<<"$out") lines, not 50000"
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_pipe_write"
