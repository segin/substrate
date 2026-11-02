#!/bin/sh
# A structure tag defined in a block ends with the block: cc hid a
# function's block-scope tags only when the function ended, so a sibling
# block defining the same tag was refused with "redefinition of
# struct/union tag" -- Linux's tg3.c defines `static struct tg3_dev_id`
# in two `if` blocks of one function, e1000_main.c `struct my_u` in a loop
# body.  Sibling blocks, a static local's type, a tag shadowing an outer
# one and the outer one in effect again after the block: the values are
# gcc's, -m64 and -m32, -O0 and -O2, through both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_block_struct_tags: skip (no gcc)"
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

	if (n & 1) {
		static struct id { int v, w; } a[] = { { 1, 2 }, { 0 } };
		struct id *p = a;

		while (p->v)
			s += p++->w;
	}
	if (n & 2) {
		static struct id { int v; long w; } b[] = { { 3, 40 }, { 5, 60 }, { 0 } };
		struct id *p = b;

		while (p->v)
			s += (int)p++->w;
	}
	return s;
}

static int g(int n)
{
	int s = 0;

	if (n) {
		struct q { int v; } x = { 1 };
		s += x.v;
	} else {
		struct q { char c[3]; } y = { "ab" };
		s += y.c[1];
	}
	return s;
}

static int h(void)
{
	struct o { int a; } outer = { 7 };
	int s = 0;

	for (int i = 0; i < 2; i++) {
		struct o { long b[3]; } inner = { { i, 2, 3 } };
		s += (int)sizeof inner + (int)inner.b[0];
	}
	{
		struct o again = { 9 };
		s += again.a + outer.a + (int)sizeof again;
	}
	return s;
}

int main(void)
{
	printf("%d %d %d %d %d %d\n", f(1), f(2), f(3), g(1), g(0), h());
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
				echo "run_block_struct_tags: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_block_struct_tags: ok"
