#!/bin/sh
# A plain load of what a store through a pointer just wrote -- a pointer
# of unknown object, a parameter's member -- with nothing between that
# may write memory, reads the value stored, as gcc's value numbering
# reads it.  Linux's virtiovf_init_legacy_io() stores a constant (24 plus
# what a static function returns) to virtvdev->bar0_virtual_buf_size,
# an u8 member, and BUILD_BUG_ON()s that the member it reads back is a
# power of two: reading it back from memory lost the constant, and
# virtio_vfio_pci kept a call of an undefined __compiletime_assert_N.  A
# call, or a store through another pointer that may reach the bytes,
# between the two keeps the load, as gcc keeps it.  -O2, -m64 and -m32,
# both code paths, every function through MIR; and the program, run,
# prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_cse_store_ptr_load: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
extern void member_bad(void), word_bad(void), long_bad(void), call_kept(void), alias_kept(void);
extern int read_info(void *);
extern void touch(void);

struct dev { unsigned short device; };
struct vd { struct dev *pdev; int x; unsigned char sz; long l; };

static int config_size(unsigned short device) { return 8; }
static inline __attribute__((always_inline)) _Bool pow2(unsigned long n)
{
	return n - 1 < (n ^ (n - 1));
}

int member(struct vd *v)
{
	struct dev *pdev = v->pdev;
	int ret = read_info(v);

	if (ret)
		return ret;
	v->sz = 24 + config_size(pdev->device);
	if (!pow2(v->sz))
		member_bad();
	return v->sz;
}

int word(int *p, int x)
{
	*p = x & 0xff;
	if (*p > 255)
		word_bad();
	return *p;
}

long longs(struct vd *v, long x)
{
	v->l = x & 0xffff;
	if (v->l > 0xffff)
		long_bad();
	return v->l;
}

/* a call between may write it */
int called(struct vd *v)
{
	v->sz = 32;
	touch();
	if (v->sz != 32)
		call_kept();
	return v->sz;
}

/* a store through a pointer that may reach it */
int aliased(struct vd *v, unsigned char *q)
{
	v->sz = 32;
	*q = 5;
	if (v->sz != 32)
		alias_kept();
	return v->sz;
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads, kepts;
void member_bad(void) { bads++; }
void word_bad(void) { bads++; }
void long_bad(void) { bads++; }
void call_kept(void) { kepts++; }
void alias_kept(void) { kepts++; }
int read_info(void *v) { return v == 0; }
void touch(void) { }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	struct dev d = { 0x1041 };
	struct vd v = { &d, 0, 0, 0 };
	int a = member(&v);
	int b = word(&v.x, 0x1234);
	long c = longs(&v, -77);
	int e = called(&v);
	int f = aliased(&v, &v.sz);

	printf("%d %d %ld %d %d bads %d kepts %d\n", a, b, c, e, f, bads, kepts);
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
			echo "run_cse_store_ptr_load: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_cse_store_ptr_load: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ "$(echo $want)" != "alias_kept call_kept" ]; then
			echo "run_cse_store_ptr_load: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_cse_store_ptr_load: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_cse_store_ptr_load: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_cse_store_ptr_load: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_cse_store_ptr_load: ok"
