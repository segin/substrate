#!/bin/sh
# -funsigned-char makes plain char unsigned, as gcc does; Linux builds
# with it.  cc took the option and ignored it, so in a kernel it built a
# char holding 128..255 compared, converted and extended as a negative
# number.  The program -- comparisons, loads through char pointers and
# arrays, conversions, a char bit-field, '\xff', a constant folded into
# a static initializer, a switch, a char parameter and result --
# prints what gcc's does, with the option and without, -m64 and -m32;
# __CHAR_UNSIGNED__ is gcc's; and _Generic still tells char from
# unsigned char.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_unsigned_char: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stdio.h>

struct bits { char c : 7; char d : 8; };
static const int folded = (char)200;
static char table[] = { 'a', (char)0x80, (char)0xff, 0x7f };

__attribute__((noinline)) char pass(char c) { return c; }
__attribute__((noinline)) int widen(const char *p) { return p[0] + p[1]; }
__attribute__((noinline)) int which(char c)
{
	switch (c) {
	case -1: return 1;
	case (char)0xff: return 2;
	default: return 3;
	}
}

int main(void)
{
	volatile char c = (char)200;
	struct bits b = { 0, 0 };
	unsigned i;
	long sum = 0;

	b.c = 0x7f;
	b.d = (char)0xf0;
	for (i = 0; i < sizeof(table); ++i) {
		sum = sum * 3 + table[i];
	}
	printf("%d %d %d %d %d %d %ld\n", c > 0, c < 0, (int)c, c >> 1, '\xff', folded, sum);
	printf("%d %d %d %d %d\n", b.c, b.d, pass((char)0x90), widen(table + 1), which((char)0xff));
	printf("%d %d\n", _Generic((char)0, char: 1, signed char: 2, unsigned char: 3),
	       _Generic((unsigned char)0, char: 1, signed char: 2, unsigned char: 3));
#ifdef __CHAR_UNSIGNED__
	printf("unsigned\n");
#endif
	return 0;
}
EOF2
for m in 64 32; do
	for f in -funsigned-char "" -fsigned-char "-funsigned-char -fno-unsigned-char" -fno-signed-char; do
		# shellcheck disable=SC2086
		gcc -m$m -O1 $f -w -o "$CC_TMP/g" "$CC_TMP/b.c" 2>/dev/null || continue
		want=$("$CC_TMP/g")
		for o in O0 O2; do
			for path in mir legacy; do
				run="env"
				if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
				# shellcheck disable=SC2086
				if ! $run "$CC_BIN" -m$m -$o $f -o "$CC_TMP/c" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
					echo "run_unsigned_char: -m$m -$o $path [$f]: $(head -1 "$CC_TMP/c.err")"
					exit 1
				fi
				got=$("$CC_TMP/c")
				if [ "$got" != "$want" ]; then
					echo "run_unsigned_char: -m$m -$o $path [$f] prints [$got], gcc's [$want]"
					exit 1
				fi
			done
		done
	done
done
echo "run_unsigned_char: ok"
