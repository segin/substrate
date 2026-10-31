#!/bin/sh
# A file of many declarations compiles in time near gcc's.  Each
# declaration, call and use looked its name up by walking every function,
# object and variable declared before it, and a file of Linux, whose
# headers declare thousands, took cc 19 seconds and gcc 1.  Functions,
# file-scope objects and the parser's variables are now found by name.
# The file here declares 20000 functions and 20000 objects, each twice,
# and calls some of them: cc must take no more than eight times gcc's
# time and two seconds -- before, 27 times.  What the declarations mean
# is checked too: a redeclaration merges with the first, a block's
# variable hides a file-scope one and is gone after the block, a
# function's attributes are those of all its declarations.  Results are
# gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_many_declarations: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# milliseconds since the epoch
now_ms() {
	echo $(($(date +%s%N) / 1000000))
}

awk 'BEGIN {
	n = 20000
	for (i = 0; i < n; i++)
		printf "extern int f%d(int);\nint f%d(int);\nextern int g%d;\nextern int g%d;\n", i, i, i, i
	for (k = 0; k < 300; k++)
		printf "int c%d(int x)\n{\n\treturn f%d(x) + g%d + f%d(x);\n}\n", k, k * 61 % n, k * 67 % n, k * 71 % n
}' >"$CC_TMP/many.c"
t0=$(now_ms)
gcc -O2 -S -o "$CC_TMP/g.s" "$CC_TMP/many.c"
t1=$(now_ms)
"$CC_BIN" -O2 -S -o "$CC_TMP/c.s" "$CC_TMP/many.c"
t2=$(now_ms)
gcc_ms=$((t1 - t0))
cc_ms=$((t2 - t1))
if [ "$cc_ms" -gt $((gcc_ms * 8 + 2000)) ]; then
	echo "run_many_declarations: cc took ${cc_ms} ms, gcc ${gcc_ms}"
	exit 1
fi

cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>

extern int shared;
int shared;
extern int shared;
int shared = 7;

int twice(int);
__attribute__((noinline)) int twice(int);
int twice(int x)
{
	return x * 2;
}

int x = 1;

static int hidden(void)
{
	int r = x;
	{
		int x = 10;

		r += x;
		{
			int x = 100;

			r += x;
		}
		r += x;
	}
	return r + x;
}

int main(void)
{
	printf("%d %d %d\n", shared, twice(21), hidden());
	return 0;
}
EOF2
gcc -O1 -o "$CC_TMP/g" "$CC_TMP/m.c"
want=$("$CC_TMP/g")
for o in O0 O2; do
	if ! "$CC_BIN" -$o -o "$CC_TMP/c" "$CC_TMP/m.c" 2>"$CC_TMP/c.err"; then
		echo "run_many_declarations: -$o: $(head -1 "$CC_TMP/c.err")"
		exit 1
	fi
	got=$("$CC_TMP/c")
	if [ "$got" != "$want" ]; then
		echo "run_many_declarations: -$o prints [$got], gcc's [$want]"
		exit 1
	fi
done
echo "run_many_declarations: ok"
