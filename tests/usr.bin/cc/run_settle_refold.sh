#!/bin/sh
# The folding that settles a function repeats while the folding itself
# changes anything, not only while it drops code: a value it makes a
# constant is known past a label only on the next round.  Linux's min()
# asks `(2 + __is_nonneg(x)) & (2 + __is_nonneg(y))` in a BUILD_BUG_ON;
# once __builtin_constant_p was decided 0, the 2 it made in one block was
# tested in the next, nothing else changed, and the folding stopped there:
# kernel/pid.c's pid_idr_init() kept its call of an undefined
# __compiletime_assert_N and vmlinux did not link.  The functions left
# undefined are gcc's, -O1 and -O2, -m64 and -m32, both code paths, every
# function through MIR; and a program computing the same prints what gcc's
# prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_settle_refold: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must go: each test calls a function nothing defines
cat >"$CC_TMP/u.c" <<'EOF2'
#define nonneg(x) (__builtin_constant_p((long long)(x) >= 0) && ((long long)(x) >= 0))
#define sign_use(x) (((typeof(x))(-1)) < (typeof(x))1 ? (2 + nonneg(x)) : (1 + 2 * (sizeof(x) < 4)))
#define max(a, b) ({ int ma = (a); int mb = (b); ma > mb ? ma : mb; })
#define min(a, b) ({ __auto_type ua = (a); __auto_type ub = (b); \
	do { if (!(sign_use(ua) & sign_use(ub))) min_bad(); } while (0); ua < ub ? ua : ub; })
extern void min_bad(void), size_bad(void);
extern int lim, cur;
extern unsigned int ncpu;
void init(void)
{
	do { if (sizeof(long) > 8) size_bad(); } while (0);
	cur = min(lim, max(cur, 1024 * ncpu));
}
EOF2

# what it computes
cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>
#define nonneg(x) (__builtin_constant_p((long long)(x) >= 0) && ((long long)(x) >= 0))
#define sign_use(x) (((typeof(x))(-1)) < (typeof(x))1 ? (2 + nonneg(x)) : (1 + 2 * (sizeof(x) < 4)))
#define max(a, b) ({ int ma = (a); int mb = (b); ma > mb ? ma : mb; })
#define min(a, b) ({ __auto_type ua = (a); __auto_type ub = (b); \
	do { if (!(sign_use(ua) & sign_use(ub))) bad++; } while (0); ua < ub ? ua : ub; })
int lim = 4194304, cur = 32768, bad;
unsigned int ncpu = 8;
__attribute__((noinline)) int init(void)
{
	do { if (sizeof(long) > 8) bad += 10; } while (0);
	cur = min(lim, max(cur, 1024 * ncpu));
	return cur;
}
int main(void)
{
	int a = init();

	ncpu = 9000;
	printf("%d %d %d\n", a, init(), bad);
	return 0;
}
EOF2

undefined() {
	nm -u "$1" | awk '{ print $NF }' | grep -E '_bad$' | sort || true
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
				echo "run_settle_refold: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_settle_refold: $what: a function fell back from MIR"
				fail=1
			fi
			got=$(undefined "$CC_TMP/c.o")
			if [ "$got" != "$want" ]; then
				echo "run_settle_refold: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
				fail=1
			fi
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
				echo "run_settle_refold: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_settle_refold: $what: a function of the program fell back from MIR"
				fail=1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
			got_run=$("$CC_TMP/c")
			if [ "$got_run" != "$want_run" ]; then
				echo "run_settle_refold: $what prints [$got_run], gcc's [$want_run]"
				fail=1
			fi
		done
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_settle_refold: ok"
