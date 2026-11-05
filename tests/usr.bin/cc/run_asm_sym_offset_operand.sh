#!/bin/sh
# An inline asm's memory operand of a member of a global, or of an
# element of one at a constant index -- "m" (pv_ops.lock), "=m" (arr[3])
# -- is written as the symbol and the offset, off %rip on x86-64, as gcc
# writes it: `call *pv_ops+224(%rip)`.  Its address was made in a register
# (lea pv_ops(%rip), %r10; add $224, %r10) for the asm to read through:
# `call *0(%r10)`.  Linux finds each paravirt call by its `call
# *pv_ops+N(%rip)` (ff 15) to make it a direct call, and stopped on a
# BUG() in apply_alternatives() at the first that was not one.
# Results are gcc's; under -fPIC the symbol is still read through the GOT.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_sym_offset_operand: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/s.c" <<'EOF2'
#include <stdio.h>

#ifdef __x86_64__
#define MOVL "movq %1, %0"
#define CALLM "call *%[op]"
#else
#define MOVL "movl %1, %0"
#define CALLM "call *%[op]"
#endif

static int hits;
static void bump(void) { hits += 3; }

struct ops { long pad[28]; void (*lock)(void); long tail; } pv_ops = { .lock = bump, .tail = 7 };
long arr[10] = { 1, 2, 3, 4 };

static inline long read_tail(void)
{
	long v;

	asm(MOVL : "=r"(v) : "m"(pv_ops.tail));
	return v;
}

static inline void set3(long x) { asm volatile(MOVL : "=m"(arr[3]) : "r"(x)); }

static void __attribute__((noinline)) do_lock(void)
{
	asm volatile(CALLM :: [op] "m"(pv_ops.lock)
		     : "memory", "cc", "ax", "cx", "dx"
#ifdef __x86_64__
		       , "si", "di", "r8", "r9", "r10", "r11"
#endif
		     );
}

int main(void)
{
	set3(40);
	do_lock();
	do_lock();
	printf("%ld %ld %d\n", read_tail(), arr[3], hits);
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
	for o in O0 O2; do
		for pic in "" -fPIC; do
			if ! "$CC_BIN" -m$m -$o $pic -c -o "$CC_TMP/c.o" "$CC_TMP/s.c" 2>"$CC_TMP/err" ||
				! "$CC_BIN" -m$m -o "$CC_TMP/c" "$CC_TMP/c.o" 2>>"$CC_TMP/err"; then
				echo "run_asm_sym_offset_operand: -m$m -$o $pic: cc rejected it"
				head -3 "$CC_TMP/err"
				exit 1
			fi
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_asm_sym_offset_operand: -m$m -$o $pic prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/s.s" "$CC_TMP/s.c"
	if [ $m = 64 ]; then
		lock='call \*pv_ops\+224\(%rip\)'
		tail='pv_ops\+232\(%rip\)'
		a3='arr\+24\(%rip\)'
	else
		lock='call \*pv_ops\+112$'
		tail='pv_ops\+116,'
		a3='arr\+12$'
	fi
	for want_re in "$lock" "$tail" "$a3"; do
		if ! grep -qE "$want_re" "$CC_TMP/s.s"; then
			echo "run_asm_sym_offset_operand: -m$m: no operand written as /$want_re/:"
			grep -nE 'pv_ops|arr' "$CC_TMP/s.s" | head -8
			exit 1
		fi
	done
done
echo "run_asm_sym_offset_operand: ok"
