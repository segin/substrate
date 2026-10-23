#!/bin/sh
# run_i386_frame_slots.sh: an i386 frame is no larger than the values and
# locals live at once need, as a Linux kernel stack is 8 KiB.
#
# A value live across blocks shares a frame slot with others whose live
# ranges do not meet its own: cc gave each its own slot, and Linux's
# siphash_1u32() had a 2 KiB frame where a kernel stack is 8 KiB.
#
# codegen/frame_slots.c checks the functions' results against gcc.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
status=0

# frame FILE FUNC: the bytes FUNC's prologue allocates (its subl), 0 for none
frame() {
	awk -v fn="$2" '$0 == fn ":" { p = 1 } p && /subl \$[0-9]+, %esp/ { gsub(/[^0-9]/, "", $2); print $2; exit }
	    p && /^\.size/ { print 0; exit }' "$1"
}

# check FILE FUNC LIMIT WHAT
check() {
	n=$(frame "$1" "$2")
	if [ -z "$n" ]; then
		echo "FAIL $2: not in the assembly"
		status=1
	elif [ "$n" -gt "$3" ]; then
		echo "FAIL $2: a frame of $n bytes, over $3 ($4)"
		status=1
	fi
}

"$CC_BIN" -m32 -O2 -S -o "$CC_TMP/fs.s" "$ROOT/tests/usr.bin/cc/codegen/frame_slots.c"
check "$CC_TMP/fs.s" chain_blocks 96 "values live across blocks share slots"
[ "$status" -eq 0 ] && echo "run_i386_frame_slots: ok"
exit "$status"
