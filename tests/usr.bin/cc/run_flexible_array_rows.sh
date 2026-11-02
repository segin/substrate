#!/bin/sh
# A flexible array member whose elements are arrays, `const int map[][4]`
# (Linux's drivers/ata/ata_piix.c struct piix_map_db), is initialized row
# by row in a static object: cc sized the object by the innermost element
# and flattened the rows as one dimension of pointers, and refused it
# ("global initializer write exceeds destination object size").  Rows
# braced, elided and mixed, rows of characters from strings, and a plain
# flexible string still: the bytes are gcc's, -m64 and -m32, -O0 and -O2,
# on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_flexible_array_rows: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>
#include <string.h>
struct db { const unsigned mask; const unsigned short port; const int map[][4]; };
static const struct db d5 = { .mask = 7, .port = 3, .map = { { 1, -1, 2, -1 }, { 3, 4, 5, 6 }, { 7 } } };
static const struct db de = { 1, 2, { 1, 2, 3, 4, 5, 6 } };
static const struct db dm = { 1, 2, { 1, 2, 3, 4, { 9, 8 } } };
struct cr { int n; char rows[][3]; };
static const struct cr c = { 2, { "ab", { 'x', 'y' }, "z" } };
static const struct cr c1 = { 1, { "ab" } };
struct f1 { int n; char s[]; };
static struct f1 one = { 2, "hello" };
static void dump(const void *p, size_t n) { const unsigned char *b = p; while (n--) printf("%02x", *b++); printf(" "); }
int main(void)
{
	dump(&d5, sizeof d5 + 3 * 16); dump(&de, sizeof de + 2 * 16); dump(&dm, sizeof dm + 2 * 16);
	dump(&c, sizeof c + 9); dump(&c1, sizeof c1 + 3);
	printf("%s\n", one.s);
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
				echo "run_flexible_array_rows: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_flexible_array_rows: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_flexible_array_rows: ok"
