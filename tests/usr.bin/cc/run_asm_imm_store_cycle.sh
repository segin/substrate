#!/bin/sh
# Whether an asm's "Ir" input is a known constant is searched through
# loads and the stores before them, the stores told apart by their
# addresses, whose offsets are themselves searched for constants; a
# load is the store's value only when that store is the sole one, which
# is checked against every store in the function.  So a pointer loaded
# from memory with a store through it at a variable offset made the
# search of its own load nest without end, and cc died of a stack
# overflow: 16 of Linux's files, mm/vmscan.c's lru_gen_seq_write among
# them.  The searches now nest only so deep.  The program compiles and
# does what gcc's does, -m64 and -m32, -O0 and -O2, through both code
# paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_imm_store_cycle: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/f.c" <<'EOF2'
void f(char **q, long n, unsigned long *w)
{
	long i = 3;
	char *p = *q;

	p[n] = 'x';
	asm volatile("btsl %1, %0" : "+m" (*w) : "Ir" (i & 7));
}
EOF2
cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>

void f(char **q, long n, unsigned long *w);

int main(void)
{
	char b[4] = "abc";
	char *p = b;
	unsigned long w = 1;

	f(&p, 1, &w);
	printf("%s %lu\n", b, w);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/f.c" "$CC_TMP/m.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O2; do
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/f.o" "$CC_TMP/f.c"; then
				echo "run_asm_imm_store_cycle: -m$m $path -$o: cc failed"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/f.o" "$CC_TMP/m.c"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_asm_imm_store_cycle: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_asm_imm_store_cycle: ok"
