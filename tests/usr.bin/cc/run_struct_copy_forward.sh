#!/bin/sh
# A slot a memcpy fills whole and nothing but loads then read, in the
# same straight run with nothing between that may write the source, is
# read from the source: the copy and the slot go.  An inlined function's
# structure argument is such a copy -- lowering copies a structure
# parameter at entry -- and each was stored to the frame and read back.
# Where the source may be written between the copy and a read (a store
# through a pointer, a call), the copy stays.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_struct_copy_forward: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/n.c" <<'EOF2'
struct pt { long x, y; };
static inline long dot(struct pt p, struct pt q) { return p.x * q.x + p.y * q.y; }
long norm2(const struct pt *v, int n)
{
	long t = 0;
	int i;

	for (i = 0; i < n; ++i)
		t += dot(v[i], v[i]);
	return t;
}
EOF2
"$CC_BIN" -m64 -O2 -S -o "$CC_TMP/n.s" "$CC_TMP/n.c"
if grep -E '\(%rbp\)' "$CC_TMP/n.s"; then
	echo "run_struct_copy_forward: the structure arguments were copied to the frame"
	exit 1
fi

cat >"$CC_TMP/c.c" <<'EOF2'
#include <stdio.h>

struct pt { long x, y; };
struct big { long a[6]; };
extern void touch(struct pt *);

static inline long first_then_write(struct pt p, struct pt *w)
{
	long a = p.x;

	w->x = 100;	/* the source itself: p keeps what it was passed */
	return a + p.x + p.y;
}

static inline long across_call(struct pt p, struct pt *w)
{
	touch(w);
	return p.x * 10 + p.y;
}

static inline long sum_big(struct big b)
{
	return b.a[0] + b.a[3] + b.a[5];
}

static inline long write_param(struct pt p)
{
	p.x += 7;	/* written: a copy of its own */
	return p.x + p.y;
}

void touch(struct pt *p) { p->x += 1000; p->y = -1; }

long __attribute__((noinline)) run(struct pt *s, struct big *b)
{
	long r = first_then_write(*s, s);

	r += across_call(*s, s);
	r += sum_big(*b);
	r += write_param(*s);
	return r * 3 + s->x + s->y;
}

int main(void)
{
	struct pt s = {3, 4};
	struct big b = {{1, 2, 3, 4, 5, 6}};

	printf("%ld\n", run(&s, &b));
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/cg" "$CC_TMP/c.c"
	want=$("$CC_TMP/cg")
	for o in O1 O2; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/cc" "$CC_TMP/c.c"
		got=$("$CC_TMP/cc")
		if [ "$got" != "$want" ]; then
			echo "run_struct_copy_forward: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_struct_copy_forward: ok"
