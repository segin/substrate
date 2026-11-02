#!/bin/sh
# A `break` or `continue` in a statement expression `({ ... })` is the
# enclosing loop's or switch's (GNU C): cc checked and lowered the
# statements of one as if no loop were around them, so Linux's
# bpf_for_each_reg_in_vstate_mask(..., ({ if (...) continue; ... }))
# in kernel/bpf/verifier.c was refused, "continue used outside loop".
# continue in a for, break in a while, break in a switch, continue from
# a statement expression used as a value in a do-while: the values are
# gcc's, -m64 and -m32, -O0 and -O2, through both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_stmt_expr_break: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>

static int f(int n)
{
	int s = 0;

	for (int i = 0; i < n; i++) {
		({ if (i & 1) continue; s += i; });
	}
	return s;
}

static int g(int n)
{
	int s = 0;

	while (n--)
		({ if (n == 3) break; s++; });
	return s;
}

static int h(int n)
{
	int s = 0;

	switch (n) {
	case 1:
		({ if (n) break; s = 5; });
		s = 7;
		break;
	default:
		s = 9;
	}
	return s;
}

static int k(int n)
{
	int s = 0;

	do {
		s += ({ int t = n; if (t == 2) { n--; continue; } t; });
	} while (--n > 0);
	return s;
}

int main(void)
{
	printf("%d %d %d %d\n", f(10), g(9), h(1), k(5));
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
				echo "run_stmt_expr_break: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_stmt_expr_break: ok"
