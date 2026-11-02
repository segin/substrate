#!/bin/sh
# gcc's __alignof__ is a type's preferred alignment: on i386, 8 for long
# long, double and _Complex double and arrays of them, where _Alignof and
# the ABI say 4.  A structure, a structure member and long double keep 4.
# cc answered 4 throughout.  Types and objects, against gcc -m32, on both
# code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_alignof_i386_preferred: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>
struct sl { long long x; };
struct sd { char c; double d; };
long long gll;
long double gld;
struct sl gs;
long long garr[3];
int main(void)
{
	double d;
	struct sd s;
	_Complex double cd;
	printf("%zu %zu %zu %zu %zu %zu %zu %zu\n", __alignof__(long long), __alignof__(unsigned long long),
	       __alignof__(double), __alignof__(long double), __alignof__(struct sl), __alignof__(struct sd),
	       __alignof__(long long[3]), __alignof__(_Complex double));
	printf("%zu %zu %zu %zu %zu %zu %zu %zu\n", __alignof__(d), __alignof__(gll), __alignof__(gld),
	       __alignof__(gs), __alignof__(garr), __alignof__(s.d), __alignof__(cd), __alignof__(gs.x));
	printf("%zu %zu\n", _Alignof(long long), _Alignof(double));
	return 0;
}
EOF2
gcc -m32 -O0 -o "$CC_TMP/g" "$CC_TMP/p.c" 2>/dev/null || {
	echo "run_alignof_i386_preferred: skip (no gcc -m32)"
	exit 0
}
want=$("$CC_TMP/g")
for path in mir legacy; do
	run="env"
	if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
	$run "$CC_BIN" -m32 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c"
	gcc -m32 -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
	got=$("$CC_TMP/c")
	if [ "$got" != "$want" ]; then
		echo "run_alignof_i386_preferred: $path prints"
		echo "$got"
		echo "gcc's"
		echo "$want"
		exit 1
	fi
done
echo "run_alignof_i386_preferred: ok"
