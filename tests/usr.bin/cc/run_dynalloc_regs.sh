#!/bin/sh
# A function that moves the stack pointer itself -- alloca, a
# variable-length array -- goes through the register allocator on i386
# and x86-64: the block taken off the stack, a loop's array given its
# storage back each time round, and the stack pointer made again from the
# frame pointer at the return, past callee-saved registers.  Such a
# function used to go to the old emitter, a stack slot for every value.
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
	echo "run_dynalloc_regs: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/d.c" <<'EOF2'
#include <stdio.h>
#include <string.h>

static long __attribute__((noinline)) many(long a, long b, long c, long d, long e, long f, long g, long h) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h;
}

/* a VLA per iteration: the same storage each time round */
static int __attribute__((noinline)) loop(int n) {
    char *first = 0;
    int same = 1;
    long t = 0;
    int i;

    for (i = 1; i <= n; ++i) {
        long v[i];
        int k;

        for (k = 0; k < i; ++k)
            v[k] = k * i;
        if (first == 0)
            first = (char *)v;
        else if ((char *)v != first)
            same = 0;
        for (k = 0; k < i; ++k)
            t += v[k];
    }
    return same * 1000000 + (int)t;
}

/* alloca and enough live values for callee-saved registers, then calls with stack arguments */
static long __attribute__((noinline)) blocks(int n, long x, long y, long z) {
    long *p = __builtin_alloca(n * sizeof(long));
    char *q = __builtin_alloca(n + 1);
    long a = x * 3, b = y * 5, c = z * 7, t = 0;
    int i;

    for (i = 0; i < n; ++i) {
        p[i] = many(i, a, b, c, x, y, z, i * 2);
        q[i] = (char)('a' + i % 26);
    }
    q[n] = 0;
    for (i = 0; i < n; ++i)
        t += p[i] ^ (long)q[i];
    return t + a + b + c + (long)strlen(q);
}

static int __attribute__((noinline)) matrix(int r, int c) {
    int m[r][c];
    int i, j, t = 0;

    for (i = 0; i < r; ++i)
        for (j = 0; j < c; ++j)
            m[i][j] = i * 31 + j;
    for (i = 0; i < r; ++i)
        t += m[i][c - 1 - i % c] * (int)sizeof(m[i]);
    return t;
}

int main(void) {
    printf("%d\n", loop(40));
    printf("%ld\n", blocks(37, 3, 4, 5));
    printf("%d\n", matrix(9, 13));
    return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/dg" "$CC_TMP/d.c"
	want=$("$CC_TMP/dg")
	for o in O0 O2; do
		CC_MIR_STATS=1 "$CC_BIN" -m$m -$o -o "$CC_TMP/dc" "$CC_TMP/d.c" 2>"$CC_TMP/stats"
		for fn in loop blocks matrix; do
			if ! grep -q "^mir: $fn: taken" "$CC_TMP/stats"; then
				echo "run_dynalloc_regs: -m$m -$o: $fn does not go through the allocator:"
				grep "^mir: $fn:" "$CC_TMP/stats" || true
				exit 1
			fi
		done
		got=$("$CC_TMP/dc")
		if [ "$got" != "$want" ]; then
			echo "run_dynalloc_regs: -m$m -$o prints"
			echo "$got"
			echo "where gcc's prints"
			echo "$want"
			exit 1
		fi
	done
done
echo "run_dynalloc_regs: ok"
