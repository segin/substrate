#!/bin/sh
# A static array of more than one dimension whose elements are
# structures is initialized as gcc initializes it: cc's walker for
# arrays of more dimensions took the innermost elements for scalars, so
# a structure given its braced list was refused ("string scalar
# initializer requires pointer-sized pointer field" for Linux's
# nf_conntrack_ftp.c `search[IP_CT_DIR_MAX][2]`, "scalar initializer
# list is empty" for amd/quirks.c's `ivrs_ioapic_quirks[][3]` with its
# `{}` rows).  Rows designated out of order, braced and elided elements,
# an inferred outer bound, a static local and an automatic array: the
# values are gcc's, -m64 and -m32, -O0 and -O2, through both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_struct_array_2d_init: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>

struct s { const char *p; long n; char c; };
struct q { int id, dev; };

struct s t1[2][2] = { [1] = { { .p = "x", .n = 1 }, { .p = "y", .c = 3 } }, [0] = { { "a", 2, 4 } } };
const struct q t2[][3] = { [0] = { { .id = 4 }, { 5, 6 }, {} }, [1] = { { 7 }, {} }, {} };
struct s t3[2][2] = { { { "m", 1, 1 }, { "n", 2, 2 } }, { { "o" } } };
struct q t4[2][2] = { 1, 2, 3, 4, 5 };

static void show(const char *tag, struct s (*t)[2])
{
	int i, j;

	printf("%s", tag);
	for (i = 0; i < 2; i++)
		for (j = 0; j < 2; j++)
			printf(" %s/%ld/%d", t[i][j].p ? t[i][j].p : "-", t[i][j].n, t[i][j].c);
	printf("\n");
}

int main(void)
{
	static struct s sl[2][2] = { [1] = { [1] = { "z", 9, 9 } } };
	struct s au[2][2] = { { { "k", 5 } }, [1] = { [1] = { .c = 6 } } };
	int i, j;

	show("t1", t1);
	show("t3", t3);
	show("sl", sl);
	show("au", au);
	printf("t2 %zu:", sizeof t2 / sizeof t2[0]);
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			printf(" %d/%d", t2[i][j].id, t2[i][j].dev);
	printf("\nt4:");
	for (i = 0; i < 2; i++)
		for (j = 0; j < 2; j++)
			printf(" %d/%d", t4[i][j].id, t4[i][j].dev);
	printf("\n");
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
				echo "run_struct_array_2d_init: -m$m $path -$o differs from gcc:"
				printf '%s\n' "$want" >"$CC_TMP/want"
				printf '%s\n' "$got" >"$CC_TMP/got"
				diff "$CC_TMP/want" "$CC_TMP/got" || true
				exit 1
			fi
		done
	done
done
echo "run_struct_array_2d_init: ok"
