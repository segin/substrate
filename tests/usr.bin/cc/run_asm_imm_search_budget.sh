#!/bin/sh
# Looking for the constant an asm's "I" operand may be stops in time.
# The search follows a load to the one store before it, telling stores
# apart by their addresses, whose offsets it searches for the same way;
# each nested search scans the function and may nest again for each store,
# so the work multiplied at every level, and an asm after many stores
# through loaded indexes -- Linux's ntfs_cluster_alloc() in
# fs/ntfs/lcnalloc.c, and mlx5's cmd.c, atp870u.c -- was searched for
# hours.  Twelve levels of such stores and a bit test: compiled within a
# minute, -O0, -O1 and -O2, on both code paths, and the program prints
# what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_asm_imm_search_budget: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

{
	echo '#include <stdio.h>'
	echo 'unsigned long sink;'
	echo 'unsigned char bit;'
	echo '__attribute__((noinline)) void f(unsigned long *a, unsigned long *b, unsigned long **pp)'
	echo '{'
	echo '	unsigned long *p = *pp;'
	k=0
	while [ $k -lt 12 ]; do
		echo "	a[b[$k] & 15] = p[a[$k] & 7];"
		echo "	b[a[$((k + 1))] & 15] = a[p[$k] & 3] + $k;"
		echo "	p[b[$((k + 2))] & 7] = b[$k];"
		k=$((k + 1))
	done
	echo '	asm volatile("btq %1, %2\n\tsetc %0" : "=q"(bit) : "Ir"(a[b[0] & 15] & 63), "r"(p[1]));'
	echo '	sink = p[0];'
	echo '}'
	echo 'int main(void)'
	echo '{'
	echo '	unsigned long a[32], b[32], c[32], *pc = c;'
	echo '	int i;'
	echo '	for (i = 0; i < 32; i++) { a[i] = i * 3 + 1; b[i] = i * 5 + 2; c[i] = i * 7 + 3; }'
	echo '	f(a, b, &pc);'
	echo '	printf("%lu %d %lu %lu\n", sink, bit, a[5], b[9]);'
	echo '	return 0;'
	echo '}'
} >"$CC_TMP/v.c"
gcc -O0 -o "$CC_TMP/g" "$CC_TMP/v.c"
want=$("$CC_TMP/g")
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	for o in O0 O1 O2; do
		if ! timeout 60 $run "$CC_BIN" -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
			echo "run_asm_imm_search_budget: $path -$o did not compile within a minute: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
		gcc -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_asm_imm_search_budget: $path -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_asm_imm_search_budget: ok"
