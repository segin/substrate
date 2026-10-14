#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
BASE="$ROOT/tests/usr.bin/cc/diff_c99"
CASES_FILE="$BASE/CASES.txt"
XFAIL_FILE="$BASE/XFAIL.txt"
HOST_CC_LIST="gcc clang"

run_compile_status() {
	compiler=$1
	src=$2
	out=$3
	log=$4
	mode=$5
	if [ "$mode" = "compile" ]; then
		cmd="-std=c99 -c"
	else
		cmd="-std=c99"
	fi
	if "$compiler" $cmd "$src" -o "$out" >"$log" 2>&1; then
		echo 0
	else
		echo 1
	fi
}

run_compile_status_ours() {
	src=$1
	out=$2
	log=$3
	mode=$4
	if [ "$mode" = "compile" ]; then
		cmd="-std=c99 -c"
	else
		cmd="-std=c99"
	fi
	if "$CC_BIN" $cmd "$src" -o "$out" >"$log" 2>&1; then
		echo 0
	else
		echo 1
	fi
}

run_program() {
	bin=$1
	out=$2
	rcfile=$3
	if "$bin" >"$out" 2>/dev/null; then
		echo 0 >"$rcfile"
	else
		echo $? >"$rcfile"
	fi
}

parse_entry() {
	entry=$1
	entry=${entry%%#*}
	entry=$(printf "%s" "$entry" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
	case_mode=${entry%%|*}
	case_name=${entry#*|}
	if [ "$case_mode" = "$entry" ]; then
		case_name=
	fi
}

is_xfail_case() {
	name=$1
	[ -f "$XFAIL_FILE" ] || return 1
	awk -F'|' -v n="$name" '
		{
			gsub(/^[[:space:]]+|[[:space:]]+$/, "", $1);
			if($1 == n) {
				found = 1;
			}
		}
		END {
			if(found) {
				exit 0;
			}
			exit 1;
		}
	' "$XFAIL_FILE"
}

mismatch=0
checked=0
xfail_ok=0

for hostcc in $HOST_CC_LIST; do
	if ! command -v "$hostcc" >/dev/null 2>&1; then
		echo "diff-c99: skipping missing host compiler: $hostcc"
		continue
	fi
	while IFS= read -r entry; do
		parse_entry "$entry"
		[ -z "$case_name" ] && continue
		if [ "$case_mode" != "compile" ] && [ "$case_mode" != "run" ]; then
			echo "diff-c99: invalid case mode '$case_mode' in $CASES_FILE"
			exit 1
		fi
		checked=$((checked + 1))
		host_bin="/tmp/cc_diff_${hostcc}_$(basename "$case_name" .c)"
		our_bin="/tmp/cc_diff_ours_$(basename "$case_name" .c)"
		if [ "$case_mode" = "compile" ]; then
			host_bin="${host_bin}.o"
			our_bin="${our_bin}.o"
		fi
		host_log="/tmp/cc_diff_${hostcc}_$(basename "$case_name" .c).log"
		our_log="/tmp/cc_diff_ours_$(basename "$case_name" .c).log"
		host_rc=$(run_compile_status "$hostcc" "$BASE/$case_name" "$host_bin" "$host_log" "$case_mode")
		our_rc=$(run_compile_status_ours "$BASE/$case_name" "$our_bin" "$our_log" "$case_mode")
		if [ "$host_rc" -ne "$our_rc" ]; then
			if is_xfail_case "$case_name"; then
				echo "diff-c99: expected mismatch [$hostcc] $case_name host=$host_rc ours=$our_rc"
				xfail_ok=$((xfail_ok + 1))
				continue
			fi
			echo "diff-c99: mismatch [$hostcc] $case_name host=$host_rc ours=$our_rc"
			echo "diff-c99: host stderr:"
			cat "$host_log"
			echo "diff-c99: our stderr:"
			cat "$our_log"
			mismatch=$((mismatch + 1))
			continue
		fi
		if [ "$case_mode" = "run" ] && [ "$host_rc" -eq 0 ]; then
			host_out="/tmp/cc_diff_${hostcc}_$(basename "$case_name" .c).out"
			our_out="/tmp/cc_diff_ours_$(basename "$case_name" .c).out"
			host_rc_file="/tmp/cc_diff_${hostcc}_$(basename "$case_name" .c).rc"
			our_rc_file="/tmp/cc_diff_ours_$(basename "$case_name" .c).rc"
			run_program "$host_bin" "$host_out" "$host_rc_file"
			run_program "$our_bin" "$our_out" "$our_rc_file"
			host_run_rc=$(cat "$host_rc_file")
			our_run_rc=$(cat "$our_rc_file")
			if [ "$host_run_rc" -ne "$our_run_rc" ] || ! cmp -s "$host_out" "$our_out"; then
				if is_xfail_case "$case_name"; then
					echo "diff-c99: expected runtime mismatch [$hostcc] $case_name host_rc=$host_run_rc ours_rc=$our_run_rc"
					xfail_ok=$((xfail_ok + 1))
					continue
				fi
				echo "diff-c99: runtime mismatch [$hostcc] $case_name host_rc=$host_run_rc ours_rc=$our_run_rc"
				echo "diff-c99: host stdout:"
				cat "$host_out"
				echo "diff-c99: our stdout:"
				cat "$our_out"
				mismatch=$((mismatch + 1))
			fi
		fi
	done < "$CASES_FILE"
done

echo "diff-c99: checked=$checked mismatches=$mismatch xfail=$xfail_ok"
if [ "$checked" -eq 0 ]; then
	echo "diff-c99: no host compiler found"
	exit 1
fi
if [ "$mismatch" -ne 0 ]; then
	exit 1
fi
exit 0
