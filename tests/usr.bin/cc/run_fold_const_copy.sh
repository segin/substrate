#!/bin/sh
# A local structure copied whole from an element of a static const table,
# at a constant index, and then only read, reads the table's initializer:
# gcc's scalar replacement and value numbering see through the copy.
# Linux's pack_fields() checks each element of its field table so --
# `typeof(_f[0]) __f = _f[i];` then BUILD_BUG_ON()s on __f's fields -- and
# drivers/net/ethernet/intel/ice/ice_common.c left 683 calls of undefined
# __compiletime_assert_N while the copy stayed memory, the ice module
# unloadable.  What gcc folds leaves no reference; a copy that something
# may write after it (its address passed on), or from a table that is not
# const, stays, as gcc keeps it.  -O2, -m64 and -m32, both code paths,
# every function through MIR; and the program, run, prints what gcc's
# prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_fold_const_copy: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
extern void order_bad(void), size_bad(void), width_bad(void), small_bad(void);
extern void touched_kept(void), mutable_kept(void);
extern void touch(void *p);
struct field {
	unsigned short startbit;
	unsigned short endbit;
	unsigned short offset;
	unsigned short size;
};
struct small {
	unsigned char lo, hi;
	signed char delta;
};
static const struct field fields[] = {
	{ 31, 0, 0, 4 },
	{ 47, 32, 4, 2 },
	{ 55, 48, 6, 1 },
	{ 127, 64, 8, 8 },
};
static const struct small smalls[] = { { 1, 9, -3 }, { 2, 7, -1 } };
static struct field mutable_fields[] = { { 31, 0, 0, 4 } };

#define CHECK_FIELD(tbl, i)                                                              \
	({                                                                               \
		typeof(&(tbl)[0]) _f = (tbl);                                            \
		typeof(_f[0]) __f = _f[i];                                               \
		if (!(__f.startbit > __f.endbit))                                         \
			order_bad();                                                     \
		if (__f.size != 1 && __f.size != 2 && __f.size != 4 && __f.size != 8)    \
			size_bad();                                                      \
		if (__f.startbit - __f.endbit >= 8 * __f.size)                           \
			width_bad();                                                     \
	})

unsigned int pack(unsigned char *buf, unsigned int n)
{
	unsigned int i, sum = 0;

	CHECK_FIELD(fields, 0);
	CHECK_FIELD(fields, 1);
	CHECK_FIELD(fields, 2);
	CHECK_FIELD(fields, 3);
	for (i = 0; i < n && i < sizeof(fields) / sizeof(fields[0]); i++)
		sum += buf[fields[i].offset] << fields[i].size;
	return sum;
}

int small_delta(int x)
{
	struct small s = smalls[1];

	if (s.lo >= s.hi || s.delta >= 0)
		small_bad();
	return x + s.delta;
}

/* the copy's address passed on: it may be written, and gcc keeps the test */
int touched(void)
{
	struct field f = fields[1];

	touch(&f);
	if (f.size != 2)
		touched_kept();
	return f.size;
}

/* a table that is not const, and written: its element is what the program left there */
void set_mutable_size(unsigned short size)
{
	mutable_fields[0].size = size;
}

int mutable_size(void)
{
	struct field f = mutable_fields[0];

	if (f.size != 4)
		mutable_kept();
	return f.size;
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads, kepts;
void order_bad(void) { bads++; }
void size_bad(void) { bads++; }
void width_bad(void) { bads++; }
void small_bad(void) { bads++; }
void touched_kept(void) { kepts++; }
void mutable_kept(void) { kepts++; }
void touch(void *p) { ((unsigned short *)p)[3] = 5; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	unsigned char buf[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	unsigned int a = pack(buf, 4);
	int b = small_delta(10);
	int c = touched();
	int d;

	set_mutable_size(3);
	d = mutable_size();
	printf("%u %d %d %d bads %d kepts %d\n", a, b, c, d, bads, kepts);
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
		run="env CC_MIR_STATS=2"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		what="-m$m -O2 $path"
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
			echo "run_fold_const_copy: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_fold_const_copy: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ "$(echo $want)" != "mutable_kept touched_kept" ]; then
			echo "run_fold_const_copy: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_fold_const_copy: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_fold_const_copy: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_fold_const_copy: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_fold_const_copy: ok"
