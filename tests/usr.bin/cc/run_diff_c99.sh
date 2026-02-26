#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
BASE="$ROOT/tests/usr.bin/cc/diff_c99"
CASES="match_simple.c match_variadic.c match_fail_implicit_decl.c"
HOST_CC_LIST="gcc clang"

run_status() {
	compiler=$1
	src=$2
	out=$3
	if "$compiler" -std=c99 -c "$src" -o "$out" >/tmp/cc_diff_host.log 2>&1; then
		echo 0
	else
		echo 1
	fi
}

run_status_ours() {
	src=$1
	out=$2
	if "$CC_BIN" -std=c99 -c "$src" -o "$out" >/tmp/cc_diff_ours.log 2>&1; then
		echo 0
	else
		echo 1
	fi
}

mismatch=0
checked=0

for hostcc in $HOST_CC_LIST; do
	if ! command -v "$hostcc" >/dev/null 2>&1; then
		echo "diff-c99: skipping missing host compiler: $hostcc"
		continue
	fi
	for c in $CASES; do
		checked=$((checked + 1))
		host_rc=$(run_status "$hostcc" "$BASE/$c" "/tmp/cc_diff_${hostcc}_$(basename "$c" .c).o")
		our_rc=$(run_status_ours "$BASE/$c" "/tmp/cc_diff_ours_$(basename "$c" .c).o")
		if [ "$host_rc" -ne "$our_rc" ]; then
			echo "diff-c99: mismatch [$hostcc] $c host=$host_rc ours=$our_rc"
			echo "diff-c99: host stderr:"
			cat /tmp/cc_diff_host.log
			echo "diff-c99: our stderr:"
			cat /tmp/cc_diff_ours.log
			mismatch=$((mismatch + 1))
		fi
	done
done

echo "diff-c99: checked=$checked mismatches=$mismatch"
if [ "$checked" -eq 0 ]; then
	echo "diff-c99: no host compiler found"
	exit 1
fi
if [ "$mismatch" -ne 0 ]; then
	exit 1
fi
exit 0
