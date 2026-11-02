#!/bin/sh
# __builtin_constant_p of a string or floating literal is 1, as gcc has
# it; cc folded it while parsing and counted only integer constants, so
# it was 0.  Linux's printk index keeps a message's format as
# `__builtin_constant_p(fmt) ? fmt : NULL`, and every one became NULL.
# In static and automatic initializers, of strings, a float, a cast
# string and an integer: the program prints what gcc's prints, -m64 and
# -m32, -O0 and -O2, on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_constant_p_literals: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF'
#include <stdio.h>
static int a = __builtin_constant_p("abc");
static int b = __builtin_constant_p(1.5);
static const char *f = __builtin_constant_p("fmt") ? "fmt" : 0;
int main(int argc, char **argv)
{
	static int c = __builtin_constant_p((const char *)"x");
	int d = __builtin_constant_p("y");
	int e = __builtin_constant_p(argc);
	int g = __builtin_constant_p(7 * 6);
	(void)argv;
	printf("%d %d %s %d %d %d %d\n", a, b, f ? f : "(null)", c, d, e, g);
	return 0;
}
EOF
for m in 64 32; do
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O2; do
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
				echo "run_constant_p_literals: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_constant_p_literals: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_constant_p_literals: ok"
