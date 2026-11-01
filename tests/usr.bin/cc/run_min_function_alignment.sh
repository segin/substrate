#!/bin/sh
# -fmin-function-alignment=N starts each function on N bytes, as gcc
# does -- and with -fpatchable-function-entry=16,16 the 16 nops before
# it, so that the function itself is on 16 too: Linux builds with both,
# and patches those 16 bytes as a whole.  cc took the option and
# ignored it.  Each function of the object is on 16 (an aligned(64) one
# on 64), the section is aligned to as much, the program runs as gcc's,
# and an N not a power of 2 is refused.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v readelf >/dev/null 2>&1; then
	echo "run_min_function_alignment: skip (no gcc or readelf)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stdio.h>

__attribute__((noinline)) char one(char x) { return x + 1; }
__attribute__((noinline)) char two(char x) { return one(x) * 2; }
__attribute__((noinline, aligned(64))) char wide(char x) { return two(x) - 3; }
__attribute__((noinline)) char three(char x) { return x ^ 3; }

int main(void)
{
	printf("%d\n", one(1) + two(2) + wide(3) + three(4));
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/b.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		for path in mir legacy; do
			run="env"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			for extra in "" "-fpatchable-function-entry=16,16 -fcf-protection=branch"; do
				what="-m$m -$o $path [$extra]"
				# shellcheck disable=SC2086
				if ! $run "$CC_BIN" -m$m -$o -fmin-function-alignment=16 $extra -c -o "$CC_TMP/b.o" "$CC_TMP/b.c" \
					2>"$CC_TMP/c.err"; then
					echo "run_min_function_alignment: $what: $(head -1 "$CC_TMP/c.err")"
					exit 1
				fi
				# (with the padding, aligned(64) is the 16 nops' before wide, as gcc has it: wide is on 16)
				wide=64
				if [ -n "$extra" ]; then wide=16; fi
				bad=$(readelf -sW "$CC_TMP/b.o" | awk -v w=$wide '$4 == "FUNC" {
						v = 0; h = tolower($2)
						for (i = 1; i <= length(h); ++i) v = v * 16 + index("0123456789abcdef", substr(h, i, 1)) - 1
						a = $8 == "wide" ? w : 16
						if (v % a != 0) print $8 "@" v
					}')
				if [ -n "$bad" ]; then
					echo "run_min_function_alignment: $what: not aligned: $bad"
					exit 1
				fi
				salign=$(readelf -SW "$CC_TMP/b.o" | awk '{ for (i = 1; i < NF; ++i) if ($i == ".text") print $NF }' | head -1)
				if [ "$salign" -lt 64 ]; then
					echo "run_min_function_alignment: $what: .text aligned to $salign"
					exit 1
				fi
				gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/b.o"
				if [ "$("$CC_TMP/c")" != "$want" ]; then
					echo "run_min_function_alignment: $what prints [$("$CC_TMP/c")], gcc's [$want]"
					exit 1
				fi
			done
		done
	done
done
for v in 3 12 x; do
	if "$CC_BIN" -fmin-function-alignment=$v -c -o "$CC_TMP/n.o" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
		echo "run_min_function_alignment: -fmin-function-alignment=$v was taken"
		exit 1
	fi
done
echo "run_min_function_alignment: ok"
