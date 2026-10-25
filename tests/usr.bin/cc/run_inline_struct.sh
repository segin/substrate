#!/bin/sh
# An inline function passing or returning a structure by value is
# inlined: a structure argument's storage is the parameter (lowering
# copies it at the callee's entry before writing it, and only that is
# allowed), and each return stores the structure -- its words, or a copy
# from where it points -- to the call's buffer.  Linux's ptep_get(),
# native_make_pte() and the lock guards' constructors, which take or
# return one-word structures, stayed calls.  The program's output is gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_inline_struct: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/s.c" <<'EOF2'
#include <stdio.h>

struct pt { long x, y; };
struct big { long a[5]; };
typedef struct { unsigned long pte; } pte_t;
struct odd { char c[13]; };

static inline long dot(struct pt p, struct pt q) { p.x += 1; return p.x * q.x + p.y * q.y; }
static inline long bigsum(struct big b) { b.a[0]++; return b.a[0] + b.a[4]; }
static inline pte_t mkpte(unsigned long v) { pte_t r = { v | 1 }; return r; }
static inline struct pt mkpt(long a) { struct pt r = { a, a * 2 }; return r; }
static inline struct big mkbig(long a) {
    struct big r = {{a, a + 1, a + 2, a + 3, a + 4}};

    if (a > 5)
        return r;
    r.a[0] = -1;
    return r;
}
static inline struct odd mkodd(int k) {
    struct odd o;
    int i;

    for (i = 0; i < 13; ++i)
        o.c[i] = (char)('a' + (i + k) % 26);
    return o;
}

long __attribute__((noinline)) use(struct pt a, struct big b, unsigned long v) {
    pte_t t = mkpte(v);
    struct pt c = mkpt(3);
    struct big d = mkbig((long)v);
    struct odd o = mkodd(2);
    long r = dot(a, a) + bigsum(b) + (long)t.pte + c.y + d.a[0] + d.a[4] + o.c[12];

    /* the arguments' own storage is untouched */
    return r + a.x + b.a[0];
}

int main(void) {
    struct pt a = {2, 3};
    struct big b = {{10, 20, 30, 40, 50}};

    printf("%ld %ld\n", use(a, b, 7), use(a, b, 2));
    return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/s.s" "$CC_TMP/s.c"
	if grep -E 'call[lq]? (dot|bigsum|mkpte|mkpt|mkbig|mkodd)$' "$CC_TMP/s.s"; then
		echo "run_inline_struct: -m$m -O2: the calls above are not inlined"
		exit 1
	fi
	gcc -m$m -O1 -o "$CC_TMP/sg" "$CC_TMP/s.c"
	want=$("$CC_TMP/sg")
	for o in O1 O2; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/sc" "$CC_TMP/s.c"
		got=$("$CC_TMP/sc")
		if [ "$got" != "$want" ]; then
			echo "run_inline_struct: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_inline_struct: ok"
