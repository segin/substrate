#!/bin/bash
# Limits that crashed ex or wrapped around:
#  - a `|` chain ran in one stack frame per command: a line of 300,000
#    `ka|` ran off the stack (SIGSEGV);
#  - an address past int was truncated to it: 4294967297p printed line 1;
#    an offset past it wrapped the same way.
#
# Usage: test_ex_limits.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_limits.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

cd "$T" || exit 1
printf 'one\ntwo\nthree\n' >f

# 300,000 commands on one line
{ printf 'ka|%.0s' $(seq 1 300000); printf '\nq\n'; } >chain
"$EX_BIN" -s f <chain >out 2>&1
status=$?
if [ "$status" -ne 0 ]; then
	fail "a 300,000-command line: status $status [$(head -c 200 out)]"
fi

# an address past int is an error, not line 1
out=$(printf '4294967297p\n' | "$EX_BIN" -s f 2>&1)
status=$?
if [ "$status" -eq 0 ] || grep -qx one <<<"$out"; then
	fail "4294967297p: status $status [$out]"
fi
out=$(printf '1+4294967296p\n' | "$EX_BIN" -s f 2>&1)
status=$?
if [ "$status" -eq 0 ] || grep -qx one <<<"$out"; then
	fail "1+4294967296p: status $status [$out]"
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_limits"
