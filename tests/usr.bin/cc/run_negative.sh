#!/bin/sh
# run_negative.sh: compile each program listed in negative/CASES.txt and
# require cc to reject it with the diagnostic the table names.  A
# rejection for another reason -- a crash, a tool that cannot run, a
# different error -- is a failure, not a pass.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN=${CC_BIN:-"$ROOT/usr.bin/cc/cc"}
BASE="$ROOT/tests/usr.bin/cc"
CASES="$BASE/negative/CASES.txt"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_negative.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

fail=0
count=0
while IFS='|' read -r src flags want; do
	case "$src" in
	'' | '#'*) continue ;;
	esac
	count=$((count + 1))
	rc=0
	# shellcheck disable=SC2086 -- flags are words
	"$CC_BIN" $flags -c "$BASE/$src" -o "$tmp/out.o" >"$tmp/log" 2>&1 || rc=$?
	if [ $rc -eq 0 ]; then
		echo "FAIL $src: accepted, expected \"$want\""
		fail=1
	elif [ $rc -ge 126 ]; then
		echo "FAIL $src: cc did not run or was killed (exit $rc)"
		fail=1
	elif ! grep -qF -- "$want" "$tmp/log"; then
		echo "FAIL $src: rejected, but without \"$want\":"
		sed -n '1,3s/^/    /p' "$tmp/log"
		fail=1
	fi
done <"$CASES"
echo "run_negative: $count programs"
exit $fail
