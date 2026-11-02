#!/bin/sh
# A row of a character array may be a string in braces: `char c[2][3] =
# { "pq", { "rs" } }`.  cc took a braced row's string for a scalar of the
# row and refused it ("string scalar initializer requires pointer-sized
# pointer field").  And a string at the row level of an array of
# pointers, braces elided, is the first pointer, not a row.  Static,
# automatic and member arrays of char, signed char, unsigned char and
# wchar_t, and arrays of pointers braced and elided: the bytes and values
# are gcc's, -m64 and -m32, -O0 and -O2, on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_char_rows_braced: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF'
#include <stdio.h>
#include <wchar.h>
static char c3[2][3] = { "pq", { "rs" } };
static const unsigned char u3[3][3] = { { "ab" }, "c", { "de" } };
signed char s3[2][4] = { { "xyz" }, { "w" } };
static const wchar_t w3[2][3] = { L"ab", { L"cd" } };
static const char *p2[2][2] = { "a", "b", "c" };
static const char *q2[2][2] = { { "d", "e" }, { "f" } };
struct m { int n; char names[2][4]; } ms = { 1, { { "one" }, "two" } };
static void dump(const void *p, size_t n) { const unsigned char *b = p; while (n--) printf("%02x", *b++); printf(" "); }
int main(void)
{
	char l3[2][3] = { "gh", { "ij" } };
	static char t3[2][3] = { { "kl" }, "m" };
	dump(c3, 6); dump(u3, 9); dump(s3, 8); dump(w3, sizeof w3); dump(l3, 6); dump(t3, 6); dump(ms.names, 8);
	printf("%s %s %s %s %s %s %d\n", p2[0][0], p2[0][1], p2[1][0], q2[0][1], q2[1][0], p2[1][1] ? "x" : "null", ms.n);
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
				echo "run_char_rows_braced: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_char_rows_braced: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_char_rows_braced: ok"
