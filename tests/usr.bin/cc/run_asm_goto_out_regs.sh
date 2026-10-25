#!/bin/sh
# A function with an asm goto that has outputs goes through the register
# allocator on i386 and x86-64: each output lives in its frame slot,
# which the asm writes.  Such a function used to go to the old emitter, a
# stack slot for every value.  CC_MIR_STATS says which path took each
# function; what falls through the asm is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_goto_out_regs: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/a.c" <<'EOF2'
#include <stdio.h>

/* the output read where the asm falls through; a value kept across it */
static int __attribute__((noinline)) twice_or_neg(int k, int keep) {
    int out;

    asm goto("movl %1, %0\n\taddl %1, %0\n\ttestl %1, %1\n\tjs %l2" : "=&r"(out) : "r"(k) : "cc" : neg);
    return out + keep;
neg:
    return -1000 + keep;
}

/* two outputs, in a loop */
static long __attribute__((noinline)) count(int n) {
    long t = 0;
    int i;

    for (i = 0; i < n; ++i) {
        int lo;
        int hi;

        asm goto("movl %2, %0\n\tleal 1(%2), %1\n\tcmpl $5, %2\n\tje %l3" : "=&r"(lo), "=&r"(hi) : "r"(i) : "cc"
                 : skip);
        t += lo * hi;
        continue;
skip:
        t += 1000000;
    }
    return t;
}

int main(void) {
    printf("%d %d\n", twice_or_neg(21, 7), twice_or_neg(-3, 7));
    printf("%ld\n", count(9));
    return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/ag" "$CC_TMP/a.c"
	want=$("$CC_TMP/ag")
	for o in O0 O2; do
		CC_MIR_STATS=1 "$CC_BIN" -m$m -$o -o "$CC_TMP/ac" "$CC_TMP/a.c" 2>"$CC_TMP/stats"
		for fn in twice_or_neg count; do
			if ! grep -q "^mir: $fn: taken" "$CC_TMP/stats"; then
				echo "run_asm_goto_out_regs: -m$m -$o: $fn does not go through the allocator:"
				grep "^mir: $fn:" "$CC_TMP/stats" || true
				exit 1
			fi
		done
		got=$("$CC_TMP/ac")
		if [ "$got" != "$want" ]; then
			echo "run_asm_goto_out_regs: -m$m -$o prints"
			echo "$got"
			echo "where gcc's prints"
			echo "$want"
			exit 1
		fi
	done
done
echo "run_asm_goto_out_regs: ok"
