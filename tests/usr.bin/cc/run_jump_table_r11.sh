#!/bin/sh
# x86-64's jump through a switch table makes the table's address in
# %r11 before it reads the table at the index, so the index must not be
# in %r11.  With six values or more live across a switch in a loop the
# allocator gave it %r11 -- `leaq table(%rip), %r11; movslq
# (%r11,%r11,4), %r11` -- and the program jumped into the weeds.  cc
# built by itself crashed so in asm_immediate_fits().  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_jump_table_r11: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

for n in 6 8 11 14; do
	{
		echo '#include <stdio.h>'
		printf '__attribute__((noinline)) long f(const unsigned char *p, long x)\n{\n\tlong s = 0;\n'
		k=1
		while [ $k -le $n ]; do
			printf '\tlong v%d = x * %d + %d;\n' $k $((k * 2 + 1)) $k
			k=$((k + 1))
		done
		printf '\tfor (; *p; p++) {\n\t\tswitch (*p) {\n'
		k=1
		while [ $k -le $n ]; do
			printf '\t\tcase %d: s += v%d; break;\n' $((96 + k)) $k
			k=$((k + 1))
		done
		printf '\t\tcase 120: s ^= 7; break;\n\t\tcase 121: s -= 9; break;\n\t\tcase 122: s += 2; break;\n'
		printf '\t\tcase 123: s *= 5; break;\n\t\tdefault: s = s * 3 + 1; break;\n\t\t}\n\t}\n\treturn s;\n}\n'
		printf 'int main(void) { printf("%%ld\\n", f((const unsigned char *)"abcdefgxyz{|nabba", 3)); return 0; }\n'
	} >"$CC_TMP/j.c"
	gcc -O1 -o "$CC_TMP/g" "$CC_TMP/j.c"
	want=$("$CC_TMP/g")
	"$CC_BIN" -m64 -O2 -c -o "$CC_TMP/j.o" "$CC_TMP/j.c"
	gcc -no-pie -o "$CC_TMP/c" "$CC_TMP/j.o"
	got=$("$CC_TMP/c" || echo "exit $?")
	if [ "$got" != "$want" ]; then
		echo "run_jump_table_r11: $n values live: prints [$got], gcc's [$want]"
		exit 1
	fi
	"$CC_BIN" -m64 -O2 -S -o "$CC_TMP/j.s" "$CC_TMP/j.c"
	if ! grep -q '_jt0(%rip), %r11' "$CC_TMP/j.s"; then
		echo "run_jump_table_r11: $n values live: no jump table"
		exit 1
	fi
	if grep -q '(%r11,%r11,4)' "$CC_TMP/j.s"; then
		echo "run_jump_table_r11: $n values live: the index is in %r11"
		exit 1
	fi
done
echo "run_jump_table_r11: ok"
