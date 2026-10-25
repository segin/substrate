#!/bin/sh
# A switch of enough cases, dense enough, is a jump through a table in
# .rodata, as gcc makes it: the value less the smallest case, checked
# against the table's size, and `jmp *table(,%reg,4)` -- 8 under
# x86-64's -mcmodel=kernel; in other x86-64 code, which may be linked
# position-independent, the table's entries are offsets from it, added
# to its address made off %rip.  Linux's ia32_sys_call(), 471 system
# calls, was 471 compares and branches, 8265 bytes against gcc's 2382.
# A sparse switch stays a chain of compares, and i386 -fPIC code gets no
# table.  The program is linked PIE as well.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_switch_table: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/s.c" <<'EOF2'
#include <stdio.h>

long a(long x) { return x + 100; }
long b(long x) { return x + 200; }
long c(long x) { return x + 300; }
long d(long x) { return x + 500; }

long __attribute__((noinline)) dispatch(int nr, long x)
{
	switch (nr) {
	case 1: return a(x);
	case 2: return b(x);
	case 3: return c(x);
	case 5: return d(x);
	case 6: return a(x + 6);
	case 7: return b(x + 7);
	default: return -38;
	}
}

/* negative cases, two values to one case, a fallthrough */
int __attribute__((noinline)) neg(int v)
{
	int r = 0;

	switch (v) {
	case -3: r += 1;
	case -2: r += 2; break;
	case -1:
	case 1: r = 7; break;
	case 0: r = 9; break;
	case 2: r = 11; break;
	}
	return r;
}

/* too sparse for a table */
int __attribute__((noinline)) sparse(unsigned v)
{
	switch (v) {
	case 1: return 1;
	case 100: return 2;
	case 1000: return 3;
	case 10000: return 4;
	case 100000: return 5;
	}
	return 0;
}

/* in a loop, the cases jumping back */
long __attribute__((noinline)) run(const unsigned char *ops, int n)
{
	long acc = 0;
	int i;

	for (i = 0; i < n; ++i) {
		switch (ops[i]) {
		case 0: acc += 1; break;
		case 1: acc *= 3; break;
		case 2: acc -= 5; break;
		case 3: acc ^= 0x55; break;
		case 4: acc = -acc; break;
		default: acc += 1000; break;
		}
	}
	return acc;
}

/* a long's cases (a word on x86-64) */
long __attribute__((noinline)) wide(long v)
{
	switch (v) {
	case 10: return 1;
	case 11: return 4;
	case 12: return 9;
	case 13: return 16;
	case 15: return 25;
	}
	return -1;
}

int main(void)
{
	static const unsigned char ops[] = {0, 1, 1, 2, 3, 9, 4, 0, 1, 7, 2};
	int i;

	for (i = -1; i < 10; i++)
		printf("%ld ", dispatch(i, 1));
	for (i = -5; i < 4; i++)
		printf("%d ", neg(i));
	printf("%d %d %d ", sparse(100), sparse(5), sparse(100000));
	printf("%ld ", run(ops, (int)sizeof(ops)));
	for (i = 8; i < 17; i++)
		printf("%ld ", wide(i));
	printf("\n");
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/s.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		for pic in "" -fPIC; do
			# (compiled -fPIC, linked without: the driver hands -fPIC to the linker)
			"$CC_BIN" -m$m -$o $pic -c -o "$CC_TMP/s.o" "$CC_TMP/s.c"
			"$CC_BIN" -m$m -o "$CC_TMP/c" "$CC_TMP/s.o"
			got=$("$CC_TMP/c") || { echo "run_switch_table: -m$m -$o $pic: the program fails"; exit 1; }
			if [ "$got" != "$want" ]; then
				echo "run_switch_table: -m$m -$o $pic prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/s.s" "$CC_TMP/s.c"
	for fn in dispatch neg run wide; do
		awk -v f="$fn" '$0 == f ":" {x=1} $0 ~ "^\\.size " f "," {x=0} x' "$CC_TMP/s.s" >"$CC_TMP/fn.s"
		if ! grep -qE '^	jmp \*(\.L[a-z_]+_jt[0-9]+\(,%[a-z0-9]+,4\)|%r[a-z0-9]+)$' "$CC_TMP/fn.s" ||
			! grep -qE '_jt[0-9]+' "$CC_TMP/fn.s"; then
			if [ "$fn" = wide ] && [ "$m" = 32 ]; then
				continue # (a long long would be a pair; a long is a word, and is tabled too)
			fi
			echo "run_switch_table: -m$m: $fn() has no jump table"
			exit 1
		fi
		if [ "$(grep -cE '^	cmp' "$CC_TMP/fn.s")" -gt 2 ]; then
			echo "run_switch_table: -m$m: $fn() compares case by case"
			exit 1
		fi
	done
	if awk '/^sparse:/{x=1} /^\.size sparse,/{x=0} x' "$CC_TMP/s.s" | grep -q 'jmp \*'; then
		echo "run_switch_table: -m$m: sparse() has a table"
		exit 1
	fi
	"$CC_BIN" -m$m -O2 -fPIC -S -o "$CC_TMP/p.s" "$CC_TMP/s.c"
	if [ "$m" = 32 ] && grep -q '_jt[0-9]' "$CC_TMP/p.s"; then
		echo "run_switch_table: -m32: -fPIC code has an absolute jump table"
		exit 1
	fi
	if [ "$m" = 64 ]; then
		"$CC_BIN" -m64 -O2 -mcmodel=kernel -S -o "$CC_TMP/k.s" "$CC_TMP/s.c"
		if ! grep -qE '^	jmp \*\.Ldispatch_jt[0-9]+\(,%r[a-z0-9]+,8\)$' "$CC_TMP/k.s"; then
			echo "run_switch_table: -mcmodel=kernel: dispatch() does not jump through an absolute table"
			exit 1
		fi
		# position-independent: linked PIE by gcc, as cc's own build is
		"$CC_BIN" -m64 -O2 -c -o "$CC_TMP/s.o" "$CC_TMP/s.c"
		gcc -m64 -pie -o "$CC_TMP/c" "$CC_TMP/s.o"
		got=$("$CC_TMP/c") || { echo "run_switch_table: PIE: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_switch_table: PIE prints [$got], gcc's [$want]"
			exit 1
		fi
	fi
done
echo "run_switch_table: ok"
