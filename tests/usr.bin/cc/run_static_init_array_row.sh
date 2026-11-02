#!/bin/sh
# A row of an array of arrays, as a value, is its address, in a static
# initializer too: Linux's drivers/hwmon/f71805f.c (and w83792d.c,
# lpc_ich.c) have `.attrs = f71805f_attributes_optin[1]`, a row of a 2-D
# array of pointers, and cc refused it ("unsupported scalar in global
# initializer (expr kind 11)").  Rows of two- and three-dimensional
# arrays, with an offset, under &, in a structure array's members: the
# values are gcc's, -m64 and -m32, -O0 and -O2, on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_static_init_array_row: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF'
#include <stdio.h>
static int v0, v1, v2;
static int *a[4][5] = { { &v0 }, { &v1, &v2 } };
struct g { int **attrs; };
static const struct g gs[2] = { { .attrs = a[0] }, { .attrs = a[1] } };
static int **p = a[1];
static int **q = a[1] + 1;
static int *(*r)[5] = &a[2];
static char m[3][4][5];
static char *s = m[1][2];
static char (*t)[5] = m[2];
int main(void)
{
	printf("%d %d %d %d %d %d %d\n", (int)(gs[1].attrs - &a[0][0]), (int)(p - &a[0][0]), (int)(q - &a[0][0]),
	       (int)(r - a), (int)(s - &m[0][0][0]), (int)(t - m[0]), *gs[1].attrs[1] == v2);
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
				echo "run_static_init_array_row: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_static_init_array_row: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_static_init_array_row: ok"
