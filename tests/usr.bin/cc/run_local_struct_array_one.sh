#!/bin/sh
# A local array of one structure initialized by one braced item,
# `struct snd_soc_dapm_route routes[] = { { "BCLK", NULL, "FSYNC" } }` in
# Linux's sound/soc/codecs/cs42l43.c (and hinic3, brcmfmac), was taken for
# a character array given a string -- the item's first member "BCLK" --
# and refused ("string initializer exceeds local array size").  Only an
# array of characters takes a string so.  Arrays of one structure, of a
# structure with a character member, of one pointer, and of characters
# from a braced string: the program prints what gcc's prints, -m64 and
# -m32, -O0 and -O2, on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_local_struct_array_one: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>
struct route { const char *sink, *control, *source; int (*fn)(void); };
struct pair { char name[4]; int v; };
static int show(const struct route *r, int n)
{
	int i, t = 0;
	for (i = 0; i < n; i++)
		t += printf("%s %s %s; ", r[i].sink, r[i].control ? r[i].control : "-", r[i].source);
	return t;
}
int main(void)
{
	struct route one[] = { { "BCLK", NULL, "FSYNC" } };
	struct route two[2] = { { "A", "B", "C" } };
	struct pair p[] = { { "xyz", 3 } };
	const char *ptrs[] = { "only" };
	char s[4] = { "abc" };
	int n = show(one, 1) + show(two, 1);
	printf("%s %d %s %s %d\n", p[0].name, p[0].v, ptrs[0], s, n);
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
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
				echo "run_local_struct_array_one: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_local_struct_array_one: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_local_struct_array_one: ok"
