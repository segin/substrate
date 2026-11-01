#!/bin/sh
# A function declared nocf_check (Linux's __noendbr) has no endbr at
# its entry under -fcf-protection=branch, as gcc's has not: no indirect
# call is to reach it, and IBT faults one that does.  cc gave it one as
# it gives every function.  The others keep theirs; __has_attribute
# knows the attribute; and the program runs as gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v objdump >/dev/null 2>&1; then
	echo "run_nocf_check: skip (no gcc or objdump)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stdio.h>

#if !__has_attribute(nocf_check)
#error no nocf_check
#endif
#define __noendbr __attribute__((nocf_check))

__noendbr __attribute__((noinline)) int direct(int x) { return x * 5; }
int later(int x);
__noendbr int later(int x);
__attribute__((noinline)) int later(int x) { return x - 3; }
__attribute__((noinline)) int plain(int x) { return x + 1; }

int main(void)
{
	printf("%d\n", direct(4) + later(10) + plain(1));
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/b.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	if [ $m = 64 ]; then endbr=endbr64; else endbr=endbr32; fi
	for o in O0 O2; do
		for path in mir legacy; do
			run="env"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			if ! $run "$CC_BIN" -m$m -$o -fcf-protection=branch -c -o "$CC_TMP/b.o" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
				echo "run_nocf_check: -m$m -$o $path: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			got=$(objdump -d --no-show-raw-insn "$CC_TMP/b.o" | awk -v e="$endbr" '
				/^[0-9a-f]+ <[^>]+>:$/ { name = $2; first = 1; next }
				first && NF > 1 { print name, ($2 == e); first = 0 }' | sort | tr '\n' ' ')
			if [ "$got" != "<direct>: 0 <later>: 0 <main>: 1 <plain>: 1 " ]; then
				echo "run_nocf_check: -m$m -$o $path: endbr at entries [$got]"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/b.o"
			if [ "$("$CC_TMP/c")" != "$want" ]; then
				echo "run_nocf_check: -m$m -$o $path prints [$("$CC_TMP/c")], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_nocf_check: ok"
