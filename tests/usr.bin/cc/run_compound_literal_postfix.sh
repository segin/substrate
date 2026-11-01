#!/bin/sh
# A compound literal is a postfix expression (C11 6.5.2), and takes the
# postfix operators after it: cc's parser returned it alone, so Linux's
# `while (j < (const int[]){ 64, 64, 64, 63 }[i])` (curve25519) was
# refused with "expected ')' after while condition".  Subscripts, member
# accesses and `->` on a decayed array, in conditions, returns and
# initializers: the values are gcc's, -m64 and
# -m32, -O0 and -O2, through both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_compound_literal_postfix: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>

struct p { int x, y; };

static int bits(int i)
{
	int j = 3;

	while (j < (const int[]){ 64, 64, 64, 63 }[i])
		j += 7;
	return j;
}

static int pick(int i) { return (int[]){ 10, 20, 30 }[i] + (struct p){ 1, 2 }.y; }
static int arrow(void) { return (struct p[]){ { 5, 6 }, { 7, 8 } }->y; }

int main(void)
{
	int k = (const char[]){ "abc" }[1];

	printf("%d %d %d %d %d %d\n", bits(0), bits(3), pick(0), pick(2), arrow(), k);
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
			$run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c"
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_compound_literal_postfix: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_compound_literal_postfix: ok"
