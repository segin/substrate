#!/bin/sh
# run_atomic8.sh: an 8-byte __atomic_load_n or __atomic_store_n on i386 is
# one access, never two torn ones.  atomic8_torn.c, built by cc for
# -march=i486 (a call of __atomic_load_8/__atomic_store_8, which
# march_atomic_lib.c answers), i586 and i686 (the x87's fildq/fistpq) at
# -O0 and -O2, races a storing thread against a loading one and must see
# no value half old and half new.
#
# Needs a host gcc that builds -m32 programs with threads; skipped, with a
# note, if not.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN=${CC_BIN:-"$ROOT/usr.bin/cc/cc"}
DIR="$ROOT/tests/usr.bin/cc"
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_atomic8.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

if ! printf 'int main(void) { return 0; }\n' | gcc -m32 -pthread -x c - -o "$tmp/probe" 2>/dev/null; then
	echo "run_atomic8: skip (gcc cannot build -m32 programs)"
	exit 0
fi
gcc -m32 -march=i686 -O2 -Wno-sync-nand -c -o "$tmp/lib.o" "$DIR/march_atomic_lib.c" || exit 1
fail=0
for arch in i486 i586 i686; do
	for o in O0 O2; do
		if ! "$CC_BIN" -m32 -march=$arch -$o -c -o "$tmp/t.o" "$DIR/atomic8_torn.c" >"$tmp/cc.log" 2>&1; then
			echo "FAIL -march=$arch -$o: cc rejected it"
			sed -n '1,5s/^/    /p' "$tmp/cc.log"
			fail=1
			continue
		fi
		gcc -m32 -no-pie -pthread -o "$tmp/t" "$tmp/t.o" "$tmp/lib.o" || exit 1
		if ! timeout 60 "$tmp/t" >"$tmp/out" 2>&1; then
			echo "FAIL -march=$arch -$o: $(cat "$tmp/out")"
			fail=1
		fi
	done
done
[ $fail -eq 0 ] && echo "run_atomic8: ok"
exit $fail
