#!/bin/sh
# A compare whose operands' ranges answer it one way is that answer, as
# gcc's value-range propagation folds it: what each value can hold from
# its definitions -- one of two constants, a narrow load, a mask, an or of
# small values, a difference that cannot overflow -- and, under a branch
# on it, what the branch allows -- `len` inside `while (len > 0)`, a
# switch's case, `t` inside `for (t = 1; t < 4U; t++)`.  Linux's min()
# asks __builtin_constant_p((long long)x >= 0) && (long long)x >= 0 in a
# BUILD_BUG_ON of a signed x against an unsigned operand (fs/exec.c's
# copy_string_kernel(), ext4's ext4_mb_mark_bb()); kaslr's
# BUILD_BUG_ON(vaddr_start >= vaddr_end), mmu_gather's flags > 3,
# task_mmu's 58 < (c ? 52 : 46) - 12 and x2apic_savic's reg != 0x300 under
# `case 0x300:` are the others: each left a call of an undefined
# __compiletime_assert_N.  What must fold leaves no reference gcc's code
# does not; what no range decides -- a value after the loop, a sum that may
# overflow, an unsigned compare across the sign, a loaded int -- stays as
# gcc keeps it.  -O1 and -O2, -m64 and -m32, both code paths, every
# function through MIR; and the program, run, prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_fold_value_ranges: skip (no gcc)"
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
extern void min_bad(void), order_bad(void), flags_bad(void), shift_bad(void), case_bad(void);
extern void byte_bad(void), tier_bad(void), diff_bad(void);
extern void after_kept(void), sum_kept(void), span_kept(void), int_kept(void), loop_kept(void);
extern int copy(char *, const char *, unsigned int);
extern int has_feature(void);
extern int sel;
extern char *pos;

int copy_string(const char *s, int len)
{
	while (len > 0) {
		unsigned int n = min(len, 4096UL - ((unsigned long)pos & 4095));

		if (copy(pos, s, n))
			return -14;
		len -= n;
		pos += n;
		s += n;
	}
	if (len < -5)
		after_kept();
	return 0;
}

int order(void)
{
	unsigned long vend = (unsigned long)-4096;
	unsigned long vstart = sel ? (unsigned long)-0x100000 : (unsigned long)-0x200000;

	if (vstart >= vend)
		order_bad();
	if (vstart >= (unsigned long)-0x180000)
		span_kept();
	return vstart != 0;
}

static inline __attribute__((always_inline)) void *encode(void *page, unsigned long flags)
{
	if (flags > 3ul)
		flags_bad();
	return (void *)((unsigned long)page | flags);
}
void *batch[8];
int nr;
int remove_page(void *page, unsigned int nr_pages, _Bool delay_rmap)
{
	int flags = delay_rmap ? 1ul : 0;

	if (nr_pages == 1) {
		batch[nr++] = encode(page, flags);
	} else {
		flags |= 2ul;
		batch[nr++] = encode(page, flags);
	}
	return nr;
}

int pfn_bits(void)
{
	int b = (has_feature() ? 52 : 46) - 12;

	if (58 < b)
		shift_bad();
	return b;
}

static inline __attribute__((always_inline)) unsigned long long reg64(const char *regs, int reg)
{
	if (reg != 0x300)
		case_bad();
	return *(const unsigned long long *)(regs + reg);
}
unsigned int read_reg(const char *ap, unsigned int reg)
{
	switch (reg) {
	case 0x20:
	case 0x30:
		return *(const unsigned int *)(ap + reg);
	case 0x300:
		return (unsigned int)reg64(ap, reg);
	case 0x100 ... 0x170:
		return 7;
	default:
		return 0;
	}
}

int byte_val(const unsigned char *p, const int *q, int a, int b)
{
	int x = *p;
	int y = *q;
	int s = a + b;
	unsigned int u = a;

	if (x > 255 || x < 0)
		byte_bad();
	if ((x & 0x7f) - 200 > 0)
		diff_bad();
	if (y > 255)
		int_kept();
	if (s < 0)
		sum_kept();
	if (u > 5)
		span_kept();
	return x + y + s;
}

int tiers(const int *w)
{
	int t;
	int sum = 0;

	for (t = 1; t < 4U; t++) {
		if (!is_nonneg(t))
			tier_bad();
		sum += w[t];
	}
	for (t = 0; t < sum; t++)
		if (t > 100)
			loop_kept();
	return sum;
}
EOF2

# the same code, run: each call of what must fold or stay counted
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
#include <string.h>
static int bads, kepts;
void min_bad(void) { bads++; }
void order_bad(void) { bads++; }
void flags_bad(void) { bads++; }
void shift_bad(void) { bads++; }
void case_bad(void) { bads++; }
void byte_bad(void) { bads++; }
void tier_bad(void) { bads++; }
void diff_bad(void) { bads++; }
void after_kept(void) { kepts++; }
void sum_kept(void) { kepts++; }
void span_kept(void) { kepts++; }
void int_kept(void) { kepts++; }
void loop_kept(void) { kepts++; }
static char area[3 * 4096];
char *pos = area + 4000;
int sel = 1;
int has_feature(void) { return sel; }
int copy(char *d, const char *s, unsigned int n) { memset(d, s[0], n); return 0; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	static const char text[5000] = "abc";
	unsigned char b = 200;
	int i = 300;
	int w[4] = { 1, 2, 3, 4 };
	char regs[0x400];
	int r;

	memset(regs, 1, sizeof(regs));
	r = copy_string(text, 5000);
	printf("%d %ld %d %d\n", r, (long)(pos - area), order(), pfn_bits());
	sel = 0;
	printf("%d %d %d\n", order(), pfn_bits(), remove_page(area, 1, 1) + remove_page(area, 3, 0));
	printf("%u %u %u %u\n", read_reg(regs, 0x20), read_reg(regs, 0x300), read_reg(regs, 0x120),
	       read_reg(regs, 0x200));
	printf("%d %d %d %d\n", byte_val(&b, &i, 7, 9), byte_val(&b, &i, -7, -9), tiers(w), (int)((unsigned long)batch[0] & 3));
	printf("bads %d kepts %d\n", bads, kepts);
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
	for o in O1 O2; do
		gcc -m$m -$o -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c"
		want=$(undefined "$CC_TMP/g.o")
		gcc -m$m -$o -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
		want_run=$("$CC_TMP/g")
		for path in mir legacy; do
			run="env CC_MIR_STATS=2"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			what="-m$m -$o $path"
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
				echo "run_fold_value_ranges: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_fold_value_ranges: $what: a function fell back from MIR"
				fail=1
			fi
			got=$(undefined "$CC_TMP/c.o")
			if [ "$got" != "$want" ]; then
				echo "run_fold_value_ranges: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
				fail=1
			fi
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
				echo "run_fold_value_ranges: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_fold_value_ranges: $what: a function of the program fell back from MIR"
				fail=1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
			got_run=$("$CC_TMP/c")
			if [ "$got_run" != "$want_run" ]; then
				echo "run_fold_value_ranges: $what prints"
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
echo "run_fold_value_ranges: ok"
