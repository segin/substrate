#!/bin/sh
# gcc's `%z` asm operand modifier prints the operand's size suffix, b, w,
# l or q.  cc printed the operand itself, so Linux's net/rxrpc/input.c
# `shr%z1 %1` of a byte in memory became `shr0(%r10) 0(%r10)` and did not
# assemble.  Memory and register operands of each size, by number and by
# name: the program prints what gcc's prints, -m64 and -m32, -O0 and -O2,
# on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_z_modifier: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>
static unsigned char b = 0x81;
static unsigned short w = 0x8001;
static unsigned int l = 0x80000001u;
int main(void)
{
	unsigned long q = 0x10;
	unsigned int r = 6;
	unsigned char *p = &b;
	asm("shr%z0 %0" : "+m"(b));
	asm("shr%z0 %0" : "+m"(w));
	asm("shr%z0 %0" : "+m"(l));
	asm("shr%z0 %0" : "+rm"(q));
	asm("shl%z0 %0" : "+r"(r));
	asm("inc%z[x] %[x]" : [x] "+m"(*p));
	printf("%x %x %x %lx %x\n", b, w, l, q, r);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O2; do
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
				echo "run_asm_z_modifier: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_asm_z_modifier: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_asm_z_modifier: ok"
