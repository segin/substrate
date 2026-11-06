#!/bin/sh
# A parameter of a static function that only direct calls reach holds
# what those calls pass it, as gcc's interprocedural range propagation
# knows: a compare its range decides folds in the function, inlined or
# not.  Linux's mm/vmscan.c has read_ctrl_pos(), too large to inline,
# with a min(tier, 3U) whose BUILD_BUG_ON asks that tier is not negative
# -- every call passes 0, 4 or a counter of `for (tier = 1; tier < 4U;
# tier++)` -- and kernel/bpf/arena.c's arena_alloc_pages() a
# min(page_cnt, ...) of a long every call passes a u32: each left a call
# of an undefined __compiletime_assert_N.  What must fold leaves no
# reference gcc's code does not; a parameter some call passes anything,
# or a negative value, stays as gcc keeps it.  -O2 (gcc's -O1 does not
# propagate ranges between functions), -m64 and -m32, both code paths,
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
	echo "run_fold_param_ranges: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
#define statically_true(x) (__builtin_constant_p(x) && (x))
#define is_nonneg(x) statically_true((long long)(x) >= 0)
#define sign_use(x) (((typeof(x))(-1)) < (typeof(x))1 ? (2 + is_nonneg(x)) : (1 + 2 * (sizeof(x) < 4)))
#define min(a, b) ({ __auto_type ua = (a); __auto_type ub = (b); \
	do { if (!(sign_use(ua) & sign_use(ub))) min_bad(); } while (0); ua < ub ? ua : ub; })
extern void min_bad(void), neg_kept(void), any_kept(void);
extern void sink(long);
struct pos { long a, b; int gain; };

static void read_pos(struct pos *p, int tier, int gain, const unsigned long *hist)
{
	int i;

	p->gain = gain;
	p->a = p->b = 0;
	for (i = tier % 4U; i <= min(tier, 4U - 1); i++) {
		p->a += (long)(hist[i] * 3 + 1);
		p->b += (long)(hist[i + 4] ^ (unsigned long)i);
		sink(p->a + p->b);
		sink(p->a - p->b * gain);
	}
	sink(p->a * 7 + p->b * 5);
}
int tier_idx(const unsigned long *h)
{
	struct pos sp, pv;
	int tier;

	read_pos(&sp, 0, 2, h);
	for (tier = 1; tier < 4U; tier++) {
		read_pos(&pv, tier, 3, h);
		if (pv.a * sp.gain > sp.a * pv.gain)
			break;
	}
	return tier - 1;
}
int type_scan(const unsigned long *h, int swappiness)
{
	struct pos sp, pv;

	read_pos(&sp, 4U, swappiness, h);
	read_pos(&pv, 4U, 200 - swappiness, h);
	return sp.a > pv.a;
}

static long alloc_pages(long uaddr, long page_cnt, int node)
{
	long n = min(page_cnt, 1024UL);
	long done;

	for (done = 0; done < n; done++) {
		sink(uaddr + done * 4096);
		sink(n + node);
		sink(done ^ n);
	}
	return done + page_cnt;
}
long alloc1(unsigned int page_cnt, long addr)
{
	if (!page_cnt)
		return 0;
	return alloc_pages(addr, page_cnt, -1);
}
long alloc2(unsigned int page_cnt, long addr)
{
	return alloc_pages(addr, page_cnt, 0);
}

static int scaled(int x, int k)
{
	int i;
	int s = 0;

	if (!is_nonneg(x))
		neg_kept();
	if (!is_nonneg(k))
		any_kept();
	for (i = 0; i < 4; i++) {
		sink(x * i + k);
		sink(x ^ i);
		s += x - i;
	}
	return s;
}
int scale1(int k) { return scaled(3, k); }
int scale2(int k) { return scaled(-1, k + 1); }
EOF2

# the same code, run (what __builtin_constant_p answers is each compiler's own, and not printed)
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static long sum;
void min_bad(void) { }
void neg_kept(void) { }
void any_kept(void) { }
void sink(long v) { sum = sum * 31 + v; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	static const unsigned long h[8] = { 5, 1, 9, 2, 6, 5, 3, 5 };
	int t = tier_idx(h);
	int s = type_scan(h, 60);
	long a = alloc1(3000, 7);
	long b = alloc1(0, 7);
	long c = alloc2(1, 9);
	int x = scale1(4);
	int y = scale2(-6);

	printf("%d %d %ld %ld %ld %d %d sum %ld\n", t, s, a, b, c, x, y, sum);
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
	# (gcc propagates ranges between functions from -O2)
	for o in O2; do
		gcc -m$m -$o -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c"
		want=$(undefined "$CC_TMP/g.o")
		gcc -m$m -$o -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
		want_run=$("$CC_TMP/g")
		for path in mir legacy; do
			run="env CC_MIR_STATS=2"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			what="-m$m -$o $path"
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
				echo "run_fold_param_ranges: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_fold_param_ranges: $what: a function fell back from MIR"
				fail=1
			fi
			got=$(undefined "$CC_TMP/c.o")
			if [ "$got" != "$want" ]; then
				echo "run_fold_param_ranges: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
				fail=1
			fi
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
				echo "run_fold_param_ranges: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_fold_param_ranges: $what: a function of the program fell back from MIR"
				fail=1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
			got_run=$("$CC_TMP/c")
			if [ "$got_run" != "$want_run" ]; then
				echo "run_fold_param_ranges: $what prints"
				echo "$got_run"
				echo "gcc's:"
				echo "$want_run"
				fail=1
			fi
		done
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_fold_param_ranges: ok"
