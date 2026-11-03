#!/bin/sh
# A structure an always_inline function builds and returns by value is
# copied from its buffer to the local that takes it, and that local is
# written after: Linux's `struct pt_state pts = pt_init(range, level,
# table)`, whose level the walkers of drivers/iommu/generic_pt then test
# to prove a BUILD_BUG_ON() for another level dead.  mem.c makes the local
# the buffer itself (the buffer zeroed and stored into for nothing but
# the copy), so the fields the initializer set are known -- also where an
# inlined function given the local's address has a local of its own,
# whose allocation once made the structure look passed out.  What gcc
# folds leaves no reference; a level the caller passes stays, as gcc
# keeps it.  -O2, -m64 and -m32, both code paths, every function through
# MIR; and the program, run, prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_slot_copy_coalesce: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
extern void lvl_bad(void), idx_bad(void), peek_bad(void);
extern void lvl_kept(void);
struct st {
	void *r;
	long a;
	long b;
	int t;
	unsigned short i;
	unsigned char lvl;
};

static inline __attribute__((always_inline)) struct st mk(void *r, unsigned int lvl)
{
	struct st s = { .r = r, .lvl = lvl };

	return s;
}
/* its own array stays memory: an allocation in what is inlined, the structure's address its first value */
static inline __attribute__((always_inline)) int peek(struct st *p, int k)
{
	int tmp[4];
	int j;

	for (j = 0; j < 4; j++)
		tmp[j] = j * (int)p->i;
	return tmp[k & 3];
}
long walk(void *r, long x)
{
	struct st s = mk(r, 0);

	s.a = x;
	s.i = (unsigned short)(x + 1);
	if (s.lvl != 0)
		lvl_bad();
	if (s.b != 0)
		idx_bad();
	return s.a + s.i;
}
long walk_peek(void *r, long x)
{
	struct st s = mk(r, 0);

	s.i = (unsigned short)x;
	s.a = peek(&s, (int)x);
	if (s.lvl != 0)
		peek_bad();
	return s.a;
}
long walk_n(void *r, long x, unsigned int n)
{
	struct st s = mk(r, n);

	s.a = x;
	if (s.lvl != 0)
		lvl_kept();
	return s.a + s.lvl;
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads, kepts;
void lvl_bad(void) { bads++; }
void idx_bad(void) { bads++; }
void peek_bad(void) { bads++; }
void lvl_kept(void) { kepts++; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	long a = walk(&bads, 41);
	long b = walk_peek(&bads, 7);
	long c = walk_n(&bads, 5, 3);
	long d = walk_n(&bads, 6, 0);

	printf("%ld %ld %ld %ld bads %d kepts %d\n", a, b, c, d, bads, kepts);
	return 0;
}
EOF2
cat "$CC_TMP/defs.c" "$CC_TMP/u.c" "$CC_TMP/main.c" >"$CC_TMP/p.c"

undefined() {
	nm -u "$1" | awk '{ print $NF }' | grep -E '_(bad|kept)$' | sort || true
}

fail=0
for m in 64 32; do
	gcc -m$m -O2 -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c" 2>/dev/null || continue
	want=$(undefined "$CC_TMP/g.o")
	gcc -m$m -O2 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
	want_run=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env CC_MIR_STATS=1"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		what="-m$m -O2 $path"
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
			echo "run_slot_copy_coalesce: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_slot_copy_coalesce: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ "$(echo $want)" != "lvl_kept" ]; then
			echo "run_slot_copy_coalesce: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_slot_copy_coalesce: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_slot_copy_coalesce: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_slot_copy_coalesce: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_slot_copy_coalesce: ok"
