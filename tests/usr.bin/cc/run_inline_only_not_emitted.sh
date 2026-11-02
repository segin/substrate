#!/bin/sh
# An inline definition is never emitted: gnu_inline's `extern inline`,
# GNU89's `extern inline` and C99's plain `inline` (C11 6.7.4p7) give
# gcc a body to inline, and a call it leaves and the function's address
# are the external function's.  cc emitted a local copy under the
# function's own name, so each file had its own `memcmp` -- Linux's
# fortified one -- and modpost refused vmlinux ("local symbol 'memcmp'
# was exported"), and the function's address in the file was not the
# address elsewhere.  The defined and undefined symbols are gcc's, and a
# program linked with the external definitions prints what gcc's does,
# -O0 and -O2, -m64 and -m32, both code paths.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_inline_only_not_emitted: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/u.c" <<'EOF2'
typedef unsigned long size_t;
extern int cmp(const void *, const void *, size_t);
extern inline __attribute__((always_inline, gnu_inline)) int cmp(const void *a, const void *b, size_t n)
{
	return __builtin_memcmp(a, b, n) + 100;
}
extern inline __attribute__((gnu_inline)) int plus(int x) { return x + 1; }
inline int twice(int x) { return 2 * x; }
int use(const void *a, const void *b) { return cmp(a, b, 4) + plus(1) + twice(3); }
int (*cmp_addr(void))(const void *, const void *, size_t) { return cmp; }
int (*plus_addr(void))(int) { return plus; }
EOF2
cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>
#include <string.h>
int cmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }
int plus(int x) { return x + 1000; }
int twice(int x) { return 2000 * x; }
int use(const void *a, const void *b);
int (*cmp_addr(void))(const void *, const void *, size_t);
int (*plus_addr(void))(int);
int main(void)
{
	printf("%d %d %d\n", use("abcd", "abcd"), cmp_addr() == cmp, plus_addr() == plus);
	return 0;
}
EOF2
syms() {
	nm "$1" | awk '{ if (NF == 2) print $1, $2; else print $2, $3 }' | sort | tr '\n' ' '
}
for m in 64 32; do
	gcc -m$m -c -o "$CC_TMP/m.o" "$CC_TMP/m.c" 2>/dev/null || continue
	for o in -O0 -O2; do
		gcc -m$m $o -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c"
		gcc -m$m -no-pie -o "$CC_TMP/g" "$CC_TMP/g.o" "$CC_TMP/m.o"
		want=$("$CC_TMP/g")
		for path in mir legacy; do
			run="env"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			$run "$CC_BIN" -m$m $o -c -o "$CC_TMP/c.o" "$CC_TMP/u.c"
			if [ "$(syms "$CC_TMP/c.o")" != "$(syms "$CC_TMP/g.o")" ]; then
				echo "run_inline_only_not_emitted: -m$m $o $path: symbols [$(syms "$CC_TMP/c.o")], gcc's [$(syms "$CC_TMP/g.o")]"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/c.o" "$CC_TMP/m.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_inline_only_not_emitted: -m$m $o $path prints [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_inline_only_not_emitted: ok"
