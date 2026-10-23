#!/bin/sh
# run_i386_frame_slots.sh: an i386 frame is no larger than the values and
# locals live at once need, as a Linux kernel stack is 8 KiB.
#
# A value live across blocks shares a frame slot with others whose live
# ranges do not meet its own: cc gave each its own slot, and Linux's
# siphash_1u32() had a 2 KiB frame where a kernel stack is 8 KiB.
#
# A local of a function with an asm goto is made a value as any other:
# Linux's static keys put an asm goto in most functions, and every local
# of intel_pmu_init() stayed in memory, 1.6 KiB of its frame.
#
# A buffer nothing reads loses its stores and its zeroing memset, and
# with them its place in the frame: under -ftrivial-auto-var-init=zero,
# Linux's parse_elf_properties() kept a 1 KiB buffer the configuration
# had made dead.
#
# In a frame of 8-byte slots (a function with a 64-bit value) a word
# takes a 4-byte slot: Linux's do_sys_poll() was 4 bytes over 1 KiB.
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
check "$CC_TMP/fs.s" chain_asm_goto 96 "locals made values under an asm goto"
check "$CC_TMP/fs.s" words_live 200 "words in 4-byte slots"
cat > "$CC_TMP/unread.c" <<'END'
int probe(int *p);

int unread(int x) {
	union {
		int head;
		char data[1024];
	} note;

	if (1 || !x)
		return 0;
	return probe(&note.head);
}
END
if "$CC_BIN" -m32 -O2 -ftrivial-auto-var-init=zero -S -o "$CC_TMP/unread.s" "$CC_TMP/unread.c"; then
	check "$CC_TMP/unread.s" unread 96 "a buffer nothing reads goes"
else
	echo "FAIL unread: cc rejected it"
	status=1
fi
[ "$status" -eq 0 ] && echo "run_i386_frame_slots: ok"
exit "$status"
