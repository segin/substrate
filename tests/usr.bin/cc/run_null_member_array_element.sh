#!/bin/sh
# An address through an element of an array of structures is a constant:
# `&((struct s *)0)->arr[2].c`, offsetof(struct s, arr[3].b[1]).  The
# evaluator scaled the index by the element's size, and with no structure
# table to hand it could size no structure, so a static initializer of
# one was refused ("unsupported global initializer"); an array member of
# scalars was right already.  The evaluator now asks its context for the
# size.  Offsets of null-based members, addresses of members of a static
# object's array element, an enumerator from offsetof: the values are
# gcc's, -m64 and -m32, -O0 and -O2, on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_null_member_array_element: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF'
#include <stdio.h>
#include <stddef.h>
struct in { int a; char b[3]; long c; };
struct s { int x; struct in arr[4]; short y; };
static unsigned long o1 = (unsigned long)&((struct s *)0)->arr[2].c;
static unsigned long o2 = offsetof(struct s, arr[3].b[1]);
static unsigned long o3 = (unsigned long)&((struct s *)0)->arr[1];
static unsigned long o4 = (unsigned long)&((struct s *)0)->arr->c;
static unsigned long o5 = (unsigned long)&(*(struct s *)0).arr[3].a;
static struct s g;
static long *p1 = &g.arr[1].c;
static char *p2 = &g.arr[2].b[2];
static struct in *p3 = &g.arr[3];
static struct in tab[3];
static int *p4 = &tab[2].a;
static char *p5 = tab[1].b + 1;
enum { E = offsetof(struct s, arr[1].c) };
int main(void)
{
	printf("%lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %d\n", o1, o2, o3, o4, o5, (char *)p1 - (char *)&g,
	       p2 - (char *)&g, (char *)p3 - (char *)&g, (char *)p4 - (char *)tab, p5 - (char *)tab, (int)E);
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
				echo "run_null_member_array_element: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_null_member_array_element: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_null_member_array_element: ok"
