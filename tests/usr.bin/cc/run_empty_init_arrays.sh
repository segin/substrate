#!/bin/sh
# `{ }` initializes a global array of any bounds, every byte zero, as
# gcc (and C23) has it.  An array of arrays with a bound of 0 -- Linux's
# fm10k `fm10k_prv_flags[FM10K_PRV_FLAG_LEN][ETH_GSTRING_LEN] = { }`,
# the length an enumerator of 0 -- was refused ("invalid array dimension
# in global initializer"), and so was one of one dimension of none ("empty
# initializer list for global").  Arrays of none and of some, of one and
# of two dimensions, static and not, const and not: the sizes and values
# are gcc's, -m64 and -m32, on both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_empty_init_arrays: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF'
#include <stdio.h>
enum { N0 };
static const char z[N0][8] = { };
static const int y[0] = { };
const char w[0][4] = { };
char e2[2][3] = { };
static long e1[3] = { };
int main(void)
{
	printf("%zu %zu %zu %zu %zu %d %ld\n", sizeof z, sizeof y, sizeof w, sizeof e2, sizeof e1, e2[1][2], e1[2]);
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
				echo "run_empty_init_arrays: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_empty_init_arrays: -m$m $path -$o prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_empty_init_arrays: ok"
