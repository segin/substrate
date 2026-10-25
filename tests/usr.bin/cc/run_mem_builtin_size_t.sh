#!/bin/sh
# A __builtin_memcpy, memmove or memset that lowering makes a call of
# passes its length as the size_t the prototype says, and memset's byte as
# an int: a wider count -- a difference of loff_t values on i386, as in
# Linux's freader_fetch() -- is cut to its word.  It was passed whole, two
# words, which under -mregparm=3 needed a fourth register (the function
# went to the old emitter) and on the stack two words where the callee
# takes one.  Through the register allocator under -mregparm=3; the
# program's output (without regparm, to link libc) is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_mem_builtin_size_t: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>
#include <string.h>

typedef long long loff_t;
static char buf[64];

static void __attribute__((noinline)) fill(loff_t off, loff_t end, long long c) {
    __builtin_memset(buf, (int)c, sizeof(buf));
    __builtin_memcpy(buf + off, "abcdefghijklmnop", end - off);
    __builtin_memmove(buf + 20, buf + off, end - off);
    __builtin_memset(buf + 40, c + 1, end - off);
}

int main(void) {
    fill(3, 9, 0x2e);
    buf[63] = 0;
    printf("%s\n", buf);
    return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/mg" "$CC_TMP/m.c"
	want=$("$CC_TMP/mg")
	for o in O0 O2; do
		if [ $m = 32 ]; then
			CC_MIR_STATS=1 "$CC_BIN" -m32 -mregparm=3 -$o -c -o "$CC_TMP/rp.o" "$CC_TMP/m.c" 2>"$CC_TMP/stats"
			if grep "^mir: fill: not taken" "$CC_TMP/stats"; then
				echo "run_mem_builtin_size_t: -m32 -mregparm=3 -$o: fill does not go through the allocator"
				exit 1
			fi
		fi
		"$CC_BIN" -m$m -$o -o "$CC_TMP/mc" "$CC_TMP/m.c"
		got=$("$CC_TMP/mc")
		if [ "$got" != "$want" ]; then
			echo "run_mem_builtin_size_t: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_mem_builtin_size_t: ok"
