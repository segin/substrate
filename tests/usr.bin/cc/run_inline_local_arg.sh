#!/bin/sh
# A function declared inline, too big to inline everywhere, is inlined
# where a call passes it the address of the caller's own local (one that
# only such calls take), up to gcc's max-inline-insns-single: gcc does,
# and reckons what it loads and stores through the pointer gone, the
# local's fields the caller's values then.  And a call an inlined body
# brings in is weighed again where it now stands.  Linux's generic
# page-table walkers (drivers/iommu/generic_pt) set up an on-stack
# `struct pt_state` with a constant level in each level's walker, an
# always_inline body, and hand it to pt_range_to_index(&pts) and
# pt_load_entry(&pts); the leaf install at level 0 then has a
# BUILD_BUG_ON() (FIELD_PREP() of a size that does not fit) in the arm
# for another level's size, which only the level known proves dead -- a
# call of an undefined __compiletime_assert_N that stopped vmlinux
# linking with CONFIG_IOMMU_PT_AMDV1.  What gcc folds leaves no
# reference; a level not known stays, as gcc keeps it.  -O2, -m64 and
# -m32, both code paths, every function through MIR; and the program,
# run, prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_inline_local_arg: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
extern void size_bad(void), level_bad(void);
extern void size_kept(void);
struct range {
	unsigned long va;
	unsigned long last_va;
	unsigned char top_level;
	unsigned char max_vasz_lg2;
};
struct state {
	struct range *range;
	unsigned long *table;
	unsigned long *table_lower;
	unsigned long entry;
	int type;
	unsigned short index;
	unsigned short end_index;
	unsigned char level;
};

static inline __attribute__((always_inline)) struct state init(struct range *range, unsigned int level,
							      unsigned long *table)
{
	struct state s = { .range = range, .table = table, .level = level };

	return s;
}
static inline unsigned int item_lg2sz(const struct state *s)
{
	return 12 + 9 * s->level;
}
/* each more than about a call's worth of code: inline only as a hint */
static inline unsigned int range_to_index(const struct state *s)
{
	unsigned int isz = item_lg2sz(s);

	if (s->range->top_level == s->level) {
		unsigned long va = s->range->va;

		if (s->range->max_vasz_lg2 != 64)
			va &= (1UL << s->range->max_vasz_lg2) - 1;
		return (unsigned int)(va >> isz);
	}
	return (unsigned int)((s->range->va >> isz) & 511);
}
static inline int load_entry_raw(struct state *s)
{
	unsigned long e = s->table[s->index];

	s->entry = e;
	if (!(e & 1))
		return 0;
	if (s->level == 0 || ((e >> 9) & 7) == 0 || ((e >> 9) & 7) == 7)
		return 2;
	return 1;
}
static inline void load_entry(struct state *s)
{
	s->type = load_entry_raw(s);
	if (s->type == 1)
		s->table_lower = (unsigned long *)(s->entry & ~4095UL);
}
static inline int load_single_entry(struct state *s)
{
	s->index = range_to_index(s);
	load_entry(s);
	return s->type;
}
static inline __attribute__((always_inline)) void install_leaf(struct state *s, unsigned long oa,
							       unsigned int oasz)
{
	unsigned int isz = item_lg2sz(s);
	unsigned long *p = s->table + s->index;

	if (oasz == isz) {
		*p = oa | 1;
	} else {
		/* another level's: as Linux's FIELD_PREP() of a size that does not fit, a call of nothing */
		if (oasz - isz > 9)
			size_bad();
		p[oasz - isz] = oa | 3;
	}
	s->entry = *p;
}
typedef int (*level_fn_t)(struct range *range, unsigned long oa, unsigned int level, unsigned long *table);

static inline __attribute__((always_inline)) int do_map(struct range *range, unsigned long oa, unsigned int level,
							unsigned long *table, level_fn_t descend)
{
	struct state s = init(range, level, table);

	s.type = load_single_entry(&s);
	if (s.level == 0) {
		if (s.type != 0)
			return -17;
		install_leaf(&s, oa, 12);
		return 0;
	}
	if (s.type == 1)
		return descend(s.range, oa, s.level - 1, s.table_lower);
	return -11;
}
static int map_err(struct range *range, unsigned long oa, unsigned int level, unsigned long *table)
{
	if (level > 2)
		level_bad();
	return -71;
}
static inline int map0(struct range *range, unsigned long oa, unsigned int level, unsigned long *table)
{
	return do_map(range, oa, 0, table, map_err);
}
static inline int map1(struct range *range, unsigned long oa, unsigned int level, unsigned long *table)
{
	return do_map(range, oa, 1, table, map0);
}
static inline int map2(struct range *range, unsigned long oa, unsigned int level, unsigned long *table)
{
	return do_map(range, oa, 2, table, map1);
}
int map_page(struct range *range, unsigned long oa, unsigned long *table)
{
	if (range->top_level == 0)
		return map0(range, oa, 0, table);
	if (range->top_level == 1)
		return map1(range, oa, 1, table);
	return map2(range, oa, 2, table);
}
/* a level not known: the other arm stays, as gcc keeps it */
int map_any(struct range *range, unsigned long oa, unsigned int level, unsigned long *table)
{
	struct state s = init(range, level, table);
	unsigned int isz;

	s.type = load_single_entry(&s);
	isz = item_lg2sz(&s);
	if (isz != 12)
		size_kept();
	/* (the helpers are called from more than one place, as Linux's are) */
	s.index = range_to_index(&s) + 1;
	load_entry(&s);
	return s.type;
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads, kepts;
void size_bad(void) { bads++; }
void level_bad(void) { bads++; }
void size_kept(void) { kepts++; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
static unsigned long top[512] __attribute__((aligned(4096)));
static unsigned long low[512] __attribute__((aligned(4096)));

int main(void)
{
	struct range r = { 0x5000, 0x5fff, 0, 30 };
	int a, b, c, d, e;

	a = map_page(&r, 0x123000, low);
	b = map_page(&r, 0x124000, low);
	r.top_level = 1;
	r.va = 0x203000;
	top[1] = (unsigned long)low | (1UL << 9) | 1;
	c = map_page(&r, 0x777000, top);
	r.top_level = 2;
	d = map_page(&r, 0x778000, top);
	e = map_any(&r, 0x1000, 1, top);
	printf("%d %d %d %d %d %lx %lx bads %d kepts %d\n", a, b, c, d, e, low[5], low[3], bads, kepts);
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
			echo "run_inline_local_arg: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_inline_local_arg: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ "$(echo $want)" != "size_kept" ]; then
			echo "run_inline_local_arg: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_inline_local_arg: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_inline_local_arg: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_inline_local_arg: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_inline_local_arg: ok"
