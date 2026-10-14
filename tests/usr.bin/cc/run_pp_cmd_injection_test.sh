#!/bin/sh
# run_pp_cmd_injection_test.sh: a shell command in CC_BOOTSTRAP or
# HOSTCC must not be run by cc -E.  The driver starts tools with
# execvp(), never a shell; this holds it to that.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_inject.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM
echo "int x = 1;" > "$tmp/dummy.c"

fail=0
for var in CC_BOOTSTRAP HOSTCC; do
	rm -f "$tmp/injected.txt"
	env "$var=echo; touch $tmp/injected.txt" "$CC_BIN" -E "$tmp/dummy.c" >/dev/null 2>&1
	rc=$?
	# 126/127: cc itself could not be run, so nothing was tested
	if [ $rc -eq 126 ] || [ $rc -eq 127 ]; then
		echo "FAIL: could not run $CC_BIN (exit $rc)"
		fail=1
	elif [ -f "$tmp/injected.txt" ]; then
		echo "FAIL: command injection succeeded via $var"
		fail=1
	fi
done
[ $fail -eq 0 ] && echo "PASS: run_pp_cmd_injection_test.sh"
exit $fail
