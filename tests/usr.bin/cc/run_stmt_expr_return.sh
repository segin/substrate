#!/bin/sh
# A `return` in a statement expression returns from the function it is
# in, and is checked against that function's type: sema checked it
# against int, so returning a structure, a double or a pointer from
# inside `({ ... })` was refused ("return type mismatch").  Each returns
# what gcc's returns, -m64 and -m32, -O0 and -O2, on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_stmt_expr_return: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF'
#include <stdio.h>
struct r { int a, b; };
static struct r f(int x) { int v = ({ if (x < 0) return (struct r){-1, -1}; x * 2; }); return (struct r){v, 1}; }
static double g(int x) { return ({ if (x) return 2.5; 1; }) + 0.25; }
static char *h(char *s) { int n = ({ if (!s) return "null"; 3; }); return s + n; }
static void k(int *p) { ({ if (!p) return; *p = 4; }); }
static long long w(int x) { for (;;) { int y = ({ if (x > 2) return 1LL << 40; x + 1; }); x = y; } }
int main(void)
{
	int z = 0;

	k(&z);
	k(0);
	printf("%d %d %d %g %g %s %s %d %lld\n", f(3).a, f(-3).a, f(3).b, g(1), g(0), h("abcdef"), h(0), z, w(0));
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
				echo "run_stmt_expr_return: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_stmt_expr_return: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_stmt_expr_return: ok"
