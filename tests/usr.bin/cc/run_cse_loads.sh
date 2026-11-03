#!/bin/sh
# A plain load of an address a plain load read before, on every path to
# it, with nothing between that may write it, reads what the first read,
# as gcc's value numbering has it -- so what the first's branch tells of
# it holds of the second.  Linux's fs/fuse/notify.c returns when
# `outarg.offset >= MAX_LFS_FILESIZE`, then reads `pos = outarg.offset`,
# and min() asks of `MAX_LFS_FILESIZE - pos` that it is not negative: the
# old code read outarg.offset twice and left a call of an undefined
# __compiletime_assert_N.  Those of the shapes here (a local structure
# whose address escapes, a member through a pointer, a store to another
# local between) fold and leave no reference, as gcc's do; and never a
# volatile read (through a volatile pointer, a READ_ONCE cast, a volatile
# object, member, array element or an element of a volatile structure's
# array member), an atomic one, nor a read past a store through a pointer,
# a call or an asm that clobbers memory: what gcc keeps, the code keeps.
# Two volatile reads of one address stay two loads.  -O2, -m64 and -m32,
# both code paths, every function through MIR; and the program, run (the
# store, call and asm between write what is read again), prints what gcc's
# prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_cse_loads: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
#define LIMIT 0x7fffffffu
/* (unsigned: no overflow the compilers may assume away; LIMIT - x for x < LIMIT is at most LIMIT) */
#define NOT_NEG(y) (__builtin_constant_p((y) <= LIMIT) && (y) <= LIMIT)
struct out { unsigned int nodeid, offset, size; unsigned int arr[4]; };
extern int fill(void *p, unsigned int n);
extern void touch(void);
extern void local_bad(void), ptr_bad(void), slot_bad(void), wide_bad(void);
extern void vptr_kept(void), once_kept(void), vglob_kept(void), vobj_kept(void), varr_kept(void);
extern void vmemb_kept(void), atomic_kept(void), store_kept(void), call_kept(void), asm_kept(void);
extern volatile unsigned int vglob;
extern volatile struct out vobj;
extern volatile unsigned int varr[4];
extern _Atomic unsigned int aglob;
int sink;

/* fs/fuse/notify.c's shape: a local structure filled through its address */
int local(unsigned int size)
{
	struct out o;
	unsigned int pos, y;
	int err = fill(&o, sizeof(o));

	if (err)
		return err;
	if (size - sizeof(o) != o.size)
		return -22;
	if (o.offset >= LIMIT)
		return -22;
	sink = o.nodeid;
	pos = o.offset;
	y = LIMIT - pos;
	if (!NOT_NEG(y))
		local_bad();
	return y;
}
int ptr(const struct out *p)
{
	unsigned int y;

	if (p->offset >= LIMIT)
		return -22;
	y = LIMIT - p->offset;
	if (!NOT_NEG(y))
		ptr_bad();
	return y;
}
/* a store to another local's bytes between: not the structure's */
int slot(int i, int v)
{
	struct out o;
	int t[8];
	unsigned int y;

	if (fill(&o, sizeof(o)) || fill(t, sizeof(t)))
		return -1;
	if (o.offset >= LIMIT)
		return -22;
	t[3] = v;
	y = LIMIT - o.offset;
	if (!NOT_NEG(y))
		slot_bad();
	return y + t[(i + 1) & 7];
}
#ifdef __x86_64__
/* the 64-bit original, unsigned 64-bit offset read into a signed loff_t */
struct out64 { unsigned long long nodeid, offset; unsigned int size, pad; };
long long wide(unsigned int size)
{
	struct out64 o;
	long long pos, y;
	int err = fill(&o, sizeof(o));

	if (err)
		return err;
	if (size - sizeof(o) != o.size)
		return -22;
	if (o.offset >= (long long)(~0ULL >> 1))
		return -22;
	pos = o.offset;
	y = (long long)(~0ULL >> 1) - pos;
	if (!(__builtin_constant_p(y >= 0) && y >= 0))
		wide_bad();
	return y;
}
#else
long long wide(unsigned int size)
{
	return size;
}
#endif
int vptr(volatile struct out *p)
{
	unsigned int y;

	if (p->offset >= LIMIT)
		return -22;
	y = LIMIT - p->offset;
	if (!NOT_NEG(y))
		vptr_kept();
	return y;
}
int once(struct out *p)
{
	unsigned int y;

	if (*(volatile unsigned int *)&p->offset >= LIMIT)
		return -22;
	y = LIMIT - *(volatile unsigned int *)&p->offset;
	if (!NOT_NEG(y))
		once_kept();
	return y;
}
int vg(void)
{
	unsigned int y;

	if (vglob >= LIMIT)
		return -22;
	y = LIMIT - vglob;
	if (!NOT_NEG(y))
		vglob_kept();
	return y;
}
int vo(void)
{
	unsigned int y;

	if (vobj.offset >= LIMIT)
		return -22;
	y = LIMIT - vobj.offset;
	if (!NOT_NEG(y))
		vobj_kept();
	return y;
}
int va(void)
{
	unsigned int y;

	if (varr[1] >= LIMIT)
		return -22;
	y = LIMIT - varr[1];
	if (!NOT_NEG(y))
		varr_kept();
	return y;
}
int vm(void)
{
	unsigned int y;

	if (vobj.arr[2] >= LIMIT)
		return -22;
	y = LIMIT - vobj.arr[2];
	if (!NOT_NEG(y))
		vmemb_kept();
	return y;
}
int at(void)
{
	unsigned int y;

	if (aglob >= LIMIT)
		return -22;
	y = LIMIT - aglob;
	if (!NOT_NEG(y))
		atomic_kept();
	return y;
}
int st(struct out *p, unsigned int *q)
{
	unsigned int y;

	if (p->offset >= LIMIT)
		return -22;
	*q = 0x7ffffff0u;
	y = LIMIT - p->offset;
	if (!NOT_NEG(y))
		store_kept();
	return y;
}
int ca(struct out *p)
{
	unsigned int y;

	if (p->offset >= LIMIT)
		return -22;
	touch();
	y = LIMIT - p->offset;
	if (!NOT_NEG(y))
		call_kept();
	return y;
}
int asmw(struct out *p, unsigned int *q)
{
	unsigned int y;

	if (p->offset >= LIMIT)
		return -22;
	__asm__ __volatile__("movl $0x7ffffff0, (%0)" : : "r"(q) : "memory");
	y = LIMIT - p->offset;
	if (!NOT_NEG(y))
		asm_kept();
	return y;
}
/* two volatile reads of one address are two loads */
unsigned int two(volatile unsigned int *p)
{
	unsigned int a = *p;
	unsigned int b = *p;

	return a - b;
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
#include <string.h>
int bads, kepts;
static unsigned int *touched;
static unsigned char filler[64];
int fill(void *p, unsigned int n) { memcpy(p, filler, n); return 0; }
void touch(void) { if (touched) *touched = 0x7ffffff0u; }
void local_bad(void) { bads++; }
void ptr_bad(void) { bads++; }
void slot_bad(void) { bads++; }
void wide_bad(void) { bads++; }
void vptr_kept(void) { kepts++; }
void once_kept(void) { kepts++; }
void vglob_kept(void) { kepts++; }
void vobj_kept(void) { kepts++; }
void varr_kept(void) { kepts++; }
void vmemb_kept(void) { kepts++; }
void atomic_kept(void) { kepts++; }
void store_kept(void) { kepts++; }
void call_kept(void) { kepts++; }
void asm_kept(void) { kepts++; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
volatile unsigned int vglob = 5;
volatile struct out vobj = { 1, 6, 3, { 1, 2, 7, 4 } };
volatile unsigned int varr[4] = { 1, 8, 3, 4 };
_Atomic unsigned int aglob = 9;

int main(void)
{
	struct out o = { 1, 10, 3, { 0, 0, 0, 0 } };
	unsigned int k;
	unsigned int sz;
	long long w;
	int r[12];

	for (k = 0; k < sizeof(filler); k++)
		filler[k] = (unsigned char)(k * 7 + 1);
	memcpy(&sz, filler + 8, sizeof(sz));
	r[0] = local(sz + (unsigned int)sizeof(o));
	r[1] = ptr(&o);
	r[2] = slot(3, 40);
	memcpy(&sz, filler + 16, sizeof(sz));
	w = wide(sz + 24);
	r[3] = vptr(&o);
	r[4] = once(&o);
	r[5] = vg();
	r[6] = vo();
	r[7] = va();
	r[8] = vm();
	r[9] = at();
	r[10] = st(&o, &o.offset);
	o.offset = 11;
	touched = &o.offset;
	r[11] = ca(&o);
	o.offset = 12;
	k = (unsigned int)asmw(&o, &o.offset);
	/* (not how many of the *_bad and *_kept functions ran: __builtin_constant_p is gcc's to decide in main) */
	printf("%d %d %d %lld %d %d %d %d %d %d %d %d %d %u %u\n", r[0], r[1], r[2], w, r[3], r[4], r[5], r[6], r[7],
	       r[8], r[9], r[10], r[11], k, two(&vglob));
	return 0;
}
EOF2
cat "$CC_TMP/defs.c" "$CC_TMP/u.c" "$CC_TMP/main.c" >"$CC_TMP/p.c"

undefined() {
	nm -u "$1" | awk '{ print $NF }' | grep -E '_(bad|kept)$' | sort || true
}

# the reads of memory through a pointer, not the frame, of function two in assembly file $1
loads_of_two() {
	sed -n '/^two:/,/ret/p' "$1" | grep '(%' | grep -vc 'bp)\|sp)' || true
}

fail=0
for m in 64 32; do
	gcc -m$m -O2 -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c" 2>/dev/null || continue
	for o in O2; do
		gcc -m$m -$o -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c"
		want=$(undefined "$CC_TMP/g.o")
		gcc -m$m -$o -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
		want_run=$("$CC_TMP/g")
		for path in mir legacy; do
			run="env CC_MIR_STATS=1"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			what="-m$m -$o $path"
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
				echo "run_cse_loads: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_cse_loads: $what: a function fell back from MIR"
				fail=1
			fi
			got=$(undefined "$CC_TMP/c.o")
			if [ "$(echo $got)" != "$(echo $want)" ]; then
				echo "run_cse_loads: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
				fail=1
			fi
			$run "$CC_BIN" -m$m -$o -S -o "$CC_TMP/c.s" "$CC_TMP/u.c" 2>/dev/null
			if [ "$(loads_of_two "$CC_TMP/c.s")" -ne 2 ]; then
				echo "run_cse_loads: $what: two volatile reads made $(loads_of_two "$CC_TMP/c.s") loads"
				fail=1
			fi
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
				echo "run_cse_loads: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_cse_loads: $what: a function of the program fell back from MIR"
				fail=1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
			got_run=$("$CC_TMP/c")
			if [ "$got_run" != "$want_run" ]; then
				echo "run_cse_loads: $what prints [$got_run], gcc's [$want_run]"
				fail=1
			fi
		done
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_cse_loads: ok"
