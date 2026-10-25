#!/bin/sh
# A structure of no whole access's size -- 3, 5, 6, 7 bytes, or a second
# eightbyte of them, 13 or 15 bytes -- goes through the register
# allocator when passed or returned in registers: its bytes loaded and
# stored in pieces, the biggest first.  An empty structure result is
# nothing in registers.  On x86-64 such a function used to go to the old
# emitter, a stack slot for every value.  CC_MIR_STATS says which path
# took each function; the program's output is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_small_struct_regs: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/s.c" <<'EOF2'
#include <stdio.h>

struct b3 { char c[3]; };
struct b5 { char c[5]; };
struct s6 { short a, b, c; };
struct b7 { char c[7]; };
struct b13 { char c[13]; };
struct b15 { short s[7]; char t; };
typedef struct { } empty_t;

#define MK(T, N) \
    static struct T __attribute__((noinline)) mk_##T(int k) { \
        struct T r; \
        int i; \
        for (i = 0; i < (int)sizeof(r); ++i) \
            ((unsigned char *)&r)[i] = (unsigned char)(k * 7 + i * 13); \
        return r; \
    } \
    static int __attribute__((noinline)) sum_##T(int a, struct T v, int b) { \
        int t = a * 1000 + b; \
        int i; \
        for (i = 0; i < (int)sizeof(v); ++i) \
            t += ((unsigned char *)&v)[i] * (i + 1); \
        return t; \
    }
MK(b3, 3)
MK(b5, 5)
MK(s6, 6)
MK(b7, 7)
MK(b13, 13)
MK(b15, 15)

static empty_t __attribute__((noinline)) none(int *k) {
    empty_t e;

    *k += 1;
    return e;
}

int main(void) {
    int k = 0;
    empty_t e = none(&k);

    (void)e;
    printf("%d %d %d\n", sum_b3(1, mk_b3(1), 2), sum_b5(3, mk_b5(2), 4), sum_s6(5, mk_s6(3), 6));
    printf("%d %d %d\n", sum_b7(7, mk_b7(4), 8), sum_b13(9, mk_b13(5), 10), sum_b15(11, mk_b15(6), 12));
    none(&k);
    printf("%d\n", k);
    return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/sg" "$CC_TMP/s.c"
	want=$("$CC_TMP/sg")
	for o in O0 O2; do
		CC_MIR_STATS=1 "$CC_BIN" -m$m -$o -o "$CC_TMP/sc" "$CC_TMP/s.c" 2>"$CC_TMP/stats"
		if grep "^mir: .*not taken" "$CC_TMP/stats"; then
			echo "run_small_struct_regs: -m$m -$o: the above do not go through the allocator"
			exit 1
		fi
		got=$("$CC_TMP/sc")
		if [ "$got" != "$want" ]; then
			echo "run_small_struct_regs: -m$m -$o prints"
			echo "$got"
			echo "where gcc's prints"
			echo "$want"
			exit 1
		fi
	done
done
echo "run_small_struct_regs: ok"
