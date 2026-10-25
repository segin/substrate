#!/bin/sh
# An inline asm's memory operand of a global -- "m" (var), "+m" (var),
# Linux's per-cpu and atomic accessors -- is written as the symbol, off
# %rip on x86-64, as gcc writes it: its address was made in a register
# (lea var(%rip), %r10) for the asm to read through.  Under -fPIC, where
# the symbol may be another object's, it is still read through the GOT.
# Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_sym_operand: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/s.c" <<'EOF2'
#include <stdio.h>

#ifdef __x86_64__
#define INC "incq %0"
#define MOV "movq %1, %0"
#else
#define INC "incl %0"
#define MOV "movl %1, %0"
#endif

unsigned long counter = 5;
struct { long a, b; } pair = {10, 20};

static inline void inc(void) { asm volatile(INC : "+m"(counter)); }

static inline long readb(void)
{
	long v;

	asm(MOV : "=r"(v) : "m"(pair.b));
	return v;
}

int main(void)
{
	long b;

	inc();
	inc();
	b = readb();
	printf("%lu %ld\n", counter, b);
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
			if ! "$CC_BIN" -m$m -$o $pic -o "$CC_TMP/c" "$CC_TMP/s.c" 2>"$CC_TMP/err"; then
				echo "run_asm_sym_operand: -m$m -$o $pic: cc rejected it"
				grep -v 'unsupported option' "$CC_TMP/err" | head -3
				exit 1
			fi
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_asm_sym_operand: -m$m -$o $pic prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/s.s" "$CC_TMP/s.c"
	if ! grep -qE 'inc[lq] counter' "$CC_TMP/s.s" || grep -qE 'lea[lq] counter' "$CC_TMP/s.s"; then
		echo "run_asm_sym_operand: -m$m: the asm's operand is not written as counter"
		grep -E 'counter' "$CC_TMP/s.s" | head -5
		exit 1
	fi
done
echo "run_asm_sym_operand: ok"
