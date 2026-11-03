#!/bin/sh
# A member of no bytes -- an empty structure, GNU C's `struct { }` -- may
# end a structure, and an initializer may name it.  cc measured such a
# member as a split bit-field of width 0, one byte, and when it ended the
# object refused the initializer ("struct member offset exceeds global
# initializer object"): Linux's mm/swapfile.c, whose per-CPU
# percpu_swap_cluster ends in a local_lock_t, empty without lock debugging,
# initialized `.lock = INIT_LOCAL_LOCK(lock)`, that is `{ }`.  An empty
# member at the end, in the middle and nested, in a global, an array and a
# static local; the program prints what gcc's prints, -m64 and -m32, both
# code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_empty_member_init: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/e.c" <<'EOF2'
#include <stdio.h>
typedef struct { } local_lock_t;
struct pc { void *si[10]; unsigned long offset[10]; local_lock_t lock; };
struct mid { int a; local_lock_t l; int b; };
struct nest { struct mid m; local_lock_t tail; };
struct pc pcv __attribute__((section(".data.pc"))) = { .si = { 0 }, .offset = { 1, 2 }, .lock = { }, };
struct pc pcw = { .lock = { } };
struct mid mv = { .a = 3, .l = { }, .b = 4 };
struct nest nv = { .m = { 5, { }, 6 }, .tail = { } };
struct pc arr[2] = { [1] = { .offset = { 7 }, .lock = { } } };
int main(void)
{
	static struct nest sv = { .m.b = 8, .tail = { } };

	printf("%zu %zu %zu %lu %lu %d %d %d %d %lu %d\n", sizeof(struct pc), sizeof(struct mid), sizeof(struct nest),
	       pcv.offset[0], pcv.offset[1], mv.a, mv.b, nv.m.a, nv.m.b, arr[1].offset[0], sv.m.b);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O2 -o "$CC_TMP/g" "$CC_TMP/e.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/e.o" "$CC_TMP/e.c" 2>"$CC_TMP/c.err"; then
			echo "run_empty_member_init: -m$m $path refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/e.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_empty_member_init: -m$m $path prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_empty_member_init: ok"
