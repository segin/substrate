#!/bin/sh
# run_stdatomic_gcc.sh: cc compiles gcc's own <stdatomic.h> and what it
# expands to does what gcc's build does.  stdatomic_gcc.c is built by gcc
# and by cc with the same flags -- -m64, and -m32 at -march=i386, i486 and
# i686, where the lock-free macros and the libcalls differ -- at -O0 and
# -O2 for cc, and the two programs must print the same.  -m32 programs
# link march_atomic_lib.c's gcc-built __atomic functions.
#
# A host whose gcc cannot build -m32 programs skips the -m32 runs.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN=${CC_BIN:-"$ROOT/usr.bin/cc/cc"}
DIR="$ROOT/tests/usr.bin/cc"
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_stdatomic.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -pthread -x c - -o "$tmp/probe" 2>/dev/null; then
	modes="64 32/i386 32/i486 32/i686"
	gcc -m32 -march=i686 -O2 -Wno-sync-nand -c -o "$tmp/lib.o" "$DIR/march_atomic_lib.c" || exit 1
else
	echo "run_stdatomic_gcc: gcc cannot build -m32 programs; skipping -m32"
fi
fail=0
for mode in $modes; do
	m=${mode%%/*}
	flags="-m$m -std=gnu17"
	lib=
	case $mode in */*) flags="$flags -march=${mode#*/}" lib=$tmp/lib.o ;; esac
	gcc $flags -O2 -c -o "$tmp/ref.o" "$DIR/stdatomic_gcc.c" || exit 1
	gcc -m$m -no-pie -pthread -o "$tmp/ref" "$tmp/ref.o" $lib || exit 1
	timeout 60 "$tmp/ref" >"$tmp/ref.out" 2>&1
	for o in O0 O2; do
		if ! "$CC_BIN" $flags -$o -c -o "$tmp/t.o" "$DIR/stdatomic_gcc.c" >"$tmp/cc.log" 2>&1; then
			echo "FAIL $flags -$o: cc rejected it"
			sed -n '1,5s/^/    /p' "$tmp/cc.log"
			fail=1
			continue
		fi
		gcc -m$m -no-pie -pthread -o "$tmp/t" "$tmp/t.o" $lib || exit 1
		timeout 60 "$tmp/t" >"$tmp/out" 2>&1
		if ! cmp -s "$tmp/ref.out" "$tmp/out"; then
			echo "FAIL $flags -$o: output differs from gcc's (< gcc, > cc)"
			diff "$tmp/ref.out" "$tmp/out" | sed -n '1,10s/^/    /p'
			fail=1
		fi
	done
done
[ $fail -eq 0 ] && echo "run_stdatomic_gcc: ok"
exit $fail
