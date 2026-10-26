#!/bin/sh
# A float parameter of the prototype a call goes through a pointer to is
# passed as a float, as a declared function's is: on x86-64 in single
# precision in its %xmm register, on i386 in one stack word.  Lowering
# took the single-precision ABI only from a called function's
# declaration, so through a pointer -- fp(), (*fp)(), s->fn() -- the
# float went as the double it is held in, and the callee read 0.  Results
# are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_call_ptr_float_param: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/f.c" <<'EOF2'
#include <stdio.h>

typedef void fn_t(float, int, float, double);

struct ops { fn_t *fn; };

void __attribute__((noinline)) take(float a, int n, float b, double d)
{
	printf("%.2f %d %.2f %.1f\n", a, n, b, d);
}

fn_t *volatile gp = take;
static struct ops o = { take };

int main(void)
{
	fn_t *lp = take;
	struct ops *po = &o;
	double h = 2.5;
	float q = 0.75f;
	int k = 9;

	gp(h, k, q, q);
	lp(h, k, q, q);
	(*gp)(h, k, q, q);
	po->fn(h, k, q, q);
	o.fn(k, k, h, k);
	take(h, k, q, q);
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/f.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/f.c"
		got=$("$CC_TMP/c") || { echo "run_call_ptr_float_param: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_call_ptr_float_param: -m$m -$o prints:"
			echo "$got"
			echo "gcc's:"
			echo "$want"
			exit 1
		fi
	done
done
echo "run_call_ptr_float_param: ok"
