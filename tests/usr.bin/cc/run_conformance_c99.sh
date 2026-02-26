#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
BASE="$ROOT/tests/usr.bin/cc/conformance_c99"

pass_ok=0
pass_fail=0
xfail_ok=0
xpass_fail=0

while IFS= read -r t; do
	[ -z "$t" ] && continue
	out="/tmp/cc_conformance_pass_$(basename "$t" .c).o"
	if "$CC_BIN" -std=c99 -c "$BASE/$t" -o "$out" >/tmp/cc_conformance_pass.log 2>&1; then
		pass_ok=$((pass_ok + 1))
	else
		echo "conformance-c99: PASS failed: $t"
		cat /tmp/cc_conformance_pass.log
		pass_fail=$((pass_fail + 1))
	fi
done < "$BASE/PASS.list"

while IFS= read -r t; do
	[ -z "$t" ] && continue
	out="/tmp/cc_conformance_xfail_$(basename "$t" .c).o"
	if "$CC_BIN" -std=c99 -c "$BASE/$t" -o "$out" >/tmp/cc_conformance_xfail.log 2>&1; then
		echo "conformance-c99: XPASS: $t"
		xpass_fail=$((xpass_fail + 1))
	else
		xfail_ok=$((xfail_ok + 1))
	fi
done < "$BASE/XFAIL.list"

echo "conformance-c99: pass=$pass_ok pass_fail=$pass_fail xfail=$xfail_ok xpass=$xpass_fail"
if [ "$pass_fail" -ne 0 ] || [ "$xpass_fail" -ne 0 ]; then
	exit 1
fi
exit 0
