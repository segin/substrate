#!/bin/sh
# The address of a member of an object a constant points to,
# `&((T *)K)->m`, is a constant -- K and the member's offset -- in a
# static initializer, as gcc has it; so is an element of an array member
# of one, `&((T *)0)->a[k]`, and a member of a member.  cc refused them
# ("unsupported scalar in global initializer"): Linux's
# sound/synth/emux has `((long)(&((type)NULL)->tag) - (long)(NULL))`,
# offsetof the old way, and net/core/neighbour.c
# `&((struct neigh_parms *) 0)->data[index]` (the Arch config).  Values
# are gcc's, -m64 and -m32.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_null_member_address: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/n.c" <<'EOF2'
#include <stddef.h>
#include <stdio.h>

struct s {
	int a;
	short b;
	long c;
};
struct n {
	long a;
	int data[8];
	struct s one;
};
#define xoffsetof(type, tag) ((long)(&((type)NULL)->tag) - (long)(NULL))

static long t1 = xoffsetof(struct s *, c);
static struct {
	int x;
	long off;
} t2[] = { { 1, xoffsetof(struct s *, b) }, { 2, xoffsetof(struct s *, c) } };
static long t3 = (long)&((struct s *)0)->c;
static long t4 = (long)&((struct s *)16)->b;
static long t5 = (long)&((struct n *)0)->data[3];
static long t6 = (long)&((struct n *)0)->one.c;
static void *t7 = &((struct n *)0)->data[5];
static int *t8 = &((struct n *)64)->data[2];

int main(void)
{
	printf("%ld %ld %ld %ld %ld %ld %ld %ld %ld\n", t1, t2[0].off, t2[1].off, t3, t4, t5, t6, (long)t7, (long)t8);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/n.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		if ! "$CC_BIN" -m$m -$o -c -o "$CC_TMP/n.o" "$CC_TMP/n.c" 2>"$CC_TMP/c.err"; then
			echo "run_null_member_address: -m$m -$o: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/n.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_null_member_address: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_null_member_address: ok"
