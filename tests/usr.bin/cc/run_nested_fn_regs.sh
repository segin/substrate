#!/bin/sh
# GNU nested functions go through the register allocator on i386 and
# x86-64: the environment read from the static chain register (%ecx,
# %r10) at entry, and loaded there before a direct call of one; through a
# trampoline, called by pointer, as well.  They and the functions calling
# them used to go to the old emitter, a stack slot for every value.
# CC_MIR_STATS says which path took each function; the program's output
# is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_nested_fn_regs: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/n.c" <<'EOF2'
#include <stdio.h>

static int __attribute__((noinline)) apply(int (*fn)(int), int n) {
    int t = 0;
    int i;

    for (i = 0; i < n; ++i)
        t += fn(i);
    return t;
}

static long __attribute__((noinline)) outer(int base, int n) {
    long acc = 0;
    int calls = 0;

    int add(int x) {
        calls++;
        acc += x + base;
        return x * base;
    }
    /* two levels: the inner one reaches the outer's variables through its own chain */
    int twice(int x) {
        int inner(int y) {
            return add(y) + add(y + 1);
        }
        return inner(x) + calls;
    }
    long r = 0;
    int i;

    for (i = 0; i < n; ++i)
        r += twice(i);
    /* by pointer: a trampoline on the stack loads the chain */
    r += apply(add, n);
    return r * 1000 + acc + calls;
}

int main(void) {
    printf("%ld %ld\n", outer(3, 5), outer(-2, 7));
    return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/ng" "$CC_TMP/n.c" 2>/dev/null
	want=$("$CC_TMP/ng")
	for o in O0 O2; do
		CC_MIR_STATS=1 "$CC_BIN" -m$m -$o -o "$CC_TMP/nc" "$CC_TMP/n.c" 2>"$CC_TMP/stats"
		if grep "^mir: .*not taken" "$CC_TMP/stats"; then
			echo "run_nested_fn_regs: -m$m -$o: the above do not go through the allocator"
			exit 1
		fi
		got=$("$CC_TMP/nc")
		if [ "$got" != "$want" ]; then
			echo "run_nested_fn_regs: -m$m -$o prints"
			echo "$got"
			echo "where gcc's prints"
			echo "$want"
			exit 1
		fi
	done
done
echo "run_nested_fn_regs: ok"
