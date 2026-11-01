#!/bin/sh
# An asm operand that may be a constant ("Ir") is one only where its
# value is: a load reads the store before it only if that is the one
# store there is.  cc took the store before the load in the code for
# its value, and at -O0 a loop's `bit = i` in memory was the 0 of its
# first trip on every trip -- `bt $0`.  Results are gcc's, -m64 and -m32,
# -O0 and -O2.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_imm_loop_store: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/l.c" <<'EOF2'
#include <stdio.h>

#ifdef __x86_64__
#define BT "btq"
#else
#define BT "btl"
#endif

int main(void)
{
	unsigned long v = 0xa5;
	unsigned n = 0;
	unsigned m = 0;
	long i;
	long fixed = 2;

	for (i = 0; i < 8; i++) {
		unsigned char c;
		long bit = i;

		__asm__(BT " %2, %1" : "=@ccc"(c) : "r"(v), "Ir"(bit));
		n = n * 2 + c;
		/* stored once: still the constant it is */
		__asm__(BT " %2, %1" : "=@ccc"(c) : "r"(v), "Ir"(fixed));
		m += c;
	}
	printf("%u %u\n", n, m);
	return 0;
}
EOF2
for mm in 64 32; do
	gcc -m$mm -O0 -o "$CC_TMP/g" "$CC_TMP/l.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		if ! "$CC_BIN" -m$mm -$o -c -o "$CC_TMP/l.o" "$CC_TMP/l.c" 2>"$CC_TMP/c.err"; then
			echo "run_asm_imm_loop_store: -m$mm -$o: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
		gcc -m$mm -no-pie -o "$CC_TMP/c" "$CC_TMP/l.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_asm_imm_loop_store: -m$mm -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_asm_imm_loop_store: ok"
