#!/bin/sh
# A function declared in a block, `extern int k(int);`, is a declaration
# of the file's k: cc's probe for a GNU nested function took any
# declarator with a parameter list for one, so the declaration was
# parsed as a nested function, made static, and refused ("multiple
# storage-class specifiers in function declaration"; "conflicting
# storage-class" for a later one in an inner block).  A nested function
# has a body; a declaration in a block now declares.  The program --
# declarations first and after statements, in an inner block, with an
# attribute, with a nested function beside them -- prints what gcc's
# prints, -m64 and -m32, -O0 and -O2, on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_block_extern_function: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF'
#include <stdio.h>
static int two(void) { return 2; }
int main(void)
{
	extern int first(void);
	int a = first();
	a += 1;
	extern int k(int);
	int (*fp)(void) = two;
	a += fp();
	extern int g;
	int x = k(a) + g;
	{
		a++;
		extern int k(int);
		extern int noinl(int) __attribute__((noinline));
		x += k(1) + noinl(2);
	}
	int sq(int v) { return v * v + a; }
	x += sq(3);
	printf("%d %d\n", a, x);
	return 0;
}
int g = 10;
int first(void) { return 1; }
int k(int v) { return v * 3; }
int noinl(int v) { return v + 100; }
EOF
for m in 64 32; do
	gcc -m$m -O0 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O2; do
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
				echo "run_block_extern_function: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_block_extern_function: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_block_extern_function: ok"
