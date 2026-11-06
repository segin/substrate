#!/bin/sh
# A subscript's operand is evaluated once, however many subscripts follow
# it.  Pointer arithmetic on a row of an array of arrays (`x[i]` where
# x[i] is itself an array) lowered the row's operand a second time, for
# its address, after lowering the row: at each level from the third the
# side effects ran twice as often -- `(*f())[i][j][k]` called f() four
# times.  Linux's hybrid_var(pmu, v)[i][j][k] is such an operand (a
# statement expression), whose static branch cc emitted four times.
# gcc is the judge: each side effect once, at -O0, -O2 and -Os, -m64 and
# -m32, both code paths (the machine IR's and the slot layout's).
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/s.c" <<'EOF2'
int n;
long a[2][3][4];
long b[5];

__attribute__((noinline)) long (*rows(void))[2][3][4]
{
	n += 1000;
	return &a;
}

int main(void)
{
	long r;
	long *p;

	(*({ n++; &a; }))[1][2][3] = 7;
	({ n += 10; a; })[1][1][1] = 5;
	({ n += 100; b; })[2] = 1;
	r = (*rows())[1][2][3];
	p = &({ n += 100000; a; })[1][0][2];
	*p = 9;
	return n == 101111 && r == 7 && a[1][1][1] == 5 && b[2] == 1 && a[1][0][2] == 9 ? 0 : 1;
}
EOF2
if command -v gcc >/dev/null 2>&1 && gcc -O2 -o "$CC_TMP/g" "$CC_TMP/s.c" 2>/dev/null && ! "$CC_TMP/g"; then
	echo "run_subscript_once: skip (gcc gives otherwise)"
	exit 0
fi
for m in 64 32; do
	for o in O0 O2 Os; do
		for path in mir legacy; do
			run=env
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			$run "$CC_BIN" -m$m -$o -o "$CC_TMP/s" "$CC_TMP/s.c"
			if ! "$CC_TMP/s"; then
				echo "run_subscript_once: -m$m -$o $path: a subscript's operand ran more than once"
				exit 1
			fi
		done
	done
done
echo "run_subscript_once: ok"
