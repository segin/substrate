#!/bin/sh
# A loop that computes only from constants and const tables -- entered
# once, with known values, no store, call or asm in it -- is what it
# leaves, as gcc unrolls and folds it.  Linux's net/core/skbuff.c has
# BUILD_BUG_ON(skb_ext_total_length() > 255), the always_inline sum of a
# const table of sizes over its elements, which left a call of an
# undefined __compiletime_assert_N.  A table sum, nested loops, a product,
# a loop leaving by one of two ways and one i386 computes in pairs of words
# fold, and leave no reference -- gcc's -O2 folds the table sum, which the
# old code did not -- while a loop reading a writable table, or bounded by
# a parameter, stays as gcc keeps it.  -O2, -m64 and -m32, both code
# paths, every function through MIR; and the program, run, prints what
# gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_eval_const_loops: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
extern void sum_bad(void), len_bad(void), nest_bad(void), mul_bad(void), exit_bad(void), wide_bad(void);
extern void rw_kept(void), n_kept(void);
static const unsigned char lens[] = { 3, 5, 2, 7, 1, 4, 6 };
static const short w[4] = { 1, -2, 3, -4 };
int rw[4] = { 1, 2, 3, 4 };

static inline __attribute__((always_inline)) unsigned int total(void)
{
	unsigned int l = 2;
	int i;

	for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++)
		l += lens[i];
	return l;
}
void init(void)
{
	if (total() > 255)
		sum_bad();
	if (total() != 30)
		len_bad();
}
int nested(void)
{
	int s = 0;
	int i;
	int j;

	for (i = 0; i < 4; i++)
		for (j = 0; j <= i; j++)
			s += w[j] * i;
	if (s != -3)
		nest_bad();
	return s;
}
unsigned int fact(void)
{
	unsigned int f = 1;
	int i;

	for (i = 1; i <= 10; i++)
		f *= i;
	if (f != 3628800)
		mul_bad();
	return f;
}
int first_big(void)
{
	int i;

	for (i = 0; i < 7; i++)
		if (lens[i] > 6)
			break;
	if (i != 3)
		exit_bad();
	return i;
}
unsigned long long wide(void)
{
	unsigned long long x = 0x123456789ULL;
	int i;

	for (i = 0; i < 7; i++)
		x = (x << 5) ^ (x >> 3) ^ lens[i];
	if (x == 0)
		wide_bad();
	return x;
}
int sum_rw(void)
{
	int s = 0;
	int i;

	for (i = 0; i < 4; i++)
		s += rw[i];
	if (s > 10)
		rw_kept();
	return s;
}
int sum_n(int n)
{
	int s = 0;
	int i;

	for (i = 0; i < n; i++)
		s += lens[i % 7];
	if (s > 100)
		n_kept();
	return s;
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads, kepts;
void sum_bad(void) { bads++; }
void len_bad(void) { bads++; }
void nest_bad(void) { bads++; }
void mul_bad(void) { bads++; }
void exit_bad(void) { bads++; }
void wide_bad(void) { bads++; }
void rw_kept(void) { kepts++; }
void n_kept(void) { kepts++; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	int a;
	int b;
	unsigned int c;
	unsigned long long d;
	int e;
	int g;

	init();
	a = nested();
	b = first_big();
	c = fact();
	d = wide();
	e = sum_rw();
	rw[2] = 50;
	e += sum_rw();
	g = sum_n(30);
	printf("%d %d %u %llu %d %d bads %d kepts %d\n", a, b, c, d, e, g, bads, kepts);
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
				echo "run_eval_const_loops: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_eval_const_loops: $what: a function fell back from MIR"
				fail=1
			fi
			# every loop here runs to a constant: none of what must fold is left, gcc's or not (gcc's
			# -O2 unrolls the table sum, not all the others); what must stay is what gcc keeps
			got=$(undefined "$CC_TMP/c.o")
			if [ "$(echo $got)" != "n_kept rw_kept" ] ||
			    [ "$(echo "$want" | grep '_kept$' | tr '\n' ' ')" != "n_kept rw_kept " ]; then
				echo "run_eval_const_loops: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
				fail=1
			fi
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
				echo "run_eval_const_loops: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_eval_const_loops: $what: a function of the program fell back from MIR"
				fail=1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
			got_run=$("$CC_TMP/c")
			if [ "$got_run" != "$want_run" ]; then
				echo "run_eval_const_loops: $what prints [$got_run], gcc's [$want_run]"
				fail=1
			fi
		done
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_eval_const_loops: ok"
