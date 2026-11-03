#!/bin/sh
# A compare of two constant offsets from one object's address is the
# compare of the offsets, and the address of a stack slot, or of an object
# or a function the file defines, is not null -- as gcc folds them, -fno-
# delete-null-pointer-checks or not.  Linux's ksm.c has
# BUILD_BUG_ON(&migrate_nodes.prev <= &migrate_nodes) of its own static
# list head, and landlock's always_inline copy_min_struct_from_user()
# BUILD_BUG_ON(!dst) of its caller's local: each left a call of an
# undefined __compiletime_assert_N and vmlinux did not link.  What must
# fold leaves no reference gcc's code does not; what may be null -- a
# weak symbol's address, one only declared, an arbitrary pointer -- or two
# objects' addresses, cc leaves to the program.  -O1 and -O2, -m64 and
# -m32, with and without -fno-delete-null-pointer-checks, both code paths,
# every function through MIR; and a program prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_fold_address_compare: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function nothing defines; a *_kept one cc must keep
cat >"$CC_TMP/u.c" <<'EOF2'
struct list_head { struct list_head *next, *prev; };
static struct list_head nodes = { &nodes, &nodes };
struct list_head gnodes = { &gnodes, &gnodes };
static int table[64];
extern struct list_head ext_head;
extern struct list_head weak_head __attribute__((weak));
extern void le_bad(void), ge_bad(void), eq_bad(void), ne_bad(void), arr_bad(void), dst_bad(void);
extern void gl_bad(void), st_bad(void), fn_bad(void), slot_bad(void);
extern void weak_kept(void), ext_kept(void), ptr_kept(void), two_kept(void);
extern int cp(void *, const void *, unsigned long);
int defined_fn(void) { return 1; }
static inline __attribute__((always_inline)) int copy_in(void *const dst, unsigned long n, const void *src)
{
	if (!dst)
		dst_bad();
	if (!src)
		return -14;
	return cp(dst, src, n);
}
struct attr { unsigned long long a, b; };
int f(const void *u, void *p)
{
	struct attr ra;
	char buf[16];
	int e;

	if ((struct list_head *)&nodes.prev <= &nodes) le_bad();
	if ((struct list_head *)&nodes.prev >= &nodes + 1) ge_bad();
	if (&gnodes.prev == &gnodes.next) eq_bad();
	if ((void *)&nodes.next != (void *)&nodes) ne_bad();
	if (&table[10] < &table[3] || &table[64] <= &table[63]) arr_bad();
	if (&gnodes == 0 || !&nodes.prev) gl_bad();
	if ((void *)defined_fn == 0) fn_bad();
	if (buf + 4 == 0) slot_bad();
	if (&weak_head == 0) weak_kept();
	if (&ext_head == 0) ext_kept();
	if (p == 0) ptr_kept();
	if ((void *)&nodes == (void *)&gnodes) two_kept();
	e = copy_in(&ra, sizeof(ra), u);
	if (e)
		return e;
	e = copy_in(&gnodes.prev, sizeof(void *), u);
	if (e)
		return e;
	return (int)(ra.a + ra.b) + cp(buf, u, 4);
}
EOF2

# what the compares are
cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>
struct list_head { struct list_head *next, *prev; };
static struct list_head nodes = { &nodes, &nodes };
struct list_head gnodes = { &gnodes, &gnodes };
static int table[64];
struct list_head weak_head __attribute__((weak));
int defined_fn(void) { return 1; }
static inline __attribute__((always_inline)) int nonnull(void *const dst) { return dst != 0; }
__attribute__((noinline)) int is_null(void *p) { return p == 0; }
int main(void)
{
	struct list_head local;
	char buf[16];

	printf("%d %d %d %d\n", (struct list_head *)&nodes.prev <= &nodes, (struct list_head *)&nodes.prev >= &nodes + 1,
	       &gnodes.prev == &gnodes.next, (void *)&nodes.next == (void *)&nodes);
	printf("%d %d %d %d %d\n", &table[10] < &table[3], &table[64] > &table[63], &gnodes != 0, (void *)defined_fn != 0,
	       buf + 4 != 0);
	printf("%d %d %d %d %d\n", &weak_head == 0, nonnull(&local), nonnull(&gnodes.prev), is_null(buf),
	       (void *)&nodes == (void *)&gnodes);
	return 0;
}
EOF2

undefined() {
	nm -u "$1" | awk '{ print $NF }' | grep -E '_(bad|kept)$' | sort || true
}

fail=0
for m in 64 32; do
	gcc -m$m -O2 -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c" 2>/dev/null || continue
	for o in O1 O2; do
		for nd in "" -fno-delete-null-pointer-checks; do
			gcc -m$m -$o $nd -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c"
			gcc_bad=$(undefined "$CC_TMP/g.o" | grep '_bad$' || true)
			gcc -m$m -$o $nd -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
			want_run=$("$CC_TMP/g")
			for path in mir legacy; do
				run="env CC_MIR_STATS=1"
				if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
				what="-m$m -$o $nd $path"
				if ! $run "$CC_BIN" -m$m -$o $nd -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
					echo "run_fold_address_compare: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
					exit 1
				fi
				if grep 'not taken' "$CC_TMP/c.err"; then
					echo "run_fold_address_compare: $what: a function fell back from MIR"
					fail=1
				fi
				got=$(undefined "$CC_TMP/c.o")
				got_bad=$(echo "$got" | grep '_bad$' || true)
				got_kept=$(echo "$got" | grep '_kept$' || true)
				if [ "$got_bad" != "$gcc_bad" ]; then
					echo "run_fold_address_compare: $what leaves [$(echo $got_bad)] undefined, gcc [$(echo $gcc_bad)]"
					fail=1
				fi
				if [ "$(echo $got_kept)" != "ext_kept ptr_kept two_kept weak_kept" ]; then
					echo "run_fold_address_compare: $what keeps [$(echo $got_kept)], not all four"
					fail=1
				fi
				if ! $run "$CC_BIN" -m$m -$o $nd -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
					echo "run_fold_address_compare: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
					exit 1
				fi
				if grep 'not taken' "$CC_TMP/c.err"; then
					echo "run_fold_address_compare: $what: a function of the program fell back from MIR"
					fail=1
				fi
				gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
				got_run=$("$CC_TMP/c")
				if [ "$got_run" != "$want_run" ]; then
					echo "run_fold_address_compare: $what prints"
					echo "$got_run"
					echo "gcc's:"
					echo "$want_run"
					fail=1
				fi
			done
		done
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_fold_address_compare: ok"
