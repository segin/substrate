#!/bin/sh
# A file-scope compound literal in a declaration is hoisted once: the
# parser probes whether a declaration is a function's on a copy of
# itself, and the copy hoisted the literals in an array bound into
# globals that stayed when the probe was thrown away, so the real parse
# defined them again under the same names -- Linux's peb2466.c, whose
# `tlv_array[ARRAY_SIZE((unsigned int[]){ ... })]` was refused with
# "duplicate file-scope definition: __cc_literal_0".  The sizes and
# values are gcc's, -m64 and -m32, -O0 and -O2, through both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_file_literal_probe: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>

#define N(x) (sizeof(x) / sizeof((x)[0]))
#define SZ N(((unsigned int[]){ 1, 2, 3 }))

struct t { unsigned int tlv[SZ]; };
static struct t a;
unsigned int g1[SZ], g2[N(((char[]){ "abcde" }))];
const int *pl = (const int[]){ 7, 8 };

int f(const unsigned char *data, unsigned long size)
{
	unsigned int b[SZ];

	return (int)(sizeof b + sizeof a + data[0] + size);
}

int main(void)
{
	unsigned char d[1] = { 1 };

	printf("%zu %zu %zu %d %d\n", sizeof a.tlv, sizeof g1, sizeof g2, f(d, 2), pl[1]);
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
				echo "run_file_literal_probe: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_file_literal_probe: ok"
