#!/bin/sh
# A structure member that is an array of more than one dimension, in a
# static object, is initialized as gcc initializes it: cc stepped through
# it one innermost element at a time but took each item as a whole row,
# so `int a[2][3]` given { {1, 2, 3}, {4, 5, 6} } held 1 and 4, and a row
# of characters given a string -- Linux's `u8 codecs[3][9]` given
# { "ESSX8316", ... } -- was refused.  Rows braced, flat, partly braced
# and elided, strings among braced rows, three dimensions: the bytes are
# gcc's, -m64 and -m32, -O0 and -O2, through both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_member_2d_init: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>

struct c { int n; unsigned char codecs[3][9]; int t; };
struct i { int a[2][3]; int x; };
struct w { short s[2][2][2]; };

const struct c x1 = { .n = 3, .codecs = { "ESSX8316", "ESSX8326", "ESSX8336" }, 7 };
const struct c x2 = { 3, { "ab", { 1, 2 }, "cd" }, 8 };
const struct i q1 = { { { 1, 2, 3 }, { 4, 5, 6 } }, 9 };
const struct i q2 = { { 1, 2, 3, 4 }, 9 };
const struct i q3 = { { { 1 }, 4, 5 }, 9 };
const struct i q4 = { 1, 2, 3, 4, 5, 6, 9 };
const struct i q5 = { 1, 2, .x = 9 };
const struct w z = { { { { 1, 2 } }, { 3, 4, 5 } } };

static void dump(const char *n, const void *p, unsigned long s)
{
	const unsigned char *b = p;
	unsigned long k;

	printf("%s:", n);
	for (k = 0; k < s; k++)
		printf(" %02x", b[k]);
	printf("\n");
}

int main(void)
{
	dump("x1", &x1, sizeof x1);
	dump("x2", &x2, sizeof x2);
	dump("q1", &q1, sizeof q1);
	dump("q2", &q2, sizeof q2);
	dump("q3", &q3, sizeof q3);
	dump("q4", &q4, sizeof q4);
	dump("q5", &q5, sizeof q5);
	dump("z", &z, sizeof z);
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
				echo "run_member_2d_init: -m$m $path -$o differs from gcc:"
				printf '%s\n' "$want" >"$CC_TMP/want"
				printf '%s\n' "$got" >"$CC_TMP/got"
				diff "$CC_TMP/want" "$CC_TMP/got" || true
				exit 1
			fi
		done
	done
done
echo "run_member_2d_init: ok"
