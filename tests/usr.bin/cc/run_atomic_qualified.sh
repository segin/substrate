#!/bin/sh
# run_atomic_qualified.sh: operations on _Atomic objects are atomic.
# atomic_qualified.c, built by cc for -m64 and for -m32 at -march=i386,
# i486 and i686 (a call of __atomic_compare_exchange_N, lock cmpxchg) at
# -O0 and -O2, must print what gcc's build prints: the values ++, --,
# compound and plain assignment give, and the totals two threads leave
# updating the same objects, which a lost update shows in.  -m32 programs
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

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_atomic_q.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

gcc -O2 -pthread -o "$tmp/ref" "$DIR/atomic_qualified.c" || exit 1
"$tmp/ref" >"$tmp/ref.out" 2>&1
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -pthread -x c - -o "$tmp/probe" 2>/dev/null; then
	modes="64 32/i386 32/i486 32/i686"
	gcc -m32 -march=i686 -O2 -Wno-sync-nand -c -o "$tmp/lib.o" "$DIR/march_atomic_lib.c" || exit 1
else
	echo "run_atomic_qualified: gcc cannot build -m32 programs; skipping -m32"
fi
fail=0
for mode in $modes; do
	m=${mode%%/*}
	flags=-m$m
	lib=
	case $mode in */*) flags="$flags -march=${mode#*/}" lib=$tmp/lib.o ;; esac
	for o in O0 O2; do
		if ! "$CC_BIN" $flags -$o -c -o "$tmp/t.o" "$DIR/atomic_qualified.c" >"$tmp/cc.log" 2>&1; then
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
[ $fail -eq 0 ] && echo "run_atomic_qualified: ok"
exit $fail
