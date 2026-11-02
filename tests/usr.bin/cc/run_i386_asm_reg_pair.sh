#!/bin/sh
# A 64-bit asm operand in a general register on i386 is held in a pair of
# them, as gcc holds it: the register %0 names and the next in gcc's order
# (%eax:%edx, %edx:%ecx, %ecx:%ebx, %ebx:%esi, %esi:%edi).  cc gave it one
# register, only "A" a pair, so `asm("" : "=r"(y) : "0"(x))` of a long
# long returned garbage in its high word at every -O level on both code
# paths.  Outputs tied to inputs, "+r", two pairs in one asm, "q", a
# clobbered %eax, a fixed register beside a pair, an early clobber, "rm",
# "A" and a double: what a program prints is gcc's, -O0, -O1 and -O2, both
# code paths, no MIR fallback; and the register %0 names is the one gcc
# names where gcc's choice does not depend on its allocation.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_i386_asm_reg_pair: skip (no gcc)"
	exit 0
fi
printf 'int main(void){return 0;}\n' >"$CC_TMP/m32.c"
if ! gcc -m32 -o "$CC_TMP/m32" "$CC_TMP/m32.c" 2>/dev/null; then
	echo "run_i386_asm_reg_pair: skip (no gcc -m32)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF'
#include <stdio.h>
__attribute__((noinline)) long long same(long long x) { long long y; asm("" : "=r"(y) : "0"(x)); return y; }
__attribute__((noinline)) long long plus(long long x) { asm("" : "+r"(x)); return x + 1; }
__attribute__((noinline)) long long two(long long x, long long z) { long long y; asm("" : "=r"(y) : "0"(x), "r"(z)); return y + z; }
__attribute__((noinline)) unsigned long long sw(unsigned long long x) { asm("xchgl %%eax, %%edx" : "=A"(x) : "0"(x)); return x; }
__attribute__((noinline)) long long q(long long x) { long long y; asm("" : "=q"(y) : "0"(x)); return y; }
__attribute__((noinline)) long long clob(long long x) { long long y; asm("movl $5, %%eax" : "=r"(y) : "0"(x) : "eax"); return y; }
__attribute__((noinline)) long long fixa(long long x) { long long y; int k = 3; asm("" : "=r"(y) : "a"(k), "0"(x)); return y + k; }
__attribute__((noinline)) long long ec(long long x) { long long y; asm("" : "=&r"(y) : "r"(x)); return (y & 0) + x; }
__attribute__((noinline)) long long rm(long long x) { long long y; asm("" : "=rm"(y) : "0"(x)); return y; }
__attribute__((noinline)) double dbl(double x) { double y; asm("" : "=r"(y) : "0"(x)); return y; }
int main(void)
{
	printf("%llx %llx %llx %llx %llx %llx %llx %llx %llx %g\n", same(0x1122334455667788LL), plus(0x1ffffffffLL),
	       two(0x100000002LL, 0x300000004LL), sw(0x1122334455667788ULL), q(0x8877665544332211LL),
	       clob(0x7766554433221100LL), fixa(0x500000001LL), ec(0xa0000000bLL), rm(0xc0000000dLL), dbl(2.5));
	return 0;
}
EOF
gcc -m32 -O2 -o "$CC_TMP/g" "$CC_TMP/v.c"
want=$("$CC_TMP/g")
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	for o in O0 O1 O2; do
		if ! $run CC_MIR_STATS=1 "$CC_BIN" -m32 -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
			echo "run_i386_asm_reg_pair: $path -$o refused: $(grep -v '^mir:' "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep -q 'not taken' "$CC_TMP/c.err"; then
			echo "run_i386_asm_reg_pair: $path -$o: $(grep 'not taken' "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		gcc -m32 -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_i386_asm_reg_pair: $path -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done

# the register %0 names, where gcc's choice is the first free pair
cat >"$CC_TMP/n.c" <<'EOF'
long long a(long long x) { long long y; asm("# A %0 %1" : "=r"(y) : "0"(x)); return y; }
long long c(long long x) { asm("# C %0" : "+r"(x)); return x; }
long long d(long long x) { long long y; asm("# D %0" : "=r"(y) : "0"(x) : "eax"); return y; }
long long e(long long x) { long long y; int k = 3; asm("# E %0 %1" : "=r"(y) : "a"(k), "0"(x)); return y; }
long long f(long long x) { long long y; asm("# F %0" : "=q"(y) : "0"(x)); return y; }
void g(long long *p, long long x) { asm("# G %0" : : "r"(x)); *p = x; }
long long h(long long x) { long long y; asm("# H %0" : "=&r"(y) : "r"(x)); return y; }
EOF
gcc -m32 -O2 -S -o - "$CC_TMP/n.c" | grep -E '^[[:space:]]*# [A-H] ' | sed 's/^[[:space:]]*//' >"$CC_TMP/g.names"
"$CC_BIN" -m32 -O2 -S -o "$CC_TMP/n.s" "$CC_TMP/n.c"
grep -E '^[[:space:]]*# [A-H] ' "$CC_TMP/n.s" | sed 's/^[[:space:]]*//' >"$CC_TMP/c.names"
if ! cmp -s "$CC_TMP/g.names" "$CC_TMP/c.names"; then
	echo "run_i386_asm_reg_pair: names [$(tr '\n' ';' <"$CC_TMP/c.names")], gcc's [$(tr '\n' ';' <"$CC_TMP/g.names")]"
	exit 1
fi
echo "run_i386_asm_reg_pair: ok"
