#!/bin/sh
# An asm output in a register whose lvalue is a global or a static
# object is stored to it after the asm, as through a pointer:
# `asm("movl $5, %0" : "=r"(seen))` with `seen` a global left it as it
# was -- the operand was its value, loaded, and the asm's result went
# nowhere -- at every level.  A "+r" one is loaded first.  A global
# register variable is the register itself, nothing stored.  Results
# are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_global_out: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/o.c" <<'EOF2'
#include <stdio.h>

long seen;
long acc = 40;
short narrow = -1;
static long hidden;

void __attribute__((noinline)) f(void) { asm volatile("mov $5, %0" : "=r"(seen)); }

long __attribute__((noinline)) g(void)
{
	static long count = 10;
	long o = 0;

	asm volatile("mov $7, %0\n\tmov $9, %1" : "=r"(hidden), "+r"(o));
	asm volatile("add $2, %0" : "+r"(acc));
	asm volatile("add $3, %0" : "+r"(count));
	asm volatile("movw $300, %0" : "=r"(narrow));
	return o + count;
}

int main(void)
{
	long a, b;

	f();
	a = g();
	b = g();
	printf("%ld %ld %ld %ld %ld %d\n", seen, hidden, acc, a, b, narrow);
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/o.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/o.c"
		got=$("$CC_TMP/c") || { echo "run_asm_global_out: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_asm_global_out: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_asm_global_out: ok"
