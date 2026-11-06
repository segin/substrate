#!/bin/sh
# What a branch teaches of a value holds past a label nothing reaches:
# the code after such a label does not run, so it cannot change what is
# known.  Folding leaves those labels behind -- an arm that only breaks
# out of a loop, `if (!page) break;`, has its branch threaded straight to
# the loop's exit and its label orphaned -- and range folding forgot
# `while (datalen > 0)` at it.  Linux's pktgen_finalize_skb() then asks
# min(datalen, PAGE_SIZE) inside that loop, whose BUILD_BUG_ON tests
# __builtin_constant_p((long long)datalen >= 0), true for gcc there: a
# call of an undefined __compiletime_assert_N stayed, and pktgen could
# not be loaded.  What gcc folds leaves no reference; a value the loop
# does not know stays, as gcc keeps it.  -O2, -m64 and -m32, both code
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
	echo "run_range_dead_label: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
extern void len_bad(void), len_kept(void);
extern void fill(int i, int len);
extern void *page;
extern void *alloc(int len);

#define NONNEG(x) (__builtin_constant_p((long long)(x) >= 0) && ((long long)(x) >= 0))

int finalize(int datalen, int frags)
{
	int i = 0;

	while (datalen > 0) {
		if (!page) {
			page = alloc(datalen);
			if (!page)
				break;
		}
		if (i == frags - 1) {
			if (!NONNEG(datalen))
				len_bad();
			fill(i, datalen < 4096 ? datalen : 4096);
		} else {
			fill(i, 7);
		}
		datalen -= 4096;
		i++;
	}
	return i;
}

/* after the loop the value is not known not to be negative */
int rest(int datalen)
{
	while (datalen > 0) {
		if (!page) {
			page = alloc(datalen);
			if (!page)
				break;
		}
		datalen -= 4096;
	}
	if (!NONNEG(datalen))
		len_kept();
	return datalen;
}
EOF2

# the same code, run
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static int bads, kepts, filled;
void len_bad(void) { bads++; }
void len_kept(void) { kepts++; }
void fill(int i, int len) { filled += i * 10000 + len; }
void *page;
static char one;
void *alloc(int len) { return len > 9000 ? &one : 0; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	int a = finalize(10000, 3);
	int b = finalize(5000, 2);
	int c = rest(10000);

	printf("%d %d %d filled %d bads %d kepts %d\n", a, b, c, filled, bads, kepts);
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
			echo "run_range_dead_label: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_range_dead_label: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$(echo $got)" != "$(echo $want)" ] || [ "$(echo $want)" != "len_kept" ]; then
			echo "run_range_dead_label: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_range_dead_label: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_range_dead_label: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_range_dead_label: $what prints [$got_run], gcc's [$want_run]"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_range_dead_label: ok"
