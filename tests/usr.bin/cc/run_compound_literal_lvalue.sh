#!/bin/sh
# A compound literal is an lvalue (C11 6.5.2.5p4), and one of scalar type
# can be incremented and assigned to as itself: `(int){4}++`,
# `++(int){4}`, `(int){1} += 2`.  cc refused them ("++/-- requires an
# identifier, dereference, or member lvalue", "left-hand side of
# assignment must be an identifier, dereference, or member access"), its
# members and elements having been accepted already.  The values are
# gcc's, -m64 and -m32, -O0 and -O2, on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_compound_literal_lvalue: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF'
#include <stdio.h>
struct s { int x; int a[2]; };
int main(void)
{
	int a = (int){4}++;
	int b = ++(int){4};
	int c = (struct s){5, {1, 2}}.x++;
	int d = --(struct s){5, {1, 2}}.a[1];
	long e = ((long){7} *= 3);
	unsigned char f = --(unsigned char){0};
	double g = ++(double){1.5};
	int h = ((int){1} = 9) + ((int){2} <<= 3);
	int *p = &(int){7};
	int i = (*p)++ + *p;
	printf("%d %d %d %d %ld %u %g %d %d\n", a, b, c, d, e, f, g, h, i);
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
				echo "run_compound_literal_lvalue: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_compound_literal_lvalue: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_compound_literal_lvalue: ok"
