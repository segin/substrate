#!/bin/sh
# A static local array of structures is given its initializer as data,
# as a file-scope array is, where every name the initializer uses is of
# file scope: it was code that filled it on the function's first call,
# under a guard -- Linux's format_decode() has a table of 256 elements,
# `['l'] = { 0, sizeof(long), sizeof(long long) }` and so on, that was 149
# byte stores.  Designators, a range, elided braces, holes left zero, and
# a member pointing at a file-scope object (a relocation) all hold; an
# initializer naming a block-scope object still fills the array at run
# time.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_static_struct_array_data: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/s.c" <<'EOF2'
#include <stdio.h>

enum { ST_NONE, ST_CHAR, ST_STR };
static const char hello[] = "hello";
int counter = 7;

int __attribute__((noinline)) look(unsigned char c)
{
	static const struct format_state {
		unsigned char state;
		unsigned char size;
		unsigned char flags_or_double_size;
		unsigned char base;
	} lookup_state[256] = {
		['l'] = { 0, sizeof(long), sizeof(long long) },
		['h'] = { 0, sizeof(short), sizeof(char) },
		['c'] = { ST_CHAR },
		['s'] = { ST_STR },
		['0' ... '9'] = { 0, 0, 0, 10 },
		['x'] = 3, 4, 5, 6,
	};
	const struct format_state *p = lookup_state + c;

	return p->state * 1000 + p->size * 100 + p->flags_or_double_size * 10 + p->base;
}

const char *__attribute__((noinline)) name(int i, int *n)
{
	static const struct ent {
		const char *s;
		int *count;
		short k;
	} ents[] = {
		{ hello, &counter, 3 },
		[2] = { hello + 2, 0, -4 },
		{ .k = 9, .s = "lit" },
	};

	*n = ents[i].k + (ents[i].count != 0 ? *ents[i].count : 0);
	return ents[i].s;
}

int __attribute__((noinline)) local_names(int i)
{
	static int x = 5, y = 6;
	static const struct { int *p; } t[] = { { &x }, { &y } };

	return *t[i].p;
}

int main(void)
{
	int n0, n1, n2, n3;
	const char *a = name(0, &n0), *b = name(2, &n2), *c = name(3, &n3);
	int hole = name(1, &n1) == 0;
	int v[8];

	v[0] = look('l');
	v[1] = look('h');
	v[2] = look('c');
	v[3] = look('5');
	v[4] = look('x');
	v[5] = look('q');
	v[6] = local_names(0);
	v[7] = local_names(1);
	printf("%d %d %d %d %d %d %d %d %s %d %s %d %s %d %d\n", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7],
	       a, n0, b, n2, c, n3, hole);
	return 0;
}
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/s.c"
	want=$("$CC_TMP/g")
	for o in O0 O1 O2 Os; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/c" "$CC_TMP/s.c"
		got=$("$CC_TMP/c") || { echo "run_static_struct_array_data: -m$m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_static_struct_array_data: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/s.s" "$CC_TMP/s.c"
	for fn in look name; do
		if awk -v f="$fn" '$0 == f ":" {x=1} $0 ~ "^\\.size " f "," {x=0} x' "$CC_TMP/s.s" | grep -qE '__guard|^	mov[bwlq] \$'; then
			echo "run_static_struct_array_data: -m$m: $fn() fills its table at run time"
			exit 1
		fi
	done
done
echo "run_static_struct_array_data: ok"
