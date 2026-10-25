#!/bin/sh
# An x86-64 variadic function goes through the register allocator: its
# argument registers saved at entry, va_start filling the record its
# va_lists point at.  It used to go to the old emitter, which gives every
# value a stack slot of its own -- Linux's printk family among them, on
# 16 KiB kernel stacks.  CC_MIR_STATS says which path took each function;
# the program's output is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_x64_variadic_regs: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdarg.h>
#include <stdio.h>

struct pt {
    long x;
    long y;
};

struct big {
    long a[5];
};

/* the arguments past six in the overflow area */
static long __attribute__((noinline)) sum(int n, ...) {
    va_list ap;
    long t = 0;
    int i;

    va_start(ap, n);
    for (i = 0; i < n; ++i)
        t += va_arg(ap, long) * (i + 1);
    va_end(ap);
    return t;
}

/* seven fixed parameters: the first variadic one is on the stack after the seventh */
static long __attribute__((noinline)) late(long a, long b, long c, long d, long e, long f, long g, ...) {
    va_list ap;
    long t;

    va_start(ap, g);
    t = a + b + c + d + e + f + g;
    t = t * 1000 + va_arg(ap, int);
    t = t * 1000 + va_arg(ap, int);
    va_end(ap);
    return t;
}

/* va_copy: two lists that advance apart; structures, in registers and in memory */
static long __attribute__((noinline)) pairs(int n, ...) {
    va_list ap;
    va_list aq;
    long t = 0;
    int i;

    va_start(ap, n);
    va_copy(aq, ap);
    for (i = 0; i < n; ++i) {
        struct pt p = va_arg(ap, struct pt);

        t = t * 7 + p.x - p.y;
    }
    for (i = 0; i < n; ++i)
        t += va_arg(aq, struct pt).y;
    {
        struct big b = va_arg(ap, struct big);

        t += b.a[0] + b.a[4];
    }
    va_end(aq);
    va_end(ap);
    return t;
}

static int __attribute__((noinline)) fmt(char *buf, unsigned long size, const char *f, ...) {
    va_list ap;
    int r;

    va_start(ap, f);
    r = vsnprintf(buf, size, f, ap);
    va_end(ap);
    return r;
}

int main(void) {
    struct pt a = {10, 3};
    struct pt b = {20, 5};
    struct pt c = {30, 7};
    struct big g = {{100, 2, 3, 4, 500}};
    char buf[96];

    printf("%ld\n", sum(10, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L));
    printf("%ld\n", late(1, 2, 3, 4, 5, 6, 7, 11, 22));
    printf("%ld\n", pairs(3, a, b, c, g));
    fmt(buf, sizeof(buf), "%d %s %ld %c %u", 42, "str", 1234567890123L, 'x', 7u);
    puts(buf);
    return 0;
}
EOF2
gcc -O1 -o "$CC_TMP/vg" "$CC_TMP/v.c"
want=$("$CC_TMP/vg")
for o in O0 O2; do
	CC_MIR_STATS=1 "$CC_BIN" -m64 -$o -o "$CC_TMP/vc" "$CC_TMP/v.c" 2>"$CC_TMP/stats"
	for fn in sum late pairs fmt; do
		if ! grep -q "^mir: $fn: taken" "$CC_TMP/stats"; then
			echo "run_x64_variadic_regs: -$o: $fn does not go through the allocator:"
			grep "^mir: $fn:" "$CC_TMP/stats" || true
			exit 1
		fi
	done
	got=$("$CC_TMP/vc")
	if [ "$got" != "$want" ]; then
		echo "run_x64_variadic_regs: -$o prints"
		echo "$got"
		echo "where gcc's prints"
		echo "$want"
		exit 1
	fi
done
echo "run_x64_variadic_regs: ok"
