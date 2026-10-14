#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
BASE="$ROOT/tests/usr.bin/cc/conformance_c99"
PASS_LOG="/tmp/cc_conformance_pass.log"
XFAIL_LOG="/tmp/cc_conformance_xfail.log"

pass_ok=0
pass_fail=0
xfail_ok=0
xpass_fail=0
pass_fail_list=""
xpass_list=""

parse_entry() {
	entry=$1
	entry=${entry%%#*}
	entry=$(printf "%s" "$entry" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
	case_name=${entry%%|*}
	case_reason=
	if [ "$case_name" != "$entry" ]; then
		case_reason=${entry#*|}
		case_reason=$(printf "%s" "$case_reason" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
	fi
}

while IFS= read -r entry; do
	parse_entry "$entry"
	[ -z "$case_name" ] && continue
	out="/tmp/cc_conformance_pass_$(basename "$case_name" .c).o"
	if "$CC_BIN" -std=c99 -c "$BASE/$case_name" -o "$out" >"$PASS_LOG" 2>&1; then
		pass_ok=$((pass_ok + 1))
	else
		echo "conformance-c99: PASS failed: $case_name"
		cat "$PASS_LOG"
		pass_fail_list="$pass_fail_list $case_name"
		pass_fail=$((pass_fail + 1))
	fi
done < "$BASE/PASS.txt"

while IFS= read -r entry; do
	parse_entry "$entry"
	[ -z "$case_name" ] && continue
	out="/tmp/cc_conformance_xfail_$(basename "$case_name" .c).o"
	if "$CC_BIN" -std=c99 -c "$BASE/$case_name" -o "$out" >"$XFAIL_LOG" 2>&1; then
		if [ -n "$case_reason" ]; then
			echo "conformance-c99: XPASS: $case_name ($case_reason)"
		else
			echo "conformance-c99: XPASS: $case_name"
		fi
		xpass_list="$xpass_list $case_name"
		xpass_fail=$((xpass_fail + 1))
	else
		xfail_ok=$((xfail_ok + 1))
	fi
done < "$BASE/XFAIL.txt"

echo "conformance-c99: pass=$pass_ok pass_fail=$pass_fail xfail=$xfail_ok xpass=$xpass_fail"
if [ -n "$pass_fail_list" ]; then
	echo "conformance-c99: failed-pass cases:$pass_fail_list"
fi
if [ -n "$xpass_list" ]; then
	echo "conformance-c99: unexpected-pass cases:$xpass_list"
fi
if [ "$pass_fail" -ne 0 ] || [ "$xpass_fail" -ne 0 ]; then
	exit 1
fi
exit 0
