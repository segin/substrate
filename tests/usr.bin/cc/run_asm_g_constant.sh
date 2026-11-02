#!/bin/sh
# An asm "g" operand that is a constant is an immediate, whatever its
# range, as gcc has it: "g" is "rmi".  cc put it in a register, so Linux's
# acrn_hypercall0(), `movl %1, %%r8d` of "g"(0x80000080UL), became
# `movl %r10, %r8d` and did not assemble (drivers/virt/acrn).  Constants
# small, past 2^31 and past 2^32, and an address, through "g" into
# 32-bit moves: the program prints what gcc's prints, -m64 and -m32, -O0
# and -O2, on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_g_constant: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF'
#include <stdio.h>
#define low32(id) ({ unsigned int r_; asm volatile("movl %1, %0" : "=r"(r_) : "g"((unsigned long)(id)) : "memory"); r_; })
static int obj;
int main(void)
{
	unsigned int a = low32(5);
	unsigned int b = low32(0x80000080UL);
	unsigned int c = low32(0x123456789UL);
	unsigned int d;

	asm volatile("movl %1, %0" : "=r"(d) : "g"(-3));
	printf("%x %x %x %x %d\n", a, b, c, d, (int)sizeof(obj));
	return 0;
}
EOF
for m in 64 32; do
	gcc -m$m -O2 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O2; do
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
				echo "run_asm_g_constant: -m$m $path -$o refused: $(grep -v Messages "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_asm_g_constant: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_asm_g_constant: ok"
