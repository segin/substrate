#!/bin/sh
# A string whose characters fill a character array exactly initializes it
# without its terminating null (C11 6.7.9p14), braced or not.  Braced in
# a static array -- Linux's carl9170 `static const u8 otus_magic[4] =
# { "OTAR" }` -- it was refused ("string/byte initializer list for
# otus_magic exceeds array size"), the emitter writing the null always.
# And a static array of signed char was refused a braced string at all
# ("string element in initializer list ... requires pointer element
# type"), signed char not being counted among the character types.
# Static, file-scope and automatic arrays, braced and not, of char,
# signed char and unsigned char, and a string with room for its null:
# the bytes are gcc's, -m64 and -m32, -O0 and -O2, on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_string_init_exact_fit: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF'
#include <stdio.h>
static const unsigned char m1[4] = { "OTAR" };
static const unsigned char m2[4] = "OTAR";
const unsigned char m3[3] = { "abc" };
char m4[5] = { "abc" };
static const signed char s1[3] = { "abc" };
signed char s2[5] = "ab";
static signed char s3[2][3] = { "pq", "rs" };
struct s { char tag[2]; int v; } sv = { { "hi" }, 7 };
static void dump(const void *p, size_t n) { const unsigned char *b = p; while (n--) printf("%02x", *b++); printf(" "); }
int main(void)
{
	static const char l1[3] = { "xyz" };
	char l2[3] = { "pqr" };
	unsigned char l3[4] = "wxyz";
	static const signed char ls[4] = { "neg" };
	dump(m1, 4); dump(m2, 4); dump(m3, 3); dump(m4, 5); dump(sv.tag, 2); dump(l1, 3); dump(l2, 3); dump(l3, 4);
	dump(s1, 3); dump(s2, 5); dump(s3, 6); dump(ls, 4);
	printf("%d\n", sv.v);
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
				echo "run_string_init_exact_fit: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_string_init_exact_fit: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_string_init_exact_fit: ok"
