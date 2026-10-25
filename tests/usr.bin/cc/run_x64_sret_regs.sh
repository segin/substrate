#!/bin/sh
# An x86-64 function returning a structure through memory -- bigger than
# two eightbytes -- goes through the register allocator: the caller's
# buffer's address taken from %rdi, the parameters from %rsi on, the
# result copied there and its address returned in %rax.  It used to go to
# the old emitter, a stack slot for every value.  CC_MIR_STATS says which
# path took each function; the program's output is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_x64_sret_regs: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/r.c" <<'EOF2'
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct s3 {
    long a, b, c;
};

struct big {
    char name[100];
    int n;
};

struct odd {
    char c[19];
};

/* the sixth parameter on the stack: %rdi is the buffer's */
static struct s3 __attribute__((noinline)) mk(long x, long y, long z, long w, long v, long u) {
    struct s3 r;

    r.a = x + w;
    r.b = y * v;
    r.c = z - u;
    return r;
}

/* two returns; past the size copied without a call */
static struct big __attribute__((noinline)) named(const char *s, int n) {
    struct big b;

    memset(&b, 0, sizeof(b));
    strncpy(b.name, s, sizeof(b.name) - 1);
    b.n = n;
    if (n > 3)
        return b;
    b.n = -n;
    return b;
}

/* a size of no whole word */
static struct odd __attribute__((noinline)) odd(int k) {
    struct odd o;
    int i;

    for (i = 0; i < 19; ++i)
        o.c[i] = (char)('a' + (i + k) % 26);
    return o;
}

static struct s3 __attribute__((noinline)) chain(int n) {
    struct s3 t;

    if (n == 0)
        return mk(1, 2, 3, 4, 5, 6);
    t = chain(n - 1);
    t.a += n;
    return t;
}

/* variadic as well: gp_offset counts %rdi */
static struct s3 __attribute__((noinline)) vmk(int n, ...) {
    struct s3 r = {0, 0, 0};
    va_list ap;
    int i;

    va_start(ap, n);
    for (i = 0; i < n; ++i) {
        long v = va_arg(ap, long);

        r.a += v;
        r.b = r.b * 3 + v;
        r.c ^= v << i;
    }
    va_end(ap);
    return r;
}

int main(void) {
    struct s3 r = mk(1, 2, 3, 4, 5, 6);
    struct big b = named("hello", 5);
    struct big c = named("x", 2);
    struct odd o = odd(3);
    struct s3 q = chain(4);
    struct s3 v = vmk(8, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L);

    printf("%ld %ld %ld\n", r.a, r.b, r.c);
    printf("%s %d %s %d\n", b.name, b.n, c.name, c.n);
    printf("%.19s\n", o.c);
    printf("%ld %ld %ld\n", q.a, q.b, q.c);
    printf("%ld %ld %ld\n", v.a, v.b, v.c);
    return 0;
}
EOF2
gcc -O1 -o "$CC_TMP/rg" "$CC_TMP/r.c"
want=$("$CC_TMP/rg")
for o in O0 O2; do
	CC_MIR_STATS=1 "$CC_BIN" -m64 -$o -o "$CC_TMP/rc" "$CC_TMP/r.c" 2>"$CC_TMP/stats"
	for fn in mk named odd chain vmk; do
		if ! grep -q "^mir: $fn: taken" "$CC_TMP/stats"; then
			echo "run_x64_sret_regs: -$o: $fn does not go through the allocator:"
			grep "^mir: $fn:" "$CC_TMP/stats" || true
			exit 1
		fi
	done
	got=$("$CC_TMP/rc")
	if [ "$got" != "$want" ]; then
		echo "run_x64_sret_regs: -$o prints"
		echo "$got"
		echo "where gcc's prints"
		echo "$want"
		exit 1
	fi
done
echo "run_x64_sret_regs: ok"
